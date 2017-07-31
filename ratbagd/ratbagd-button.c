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
#include "libratbag-util.h"

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
	enum ratbag_button_type t;

	t = ratbag_button_get_type(button->lib_button);

	return sd_bus_message_append(reply, "u", t);
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
					       RATBAGD_NAME_ROOT ".Button",
					       "ButtonMapping",
					       NULL);

		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "ActionType",
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
	enum ratbag_button_action_special special;

	special = ratbag_button_get_special(button->lib_button);

	return sd_bus_message_append(reply, "u", special);
}

static int ratbagd_button_set_special(sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	enum ratbag_button_action_special special;
	int r;

	r = sd_bus_message_read(m, "u", &special);
	if (r < 0)
		return r;

	r = ratbag_button_set_special(button->lib_button, special);

	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "SpecialMapping",
					       NULL);

		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "ActionType",
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

	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "KeyMapping",
					       NULL);

		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "ActionType",
					       NULL);
	}

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
	enum ratbag_button_action_type type;

	type = ratbag_button_get_action_type(button->lib_button);

	return sd_bus_message_append(reply, "u", type);
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
	int r;
	enum ratbag_button_action_type types[] = {
		RATBAG_BUTTON_ACTION_TYPE_BUTTON,
		RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
		RATBAG_BUTTON_ACTION_TYPE_KEY,
		RATBAG_BUTTON_ACTION_TYPE_MACRO
	};
	enum ratbag_button_action_type *t;


	r = sd_bus_message_open_container(reply, 'a', "u");
	if (r < 0)
		return r;

	ARRAY_FOR_EACH(types, t) {
		if (!ratbag_button_has_action_type(button->lib_button, *t))
			continue;

		r = sd_bus_message_append(reply, "u", *t);
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbag_button_macro *, ratbag_button_macro_unref);

static int ratbagd_button_get_macro(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    const char *property,
				    sd_bus_message *reply,
				    void *userdata,
				    sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	_cleanup_(ratbag_button_macro_unrefp) struct ratbag_button_macro *macro = NULL;
	int r;
	unsigned int idx;

	r = sd_bus_message_open_container(reply, 'a', "(uu)");
	if (r < 0)
		return r;

	macro = ratbag_button_get_macro(button->lib_button);
	if (!macro)
		goto out;

	for (idx = 0; idx < ratbag_button_macro_get_num_events(macro); idx++) {
		enum ratbag_macro_event_type type;
		int value;

		type = ratbag_button_macro_get_event_type(macro, idx);
		switch (type) {
		case RATBAG_MACRO_EVENT_INVALID:
			abort();
			break;
		case RATBAG_MACRO_EVENT_NONE:
			goto out;
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			value = ratbag_button_macro_get_event_key(macro, idx);
			break;
		case RATBAG_MACRO_EVENT_WAIT:
			value = ratbag_button_macro_get_event_timeout(macro, idx);
			break;
		}

		r = sd_bus_message_append(reply, "(uu)", type, value);
		if (r < 0)
			return r;
	}

out:
	return sd_bus_message_close_container(reply);
}

static int ratbagd_button_set_macro(sd_bus_message *m,
				    void *userdata,
				    sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int type, value;
	int r, idx = 0;
	_cleanup_(ratbag_button_macro_unrefp) struct ratbag_button_macro *macro = NULL;

	r = sd_bus_message_enter_container(m, 'a', "(uu)");
	if (r < 0)
		return r;

	macro = ratbag_button_macro_new("macro");
	while ((r = sd_bus_message_read(m, "(uu)", &type, &value)) > 0) {
		r = ratbag_button_macro_set_event(macro, idx++, type, value);
		if (r < 0)
			return r;
	}
	if (r < 0)
		return r;

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return r;

	r = ratbag_button_set_macro(button->lib_button, macro);
	if (r < 0)
		return r;

	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "Macro",
					       NULL);

		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "ActionType",
					       NULL);
	}

	return sd_bus_reply_method_return(m, "u", r);
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
	SD_BUS_PROPERTY("Type", "u", ratbagd_button_get_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ButtonMapping", "u", ratbagd_button_get_button, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("SpecialMapping", "u", ratbagd_button_get_special, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("KeyMapping", "au", ratbagd_button_get_key, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ActionType", "u", ratbagd_button_get_action_type, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ActionTypes", "au", ratbagd_button_get_action_types, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Macro", "a(uu)", ratbagd_button_get_macro, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_METHOD("SetButtonMapping", "u", "u", ratbagd_button_set_button, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetSpecialMapping", "u", "u", ratbagd_button_set_special, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetKeyMapping", "au", "u", ratbagd_button_set_key, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetMacro", "a(uu)", "u", ratbagd_button_set_macro, SD_BUS_VTABLE_UNPRIVILEGED),
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

	button = zalloc(sizeof(*button));
	button->lib_button = lib_button;
	button->index = index;

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(button_buffer, "b%u", index);
	r = sd_bus_path_encode_many(&button->path,
				    RATBAGD_OBJ_ROOT "/button/%/%/%",
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

