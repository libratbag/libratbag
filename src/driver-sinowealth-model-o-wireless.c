/*
 * Copyright © 2026 TitanHZZ
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Based on:
 *   https://github.com/korkje/mow
 */

/*
 * This is not a full implementation as some things are not supported by libratbag:
 * - more RGB effects
 * - sleep time
 * - DPI colors
 * - lift off distance
 * - dedicated scroll inversion toggle (can be done by swapping the wheel up and wheel down actions)
 * - separate wired/wireless brightness
 * - handling specific mouse events such as:
 *   - events from the DPI button when pressed
 * 
 * And some things are simply not reversed engineered yet:
 * - full macro support
 * - shortcuts
 */

#include "libratbag-private.h"
#include "shared-macro.h"

#define SINOWEALTH_MODEL_O_WIRELESS_REPORT_ID 0x02
#define SINOWEALTH_BUFF_SIZE 65
#define SINOWEALTH_SET_AND_CHECK_BUFF_SIZE 55

#define SINOWEALT_NUM_PROFILES 3
#define SINOWEALT_NUM_RESOLUTIONS 4
#define SINOWEALT_NUM_BUTTONS 8
#define SINOWEALT_NUM_LEDS 1

#define DPI_MIN 100
#define DPI_MAX 19000
#define DPI_DEFAULT_IDX 2 // default dpi index in the DPILIST array below

#define DEBOUNCE_DEFAULT_IDX 4

static const unsigned int REPORT_RATES[] = { 125, 250, 500, 1000 };
static const unsigned int DPILIST[] = { 400, 800, 1600, 3200 };

/* The mouse actually supports debouce times from 1 to 16 but libratbag does not support more than 8 values. */
static const unsigned int DEBOUNCE_TIMES[] = { 2, 4, 6, 8, 10, 12, 14, 16 };

/* The mouse supports a lot more effects but those are not available in libratbag. */
static const unsigned int LED_CAPABILITIES[] = { RATBAG_LED_OFF, RATBAG_LED_ON, RATBAG_LED_BREATHING, RATBAG_LED_CYCLE };

struct sinowealth_button_mapping {
	uint8_t id;
	struct ratbag_button_action action;
};

static const struct sinowealth_button_mapping sinowealth_button_mapping[] = {
	{ 0x01, BUTTON_ACTION_BUTTON(1) }, // left click
	{ 0x02, BUTTON_ACTION_BUTTON(2) }, // right click
	{ 0x03, BUTTON_ACTION_BUTTON(3) }, // middle/wheel click
	{ 0x04, BUTTON_ACTION_BUTTON(4) }, // side button 1 (back)
	{ 0x05, BUTTON_ACTION_BUTTON(5) }, // side button 2 (forward)
	{ 0x14, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) }, // DPI button
	{ 0x10, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },            // scroll up
	{ 0x11, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },          // scroll down
};

#define SINOWEALTH_BUTTON_MAPPING_SIZE (sizeof(sinowealth_button_mapping) / sizeof(sinowealth_button_mapping[0]))

static const uint8_t sinowealth_button_profiles_simple[8][3] = {
	{ 0x01, 0x01, 0x01 }, // left click
	{ 0x01, 0x01, 0x02 }, // right click
	{ 0x01, 0x01, 0x03 }, // middle/wheel click
	{ 0x01, 0x01, 0x04 }, // side button 1 (back)
	{ 0x01, 0x01, 0x05 }, // side button 2 (forward)
	{ 0x07, 0x01, 0x06 }, // DPI button
	{ 0x01, 0x01, 0x10 }, // scroll up
	{ 0x01, 0x01, 0x11 }, // scroll down
};

struct sinowealth_button_profile_special {
    enum ratbag_button_action_special action;
    uint8_t profile[3];
};

static const struct sinowealth_button_profile_special sinowealth_button_profiles_special[] = {
    { RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP,              { 0x01, 0x01, 0x10 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN,            { 0x01, 0x01, 0x11 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,   { 0x07, 0x01, 0x06 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN, { 0x07, 0x01, 0x07 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,         { 0x07, 0x01, 0x01 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,       { 0x07, 0x01, 0x02 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,      { 0x08, 0x01, 0x04 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_DOWN,    { 0x08, 0x01, 0x03 } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL,         { 0x0C, 0x01, 0x01 } },
};

static const struct ratbag_button_action*
sinowealth_btn_action_from_btn_idx(uint8_t index)
{
	if (index >= SINOWEALTH_BUTTON_MAPPING_SIZE)
		return NULL;

	return &sinowealth_button_mapping[index].action;
}

static int
sinowealth_btn_id_from_btn_idx(uint8_t index)
{
	if (index >= SINOWEALTH_BUTTON_MAPPING_SIZE)
		return -EINVAL;

	return sinowealth_button_mapping[index].id;
}

static int
sinowealth_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, SINOWEALTH_MODEL_O_WIRELESS_REPORT_ID);
}

static int
sinowealth_get_firmware(struct ratbag_device *device, bool wired)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};
	char fw_version[32] = {0};

	if (wired)
		buff[3] = 0x02;

	buff[4] = 0x03;
	buff[6] = 0x81;

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to get firmware version: %d\n", error);
		return error;
	}

	msleep(50);
	error = ratbag_hidraw_get_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to get firmware version: %d\n", error);
		return error;
	}

	snprintf(fw_version, sizeof(fw_version), "%d.%d.%d.%d", buff[7], buff[8], buff[9], buff[10]);
	ratbag_device_set_firmware_version(device, fw_version);
	log_info(device->ratbag,"Firmware: %s\n", fw_version);
	return 0;
}

static int
sinowealth_probe(struct ratbag_device *device)
{
	int error;
	bool wired;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	struct ratbag_button *button;

	error = ratbag_find_hidraw(device, sinowealth_test_hidraw);
	if (error)
		return error;

	wired = ratbag_device_get_product_id(device) == 0x2011;
	log_msg(device->ratbag, RATBAG_LOG_PRIORITY_DEBUG, "is wired: %d\n", wired);

	error = sinowealth_get_firmware(device, wired);
	if (error)
		return error;

	ratbag_device_init_profiles(device,
		SINOWEALT_NUM_PROFILES,
		SINOWEALT_NUM_RESOLUTIONS,
		SINOWEALT_NUM_BUTTONS,
		SINOWEALT_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = profile->index == 0;

		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
		ratbag_profile_set_report_rate_list(profile, REPORT_RATES, ARRAY_LENGTH(REPORT_RATES));
		ratbag_profile_set_debounce_list(profile, DEBOUNCE_TIMES, ARRAY_LENGTH(DEBOUNCE_TIMES));
		profile->debounce = DEBOUNCE_TIMES[DEBOUNCE_DEFAULT_IDX];
		profile->hz = REPORT_RATES[ARRAY_LENGTH(REPORT_RATES) - 1];

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list_from_range(resolution, DPI_MIN, DPI_MAX);
			ratbag_resolution_set_resolution(resolution, DPILIST[resolution->index], DPILIST[resolution->index]);
			resolution->is_active = resolution->index == DPI_DEFAULT_IDX;
			resolution->is_default = resolution->index == DPI_DEFAULT_IDX;
		}

		const enum ratbag_led_mode *capability;
		ratbag_profile_for_each_led(profile, led) {
			ARRAY_FOR_EACH(LED_CAPABILITIES, capability)
				ratbag_led_set_mode_capability(led, *capability);

			led->color.red = led->color.green = led->color.blue = 0xFF;
			led->brightness = 255;
			led->ms = 5000;
			led->mode = RATBAG_LED_OFF;
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
		}

		const struct ratbag_button_action *action;
		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

			action = sinowealth_btn_action_from_btn_idx(button->index);
			if (action) {
				ratbag_button_set_action(button, action);
			}
		}
	}

	return 0;
}

static int
sinowealth_select_dpi(struct ratbag_device *device, unsigned int profile, unsigned int id)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	if (profile >= SINOWEALT_NUM_PROFILES || id >= SINOWEALT_NUM_RESOLUTIONS)
		return -EINVAL;

	buff[3] = 0x02;
    buff[4] = 0x02;
    buff[5] = 0x01;
    buff[6] = 0x02;

	buff[7] = profile + 1;
    buff[8] = id + 1;

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to select a DPI: %d\n", error);
		return error;
	}

	return 0;
}

static int
sinowealth_set_dpis(struct ratbag_device *device, unsigned int profile, unsigned int *dpis)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	if (profile >= SINOWEALT_NUM_PROFILES)
		return -EINVAL;

	buff[3] = 0x02;
    buff[4] = 0x12;
    buff[5] = 0x01;
    buff[6] = 0x01;

	buff[7] = profile + 1;
    buff[8] = SINOWEALT_NUM_RESOLUTIONS;

	for (size_t i = 0; i < SINOWEALT_NUM_RESOLUTIONS; i++) {
		uint8_t first = (dpis[i] >> 0x8) & 0xFF;
		uint8_t second = dpis[i] & 0xFF;

        buff[9 + (4 * i) + 0] = first;
        buff[9 + (4 * i) + 1] = second;
        buff[9 + (4 * i) + 2] = first;
        buff[9 + (4 * i) + 3] = second;
	}

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set DPIs: %d\n", error);
		return error;
	}

	return 0;
}

static int
sinowealth_set_report_rate(struct ratbag_device *device, unsigned int hz)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	buff[3] = 0x02;
	buff[4] = 0x01;
	buff[5] = 0x01;
	buff[7] = 1000 / hz;

	/* This is a global setting so apply it to all profiles. */
	struct ratbag_profile *profile;
	list_for_each(profile, &device->profiles, link) {
		profile->hz = hz;
	}

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set the report rate: %d\n", error);
		return error;
	}

	return 0;
}

static int
sinowealth_set_debouce(struct ratbag_device *device, unsigned int profile, unsigned int debounce)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	if (profile >= SINOWEALT_NUM_PROFILES)
		return -EINVAL;

	buff[3] = 0x02;
	buff[4] = 0x01;
	buff[6] = 0x08;

	buff[7] = profile + 1;
	buff[8] = debounce;

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set the debouce: %d\n", error);
		return error;
	}

	return 0;
}

static int
sinowealth_set_led_brightness(struct ratbag_device *device, unsigned int brightness)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	/* Set wired brightness. */
	buff[3] = 0x02;
	buff[4] = 0x02;
	buff[5] = 0x02;
	buff[6] = 0x02;
	buff[7] = 0x01;
	buff[8] = brightness;

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set wired led brightness: %d\n", error);
		return error;
	}

	msleep(30);

	/* Set wireless brightness. */
	buff[7] = 0x00;
	buff[8] = brightness;

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set wireless led brightness: %d\n", error);
		return error;
	}

	return 0;
}

static int
sinowealth_set_led(struct ratbag_device *device, unsigned int profile, struct ratbag_led *led)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	if (profile >= SINOWEALT_NUM_PROFILES)
		return -EINVAL;

	buff[3] = 0x02;
	buff[5] = 0x02;
	buff[7] = profile + 1;
	buff[8] = 0xFF;

	switch (led->mode) {
		case RATBAG_LED_OFF:
			buff[4] = 0x05;
			buff[9] = 0x00;
			break;

		case RATBAG_LED_ON:
			buff[4] = 0x08;
			buff[9] = 0x04;

			buff[12 + 0] = led->color.red;
			buff[12 + 1] = led->color.green;
			buff[12 + 2] = led->color.blue;
			break;

		case RATBAG_LED_CYCLE:
			buff[4] = 0x05;
			buff[9] = 0x02;
			buff[11] = (105 - ((10000 - led->ms) / 100)) / 5;
			buff[12] = 0xFF;
			break;

		case RATBAG_LED_BREATHING:
			buff[4] = 0x08;
			buff[9] = 0x05;
			buff[11] = (105 - ((10000 - led->ms) / 100)) / 5;

			buff[12 + 0] = led->color.red;
			buff[12 + 1] = led->color.green;
			buff[12 + 2] = led->color.blue;
			break;

		default:
			log_error(device->ratbag, "Error while trying to set the RGB mode\n");
			return -EINVAL;
	}

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set the RGB mode: %d\n", error);
		return error;
	}

	error = sinowealth_set_led_brightness(device, led->brightness);
	if (error < 0)
		return error;

	return 0;
}

static int
sinowealth_set_and_check(struct ratbag_device *device, uint8_t _buff[SINOWEALTH_BUFF_SIZE], uint8_t depth, bool waiting)
{
	int error;

	if (depth >= 3)
		return -1;

	if (waiting) {
		msleep(100);
		sinowealth_set_and_check(device, _buff, depth + 1, true);
	} else {
		msleep(100);
		uint8_t buff[SINOWEALTH_SET_AND_CHECK_BUFF_SIZE] = {0};
		error = ratbag_hidraw_get_feature_report(device, 0, buff, SINOWEALTH_SET_AND_CHECK_BUFF_SIZE);
		if (error < 0) {
			log_error(device->ratbag, "Error while trying to set key binding: %d\n", error);
			return error;
		}

		msleep(40);
		if (buff[0] == 0xA2) {
			error = ratbag_hidraw_set_feature_report(device, 0, _buff, SINOWEALTH_BUFF_SIZE);
			if (error < 0) {
				log_error(device->ratbag, "Error while trying to set key binding: %d\n", error);
				return error;
			}

			sinowealth_set_and_check(device, _buff, depth + 1, false);
		} else if (buff[0] == 0xA0) {
			sinowealth_set_and_check(device, _buff, depth + 1, false);
		} else if (buff[0] == 0xA4) {
			sinowealth_set_and_check(device, _buff, depth + 1, true);
		} else {
			return 0;
		}
	}

	return 0;
}

static int
sinowealth_set_button_simple(struct ratbag_button *button, uint8_t buff[SINOWEALTH_BUFF_SIZE])
{
	if (button->action.action.button > 8)
		return -EINVAL;

	buff[10] = sinowealth_button_profiles_simple[button->action.action.button - 1][0];
	buff[11] = sinowealth_button_profiles_simple[button->action.action.button - 1][1];
	buff[12] = sinowealth_button_profiles_simple[button->action.action.button - 1][2];

	return 0;
}

static int
sinowealth_set_button_special(struct ratbag_button *button, uint8_t buff[SINOWEALTH_BUFF_SIZE])
{
	const struct sinowealth_button_profile_special* special_profile;
	ARRAY_FOR_EACH(sinowealth_button_profiles_special, special_profile) {
		if (button->action.action.special == special_profile->action) {
			buff[10] = special_profile->profile[0];
			buff[11] = special_profile->profile[1];
			buff[12] = special_profile->profile[2];

			return 0;
		}
	}

	return -EINVAL;
}

static int
sinowealth_set_button_key_payload(struct ratbag_device *device, uint8_t buff[SINOWEALTH_BUFF_SIZE], unsigned int modifiers, unsigned int key)
{
	uint8_t code;

	code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
	if (code == 0) {
		code = ratbag_hidraw_get_consumer_usage_from_keycode(device, key);
		if (code == 0)
			return -EINVAL;
	}

	buff[10] = 0x04;
	buff[11] = 0x02;

	if (modifiers & MODIFIER_LEFTCTRL)
		buff[12] = 0x01;
	else if (modifiers & MODIFIER_RIGHTSHIFT)
		buff[12] = 0x02;
	else if (modifiers & MODIFIER_RIGHTALT)
		buff[12] = 0x04;
	else if (modifiers & MODIFIER_LEFTMETA)
		buff[12] = 0x08;
	else
		buff[12] = 0x00;

	buff[13] = code;
	return 0;
}

static int
sinowealth_set_button_key(struct ratbag_device *device, struct ratbag_button *button, uint8_t buff[SINOWEALTH_BUFF_SIZE])
{
	return sinowealth_set_button_key_payload(device, buff, 0, button->action.action.key);
}

static int
sinowealth_set_button_macro(struct ratbag_device *device, struct ratbag_button *button, uint8_t buff[SINOWEALTH_BUFF_SIZE])
{
	int error;
	unsigned int modifiers, key;

	error = ratbag_action_keycode_from_macro(&button->action, &key, &modifiers);
	if (error < 0)
		return -EINVAL;

	return sinowealth_set_button_key_payload(device, buff, modifiers, key);
}

static int
sinowealth_set_button(struct ratbag_device *device, unsigned int profile, struct ratbag_button *button)
{
	int btn_id;
	int error = 0;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	if (profile >= SINOWEALT_NUM_PROFILES)
		return -EINVAL;

	btn_id = sinowealth_btn_id_from_btn_idx(button->index);
	if (btn_id < 0) {
		log_error(device->ratbag, "Error while trying to set key binding: %d\n", btn_id);
		return btn_id;
	}

	buff[3] = 0x02;
	buff[4] = 0x09;
	buff[5] = 0x03;
	buff[7] = profile + 1;
	buff[8] = btn_id;

	switch (button->action.type) {
		case RATBAG_BUTTON_ACTION_TYPE_NONE:
			/* Doing nothing else will disable the button. */
			break;

		case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
			error = sinowealth_set_button_simple(button, buff);
			break;

		case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
			error = sinowealth_set_button_special(button, buff);
			break;

		case RATBAG_BUTTON_ACTION_TYPE_KEY:
			error = sinowealth_set_button_key(device, button, buff);
			break;

		case RATBAG_BUTTON_ACTION_TYPE_MACRO:
			/* This is more of a "keys with modifiers" situation than full macros. */
			error = sinowealth_set_button_macro(device, button, buff);
			break;

		default:
			return -EINVAL;
	}

	if (error) {
		log_error(device->ratbag, "Error while trying to set key binding: %d\n", error);
		return error;
	}

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set key binding: %d\n", error);
		return error;
	}

	error = sinowealth_set_and_check(device, buff, 0, false);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set key binding: %d\n", error);
		return error;
	}

	return 0;
}

static int
sinowealth_write_profile(struct ratbag_device *device, struct ratbag_profile *profile)
{
	int error;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	struct ratbag_button *button;
	unsigned int dpis[SINOWEALT_NUM_RESOLUTIONS] = { DPI_MIN, DPI_MIN, DPI_MIN, DPI_MIN };
	bool resolutions_dirty = false;
	unsigned int resolutions_id = 0;

	ratbag_profile_for_each_resolution(profile, resolution) {
		resolutions_dirty |= resolution->dirty;
		dpis[resolution->index] = resolution->dpi_x;

		if (resolution->is_active)
			resolutions_id = resolution->index;
	};

	if (resolutions_dirty) {
		error = sinowealth_set_dpis(device, profile->index, dpis);
		if (error)
			return error;

		error = sinowealth_select_dpi(device, profile->index, resolutions_id);
		if (error)
			return error;
	}

	if (profile->rate_dirty) {
		error = sinowealth_set_report_rate(device, profile->hz);
		if (error)
			return error;
	}

	if (profile->debounce_dirty) {
		error = sinowealth_set_debouce(device, profile->index, profile->debounce);
		if (error)
			return error;
	}

	ratbag_profile_for_each_led(profile, led) {
		if (!led->dirty)
			continue;

		error = sinowealth_set_led(device, profile->index, led);
		if (error)
			return error;
	};

	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;

		error = sinowealth_set_button(device, profile->index, button);
		if (error)
			return error;
	};

	return 0;
}

static int
sinowealth_commit(struct ratbag_device *device)
{
	int error;
	struct ratbag_profile *profile;
	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		error = sinowealth_write_profile(device, profile);
		if (error)
			return error;
	}

	return 0;
}

static int
sinowealth_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	int error;
	uint8_t buff[SINOWEALTH_BUFF_SIZE] = {0};

	if (index >= SINOWEALT_NUM_PROFILES)
		return -EINVAL;

	buff[3] = 0x02;
	buff[4] = 0x01;
	buff[6] = 0x05;
	buff[7] = index + 1;

	error = ratbag_hidraw_set_feature_report(device, 0, buff, SINOWEALTH_BUFF_SIZE);
	if (error < 0) {
		log_error(device->ratbag, "Error while trying to set the active profile: %d\n", error);
		return error;
	}

	return 0;
}

static void
sinowealth_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

struct ratbag_driver sinowealth_model_o_wireless_driver = {
	.name = "Sinowealth Model O Wireless",
	.id = "sinowealth_model_o_wireless",
	.probe = sinowealth_probe,
	.remove = sinowealth_remove,
	.commit = sinowealth_commit,
	.set_active_profile = sinowealth_set_active_profile,
};
