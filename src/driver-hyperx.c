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
#include <libevdev/libevdev.h>
#include <linux/input.h>
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

#define HYPERX_ACTION_DPI_TOGGLE 8

#define hyperx_brightness_value(x) ((int) ((x / 255.0) * 100))

#define BYTES_AFTER
#define PADDING 0

enum hyperx_config_value {
	HYPERX_CONFIG_POLLING_RATE              = 0xd0,
	HYPERX_CONFIG_LED_EFFECT                = 0xda,
	HYPERX_CONDIG_LED_MODE                  = 0xd9,
	HYPERX_CONFIG_DPI                       = 0xd3,
	HYPERX_CONFIG_BUTTON_ASSIGNMENT         = 0xd4,
	HYPERX_CONFIG_MACRO_ASSIGNMENT          = 0xd5,
	HYPERX_CONFIG_MACRO_DATA                = 0xd6,

	HYPERX_CONFIG_SAVE_SETTINGS             = 0xde
};

enum hyperx_save_byte {
	HYPERX_SAVE_BYTE_ALL                    = 0xff,
	HYPERX_SAVE_BYTE_DPI_PROFILE_INDICATORS = 0x03
};

enum hyperx_dpi_config {
	HYPERX_DPI_CONFIG_SELECTED_PROFILE	= 0x00,
	HYPERX_DPI_CONFIG_ENABLED_PROFILES	= 0x01,
	HYPERX_DPI_CONFIG_DPI_VALUE         = 0x02,
};

enum hyperx_led_mode {
	HYPERX_LED_MODE_SOLID = 0x01
};

enum {
	HYPERX_ACTION_TYPE_DISABLED,
	HYPERX_ACTION_TYPE_MOUSE,
	HYPERX_ACTION_TYPE_KEY,
	HYPERX_ACTION_TYPE_MEDIA,
	HYPERX_ACTION_TYPE_MACRO,
	HYPERX_ACTION_TYPE_SHORTCUT,
	HYPERX_ACTION_TYPE_DPI_TOGGLE = 0x07,
	HYPERX_ACTION_TYPE_UNKNOWN
};

struct hyperx_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct hyperx_action {
	uint8_t type;
	uint8_t action;
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

union hyperx_dpi_profile_packet {
	struct {
		uint8_t dpi_cmd;
		uint8_t value_type; // A HYPERX_DPI_CONFIG value
		uint8_t dpi_profile_index;
		uint8_t bytes_after;
		uint16_t dpi_step_value;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

union hyperx_dpi_config_packet {
	struct {
		uint8_t dpi_cmd;
		uint8_t config_type; // A HYPERX_DPI_CONFIG value
		uint8_t _padding;
		uint8_t bytes_after;
		union {
			uint8_t enabled_dpi_profiles;
			uint8_t selected_profile;
		};
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

union hyperx_button_packet {
	struct {
		uint8_t button_cmd;
		uint8_t button;
		uint8_t action_type;
		uint8_t bytes_after;
		union {
			uint8_t macro_button; // The mouse button that a macro is assigned to
			uint8_t action;
		};
		uint8_t unknown;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_data {
	uint8_t enabled_dpi_profiles; // A 5-bit (little-endian) number, where the nth bit corresponds to profile n
	uint8_t active_dpi_profile_index;
};

static int
hyperx_write(struct ratbag_device *device, uint8_t buf[HYPERX_PACKET_SIZE])
{
	return ratbag_hidraw_output_report(device, buf, HYPERX_PACKET_SIZE);
}

static inline struct hyperx_action
hyperx_button_action_get_raw_action(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct ratbag_button_action action = button->action;

	uint8_t type_mapping[] = {
		[RATBAG_BUTTON_ACTION_TYPE_NONE] = HYPERX_ACTION_TYPE_DISABLED,
		[RATBAG_BUTTON_ACTION_TYPE_BUTTON] = HYPERX_ACTION_TYPE_MOUSE,
		[RATBAG_BUTTON_ACTION_TYPE_KEY] = HYPERX_ACTION_TYPE_KEY,
		[RATBAG_BUTTON_ACTION_TYPE_SPECIAL] = HYPERX_ACTION_TYPE_DPI_TOGGLE,
		[RATBAG_BUTTON_ACTION_TYPE_MACRO] = HYPERX_ACTION_TYPE_MACRO,
	};

	if (action.type == RATBAG_BUTTON_ACTION_TYPE_UNKNOWN) {
		return (struct hyperx_action) {.type = HYPERX_ACTION_TYPE_UNKNOWN};
	}

	struct hyperx_action raw_action = {
		.type = type_mapping[action.type]
	};

	switch (action.type) {
		case RATBAG_BUTTON_ACTION_TYPE_NONE:
			raw_action.action = 0;
			break;
		case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
			raw_action.action = action.action.button;
			if (raw_action.action >= HYPERX_BUTTON_COUNT) {
				raw_action.type = HYPERX_ACTION_TYPE_UNKNOWN;
			}

			break;
		case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
			if (action.action.special != RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) {
				raw_action.type = HYPERX_ACTION_TYPE_UNKNOWN;
			}

			raw_action.action = HYPERX_ACTION_DPI_TOGGLE;
			break;
		case RATBAG_BUTTON_ACTION_TYPE_KEY:
			raw_action.action = ratbag_hidraw_get_keyboard_usage_from_keycode(device, action.action.key);
			break;
		case RATBAG_BUTTON_ACTION_TYPE_MACRO:
			raw_action.action = button->index;
			break;
		default:
			break;
	}

	return raw_action;
}

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

	int rc = hyperx_write(device, buf);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Changed polling rate successfully\n");
	return 0;
}


/**
 * @brief Sets the enabled dpi profiles and the selected dpi profile.
 */
static int
hyperx_write_dpi_configuration(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct hyperx_data *drv_data = ratbag_get_drv_data(device);

	log_debug(device->ratbag, "Writing dpi configuration\n");

	union hyperx_dpi_config_packet buf = {
		.dpi_cmd = HYPERX_CONFIG_DPI,
		.config_type = HYPERX_DPI_CONFIG_ENABLED_PROFILES,
		.bytes_after = sizeof(buf.enabled_dpi_profiles),
		.enabled_dpi_profiles = drv_data->enabled_dpi_profiles
	};

	_Static_assert(sizeof(buf.enabled_dpi_profiles) == 1, "Incorrect value for 'bytes_after'");

	int rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	buf.config_type = HYPERX_DPI_CONFIG_SELECTED_PROFILE;
	buf.selected_profile = drv_data->active_dpi_profile_index;

	rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Wrote dpi configuration successfully\n");
	return 0;
}

static int
hyperx_write_resolution(struct ratbag_resolution *resolution)
{
	struct ratbag_device *device = resolution->profile->device;
	struct hyperx_data *drv_data = ratbag_get_drv_data(device);

	if (resolution->is_disabled) {
		drv_data->enabled_dpi_profiles &= ~(1 << resolution->index);
		return 0;
	} else {
		drv_data->enabled_dpi_profiles |= 1 << resolution->index;
	}

	log_debug(device->ratbag, "\nChanging resolution %d\nEnabled profiles: %b\n", resolution->index, drv_data->enabled_dpi_profiles);

	if (resolution->is_active) {
		drv_data->active_dpi_profile_index = resolution->index;
	}

	union hyperx_dpi_profile_packet buf = {
		.dpi_cmd = HYPERX_CONFIG_DPI,
		.value_type = HYPERX_DPI_CONFIG_DPI_VALUE,
		.dpi_profile_index = resolution->index,
		.bytes_after = sizeof(buf.dpi_step_value),
		.dpi_step_value = htole16(ratbag_resolution_get_dpi(resolution) / 100),
	};

	_Static_assert(sizeof(buf.dpi_step_value) == 2, "Incorrect value for 'bytes_after'");

	int rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Changed resolution successfully\n");

	return 0;
}

static int
hyperx_write_macro(struct ratbag_button *button)
{
	return 0;
}

static int
hyperx_write_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;

	log_debug(device->ratbag, "Changing action for button %d\n", button->index);

	struct hyperx_action raw_action = hyperx_button_action_get_raw_action(button);
	if (raw_action.type == HYPERX_ACTION_TYPE_UNKNOWN) return -EINVAL;

	union hyperx_button_packet buf = {
		.button_cmd = HYPERX_CONFIG_BUTTON_ASSIGNMENT,
		.button = button->index,
		.action_type = raw_action.type,
		.bytes_after = sizeof(buf.action) + sizeof(buf.unknown),
		.action = raw_action.action
	};

	int rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	if (raw_action.type == HYPERX_ACTION_TYPE_MACRO) {
		return hyperx_write_macro(button);
	}

	log_debug(device->ratbag, "Button assignment successful\n");

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
		int rc = hyperx_write(device, led_effect.data);
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

	int rc = hyperx_write(device, led_mode);
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
	struct ratbag_device *device = profile->device;
	struct hyperx_data *drv_data = ratbag_get_drv_data(device);
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
		ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);

		ratbag_resolution_set_dpi_list_from_range(resolution,
			HYPERX_MIN_DPI, HYPERX_MAX_DPI);
		ratbag_resolution_set_dpi(resolution, dpi_levels[resolution->index]);

		ratbag_resolution_set_disabled(resolution, true);

		if (resolution->index == 0) {
			ratbag_resolution_set_disabled(resolution, false);
			ratbag_resolution_set_active(resolution);
			ratbag_resolution_set_default(resolution);
		}
	}

	// Enable first profile only
	drv_data->enabled_dpi_profiles = 1;
	drv_data->active_dpi_profile_index = 0;

	ratbag_profile_for_each_button(profile, button) {
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);

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
	struct hyperx_data *drv_data;

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

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	ratbag_device_for_each_profile(device, profile) {
		hyperx_read_profile(profile);
	}

	return 0;
}

static void
hyperx_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
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
		if (!profile->dirty) continue;

		if (profile->rate_dirty) {
			rc = hyperx_write_polling_rate(profile);
			if (rc) return rc;
		}

		int changed_resolutions = 0;
		ratbag_profile_for_each_resolution(profile, resolution) {
			if (!resolution->dirty) continue;
			changed_resolutions++;

			rc = hyperx_write_resolution(resolution);
			if (rc) return rc;
		}

		if (changed_resolutions > 0) {
			hyperx_write_dpi_configuration(device, profile);
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
