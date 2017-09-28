/*
 * Copyright © 2016 Thomas Hindoe Paaboel Andersen.
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

#define LOGITECH_G300_PROFILE_MAX			2
#define LOGITECH_G300_BUTTON_MAX			8
#define LOGITECH_G300_NUM_DPI				4
#define LOGITECH_G300_NUM_LED				1
#define LOGITECH_G300_DPI_MIN				250
#define LOGITECH_G300_DPI_MAX				2500

#define LOGITECH_G300_REPORT_ID_GET_ACTIVE		0xF0
#define LOGITECH_G300_REPORT_ID_SET_ACTIVE		0xF0
#define LOGITECH_G300_REPORT_ID_GET_ACTIVE_LED		0xF1
#define LOGITECH_G300_REPORT_ID_PROFILE_0		0xF3
#define LOGITECH_G300_REPORT_ID_PROFILE_1		0xF4
#define LOGITECH_G300_REPORT_ID_PROFILE_2		0xF5

#define LOGITECH_G300_REPORT_SIZE_ACTIVE		4
#define LOGITECH_G300_REPORT_SIZE_PROFILE		35

struct logitech_g300_resolution {
	uint8_t dpi :7; /* Range 1-10. dpi = 250*value */
	uint8_t is_default :1;
} __attribute__((packed));

struct logitech_g300_button {
	uint8_t code;
	uint8_t modifier;
	uint8_t key;
} __attribute__((packed));

struct logitech_g300_profile_report {
	uint8_t id; /* F3, F4, F5 */
	uint8_t led_red :1;
	uint8_t led_green :1;
	uint8_t led_blue :1;
	uint8_t unknown1 :5;
	uint8_t frequency; /* 00=1000, 01=125, 02=250, 03=500 */
	struct logitech_g300_resolution dpi_levels[LOGITECH_G300_NUM_DPI];
	uint8_t unknown2; /* dpi index for shift, but something else too */
	struct logitech_g300_button buttons[LOGITECH_G300_BUTTON_MAX + 1];
} __attribute__((packed));

struct logitech_g300_profile_data {
	struct logitech_g300_profile_report report;
};

struct logitech_g300_data {
	struct logitech_g300_profile_data profile_data[LOGITECH_G300_PROFILE_MAX + 1];
};

_Static_assert(sizeof(struct logitech_g300_profile_report) == LOGITECH_G300_REPORT_SIZE_PROFILE,
	       "Size of logitech_g300_profile_report is wrong");

struct logitech_g300_button_type_mapping {
	uint8_t raw;
	enum ratbag_button_type type;
};

static const struct logitech_g300_button_type_mapping logitech_g300_button_type_mapping[] = {
	{ 0, RATBAG_BUTTON_TYPE_LEFT },
	{ 1, RATBAG_BUTTON_TYPE_RIGHT },
	{ 2, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 3, RATBAG_BUTTON_TYPE_THUMB },
	{ 4, RATBAG_BUTTON_TYPE_THUMB2 },
	{ 5, RATBAG_BUTTON_TYPE_PINKIE },
	{ 6, RATBAG_BUTTON_TYPE_PINKIE2 },
	{ 7, RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP },
	{ 8, RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP },
};

static enum ratbag_button_type
logitech_g300_raw_to_button_type(uint8_t data)
{
	const struct logitech_g300_button_type_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g300_button_type_mapping, mapping) {
		if (mapping->raw == data)
			return mapping->type;
	}

	return RATBAG_BUTTON_TYPE_UNKNOWN;
}

struct logitech_g300_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static struct logitech_g300_button_mapping logitech_g300_button_mapping[] = {
	/* 0x00 is either key or unassigned. Must be handled separatly  */
	{ 0x01, BUTTON_ACTION_BUTTON(1) },
	{ 0x02, BUTTON_ACTION_BUTTON(2) },
	{ 0x03, BUTTON_ACTION_BUTTON(3) },
	{ 0x04, BUTTON_ACTION_BUTTON(4) },
	{ 0x05, BUTTON_ACTION_BUTTON(5) },
	{ 0x06, BUTTON_ACTION_BUTTON(6) },
	{ 0x07, BUTTON_ACTION_BUTTON(7) },
	{ 0x08, BUTTON_ACTION_BUTTON(8) },
	{ 0x09, BUTTON_ACTION_BUTTON(9) },
	{ 0x0A, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 0x0B, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 0x0C, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 0x0D, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 0x0E, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE) },
	{ 0x0F, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT) },
};

static const struct ratbag_button_action*
logitech_g300_raw_to_button_action(uint8_t data)
{
	struct logitech_g300_button_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g300_button_mapping, mapping) {
		if (mapping->raw == data)
			return &mapping->action;
	}

	return NULL;
}

static uint8_t
logitech_g300_button_action_to_raw(const struct ratbag_button_action *action)
{
	struct logitech_g300_button_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g300_button_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->raw;
	}

	return 0;
}

struct logitech_g300_frequency_mapping {
	uint8_t raw;
	unsigned int frequency;
};

static struct logitech_g300_frequency_mapping logitech_g300_frequency_mapping[] = {
	{ 0, 1000 },
	{ 1, 125 },
	{ 2, 250 },
	{ 3, 500 },
};

static unsigned int
logitech_g300_raw_to_frequency(uint8_t data)
{
	struct logitech_g300_frequency_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g300_frequency_mapping, mapping) {
		if (mapping->raw == data)
			return mapping->frequency;
	}

	return 0;
}

static uint8_t
logitech_g300_frequency_to_raw(unsigned int frequency)
{
	struct logitech_g300_frequency_mapping *mapping;

	ARRAY_FOR_EACH(logitech_g300_frequency_mapping, mapping) {
		if (mapping->frequency == frequency)
			return mapping->raw;
	}

	return 0;
}

struct logitech_g300_F0_report {
	uint8_t id;
	uint8_t unknown1 :1;
	uint8_t resolution :3;
	uint8_t profile :4;
	uint8_t unknown2;
	uint8_t unknown3;
} __attribute__((packed));


static int
logitech_g300_get_active_profile_and_resolution(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct logitech_g300_F0_report buf;
	unsigned int i;
	int ret;

	ret = ratbag_hidraw_raw_request(device, 0xF0, (uint8_t*)&buf,
			sizeof(buf), HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	if (ret < 0)
		return ret;

	if (ret != sizeof(buf))
		return -EIO;

	list_for_each(profile, &device->profiles, link) {
		if (profile->index != buf.profile)
			continue;

		profile->is_active = true;
		for (i = 0; i < profile->resolution.num_modes; i++) {
			profile->resolution.modes[i].is_active = i == buf.resolution;
		}
	}

	return 0;
}

static int
logitech_g300_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_profile *profile;
	uint8_t buf[] = {LOGITECH_G300_REPORT_ID_SET_ACTIVE, 0x80 | (index << 4), 0x00, 0x00};
	unsigned int i;
	int ret;

	if (index > LOGITECH_G300_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (ret != sizeof(buf))
		return ret;

	/* Update the active resolution. After profile change the default is used. */
	list_for_each(profile, &device->profiles, link) {
		if (profile->index != index)
			continue;

		for (i = 0; i < profile->resolution.num_modes; i++) {
			profile->resolution.modes[i].is_active = profile->resolution.modes[i].is_default;
		}
	}

	return 0;
}

static int
logitech_g300_set_current_resolution(struct ratbag_device *device, unsigned int index)
{
	uint8_t buf[] = {LOGITECH_G300_REPORT_ID_SET_ACTIVE, 0x40 | (index << 1), 0x00, 0x00};
	int ret;

	if (index >= LOGITECH_G300_NUM_DPI)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	return ret == sizeof(buf) ? 0 : ret;
}

static void
logitech_g300_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct logitech_g300_data *drv_data = device->drv_data;
	struct logitech_g300_profile_data *pdata;
	struct logitech_g300_profile_report *report;
	struct ratbag_resolution *resolution;
	unsigned int i, hz;
	uint8_t report_id;
	int rc;

	assert(index <= LOGITECH_G300_PROFILE_MAX);

	pdata = &drv_data->profile_data[index];
	report = &pdata->report;

	switch (index) {
	case 0: report_id = LOGITECH_G300_REPORT_ID_PROFILE_0; break;
	case 1: report_id = LOGITECH_G300_REPORT_ID_PROFILE_1; break;
	case 2: report_id = LOGITECH_G300_REPORT_ID_PROFILE_2; break;
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

	hz = logitech_g300_raw_to_frequency(report->frequency);

	for (i = 0; i < profile->resolution.num_modes; i++) {
		struct logitech_g300_resolution *res =
			&report->dpi_levels[i];

		resolution = &profile->resolution.modes[i];
		resolution->dpi_x = res->dpi * 250;
		resolution->dpi_y = res->dpi * 250;
		resolution->hz = hz;
		resolution->is_default = res->is_default;
		resolution->is_active = res->is_default;

		ratbag_resolution_set_range(resolution,
					    LOGITECH_G300_DPI_MIN,
					    LOGITECH_G300_DPI_MAX);
	}
}

static void
logitech_g300_read_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action;
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct logitech_g300_data *drv_data = device->drv_data;
	struct logitech_g300_profile_data *pdata;
	struct logitech_g300_profile_report *profile_report;
	struct logitech_g300_button *button_report;

	pdata = &drv_data->profile_data[profile->index];
	profile_report = &pdata->report;
	button_report = &profile_report->buttons[button->index];

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

	button->type = logitech_g300_raw_to_button_type(button->index);

	action = logitech_g300_raw_to_button_action(button_report->code);
	if (action) {
		button->action = *action;
	}
	else if (button_report->code == 0x00 && (button_report->modifier > 0x00 || button_report->key > 0x00))
	{
		struct ratbag_button_action *key_action = &button->action;

		key_action->type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		key_action->action.key.key = ratbag_hidraw_get_keycode_from_keyboard_usage(
			device, button_report->key);
	}
}

static void
logitech_g300_read_led(struct ratbag_led *led)
{
	struct ratbag_profile *profile = led->profile;
	struct ratbag_device *device = profile->device;
	struct logitech_g300_data *drv_data = device->drv_data;
	struct logitech_g300_profile_data *pdata;
	struct logitech_g300_profile_report *profile_report;

	pdata = &drv_data->profile_data[profile->index];
	profile_report = &pdata->report;

	led->type = RATBAG_LED_TYPE_SIDE;
	led->mode = RATBAG_LED_ON;
	led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;
	led->color.red = profile_report->led_red * 255;
	led->color.green = profile_report->led_green * 255;
	led->color.blue = profile_report->led_blue * 255;
}

static int
logitech_g300_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, LOGITECH_G300_REPORT_ID_GET_ACTIVE);
}

static int
logitech_g300_probe(struct ratbag_device *device)
{
	int rc;
	struct logitech_g300_data *drv_data = NULL;
	int active_idx;

	rc = ratbag_find_hidraw(device, logitech_g300_test_hidraw);
	if (rc)
		goto err;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	/* profiles are 0-indexed */
	ratbag_device_init_profiles(device,
				    LOGITECH_G300_PROFILE_MAX + 1,
				    LOGITECH_G300_NUM_DPI,
				    LOGITECH_G300_BUTTON_MAX + 1,
				    LOGITECH_G300_NUM_LED);

	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_RESOLUTION);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_PROFILE);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_KEY);

	active_idx = logitech_g300_get_active_profile_and_resolution(device);

	if (active_idx < 0) {
		log_error(device->ratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-active_idx),
			  active_idx);
		rc = -ENODEV;
		goto err;
	}

	log_raw(device->ratbag,
		"'%s' is in profile %d\n",
		ratbag_device_get_name(device),
		active_idx);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static int
logitech_g300_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct logitech_g300_data *drv_data = device->drv_data;
	struct logitech_g300_profile_data *pdata;
	struct logitech_g300_profile_report *report;
	struct ratbag_button *button;
	struct ratbag_led *led;

	uint8_t *buf;
	unsigned int hz, i;
	int rc;

	pdata = &drv_data->profile_data[profile->index];
	report = &pdata->report;

	/* The same hz is used for all resolutions */
	hz = profile->resolution.modes[0].hz;
	report->frequency = logitech_g300_frequency_to_raw(hz);

	for (i = 0; i < profile->resolution.num_modes; i++) {
		struct ratbag_resolution *resolution = &profile->resolution.modes[i];
		struct logitech_g300_resolution *res = &report->dpi_levels[i];

		res->dpi = resolution->dpi_x / 250;
		res->is_default = resolution->is_default;

		if (profile->is_active && resolution->is_active)
			logitech_g300_set_current_resolution(device, i);
	}

	list_for_each(button, &profile->buttons, link) {
		struct ratbag_button_action *action = &button->action;
		struct logitech_g300_button *raw_button;

		if (!button->dirty)
			continue;

		raw_button = &report->buttons[button->index];

		raw_button->code = logitech_g300_button_action_to_raw(action);
		raw_button->modifier = 0x00;
		raw_button->key = 0x00;
		if (action->type == RATBAG_BUTTON_ACTION_TYPE_KEY) {
			raw_button->key = ratbag_hidraw_get_keyboard_usage_from_keycode(
				device, action->action.key.key);
		}
	}

	list_for_each(led, &profile->leds, link) {
		if (!led->dirty)
			continue;

		/* Clamp the 8 bit colors to 1 bit */
		report->led_red = led->color.red > 127;
		report->led_green = led->color.green > 127;
		report->led_blue = led->color.blue > 127;
	}


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

	return 0;
}

static int
logitech_g300_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc = 0;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		log_debug(device->ratbag,
			  "Profile %d changed, rewriting\n", profile->index);

		rc = logitech_g300_write_profile(profile);
		if (rc)
			return rc;
	}

	return 0;
}

static void
logitech_g300_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver logitech_g300_driver = {
	.name = "Logitech G300",
	.id = "logitech_g300",
	.probe = logitech_g300_probe,
	.remove = logitech_g300_remove,
	.read_profile = logitech_g300_read_profile,
	.commit = logitech_g300_commit,
	.set_active_profile = logitech_g300_set_active_profile,
	.read_button = logitech_g300_read_button,
	.read_led = logitech_g300_read_led,
};
