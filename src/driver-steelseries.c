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

#include "driver-steelseries.h"
#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-data.h"

#define STEELSERIES_NUM_PROFILES	1
#define STEELSERIES_NUM_DPI		2
#define STEELSERIES_INPUT_ENDPOINT	0
#define STEELSERIES_INPUT_HIDRAW	1

/* not sure these two are used for */
#define STEELSERIES_REPORT_ID			0x00 // steelseries doesn't use numbered reports
#define STEELSERIES_REPORT_ID_1			0x01
#define STEELSERIES_REPORT_ID_2			0x02

#define STEELSERIES_REPORT_SIZE_SHORT		32
#define STEELSERIES_REPORT_SIZE			64
#define STEELSERIES_REPORT_LONG_SIZE		262

#define STEELSERIES_ID_DPI_SHORT		0x03
#define STEELSERIES_ID_REPORT_RATE_SHORT	0x04
#define STEELSERIES_ID_LED_INTENSITY_SHORT	0x05
#define STEELSERIES_ID_LED_EFFECT_SHORT		0x07
#define STEELSERIES_ID_LED_COLOR_SHORT		0x08
#define STEELSERIES_ID_LED_COLOR_SHORT_RIVAL100	0x05
#define STEELSERIES_ID_SAVE_SHORT		0x09
#define STEELSERIES_ID_FIRMWARE_PROTOCOL1	0x10

#define STEELSERIES_ID_BUTTONS			0x31
#define STEELSERIES_ID_DPI			0x53
#define STEELSERIES_ID_REPORT_RATE		0x54
#define STEELSERIES_ID_LED			0x5b
#define STEELSERIES_ID_SAVE			0x59
#define STEELSERIES_ID_FIRMWARE_PROTOCOL2	0x90
#define STEELSERIES_ID_SETTINGS		0x92

#define STEELSERIES_ID_DPI_PROTOCOL3		0x03
#define STEELSERIES_ID_REPORT_RATE_PROTOCOL3	0x04
#define STEELSERIES_ID_LED_PROTOCOL3		0x05
#define STEELSERIES_ID_SAVE_PROTOCOL3		0x09
#define STEELSERIES_ID_FIRMWARE_PROTOCOL3	0x10
#define STEELSERIES_ID_SETTINGS_PROTOCOL3	0x16

#define STEELSERIES_ID_DPI_PROTOCOL4		0x15
#define STEELSERIES_ID_REPORT_RATE_PROTOCOL4	0x17

#define STEELSERIES_BUTTON_OFF			0x00
#define STEELSERIES_BUTTON_RES_CYCLE		0x30
#define STEELSERIES_BUTTON_WHEEL_UP		0x31
#define STEELSERIES_BUTTON_WHEEL_DOWN		0x32
#define STEELSERIES_BUTTON_KEY			0x10
#define STEELSERIES_BUTTON_KBD			0x51
#define STEELSERIES_BUTTON_CONSUMER		0x61

struct steelseries_point {
	struct list link;

	struct ratbag_color color;	/* point color */
	uint8_t pos;			/* relative position in the cycle */
};

struct steelseries_led_cycle {
	uint8_t led_id;			/* led id */
	uint16_t duration;		/* cycle duration */
	bool repeat;			/* if the cycle restarts automatically */
	uint8_t trigger_buttons;	/* trigger button combination */
	struct list points;		/* colors in the cycle */
};

struct steelseries_led_cycle_spec {
	uint8_t hid_report_type;/* either HID_OUTPUT_REPORT or HID_FEATURE_REPORT */
	int header_len;		/* number of bytes in the header */
	uint8_t cmd_val;	/* command value for the color command */
	bool has_2_led_ids;	/* some mice have 2 fields for led id */

	int led_id_idx;		/* index of the led id field */
	int led_id2_idx;	/* 2nd led id index (if required by protocol) */
	int duration_idx;	/* index of the cycle duration field */
	int repeat_idx;		/* index of the repeat field */
	int trigger_idx;	/* index of the trigger mask field */
	int point_count_idx;	/* index of the point counter field */
};

union steelseries_message {
	struct {
		uint8_t report_id;
		uint8_t parameters[STEELSERIES_REPORT_SIZE - 1];
	} __attribute__((packed)) msg;
	uint8_t data[STEELSERIES_REPORT_SIZE];
};

_Static_assert(sizeof(union steelseries_message) == STEELSERIES_REPORT_SIZE,
	       "Size of union steelseries_message is wrong");

static int
steelseries_test_hidraw(struct ratbag_device *device)
{
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);

	/* Rival mice are composite devices with multiple HID devices
	 * and only the HID vendor device can be used to configure the
	 * device.
	 * However, this check doesn't apply to some devices, like
	 * Sensei 310, Rival 600, Rival 650, etc, so we can't rely on
	 * it.
	 */
	if (!ratbag_hidraw_has_vendor_page(device)) {
		log_debug(device->ratbag,
			  "This is a non-vendor HID device, "
			  "it may show up as a duplicate configurable device in libratbag\n");
	}

	if (device_version > 1)
		return ratbag_hidraw_has_report(device, STEELSERIES_REPORT_ID_1);

	return true;
}

static void
button_defaults_for_layout(struct ratbag_button *button, size_t button_count)
{
	/* The default button mapping vary depending on the number of buttons
	 * on the device. */
	struct ratbag_button_action button_actions[8] = {
		BUTTON_ACTION_BUTTON(1),
		BUTTON_ACTION_BUTTON(2),
		BUTTON_ACTION_BUTTON(3),
		BUTTON_ACTION_BUTTON(4),
		BUTTON_ACTION_BUTTON(5),
		BUTTON_ACTION_BUTTON(6),
		BUTTON_ACTION_BUTTON(7),
		BUTTON_ACTION_BUTTON(8),
	};

	if (button_count <= 6) {
		button_actions[5].type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		button_actions[5].action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
		button_actions[6].type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		button_actions[7].type = RATBAG_BUTTON_ACTION_TYPE_NONE;
	} else if (button_count == 7) {
		button_actions[6].type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		button_actions[6].action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
		button_actions[7].type = RATBAG_BUTTON_ACTION_TYPE_NONE;
	} else {
		button_actions[7].type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		button_actions[7].action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
	}

	ratbag_button_set_action(button, &button_actions[button->index]);
}

static int
steelseries_get_firmware_version(struct ratbag_device *device, int *major_out, int *minor_out)
{
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	size_t msg_len;
	uint8_t buf[2] = {0};
	int ret;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	switch (device_version) {
	case 1:
		msg.msg.parameters[0] = STEELSERIES_ID_FIRMWARE_PROTOCOL1;
		msg_len = STEELSERIES_REPORT_SIZE_SHORT;
		break;
	case 2:
		msg.msg.parameters[0] = STEELSERIES_ID_FIRMWARE_PROTOCOL2;
		msg_len = STEELSERIES_REPORT_SIZE;
		break;
	case 3:
		msg.msg.parameters[0] = STEELSERIES_ID_FIRMWARE_PROTOCOL3;
		msg_len = STEELSERIES_REPORT_SIZE;
		break;
	case 4:
	default:
		return -ENOTSUP;
	}

	msleep(10);
	ret = ratbag_hidraw_output_report(device, msg.data, msg_len);
	if (ret < 0)
		return ret;

	ret = ratbag_hidraw_read_input_report_index(device, buf, sizeof(buf), STEELSERIES_INPUT_HIDRAW, NULL);
	if (ret < 0)
		return ret;

	// TODO: check if these are in correct order.
	// Rivalcfg - another configuration utility for SteelSeries mice -
	// was updated to invert their order on 2022-08-26.
	*minor_out = buf[0];
	*major_out = buf[1];

	return 0;
}

static int
steelseries_read_settings(struct ratbag_device *device)
{
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

	uint8_t buf[STEELSERIES_REPORT_SIZE] = {0};
	int ret;
	unsigned int active_resolution;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	switch (device_version) {
	case 2:
		msg.msg.parameters[0] = STEELSERIES_ID_SETTINGS;
		break;
	case 3:
		msg.msg.parameters[0] = STEELSERIES_ID_SETTINGS_PROTOCOL3;
		break;
	default:
		return -ENOTSUP;
	}

	msleep(10);
	ret = ratbag_hidraw_output_report(device, msg.data, STEELSERIES_REPORT_SIZE);
	if (ret < 0)
		return ret;

	ret = ratbag_hidraw_read_input_report_index(device, buf, STEELSERIES_REPORT_SIZE, STEELSERIES_INPUT_HIDRAW, NULL);
	if (ret < 0)
		return ret;

	if (device_version == 2) {
		active_resolution = buf[1] - 1;
		ratbag_device_for_each_profile(device, profile) {
			ratbag_profile_for_each_resolution(profile, resolution) {
				resolution->is_active = resolution->index == active_resolution;

				resolution->dpi_x = 100 * (1 +  buf[2 + resolution->index*2]);
				resolution->dpi_y = resolution->dpi_x;
			}

			ratbag_profile_for_each_led(profile, led) {
				led->color.red = buf[6 + led->index * 3];
				led->color.green = buf[7 + led->index * 3];
				led->color.blue = buf[8 + led->index * 3];
			}
		}
	} else if (device_version == 3) {
		active_resolution = buf[0] - 1;
		ratbag_device_for_each_profile(device, profile) {
			ratbag_profile_for_each_resolution(profile, resolution) {
				resolution->is_active = resolution->index == active_resolution;
			}
		}
	}

	return 0;
}

static int
steelseries_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	int device_version;
	int rc;

	static const unsigned int report_rates[] = { 125, 250, 500, 1000 };

	rc = ratbag_find_hidraw(device, steelseries_test_hidraw);
	if (rc)
		return rc;

	rc = ratbag_open_hidraw_index(device,
				      STEELSERIES_INPUT_ENDPOINT,
				      STEELSERIES_INPUT_HIDRAW);
	if (rc)
		return rc;

	device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	if (device_version == -1) {
		log_error(device->ratbag, "Device version not set\n");
		return -EINVAL;
	}

	rc = ratbag_device_data_steelseries_get_button_count(device->data);
	if (rc == -1)
		rc = 0;
	const unsigned int button_count = (unsigned int)rc;

	rc = ratbag_device_data_steelseries_get_led_count(device->data);
	if (rc == -1)
		rc = 0;
	const unsigned int led_count = (unsigned int)rc;

	const enum steelseries_quirk quirk = ratbag_device_data_steelseries_get_quirk(device->data);
	const struct dpi_list *dpilist = ratbag_device_data_steelseries_get_dpi_list(device->data);
	const struct dpi_range *dpirange = ratbag_device_data_steelseries_get_dpi_range(device->data);

	ratbag_device_init_profiles(device,
				    STEELSERIES_NUM_PROFILES,
				    STEELSERIES_NUM_DPI,
				    button_count,
				    led_count);


	/* The device does not support reading the current settings. Fall back
	   to some sensible defaults */
	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
		ratbag_profile_set_report_rate_list(profile, report_rates,
						    ARRAY_LENGTH(report_rates));
		profile->hz = 1000;

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

			/* 800 and 1600 seem as reasonable defaults supported
			 * by all known devices. */
			resolution->dpi_x = 800 * (resolution->index + 1);
			resolution->dpi_y = 800 * (resolution->index + 1);
		}

		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			if (quirk != STEELSERIES_QUIRK_SENSEIRAW) {
				ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
			}

			button_defaults_for_layout(button, button_count);
		}

		ratbag_profile_for_each_led(profile, led) {
			led->mode = RATBAG_LED_ON;
			if (quirk == STEELSERIES_QUIRK_SENSEIRAW) {
				led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;
				led->brightness = 255;
			} else {
				led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
				led->color.red = 0;
				led->color.green = 0;
				led->color.blue = 255;
			}
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			if (device_version >= 2)
				ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
		}
	}

	{
		int firmware_minor = 0;
		int firmware_major = 0;
		rc = steelseries_get_firmware_version(device, &firmware_major, &firmware_minor);
		if (rc == 0) {
			_cleanup_free_ char *fw = asprintf_safe("%d.%d", firmware_major, firmware_minor);
			ratbag_device_set_firmware_version(device, fw);
		}
	}

	rc = steelseries_read_settings(device);
	/* Some devices don't support reading settings, so ignore ENOTSUP. */
	if (rc < 0 && rc != -ENOTSUP) {
		log_error(device->ratbag, "Failed to read device settings\n");
		return rc;
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

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	dpirange = ratbag_device_data_steelseries_get_dpi_range(device->data);
	dpilist = ratbag_device_data_steelseries_get_dpi_list(device->data);

	switch (device_version) {
	case 1: {
		size_t i = 0;
		/* when using lists the entries are enumerated in reverse */
		if (dpilist) {
			for (i = 0; i < resolution->ndpis; i++) {
				if (resolution->dpis[i] == resolution->dpi_x)
					break;
			}
			i = resolution->ndpis - i;
		} else {
			i = resolution->dpi_x / (size_t)dpirange->step - 1U;
		}

		buf_len = STEELSERIES_REPORT_SIZE_SHORT;
		msg.msg.parameters[0] = STEELSERIES_ID_DPI_SHORT;
		msg.msg.parameters[1] = (uint8_t)resolution->index + 1;
		msg.msg.parameters[2] = (uint8_t)i;
		break;
	}
	case 2:
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_DPI;
		msg.msg.parameters[2] = (uint8_t)resolution->index + 1;
		msg.msg.parameters[3] = (uint8_t)(resolution->dpi_x / (size_t)dpirange->step - 1);
		msg.msg.parameters[6] = 0x42; /* not sure if needed */
		break;
	case 3:
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_DPI_PROTOCOL3;
		msg.msg.parameters[2] = (uint8_t)resolution->index + 1;
		msg.msg.parameters[3] = (uint8_t)(resolution->dpi_x / (size_t)dpirange->step - 1);
		msg.msg.parameters[5] = 0x42; /* not sure if needed */
		break;
	case 4:
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_DPI_PROTOCOL4;
		msg.msg.parameters[1] = (uint8_t)resolution->index + 1;
		msg.msg.parameters[2] = (uint8_t)(resolution->dpi_x / (size_t)dpirange->step - 1);
		break;
	default:
		return -ENOTSUP;
	}

	msleep(10);
	ret = ratbag_hidraw_output_report(device, msg.data, buf_len);
	if (ret < 0)
		return ret;

	return 0;
}

static int
steelseries_write_report_rate(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	int ret;
	size_t buf_len;
	uint8_t reported_rate = 0;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	switch (device_version) {
	case 4:
	case 1:
		if (profile->hz >= 1000) {
			profile->hz = 1000;
			reported_rate = 0x01;
		} else if (profile->hz >= 375) {
			profile->hz = 500;
			reported_rate = 0x02;
		} else if (profile->hz <= 125) {
			profile->hz = 125;
			reported_rate = 0x04;
		} else {
			profile->hz = 250;
			reported_rate = 0x03;
		}

		buf_len = STEELSERIES_REPORT_SIZE_SHORT;
		msg.msg.parameters[0] = device_version == 1 ? STEELSERIES_ID_REPORT_RATE_SHORT : STEELSERIES_ID_REPORT_RATE_PROTOCOL4;
		msg.msg.parameters[2] = reported_rate;
		break;
	case 2:
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_REPORT_RATE;
		msg.msg.parameters[2] = (uint8_t)1000 / profile->hz;
		break;
        case 3:
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_REPORT_RATE_PROTOCOL3;
		msg.msg.parameters[2] = (uint8_t)1000 / profile->hz;
		break;
	default:
		return -ENOTSUP;
	}

	msleep(10);
	ret = ratbag_hidraw_output_report(device, msg.data, buf_len);
	if (ret < 0)
		return ret;

	return 0;
}

static int
steelseries_write_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_button *button;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	int ret;

	if (ratbag_device_data_steelseries_get_macro_length(device->data) == 0)
		return 0;

	const bool is_senseiraw = ratbag_device_data_steelseries_get_quirk(device->data) == STEELSERIES_QUIRK_SENSEIRAW;
	const size_t button_size = is_senseiraw ? 3 : 5;
	const size_t report_size = is_senseiraw ? STEELSERIES_REPORT_SIZE_SHORT : STEELSERIES_REPORT_LONG_SIZE;
	const int max_modifiers = is_senseiraw ? 0 : 3;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	msg.msg.parameters[0] = STEELSERIES_ID_BUTTONS;

	ratbag_profile_for_each_button(profile, button) {
		struct ratbag_button_action *action = &button->action;
		unsigned int key;
		unsigned int modifiers;

		/* Each button takes up 3 or 5 bytes starting from index 2 */
		size_t idx = 2 + button->index * button_size;

		switch (action->type) {
		case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
			msg.msg.parameters[idx] = (uint8_t)action->action.button;
			break;

		case RATBAG_BUTTON_ACTION_TYPE_MACRO:
			ratbag_action_keycode_from_macro(action, &key, &modifiers);

			/* There is only space for 3 modifiers */
			if (__builtin_popcount(modifiers) > max_modifiers) {
				log_error(device->ratbag,
					"Too many modifiers in macro for button %d (maximum %d)\n",
					button->index, max_modifiers);
				break;
			}

			const uint8_t keyboard_code = ratbag_hidraw_get_keyboard_usage_from_keycode(
						device, key);
			if (keyboard_code) {
				if (is_senseiraw) {
					msg.msg.parameters[idx] = STEELSERIES_BUTTON_KEY;
				} else {
					msg.msg.parameters[idx] = STEELSERIES_BUTTON_KBD;

					if (modifiers & MODIFIER_LEFTCTRL)
						msg.msg.parameters[++idx] = 0xE0;
					if (modifiers & MODIFIER_LEFTSHIFT)
						msg.msg.parameters[++idx] = 0xE1;
					if (modifiers & MODIFIER_LEFTALT)
						msg.msg.parameters[++idx] = 0xE2;
					if (modifiers & MODIFIER_LEFTMETA)
						msg.msg.parameters[++idx] = 0xE3;
					if (modifiers & MODIFIER_RIGHTCTRL)
						msg.msg.parameters[++idx] = 0xE4;
					if (modifiers & MODIFIER_RIGHTSHIFT)
						msg.msg.parameters[++idx] = 0xE5;
					if (modifiers & MODIFIER_RIGHTALT)
						msg.msg.parameters[++idx] = 0xE6;
					if (modifiers & MODIFIER_RIGHTMETA)
						msg.msg.parameters[++idx] = 0xE7;
				}

				msg.msg.parameters[idx + 1] = keyboard_code;
			} else {
				const uint16_t consumer_code = ratbag_hidraw_get_consumer_usage_from_keycode(
						device, action->action.key);
				msg.msg.parameters[idx] = STEELSERIES_BUTTON_CONSUMER;
				msg.msg.parameters[idx + 1] = (uint8_t)consumer_code;
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
			switch (action->action.special) {
			case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
				msg.msg.parameters[idx] = STEELSERIES_BUTTON_RES_CYCLE;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
				msg.msg.parameters[idx] = STEELSERIES_BUTTON_WHEEL_UP;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
				msg.msg.parameters[idx] = STEELSERIES_BUTTON_WHEEL_DOWN;
				break;
			default:
				break;
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_NONE:
		default:
			msg.msg.parameters[idx] = STEELSERIES_BUTTON_OFF;
			break;
		}
	}

	msleep(10);
	if (device_version == 3)
		ret = ratbag_hidraw_raw_request(device, STEELSERIES_ID_BUTTONS,
						msg.msg.parameters, report_size - 1,
						HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	else
		ret = ratbag_hidraw_output_report(device, msg.data, report_size);

	if (ret < 0)
		return ret;

	return 0;
}

static int
steelseries_write_led_v1(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	const enum steelseries_quirk quirk = ratbag_device_data_steelseries_get_quirk(device->data);
	int ret;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	msg.msg.parameters[0] = STEELSERIES_ID_LED_EFFECT_SHORT;
	msg.msg.parameters[1] = quirk == STEELSERIES_QUIRK_RIVAL100 ? 0x00 : (uint8_t)led->index + 1;

	switch(led->mode) {
	case RATBAG_LED_OFF:
	case RATBAG_LED_ON:
		msg.msg.parameters[2] = 0x01;
		break;
	case RATBAG_LED_BREATHING:
		/* 0x2/3/4 - speed (by eye it's 3, 5 and 7 seconds) */
		if (led->ms <= 3000) {
			led->ms = 3000;
			msg.msg.parameters[2] = 0x04;
		} else if (led->ms <= 5000) {
			led->ms = 5000;
			msg.msg.parameters[2] = 0x03;
		} else {
			led->ms = 7000;
			msg.msg.parameters[2] = 0x02;
		}
		break;
	case RATBAG_LED_CYCLE:
		/* Cycle mode is not supported on this version */
	default:
		return -EINVAL;
	}

	msleep(10);
	ret = ratbag_hidraw_output_report(device, msg.data, STEELSERIES_REPORT_SIZE_SHORT);
	if (ret < 0)
		return ret;

	memset(msg.data, 0, STEELSERIES_REPORT_SIZE); // reset the msg buffer before reusing

	if (quirk == STEELSERIES_QUIRK_SENSEIRAW) {
		msg.msg.parameters[0] = STEELSERIES_ID_LED_INTENSITY_SHORT;
		msg.msg.parameters[1] = (uint8_t)led->index + 1;
		if (led->mode == RATBAG_LED_OFF || led->brightness == 0) {
			msg.msg.parameters[2] = 1;
		} else {
			//split the brightness into roughly 3 equal intensities
			msg.msg.parameters[2] = (led->brightness / 86) + 2;
		}
	} else {
		if (quirk != STEELSERIES_QUIRK_RIVAL100) {
			msg.msg.parameters[0] = STEELSERIES_ID_LED_COLOR_SHORT;
			msg.msg.parameters[1] = (uint8_t)led->index + 1;
		} else {
			msg.msg.parameters[0] = STEELSERIES_ID_LED_COLOR_SHORT_RIVAL100;
			msg.msg.parameters[1] = 0x00;
		}
		msg.msg.parameters[2] = led->color.red;
		msg.msg.parameters[3] = led->color.green;
		msg.msg.parameters[4] = led->color.blue;
	}

	msleep(10);
	ret = ratbag_hidraw_output_report(device, msg.data, STEELSERIES_REPORT_SIZE_SHORT);
	if (ret < 0)
		return ret;

	return 0;
}

static void
create_cycle(struct steelseries_led_cycle *cycle)
{
	cycle->led_id = 0x00;
	cycle->duration = 5000;
	cycle->repeat = true;
	cycle->trigger_buttons = 0x00;

	list_init(&cycle->points);
}

static void
construct_cycle_buffer(struct steelseries_led_cycle *cycle,
		       struct steelseries_led_cycle_spec *spec,
		       uint8_t *buf,
		       uint8_t buf_size)
{
	struct steelseries_point *point;
	uint16_t duration;
	uint8_t npoints = 0;
	uint16_t cycle_size = 0;

	buf[0] = spec->cmd_val;
	buf[spec->led_id_idx] = cycle->led_id;
	if (spec->has_2_led_ids)
		buf[spec->led_id2_idx] = cycle->led_id;

	if (!cycle->repeat)
		buf[spec->repeat_idx] = 0x01;

	buf[spec->trigger_idx] = cycle->trigger_buttons;

	int color_idx = spec->header_len;

	list_for_each(point, &cycle->points, link) {
		if(npoints == 0){
			/* this is not a mistake,
				we need to write the first
				point as the first data after the header */
			buf[color_idx++] = point->color.red;
			buf[color_idx++] = point->color.green;
			buf[color_idx++] = point->color.blue;
		}

		cycle_size += point->pos;
		assert(cycle_size < 256);
		assert(34 + npoints*4 <= buf_size);

		buf[(color_idx  ) + npoints*4] = point->color.red;
		buf[(color_idx+1) + npoints*4] = point->color.green;
		buf[(color_idx+2) + npoints*4] = point->color.blue;
		buf[(color_idx+3) + npoints*4] = point->pos;

		npoints++;
	}

	buf[spec->point_count_idx] = npoints;

	/* this seems to be the minimum allowed */
	duration = max((uint16_t)(buf[spec->point_count_idx] * 330), cycle->duration);

	set_unaligned_le_u16(&buf[spec->duration_idx], duration);
}

static int
steelseries_write_led_cycle(struct ratbag_led *led,
			    struct steelseries_led_cycle_spec *cycle_spec)
{
	struct ratbag_device *device = led->profile->device;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	int ret;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	struct steelseries_led_cycle cycle;
	struct steelseries_point point[4];

	const struct ratbag_color black  = { 0x00 };
	const struct ratbag_color red = { 0xFF, 0x00, 0x00 };
	const struct ratbag_color green = { 0x00, 0xFF, 0x00 };
	const struct ratbag_color blue = { 0x00, 0x00, 0xFF };

	create_cycle(&cycle);
	cycle.led_id = (uint8_t)led->index;

	switch(led->mode) {
	case RATBAG_LED_OFF:
		cycle.repeat = false;

		point[0].color = black;
		point[0].pos = 0x00;

		list_append(&cycle.points, &point[0].link);
		break;
	case RATBAG_LED_ON:
		cycle.repeat = false;

		point[0].color = led->color;
		point[0].pos = 0x00;

		list_append(&cycle.points, &point[0].link);
		break;
	case RATBAG_LED_CYCLE:
		point[0].color = red;
		point[0].pos = 0x00;

		point[1].color = green;
		point[1].pos = 0x55;

		point[2].color = blue;
		point[2].pos = 0x55;

		point[3].color = red;
		point[3].pos = 0x55;

		list_append(&cycle.points, &point[0].link);
		list_append(&cycle.points, &point[1].link);
		list_append(&cycle.points, &point[2].link);
		list_append(&cycle.points, &point[3].link);

		cycle.duration = (uint16_t)led->ms;
		break;
	case RATBAG_LED_BREATHING:
		point[0].color = black;
		point[0].pos = 0x00;

		point[1].color = led->color;
		point[1].pos = 0x7F;

		point[2].color = black;
		point[2].pos = 0x7F;

		list_append(&cycle.points, &point[0].link);
		list_append(&cycle.points, &point[1].link);
		list_append(&cycle.points, &point[2].link);

		cycle.duration = (uint16_t)led->ms;
		break;
	default:
		return -EINVAL;
	}

	construct_cycle_buffer(&cycle, cycle_spec, msg.msg.parameters, sizeof(msg.msg.parameters));

	msleep(10);
	if (device_version == 3)
		ret = ratbag_hidraw_raw_request(device, cycle_spec->cmd_val, msg.msg.parameters,
						sizeof(msg.msg.parameters), cycle_spec->hid_report_type,
						HID_REQ_SET_REPORT);
	else
		ret = ratbag_hidraw_output_report(device, msg.data, sizeof(msg.data));

	if (ret < 0)
		return ret;

	return 0;
}

static int
steelseries_write_led_v2(struct ratbag_led *led)
{
	struct steelseries_led_cycle_spec spec;

	spec.hid_report_type = HID_OUTPUT_REPORT;
	spec.header_len = 28;
	spec.cmd_val = STEELSERIES_ID_LED;
	spec.has_2_led_ids = false;
	spec.led_id_idx = 2;
	spec.duration_idx = 3;
	spec.repeat_idx = 19;
	spec.trigger_idx = 23;
	spec.point_count_idx = 27;

	return steelseries_write_led_cycle(led, &spec);
}

static int
steelseries_write_led_v3(struct ratbag_led *led)
{
	struct steelseries_led_cycle_spec spec;

	spec.hid_report_type = HID_FEATURE_REPORT;
	spec.header_len = 30;
	spec.cmd_val = STEELSERIES_ID_LED_PROTOCOL3;
	spec.has_2_led_ids = true;
	spec.led_id_idx = 2;
	spec.led_id2_idx = 7;
	spec.duration_idx = 8;
	spec.repeat_idx = 24;
	spec.trigger_idx = 25;
	spec.point_count_idx = 29;

	return steelseries_write_led_cycle(led, &spec);
}

static int
steelseries_write_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);

	if (device_version == 1)
		return steelseries_write_led_v1(led);
	if (device_version == 2)
		return steelseries_write_led_v2(led);
	if (device_version == 3)
		return steelseries_write_led_v3(led);

	return -ENOTSUP;
}

static int
steelseries_write_save(struct ratbag_device *device)
{
	int device_version = ratbag_device_data_steelseries_get_device_version(device->data);
	int ret;
	size_t buf_len;

	union steelseries_message msg = {
		.msg.report_id = STEELSERIES_REPORT_ID,
		.msg.parameters = {0},
	};

	if (device_version == 1) {
		buf_len = STEELSERIES_REPORT_SIZE_SHORT;
		msg.msg.parameters[0] = STEELSERIES_ID_SAVE_SHORT;
	} else if (device_version == 2) {
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_SAVE;
	} else if (device_version == 3 || device_version == 4) {
		buf_len = STEELSERIES_REPORT_SIZE;
		msg.msg.parameters[0] = STEELSERIES_ID_SAVE_PROTOCOL3;
	} else {
		return -ENOTSUP;
	}

	msleep(20);
	ret = ratbag_hidraw_output_report(device, msg.data, buf_len);
	if (ret < 0)
		return ret;

	return 0;
}

static int
steelseries_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_device *device = profile->device;
	struct ratbag_led *led;
	int rc;
	bool buttons_dirty = false;

	if (profile->rate_dirty) {
		log_debug(device->ratbag,
			  "Report rate changed, rewriting\n");

		rc = steelseries_write_report_rate(profile);
		if (rc != 0) {
			log_error(device->ratbag,
				  "Failed to write report rate: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}
	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dirty)
			continue;

		log_debug(device->ratbag,
			  "Resolution %d changed, rewriting\n", resolution->index);

		rc = steelseries_write_dpi(resolution);
		if (rc != 0) {
			log_error(device->ratbag,
				  "Failed to write resolution: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}

		/* The same hz is used for all resolutions. Only write once. */
		break;
	}

	ratbag_profile_for_each_button(profile, button) {
		if (button->dirty)
			buttons_dirty = true;

		log_debug(device->ratbag,
			  "Button %d changed, rewriting\n", button->index);
	}

	if (buttons_dirty) {
		rc = steelseries_write_buttons(profile);
		if (rc != 0) {
			log_error(device->ratbag,
				  "Failed to write buttons: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}
	}

	ratbag_profile_for_each_led(profile, led) {
		if (!led->dirty)
			continue;

		log_debug(device->ratbag,
			  "LED %d changed, rewriting\n", led->index);

		rc = steelseries_write_led(led);
		if (rc != 0) {
			log_error(device->ratbag,
				  "Failed to write LED: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}
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
		if (rc) {
			log_error(device->ratbag,
				  "Failed to write profile: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}

		/* persist the current settings on the device */

		rc = steelseries_write_save(device);
		if (rc) {
			log_error(device->ratbag,
				  "Failed to save profile: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}
	}

	return 0;
}

static void
steelseries_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw_index(device, 0);
	ratbag_close_hidraw_index(device, 1);
}

struct ratbag_driver steelseries_driver = {
	.name = "SteelSeries",
	.id = "steelseries",
	.probe = steelseries_probe,
	.remove = steelseries_remove,
	.commit = steelseries_commit,
};
