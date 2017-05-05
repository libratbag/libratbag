/***
  This file is part of ratbagd.

  Copyright 2016 Red Hat, Inc.

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice (including the next
  paragraph) shall be included in all copies or substantial portions of the
  Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
***/

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <libratbag.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include <linux/input.h>
#include "ratbagd.h"
#include "shared-macro.h"

struct ratbagd_button {
	struct ratbag_button *lib_button;
	unsigned int index;
	char *path;
};

static int ratbagd_button_get_type(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *reply,
				   void *userdata,
				   sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	const char *type = NULL;
	enum ratbag_button_type t;

	t = ratbag_button_get_type(button->lib_button);

	switch(t) {
		default:
			log_error("Unknown button type %d\n", t);
			/* fallthrough */
		case RATBAG_BUTTON_TYPE_UNKNOWN:
			type = "unknown";
			break;
		case RATBAG_BUTTON_TYPE_LEFT:
			type = "left";
			break;
		case RATBAG_BUTTON_TYPE_MIDDLE:
			type = "middle";
			break;
		case RATBAG_BUTTON_TYPE_RIGHT:
			type = "right";
			break;
		case RATBAG_BUTTON_TYPE_THUMB:
			type = "thumb";
			break;
		case RATBAG_BUTTON_TYPE_THUMB2:
			type = "thumb2";
			break;
		case RATBAG_BUTTON_TYPE_THUMB3:
			type = "thumb3";
			break;
		case RATBAG_BUTTON_TYPE_THUMB4:
			type = "thumb4";
			break;
		case RATBAG_BUTTON_TYPE_WHEEL_LEFT:
			type = "wheel-left";
			break;
		case RATBAG_BUTTON_TYPE_WHEEL_RIGHT:
			type = "wheel-right";
			break;
		case RATBAG_BUTTON_TYPE_WHEEL_CLICK:
			type = "wheel-click";
			break;
		case RATBAG_BUTTON_TYPE_WHEEL_UP:
			type = "wheel-up";
			break;
		case RATBAG_BUTTON_TYPE_WHEEL_DOWN:
			type = "wheel-down";
			break;
		case RATBAG_BUTTON_TYPE_WHEEL_RATCHET_MODE_SHIFT:
			type = "wheel-ratchet_mode_shift";
			break;
		case RATBAG_BUTTON_TYPE_EXTRA:
			type = "extra";
			break;
		case RATBAG_BUTTON_TYPE_SIDE:
			type = "side";
			break;
		case RATBAG_BUTTON_TYPE_PINKIE:
			type = "pinkie";
			break;
		case RATBAG_BUTTON_TYPE_PINKIE2:
			type = "pinkie2";
			break;
		case RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP:
			type = "resolution-cycle-up";
			break;
		case RATBAG_BUTTON_TYPE_RESOLUTION_UP:
			type = "resolution-up";
			break;
		case RATBAG_BUTTON_TYPE_RESOLUTION_DOWN:
			type = "resolution-down";
			break;
		case RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP:
			type = "profile-cycle-up";
			break;
		case RATBAG_BUTTON_TYPE_PROFILE_UP:
			type = "profile-up";
			break;
		case RATBAG_BUTTON_TYPE_PROFILE_DOWN:
			type = "profile-down";
			break;
	}

	return sd_bus_message_append(reply, "s", type);
}

static int ratbagd_button_get_button(sd_bus *bus,
				     const char *path,
				     const char *interface,
				     const char *property,
				     sd_bus_message *reply,
				     void *userdata,
				     sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int b;

	b = ratbag_button_get_button(button->lib_button);

	return sd_bus_message_append(reply, "u", b);
}

static int ratbagd_button_set_button(sd_bus_message *m,
				     void *userdata,
				     sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int map;
	int r;

	r = sd_bus_message_read(m, "u", &map);
	if (r < 0)
		return r;

	if (map == 0 || map > 30)
		return 0;

	r = ratbag_button_set_button(button->lib_button, map);

	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       "org.freedesktop.ratbag1.Button",
					       "ButtonMapping",
					       NULL);
	}

	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_button_get_special(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *reply,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	const char *type;
	enum ratbag_button_action_special special;

	special = ratbag_button_get_special(button->lib_button);

	switch(special) {
	default:
		log_error("Unknown special type %d\n", special);
		/* fallthrough */
	case RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN:
		type = "unknown";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_INVALID:
		type = "n/a";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK:
		type = "doubleclick";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT:
		type = "wheel-left";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT:
		type = "wheel-right";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
		type = "wheel-up";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
		type = "wheel-down";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH:
		type = "ratchet-mode-switch";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
		type = "resolution-cycle-up";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:
		type = "resolution-up";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:
		type = "resolution-down";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE:
		type = "resolution-alternate";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT:
		type = "resolution-default";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP:
		type = "profile-cycle-up";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP:
		type = "profile-up";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN:
		type = "profile-down";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE:
		type = "second-mode";
		break;
	case RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL:
		type = "battery-level";
		break;
	}

	return sd_bus_message_append(reply, "s", type);
}

static int ratbagd_button_set_special(sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	const char *s;
	enum ratbag_button_action_special special;
	int r;

	r = sd_bus_message_read(m, "s", &s);
	if (r < 0)
		return r;

	if (streq(s, "doubleclick"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK;
	else if (streq(s, "wheel-left"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT;
	else if (streq(s, "wheel-right"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT;
	else if (streq(s, "wheel-up"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP;
	else if (streq(s, "wheel-down"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN;
	else if (streq(s, "ratchet-mode-switch"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH;
	else if (streq(s, "resolution-cycle-up"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
	else if (streq(s, "resolution-up"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP;
	else if (streq(s, "resolution-down"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN;
	else if (streq(s, "resolution-alternate"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE;
	else if (streq(s, "resolution-default"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT;
	else if (streq(s, "profile-cycle-up"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP;
	else if (streq(s, "profile-up"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP;
	else if (streq(s, "profile-down"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN;
	else if (streq(s, "second-mode"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE;
	else if (streq(s, "battery-level"))
		special = RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL;
	else {
		log_error("Unknown button special action %s\n", s);
		return sd_bus_reply_method_return(m, "u", -1);
	}

	r = ratbag_button_set_special(button->lib_button, special);

	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       "org.freedesktop.ratbag1.Button",
					       "SpecialMapping",
					       NULL);
	}

	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_button_get_key(sd_bus *bus,
				  const char *path,
				  const char *interface,
				  const char *property,
				  sd_bus_message *reply,
				  void *userdata,
				  sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int key;
	unsigned int modifiers[10] = {0};
	size_t nmodifiers = ELEMENTSOF(modifiers);
	size_t i;
	int r;

	/* Return an array with the first element the key, the rest
	   are modifiers (or 0 if unset). If no key is set, the array is
	   just [0] */

	r = sd_bus_message_open_container(reply, 'a', "u");
	if (r < 0)
		return r;

	key = ratbag_button_get_key(button->lib_button,
				    modifiers,
				    &nmodifiers);

	r = sd_bus_message_append(reply, "u", key);
	if (r < 0)
		return r;

	if (key == 0)
		nmodifiers = 0;

	for (i = 0; i < nmodifiers; i++) {
		r = sd_bus_message_append(reply, "u", modifiers[0]);
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_button_set_key(sd_bus_message *m,
				  void *userdata,
				  sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int key;
	size_t nmodifiers = 10;
	unsigned int modifiers[nmodifiers];
	int r;

	/* Expect an array with the first element the key, the rest
	   are modifiers (or 0 if unset). */

	r = sd_bus_message_enter_container(m, 'a', "u");
	if (r < 0)
		return r;

	r = sd_bus_message_read(m, "u", &key);
	if (r < 0)
		return r;

	nmodifiers = 0;
	while ((r = sd_bus_message_read_basic(m, 'u',
					      &modifiers[nmodifiers++])) > 0)
		;

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return r;

	r = ratbag_button_set_key(button->lib_button, key, modifiers, nmodifiers);

	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_button_get_action_type(sd_bus *bus,
					  const char *path,
					  const char *interface,
					  const char *property,
					  sd_bus_message *reply,
					  void *userdata,
					  sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	const char *type;

	switch (ratbag_button_get_action_type(button->lib_button)) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		type = "none";
		break;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		type = "button";
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		type = "key";
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		type = "special";
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		type = "macro";
		break;
	case RATBAG_BUTTON_ACTION_TYPE_UNKNOWN:
		type = "unknown";
		break;
	}

	return sd_bus_message_append(reply, "s", type);
}

static int ratbagd_button_get_action_types(sd_bus *bus,
					   const char *path,
					   const char *interface,
					   const char *property,
					   sd_bus_message *reply,
					   void *userdata,
					   sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	const char *types[5] = {NULL};
	const char **t;
	int idx = 0;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "s");
	if (r < 0)
		return r;

	if (ratbag_button_has_action_type(button->lib_button,
					  RATBAG_BUTTON_ACTION_TYPE_BUTTON))
		types[idx++] = "button";
	if (ratbag_button_has_action_type(button->lib_button,
					  RATBAG_BUTTON_ACTION_TYPE_KEY))
		types[idx++] = "key";
	if (ratbag_button_has_action_type(button->lib_button,
					  RATBAG_BUTTON_ACTION_TYPE_SPECIAL))
		types[idx++] = "special";
	if (ratbag_button_has_action_type(button->lib_button,
					  RATBAG_BUTTON_ACTION_TYPE_MACRO))
		types[idx++] = "macro";

	t = types;
	while (*t) {
		r = sd_bus_message_append(reply, "s", *t);
		if (r < 0)
			return r;
		t++;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_button_disable(sd_bus_message *m,
				  void *userdata,
				  sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	int r;

	r = sd_bus_message_read(m, "");
	if (r < 0)
		return r;

	r = ratbag_button_disable(button->lib_button);

	return sd_bus_reply_method_return(m, "u", r);
}

const sd_bus_vtable ratbagd_button_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_button, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Type", "s", ratbagd_button_get_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ButtonMapping", "u", ratbagd_button_get_button, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("SpecialMapping", "s", ratbagd_button_get_special, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("KeyMapping", "au", ratbagd_button_get_key, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ActionType", "s", ratbagd_button_get_action_type, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ActionTypes", "as", ratbagd_button_get_action_types, 0, SD_BUS_VTABLE_PROPERTY_CONST),

	SD_BUS_METHOD("SetButtonMapping", "u", "u", ratbagd_button_set_button, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetSpecialMapping", "s", "u", ratbagd_button_set_special, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetKeyMapping", "au", "u", ratbagd_button_set_key, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("Disable", "", "u", ratbagd_button_disable, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int ratbagd_button_new(struct ratbagd_button **out,
		       struct ratbagd_device *device,
		       struct ratbagd_profile *profile,
		       struct ratbag_button *lib_button,
		       unsigned int index)
{
	_cleanup_(ratbagd_button_freep) struct ratbagd_button *button = NULL;
	char profile_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1],
	     button_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	assert(out);
	assert(lib_button);

	button = calloc(1, sizeof(*button));
	if (!button)
		return -ENOMEM;

	button->lib_button = lib_button;
	button->index = index;

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(button_buffer, "b%u", index);
	r = sd_bus_path_encode_many(&button->path,
				    "/org/freedesktop/ratbag1/button/%/%/%",
				    ratbagd_device_get_name(device),
				    profile_buffer,
				    button_buffer);
	if (r < 0)
		return r;

	*out = button;
	button = NULL;
	return 0;
}

const char *ratbagd_button_get_path(struct ratbagd_button *button)
{
	assert(button);
	return button->path;
}

struct ratbagd_button *ratbagd_button_free(struct ratbagd_button *button)
{
	if (!button)
		return NULL;

	button->path = mfree(button->path);
	button->lib_button = ratbag_button_unref(button->lib_button);

	return mfree(button);
}

