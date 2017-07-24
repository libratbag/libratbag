/***
  This file is part of ratbagd.

  Copyright 2015 David Herrmann <dh.herrmann@gmail.com>

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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "ratbagd.h"
#include "libratbag-util.h"
#include "shared-macro.h"
#include "libratbag-util.h"

struct ratbagd_profile {
	struct ratbagd_device *device;
	struct ratbag_profile *lib_profile;
	unsigned int index;
	char *path;

	sd_bus_slot *resolution_vtable_slot;
	sd_bus_slot *resolution_enum_slot;
	unsigned int n_resolutions;
	struct ratbagd_resolution **resolutions;

	sd_bus_slot *button_vtable_slot;
	sd_bus_slot *button_enum_slot;
	unsigned int n_buttons;
	struct ratbagd_button **buttons;

	sd_bus_slot *led_vtable_slot;
	sd_bus_slot *led_enum_slot;
	unsigned int n_leds;
	struct ratbagd_led **leds;
};

static int ratbagd_profile_find_resolution(sd_bus *bus,
					   const char *path,
					   const char *interface,
					   void *userdata,
					   void **found,
					   sd_bus_error *error)
{
	_cleanup_(freep) char *name = NULL;
	struct ratbagd_profile *profile = userdata;
	unsigned int index = 0;
	int r;

	r = sd_bus_path_decode_many(path,
				    RATBAGD_OBJ_ROOT "/resolution/%/p%/r%",
				    NULL,
				    NULL,
				    &name);
	if (r <= 0)
		return r;

	r = safe_atou(name, &index);
	if (r < 0)
		return 0;

	if (index >= profile->n_resolutions || !profile->resolutions[index])
		return 0;

	*found = profile->resolutions[index];
	return 1;
}

static int ratbagd_profile_get_resolutions(sd_bus *bus,
					   const char *path,
					   const char *interface,
					   const char *property,
					   sd_bus_message *reply,
					   void *userdata,
					   sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_resolution *resolution;
	unsigned int i;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "o");
	if (r < 0)
		return r;

	for (i = 0; i < profile->n_resolutions; ++i) {
		resolution = profile->resolutions[i];
		if (!resolution)
			continue;

		r = sd_bus_message_append(reply,
					  "o",
					  ratbagd_resolution_get_path(resolution));
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_profile_get_buttons(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *reply,
				       void *userdata,
				       sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_button *button;
	unsigned int i;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "o");
	if (r < 0)
		return r;

	for (i = 0; i < profile->n_buttons; ++i) {
		button = profile->buttons[i];
		if (!button)
			continue;

		r = sd_bus_message_append(reply,
					  "o",
					  ratbagd_button_get_path(button));
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_profile_get_leds(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    const char *property,
				    sd_bus_message *reply,
				    void *userdata,
				    sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_led *led;
	unsigned int i;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "o");
	if (r < 0)
		return r;

	for (i = 0; i < profile->n_leds; ++i) {
		led = profile->leds[i];
		if (!led)
			continue;

		r = sd_bus_message_append(reply,
					  "o",
					  ratbagd_led_get_path(led));
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_profile_is_active(sd_bus *bus,
				     const char *path,
				     const char *interface,
				     const char *property,
				     sd_bus_message *reply,
				     void *userdata,
				     sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	bool is_active;

	is_active = !!ratbag_profile_is_active(profile->lib_profile);

	return sd_bus_message_append(reply, "b", is_active);
}

static int ratbagd_profile_find_button(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       void *userdata,
				       void **found,
				       sd_bus_error *error)
{
	_cleanup_(freep) char *name = NULL;
	struct ratbagd_profile *profile = userdata;
	unsigned int index = 0;
	int r;

	r = sd_bus_path_decode_many(path,
				    RATBAGD_OBJ_ROOT "/button/%/p%/b%",
				    NULL,
				    NULL,
				    &name);
	if (r <= 0)
		return r;

	r = safe_atou(name, &index);
	if (r < 0)
		return 0;

	if (index >= profile->n_buttons || !profile->buttons[index])
		return 0;

	*found = profile->buttons[index];
	return 1;
}

static int ratbagd_profile_find_led(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    void *userdata,
				    void **found,
				    sd_bus_error *error)
{
	_cleanup_(freep) char *name = NULL;
	struct ratbagd_profile *profile = userdata;
	unsigned int index = 0;
	int r;

	r = sd_bus_path_decode_many(path,
				    RATBAGD_OBJ_ROOT "/led/%/p%/l%",
				    NULL,
				    NULL,
				    &name);
	if (r <= 0)
		return r;

	r = safe_atou(name, &index);
	if (r < 0)
		return 0;

	if (index >= profile->n_leds || !profile->leds[index])
		return 0;

	*found = profile->leds[index];
	return 1;
}

static int ratbagd_profile_active_signal_cb(sd_bus *bus,
					    struct ratbagd_profile *profile)
{
	struct ratbag_profile *lib_profile = profile->lib_profile;

	/* FIXME: we should cache is active and only send the signal for
	 * those profiles where it changed */

	(void) sd_bus_emit_signal(bus,
				  profile->path,
				  RATBAGD_NAME_ROOT ".Profile",
				  "IsActive",
				  "b",
				  ratbag_profile_is_active(lib_profile));

	return 0;
}

static int ratbagd_profile_set_active(sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	int r;

	r = sd_bus_message_read(m, "");
	if (r < 0)
		return r;

	ratbagd_for_each_profile_signal(sd_bus_message_get_bus(m),
					profile->device,
					ratbagd_profile_active_signal_cb);

        return sd_bus_reply_method_return(m, "u",
                                          ratbag_profile_set_active(profile->lib_profile));
}

static int ratbagd_profile_get_resolution_by_index(sd_bus_message *m,
						   void *userdata,
						   sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_resolution *resolution;
	unsigned int index = 0;
	int r;

	r = sd_bus_message_read(m, "u", &index);
	if (r < 0)
		return r;

	if (index >= profile->n_resolutions || !profile->resolutions[index])
		return 0;

	resolution = profile->resolutions[index];
	return sd_bus_reply_method_return(m, "o",
					  ratbagd_resolution_get_path(resolution));
}

const sd_bus_vtable ratbagd_profile_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_profile, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Resolutions", "ao", ratbagd_profile_get_resolutions, 0, 0),
	SD_BUS_PROPERTY("Buttons", "ao", ratbagd_profile_get_buttons, 0, 0),
	SD_BUS_PROPERTY("Leds", "ao", ratbagd_profile_get_leds, 0, 0),
	SD_BUS_PROPERTY("IsActive", "b", ratbagd_profile_is_active, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_METHOD("SetActive", "", "u", ratbagd_profile_set_active, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("GetResolutionByIndex", "u", "o", ratbagd_profile_get_resolution_by_index, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int ratbagd_profile_new(struct ratbagd_profile **out,
			struct ratbagd_device *device,
			struct ratbag_profile *lib_profile,
			unsigned int index)
{
	_cleanup_(ratbagd_profile_freep) struct ratbagd_profile *profile = NULL;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	unsigned int i;
	int r;

	assert(out);
	assert(lib_profile);

	profile = zalloc(sizeof(*profile));

	profile->device = device;
	profile->lib_profile = lib_profile;
	profile->index = index;

	sprintf(index_buffer, "p%u", index);
	r = sd_bus_path_encode_many(&profile->path,
				    RATBAGD_OBJ_ROOT "/profile/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);
	if (r < 0)
		return r;

	profile->n_resolutions = ratbag_profile_get_num_resolutions(profile->lib_profile);
	profile->resolutions = zalloc(profile->n_resolutions * sizeof(*profile->resolutions));

	profile->n_buttons = ratbagd_device_get_num_buttons(device);
	profile->buttons = zalloc(profile->n_buttons * sizeof(*profile->buttons));

	profile->n_leds = ratbagd_device_get_num_leds(device);
	profile->leds = zalloc(profile->n_leds * sizeof(*profile->leds));

	for (i = 0; i < profile->n_resolutions; ++i) {
		resolution = ratbag_profile_get_resolution(profile->lib_profile, i);
		if (!resolution)
			continue;

		r = ratbagd_resolution_new(&profile->resolutions[i],
					   device,
					   profile,
					   resolution,
					   i);
		if (r < 0) {
			errno = -r;
			log_error("Cannot allocate resolution for '%s': %m\n",
				  ratbagd_device_get_name(device));
		}
	}

	for (i = 0; i < profile->n_buttons; ++i) {
		button = ratbag_profile_get_button(profile->lib_profile, i);
		if (!button)
			continue;

		r = ratbagd_button_new(&profile->buttons[i],
				       device,
				       profile,
				       button,
				       i);
		if (r < 0) {
			errno = -r;
			log_error("Cannot allocate button for '%s': %m\n",
				  ratbagd_device_get_name(device));
		}
	}

	for (i = 0; i < profile->n_leds; ++i) {
		led = ratbag_profile_get_led(profile->lib_profile, i);
		if (!led)
			continue;

		r = ratbagd_led_new(&profile->leds[i],
				    device,
				    profile,
				    led,
				    i);
		if (r < 0) {
			errno = -r;
			log_error("Cannot allocate led for '%s': %m\n",
				  ratbagd_device_get_name(device));
		}
	}

	*out = profile;
	profile = NULL;
	return 0;
}

struct ratbagd_profile *ratbagd_profile_free(struct ratbagd_profile *profile)
{
	unsigned int i;

	if (!profile)
		return NULL;

	profile->resolution_vtable_slot = sd_bus_slot_unref(profile->resolution_vtable_slot);
	profile->resolution_enum_slot = sd_bus_slot_unref(profile->resolution_enum_slot);
	profile->button_vtable_slot = sd_bus_slot_unref(profile->button_vtable_slot);
	profile->button_enum_slot = sd_bus_slot_unref(profile->button_enum_slot);
	profile->led_vtable_slot = sd_bus_slot_unref(profile->led_vtable_slot);
	profile->led_enum_slot = sd_bus_slot_unref(profile->led_enum_slot);

	for (i = 0; i< profile->n_leds; ++i)
		ratbagd_led_free(profile->leds[i]);
	for (i = 0; i< profile->n_buttons; ++i)
		ratbagd_button_free(profile->buttons[i]);
	for (i = 0; i< profile->n_resolutions; ++i)
		ratbagd_resolution_free(profile->resolutions[i]);

	mfree(profile->leds);
	mfree(profile->buttons);
	mfree(profile->resolutions);

	profile->path = mfree(profile->path);
	profile->lib_profile = ratbag_profile_unref(profile->lib_profile);

	return mfree(profile);
}

const char *ratbagd_profile_get_path(struct ratbagd_profile *profile)
{
	assert(profile);
	return profile->path;
}

unsigned int ratbagd_profile_get_index(struct ratbagd_profile *profile)
{
	assert(profile);
	return profile->index;
}

static int ratbagd_profile_list_resolutions(sd_bus *bus,
					    const char *path,
					    void *userdata,
					    char ***paths,
					    sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_resolution *resolution;
	char **resolutions;
	unsigned int i;

	resolutions = zalloc((profile->n_resolutions + 1) * sizeof(char *));

	for (i = 0; i < profile->n_resolutions; ++i) {
		resolution = profile->resolutions[i];
		if (!resolution)
			continue;

		resolutions[i] = strdup_safe(ratbagd_resolution_get_path(resolution));
	}

	resolutions[i] = NULL;
	*paths = resolutions;
	return 1;
}


int ratbagd_profile_register_resolutions(struct sd_bus *bus,
					 struct ratbagd_device *device,
					 struct ratbagd_profile *profile)
{
	_cleanup_(freep) char *prefix = NULL;
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	sprintf(index_buffer, "p%u", profile->index);

	/* register resolution interfaces */
	r = sd_bus_path_encode_many(&prefix,
				    RATBAGD_OBJ_ROOT "/resolution/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);

	if (r >= 0) {
		r = sd_bus_add_fallback_vtable(bus,
					       &profile->resolution_vtable_slot,
					       prefix,
					       RATBAGD_NAME_ROOT ".Resolution",
					       ratbagd_resolution_vtable,
					       ratbagd_profile_find_resolution,
					       profile);
		if (r >= 0)
			r = sd_bus_add_node_enumerator(bus,
						       &profile->resolution_enum_slot,
						       prefix,
						       ratbagd_profile_list_resolutions,
						       profile);
	}
	if (r < 0) {
		errno = -r;
		log_error("Cannot register resolutions for '%s': %m\n",
			  ratbagd_device_get_name(device));
	}

	return 0;
}

static int ratbagd_profile_list_buttons(sd_bus *bus,
					const char *path,
					void *userdata,
					char ***paths,
					sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_button *button;
	char **buttons;
	unsigned int i;

	buttons = zalloc((profile->n_buttons + 1) * sizeof(char *));

	for (i = 0; i < profile->n_buttons; ++i) {
		button = profile->buttons[i];
		if (!button)
			continue;

		buttons[i] = strdup_safe(ratbagd_button_get_path(button));
	}

	buttons[i] = NULL;
	*paths = buttons;
	return 1;
}

int ratbagd_profile_register_buttons(struct sd_bus *bus,
				     struct ratbagd_device *device,
				     struct ratbagd_profile *profile)
{
	_cleanup_(freep) char *prefix = NULL;
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	sprintf(index_buffer, "p%u", profile->index);

	/* register button interfaces */
	r = sd_bus_path_encode_many(&prefix,
				    RATBAGD_OBJ_ROOT "/button/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);

	if (r >= 0) {
		r = sd_bus_add_fallback_vtable(bus,
					       &profile->button_vtable_slot,
					       prefix,
					       RATBAGD_NAME_ROOT ".Button",
					       ratbagd_button_vtable,
					       ratbagd_profile_find_button,
					       profile);
		if (r >= 0)
			r = sd_bus_add_node_enumerator(bus,
						       &profile->button_enum_slot,
						       prefix,
						       ratbagd_profile_list_buttons,
						       profile);
	}
	if (r < 0) {
		errno = -r;
		log_error("Cannot register buttons for '%s': %m\n",
			  ratbagd_device_get_name(device));
	}

	return 0;
}

static int ratbagd_profile_list_leds(sd_bus *bus,
				     const char *path,
				     void *userdata,
				     char ***paths,
				     sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_led *led;
	char **leds;
	unsigned int i;

	leds = zalloc((profile->n_leds + 1) * sizeof(char *));

	for (i = 0; i < profile->n_leds; ++i) {
		led = profile->leds[i];
		if (!led)
			continue;

		leds[i] = strdup_safe(ratbagd_led_get_path(led));
	}

	leds[i] = NULL;
	*paths = leds;
	return 1;
}

int ratbagd_profile_register_leds(struct sd_bus *bus,
				  struct ratbagd_device *device,
				  struct ratbagd_profile *profile)
{
	_cleanup_(freep) char *prefix = NULL;
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	sprintf(index_buffer, "p%u", profile->index);

	/* register led interfaces */
	r = sd_bus_path_encode_many(&prefix,
				    RATBAGD_OBJ_ROOT "/led/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);

	if (r >= 0) {
		r = sd_bus_add_fallback_vtable(bus,
					       &profile->led_vtable_slot,
					       prefix,
					       RATBAGD_NAME_ROOT ".Led",
					       ratbagd_led_vtable,
					       ratbagd_profile_find_led,
					       profile);
		if (r >= 0)
			r = sd_bus_add_node_enumerator(bus,
						       &profile->led_enum_slot,
						       prefix,
						       ratbagd_profile_list_leds,
						       profile);
	}
	if (r < 0) {
		errno = -r;
		log_error("Cannot register leds for '%s': %m\n",
			  ratbagd_device_get_name(device));
	}

	return 0;
}

int ratbagd_for_each_resolution_signal(sd_bus *bus,
				       struct ratbagd_profile *profile,
				       int (*func)(sd_bus *bus,
						   struct ratbagd_resolution *resolution))
{
	int rc = 0;

	for (size_t i = 0; rc == 0 && i < profile->n_resolutions; i++)
		rc = func(bus, profile->resolutions[i]);

	return rc;
}
