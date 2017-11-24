/*
 * Copyright © 2017 Thomas Hindoe Paaboel Andersen.
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
#include "libratbag-data.h"
#include "hidpp-generic.h"

#define STEELSERIES_NUM_PROFILES	1
#define STEELSERIES_NUM_DPI		2

/* not sure these two are used for */
#define STEELSERIES_REPORT_ID_1			0x01
#define STEELSERIES_REPORT_ID_2			0x02

#define STEELSERIES_REPORT_SIZE_SHORT		32
#define STEELSERIES_REPORT_SIZE			64
#define STEELSERIES_REPORT_LONG_SIZE		262

#define STEELSERIES_ID_DPI_SHORT		0x03
#define STEELSERIES_ID_REPORT_RATE_SHORT	0x04
#define STEELSERIES_ID_SAVE_SHORT		0x09

#define STEELSERIES_ID_BUTTONS			0x31
#define STEELSERIES_ID_DPI			0x53
#define STEELSERIES_ID_REPORT_RATE		0x54
#define STEELSERIES_ID_LED			0x5b
#define STEELSERIES_ID_SAVE			0x59

#define STEELSERIES_BUTTON_OFF			0x00
#define STEELSERIES_BUTTON_RES_CYCLE		0x30
#define STEELSERIES_BUTTON_WHEEL_UP		0x31
#define STEELSERIES_BUTTON_WHEEL_DOWN		0x32
#define STEELSERIES_BUTTON_KBD			0x51
#define STEELSERIES_BUTTON_CONSUMER		0x61

static const enum ratbag_button_type button_types[6] =
{
	RATBAG_BUTTON_TYPE_LEFT,
	RATBAG_BUTTON_TYPE_RIGHT,
	RATBAG_BUTTON_TYPE_MIDDLE,
	RATBAG_BUTTON_TYPE_THUMB,
	RATBAG_BUTTON_TYPE_THUMB2,
	RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP,
};

static const struct ratbag_button_action button_actions[6] =
{
	BUTTON_ACTION_BUTTON(1),
	BUTTON_ACTION_BUTTON(2),
	BUTTON_ACTION_BUTTON(3),
	BUTTON_ACTION_BUTTON(4),
	BUTTON_ACTION_BUTTON(5),
	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP),
};

static int
steelseries_test_hidraw(struct ratbag_device *device)
{
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);

	if (device_version > 1)
		return ratbag_hidraw_has_report(device, STEELSERIES_REPORT_ID_1);
	else
		return true;
}

static int
steelseries_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	int rc, button_count, led_count, device_version;
	_cleanup_(dpi_list_freep) struct dpi_list *dpilist = NULL;
	_cleanup_(freep) struct dpi_range *dpirange = NULL;

	unsigned int report_rates[] = { 125, 250, 500, 1000 };

	rc = ratbag_find_hidraw(device, steelseries_test_hidraw);
	if (rc)
		return rc;

	device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	button_count = ratbag_device_data_steelseries_get_button_count(device->data);
	led_count = ratbag_device_data_steelseries_get_led_count(device->data);
	dpirange = ratbag_device_data_steelseries_get_dpi_range(device->data);
	dpilist = ratbag_device_data_steelseries_get_dpi_list(device->data);

	ratbag_device_init_profiles(device,
				    STEELSERIES_NUM_PROFILES,
				    STEELSERIES_NUM_DPI,
				    button_count,
				    led_count);

	/* only later models allow setting buttons on the device */
	if (device_version > 1) {
		/* set these caps manually as they are not assumed with only 1 profile */
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON);
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_KEY);
	}

	ratbag_device_unset_capability(device, RATBAG_DEVICE_CAP_QUERY_CONFIGURATION);

	/* The device does not support reading the current settings. Fall back
	   to some sensible defaults */
	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		ratbag_profile_for_each_resolution(profile, resolution) {
			if (resolution->index == 0) {
				resolution->is_active = true;
				resolution->is_default = true;
			}

			if (dpirange)
				ratbag_resolution_set_dpi_list_from_range(resolution,
									  dpirange->min,
									  dpirange->max);
			if (dpilist)
				ratbag_resolution_set_dpi_list(resolution,
							       (unsigned int *)dpilist->entries,
							       dpilist->nentries);

			resolution->dpi_x = resolution->dpis[resolution->index];
			resolution->dpi_y = resolution->dpis[resolution->index];

			ratbag_resolution_set_report_rate_list(resolution, report_rates,
							       ARRAY_LENGTH(report_rates));
			resolution->hz = 1000;
		}

		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

			button->type = button_types[button->index];
			ratbag_button_set_action(button, &button_actions[button->index]);
		}

		ratbag_profile_for_each_led(profile, led) {
			led->type = led->index == 0 ? RATBAG_LED_TYPE_LOGO : RATBAG_LED_TYPE_WHEEL;
			led->mode = RATBAG_LED_ON;
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			led->color.red = 0;
			led->color.green = 0;
			led->color.blue = 255;
		}
	}

	return 0;
}

static int
steelseries_write_dpi(struct ratbag_resolution *resolution)
{
	struct ratbag_device *device = resolution->profile->device;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	struct dpi_list *dpilist = NULL;
	struct dpi_range *dpirange = NULL;
	int ret;
	size_t buf_len;
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};

	dpirange = ratbag_device_data_steelseries_get_dpi_range(device->data);
	dpilist = ratbag_device_data_steelseries_get_dpi_list(device->data);

	if (device_version == 1) {
		int i = 0;

		/* when using lists the entries are enumerated in reverse */
		if (dpilist) {
			for (i = 0; i < (int)resolution->ndpis; i++) {
				if (resolution->dpis[i] == resolution->dpi_x)
					break;
			}
			i = resolution->ndpis - i;
		} else {
			i = resolution->dpi_x / dpirange->step - 1;
		}

		buf_len = STEELSERIES_REPORT_SIZE_SHORT;
		buf[0] = STEELSERIES_ID_DPI_SHORT;
		buf[1] = resolution->index + 1;
		buf[2] = i;
	} else {
		buf_len = STEELSERIES_REPORT_SIZE;
		buf[0] = STEELSERIES_ID_DPI;
		buf[2] = resolution->index + 1;
		buf[3] = resolution->dpi_x / dpirange->step - 1;
		buf[6] = 0x42; /* not sure if needed */
	}

	ret = ratbag_hidraw_output_report(device, buf, buf_len);
	if ((size_t)ret != buf_len)
		return ret;

	return 0;
}

static int
steelseries_write_report_rate(struct ratbag_resolution *resolution)
{
	struct ratbag_device *device = resolution->profile->device;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	int ret;
	size_t buf_len;
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};

	if (device_version == 1) {
		buf_len = STEELSERIES_REPORT_SIZE_SHORT;
		buf[0] = STEELSERIES_ID_REPORT_RATE_SHORT;
		buf[2] = 1000 / resolution->hz;
	} else {
		buf_len = STEELSERIES_REPORT_SIZE;
		buf[0] = STEELSERIES_ID_REPORT_RATE;
		buf[2] = 1000 / resolution->hz;
	}

	ret = ratbag_hidraw_output_report(device, buf, buf_len);
	if ((size_t)ret != buf_len)
		return ret;

	return 0;
}

static int
steelseries_write_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	uint8_t buf[STEELSERIES_REPORT_LONG_SIZE] = {0};
	struct ratbag_button *button;
	int ret;

	if (device_version < 2)
		return 0;

	buf[0] = STEELSERIES_ID_BUTTONS;

	ratbag_profile_for_each_button(profile, button) {
		struct ratbag_button_action *action = &button->action;
		uint16_t code;
		int idx;

		/* Each button takes up 5 bytes starting from index 2 */
		idx = 2 + button->index * 5;

		switch (action->type) {
		case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
			buf[idx] = action->action.button;
			break;

		case RATBAG_BUTTON_ACTION_TYPE_KEY:
			code = ratbag_hidraw_get_keyboard_usage_from_keycode(
						device, action->action.key.key);
			if (code) {
				buf[idx] = STEELSERIES_BUTTON_KBD;
				buf[idx + 1] = code;
			} else {
				code = ratbag_hidraw_get_consumer_usage_from_keycode(
						device, action->action.key.key);
				buf[idx] = STEELSERIES_BUTTON_CONSUMER;
				buf[idx + 1] = code;
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
			switch (action->action.special) {
			case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
				buf[idx] = STEELSERIES_BUTTON_RES_CYCLE;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
				buf[idx] = STEELSERIES_BUTTON_WHEEL_UP;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
				buf[idx] = STEELSERIES_BUTTON_WHEEL_DOWN;
				break;
			default:
				break;
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_NONE:
		default:
			buf[idx] = STEELSERIES_BUTTON_OFF;
			break;
		}
	}

	ret = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	if (ret != sizeof(buf))
		return ret;

	return 0;
}

static int
steelseries_write_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};
	uint16_t duration, min_duration;
	int ret;

	buf[0] = STEELSERIES_ID_LED;
	buf[2] = led->index;

	/* not sure if these two are needed */
	buf[15] = 0x01;
	buf[17] = 0x01;

	switch(led->mode) {
	case RATBAG_LED_OFF:
		buf[19] = 0x01;
		buf[27] = 0x01;
		buf[28] = buf[31] = 0x00;
		buf[29] = buf[32] = 0x00;
		buf[30] = buf[33] = 0x00;
		/* not sure why the duration is set for the steady color or why
		   it is different for the two LEDs */
		duration = led->index == 0 ? 10000 : 5000; /* 0x1027 or 0x8813 */
		break;
	case RATBAG_LED_ON:
		buf[19] = 0x01;
		buf[27] = 0x01;
		buf[28] = buf[31] = led->color.red;
		buf[29] = buf[32] = led->color.green;
		buf[30] = buf[33] = led->color.blue;

		/* not sure why the duration is set for the steady color or why
		   it is different for the two LEDs */
		duration = led->index == 0 ? 10000 : 5000; /* 0x1027 or 0x8813 */
		break;
	case RATBAG_LED_CYCLE:
		buf[27] = 0x04; /* number of steps in cycle */

		/* start color */
		buf[28] = buf[31] = 0xFF;
		buf[29] = buf[32] = 0x00;
		buf[30] = buf[33] = 0x00;

		/* Cycle to green */
		buf[35] = 0x00;
		buf[36] = 0xFF;
		buf[37] = 0x00;
		buf[38] = 0x54; /* normalized time share of animation */

		/* Cycle to blue */
		buf[39] = 0x00;
		buf[40] = 0x00;
		buf[41] = 0xFF;
		buf[42] = 0x54; /* normalized time share of animation */

		/* Cycle to red */
		buf[43] = 0xFF;
		buf[44] = 0x00;
		buf[45] = 0x00;
		buf[46] = 0x56; /* normalized time share of animation */

		duration = led->ms;
		break;
	case RATBAG_LED_BREATHING:
		buf[27] = 0x03; /* number of steps in cycle */

		/* start color */
		buf[28] = buf[31] = led->color.red;
		buf[29] = buf[32] = led->color.green;
		buf[30] = buf[33] = led->color.blue;

		/* Cycle to black */
		buf[35] = 0x00;
		buf[36] = 0x00;
		buf[37] = 0x00;
		buf[38] = 0x7F; /* normalized time share of animation */

		/* Cycle to selected color */
		buf[39] = led->color.red;
		buf[40] = led->color.green;
		buf[41] = led->color.blue;
		buf[42] = 0x7F; /* normalized time share of animation */

		duration = led->ms;
		break;
	default:
		return -EINVAL;
	}

	/* this seems to be the minimum allowed */
	min_duration = buf[27] * 330;
	if (duration < min_duration)
		duration = min_duration;

	hidpp_set_unaligned_le_u16(&buf[3], duration);

	ret = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	if (ret != sizeof(buf))
		return ret;

	return 0;
}

static int
steelseries_write_save(struct ratbag_device *device)
{
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	int ret;
	size_t buf_len;
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};

	if (device_version == 1) {
		buf_len = STEELSERIES_REPORT_SIZE_SHORT;
		buf[0] = STEELSERIES_ID_SAVE_SHORT;
	} else {
		buf_len = STEELSERIES_REPORT_SIZE;
		buf[0] = STEELSERIES_ID_SAVE;
	}

	ret = ratbag_hidraw_output_report(device, buf, buf_len);
	if ((size_t)ret != buf_len)
		return ret;

	return 0;
}

static int
steelseries_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		rc = steelseries_write_dpi(resolution);
		if (rc != 0)
			return rc;

		/* The same hz is used for all resolutions. Only write once. */
		if (resolution->index > 0)
			continue;

		rc = steelseries_write_report_rate(resolution);
		if (rc != 0)
			return rc;
	}

	rc = steelseries_write_buttons(profile);
	if (rc != 0)
		return rc;

	ratbag_profile_for_each_led(profile, led) {
		if (!led->dirty)
			continue;

		rc = steelseries_write_led(led);
		if (rc != 0)
			return rc;
	}

	return 0;
}

static int
steelseries_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc = 0;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		log_debug(device->ratbag,
			  "Profile %d changed, rewriting\n", profile->index);

		rc = steelseries_write_profile(profile);
		if (rc)
			return rc;

		/* persist the current settings on the device */

		rc = steelseries_write_save(device);
		if (rc)
			return rc;
	}

	return 0;
}

static void
steelseries_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

struct ratbag_driver steelseries_driver = {
	.name = "SteelSeries",
	.id = "steelseries",
	.probe = steelseries_probe,
	.remove = steelseries_remove,
	.commit = steelseries_commit,
};
