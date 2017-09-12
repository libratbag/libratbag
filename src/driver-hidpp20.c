/*
 * Copyright 2013-2015 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013-2015 Red Hat, Inc
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
 * Based on the HID++ 1.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

/*
 * for this driver to work, you need a kernel >= v3.19 or one which contains
 * 925f0f3ed24f98b40c28627e74ff3e7f9d1e28bc ("HID: logitech-dj: allow transfer
 * of HID++ reports from/to the correct dj device")
 */

#include "config.h"

#include <linux/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp20.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-data.h"

#define HIDPP_CAP_RESOLUTION_2200			(1 << 0)
#define HIDPP_CAP_SWITCHABLE_RESOLUTION_2201		(1 << 1)
#define HIDPP_CAP_BUTTON_KEY_1b04			(1 << 2)
#define HIDPP_CAP_BATTERY_LEVEL_1000			(1 << 3)
#define HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00		(1 << 4)
#define HIDPP_CAP_COLOR_LED_EFFECTS_8070			(1 << 5)
#define HIDPP_CAP_ONBOARD_PROFILES_8100			(1 << 6)

struct hidpp20drv_data {
	struct hidpp20_device *dev;
	unsigned long capabilities;
	unsigned num_sensors;
	struct hidpp20_sensor *sensors;
	unsigned num_controls;
	struct hidpp20_control_id *controls;
	struct hidpp20_profiles *profiles;
	struct hidpp20_color_led_zone_info *led_infos;

	unsigned int num_profiles;
	unsigned int num_resolutions;
	unsigned int num_buttons;
	unsigned int num_leds;
};

static void
hidpp20drv_read_button_1b04(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_control_id *control;
	const struct ratbag_button_action *action;
	uint16_t mapping;

	if (!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04))
		return;

	control = &drv_data->controls[button->index];
	mapping = control->control_id;
	if (control->reporting.divert || control->reporting.persist)
		mapping = control->reporting.remapped;
	log_raw(device->ratbag,
		  " - button%d: %s (%02x) %s%s:%d\n",
		  button->index,
		  hidpp20_1b04_get_logical_mapping_name(mapping),
		  mapping,
		  control->reporting.divert || control->reporting.persist ? "(redirected) " : "",
		  __FILE__, __LINE__);
	button->type = hidpp20_1b04_get_physical_mapping(control->task_id);
	action = hidpp20_1b04_get_logical_mapping(mapping);
	if (action)
		button->action = *action;

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
}

static unsigned int
hidpp20drv_read_macro_key_8100(struct ratbag_device *device, union hidpp20_macro_data *macro)
{
	switch (macro->key.modifier) {
	case 0x01: return KEY_LEFTCTRL;
	case 0x02: return KEY_LEFTSHIFT;
	case 0x04: return KEY_LEFTALT;
	case 0x08: return KEY_LEFTMETA;
	case 0x10: return KEY_RIGHTCTRL;
	case 0x20: return KEY_RIGHTSHIFT;
	case 0x40: return KEY_RIGHTALT;
	case 0x80: return KEY_RIGHTMETA;
	}

	return ratbag_hidraw_get_keycode_from_keyboard_usage(device, macro->key.key);
}

static int
hidpp20drv_read_macro_8100(struct ratbag_button *button,
			   struct hidpp20_profile *profile,
			   union hidpp20_button_binding *binding)
{
	struct ratbag_device *device = button->profile->device;
	struct ratbag_button_macro *m;
	union hidpp20_macro_data *macro;
	unsigned int i, keycode;
	bool delay = true;

	macro = profile->macros[binding->macro.page];

	if (!macro)
		return -EINVAL;

	i = 0;

	m = ratbag_button_macro_new("macro");

	while (macro && macro->any.type != HIDPP20_MACRO_END && i < MAX_MACRO_EVENTS) {
		switch (macro->any.type) {
		case HIDPP20_MACRO_DELAY:
			ratbag_button_macro_set_event(m,
						      i++,
						      RATBAG_MACRO_EVENT_WAIT,
						      macro->delay.time);
			delay = true;
			break;
		case HIDPP20_MACRO_KEY_PRESS:
			keycode = hidpp20drv_read_macro_key_8100(device, macro);
			if (!delay)
				ratbag_button_macro_set_event(m,
							      i++,
							      RATBAG_MACRO_EVENT_WAIT,
							      1);
			ratbag_button_macro_set_event(m,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_PRESSED,
						      keycode);
			delay = false;
			break;
		case HIDPP20_MACRO_KEY_RELEASE:
			keycode = hidpp20drv_read_macro_key_8100(device, macro);
			if (!delay)
				ratbag_button_macro_set_event(m,
							      i++,
							      RATBAG_MACRO_EVENT_WAIT,
							      1);
			ratbag_button_macro_set_event(m,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_RELEASED,
						      keycode);
			delay = false;
			break;
		}
		macro++;
	}

	ratbag_button_copy_macro(button, m);
	ratbag_button_macro_unref(m);

	return 0;
}

static void
hidpp20drv_read_button_8100(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *profile;
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100))
		return;

	profile = &drv_data->profiles->profiles[button->profile->index];

	switch (profile->buttons[button->index].any.type) {
	case HIDPP20_BUTTON_HID_TYPE:
		switch (profile->buttons[button->index].subany.subtype) {
		case HIDPP20_BUTTON_HID_TYPE_MOUSE:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
			button->action.action.button = profile->buttons[button->index].button.buttons;
			break;
		case HIDPP20_BUTTON_HID_TYPE_KEYBOARD:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
			button->action.action.key.key = ratbag_hidraw_get_keycode_from_keyboard_usage(device,
								profile->buttons[button->index].keyboard_keys.key);
			break;
		case HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
			button->action.action.key.key = ratbag_hidraw_get_keycode_from_consumer_usage(device,
								profile->buttons[button->index].consumer_control.consumer_control);
			break;
		}
		break;
	case HIDPP20_BUTTON_SPECIAL:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		button->action.action.special = hidpp20_onboard_profiles_get_special(profile->buttons[button->index].special.special);
		break;
	case HIDPP20_BUTTON_MACRO:
		rc = hidpp20drv_read_macro_8100(button, profile, &profile->buttons[button->index]);
		if (rc)
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		break;
	default:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		break;
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
}

static void
hidpp20drv_read_button(struct ratbag_button *button)
{
	hidpp20drv_read_button_1b04(button);
	hidpp20drv_read_button_8100(button);
}

static void
hidpp20drv_read_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *profile;
	struct hidpp20_color_led_zone_info *led_info;
	struct hidpp20_led *h_led;

	if (!(drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070))
		return;

	led_info = &drv_data->led_infos[led->index];
	profile = &drv_data->profiles->profiles[led->profile->index];
	h_led = &profile->leds[led->index];

	switch (h_led->mode) {
	case HIDPP20_LED_ON:
		led->mode = RATBAG_LED_ON;
		break;
	case HIDPP20_LED_CYCLE:
		led->mode = RATBAG_LED_CYCLE;
		break;
	case HIDPP20_LED_BREATHING:
		led->mode = RATBAG_LED_BREATHING;
		break;
	default:
		led->mode = RATBAG_LED_OFF;
		break;
	}
	led->type = hidpp20_8070_get_location_mapping(led_info->location);
	led->color.red = h_led->color.red;
	led->color.green = h_led->color.green;
	led->color.blue = h_led->color.blue;
	led->hz = h_led->period;  /* FIXME: should be ceil(1000.0 / h_led->period), but that would be very small */
	led->brightness = h_led->brightness * 255 / 100;
}

static int
hidpp20drv_write_button_1b04(struct ratbag_button *button,
			     const struct ratbag_button_action *action)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_control_id *control;
	uint16_t mapping;
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04))
		return -ENOTSUP;

	control = &drv_data->controls[button->index];
	mapping = hidpp20_1b04_get_logical_control_id(action);
	if (!mapping)
		return -EINVAL;

	control->reporting.divert = 0;
	control->reporting.remapped = mapping;
	control->reporting.updated = 1;

	rc = hidpp20_special_key_mouse_set_control(drv_data->dev, control);
	if (rc == HIDPP20_ERR_INVALID_ARGUMENT)
		return -EINVAL;

	if (rc)
		log_error(device->ratbag,
			  "Error while writing profile: '%s' (%d)\n",
			  strerror(-rc),
			  rc);

	return rc;
}

static int
hidpp20drv_update_button_8100(struct ratbag_button *button,
			      const struct ratbag_button_action *action)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *profile;
	uint8_t code, type, subtype;

	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100))
		return -ENOTSUP;

	profile = &drv_data->profiles->profiles[button->profile->index];

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		profile->buttons[button->index].button.type = HIDPP20_BUTTON_HID_TYPE;
		profile->buttons[button->index].button.subtype = HIDPP20_BUTTON_HID_TYPE_MOUSE;
		profile->buttons[button->index].button.buttons = action->action.button;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		type = HIDPP20_BUTTON_HID_TYPE;
		subtype = HIDPP20_BUTTON_HID_TYPE_KEYBOARD;
		code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, action->action.key.key);
		if (code == 0) {
			subtype = HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL;
			code = ratbag_hidraw_get_consumer_usage_from_keycode(device, action->action.key.key);
			if (code == 0)
				return -EINVAL;
		}
		profile->buttons[button->index].subany.type = type;
		profile->buttons[button->index].subany.subtype = subtype;
		if (subtype == HIDPP20_BUTTON_HID_TYPE_KEYBOARD)
			profile->buttons[button->index].keyboard_keys.key = code;
		else
			profile->buttons[button->index].consumer_control.consumer_control = code;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		code = hidpp20_onboard_profiles_get_code_from_special(action->action.special);
		if (code == 0)
			return -EINVAL;
		profile->buttons[button->index].special.type = HIDPP20_BUTTON_SPECIAL;
		profile->buttons[button->index].special.special = code;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int
hidpp20drv_update_button(struct ratbag_button *button,
			const struct ratbag_button_action *action)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)
		return hidpp20drv_update_button_8100(button, action);

	if (drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04)
		return hidpp20drv_write_button_1b04(button, action);

	return -ENOTSUP;
}

static int
hidpp20drv_update_led(struct ratbag_led *led)
{
	struct ratbag_profile *profile = led->profile;
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *h_profile;
	struct hidpp20_led *h_led;

	if (!(drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070))
		return -ENOTSUP;

	h_profile = &drv_data->profiles->profiles[profile->index];

	h_led = &(h_profile->leds[led->index]);

	if (!h_led)
		return -EINVAL;

	switch (led->mode) {
	case RATBAG_LED_ON:
		h_led->mode = HIDPP20_LED_ON;
		break;
	case RATBAG_LED_CYCLE:
		h_led->mode = HIDPP20_LED_CYCLE;
		break;
	case RATBAG_LED_BREATHING:
		h_led->mode = HIDPP20_LED_BREATHING;
		break;
	default:
		h_led->mode = HIDPP20_LED_OFF;
		break;
	}
	h_led->color.red = led->color.red;
	h_led->color.green = led->color.green;
	h_led->color.blue = led->color.blue;
	h_led->period = led->hz; /* FIXME: should be 1000 / led->hz, but that would be very small */
	h_led->brightness = led->brightness * 100 / 255;

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_current_profile(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data((struct ratbag_device *)device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100))
		return 0;

	rc = hidpp20_onboard_profiles_get_current_profile(drv_data->dev);
	if (rc < 0)
		return rc;

	return rc - 1;
}

static int
hidpp20drv_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data((struct ratbag_device *)device);
	struct hidpp20_profile *h_profile;
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100))
		return 0;

	if (index >= drv_data->num_profiles)
		return -EINVAL;

	h_profile = &drv_data->profiles->profiles[index];
	if (!h_profile->enabled) {
		h_profile->enabled = 1;
		rc = hidpp20_onboard_profiles_commit(drv_data->dev, drv_data->profiles);
		if (rc)
			return rc;
	}

	return hidpp20_onboard_profiles_set_current_profile(drv_data->dev, index);
}

static int
hidpp20drv_read_resolution_dpi_2201(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag *ratbag = device->ratbag;
	int rc;

	free(drv_data->sensors);
	drv_data->sensors = NULL;
	drv_data->num_sensors = 0;
	rc = hidpp20_adjustable_dpi_get_sensors(drv_data->dev, &drv_data->sensors);
	if (rc < 0) {
		log_error(ratbag,
			  "Error while requesting resolution: %s (%d)\n",
			  strerror(-rc), rc);
		return rc;
	} else if (rc == 0) {
		log_error(ratbag, "Error, no compatible sensors found.\n");
		return -ENODEV;
	}
	log_debug(ratbag,
		  "device is at %d dpi (variable between %d and %d).\n",
		  drv_data->sensors[0].dpi,
		  drv_data->sensors[0].dpi_min,
		  drv_data->sensors[0].dpi_max);

	drv_data->num_sensors = rc;
	drv_data->num_resolutions = drv_data->num_sensors;

	return 0;
}

static int
hidpp20drv_read_resolution_dpi(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag *ratbag = device->ratbag;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *res;
	int rc;
	unsigned int i;

	if (drv_data->capabilities & HIDPP_CAP_RESOLUTION_2200) {
		uint16_t resolution;
		uint8_t flags;

		rc = hidpp20_mousepointer_get_mousepointer_info(drv_data->dev, &resolution, &flags);
		if (rc) {
			log_error(ratbag,
				  "Error while requesting resolution: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}

		return 0;
	}

	if (drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201) {
		rc = hidpp20drv_read_resolution_dpi_2201(device);
		if (rc < 0)
			return rc;

		for (i = 0; i < profile->resolution.num_modes; i++) {
			struct hidpp20_sensor *sensor;

			/* We only look at the first sensor. Multiple
			 * sensors is too niche to care about right now */
			sensor = &drv_data->sensors[0];

			/* FIXME: retrieve the refresh rate */
			res = ratbag_resolution_init(profile, i, sensor->dpi, sensor->dpi, 0);
			ratbag_resolution_set_range(res, sensor->dpi_min, sensor->dpi_max);

			/* FIXME: we mark all resolutions as active because
			 * they are from different sensors */
			res->is_active = true;
		}

		return 0;
	}

	return 0;
}

static int
hidpp20drv_update_resolution_dpi_8100(struct ratbag_resolution *resolution,
				      int dpi_x, int dpi_y)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *h_profile;
	unsigned int index;
	int dpi = dpi_x; /* dpi_x == dpi_y if we don't have the individual resolution cap */

	/* retrieve which resolution is asked to be changed */
	index = resolution - profile->resolution.modes;

	h_profile = &drv_data->profiles->profiles[profile->index];
	h_profile->dpi[index] = dpi;

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_update_resolution_dpi(struct ratbag_resolution *resolution,
				 int dpi_x, int dpi_y)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_sensor *sensor;
	int rc, i;
	int dpi = dpi_x; /* dpi_x == dpi_y if we don't have the individual resolution cap */

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)
		return hidpp20drv_update_resolution_dpi_8100(resolution, dpi_x, dpi_y);

	if (!(drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201))
		return -ENOTSUP;

	if (!drv_data->num_sensors)
		return -ENOTSUP;

	/* just for clarity, we use the first available sensor only */
	sensor = &drv_data->sensors[0];

	/* validate that the sensor accepts the given DPI */
	rc = -EINVAL;
	if (dpi < sensor->dpi_min || dpi > sensor->dpi_max)
		goto out;
	if (sensor->dpi_steps) {
		for (i = sensor->dpi_min; i < dpi; i += sensor->dpi_steps) {
		}
		if (i != dpi)
			goto out;
	} else {
		i = 0;
		while (sensor->dpi_list[i]) {
			if (sensor->dpi_list[i] == dpi)
				break;
		}
		if (sensor->dpi_list[i] != dpi)
			goto out;
	}

	rc = hidpp20_adjustable_dpi_set_sensor_dpi(drv_data->dev, sensor, dpi);

out:
	return rc;
}

static int
hidpp20drv_read_special_key_mouse(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04))
		return 0;

	free(drv_data->controls);
	drv_data->controls = NULL;
	drv_data->num_controls = 0;
	rc = hidpp20_special_key_mouse_get_controls(drv_data->dev, &drv_data->controls);
	if (rc > 0) {
		drv_data->num_controls = rc;
		rc = 0;
	}

	return rc;
}

static int
hidpp20drv_read_kbd_reprogrammable_key(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00))
		return 0;

	free(drv_data->controls);
	drv_data->controls = NULL;
	drv_data->num_controls = 0;
	rc = hidpp20_kbd_reprogrammable_keys_get_controls(drv_data->dev, &drv_data->controls);
	if (rc > 0) {
		drv_data->num_controls = rc;
		rc = 0;
	}

	return rc;
}

static int
hidpp20drv_read_color_leds(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070))
		return 0;

	free(drv_data->led_infos);
	drv_data->led_infos = NULL;
	drv_data->num_leds = 0;

	rc = hidpp20_color_led_effects_get_zone_infos(drv_data->dev, &drv_data->led_infos);

	if (rc > 0) {
		drv_data->num_leds = rc;
		rc = 0;
	}

	return rc;
}

static void
hidpp20drv_read_profile_8100(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *res;
	struct hidpp20_profile *p;
	unsigned int i;
	int dpi_index = 0xff;

	profile->is_enabled = drv_data->profiles->profiles[index].enabled;

	profile->is_active = false;
	if ((int)index == hidpp20drv_current_profile(device))
		profile->is_active = true;

	if (profile->is_active)
		dpi_index = hidpp20_onboard_profiles_get_current_dpi_index(drv_data->dev);

	if (dpi_index < 0)
		dpi_index = 0xff;

	p = &drv_data->profiles->profiles[index];

	for (i = 0; i < profile->resolution.num_modes; i++) {
		struct hidpp20_sensor *sensor;

		/* We only look at the first sensor. Multiple
		 * sensors is too niche to care about right now */
		sensor = &drv_data->sensors[0];

		res = ratbag_resolution_init(profile, i,
					     p->dpi[i],
					     p->dpi[i],
					     p->report_rate);

		if (profile->is_active &&
		    i == (unsigned int)dpi_index)
			res->is_active = true;
		if (i == p->default_dpi) {
			res->is_default = true;
			if (!profile->is_active || dpi_index < 0 || dpi_index > 4)
				res->is_active = true;
		}

		ratbag_resolution_set_range(res, sensor->dpi_min, sensor->dpi_max);
	}
}

static void
hidpp20drv_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)
		return hidpp20drv_read_profile_8100(profile, index);

	hidpp20drv_read_resolution_dpi(profile);
	hidpp20drv_read_special_key_mouse(device);

	profile->is_active = (index == 0);
}

static int
hidpp20drv_init_feature(struct ratbag_device *device, uint16_t feature)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag *ratbag = device->ratbag;
	int rc;
	uint8_t feature_index, feature_type, feature_version;

	rc = hidpp_root_get_feature(drv_data->dev,
				    feature,
				    &feature_index,
				    &feature_type,
				    &feature_version);
	if (rc < 0)
		return rc;

	switch (feature) {
	case HIDPP_PAGE_ROOT:
	case HIDPP_PAGE_FEATURE_SET:
		/* these features are mandatory and already handled */
		break;
	case HIDPP_PAGE_MOUSE_POINTER_BASIC: {
		drv_data->capabilities |= HIDPP_CAP_RESOLUTION_2200;
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_RESOLUTION);
		break;
	}
	case HIDPP_PAGE_ADJUSTABLE_DPI: {
		log_debug(ratbag, "device has adjustable dpi\n");
		/* we read the profile once to get the correct number of
		 * supported resolutions. */
		rc = hidpp20drv_read_resolution_dpi_2201(device);
		if (rc < 0)
			return 0; /* this is not a hard failure */
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_RESOLUTION);
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION);
		drv_data->capabilities |= HIDPP_CAP_SWITCHABLE_RESOLUTION_2201;
		break;
	}
	case HIDPP_PAGE_SPECIAL_KEYS_BUTTONS: {
		log_debug(ratbag, "device has programmable keys/buttons\n");
		drv_data->capabilities |= HIDPP_CAP_BUTTON_KEY_1b04;
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON);
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_KEY);
		/* we read the profile once to get the correct number of
		 * supported buttons. */
		if (!hidpp20drv_read_special_key_mouse(device))
			device->num_buttons = drv_data->num_controls;
		break;
	}
	case HIDPP_PAGE_BATTERY_LEVEL_STATUS: {
		uint16_t level, next_level;
		enum hidpp20_battery_status status;

		rc = hidpp20_batterylevel_get_battery_level(drv_data->dev, &level, &next_level);
		if (rc < 0)
			return rc;
		status = rc;

		log_debug(ratbag, "device battery level is %d%% (next %d%%), status %d \n",
			  level, next_level, status);

		drv_data->capabilities |= HIDPP_CAP_BATTERY_LEVEL_1000;
		break;
	}
	case HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS: {
		log_debug(ratbag, "device has programmable keys/buttons\n");
		drv_data->capabilities |= HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00;

		/* we read the profile once to get the correct number of
		 * supported buttons. */
		if (!hidpp20drv_read_kbd_reprogrammable_key(device))
			device->num_buttons = drv_data->num_controls;
		break;
	}
	case HIDPP_PAGE_ADJUSTABLE_REPORT_RATE: {
		log_debug(ratbag, "device has adjustable report rate\n");
		break;
	}
	case HIDPP_PAGE_COLOR_LED_EFFECTS: {
		log_debug(ratbag, "device has color effects\n");
		drv_data->capabilities |= HIDPP_CAP_COLOR_LED_EFFECTS_8070;

		/* we read the profile once to get the correct number of
		 * supported leds. */
		if (hidpp20drv_read_color_leds(device))
			return 0;

		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_LED);
		device->num_leds = drv_data->num_leds;
		break;
	}
	case HIDPP_PAGE_ONBOARD_PROFILES: {
		log_debug(ratbag, "device has onboard profiles\n");
		drv_data->capabilities |= HIDPP_CAP_ONBOARD_PROFILES_8100;
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_PROFILE);
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_DISABLE_PROFILE);

		rc = hidpp20_onboard_profiles_allocate(drv_data->dev, &drv_data->profiles);
		if (rc < 0)
			return rc;

		rc = hidpp20_onboard_profiles_initialize(drv_data->dev, drv_data->profiles);
		if (rc < 0)
			return rc;

		drv_data->num_profiles = drv_data->profiles->num_profiles;
		drv_data->num_resolutions = drv_data->profiles->num_modes;
		drv_data->num_buttons = drv_data->profiles->num_buttons;
		drv_data->num_leds = drv_data->profiles->num_leds;

		break;
	}
	case HIDPP_PAGE_MOUSE_BUTTON_SPY: {
		log_debug(ratbag, "device has configurable mouse button spy\n");
		break;
	}
	default:
		log_raw(device->ratbag, "unknown feature 0x%04x\n", feature);
	}
	return 0;
}

static int
hidpp20drv_commit(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;
	int rc;
	unsigned int i, dpi_index = 0;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		drv_data->profiles->profiles[profile->index].enabled = profile->is_enabled;

		for (i = 0; i < profile->resolution.num_modes; i++) {
			resolution = &profile->resolution.modes[i];
			if (resolution->is_active)
				dpi_index = i;

			rc = hidpp20drv_update_resolution_dpi(resolution,
							      resolution->dpi_x,
							      resolution->dpi_y);
			if (rc)
				return RATBAG_ERROR_DEVICE;
		}

		list_for_each(button, &profile->buttons, link) {
			struct ratbag_button_action action = button->action;

			if (!button->dirty)
				continue;

			rc = hidpp20drv_update_button(button, &action);
			if (rc)
				return RATBAG_ERROR_DEVICE;
		}

		list_for_each(led, &profile->leds, link) {
			if (!led->dirty)
				continue;

			rc = hidpp20drv_update_led(led);
			if (rc)
				return RATBAG_ERROR_DEVICE;
		}

	}

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		rc = hidpp20_onboard_profiles_commit(drv_data->dev,
						     drv_data->profiles);
		if (rc)
			return RATBAG_ERROR_DEVICE;

		list_for_each(profile, &device->profiles, link) {
			if (profile->is_active)
				hidpp20_onboard_profiles_set_current_dpi_index(drv_data->dev,
									       dpi_index);
		}
	}

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_20_probe(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_device *dev = drv_data->dev;
	struct hidpp20_feature *feature_list = dev->feature_list;
	unsigned int i;
	int rc;

	log_raw(device->ratbag,
		"'%s' has %d features\n",
		ratbag_device_get_name(device),
		dev->feature_count);

	for (i = 0; i < dev->feature_count; i++) {
		log_raw(device->ratbag, "Init feature %s (0x%04x) \n",
			hidpp20_feature_get_name(feature_list[i].feature),
			feature_list[i].feature);
		rc = hidpp20drv_init_feature(device, feature_list[i].feature);
		if (rc < 0)
			return rc;
	}

	return 0;

}

static int
hidpp20drv_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, REPORT_ID_LONG);
}

static void
hidpp20_log(void *userdata, enum hidpp_log_priority priority, const char *format, va_list args)
{
	struct ratbag_device *device = userdata;

	log_msg_va(device->ratbag, priority, format, args);
}

static void
hidpp20drv_remove(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data;

	if (!device)
		return;

	drv_data = ratbag_get_drv_data(device);

	ratbag_close_hidraw(device);

	if (drv_data->profiles)
		hidpp20_onboard_profiles_destroy(drv_data->profiles);
	free(drv_data->led_infos);
	free(drv_data->controls);
	free(drv_data->sensors);
	if (drv_data->dev)
		hidpp20_device_destroy(drv_data->dev);
	free(drv_data);
}

static int
hidpp20drv_probe(struct ratbag_device *device)
{
	int rc;
	struct hidpp20drv_data *drv_data;
	struct hidpp_device base;
	struct hidpp20_device *dev;
	int device_idx = HIDPP_RECEIVER_IDX;

	rc = ratbag_find_hidraw(device, hidpp20drv_test_hidraw);
	if (rc)
		return rc;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);
	hidpp_device_init(&base, device->hidraw.fd);
	hidpp_device_set_log_handler(&base, hidpp20_log, HIDPP_LOG_PRIORITY_RAW, device);

	device_idx = ratbag_device_data_hidpp20_get_index(device->data);
	if (device_idx == -1)
		device_idx = HIDPP_RECEIVER_IDX;

	/* In the general case, we can treat all devices as wired devices
	 * here. If we talk to the correct hidraw device the kernel adjusts
	 * the device index for us, so even for unifying receiver devices
	 * we can just use 0xff as device index.
	 *
	 * If there is a special need like for G900, we can add this in the
	 * device data file.
	 */
	dev = hidpp20_device_new(&base, device_idx);
	if (!dev) {
		rc = -ENODEV;
		goto err;
	}

	drv_data->dev = dev;

	log_debug(device->ratbag, "'%s' is using protocol v%d.%d\n", ratbag_device_get_name(device), dev->proto_major, dev->proto_minor);

	/* add some defaults that will be overwritten by the device */
	drv_data->num_profiles = 1;
	drv_data->num_resolutions = 1;
	drv_data->num_buttons = 8;
	drv_data->num_leds = 2;

	rc = hidpp20drv_20_probe(device);
	if (rc)
		goto err;

	ratbag_device_init_profiles(device,
				    drv_data->num_profiles,
				    drv_data->num_resolutions,
				    drv_data->num_buttons,
				    drv_data->num_leds);

	return rc;
err:
	hidpp20drv_remove(device);
	return rc;
}

struct ratbag_driver hidpp20_driver = {
	.name = "Logitech HID++2.0",
	.id = "hidpp20",
	.probe = hidpp20drv_probe,
	.remove = hidpp20drv_remove,
	.commit = hidpp20drv_commit,
	.read_profile = hidpp20drv_read_profile,
	.set_active_profile = hidpp20drv_set_current_profile,
	.read_button = hidpp20drv_read_button,
	.read_led = hidpp20drv_read_led,
};
