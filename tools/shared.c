/*
 * Copyright © 2015 Red Hat, Inc.
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

#include "shared.h"

struct udev_device*
udev_device_from_path(struct udev *udev, const char *path)
{
	struct udev_device *udev_device;
	const char *hidraw_prefix = "/dev/";
	_cleanup_(freep) char *path_canonical = NULL;

	if ((path_canonical = realpath(path, NULL)) == NULL) {
		error("Failed to canonicalize path '%s': %s\n", path, strerror(errno));
		return NULL;
	}
	if (strneq(path_canonical, hidraw_prefix, strlen(hidraw_prefix))) {
		struct stat st;
		if (stat(path_canonical, &st) == -1) {
			error("Failed to stat '%s': %s\n", path, strerror(errno));
			return NULL;
		}
		udev_device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	} else {
		udev_device = udev_device_new_from_syspath(udev, path_canonical);
	}
	if (!udev_device) {
		error("Can't open '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	return udev_device;
}

const char *
led_mode_to_str(enum ratbag_led_mode mode)
{
	const char *str = "UNKNOWN";
	switch (mode) {
	case RATBAG_LED_OFF:
		str = "off";
		break;
	case RATBAG_LED_ON:
		str = "on";
		break;
	case RATBAG_LED_CYCLE:
		str = "cycle";
		break;
	case RATBAG_LED_BREATHING:
		str = "breathing";
		break;
	}

	return str;
}

static const struct map {
	enum ratbag_button_action_special special;
	const char *str;
} special_map[] =  {
	{ RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN,			"unknown" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK,		"doubleclick" },

	/* Wheel mappings */
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,		"wheel left" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,		"wheel right" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP,		"wheel up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN,		"wheel down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH,	"ratchet mode switch" },

	/* DPI switch */
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,	"resolution cycle up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN,	"resolution cycle down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,		"resolution up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,		"resolution down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE,	"resolution alternate" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT,	"resolution default" },

	/* Profile */
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,	"profile cycle up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_DOWN,	"profile cycle down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP,		"profile up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN,		"profile down" },

	/* Second mode for buttons */
	{ RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE,		"secondary mode" },

	/* battery level */
	{ RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL,		"battery level" },

	/* must be the last entry in the table */
	{ RATBAG_BUTTON_ACTION_SPECIAL_INVALID,		NULL },
};

const char *
button_action_special_to_str(struct ratbag_button *button)
{
	enum ratbag_button_action_special special;
	const struct map *m = special_map;

	special = ratbag_button_get_special(button);

	while (m->special != RATBAG_BUTTON_ACTION_SPECIAL_INVALID) {
		if (m->special == special)
			return m->str;
		m++;
	}
	return "UNKNOWN";
}

char *
button_action_button_to_str(struct ratbag_button *button)
{
	char str[96];

	sprintf_safe(str, "button %d", ratbag_button_get_button(button));

	return strdup_safe(str);
}

char *
button_action_key_to_str(struct ratbag_button *button)
{
	const char *str;

	str = libevdev_event_code_get_name(EV_KEY, ratbag_button_get_key(button));
	if (!str)
		str = "UNKNOWN";

	return strdup_safe(str);
}

static const char *strip_ev_key(int key)
{
	const char *str = libevdev_event_code_get_name(EV_KEY, key);

	if (strneq(str, "KEY_", 4))
		return str + 4;
	return str;
};

char *
button_action_macro_to_str(struct ratbag_button *button)
{
	struct ratbag_button_macro *macro;
	char str[4096] = {0};
	const char *name;
	int offset;
	unsigned int i;

	macro = ratbag_button_get_macro(button);
	name = ratbag_button_macro_get_name(macro);
	offset = snprintf(str, sizeof(str), "macro \"%s\":",
			  name ? name : "UNKNOWN");
	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		if (offset < 0 || offset >= (int)sizeof(str) - offset) {
			break;
		}

		enum ratbag_macro_event_type type = ratbag_button_macro_get_event_type(macro, i);
		int key = ratbag_button_macro_get_event_key(macro, i);
		int timeout = ratbag_button_macro_get_event_timeout(macro, i);

		if (type == RATBAG_MACRO_EVENT_NONE)
			break;

		switch (type) {
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			offset += snprintf(str + offset, sizeof(str) - offset, " %s↓", strip_ev_key(key));
			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			offset += snprintf(str + offset, sizeof(str) - offset, " %s↑", strip_ev_key(key));
			break;
		case RATBAG_MACRO_EVENT_WAIT:
			offset += snprintf(str + offset, sizeof(str) - offset, " %.03f⏱", timeout / 1000.0);
			break;
		default:
			offset += snprintf(str + offset, sizeof(str) - offset, " ###");
		}
	}

	ratbag_button_macro_unref(macro);

	return strdup_safe(str);
}

char *
button_action_to_str(struct ratbag_button *button)
{
	enum ratbag_button_action_type type;
	char *str;

	type = ratbag_button_get_action_type(button);

	switch (type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:	str = button_action_button_to_str(button); break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:	str = button_action_key_to_str(button); break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:	str = strdup_safe(button_action_special_to_str(button)); break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:	str = button_action_macro_to_str(button); break;
	case RATBAG_BUTTON_ACTION_TYPE_NONE:	str = strdup_safe("none"); break;
	default:
		error("type %d unknown\n", type);
		str = strdup_safe("UNKNOWN");
	}

	return str;
}

struct ratbag_device *
ratbag_cmd_open_device(struct ratbag *ratbag, const char *path)
{
	struct ratbag_device *device;
	_cleanup_(udev_unrefp) struct udev *udev = NULL;
	_cleanup_(udev_device_unrefp) struct udev_device *udev_device = NULL;
	enum ratbag_error_code error;

	udev = udev_new();
	udev_device = udev_device_from_path(udev, path);
	if (!udev_device)
		return NULL;

	error = ratbag_device_new_from_udev_device(ratbag, udev_device,
						   &device);
	if (error != RATBAG_SUCCESS)
		return NULL;

	return device;
}

enum ratbag_button_action_special
str_to_special_action(const char *str) {
	const struct map *m = special_map;

	while (m->str) {
		if (streq(m->str, str))
			return m->special;
		m++;
	}
	return RATBAG_BUTTON_ACTION_SPECIAL_INVALID;
}


static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);

	if (fd < 0)
		error("Failed to open %s (%s)\n",
			path, strerror(errno));

	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

const struct ratbag_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};
