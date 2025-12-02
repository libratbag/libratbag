/*
 * Copyright © 2025 Evan Razzaque.
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
#include <stddef.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define HYPERX_PROFILE_COUNT 1
#define HYPERX_BUTTON_COUNT 6
#define HYPERX_NUM_DPI 5
#define HYPERX_MIN_DPI 200
#define HYPERX_MAX_DPI 16000
#define HYPERX_LED_COUNT 1

#define HYPERX_USAGE_PAGE 0xff00
#define HYPERX_PACKET_SIZE 64

#define HYPERX_LED_PACKET_COUNT 6

// Magic numbers, no clue what these mean
#define HYPERX_LED_MODE_VALUE_BEFORE 0x55
#define HYPERX_LED_MODE_VALUE_AFTER 0x23

#define hyperx_brightness_value(x) ((int) ((x / 255.0) * 100))

#define BYTES_AFTER
#define PADDING 0

enum {
	HYPERX_CONFIG_POLLING_RATE              = 0xd0,
	HYPERX_CONFIG_LED_EFFECT                = 0xda,
	HYPERX_CONDIG_LED_MODE                  = 0xd9,
	HYPERX_CONFIG_DPI                       = 0xd3,
	HYPERX_CONFIG_BUTTON_ASSIGNMENT         = 0xd4,
	HYPERX_CONFIG_MACRO_ASSIGNMENT          = 0xd5,
	HYPERX_CONFIG_MACRO_DATA                = 0xd6,

	HYPERX_CONFIG_SAVE_SETTINGS             = 0xde
};

enum {
	HYPERX_SAVE_BYTE_ALL                    = 0xff,
	HYPERX_SAVE_BYTE_DPI_PROFILE_INDICATORS = 0x03
};

enum {
	HYPERX_LED_MODE_SOLID = 0x01
};

struct hyperx_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

union hyperx_led_packet {
	struct {
		uint8_t led_cmd;
		uint8_t led_mode;
		uint8_t packet_number;
		uint8_t bytes_after;
		struct hyperx_color colors[20];
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

static int
hyperx_write_polling_rate(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	log_debug(device->ratbag, "Changing polling rate to %d\n", profile->hz);

	int rate_count = profile->nrates;
	bool valid_polling_rate = false;

	int rate_index;
	for (rate_index = 0; rate_index < rate_count; rate_index++) {
		if (profile->hz == profile->rates[rate_index]) {
			valid_polling_rate = true;
			break;
		}
	}

	if (!valid_polling_rate) return -EINVAL;

	uint8_t buf[HYPERX_PACKET_SIZE] = {
		HYPERX_CONFIG_POLLING_RATE,
		PADDING,
		PADDING,
		BYTES_AFTER 1,
		rate_index
	};

	int rc = ratbag_hidraw_output_report(device, buf, HYPERX_PACKET_SIZE);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Changed polling rate successfully\n");
	return 0;
}

static int
hyperx_write_resolution(struct ratbag_resolution *resolution)
{
	return 0;
}

static int
hyperx_write_button(struct ratbag_button *button)
{
	return 0;
}

static int
hyperx_write_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	log_debug(device->ratbag, "Changing led\n");

	uint8_t brightness = hyperx_brightness_value(led->brightness);
	if (led->mode == RATBAG_LED_OFF) brightness = 0;

	uint8_t red = led->color.red * ((float) brightness / 100);
	uint8_t green = led->color.green * ((float) brightness / 100);
	uint8_t blue = led->color.blue * ((float) brightness / 100);

	union hyperx_led_packet led_effect = {
		.led_cmd = HYPERX_CONFIG_LED_EFFECT,
		.led_mode = HYPERX_LED_MODE_SOLID,
		.packet_number = 0,
		.bytes_after = sizeof(led_effect.colors),
		.colors = {{.red = red, .green = green, .blue = blue}}
	};

	assert(led_effect.bytes_after == 60);

	for (int i = 0; i < HYPERX_LED_PACKET_COUNT; i++) {
		int rc = ratbag_hidraw_output_report(device, led_effect.data, HYPERX_PACKET_SIZE);
		if (rc < 0) return rc;

		memset(&led_effect.colors, 0, led_effect.bytes_after);
		led_effect.packet_number = i + 1;
	}

	uint8_t led_mode[HYPERX_PACKET_SIZE] = {
		HYPERX_CONDIG_LED_MODE,
		PADDING,
		PADDING,
		BYTES_AFTER 3,
		HYPERX_LED_MODE_VALUE_BEFORE,
		HYPERX_LED_MODE_SOLID,
		HYPERX_LED_MODE_VALUE_AFTER
	};

	int rc = ratbag_hidraw_output_report(device, led_mode, HYPERX_PACKET_SIZE);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Changed led successfully\n");
	return 0;
}

/**
 * Reading settings from the mouse is not implemented, so we load default settings.
 */
static void
hyperx_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;

	struct ratbag_button_action default_actions[] = {
		BUTTON_ACTION_BUTTON(1),
		BUTTON_ACTION_BUTTON(2),
		BUTTON_ACTION_BUTTON(3),
		BUTTON_ACTION_BUTTON(4),
		BUTTON_ACTION_BUTTON(5),
		BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP),
	};

	int dpi_levels[] = { 400, 800, 1600, 3200, 6000 };
	unsigned int report_rates[] = { 125, 250, 500, 1000 };

	int polling_rate = 1000;

	profile->is_active = true;

	ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
	ratbag_profile_set_report_rate_list(profile, report_rates,
		ARRAY_LENGTH(report_rates));

	profile->hz = polling_rate;

	ratbag_profile_for_each_resolution(profile, resolution) {
		ratbag_resolution_set_dpi_list_from_range(resolution,
			HYPERX_MIN_DPI, HYPERX_MAX_DPI);
		ratbag_resolution_set_dpi(resolution, dpi_levels[resolution->index]);

		if (resolution->index == 0) {
			resolution->is_active = true;
			ratbag_resolution_set_default(resolution);
		}
	}

	ratbag_profile_for_each_button(profile, button) {
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

		ratbag_button_set_action(button, default_actions + button->index);
	}

	ratbag_profile_for_each_led(profile, led) {
		ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);

		ratbag_led_set_mode(led, RATBAG_LED_ON);
		ratbag_led_set_color(led, (struct ratbag_color) {
			.red = 0xff,
			.green = 0,
			.blue = 0
		});
		ratbag_led_set_brightness(led, 255);
	}
}

static int
hyperx_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc;

	rc = ratbag_open_hidraw(device);
	if (rc) return rc;

	if (ratbag_hidraw_get_usage_page(device, 0) != HYPERX_USAGE_PAGE) {
		ratbag_close_hidraw(device);
		return -ENODEV;
	}

	ratbag_device_init_profiles(device,
		HYPERX_PROFILE_COUNT,
		HYPERX_NUM_DPI,
		HYPERX_BUTTON_COUNT,
		HYPERX_LED_COUNT
	);

	ratbag_device_for_each_profile(device, profile) {
		hyperx_read_profile(profile);
	}

	return 0;
}

static void
hyperx_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

static int
hyperx_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;

	int rc;
	log_debug(device->ratbag, "Commiting settings\n");

	ratbag_device_for_each_profile(device, profile) {
		if (profile->rate_dirty) {
			rc = hyperx_write_polling_rate(profile);
			if (rc) return rc;
		}

		ratbag_profile_for_each_resolution(profile, resolution) {
			if (!resolution->dirty) continue;

			rc = hyperx_write_resolution(resolution);
			if (rc) return rc;
		}

		ratbag_profile_for_each_button(profile, button) {
			if (!button->dirty) continue;

			rc = hyperx_write_button(button);
			if (rc) return rc;
		}

		ratbag_profile_for_each_led(profile, led) {
			if (!led->dirty) continue;

			rc = hyperx_write_led(led);
			if (rc) return rc;
		}
	}

	log_debug(device->ratbag, "Commit successful\n");

	return 0;
}

struct ratbag_driver hyperx_driver = {
	.name = "HP HyperX",
	.id = "hyperx",
	.probe = hyperx_probe,
	.remove = hyperx_remove,
	.commit = hyperx_commit
};
