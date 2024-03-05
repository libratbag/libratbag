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
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp10.h"
#include "usb-ids.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-data.h"

struct hidpp10drv_data {
	struct hidpp10_device *dev;
};

static unsigned int
hidpp10drv_read_macro_modifier(struct ratbag_device *device, union hidpp10_macro_data *macro)
{
	switch (macro->key.key) {
	case 0x01: return KEY_LEFTCTRL;
	case 0x02: return KEY_LEFTSHIFT;
	case 0x04: return KEY_LEFTALT;
	case 0x08: return KEY_LEFTMETA;
	case 0x10: return KEY_RIGHTCTRL;
	case 0x20: return KEY_RIGHTSHIFT;
	case 0x40: return KEY_RIGHTALT;
	case 0x80: return KEY_RIGHTMETA;
	}

	return KEY_RESERVED;
}

static void
hidpp10drv_read_macro(struct ratbag_button *button,
		      struct hidpp10_profile *profile,
		      union hidpp10_button *binding)
{
	struct ratbag_device *device = button->profile->device;
	struct ratbag_button_macro *m;
	const char *name;
	union hidpp10_macro_data *macro;
	unsigned int i, keycode;
	bool delay = true;

	macro = profile->macros[binding->macro.address];

	name = binding->macro.address > 1 ? (const char *)profile->macro_names[binding->macro.address - 2] : "";
	i = 0;
	m = ratbag_button_macro_new(name);

	while (macro && macro->any.type != HIDPP10_MACRO_END && i < MAX_MACRO_EVENTS) {
		switch (macro->any.type) {
		case HIDPP10_MACRO_DELAY:
			ratbag_button_macro_set_event(m,
						      i++,
						      RATBAG_MACRO_EVENT_WAIT,
						      macro->delay.time);
			delay = true;
			break;
		case HIDPP10_MACRO_KEY_PRESS:
			keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(device, macro->key.key);
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
		case HIDPP10_MACRO_KEY_RELEASE:
			keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(device, macro->key.key);
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
		case HIDPP10_MACRO_MOD_PRESS:
			keycode = hidpp10drv_read_macro_modifier(device, macro);
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
		case HIDPP10_MACRO_MOD_RELEASE:
			keycode = hidpp10drv_read_macro_modifier(device, macro);
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
}

static void
hidpp10drv_map_button(struct ratbag_device *device,
		      struct hidpp10_device *hidpp10,
		      struct ratbag_button *button)
{
	struct hidpp10_profile profile;
	int ret;
	unsigned int modifiers = 0;

	ret = hidpp10_get_profile(hidpp10, button->profile->index, &profile);
	if (ret)
		return;

	switch (profile.buttons[button->index].any.type) {
	case PROFILE_BUTTON_TYPE_BUTTON:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
		button->action.action.button = profile.buttons[button->index].button.button;
		break;
	case PROFILE_BUTTON_TYPE_KEYS:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		button->action.action.key = ratbag_hidraw_get_keycode_from_keyboard_usage(device,
							profile.buttons[button->index].keys.key);
		modifiers = profile.buttons[button->index].keys.modifier_flags;
		break;
	case PROFILE_BUTTON_TYPE_CONSUMER_CONTROL:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		button->action.action.key = ratbag_hidraw_get_keycode_from_consumer_usage(device,
							profile.buttons[button->index].consumer_control.consumer_control);
		break;
	case PROFILE_BUTTON_TYPE_SPECIAL:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		button->action.action.special = hidpp10_onboard_profiles_get_special(profile.buttons[button->index].special.special);
		break;
	case PROFILE_BUTTON_TYPE_DISABLED:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		break;
	default:
		if (profile.buttons[button->index].any.type & 0x80) {
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		} else {
			hidpp10drv_read_macro(button, &profile, &profile.buttons[button->index]);
		}
	}

	if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_KEY) {
		ret = ratbag_button_macro_new_from_keycode(button, button->action.action.key, modifiers);
		if (ret < 0) {
			log_error(device->ratbag, "hidpp10: error while reading button %d\n", button->index);
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		}
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
}

static void
hidpp10drv_read_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp10drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp10_device *hidpp10 = drv_data->dev;

	switch (hidpp10->profile_type) {
	case HIDPP10_PROFILE_G500:
	case HIDPP10_PROFILE_G700:
	case HIDPP10_PROFILE_G9:
		hidpp10drv_map_button(device, hidpp10, button);
		break;
	default:
		break;
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
}

static int
hidpp10drv_write_button(struct hidpp10_profile *profile,
			struct ratbag_button *button,
			const struct ratbag_button_action *action)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp10drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp10_device *hidpp10 = drv_data->dev;
	uint8_t code;
	unsigned int modifiers, key;
	int rc;

	if (hidpp10->profile_type == HIDPP10_PROFILE_UNKNOWN)
		return -ENOTSUP;

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		profile->buttons[button->index].button.type = PROFILE_BUTTON_TYPE_BUTTON;
		profile->buttons[button->index].button.button = action->action.button;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, action->action.key);
		if (code == 0) {
			code = ratbag_hidraw_get_consumer_usage_from_keycode(device, action->action.key);
			if (code == 0)
				return -EINVAL;

			profile->buttons[button->index].consumer_control.type = PROFILE_BUTTON_TYPE_CONSUMER_CONTROL;
			profile->buttons[button->index].consumer_control.consumer_control = code;
		} else {
			profile->buttons[button->index].keys.type = PROFILE_BUTTON_TYPE_KEYS;
			profile->buttons[button->index].keys.key = code;
		}
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		code = hidpp10_onboard_profiles_get_code_from_special(action->action.special);
		if (code == 0)
			return -EINVAL;
		profile->buttons[button->index].special.type = PROFILE_BUTTON_TYPE_SPECIAL;
		profile->buttons[button->index].special.special = code;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		rc = ratbag_action_keycode_from_macro(action, &key, &modifiers);
		if (rc < 0) {
			log_error(device->ratbag, "hidpp10: can't convert macro action to keycode in button %d\n", button->index);
			return -EINVAL;
		}

		code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
		if (code == 0) {
			code = ratbag_hidraw_get_consumer_usage_from_keycode(device, key);
			if (code == 0)
				return -EINVAL;

			profile->buttons[button->index].consumer_control.type = PROFILE_BUTTON_TYPE_CONSUMER_CONTROL;
			profile->buttons[button->index].consumer_control.consumer_control = code;
		} else {
			profile->buttons[button->index].keys.type = PROFILE_BUTTON_TYPE_KEYS;
			profile->buttons[button->index].keys.key = code;
			profile->buttons[button->index].keys.modifier_flags = modifiers;
		}
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static void
hidpp10drv_read_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	struct hidpp10drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp10_device *hidpp10 = drv_data->dev;
	struct hidpp10_profile profile;
	int ret;

	ret = hidpp10_get_profile(hidpp10, led->profile->index, &profile);
	if (ret)
		return;

	switch (hidpp10->profile_type) {
	case HIDPP10_PROFILE_G500:
		led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
		break;
	default:
		led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;
		break;
	}

	led->mode = RATBAG_LED_ON;
	led->color.red = profile.red;
	led->color.green = profile.green;
	led->color.blue = profile.blue;
}

static void
hidpp10drv_write_led(struct hidpp10_profile *profile,
		     struct ratbag_led *led)
{
	profile->red = led->color.red;
	profile->green = led->color.green;
	profile->blue = led->color.blue;
}

static int
hidpp10drv_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	struct hidpp10drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp10_device *hidpp10 = drv_data->dev;

	return hidpp10_set_current_profile(hidpp10, index);
}

static void
hidpp10drv_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct hidpp10drv_data *drv_data;
	struct hidpp10_device *hidpp10;
	struct hidpp10_profile p = {0};
	struct ratbag_resolution *res;
	int rc;
	uint16_t xres, yres;
	uint8_t idx;

	/* 0x64 USB_REFRESH_RATE has time between reports in ms. so let's
	 * assume the 1000/500/250 rates exist on the devices */
	unsigned int rates[] = {250, 500, 1000};

	drv_data = ratbag_get_drv_data(device);
	hidpp10 = drv_data->dev;
	rc = hidpp10_get_profile(hidpp10, profile->index, &p);
	if (rc)
		return;

	if (hidpp10->profile_type != HIDPP10_PROFILE_UNKNOWN)
		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_DISABLE);

	profile->is_enabled = p.enabled;

	if (profile->name) {
		free(profile->name);
		profile->name = NULL;
	}
	if (p.name[0] != '\0')
		profile->name = strdup_safe((char*)p.name);

	rc = hidpp10_get_current_profile(hidpp10, &idx);
	if (rc == 0 && (unsigned int)idx == profile->index)
		profile->is_active = true;

	rc = hidpp10_get_current_resolution(hidpp10, &xres, &yres);
	if (rc)
		xres = 0xffff;

	ratbag_profile_for_each_resolution(profile, res) {
		unsigned int dpis[hidpp10->dpi_count];

		ratbag_resolution_set_resolution(res,
						 p.dpi_modes[res->index].xres,
						 p.dpi_modes[res->index].yres);
		ratbag_resolution_set_cap(res,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		if (profile->is_active &&
		    res->dpi_x == xres &&
		    res->dpi_y == yres)
			res->is_active = true;
		if (res->index == p.default_dpi_mode) {
			res->is_default = true;
			if (!profile->is_active)
				res->is_active = true;
		}

		if (hidpp10->dpi_table_is_range) {
			unsigned int min, max;

			min = hidpp10_dpi_table_get_min_dpi(hidpp10);
			max = hidpp10_dpi_table_get_max_dpi(hidpp10);
			/* FIXME: this relies on libratbag using the
			 * same steps that we support */
			ratbag_resolution_set_dpi_list_from_range(res, min, max);
		} else if (hidpp10->dpi_count > 0) {
			for (uint8_t i = 0; i < hidpp10->dpi_count; i++)
				dpis[i] = hidpp10->dpi_table[i].dpi;

			ratbag_resolution_set_dpi_list(res, dpis, hidpp10->dpi_count);
		}

	}

	ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));
	profile->hz = p.refresh_rate;

	ratbag_profile_for_each_button(profile, button)
		hidpp10drv_read_button(button);

	ratbag_profile_for_each_led(profile, led)
		hidpp10drv_read_led(led);
}

static int
hidpp10drv_fill_from_profile(struct ratbag_device *device, struct hidpp10_device *dev)
{
	int rc, num_leds;
	struct hidpp10_profile profile = {0};
	unsigned int i;

	/* We don't know the HID++1.0 requests to query for buttons, etc.
	 * Simply get the first enabled profile and fill in the device
	 * information from that.
	 */
	for (i = 0; i < dev->profile_count; i++) {
		rc = hidpp10_get_profile(dev, i, &profile);
		if (rc)
			return rc;
		if (profile.enabled)
			break;
	}

	/* let the .device file override LED count from the profile */
	num_leds = ratbag_device_data_hidpp10_get_led_count(device->data);
	if (num_leds >= 0)
		profile.num_leds = num_leds;

	ratbag_device_init_profiles(device,
				    dev->profile_count,
				    profile.num_dpi_modes,
				    profile.num_buttons,
				    profile.num_leds);

	return 0;
}

static int
hidpp10drv_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, REPORT_ID_SHORT);
}

static void
hidpp10_log(void *userdata, enum hidpp_log_priority priority, const char *format, va_list args)
{
	struct ratbag_device *device = userdata;

	log_msg_va(device->ratbag, (enum ratbag_log_priority)priority, format, args);
}

static int
hidpp10drv_commit(struct ratbag_device *device)
{
	struct hidpp10drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp10_device *dev = drv_data->dev;
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;
	struct ratbag_resolution *active_resolution = NULL;
	struct hidpp10_profile p;
	int rc;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		rc = hidpp10_get_profile(dev, profile->index, &p);
		if (rc)
			return rc;

		p.enabled = profile->is_enabled;
		if (profile->name != NULL)
			strncpy_safe((char*)p.name, profile->name, 24);

		ratbag_profile_for_each_resolution(profile, resolution) {
			p.dpi_modes[resolution->index].xres = resolution->dpi_x;
			p.dpi_modes[resolution->index].yres = resolution->dpi_y;

			if (profile->is_active && resolution->is_active)
				active_resolution = resolution;
		}

		list_for_each(button, &profile->buttons, link) {
			struct ratbag_button_action action = button->action;

			if (!button->dirty)
				continue;

			rc = hidpp10drv_write_button(&p, button, &action);
			if (rc) {
				log_error(device->ratbag, "hidpp10: failed to update buttons (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}
		}

		ratbag_profile_for_each_led(profile, led)
			hidpp10drv_write_led(&p, led);

		if (dev->profile_type != HIDPP10_PROFILE_UNKNOWN) {
			rc = hidpp10_set_profile(dev, profile->index, &p);
			if (rc) {
				log_error(device->ratbag, "hidpp10: failed to set profile (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}
		}

		/* Update the current resolution in case it changed */
		if (active_resolution) {
			rc = hidpp10_set_current_resolution(dev,
						       active_resolution->dpi_x,
						       active_resolution->dpi_y);
			if (rc) {
				log_error(device->ratbag, "hidpp10: failed to set active resolution (%d)\n", rc);
				return RATBAG_ERROR_DEVICE;
			}

			active_resolution = NULL;
		}
	}

	return RATBAG_SUCCESS;
}

static int
hidpp10drv_probe(struct ratbag_device *device)
{
	int rc;
	struct hidpp10drv_data *drv_data = NULL;
	struct hidpp10_device *dev = NULL;
	struct hidpp_device base;
	enum hidpp10_profile_type type = HIDPP10_PROFILE_UNKNOWN;
	const char *typestr;
	int device_idx = HIDPP_WIRED_DEVICE_IDX;
	unsigned int profile_count = 1;

	rc = ratbag_find_hidraw(device, hidpp10drv_test_hidraw);
	if (rc)
		goto err;

	drv_data = zalloc(sizeof(*drv_data));
	hidpp_device_init(&base, device->hidraw[0].fd);
	hidpp_device_set_log_handler(&base, hidpp10_log, HIDPP_LOG_PRIORITY_RAW, device);

	typestr = ratbag_device_data_hidpp10_get_profile_type(device->data);
	if (typestr) {
		if (strcasecmp("G500", typestr) == 0)
			type = HIDPP10_PROFILE_G500;
		else if (strcasecmp("G700", typestr) == 0)
			type = HIDPP10_PROFILE_G700;
		else if (strcasecmp("G9", typestr) == 0)
			type = HIDPP10_PROFILE_G9;

		rc = ratbag_device_data_hidpp10_get_profile_count(device->data);
		if (rc == -1) {
			log_error(device->ratbag,
				  "Device %s has no profile count set, even though profiles are enabled. "
				  "Please adjust the .device file.\n",
				  device->name);
		} else {
			profile_count = (unsigned int)rc;
		};
	}

	device_idx = ratbag_device_data_hidpp10_get_index(device->data);
	if (device_idx == -1)
		device_idx = HIDPP_WIRED_DEVICE_IDX;

	/* In the general case, we can treat all devices as wired devices
	 * here. If we talk to the correct hidraw device the kernel adjusts
	 * the device index for us, so even for unifying receiver devices
	 * we can just use 0x00 as device index.
	 *
	 * If there is a special need like for G700(s), then add a DeviceIndex
	 * entry to the .device file.
	 */
	rc = hidpp10_device_new(&base, device_idx, type, profile_count, &dev);

	if (rc) {
		log_error(device->ratbag,
			  "Failed to get HID++1.0 device for %s\n",
			  device->name);
		goto err;
	}

	if (type != HIDPP10_PROFILE_UNKNOWN) {
		_cleanup_(dpi_list_freep) struct dpi_list *list = NULL;
		_cleanup_(freep) struct dpi_range *range = NULL;

		range = ratbag_device_data_hidpp10_get_dpi_range(device->data);
		if (range) {
			rc = hidpp10_build_dpi_table_from_dpi_info(dev, range);
			if (rc)
				log_error(device->ratbag,
					  "Error parsing DpiRange for %s\n",
					  device->name);
		}

		list = ratbag_device_data_hidpp10_get_dpi_list(device->data);
		if (list) {
			rc = hidpp10_build_dpi_table_from_list(dev, list);
			if (rc)
				log_error(device->ratbag,
					  "Error parsing DpiList for %s\n",
					  device->name);
		}

		if (!dev->dpi_count)
			log_info(device->ratbag,
				  "Device %s might have wrong dpi settings. "
				  "Please adjust the .device file.\n",
				  device->name);
	}

	rc = hidpp10_device_read_profiles(dev);
	if (rc)
		goto err;

	drv_data->dev = dev;
	ratbag_set_drv_data(device, drv_data);

	if (hidpp10drv_fill_from_profile(device, dev)) {
		/* Fall back to something that every mouse has */
		_cleanup_profile_ struct ratbag_profile *profile;

		ratbag_device_init_profiles(device, 1, 1, 3, 0);
		profile = ratbag_device_get_profile(device, 0);
		profile->is_active = true;
	}

	{
		struct ratbag_profile *profile;
		ratbag_device_for_each_profile(device, profile)
			hidpp10drv_read_profile(profile);
	}

	if (device->num_profiles == 1) {
		_cleanup_profile_ struct ratbag_profile *profile;

		profile = ratbag_device_get_profile(device, 0);
		if (!profile->is_active) {
			log_debug(device->ratbag,
				  "%s: forcing profile 0 to active.\n",
				  device->name);
			profile->is_active = true;
		}
	}

	return 0;
err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	if (dev)
		hidpp10_device_destroy(dev);

	return rc;
}

static void
hidpp10drv_remove(struct ratbag_device *device)
{
	struct hidpp10drv_data *drv_data;

	ratbag_close_hidraw(device);

	drv_data = ratbag_get_drv_data(device);
	if (!drv_data)
		return;

	hidpp10_device_destroy(drv_data->dev);

	free(drv_data);
}

struct ratbag_driver hidpp10_driver = {
	.name = "Logitech HID++1.0",
	.id = "hidpp10",
	.probe = hidpp10drv_probe,
	.remove = hidpp10drv_remove,
	.set_active_profile = hidpp10drv_set_current_profile,
	.commit = hidpp10drv_commit,
};
