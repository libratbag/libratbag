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
	struct ratbagd_device *device;
	struct ratbag_button *lib_button;
	unsigned int index;
	char *path;
};

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

	verify_unsigned_int(b);

	return sd_bus_message_append(reply, "(uv)",
				     RATBAG_BUTTON_ACTION_TYPE_BUTTON,
				     "u", b);
}

static int ratbagd_button_set_button(sd_bus *bus,
				     const char *path,
				     const char *interface,
				     const char *property,
				     sd_bus_message *m,
				     void *userdata,
				     sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int map;
	int r;

	r = sd_bus_message_read(m, "v", "u", &map);
	if (r < 0)
		return r;

	if (map == 0 || map > 30)
		return 0;

	r = ratbag_button_set_button(button->lib_button, map);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "Mapping",
					       NULL);
	}

	return 0;
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
	if (special == RATBAG_BUTTON_ACTION_SPECIAL_INVALID)
		special = RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN;

	verify_unsigned_int(special);

	CHECK_CALL(sd_bus_message_append(reply, "(uv)",
					 RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
					 "u",
					 special));

	return 0;
}

static int ratbagd_button_set_special(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	enum ratbag_button_action_special special;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "v", "u", &special));

	r = ratbag_button_set_special(button->lib_button, special);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "Mapping",
					       NULL);
	}

	return 0;
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

	const unsigned int key = ratbag_button_get_key(button->lib_button);

	verify_unsigned_int(key);

	CHECK_CALL(sd_bus_message_append(reply, "(uv)",
					 RATBAG_BUTTON_ACTION_TYPE_KEY,
					 "u",
					 key));

	return 0;
}

static int ratbagd_button_set_key(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int key;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "v", "u", &key));

	r = ratbag_button_set_key(button->lib_button, key);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "Mapping",
					       NULL);
	}

	return 0;
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
	unsigned int idx;

	CHECK_CALL(sd_bus_message_open_container(reply, SD_BUS_TYPE_STRUCT, "uv"));
	CHECK_CALL(sd_bus_message_append(reply, "u", RATBAG_BUTTON_ACTION_TYPE_MACRO));
	CHECK_CALL(sd_bus_message_open_container(reply, 'v', "a(uu)"));
	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "(uu)"));

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
		default:
			abort();
		}

		verify_unsigned_int(type);
		verify_unsigned_int(value);

		CHECK_CALL(sd_bus_message_append(reply, "(uu)", type, value));
	}

out:
	CHECK_CALL(sd_bus_message_close_container(reply)); /* a(uu) */
	CHECK_CALL(sd_bus_message_close_container(reply)); /* v */
	CHECK_CALL(sd_bus_message_close_container(reply)); /* ) */

	return 0;
}

static int ratbagd_button_set_macro(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    const char *property,
				    sd_bus_message *m,
				    void *userdata,
				    sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	unsigned int type, value;
	int r, idx = 0;
	_cleanup_(ratbag_button_macro_unrefp) struct ratbag_button_macro *macro = NULL;

	CHECK_CALL(sd_bus_message_enter_container(m, 'v', "a(uu)"));
	CHECK_CALL(sd_bus_message_enter_container(m, 'a', "(uu)"));

	macro = ratbag_button_macro_new("macro");
	while ((r = sd_bus_message_read(m, "(uu)", &type, &value)) > 0) {
		r = ratbag_button_macro_set_event(macro, idx++, type, value);
		if (r < 0) {
			r = ratbagd_device_resync(button->device, bus);
			if (r < 0)
				return r;
		}
	}
	if (r < 0)
		return r;

	CHECK_CALL(sd_bus_message_exit_container(m)); /* (uu) */
	CHECK_CALL(sd_bus_message_exit_container(m)); /* a(uu) */

	r = ratbag_button_set_macro(button->lib_button, macro);
	if (r < 0) {
		r = ratbagd_device_resync(button->device, bus);
		if (r < 0)
			return r;
	}

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "Mapping",
					       NULL);
	}

	return 0;
}

static int ratbagd_button_get_none(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *reply,
				   void *userdata,
				   sd_bus_error *error)
{
	CHECK_CALL(sd_bus_message_append(reply, "(uv)",
					 RATBAG_BUTTON_ACTION_TYPE_NONE,
					 "u",
					 0));

	return 0;
}

static int ratbagd_button_set_none(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *m,
				   void *userdata,
				   sd_bus_error *error)
{
	struct ratbagd_button *button = userdata;
	int r;
	_unused_ unsigned int zero;

	CHECK_CALL(sd_bus_message_read(m, "v", "u", &zero));

	r = ratbag_button_disable(button->lib_button);
	if (r < 0) {
		r = ratbagd_device_resync(button->device, bus);
		if (r < 0)
			return r;
	}
	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       button->path,
					       RATBAGD_NAME_ROOT ".Button",
					       "Mapping",
					       NULL);
	}

	return 0;
}

static int ratbagd_button_get_mapping(sd_bus *bus,
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
	verify_unsigned_int(type);

	switch (type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		return ratbagd_button_get_none(bus, path, interface, property,
					       reply, userdata, error);
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		return ratbagd_button_get_button(bus, path, interface, property,
						 reply, userdata, error);
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		return ratbagd_button_get_special(bus, path, interface, property,
						 reply, userdata, error);
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		return ratbagd_button_get_key(bus, path, interface, property,
						  reply, userdata, error);
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		return ratbagd_button_get_macro(bus, path, interface, property,
						reply, userdata, error);
	default:
		return sd_bus_message_append(reply, "(uv)",
					     RATBAG_BUTTON_ACTION_TYPE_UNKNOWN,
					     "u", 0);
	}

	return 0;
}

static int ratbagd_button_set_mapping(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	enum ratbag_button_action_type type;

	CHECK_CALL(sd_bus_message_enter_container(m, SD_BUS_TYPE_STRUCT, "uv"));
	CHECK_CALL(sd_bus_message_read(m, "u", &type));

	switch (type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		CHECK_CALL(ratbagd_button_set_none(bus, path, interface, property,
						   m, userdata, error));
		break;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		CHECK_CALL(ratbagd_button_set_button(bus, path, interface, property,
						     m, userdata, error));
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		CHECK_CALL(ratbagd_button_set_special(bus, path, interface, property,
						      m, userdata, error));
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		CHECK_CALL(ratbagd_button_set_key(bus, path, interface, property,
						      m, userdata, error));
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		CHECK_CALL(ratbagd_button_set_macro(bus, path, interface, property,
						    m, userdata, error));
		break;
	default:
		/* FIXME */
		return 1;
	}

	CHECK_CALL(sd_bus_message_exit_container(m));

	return 0;

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
	enum ratbag_button_action_type types[] = {
		RATBAG_BUTTON_ACTION_TYPE_NONE,
		RATBAG_BUTTON_ACTION_TYPE_BUTTON,
		RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
		RATBAG_BUTTON_ACTION_TYPE_KEY,
		RATBAG_BUTTON_ACTION_TYPE_MACRO
	};
	enum ratbag_button_action_type *t;


	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "u"));

	ARRAY_FOR_EACH(types, t) {
		if (!ratbag_button_has_action_type(button->lib_button, *t))
			continue;

		verify_unsigned_int(*t);
		CHECK_CALL(sd_bus_message_append(reply, "u", *t));
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
}

const sd_bus_vtable ratbagd_button_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_button, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_WRITABLE_PROPERTY("Mapping", "(uv)",
				 ratbagd_button_get_mapping,
				 ratbagd_button_set_mapping,
				 0, SD_BUS_VTABLE_UNPRIVILEGED | SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ActionTypes", "au", ratbagd_button_get_action_types, 0, SD_BUS_VTABLE_PROPERTY_CONST),
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
	button->device = device;
	button->lib_button = lib_button;
	button->index = index;

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(button_buffer, "b%u", index);
	r = sd_bus_path_encode_many(&button->path,
				    RATBAGD_OBJ_ROOT "/button/%/%/%",
				    ratbagd_device_get_sysname(device),
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

int ratbagd_button_resync(sd_bus *bus,
			      struct ratbagd_button *button)
{
	return sd_bus_emit_properties_changed(bus,
					      button->path,
					      RATBAGD_NAME_ROOT ".Button",
					      "ButtonMapping",
					      "SpecialMapping",
					      "Macro",
					      "ActionType",
					      NULL);
}
