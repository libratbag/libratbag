/*
 * Copyright (C) 2021 Kyoken, kyoken@kyoken.ninja
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
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-data.h"

#include "asus.h"

/* ButtonMapping configuration property defaults */
static int ASUS_CONFIG_BUTTON_MAPPING[] = {
	0xf0,  /* left */
	0xf1,  /* right (button 3 in xev) */
	0xf2,  /* middle (button 2 in xev) */
	0xe4,  /* backward */
	0xe5,  /* forward */
	0xe6,  /* DPI */
	0xe8,  /* wheel up */
	0xe9,  /* wheel down */
};

/* LedModes configuration property defaults */
static unsigned int ASUS_LED_MODE[] = {
	RATBAG_LED_ON,
	RATBAG_LED_BREATHING,
	RATBAG_LED_CYCLE,
	RATBAG_LED_ON,  /* rainbow wave */
	RATBAG_LED_ON,  /* reactive - react to clicks */
	RATBAG_LED_ON,  /* custom - depends on mouse type */
	RATBAG_LED_ON,  /* battery - battery indicator */
};

struct asus_data {
	uint8_t is_ready;
	int button_mapping[ASUS_MAX_NUM_BUTTON * ASUS_MAX_NUM_BUTTON_GROUP];
	int button_indices[ASUS_MAX_NUM_BUTTON * ASUS_MAX_NUM_BUTTON_GROUP];
	int led_modes[ASUS_MAX_NUM_LED_MODES];
};

static int
asus_driver_load_profile(struct ratbag_device *device, struct ratbag_profile *profile, int dpi_preset)
{
	int rc;
	const struct _asus_binding *asus_binding;
	const struct _asus_led *asus_led;
	const struct asus_button *asus_button;
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;
	union asus_binding_data binding_data;
	union asus_binding_data binding_data_secondary;
	union asus_led_data led_data;
	union asus_resolution_data resolution_data;
	union asus_resolution_data xy_resolution_data;
	unsigned int dpi_count = ratbag_device_get_profile(device, 0)->num_resolutions;
	unsigned int led_count = ratbag_device_get_num_leds(device);
	uint32_t quirks = ratbag_device_data_asus_get_quirks(device->data);
	struct asus_data *drv_data = ratbag_get_drv_data(device);

	/* get buttons */

	log_debug(device->ratbag, "Loading buttons data\n");
	rc = asus_get_binding_data(device, &binding_data, 0);
	if (rc)
		return rc;

	if (quirks & ASUS_QUIRK_BUTTONS_SECONDARY) {
		rc = asus_get_binding_data(device, &binding_data_secondary, 1);
		if (rc)
			return rc;
	}

	ratbag_profile_for_each_button(profile, button) {
		int asus_index = drv_data->button_indices[button->index];
		if (asus_index == -1) {
			log_debug(device->ratbag, "No mapping for button %d\n", button->index);
			continue;
		}

		if (asus_index < ASUS_MAX_NUM_BUTTON) {
			asus_binding = &binding_data.data.binding[asus_index];
		} else {
			asus_binding = &binding_data_secondary.data.binding[asus_index % ASUS_MAX_NUM_BUTTON];
		}

		/* disabled */
		if (asus_binding->action == ASUS_BUTTON_CODE_DISABLED) {
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
			continue;
		}

		/* got action */
		switch (asus_binding->type) {
		case ASUS_BUTTON_ACTION_TYPE_KEY:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
			rc = asus_get_linux_key_code(asus_binding->action);
			if (rc > 0) {
				button->action.action.key = (unsigned int)rc;
			} else {
				log_debug(device->ratbag, "Unknown button code %02x\n", asus_binding->action);
			}
			break;

		case ASUS_BUTTON_ACTION_TYPE_BUTTON:
			asus_button = asus_find_button_by_code(asus_binding->action);
			if (asus_button != NULL) {  /* found button to bind to */
				button->action.type = asus_button->type;
				if (asus_button->type == RATBAG_BUTTON_ACTION_TYPE_BUTTON)
					button->action.action.button = asus_button->button;
				else if (asus_button->type == RATBAG_BUTTON_ACTION_TYPE_SPECIAL)
					button->action.action.special = asus_button->special;
			} else {
				log_debug(device->ratbag, "Unknown action code %02x\n", asus_binding->action);
			}
			break;

		default:
			break;
		}
	}

	/* get DPIs */

	log_debug(device->ratbag, "Loading resolutions data\n");
	rc = asus_get_resolution_data(device, &resolution_data, false);
	if (rc)
		return rc;

	if (quirks & ASUS_QUIRK_SEPARATE_XY_DPI) {
		rc = asus_get_resolution_data(device, &xy_resolution_data, true);
		if (rc)
			return rc;
	}

	switch (dpi_count) {
	case 2:  /* 2 DPI presets */
		profile->hz = resolution_data.data2.rate;
		profile->angle_snapping = resolution_data.data2.snapping;
		profile->debounce = resolution_data.data2.response;
		ratbag_profile_for_each_resolution(profile, resolution) {
			if (quirks & ASUS_QUIRK_SEPARATE_XY_DPI) {
				ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
				ratbag_resolution_set_resolution(
					resolution,
					xy_resolution_data.data_xy.dpi[resolution->index].x,
					xy_resolution_data.data_xy.dpi[resolution->index].y);
			} else {
				ratbag_resolution_set_resolution(
					resolution,
					resolution_data.data2.dpi[resolution->index],
					resolution_data.data2.dpi[resolution->index]);
			}
			if (dpi_preset != -1 && (unsigned int)dpi_preset == resolution->index)
				resolution->is_active = true;
		}
		break;

	case 4:  /* 4 DPI presets */
		profile->hz = resolution_data.data4.rate;
		profile->angle_snapping = resolution_data.data4.snapping;
		profile->debounce = resolution_data.data4.response;
		ratbag_profile_for_each_resolution(profile, resolution) {
			if (quirks & ASUS_QUIRK_SEPARATE_XY_DPI) {
				ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
				ratbag_resolution_set_resolution(
					resolution,
					xy_resolution_data.data_xy.dpi[resolution->index].x,
					xy_resolution_data.data_xy.dpi[resolution->index].y);
			} else {
				ratbag_resolution_set_resolution(
					resolution,
					resolution_data.data4.dpi[resolution->index],
					resolution_data.data4.dpi[resolution->index]);
			}
			if (dpi_preset != -1 && (unsigned int)dpi_preset == resolution->index)
				resolution->is_active = true;
		}
		break;

	default:
		break;
	}

	/* get LEDs */

	if (!(quirks & ASUS_QUIRK_SEPARATE_LEDS) && led_count) {
		log_debug(device->ratbag, "Loading LEDs data\n");
		rc = asus_get_led_data(device, &led_data, 0);
		if (rc)
			return rc;
	}

	ratbag_profile_for_each_led(profile, led) {
		if (quirks & ASUS_QUIRK_SEPARATE_LEDS) {
			log_debug(device->ratbag, "Loading LED %d data\n", led->index);
			rc = asus_get_led_data(device, &led_data, led->index);
			if (rc)
				return rc;
			asus_led = &led_data.data.led[0];
		} else {
			asus_led = &led_data.data.led[led->index];
		}

		led->mode = drv_data->led_modes[asus_led->mode];
		if (quirks & ASUS_QUIRK_RAW_BRIGHTNESS) {
			led->brightness = asus_led->brightness;
		} else {
			/* convert brightness from 0-4 to 0-256 */
			led->brightness = asus_led->brightness * 64;
		}
		led->color.red = asus_led->r;
		led->color.green = asus_led->g;
		led->color.blue = asus_led->b;
	}

	return 0;
}

static int
asus_driver_save_profile(struct ratbag_device *device, struct ratbag_profile *profile)
{
	int rc = 0;
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;
	struct asus_data *drv_data = ratbag_get_drv_data(device);
	uint32_t quirks = ratbag_device_data_asus_get_quirks(device->data);

	/* set buttons */
	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;

		int asus_index = drv_data->button_indices[button->index];
		if (asus_index == -1) {
			log_debug(device->ratbag, "No mapping for button %d\n", button->index);
			continue;
		}

		const struct asus_button *asus_button;
		uint8_t asus_code_src;
		uint8_t asus_code_dst;
		bool is_joystick;

		rc = drv_data->button_mapping[asus_index];
		if (rc == -1) {
			log_debug(device->ratbag, "No mapping for button %d\n", button->index);
			continue;
		}

		asus_code_src = (uint8_t)rc;
		log_debug(device->ratbag, "Button %d (%02x) changed\n",
			  button->index, asus_code_src);

		switch (button->action.type) {
		case RATBAG_BUTTON_ACTION_TYPE_NONE:
			rc = asus_set_button_action(
				device, asus_code_src, ASUS_BUTTON_CODE_DISABLED,
				ASUS_BUTTON_ACTION_TYPE_BUTTON);
			break;

		case RATBAG_BUTTON_ACTION_TYPE_KEY:
			/* Linux code to ASUS code */
			rc = asus_find_key_code(button->action.action.key);
			if (rc != -1) {
				asus_code_dst = (uint8_t)rc;
				rc = asus_set_button_action(
					device, asus_code_src, asus_code_dst,
					ASUS_BUTTON_ACTION_TYPE_KEY);
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
			/* ratbag action to ASUS code */
			is_joystick = asus_code_is_joystick(asus_code_src);
			if (is_joystick) {
				asus_button = asus_find_button_by_action(button->action, true);
				if (asus_button != NULL) {  /* found button to bind to */
					rc = asus_set_button_action(
						device, asus_code_src, asus_button->asus_code,
						ASUS_BUTTON_ACTION_TYPE_BUTTON);
					break;
				}
			}

			asus_button = asus_find_button_by_action(button->action, false);
			if (asus_button != NULL)  /* found button to bind to */
				rc = asus_set_button_action(
					device, asus_code_src, asus_button->asus_code,
					ASUS_BUTTON_ACTION_TYPE_BUTTON);
			break;

		default:
			break;
		}

		if (rc)
			return rc;
	}

	/* set extra settings */
	if (profile->rate_dirty) {
		log_debug(device->ratbag, "Polling rate changed to %d Hz\n", profile->hz);
		rc = asus_set_polling_rate(device, profile->hz);
		if (rc)
			return rc;
	}
	if (profile->angle_snapping_dirty) {
		log_debug(device->ratbag, "Angle snapping changed to %d\n", profile->angle_snapping);
		rc = asus_set_angle_snapping(device, profile->angle_snapping);
		if (rc)
			return rc;
	}
	if (profile->debounce_dirty) {
		log_debug(device->ratbag, "Debounce time changed to %d\n", profile->debounce);
		rc = asus_set_button_response(device, profile->debounce);
		if (rc)
			return rc;
	}

	/* set DPIs */
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dirty)
			continue;

		log_debug(
			device->ratbag, "Resolution %d changed to %d\n",
			resolution->index, resolution->dpi_x);

		rc = asus_set_dpi(device, resolution->index, resolution->dpi_x);
		if (rc)
			return rc;
	}

	/* set LEDs */
	ratbag_profile_for_each_led(profile, led) {
		if (!led->dirty)
			continue;

		log_debug(device->ratbag, "LED %d changed\n", led->index);
		uint8_t led_mode = 0;
		for (unsigned int i = 0; i < ASUS_MAX_NUM_LED_MODES; i++) {
			if (drv_data->led_modes[i] == (int) led->mode) {
				led_mode = i;
				break;
			}
		}

		uint8_t led_brightness;
		if (quirks & ASUS_QUIRK_RAW_BRIGHTNESS) {
			led_brightness = led->brightness;
		} else {
			/* convert brightness from 0-256 to 0-4 */
			led_brightness = (uint8_t)round((double)led->brightness / 64.0);
		}
		rc = asus_set_led(device, led->index, led_mode, led_brightness, led->color);
		if (rc)
			return rc;
	}

	return 0;
}

static int
asus_driver_load_profiles(struct ratbag_device *device)
{
	int rc;
	struct asus_profile_data profile_data;
	struct ratbag_profile *profile;
	unsigned int current_profile_id = 0;

	/* get current profile id */
	rc = asus_get_profile_data(device, &profile_data);
	if (rc)
		return rc;

	if (device->num_profiles > 1) {
		current_profile_id = profile_data.profile_id;
		log_debug(device->ratbag, "Initial profile is %d\n", current_profile_id);
	}

	log_debug(
		device->ratbag, "Primary version %02X.%02X.%02X\n",
		profile_data.version_primary_major,
		profile_data.version_primary_minor,
		profile_data.version_primary_build);
	log_debug(
		device->ratbag, "Secondary version %02X.%02X.%02X\n",
		profile_data.version_secondary_major,
		profile_data.version_secondary_minor,
		profile_data.version_secondary_build);

	/* read ratbag profiles */
	ratbag_device_for_each_profile(device, profile) {
		if (profile->index == current_profile_id) {  /* profile is already selected */
			profile->is_active = true;
		} else {  /* switch profile */
			profile->is_active = false;

			log_debug(device->ratbag, "Switching to profile %d\n", profile->index);
			rc = asus_set_profile(device, profile->index);
			if (rc)
				return rc;
		}

		rc = asus_driver_load_profile(device, profile, profile_data.dpi_preset);
		if (rc)
			return rc;
	}

	/* back to initial profile */
	if (device->num_profiles > 1) {
		log_debug(device->ratbag, "Switching back to initial profile %d\n", current_profile_id);
		rc = asus_set_profile(device, current_profile_id);
		if (rc)
			return rc;
	}

	return 0;
}

static int
asus_driver_save_profiles(struct ratbag_device *device)
{
	int rc;
	struct asus_profile_data profile_data;
	struct ratbag_profile *profile;
	unsigned int current_profile_id = 0;

	/* get current profile id */
	if (device->num_profiles > 1) {
		rc = asus_get_profile_data(device, &profile_data);
		if (rc)
			return rc;

		current_profile_id = profile_data.profile_id;
		log_debug(device->ratbag, "Initial profile is %d\n", current_profile_id);
	}

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		log_debug(device->ratbag, "Profile %d changed\n", profile->index);

		/* switch profile */
		if (profile->index != current_profile_id) {
			log_debug(device->ratbag, "Switching to profile %d\n", profile->index);
			rc = asus_set_profile(device, profile->index);
			if (rc)
				return rc;
		}

		rc = asus_driver_save_profile(device, profile);
		if (rc)
			return rc;

		/* save profile */
		log_debug(device->ratbag, "Saving profile\n");
		rc = asus_save_profile(device);
		if (rc)
			return rc;
	}

	/* back to initial profile */
	if (device->num_profiles > 1) {
		log_debug(device->ratbag, "Switching back to initial profile %d\n", current_profile_id);
		rc = asus_set_profile(device, current_profile_id);
		if (rc)
			return rc;
	}

	return 0;
}

static int
asus_driver_probe(struct ratbag_device *device)
{
	int rc;
	unsigned int profile_count, dpi_count, button_count, led_count;
	const struct asus_button *asus_button;
	struct asus_data *drv_data;
	struct asus_profile_data profile_data;
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

	rc = ratbag_open_hidraw(device);
	if (rc)
		return rc;

	rc = asus_get_profile_data(device, &profile_data);
	if (rc) {
		ratbag_close_hidraw(device);
		return -ENODEV;
	}

	/* create device state data */
	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);
	drv_data->is_ready = 1;

	/* get device properties from configuration file */
	profile_count = ratbag_device_data_asus_get_profile_count(device->data);
	dpi_count = ratbag_device_data_asus_get_dpi_count(device->data);
	button_count = ratbag_device_data_asus_get_button_count(device->data);
	led_count = ratbag_device_data_asus_get_led_count(device->data);
	const int *bm = ratbag_device_data_asus_get_button_mapping(device->data);
	const int *led_modes = ratbag_device_data_asus_get_led_modes(device->data);

	/* merge ButtonMapping configuration property with defaults */
	for (unsigned int i = 0; i < ASUS_MAX_NUM_BUTTON * ASUS_MAX_NUM_BUTTON_GROUP; i++) {
		if (bm[i] != -1) {
			drv_data->button_mapping[i] = bm[i];
		} else {
			if (i < ARRAY_LENGTH(ASUS_CONFIG_BUTTON_MAPPING)) {
				drv_data->button_mapping[i] = ASUS_CONFIG_BUTTON_MAPPING[i];
			} else {
				drv_data->button_mapping[i] = -1;
			}
		}
		drv_data->button_indices[i] = -1;
	}

	/* merge LedModes configuration property with defaults */
	for (unsigned int i = 0; i < ASUS_MAX_NUM_LED_MODES; i++)
		drv_data->led_modes[i] = (led_modes[i] != -1) ? led_modes[i] : (int) ASUS_LED_MODE[i];

	/* setup a lookup table for all defined buttons */
	unsigned int button_index = 0;
	ARRAY_FOR_EACH(ASUS_BUTTON_MAPPING, asus_button) {
		/* search for this button in the ButtonMapping by it's ASUS code */
		for (unsigned int i = 0; i < ASUS_MAX_NUM_BUTTON * ASUS_MAX_NUM_BUTTON_GROUP; i++) {
			if (drv_data->button_mapping[i] == (int)asus_button->asus_code) {
				/* add button to indices array */
				drv_data->button_indices[button_index] = (int)i;
				log_debug(device->ratbag, "Button %d is mapped to 0x%02x at position %d group %d\n",
					  button_index, (uint8_t)drv_data->button_mapping[i],
					  i % ASUS_MAX_NUM_BUTTON, i / ASUS_MAX_NUM_BUTTON);
				button_index++;
				break;
			}
		}
	}

	/* init profiles */
	ratbag_device_init_profiles(
		device,
		max(profile_count, 1),
		max(dpi_count, 2),
		max(button_count, 8),
		max(led_count, 0));

	/* setup profiles */
	ratbag_device_for_each_profile(device, profile) {
		if (profile->index == 0) {
			profile->is_active = true;
		}

		asus_setup_profile(device, profile);

		ratbag_profile_for_each_button(profile, button)
			asus_setup_button(device, button);

		ratbag_profile_for_each_resolution(profile, resolution)
			asus_setup_resolution(device, resolution);

		ratbag_profile_for_each_led(profile, led)
			asus_setup_led(device, led);
	}

	/* load profiles */
	rc = asus_driver_load_profiles(device);
	if (rc == ASUS_STATUS_ERROR) {  /* mouse in invalid state */
		drv_data->is_ready = 0;
	} else if (rc) {  /* other errors */
		log_error(
			device->ratbag, "Can't talk to the mouse: '%s' (%d)\n",
			strerror(-rc), rc);
		free(drv_data);
		ratbag_set_drv_data(device, NULL);
		return -ENODEV;
	}

	return 0;
}

static void
asus_driver_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

static int
asus_driver_commit(struct ratbag_device *device)
{
	int rc;
	struct asus_data *drv_data;

	/* check last device state */
	drv_data = ratbag_get_drv_data(device);
	if (!drv_data->is_ready) {  /* device was not ready */
		log_error(device->ratbag, "Device was not ready, trying to reload\n");
		rc = asus_driver_load_profiles(device);
		if (rc) {
			log_error(device->ratbag, "Device reloading failed (%d)\n", rc);
			if (rc != ASUS_STATUS_ERROR)
				return rc;
		} else {
			drv_data->is_ready = 1;
			log_error(device->ratbag, "Device was successfully reloaded\n");
		}
		return RATBAG_ERROR_DEVICE;  /* fail in any case because we tried to rollback instead of commit */
	}

	/* save profiles */
	rc = asus_driver_save_profiles(device);
	if (rc) {
		log_error(device->ratbag, "Commit failed (%d)\n", rc);
		if (rc != ASUS_STATUS_ERROR)
			return rc;
		return RATBAG_ERROR_DEVICE;
	}

	return 0;
}

struct ratbag_driver asus_driver = {
	.name = "ASUS",
	.id = "asus",
	.probe = asus_driver_probe,
	.remove = asus_driver_remove,
	.commit = asus_driver_commit,
	.set_active_profile = asus_set_profile,
};
