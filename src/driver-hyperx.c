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
#include <math.h>
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

#define HYPERX_MAX_MACRO_EVENTS 80
#define HYPERX_MAX_MACRO_PACKETS 14
#define HYPERX_MACRO_EVENT_MAX_KEYS 6
#define HYPERX_MACRO_EVENT_DEFAULT_DELAY htole16(20)

// Max number of events in a macro data packet
#define HYPERX_MACRO_DATA_MAX_EVENTS 6

#define hyperx_is_dpi_profile_enabled(profile_bitmask, n) ((profile_bitmask) & (1 << (n)))
#define hyperx_brightness_value(x) ((int) ((x / 255.0) * 100))

/**
 * Every macro data packet seems to have a sum byte? after the button byte that alternates between adding 1 and 2 each packet.
 * Another way of thinking about it is half of the numbers in the sum are 1 and half are 2.
 *
 * Thus, we get the following formula: (1x / 2) + (2x / 2). Which is simplified to 3x / 2. Note that this is int division, so there would be no fractional part.
 *
 * For example, the sum bytes for 6 packets would be: 0x00, 0x01, 0x03, 0x04, 0x06, 0x07
 */
#define HYPERX_MACRO_PACKET_SUM(x) ((3*(x)) / 2)

/**
 * The event count byte in odd indexed macro data packets have 0x80 added to it.
 * For example, a packet with 2 events would be 0x02 if it was even, and 0x82 if it was odd.
 */
#define HYPERX_MACRO_PACKET_EVENT_COUNT(x) (((x) % 2) * 0x80)

#define _Static_assert_bytes_after(expr) _Static_assert(expr, "Incorrect value for 'bytes_after'")
#define _Static_assert_enum_size(enum_name) _Static_assert(sizeof(enum enum_name) == 1, \
		"Incorrect size for '" #enum_name "''")

enum hyperx_config_value {
	HYPERX_CONFIG_POLLING_RATE              = 0xd0,
	HYPERX_CONFIG_LED_EFFECT                = 0xda,
	HYPERX_CONDIG_LED_MODE                  = 0xd9,
	HYPERX_CONFIG_DPI                       = 0xd3,
	HYPERX_CONFIG_BUTTON_ASSIGNMENT         = 0xd4,
	HYPERX_CONFIG_MACRO_ASSIGNMENT          = 0xd5,
	HYPERX_CONFIG_MACRO_DATA                = 0xd6,

	HYPERX_CONFIG_SAVE_SETTINGS             = 0xde
} __attribute((packed));

_Static_assert_enum_size(hyperx_config_value);

enum hyperx_save_type {
	HYPERX_SAVE_TYPE_ALL                    = 0xff,
	HYPERX_SAVE_TYPE_DPI_PROFILES           = 0x03
} __attribute((packed));

_Static_assert_enum_size(hyperx_save_type);

enum hyperx_dpi_config {
	HYPERX_DPI_CONFIG_SELECTED_PROFILE	= 0x00,
	HYPERX_DPI_CONFIG_ENABLED_PROFILES	= 0x01,
	HYPERX_DPI_CONFIG_DPI_VALUE         = 0x02,
} __attribute((packed));

_Static_assert_enum_size(hyperx_dpi_config);

enum hyperx_led_mode {
	HYPERX_LED_MODE_SOLID = 0x01
} __attribute((packed));

_Static_assert_enum_size(hyperx_led_mode);

enum hyperx_action_type {
	HYPERX_ACTION_TYPE_DISABLED,
	HYPERX_ACTION_TYPE_MOUSE,
	HYPERX_ACTION_TYPE_KEY,
	HYPERX_ACTION_TYPE_MEDIA,
	HYPERX_ACTION_TYPE_MACRO,
	HYPERX_ACTION_TYPE_SHORTCUT,
	HYPERX_ACTION_TYPE_DPI_TOGGLE = 0x07,
	HYPERX_ACTION_TYPE_UNKNOWN
} __attribute((packed));

_Static_assert_enum_size(hyperx_action_type);

enum hyperx_macro_event_type {
	HYPERX_MACRO_EVENT_TYPE_KEY = 0x1a
} __attribute((packed));

_Static_assert_enum_size(hyperx_macro_event_type);

enum {
	HYPERX_BYTES_AFTER_MACRO_ASSIGNMENT = 5,
	HYPERX_BYTES_AFTER_LED_MODE = 3,
};

union hyperx_polling_rate_packet {
	struct {
		enum hyperx_config_value polling_rate_cmd;
		uint8_t _padding[2];
		uint8_t bytes_after;
		uint8_t rate_index;
	} __attribute((packed));

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} __attribute((packed));

union hyperx_led_packet {
	struct {
		enum hyperx_config_value led_cmd;
		uint8_t led_mode;
		uint8_t packet_number;
		uint8_t bytes_after;
		struct hyperx_color colors[20];
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

union hyperx_led_mode_packet {
	struct {
		enum hyperx_config_value led_mode_cmd;
		uint8_t _padding[2];
		uint8_t bytes_after;
		uint8_t led_mode_value_before;
		enum hyperx_led_mode led_mode;
		uint8_t led_mode_value_after;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
} __attribute((packed));

union hyperx_dpi_profile_packet {
	struct {
		enum hyperx_config_value dpi_cmd;
		enum hyperx_dpi_config value_type;
		uint8_t dpi_profile_index;
		uint8_t bytes_after;
		uint16_t dpi_step_value;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
} __attribute((packed));

union hyperx_dpi_config_packet {
	struct {
		enum hyperx_config_value dpi_cmd;
		enum hyperx_dpi_config config_type;
		uint8_t _padding[1];
		uint8_t bytes_after;
		union {
			uint8_t enabled_dpi_profiles;
			uint8_t selected_profile;
		};
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_action {
	uint8_t type;
	uint8_t action;
	uint8_t button_index;
	struct ratbag_macro *macro;
};

union hyperx_button_packet {
	struct {
		enum hyperx_config_value button_cmd;
		uint8_t button;
		uint8_t action_type;
		uint8_t bytes_after;
		uint8_t action;
		uint8_t unknown;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_macro_event {
	enum hyperx_macro_event_type event_type;
	uint8_t modifier;
	uint8_t keys[HYPERX_MACRO_EVENT_MAX_KEYS];
	uint16_t delay_next_event;
} __attribute__((packed));

union hyperx_macro_data_packet {
	struct {
		enum hyperx_config_value macro_data_cmd;
		uint8_t button_index;
		uint8_t sum_value;
		uint8_t event_count;
		struct hyperx_macro_event events[HYPERX_MACRO_DATA_MAX_EVENTS];
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_macro {
	int event_count;
	union hyperx_macro_data_packet macro_packets[HYPERX_MAX_MACRO_PACKETS];
};

union hyperx_macro_assigment_packet {
	struct {
		enum hyperx_config_value macro_assign_cmd;
		uint8_t button;
		uint8_t _padding[1];
		uint8_t bytes_after;
		uint8_t event_count;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

union hyperx_save_settings_packet {
	struct {
		enum hyperx_config_value save_settings_cmd;
		enum hyperx_save_type save_type;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_data {
	uint8_t enabled_dpi_profiles; // A 5-bit little-endian number, where the nth bit corresponds to profile n
	uint8_t active_dpi_profile_index;
};

static int
hyperx_write(struct ratbag_device *device, uint8_t buf[HYPERX_PACKET_SIZE])
{
	//log_buf_debug(device->ratbag, "hyperx_write output report: ", buf, HYPERX_PACKET_SIZE);
	//return 0;
	return ratbag_hidraw_output_report(device, buf, HYPERX_PACKET_SIZE);
}

static struct hyperx_action
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
		.type = type_mapping[action.type],
		.button_index = button->index
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
			raw_action.macro = button->action.macro;
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

	uint8_t rate_index;
	for (rate_index = 0; rate_index < rate_count; rate_index++) {
		if (profile->hz == profile->rates[rate_index]) {
			valid_polling_rate = true;
			break;
		}
	}

	if (!valid_polling_rate) return -EINVAL;

	union hyperx_polling_rate_packet buf = {
		.polling_rate_cmd = HYPERX_CONFIG_POLLING_RATE,
		.bytes_after = sizeof(rate_count),
		.rate_index = rate_index
	};

	_Static_assert_bytes_after(sizeof(buf.rate_index) == 1);

	int rc = hyperx_write(device, buf.data);
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

	_Static_assert_bytes_after(sizeof(buf.enabled_dpi_profiles) == 1);

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

	_Static_assert_bytes_after(sizeof(buf.dpi_step_value) == 2);

	int rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Changed resolution successfully\n");

	return 0;
}

static int
hyperx_write_button_assignment(struct ratbag_device *device, struct hyperx_action *action)
{
	union hyperx_button_packet buf = {
		.button_cmd = HYPERX_CONFIG_BUTTON_ASSIGNMENT,
		.button = action->button_index,
		.action_type = action->type,
		.bytes_after = sizeof(buf.action) + sizeof(buf.unknown),
		.action = action->action
	};

	_Static_assert_bytes_after(sizeof(buf.action) + sizeof(buf.unknown) == 2);

	int rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Button assignment successful\n");
	return 0;
}

static void hyperx_initialize_macro(struct hyperx_macro *macro, uint8_t button_index)
{
	for (int i = 0; i < HYPERX_MAX_MACRO_PACKETS; i++) {
		macro->macro_packets[i].macro_data_cmd = HYPERX_CONFIG_MACRO_DATA;
		macro->macro_packets[i].button_index = button_index;
		macro->macro_packets[i].sum_value = HYPERX_MACRO_PACKET_SUM(i);
		macro->macro_packets[i].event_count = HYPERX_MACRO_PACKET_EVENT_COUNT(i);
	}
}

static void hyperx_macro_event_add_key(struct hyperx_macro_event *event, unsigned int keycode,
	int *keys_down_count_ref, int *event_key_count_ref, int *event_index_ref)
{
	if (*keys_down_count_ref == 0) {
		*event_index_ref += 1;
		event++;

		*event = (struct hyperx_macro_event) {
			.event_type = HYPERX_MACRO_EVENT_TYPE_KEY,
			.delay_next_event = HYPERX_MACRO_EVENT_DEFAULT_DELAY
		};
	}

	const uint8_t modifier_map[] = {
		[KEY_LEFTCTRL]	 = MODIFIER_LEFTCTRL,
		[KEY_LEFTSHIFT]  = MODIFIER_LEFTSHIFT,
		[KEY_LEFTALT]	 = MODIFIER_LEFTALT,
		[KEY_LEFTMETA]	 = MODIFIER_LEFTMETA,
		[KEY_RIGHTCTRL]  = MODIFIER_RIGHTCTRL,
		[KEY_RIGHTSHIFT] = MODIFIER_RIGHTSHIFT,
		[KEY_RIGHTALT]	 = MODIFIER_RIGHTALT,
		[KEY_RIGHTMETA]  = MODIFIER_RIGHTMETA
	};

	uint8_t hid_code = ratbag_hidraw_get_keyboard_usage_from_keycode(NULL, keycode);

	if (ratbag_key_is_modifier(keycode)) {
		event->modifier |= modifier_map[keycode];
	} else {
		event->keys[*event_key_count_ref] = hid_code;
		*event_key_count_ref += 1;
	}

	*keys_down_count_ref += 1;
}

static struct hyperx_macro_event *
hyperx_get_macro_events(struct ratbag_device *device, struct ratbag_macro *macro, int *out_event_count)
{
	struct hyperx_macro_event *hyperx_events = zalloc(sizeof(*hyperx_events) * HYPERX_MAX_MACRO_EVENTS);

	int event_index = -1;
	int keys_down_count = 0;
	int event_key_count = 0;

	bool keys_down[UINT8_MAX] = {0};
	bool event_keys[UINT8_MAX] = {0};
	bool has_key;

	for (int i = 0; i < MAX_MACRO_EVENTS; i++) {
		struct ratbag_macro_event *event = macro->events + i;
		unsigned int key = event->event.key;

		switch (event->type) {
		case RATBAG_MACRO_EVENT_INVALID:
			goto invalid_macro;
		case RATBAG_MACRO_EVENT_NONE:
			goto loop_end;
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			assert(key <= UINT8_MAX);
			has_key = keys_down[key] || event_keys[key];
			if (has_key
				|| (event_key_count == HYPERX_MACRO_EVENT_MAX_KEYS
					&& !ratbag_key_is_modifier(key))
			) {
				continue;
			}

			keys_down[key] = true;
			event_keys[key] = true;

			hyperx_macro_event_add_key(hyperx_events + event_index, key,
				&keys_down_count, &event_key_count, &event_index);

			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			assert(event->event.key <= UINT8_MAX);
			has_key = keys_down[key] || event_keys[key];
			if (!has_key) continue;

			if (keys_down_count == 0) {
				log_error(device->ratbag, "Lone key up event\n");
				goto invalid_macro;
			}

			keys_down[key] = false;
			keys_down_count--;

			if (keys_down_count > 0) continue;

			event_index++;

			hyperx_events[event_index] = (struct hyperx_macro_event) {
				.event_type = HYPERX_MACRO_EVENT_TYPE_KEY,
				.delay_next_event = HYPERX_MACRO_EVENT_DEFAULT_DELAY
			};

			memset(event_keys, 0, sizeof(bool) * UINT8_MAX);
			event_key_count = 0;

			break;
		case RATBAG_MACRO_EVENT_WAIT:
			if (event_index < 0) continue;
			if (event->event.timeout > UINT16_MAX) goto invalid_macro;

			hyperx_events[event_index].delay_next_event = htole16(event->event.timeout);
			break;
		}
	}

	loop_end:

	if (keys_down_count > 0 || (event_index + 1) > HYPERX_MAX_MACRO_EVENTS) {
		goto invalid_macro;
	}

	*out_event_count = event_index + 1;

	return hyperx_events;

	invalid_macro:
		free(hyperx_events);
		return NULL;
}

static struct hyperx_macro *
hyperx_parse_macro(struct ratbag_device *device, struct ratbag_macro *macro, uint8_t button_index)
{
	int packet_index = 0;
	int event_index = 0;
	int event_count = 0;

	struct hyperx_macro *hyperx_macro = zalloc(sizeof(*hyperx_macro));
	hyperx_initialize_macro(hyperx_macro, button_index);

	struct hyperx_macro_event *hyperx_events = hyperx_get_macro_events(device, macro, &event_count);
	if (!hyperx_events) goto invalid_macro;

	union hyperx_macro_data_packet *packet;

	for (int i = 0; i < event_count; i++) {
		packet_index = i / HYPERX_MACRO_DATA_MAX_EVENTS;
		event_index = i % HYPERX_MACRO_DATA_MAX_EVENTS;
		packet = hyperx_macro->macro_packets + packet_index;

		packet->events[event_index] = hyperx_events[i];
		packet->event_count++;
	}

	hyperx_macro->event_count = event_count;
	free(hyperx_events);

	return hyperx_macro;

	invalid_macro:
		free(hyperx_macro);
		return NULL;
}

static int
hyperx_write_macro(struct ratbag_device *device, struct hyperx_action *action)
{
	struct ratbag_macro *macro = action->macro;
	struct hyperx_macro *hyperx_macro = hyperx_parse_macro(device, macro, action->button_index);

	if (!hyperx_macro) return -EINVAL;

	const int events_per_packet = ARRAY_LENGTH(hyperx_macro->macro_packets->events);
	uint8_t packet_count = ceil(hyperx_macro->event_count / (double) events_per_packet);

	int rc = hyperx_write_button_assignment(device, action);
	if (rc < 0) goto free_macro;

	for (int i = 0; i < packet_count; i++) {
		rc = hyperx_write(device, hyperx_macro->macro_packets[i].data);
		if (rc < 0) goto free_macro;
	}

	union hyperx_macro_assigment_packet buf = {
		.macro_assign_cmd = HYPERX_CONFIG_MACRO_ASSIGNMENT,
		.button = action->button_index,
		.bytes_after = HYPERX_BYTES_AFTER_MACRO_ASSIGNMENT,
		.event_count = hyperx_macro->event_count
	};

	rc = hyperx_write(device, buf.data);

	free_macro:
		free(hyperx_macro);

	return rc;
}

static int
hyperx_write_button_action(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;

	log_debug(device->ratbag, "Changing action for button %d\n", button->index);

	struct hyperx_action action = hyperx_button_action_get_raw_action(button);
	if (action.type == HYPERX_ACTION_TYPE_UNKNOWN) return -EINVAL;

	if (action.type == HYPERX_ACTION_TYPE_MACRO) {
		return hyperx_write_macro(device, &action);
	}

	return hyperx_write_button_assignment(device, &action);
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

	_Static_assert_bytes_after(sizeof(led_effect.colors) == 60);

	for (int i = 0; i < HYPERX_LED_PACKET_COUNT; i++) {
		int rc = hyperx_write(device, led_effect.data);
		if (rc < 0) return rc;

		memset(&led_effect.colors, 0, led_effect.bytes_after);
		led_effect.packet_number = i + 1;
	}

	union hyperx_led_mode_packet led_mode = {
		.led_mode_cmd = HYPERX_CONDIG_LED_MODE,
		.bytes_after = HYPERX_BYTES_AFTER_LED_MODE,
		.led_mode_value_before = HYPERX_LED_MODE_VALUE_BEFORE,
		.led_mode = HYPERX_LED_MODE_SOLID,
		.led_mode_value_after = HYPERX_LED_MODE_VALUE_AFTER
	};

	int rc = hyperx_write(device, led_mode.data);
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

	const uint8_t default_enabled_dpi_profiles = 0b01111;
	const int polling_rate = 1000;
	const int dpi_levels[] = { 400, 800, 1600, 3200, 6000 };
	const unsigned int report_rates[] = { 125, 250, 500, 1000 };

	profile->is_active = true;

	ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
	ratbag_profile_set_report_rate_list(profile, report_rates,
		ARRAY_LENGTH(report_rates));

	ratbag_profile_set_report_rate(profile, polling_rate);

	drv_data->enabled_dpi_profiles = default_enabled_dpi_profiles;
	drv_data->active_dpi_profile_index = 0;

	ratbag_profile_for_each_resolution(profile, resolution) {
		ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);

		ratbag_resolution_set_dpi_list_from_range(resolution,
			HYPERX_MIN_DPI, HYPERX_MAX_DPI);
		ratbag_resolution_set_dpi(resolution, dpi_levels[resolution->index]);

		ratbag_resolution_set_disabled(resolution,
			!hyperx_is_dpi_profile_enabled(default_enabled_dpi_profiles, resolution->index));

		if (resolution->index == drv_data->active_dpi_profile_index) {
			ratbag_resolution_set_active(resolution);
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
		button->dirty = true;
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
		ratbag_led_set_brightness(led, 0xff);
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

			rc = hyperx_write_button_action(button);
			if (rc) return rc;
		}

		ratbag_profile_for_each_led(profile, led) {
			if (!led->dirty) continue;

			rc = hyperx_write_led(led);
			if (rc) return rc;
		}
	}

	union hyperx_save_settings_packet buf = {
		.save_settings_cmd = HYPERX_CONFIG_SAVE_SETTINGS,
		.save_type = HYPERX_SAVE_TYPE_ALL
	};

	rc = hyperx_write(device, buf.data);
	if (rc < 0) return rc;

	log_debug(device->ratbag, "Commit successful\n\n");

	return 0;
}

struct ratbag_driver hyperx_driver = {
	.name = "HP HyperX",
	.id = "hyperx",
	.probe = hyperx_probe,
	.remove = hyperx_remove,
	.commit = hyperx_commit
};
