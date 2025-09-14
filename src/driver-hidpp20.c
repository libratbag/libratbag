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
#define HIDPP_CAP_COLOR_LED_EFFECTS_8070		(1 << 5)
#define HIDPP_CAP_ONBOARD_PROFILES_8100			(1 << 6)
#define HIDPP_CAP_LED_SW_CONTROL_1300			(1 << 7)
#define HIDPP_CAP_ADJUSTABLE_REPORT_RATE_8060		(1 << 8)
#define HIDPP_CAP_BATTERY_VOLTAGE_1001			(1 << 9)
#define HIDPP_CAP_RGB_EFFECTS_8071			(1 << 10)

#define HIDPP_HIDDEN_FEATURE				(1 << 6)

struct hidpp20drv_data {
	struct hidpp20_device *dev;
	unsigned long capabilities;
	unsigned num_sensors;
	struct hidpp20_sensor *sensors;
	unsigned num_controls;
	struct hidpp20_control_id *controls;
	struct hidpp20_profiles *profiles;
	struct hidpp20_led *leds;
	union hidpp20_generic_led_zone_info led_infos;

	unsigned int report_rates[4];
	unsigned int num_report_rates;

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
	if (control->reporting.remapped)
		mapping = control->reporting.remapped;
	log_raw(device->ratbag,
		  " - button%d: %s (%02x) %s%s:%d\n",
		  button->index,
		  hidpp20_1b04_get_logical_mapping_name(mapping),
		  mapping,
		  control->reporting.divert || control->reporting.persist ? "(redirected) " : "",
		  __FILE__, __LINE__);
	action = hidpp20_1b04_get_logical_mapping(mapping);
	if (action)
		ratbag_button_set_action(button, action);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
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
	unsigned int modifiers = 0;
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100))
		return;

	profile = &drv_data->profiles->profiles[button->profile->index];

	switch (profile->buttons[button->index].any.type) {
	case HIDPP20_BUTTON_HID_TYPE:
		switch (profile->buttons[button->index].subany.subtype) {
		case HIDPP20_BUTTON_HID_TYPE_NOOP:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
			break;
		case HIDPP20_BUTTON_HID_TYPE_MOUSE:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
			button->action.action.button = profile->buttons[button->index].button.buttons;
			break;
		case HIDPP20_BUTTON_HID_TYPE_KEYBOARD:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
			button->action.action.key = ratbag_hidraw_get_keycode_from_keyboard_usage(device,
								profile->buttons[button->index].keyboard_keys.key);
			modifiers = profile->buttons[button->index].keyboard_keys.modifier_flags;
			break;
		case HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
			button->action.action.key = ratbag_hidraw_get_keycode_from_consumer_usage(device,
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

	if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_KEY) {
		rc = ratbag_button_macro_new_from_keycode(button,
							  button->action.action.key,
							  modifiers);
		if (rc < 0) {
			log_error(device->ratbag,
				  "Error while reading button %d\n",
				  button->index);
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		}
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
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
hidpp20drv_read_led_1300(struct ratbag_led *led, struct hidpp20drv_data* data)
{
	struct hidpp20_led_sw_ctrl_led_state state;
	struct hidpp20_led_sw_ctrl_led_info *info;
	int rc;

	info = &data->led_infos.leds[led->index];

	rc = hidpp20_led_sw_control_get_led_state(data->dev, led->index, &state);

	if (rc != 0)
		return;

	led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;

	switch (state.mode)
	{
	case HIDPP20_LED_MODE_OFF:
		led->mode = RATBAG_LED_OFF;
		break;
	case HIDPP20_LED_MODE_ON:
		led->mode = RATBAG_LED_ON;
		break;
	case HIDPP20_LED_MODE_BREATHING:
		led->mode = RATBAG_LED_BREATHING;
		led->ms = state.breathing.period;
		led->brightness = state.breathing.brightness;
		break;
	default:
		led->mode = RATBAG_LED_ON;
	}

	if (info->caps & HIDPP20_LED_MODE_ON)
		ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	if (info->caps & HIDPP20_LED_MODE_OFF)
		ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
	if (info->caps & (HIDPP20_LED_MODE_BLINK | HIDPP20_LED_MODE_TRAVEL |
			  HIDPP20_LED_MODE_RAMP_UP | HIDPP20_LED_MODE_RAMP_DOWN |
			  HIDPP20_LED_MODE_HEARTBEAT | HIDPP20_LED_MODE_BREATHING))
		ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
}

static void
hidpp20drv_read_led_8070(struct ratbag_led *led, struct hidpp20drv_data* drv_data)
{
	struct hidpp20_profile *profile;
	struct hidpp20_led h_led_val;
	struct hidpp20_led *h_led = &h_led_val;
	struct hidpp20_color_led_zone_info* led_info;
	struct hidpp20_color_led_info info;
	int rc;

	led_info = &drv_data->led_infos.color_leds_8070[led->index];

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		profile = &drv_data->profiles->profiles[led->profile->index];
		h_led = &profile->leds[led->index];
	} else {
		rc = hidpp20_color_led_effects_get_zone_effect(drv_data->dev, led->index, h_led);
		if (rc) {
			log_debug(led->profile->device->ratbag,
				  "Failed to read led settings\n");
		}
	}

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

	led->color.red = h_led->color.red;
	led->color.green = h_led->color.green;
	led->color.blue = h_led->color.blue;
	led->ms = h_led->period;
	led->brightness = h_led->brightness * 255 / 100;

	rc = hidpp20_color_led_effects_get_info(drv_data->dev, &info);
	if (rc == 0 &&
	    info.ext_caps & HIDPP20_COLOR_LED_INFO_EXT_CAP_MONOCHROME_ONLY)
		led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;
	else
		led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;

	for (int i = 0; i < led_info->num_effects; i++) {
		struct hidpp20_color_led_zone_effect_info ei;
		rc = hidpp20_color_led_effect_get_zone_effect_info(drv_data->dev,
								   led_info->index,
								   i, &ei);
		if (rc < 0)
			break;

		switch (ei.effect_id) {
		case HIDPP20_COLOR_LED_ZONE_EFFECT_DISABLED:
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			break;
		case HIDPP20_COLOR_LED_ZONE_EFFECT_FIXED:
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			break;
		case HIDPP20_COLOR_LED_ZONE_EFFECT_CYCLING:
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
			break;
		case HIDPP20_COLOR_LED_ZONE_EFFECT_WAVE:
		case HIDPP20_COLOR_LED_ZONE_EFFECT_BREATHING:
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			break;
		default:
			log_debug(led->profile->device->ratbag,
				  "%s: Unsupported effect (%d)\n",
				  led->profile->device->name,
				  ei.effect_id);
			break;
		}
	}
}

static void
hidpp20drv_read_led_8071(struct ratbag_led *led, struct hidpp20drv_data* drv_data)
{
	struct hidpp20_profile *profile;
	struct hidpp20_led *h_led;
	struct hidpp20_rgb_device_info device_info;
	struct hidpp20_rgb_cluster_info cluster_info;
	int rc;

	hidpp20_rgb_effects_get_device_info(drv_data->dev, &device_info);
	cluster_info = drv_data->led_infos.color_leds_8071[led->index];
	profile = &drv_data->profiles->profiles[led->profile->index];
	if (drv_data->dev->quirk == HIDPP20_QUIRK_G502X_PLUS) {
		// On G502X+ the second slot controls the user configurable LED.
		// (Note: There is only 1 reported LED, it just happens to use 2nd index though)
		h_led = &profile->leds[1];
	} else {
		h_led = &profile->leds[led->index];
	}

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

	led->color.red = h_led->color.red;
	led->color.green = h_led->color.green;
	led->color.blue = h_led->color.blue;
	led->ms = h_led->period;
	led->brightness = h_led->brightness * 255 / 100;

	if (device_info.ext_caps & HIDPP20_COLOR_LED_INFO_EXT_CAP_MONOCHROME_ONLY)
		led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;
	else
		led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;

	for (int i = 0; i < cluster_info.num_effects; i++) {
		struct hidpp20_rgb_effect_info ei;
		rc = hidpp20_rgb_effects_get_effect_info(drv_data->dev,
							 cluster_info.index,
							 i, &ei);
		if (rc < 0)
			break;

		switch (ei.effect_id) {
		case HIDPP20_COLOR_LED_ZONE_EFFECT_DISABLED:
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			break;
		case HIDPP20_COLOR_LED_ZONE_EFFECT_FIXED:
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			break;
		case HIDPP20_COLOR_LED_ZONE_EFFECT_CYCLING:
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
			break;
		case HIDPP20_COLOR_LED_ZONE_EFFECT_WAVE:
		case HIDPP20_COLOR_LED_ZONE_EFFECT_BREATHING:
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			break;
		default:
			log_debug(led->profile->device->ratbag,
				  "%s: Unsupported effect (%d)\n",
				  led->profile->device->name,
				  ei.effect_id);
			break;
		}
	}
}

static void
hidpp20drv_read_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	if (drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070)
		hidpp20drv_read_led_8070(led, drv_data);
	else if (drv_data->capabilities & HIDPP_CAP_RGB_EFFECTS_8071)
		hidpp20drv_read_led_8071(led, drv_data);
	else if (drv_data->capabilities & HIDPP_CAP_LED_SW_CONTROL_1300)
		hidpp20drv_read_led_1300(led, drv_data);
}

static int
hidpp20drv_update_button_1b04(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_control_id *control;
	struct ratbag_button_action *action = &button->action;
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
	if (rc)
		log_error(device->ratbag,
			  "Error while writing profile: '%s' (%d)\n",
			  strerror(-rc),
			  rc);

	return rc;
}

static int
hidpp20drv_update_button_8100(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *profile;
	struct ratbag_button_action *action = &button->action;
	unsigned int modifiers, key;
	int rc;
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
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		type = HIDPP20_BUTTON_HID_TYPE;
		subtype = HIDPP20_BUTTON_HID_TYPE_KEYBOARD;
		rc = ratbag_action_keycode_from_macro(action,
						      &key,
						      &modifiers);
		if (rc < 0) {
			log_error(device->ratbag,
				  "Error while writing macro for button %d\n",
				  button->index);
		}

		code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
		if (code == 0) {
			subtype = HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL;
			code = ratbag_hidraw_get_consumer_usage_from_keycode(device, key);
			if (code == 0)
				return -EINVAL;
		}
		profile->buttons[button->index].subany.type = type;
		profile->buttons[button->index].subany.subtype = subtype;
		if (subtype == HIDPP20_BUTTON_HID_TYPE_KEYBOARD) {
			profile->buttons[button->index].keyboard_keys.key = code;
			profile->buttons[button->index].keyboard_keys.modifier_flags = modifiers;
		} else {
			profile->buttons[button->index].consumer_control.consumer_control = code;
		}
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		type = HIDPP20_BUTTON_HID_TYPE;
		subtype = HIDPP20_BUTTON_HID_TYPE_KEYBOARD;
		code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, action->action.key);
		if (code == 0) {
			subtype = HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL;
			code = ratbag_hidraw_get_consumer_usage_from_keycode(device, action->action.key);
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
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		profile->buttons[button->index].disabled.type = HIDPP20_BUTTON_HID_TYPE_NOOP;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int
hidpp20drv_update_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)
		return hidpp20drv_update_button_8100(button);

	if (drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04)
		return hidpp20drv_update_button_1b04(button);

	return -ENOTSUP;
}

static int
hidpp20drv_update_led_1300(struct ratbag_led *led, struct hidpp20drv_data *data)
{
	const uint16_t led_caps = data->led_infos.leds[led->index].caps;
	struct hidpp20_led_sw_ctrl_led_state h_led;
	int rc;

	h_led.index = led->index;

	switch(led->mode)
	{
	case RATBAG_LED_BREATHING:
		h_led.mode = HIDPP20_LED_MODE_BREATHING;
		h_led.breathing.brightness = led->brightness;
		h_led.breathing.period = led->ms;
		h_led.breathing.timeout = 300;
		break;
	case RATBAG_LED_OFF:
		h_led.mode = HIDPP20_LED_MODE_OFF;
		h_led.on.index = HIDPP20_LED_SW_CONTROL_LED_INDEX_ALL;
		break;
	case RATBAG_LED_ON:
		h_led.mode = HIDPP20_LED_MODE_ON;
		h_led.on.index = HIDPP20_LED_SW_CONTROL_LED_INDEX_ALL;
		break;
	case RATBAG_LED_CYCLE:
		return -ENOTSUP;
	}

	if (!(h_led.mode & led_caps)) {
		hidpp_log_error(&data->dev->base, "LED %d does not support effect %s(%04x), supports %04x\n",
						led->index, hidpp20_sw_led_control_get_mode_string(h_led.mode),
						h_led.mode, led_caps);
		return -ENOTSUP;
	}

	if (!hidpp20_led_sw_control_get_sw_ctrl(data->dev)) {
		rc = hidpp20_led_sw_control_set_sw_ctrl(data->dev, true);
		if (rc)
			return rc;
	}

	rc = hidpp20_led_sw_control_set_led_state(data->dev, &h_led);
	if (rc)
		return rc;

	/*
	 * We revert to HW control of the LEDs.
	 * This effectively disable the capability of changing the DPI/profile
	 * LEDs, but this allows to make them work with the internal DPI/profile
	 * switches.
	 *
	 * The net result is:
	 * - DPI/profiles can't be controlled (but who wants this?)
	 * - The logo can properly controlled
	 */
	rc = hidpp20_led_sw_control_set_sw_ctrl(data->dev, false);
	if (rc)
		return rc;

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_update_led_8070_8071(struct ratbag_led *led, struct ratbag_profile* profile,
				struct hidpp20drv_data *drv_data)
{
	struct hidpp20_profile *h_profile;
	struct hidpp20_led h_led_val = {0};
	struct hidpp20_led *h_led = &h_led_val;

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		h_profile = &drv_data->profiles->profiles[profile->index];
		if (drv_data->dev->quirk == HIDPP20_QUIRK_G502X_PLUS) {
			// On G502X+ the second slot controls the user configurable LED.
			// (Note: There is only 1 reported LED, it just happens to use 2nd index though)
			h_led = &(h_profile->leds[1]);
		} else {
			h_led = &(h_profile->leds[led->index]);
		}
	}

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
	h_led->period = led->ms;
	h_led->brightness = led->brightness * 100 / 255;

	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)) {
		if (drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070)
			hidpp20_color_led_effects_set_zone_effect(drv_data->dev,
								  led->index,
								  h_led_val);
	}

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_update_led(struct ratbag_led *led)
{
	struct ratbag_profile *profile = led->profile;
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	if ((drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070) |
	    (drv_data->capabilities & HIDPP_CAP_RGB_EFFECTS_8071))
		return hidpp20drv_update_led_8070_8071(led, profile, drv_data);

	if (drv_data->capabilities & HIDPP_CAP_LED_SW_CONTROL_1300)
		return hidpp20drv_update_led_1300(led, drv_data);

	return RATBAG_ERROR_CAPABILITY;
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

	/* if 0x8100 has already been enumerated we already have the supported
	 * number of resolutions and shouldn't overwrite it
	 */
	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100))
		drv_data->num_resolutions = drv_data->num_sensors;

	return 0;
}

static int
hidpp20drv_read_report_rate_8060(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag *ratbag = device->ratbag;
	struct ratbag_profile *profile;
	uint8_t bitflags_ms;
	int nrates = 0;
	int rc;
	uint8_t rate_ms;
	unsigned rate_hz;

	rc = hidpp20_adjustable_report_rate_get_report_rate_list(drv_data->dev,
								 &bitflags_ms);
	if (rc < 0)
		return rc;

	/* We only care about 'standard' rates */
	if (bitflags_ms & 0x80)
		drv_data->report_rates[nrates++] = 125;
	if (bitflags_ms & 0x8)
		drv_data->report_rates[nrates++] = 250;
	if (bitflags_ms & 0x2)
		drv_data->report_rates[nrates++] = 500;
	if (bitflags_ms & 0x1)
		drv_data->report_rates[nrates++] = 1000;

	drv_data->num_report_rates = nrates;

	if (!hidpp20_adjustable_report_rate_get_report_rate(drv_data->dev, &rate_ms)) {
		switch (rate_ms)
		{
		case 1:
			rate_hz = 1000;
			break;
		case 2:
		case 3: /* 3ms = 333.(3)Hz, we round to 500Hz */
			rate_hz = 500;
			break;
		case 4:
		case 5: /* 5ms = 200Hz, we round to 250Hz */
			rate_hz = 250;
			break;
		case 6: /* 6ms = 166.(6)Hz, we round to 125Hz */
		case 7: /* 7ms = 142Hz, we round to 125Hz */
		case 8:
			rate_hz = 125;
			break;
		default:
			rate_hz = 0;
			break;
		}

		if (rate_hz) {
			log_debug(ratbag, "report rate is %u\n", rate_hz);
			ratbag_device_for_each_profile(device, profile)
				profile->hz = rate_hz;
		}
	}

	log_debug(ratbag, "device has %d report rates\n", nrates);

	return 0;
}

static int
hidpp20drv_read_resolution_dpi(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag *ratbag = device->ratbag;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *res;
	unsigned int default_dpi = 1000;
	unsigned int default_rate = 500;
	int rc;

	if ((drv_data->capabilities & HIDPP_CAP_RESOLUTION_2200) &&
	    (drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201) == 0) {
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

		ratbag_profile_for_each_resolution(profile, res) {
			struct hidpp20_sensor *sensor;

			/* We only look at the first sensor. Multiple
			 * sensors is too niche to care about right now */
			sensor = &drv_data->sensors[0];

			/* FIXME: retrieve the refresh rate */
			ratbag_resolution_set_resolution(res, sensor->dpi, sensor->dpi);
			ratbag_resolution_set_dpi_list_from_range(res,
								  sensor->dpi_min,
								  sensor->dpi_max);
			/* FIXME: we mark all resolutions as active because
			 * they are from different sensors */
			res->is_active = true;
		}
	} else {
		ratbag_profile_for_each_resolution(profile, res)
			ratbag_resolution_set_dpi_list(res, &default_dpi, 1);
	}

	if (drv_data->capabilities & HIDPP_CAP_ADJUSTABLE_REPORT_RATE_8060) {
		rc = hidpp20drv_read_report_rate_8060(device);
		if (rc < 0 && drv_data->report_rates[0] == 0)
			return rc;

		ratbag_profile_set_report_rate_list(profile,
						    drv_data->report_rates,
						    drv_data->num_report_rates);
	} else {
		ratbag_profile_set_report_rate_list(profile, &default_rate, 1);
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
	int dpi = dpi_x; /* dpi_x == dpi_y if we don't have the individual resolution cap */

	h_profile = &drv_data->profiles->profiles[profile->index];
	h_profile->dpi[resolution->index] = dpi;

	if (resolution->is_default)
		h_profile->default_dpi = resolution->index;

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
	int i;
	int dpi = dpi_x; /* dpi_x == dpi_y if we don't have the individual resolution cap */

	if (resolution->is_disabled) {
		if (!ratbag_resolution_has_capability(resolution, RATBAG_RESOLUTION_CAP_DISABLE))
			return -ENOTSUP;

		dpi = dpi_x = dpi_y = 0;
	}

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)
		return hidpp20drv_update_resolution_dpi_8100(resolution, dpi_x, dpi_y);

	if (!(drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201))
		return -ENOTSUP;

	if (!drv_data->num_sensors)
		return -ENOTSUP;

	/* just for clarity, we use the first available sensor only */
	sensor = &drv_data->sensors[0];

	if (!resolution->is_disabled) {
		/* validate that the sensor accepts the given DPI */
		if (dpi < sensor->dpi_min || dpi > sensor->dpi_max)
			return -EINVAL;
		if (sensor->dpi_steps) {
			for (i = sensor->dpi_min; i < dpi; i += sensor->dpi_steps) {
			}
			if (i != dpi)
				return -EINVAL;
		} else {
			i = 0;
			while (sensor->dpi_list[i]) {
				if (sensor->dpi_list[i] == dpi)
					break;
			}
			if (sensor->dpi_list[i] != dpi)
				return -EINVAL;
		}
	}

	return hidpp20_adjustable_dpi_set_sensor_dpi(drv_data->dev, sensor, dpi);
}

static int
hidpp20drv_update_report_rate_8060(struct ratbag_profile *profile, int hz)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	rc = hidpp20_adjustable_report_rate_set_report_rate(drv_data->dev, 1000/hz);
	if (rc)
		return rc;

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_update_report_rate_8100(struct ratbag_profile *profile, int hz)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_profile *h_profile;

	h_profile = &drv_data->profiles->profiles[profile->index];
	h_profile->report_rate = hz;

	return RATBAG_SUCCESS;
}

static int
hidpp20drv_update_report_rate(struct ratbag_profile *profile, int hz)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100)
		return hidpp20drv_update_report_rate_8100(profile, hz);

	if (drv_data->capabilities & HIDPP_CAP_ADJUSTABLE_REPORT_RATE_8060) {
		rc = hidpp20drv_update_report_rate_8060(profile, hz);

		/* re-populate the profile with the correct value if we fail */
		if (rc)
			hidpp20drv_read_report_rate_8060(profile->device);

		return rc;
	}

	return -ENOTSUP;
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
	log_raw(device->ratbag, "num_control %d\n", rc);
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
hidpp20drv_read_color_leds_8070(struct hidpp20drv_data *drv_data)
{
	int rc;

	free(drv_data->led_infos.color_leds_8070);
	drv_data->led_infos.color_leds_8070 = NULL;
	drv_data->num_leds = 0;

	rc = hidpp20_color_led_effects_get_zone_infos(drv_data->dev, &drv_data->led_infos.color_leds_8070);

	if (rc > 0) {
		drv_data->num_leds = rc;
		rc = 0;
	}

	return rc;
}

static int
hidpp20drv_read_color_leds_8071(struct hidpp20drv_data *drv_data)
{
	int rc;

	free(drv_data->led_infos.color_leds_8071);
	drv_data->led_infos.color_leds_8071 = NULL;
	drv_data->num_leds = 0;

	rc = hidpp20_rgb_effects_get_cluster_infos(drv_data->dev, &drv_data->led_infos.color_leds_8071);

	if (rc > 0) {
		drv_data->num_leds = rc;
		rc = 0;
	}

	return rc;
}

static int
hidpp20drv_read_color_leds(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	if(drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070)
		return hidpp20drv_read_color_leds_8070(drv_data);
	else if (drv_data->capabilities & HIDPP_CAP_RGB_EFFECTS_8071)
		return hidpp20drv_read_color_leds_8071(drv_data);

	return 0;
}

static int
hidpp20drv_read_leds(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_LED_SW_CONTROL_1300))
		return 0;

	free(drv_data->led_infos.leds);
	drv_data->led_infos.leds = NULL;

	rc = hidpp20_led_sw_control_read_leds(drv_data->dev, &drv_data->led_infos.leds);
	if (rc > 0) {
		drv_data->num_leds = rc;
		rc = 0;
	}

	return rc;
}

static void
hidpp20drv_read_profile_8100(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *res;
	struct hidpp20_profile *p;
	int dpi_index = 0xff;
	int dpi;

	profile->is_enabled = drv_data->profiles->profiles[profile->index].enabled;

	profile->is_active = (profile->index == drv_data->profiles->active_profile_index);

	if (profile->is_active)
		dpi_index = hidpp20_onboard_profiles_get_current_dpi_index(drv_data->dev);

	if (dpi_index < 0)
		dpi_index = 0xff;

	p = &drv_data->profiles->profiles[profile->index];

	ratbag_profile_for_each_resolution(profile, res) {
		struct hidpp20_sensor *sensor;

		/* We only look at the first sensor. Multiple
		 * sensors is too niche to care about right now */
		sensor = &drv_data->sensors[0];

		dpi = p->dpi[res->index];

		/* If the resolution is zero dpi it is disabled,
		 * but internally we set the minimum value */
		if (dpi == 0) {
			res->is_disabled = true;
			dpi = sensor->dpi_min;
		}
		ratbag_resolution_set_resolution(res, dpi, dpi);

		if (profile->is_active &&
		    res->index == (unsigned int)dpi_index)
			res->is_active = true;
		if (res->index == p->default_dpi) {
			res->is_default = true;
			if (!profile->is_active || dpi_index < 0 || dpi_index > 4)
				res->is_active = true;
		}

		ratbag_resolution_set_dpi_list_from_range(res,
							  sensor->dpi_min,
							  sensor->dpi_max);
	}

	ratbag_profile_set_report_rate_list(profile,
					    drv_data->report_rates,
					    drv_data->num_report_rates);
	profile->hz = p->report_rate;
}

static int
hidpp20drv_init_leds_8070_8071(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag *ratbag = device->ratbag;

	/* we only support 0x8071 via 0x8100 */
	if (!(drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) &&
	    drv_data->capabilities & HIDPP_CAP_RGB_EFFECTS_8071) {
		log_debug(ratbag, "disabling 0x8071 (RGB Effects) feature because the device doesn't have 0x8100 (Onboard Memory Profiles)\n");
		drv_data->capabilities &= ~HIDPP_CAP_RGB_EFFECTS_8071;
	}

	if (drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070 ||
	    drv_data->capabilities & HIDPP_CAP_RGB_EFFECTS_8071) {
		/* we read the profile once to get the correct number of
		 * supported leds. */
		if (hidpp20drv_read_color_leds(device))
			return 0;

		device->num_leds = drv_data->num_leds;
	}

	return 0;
}

static int
hidpp20drv_init_profile_8100(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag *ratbag = device->ratbag;
	int rc;

	log_debug(ratbag, "initializing onboard profiles\n");
	rc = hidpp20_onboard_profiles_allocate(drv_data->dev, &drv_data->profiles);
	if (rc < 0)
		return rc;

	rc = hidpp20_onboard_profiles_initialize(drv_data->dev, drv_data->profiles);
	if (rc < 0)
		return rc;

	drv_data->num_profiles = drv_data->profiles->num_profiles;
	drv_data->num_buttons = drv_data->profiles->num_buttons;

	if (drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201)
		drv_data->num_resolutions = drv_data->profiles->num_modes;
	/* We ignore the profile's num_leds and require
	* HIDPP_PAGE_COLOR_LED_EFFECTS to succeed instead
	*/

	return 0;
}

static void
hidpp20drv_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	struct ratbag_button *button;

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		hidpp20drv_read_profile_8100(profile);
		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_DISABLE);

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);
		}
	} else {
		hidpp20drv_read_resolution_dpi(profile);
		hidpp20drv_read_special_key_mouse(device);

		profile->is_active = (profile->index == 0);
	}

	ratbag_profile_for_each_led(profile, led)
		hidpp20drv_read_led(led);

	ratbag_profile_for_each_button(profile, button)
		hidpp20drv_read_button(button);
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

	/* check if it is a hidden feature */
	if (feature_type & HIDPP_HIDDEN_FEATURE)
		return 0;

	switch (feature) {
	case HIDPP_PAGE_ROOT:
	case HIDPP_PAGE_FEATURE_SET:
		/* these features are mandatory and already handled */
		break;
	case HIDPP_PAGE_MOUSE_POINTER_BASIC: {
		drv_data->capabilities |= HIDPP_CAP_RESOLUTION_2200;
		break;
	}
	case HIDPP_PAGE_ADJUSTABLE_DPI: {
		log_debug(ratbag, "device has adjustable dpi\n");
		/* we read the profile once to get the correct number of
		 * supported resolutions. */
		rc = hidpp20drv_read_resolution_dpi_2201(device);
		if (rc < 0)
			return 0; /* this is not a hard failure */
		drv_data->capabilities |= HIDPP_CAP_SWITCHABLE_RESOLUTION_2201;
		break;
	}
	case HIDPP_PAGE_SPECIAL_KEYS_BUTTONS: {
		log_debug(ratbag, "device has programmable keys/buttons\n");
		drv_data->capabilities |= HIDPP_CAP_BUTTON_KEY_1b04;
		/* we read the profile once to get the correct number of
		 * supported buttons. */
		if (!hidpp20drv_read_special_key_mouse(device))
			drv_data->num_buttons = drv_data->num_controls;
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
	case HIDPP_PAGE_BATTERY_VOLTAGE: {
		uint16_t voltage;
		enum hidpp20_battery_voltage_status status;

		rc = hidpp20_batteryvoltage_get_battery_voltage(drv_data->dev, &voltage);
		if (rc < 0)
			return rc;

		status = rc;

		log_debug(ratbag, "device battery voltage is %dmv, status %02x \n",
			  voltage, status);

		drv_data->capabilities |= HIDPP_CAP_BATTERY_VOLTAGE_1001;
		break;
	}
	case HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS: {
		log_debug(ratbag, "device has programmable keys/buttons\n");
		drv_data->capabilities |= HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00;

		/* we read the profile once to get the correct number of
		 * supported buttons. */
		if (!hidpp20drv_read_kbd_reprogrammable_key(device))
			drv_data->num_buttons = drv_data->num_controls;
		break;
	}
	case HIDPP_PAGE_ADJUSTABLE_REPORT_RATE: {
		log_debug(ratbag, "device has adjustable report rate\n");

		/* we read the profile once to get the correct number of
		 * supported report rates. */
		rc = hidpp20drv_read_report_rate_8060(device);
		if (rc < 0)
			return 0; /* this is not a hard failure */

		drv_data->capabilities |= HIDPP_CAP_ADJUSTABLE_REPORT_RATE_8060;
		break;
	}
	case HIDPP_PAGE_COLOR_LED_EFFECTS: {
		/* The 8070 feature implemented in the G602 doesn't follow the spec,
		 * so we ignore it */
		if (ratbag_device_data_hidpp20_get_quirk(device->data) == HIDPP20_QUIRK_G602)
			break;

		log_debug(ratbag, "device has color effects\n");
		drv_data->capabilities |= HIDPP_CAP_COLOR_LED_EFFECTS_8070;
		break;
	}
	case HIDPP_PAGE_RGB_EFFECTS: {
		log_debug(ratbag, "device has color effects\n");
		drv_data->capabilities |= HIDPP_CAP_RGB_EFFECTS_8071;
		break;
	}
	case HIDPP_PAGE_LED_SW_CONTROL: {
		log_debug(ratbag, "device has non-rgb leds\n");
		drv_data->capabilities |= HIDPP_CAP_LED_SW_CONTROL_1300;

		if (drv_data->capabilities & HIDPP_PAGE_COLOR_LED_EFFECTS)
			return 0;

		if (hidpp20drv_read_leds(device))
			return 0;

		break;
	}
	case HIDPP_PAGE_ONBOARD_PROFILES: {
		log_debug(ratbag, "device has onboard profiles\n");
		drv_data->capabilities |= HIDPP_CAP_ONBOARD_PROFILES_8100;
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

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		if (profile->rate_dirty) {
			rc = hidpp20drv_update_report_rate(profile, profile->hz);
			if (rc) {
				log_error(device->ratbag, "hidpp20: failed to update report rate (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}
		}

		ratbag_profile_for_each_resolution(profile, resolution) {
			rc = hidpp20drv_update_resolution_dpi(resolution,
							      resolution->dpi_x,
							      resolution->dpi_y);
			if (rc) {
				log_error(device->ratbag, "hidpp20: failed to update resolution (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}
		}

		list_for_each(button, &profile->buttons, link) {
			if (!button->dirty)
				continue;

			rc = hidpp20drv_update_button(button);
			if (rc) {
				log_error(device->ratbag, "hidpp20: failed to update button (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}
		}

		list_for_each(led, &profile->leds, link) {
			if (!led->dirty)
				continue;

			rc = hidpp20drv_update_led(led);
			if (rc) {
				log_error(device->ratbag, "hidpp20: failed to update led (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}
		}
	}

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		list_for_each(profile, &device->profiles, link)
			drv_data->profiles->profiles[profile->index].enabled = profile->is_enabled;

		rc = hidpp20_onboard_profiles_commit(drv_data->dev,
						     drv_data->profiles);
		if (rc) {
			log_error(device->ratbag, "hidpp20: failed to commit profile (%d)\n", rc);
			return RATBAG_ERROR_DEVICE;
		}

		list_for_each(profile, &device->profiles, link) {
			if (profile->is_active) {
				ratbag_profile_for_each_resolution(profile, resolution) {
					if (resolution->is_active)
						hidpp20_onboard_profiles_set_current_dpi_index(drv_data->dev,
											       resolution->index);
				}
			}
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

	/* initializations that depend on other features */
	if (drv_data->capabilities & HIDPP_CAP_COLOR_LED_EFFECTS_8070 ||
	    drv_data->capabilities & HIDPP_CAP_RGB_EFFECTS_8071) {
		rc = hidpp20drv_init_leds_8070_8071(device);
		if (rc < 0)
			return rc;
	}

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		rc = hidpp20drv_init_profile_8100(device);
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

	log_msg_va(device->ratbag, (enum ratbag_log_priority)priority, format, args);
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
	free(drv_data->led_infos.color_leds_8070);
	free(drv_data->controls);
	free(drv_data->sensors);
	free(drv_data->leds);
	if (drv_data->dev)
		hidpp20_device_destroy(drv_data->dev);
	free(drv_data);
}

static void
hidpp20drv_init_device(struct ratbag_device *device,
		       struct hidpp20drv_data *drv_data)
{
	struct ratbag_profile *profile;
	bool active_profile = false;
	int num;

	num = ratbag_device_data_hidpp20_get_led_count(device->data);
	if (num >= 0)
		drv_data->num_leds = num;

	num = ratbag_device_data_hidpp20_get_button_count(device->data);
	if (num >= 0)
		drv_data->num_buttons = num;

	num = ratbag_device_data_hidpp20_get_report_rate(device->data);
	if (num >= 0)
		drv_data->report_rates[0] = num;

	ratbag_device_init_profiles(device,
				    drv_data->num_profiles,
				    drv_data->num_resolutions,
				    drv_data->num_buttons,
				    drv_data->num_leds);

	ratbag_device_for_each_profile(device, profile)
		hidpp20drv_read_profile(profile);

	if (drv_data->capabilities & HIDPP_CAP_ONBOARD_PROFILES_8100) {
		/* Fallback to the first profile if no profile is active */
		list_for_each(profile, &device->profiles, link)
			if (profile->is_active)
				active_profile = true;

		if (!active_profile && device->num_profiles >= 1) {
			profile = ratbag_device_get_profile(device, 0);
			profile->is_active = true;
		}
	}
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
	hidpp_device_init(&base, device->hidraw[0].fd);
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
	dev = hidpp20_device_new(&base, device_idx, (struct hidpp_hid_report*) device->hidraw[0].reports, device->hidraw[0].num_reports);
	if (!dev) {
		rc = -ENODEV;
		goto err;
	}

	dev->quirk = ratbag_device_data_hidpp20_get_quirk(device->data);

	drv_data->dev = dev;

	log_debug(device->ratbag, "'%s' is using protocol v%d.%d\n", ratbag_device_get_name(device), dev->proto_major, dev->proto_minor);

	if(dev->quirk != HIDPP20_QUIRK_NONE)
		log_debug(device->ratbag, "'%s' is quirked (%s)\n", ratbag_device_get_name(device), hidpp20_get_quirk_string(dev->quirk));

	/* add some defaults that will be overwritten by the device */
	drv_data->num_profiles = 1;
	drv_data->num_resolutions = 0;
	drv_data->num_buttons = 8;
	drv_data->num_leds = 0;

	rc = hidpp20drv_20_probe(device);
	if (rc)
		goto err;

	hidpp20drv_init_device(device, drv_data);

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
	.set_active_profile = hidpp20drv_set_current_profile,
};
