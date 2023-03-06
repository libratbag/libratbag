/*
 * Copyright © 2018 Thomas Hindoe Paaboel Andersen.
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

#include "config.h"
#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define LOGITECH_G600_NUM_PROFILES			3
#define LOGITECH_G600_NUM_BUTTONS			41 // 20 buttons + 1 color buffer + 20 G-shift
#define LOGITECH_G600_NUM_DPI				4
#define LOGITECH_G600_NUM_LED				1
#define LOGITECH_G600_DPI_MIN				200
#define LOGITECH_G600_DPI_MAX				8200

#define LOGITECH_G600_REPORT_ID_GET_ACTIVE		0xF0
#define LOGITECH_G600_REPORT_ID_SET_ACTIVE		0xF0
#define LOGITECH_G600_REPORT_ID_PROFILE_0		0xF3
#define LOGITECH_G600_REPORT_ID_PROFILE_1		0xF4
#define LOGITECH_G600_REPORT_ID_PROFILE_2		0xF5

#define LOGITECH_G600_REPORT_SIZE_PROFILE		154

#define LOGITECH_G600_LED_SOLID				0x00
#define LOGITECH_G600_LED_BREATHE			0x01
#define LOGITECH_G600_LED_CYCLE				0x02

struct logitech_g600_button {
	uint8_t code;
	uint8_t modifier;
	uint8_t key;
} __attribute__((packed));

struct logitech_g600_profile_report {
  uint8_t id;
  uint8_t led_red;
  uint8_t led_green;
  uint8_t led_blue;
  uint8_t led_effect;
  uint8_t led_duration;
  uint8_t unknown1[5];
  uint8_t frequency; /* frequency = 1000 / (value + 1) */
  uint8_t dpi_shift; /* value is a linear range between 200->0x04, 8200->0xa4, so value * 50, 0x00 is disabled */
  uint8_t dpi_default; /* between 1 and 4*/
  uint8_t dpi[4]; /* value is a linear range between 200->0x04, 8200->0xa4, so value * 50, 0x00 is disabled */
  uint8_t unknown2[13];
  struct logitech_g600_button buttons[20];
  uint8_t g_shift_color[3]; /* can't assign it in LGS, but the 3rd profile has one that shows the feature :) */
  struct logitech_g600_button g_shift_buttons[20];
} __attribute__((packed));

struct logitech_g600_active_profile_report {
	uint8_t id;
	uint8_t unknown1 :1;
	uint8_t resolution :2;
	uint8_t unknown2 :1;
	uint8_t profile :4;
	uint8_t unknown3;
	uint8_t unknown4;
} __attribute__((packed));

struct logitech_g600_profile_data {
	struct logitech_g600_profile_report report;
};

struct logitech_g600_data {
	struct logitech_g600_profile_data profile_data[LOGITECH_G600_NUM_PROFILES];
};

_Static_assert(sizeof(struct logitech_g600_profile_report) == LOGITECH_G600_REPORT_SIZE_PROFILE,
	       "Size of logitech_g600_profile_report is wrong");

struct logitech_g600_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static struct logitech_g600_button_mapping logitech_g600_button_mapping[] = {
	/* 0x00 is either key or unassigned. Must be handled separately  */
	{ 0x01, BUTTON_ACTION_BUTTON(1) },
	{ 0x02, BUTTON_ACTION_BUTTON(2) },
	{ 0x03, BUTTON_ACTION_BUTTON(3) },
	{ 0x04, BUTTON_ACTION_BUTTON(4) },
	{ 0x05, BUTTON_ACTION_BUTTON(5) },
	{ 0x11, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 0x12, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 0x13, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 0x14, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 0x15, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE) },
	{ 0x17, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE) },
};

static const struct ratbag_button_action*
logitech_g600_raw_to_button_action(uint8_t data)
{
	struct logitech_g600_button_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g600_button_mapping, mapping) {
		if (mapping->raw == data)
			return &mapping->action;
	}

	return NULL;
}

static uint8_t logitech_g600_modifier_to_raw(int modifier_flags)
{
	uint8_t modifiers = 0x00;

	if (modifier_flags & MODIFIER_LEFTCTRL)
		modifiers |= 0x01;
	if (modifier_flags & MODIFIER_LEFTSHIFT)
		modifiers |= 0x02;
	if (modifier_flags & MODIFIER_LEFTALT)
		modifiers |= 0x04;
	if (modifier_flags & MODIFIER_LEFTMETA)
		modifiers |= 0x08;
	if (modifier_flags & MODIFIER_RIGHTCTRL)
		modifiers |= 0x10;
	if (modifier_flags & MODIFIER_RIGHTSHIFT)
		modifiers |= 0x20;
	if (modifier_flags & MODIFIER_RIGHTALT)
		modifiers |= 0x40;
	if (modifier_flags & MODIFIER_RIGHTMETA)
		modifiers |= 0x80;

	return modifiers;
}

static int logitech_g600_raw_to_modifiers(uint8_t data)
{
	int modifiers = 0;

	if (data & 0x01)
		modifiers |= MODIFIER_LEFTCTRL;
	if (data & 0x02)
		modifiers |= MODIFIER_LEFTSHIFT;
	if (data & 0x04)
		modifiers |= MODIFIER_LEFTALT;
	if (data & 0x08)
		modifiers |= MODIFIER_LEFTMETA;
	if (data & 0x10)
		modifiers |= MODIFIER_RIGHTCTRL;
	if (data & 0x20)
		modifiers |= MODIFIER_RIGHTSHIFT;
	if (data & 0x40)
		modifiers |= MODIFIER_RIGHTALT;
	if (data & 0x80)
		modifiers |= MODIFIER_RIGHTMETA;

	return modifiers;
}

static uint8_t
logitech_g600_button_action_to_raw(const struct ratbag_button_action *action)
{
	struct logitech_g600_button_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g600_button_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->raw;
	}

	return 0;
}

static int
logitech_g600_get_active_profile_and_resolution(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct logitech_g600_active_profile_report buf;
	int ret;

	ret = ratbag_hidraw_raw_request(device, LOGITECH_G600_REPORT_ID_GET_ACTIVE,
					(uint8_t*)&buf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	if (ret < 0)
		return ret;

	if (ret != sizeof(buf))
		return -EIO;

	list_for_each(profile, &device->profiles, link) {
		struct ratbag_resolution *resolution;

		if (profile->index != buf.profile)
			continue;

		profile->is_active = true;
		ratbag_profile_for_each_resolution(profile, resolution) {
			resolution->is_active = resolution->index == buf.resolution;
		}
	}

	return 0;
}

static int
logitech_g600_set_current_resolution(struct ratbag_device *device, unsigned int index)
{
	uint8_t buf[] = {LOGITECH_G600_REPORT_ID_SET_ACTIVE, 0x40 | (index << 1), 0x00, 0x00};
	int ret;

	log_debug(device->ratbag, "Setting active resolution to %d\n", index);

	if (index >= LOGITECH_G600_NUM_DPI)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	return ret == sizeof(buf) ? 0 : ret;
}

static int
logitech_g600_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_profile *profile;
	uint8_t buf[] = {LOGITECH_G600_REPORT_ID_SET_ACTIVE, 0x80 | (index << 4), 0x00, 0x00};
	int ret, active_resolution = 0;

	if (index >= LOGITECH_G600_NUM_PROFILES)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (ret != sizeof(buf))
		return ret;

	/* Update the active resolution. After profile change the default is used. */
	list_for_each(profile, &device->profiles, link) {
		struct ratbag_resolution *resolution;

		if (profile->index != index)
			continue;

		ratbag_profile_for_each_resolution(profile, resolution) {
			resolution->is_active = resolution->is_default;

			if (resolution->is_active)
				active_resolution = resolution->index;
		}
	}

	ret = logitech_g600_set_current_resolution(device, active_resolution);
	if (ret < 0)
		return ret;

	return 0;
}

static void
logitech_g600_read_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action;
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct logitech_g600_data *drv_data = device->drv_data;
	struct logitech_g600_profile_data *pdata;
	struct logitech_g600_profile_report *profile_report;
	struct logitech_g600_button *button_report;

	pdata = &drv_data->profile_data[profile->index];
	profile_report = &pdata->report;
	button_report = &profile_report->buttons[button->index];

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	action = logitech_g600_raw_to_button_action(button_report->code);
	if (action) {
		ratbag_button_set_action(button, action);
	}
	else if (button_report->code == 0x00 && (button_report->modifier > 0x00 || button_report->key > 0x00))
	{
		unsigned int key, modifiers;
		int rc;

		key = ratbag_hidraw_get_keycode_from_keyboard_usage(device,
								    button_report->key);
		modifiers = logitech_g600_raw_to_modifiers(button_report->modifier);

		rc = ratbag_button_macro_new_from_keycode(button, key, modifiers);
		if (rc < 0) {
			log_error(device->ratbag,
				  "Error while reading button %d\n",
				  button->index);
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		}
	}
}

static void
logitech_g600_read_led(struct ratbag_led *led)
{
	struct ratbag_profile *profile = led->profile;
	struct ratbag_device *device = profile->device;
	struct logitech_g600_data *drv_data = device->drv_data;
	struct logitech_g600_profile_data *pdata;
	struct logitech_g600_profile_report *report;

	pdata = &drv_data->profile_data[profile->index];
	report = &pdata->report;

	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
	ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
	ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);

	switch (report->led_effect) {
		case LOGITECH_G600_LED_SOLID:
			led->mode = RATBAG_LED_ON;
			break;
		case LOGITECH_G600_LED_BREATHE:
			led->mode = RATBAG_LED_BREATHING;
			led->ms = report->led_duration * 1000;
			break;
		case LOGITECH_G600_LED_CYCLE:
			led->mode = RATBAG_LED_CYCLE;
			led->ms = report->led_duration * 1000;
			break;
	}
	led->color.red = report->led_red;
	led->color.green = report->led_green;
	led->color.blue = report->led_blue;
}

static void
logitech_g600_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct logitech_g600_data *drv_data = device->drv_data;
	struct logitech_g600_profile_data *pdata;
	struct logitech_g600_profile_report *report;
	struct ratbag_resolution *resolution;
	unsigned int report_rates[] = { 125, 142, 166, 200, 250, 333, 500, 1000 };
	uint8_t report_id;
	int rc;
	struct ratbag_button *button;
	struct ratbag_led *led;

	assert(profile->index < LOGITECH_G600_NUM_PROFILES);

	pdata = &drv_data->profile_data[profile->index];
	report = &pdata->report;

	switch (profile->index) {
	case 0: report_id = LOGITECH_G600_REPORT_ID_PROFILE_0; break;
	case 1: report_id = LOGITECH_G600_REPORT_ID_PROFILE_1; break;
	case 2: report_id = LOGITECH_G600_REPORT_ID_PROFILE_2; break;
	default:
		/* Should've been handled by the assertion above. */
		abort();
		break;
	}

	rc = ratbag_hidraw_raw_request(device, report_id,
					       (uint8_t*)report,
					       sizeof(*report),
					       HID_FEATURE_REPORT,
					       HID_REQ_GET_REPORT);

	if (rc < (int)sizeof(*report)) {
		log_error(device->ratbag,
			  "Error while requesting profile: %d\n", rc);
		return;
	}

	ratbag_profile_set_report_rate_list(profile, report_rates,
					    ARRAY_LENGTH(report_rates));
	profile->hz = 1000 / (report->frequency + 1);

	ratbag_profile_for_each_resolution(profile, resolution) {
		resolution->dpi_x = report->dpi[resolution->index] * 50;
		resolution->dpi_y = report->dpi[resolution->index] * 50;
		resolution->is_default = report->dpi_default - 1U == resolution->index;
		resolution->is_active = resolution->is_default;

		ratbag_resolution_set_dpi_list_from_range(resolution,
							  LOGITECH_G600_DPI_MIN,
							  LOGITECH_G600_DPI_MAX);
	}

	ratbag_profile_for_each_button(profile, button)
		logitech_g600_read_button(button);

	ratbag_profile_for_each_led(profile, led)
		logitech_g600_read_led(led);

	log_debug(device->ratbag, "Unknown data in profile %d\n", profile->index);
	log_buf_debug(device->ratbag,  "  profile->unknown1:   ",
		      report->unknown1, 5);
	log_buf_debug(device->ratbag,  "  profile->unknown2:   ",
		      report->unknown2, 13);
}

static int
logitech_g600_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, LOGITECH_G600_REPORT_ID_GET_ACTIVE);
}

static int
logitech_g600_probe(struct ratbag_device *device)
{
	int rc;
	struct logitech_g600_data *drv_data = NULL;
	struct ratbag_profile *profile;

	rc = ratbag_find_hidraw(device, logitech_g600_test_hidraw);
	if (rc)
		goto err;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	ratbag_device_init_profiles(device,
				    LOGITECH_G600_NUM_PROFILES,
				    LOGITECH_G600_NUM_DPI,
				    LOGITECH_G600_NUM_BUTTONS,
				    LOGITECH_G600_NUM_LED);

	ratbag_device_for_each_profile(device, profile)
		logitech_g600_read_profile(profile);

	rc = logitech_g600_get_active_profile_and_resolution(device);

	if (rc < 0) {
		log_error(device->ratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		rc = -ENODEV;
		goto err;
	}

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static int
logitech_g600_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct logitech_g600_data *drv_data = device->drv_data;
	struct logitech_g600_profile_data *pdata;
	struct logitech_g600_profile_report *report;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;

	uint8_t *buf;
	int rc, active_resolution = 0;

	pdata = &drv_data->profile_data[profile->index];
	report = &pdata->report;

	report->frequency = (1000 / profile->hz) - 1;

	ratbag_profile_for_each_resolution(profile, resolution) {
		report->dpi[resolution->index] = resolution->dpi_x / 50;

		if (resolution->is_default)
			report->dpi_default = resolution->index + 1;

		if (profile->is_active && resolution->is_active)
			active_resolution = resolution->index;
	}

	list_for_each(button, &profile->buttons, link) {
		struct ratbag_button_action *action = &button->action;
		struct logitech_g600_button *raw_button;

		raw_button = &report->buttons[button->index];

		raw_button->code = logitech_g600_button_action_to_raw(action);
		raw_button->modifier = 0x00;
		raw_button->key = 0x00;

		if (action->type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
			unsigned int key, modifiers;

			rc = ratbag_action_keycode_from_macro(action,
							      &key,
							      &modifiers);
			if (rc < 0) {
				log_error(device->ratbag,
					  "Error while writing macro for button %d\n",
					  button->index);
			}

			raw_button->key = ratbag_hidraw_get_keyboard_usage_from_keycode(
						device, key);
			raw_button->modifier = logitech_g600_modifier_to_raw(modifiers);
		}
	}

	list_for_each(led, &profile->leds, link) {
		report->led_red = led->color.red;
		report->led_green = led->color.green;
		report->led_blue = led->color.blue;

		switch (led->mode) {
			case RATBAG_LED_ON:
				report->led_effect = LOGITECH_G600_LED_SOLID;
				break;
			case RATBAG_LED_OFF:
				report->led_effect = LOGITECH_G600_LED_SOLID;
				report->led_red = 0x00;
				report->led_green = 0x00;
				report->led_blue = 0x00;
				break;
			case RATBAG_LED_BREATHING:
				report->led_effect = LOGITECH_G600_LED_BREATHE;
				report->led_duration = led->ms / 1000;
				break;
			case RATBAG_LED_CYCLE:
				report->led_effect = LOGITECH_G600_LED_CYCLE;
				report->led_duration = led->ms / 1000;
				break;
		}

		if (report->led_duration > 0x0f)
			report->led_duration = 0x0f;
	}

	// For now the default will copy over the main color into the g-shift color
	// future update may add support to set this via cli command
	report->g_shift_color[0] = report->led_red;
	report->g_shift_color[1] = report->led_green;
	report->g_shift_color[2] = report->led_blue;

	buf = (uint8_t*)report;

	rc = ratbag_hidraw_raw_request(device, report->id,
				       buf,
				       sizeof(*report),
				       HID_FEATURE_REPORT,
				       HID_REQ_SET_REPORT);

	if (rc < (int)sizeof(*report)) {
		log_error(device->ratbag,
			  "Error while writing profile: %d\n", rc);
		return rc;
	}

	if (profile->is_active) {
		rc = logitech_g600_set_current_resolution(device, active_resolution);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
logitech_g600_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc = 0;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		log_debug(device->ratbag,
			  "Profile %d changed, rewriting\n", profile->index);

		rc = logitech_g600_write_profile(profile);
		if (rc)
			return rc;
	}

	return 0;
}

static void
logitech_g600_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver logitech_g600_driver = {
	.name = "Logitech G600",
	.id = "logitech_g600",
	.probe = logitech_g600_probe,
	.remove = logitech_g600_remove,
	.commit = logitech_g600_commit,
	.set_active_profile = logitech_g600_set_active_profile,
};
