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
	const char *event_node_prefix = "/dev/input/event";

	if (strncmp(path, event_node_prefix, strlen(event_node_prefix)) == 0) {
		struct stat st;
		if (stat(path, &st) == -1) {
			error("Failed to stat '%s': %s\n", path, strerror(errno));
			return NULL;
		}
		udev_device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	} else {
		udev_device = udev_device_new_from_syspath(udev, path);
	}
	if (!udev_device) {
		error("Can't open '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	return udev_device;
}

const char*
button_type_to_str(enum ratbag_button_type type)
{
	const char *str = "UNKNOWN";

	switch(type) {
	case RATBAG_BUTTON_TYPE_UNKNOWN:	str = "unknown"; break;
	case RATBAG_BUTTON_TYPE_LEFT:		str = "left"; break;
	case RATBAG_BUTTON_TYPE_MIDDLE:		str = "middle"; break;
	case RATBAG_BUTTON_TYPE_RIGHT:		str = "right"; break;
	case RATBAG_BUTTON_TYPE_THUMB:		str = "thumb"; break;
	case RATBAG_BUTTON_TYPE_THUMB2:		str = "thumb2"; break;
	case RATBAG_BUTTON_TYPE_THUMB3:		str = "thumb3"; break;
	case RATBAG_BUTTON_TYPE_THUMB4:		str = "thumb4"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_LEFT:	str = "wheel left"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_RIGHT:	str = "wheel right"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_CLICK:	str = "wheel click"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_UP:	str = "wheel up"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_DOWN:	str = "wheel down"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_RATCHET_MODE_SHIFT: str = "wheel ratchet mode switch"; break;
	case RATBAG_BUTTON_TYPE_EXTRA:		str = "extra (forward)"; break;
	case RATBAG_BUTTON_TYPE_SIDE:		str = "side (backward)"; break;
	case RATBAG_BUTTON_TYPE_PINKIE:		str = "pinkie"; break;
	case RATBAG_BUTTON_TYPE_PINKIE2:	str = "pinkie2"; break;

	/* DPI switch */
	case RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP:	str = "resolution cycle up"; break;
	case RATBAG_BUTTON_TYPE_RESOLUTION_UP:		str = "resolution up"; break;
	case RATBAG_BUTTON_TYPE_RESOLUTION_DOWN:	str = "resolution down"; break;

	/* Profile */
	case RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP:	str = "profile cycle up"; break;
	case RATBAG_BUTTON_TYPE_PROFILE_UP:		str = "profile up"; break;
	case RATBAG_BUTTON_TYPE_PROFILE_DOWN:		str = "profile down"; break;
	}

	return str;
}

const char *
button_action_special_to_str(struct ratbag_button *button)
{
	enum ratbag_button_action_special special;
	const char *str = "UNKNOWN";

	special = ratbag_button_get_special(button);

	switch (special) {
	case RATBAG_BUTTON_ACTION_SPECIAL_INVALID:		str = "invalid"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN:		str = "unknown"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK:		str = "double click"; break;

	/* Wheel mappings */
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT:		str = "wheel left"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT:		str = "wheel right"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:		str = "wheel up"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:		str = "wheel down"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH:	str = "ratchet mode switch"; break;

	/* DPI switch */
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:	str = "resolution cycle up"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:	str = "resolution up"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:	str = "resolution down"; break;

	/* Profile */
	case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP:	str = "profile cycle up"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP:		str = "profile up"; break;
	case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN:		str = "profile down"; break;
	}

	return str;
}

char *
button_action_button_to_str(struct ratbag_button *button)
{
	char str[96];

	sprintf(str, "button %d", ratbag_button_get_button(button));

	return strdup(str);
}

char *
button_action_key_to_str(struct ratbag_button *button)
{
	const char *str;
	unsigned int modifiers[10];
	size_t m_size = 10;

	str = libevdev_event_code_get_name(EV_KEY, ratbag_button_get_key(button, modifiers, &m_size));
	if (!str)
		str = "UNKNOWN";

	return strdup(str);
}

static const char *strip_ev_key(int key)
{
	const char *str = libevdev_event_code_get_name(EV_KEY, key);

	if (!strncmp(str, "KEY_", 4))
		return str + 4;
	return str;
};

char *
button_action_macro_to_str(struct ratbag_button *button)
{
	char str[4096] = {0};
	const char *name;
	int offset;
	unsigned int i;

	name = ratbag_button_get_macro_name(button);
	offset = snprintf(str, sizeof(str), "macro \"%s\":",
			  name ? name : "UNKNOWN");
	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		enum ratbag_macro_event_type type = ratbag_button_get_macro_event_type(button, i);
		int key = ratbag_button_get_macro_event_key(button, i);
		int timeout = ratbag_button_get_macro_event_timeout(button, i);

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
			offset += snprintf(str + offset, sizeof(str) - offset, " %d.%d⏱", timeout / 1000, timeout % 1000);
		default:
			offset += snprintf(str + offset, sizeof(str) - offset, " ###");
		}
	}

	return strdup(str);
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
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:	str = strdup(button_action_special_to_str(button)); break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:	str = button_action_macro_to_str(button); break;
	case RATBAG_BUTTON_ACTION_TYPE_NONE:	str = strdup("none"); break;
	default:
		error("type %d unknown\n", type);
		str = strdup("UNKNOWN");
	}

	return str;
}

struct ratbag_device *
ratbag_cmd_open_device(struct ratbag *ratbag, const char *path)
{
	struct ratbag_device *device;
	struct udev *udev;
	struct udev_device *udev_device;

	udev = udev_new();
	udev_device = udev_device_from_path(udev, path);
	if (!udev_device) {
		udev_unref(udev);
		return NULL;
	}

	device = ratbag_device_new_from_udev_device(ratbag, udev_device);

	udev_device_unref(udev_device);
	udev_unref(udev);

	return device;
}

enum ratbag_button_action_special
str_to_special_action(const char *str) {
	struct map {
		enum ratbag_button_action_special special;
		const char *str;
	} map[] =  {
	{ RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK,		"doubleclick" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,		"wheel left" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,		"wheel right" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP,		"wheel up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN,		"wheel down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,	"resolution cycle up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,		"resolution up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,		"resolution down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,	"profile cycle up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP,		"profile up" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN,		"profile down" },
	{ RATBAG_BUTTON_ACTION_SPECIAL_INVALID,		NULL },
	};
	struct map *m = map;

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
