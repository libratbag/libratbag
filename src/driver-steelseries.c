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
#include "hidpp-generic.h"

#define STEELSERIES_NUM_PROFILES	1
#define STEELSERIES_NUM_BUTTONS		6
#define STEELSERIES_NUM_DPI		2
#define STEELSERIES_NUM_LED		2
#define STEELSERIES_DPI_MIN		100
#define STEELSERIES_DPI_MAX		12000

/* not sure these are used for */
#define STEELSERIES_REPORT_ID_1		0x01
#define STEELSERIES_REPORT_ID_2		0x02

#define STEELSERIES_REPORT_SIZE		64

#define STEELSERIES_ID_DPI		0x53
#define STEELSERIES_ID_REPORT_RATE	0x54
#define STEELSERIES_ID_LED		0x5b
#define STEELSERIES_ID_SAVE		0x59

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
	return ratbag_hidraw_has_report(device, 0x01);
}

static int
steelseries_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	int rc;
	unsigned int report_rates[] = { 125, 250, 500, 1000 };


	rc = ratbag_find_hidraw(device, steelseries_test_hidraw);
	if (rc)
		return rc;

	ratbag_device_init_profiles(device,
				    STEELSERIES_NUM_PROFILES,
				    STEELSERIES_NUM_DPI,
				    STEELSERIES_NUM_BUTTONS,
				    STEELSERIES_NUM_LED);


	/* set these caps manually as they are not assumed with only 1 profile */
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_KEY);

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

			resolution->dpi_x = 1000 * (1 + resolution->index);
			resolution->dpi_y = 1000 * (1 + resolution->index);
			resolution->hz = 1000;

			ratbag_resolution_set_dpi_list_from_range(resolution,
								  STEELSERIES_DPI_MIN,
								  STEELSERIES_DPI_MAX);
			ratbag_resolution_set_report_rate_list(resolution, report_rates,
							       ARRAY_LENGTH(report_rates));
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
	int ret;
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};

	buf[0] = STEELSERIES_ID_DPI;
	buf[2] = resolution->index + 1;
	buf[3] = resolution->dpi_x / 100 - 1;
	buf[6] = 0x42; /* not sure if needed */

	ret = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	if (ret != sizeof(buf))
		return ret;

	return 0;
}


static int
steelseries_write_report_rate(struct ratbag_resolution *resolution)
{
	struct ratbag_device *device = resolution->profile->device;
	int ret;
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};

	buf[0] = STEELSERIES_ID_REPORT_RATE;
	buf[2] = 1000 / resolution->hz;

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
	uint16_t rate;
	int ret;

	rate = led->index == 0 ? 10000 : 5000; /* 0x1027 or 0x8813 */

	buf[0] = STEELSERIES_ID_LED;
	buf[2] = led->index;

	/* not sure why the rate is set for the steady color or why it is
	   different for the two LEDs */
	hidpp_set_unaligned_le_u16(&buf[3], rate);

	/* not sure if these four are needed either */
	buf[15] = 0x01;
	buf[17] = 0x01;
	buf[19] = 0x01;
	buf[27] = 0x01;

	buf[28] = buf[31] = led->color.red;
	buf[29] = buf[32] = led->color.green;
	buf[30] = buf[33] = led->color.blue;

	ret = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	if (ret != sizeof(buf))
		return ret;

	return 0;
}

static int
steelseries_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
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

	ratbag_profile_for_each_button(profile, button) {
		/* TODO */
	}

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
	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};
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
		buf[0] = STEELSERIES_ID_SAVE;
		rc = ratbag_hidraw_output_report(device, buf, sizeof(buf));
		if (rc != sizeof(buf))
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
