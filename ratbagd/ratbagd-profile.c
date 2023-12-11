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
#include <unistr.h>
#include <uniconv.h>
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

	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "o"));

	for (i = 0; i < profile->n_resolutions; ++i) {
		resolution = profile->resolutions[i];
		if (!resolution)
			continue;

		CHECK_CALL(sd_bus_message_append(reply,
						 "o",
						 ratbagd_resolution_get_path(resolution)));
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
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

	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "o"));

	for (i = 0; i < profile->n_buttons; ++i) {
		button = profile->buttons[i];
		if (!button)
			continue;

		CHECK_CALL(sd_bus_message_append(reply,
					  "o",
					  ratbagd_button_get_path(button)));
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
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

	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "o"));

	for (i = 0; i < profile->n_leds; ++i) {
		led = profile->leds[i];
		if (!led)
			continue;

		CHECK_CALL(sd_bus_message_append(reply,
						 "o",
						 ratbagd_led_get_path(led)));
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
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
	int is_active;

	is_active = ratbag_profile_is_active(profile->lib_profile);

	CHECK_CALL(sd_bus_message_append(reply, "b", is_active));

	return 0;
}

static int ratbagd_profile_is_dirty(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    const char *property,
				    sd_bus_message *reply,
				    void *userdata,
				    sd_bus_error *error)
{
	const struct ratbagd_profile *profile = userdata;

	const int is_dirty = ratbag_profile_is_dirty(profile->lib_profile);

	CHECK_CALL(sd_bus_message_append(reply, "b", is_dirty));

	return 0;
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
	/* FIXME: we should cache is active and only send the signal for
	 * those profiles where it changed */
	(void) sd_bus_emit_properties_changed(bus,
					      profile->path,
					      RATBAGD_NAME_ROOT ".Profile",
					      "IsActive",
					      NULL);

	return 0;
}

static int ratbagd_profile_set_active(sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	sd_bus *bus = sd_bus_message_get_bus(m);
	struct ratbagd_profile *profile = userdata;
	int r;

	CHECK_CALL(sd_bus_message_read(m, ""));

	r = ratbag_profile_set_active(profile->lib_profile);
	if (r < 0) {
		r = ratbagd_device_resync(profile->device, bus);
		if (r < 0)
			return r;
	}

	ratbagd_for_each_profile_signal(bus,
					profile->device,
					ratbagd_profile_active_signal_cb);

	ratbagd_profile_notify_dirty(bus, profile);

	CHECK_CALL(sd_bus_reply_method_return(m, "u", 0));

	return 0;
}

static int
ratbagd_profile_set_disabled(sd_bus *bus,
			     const char *path,
			     const char *interface,
			     const char *property,
			     sd_bus_message *m,
			     void *userdata,
			     sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	int r;
	int disabled;

	CHECK_CALL(sd_bus_message_read(m, "b", &disabled));

	r = ratbag_profile_set_enabled(profile->lib_profile, !disabled);
	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       profile->path,
					       RATBAGD_NAME_ROOT ".Profile",
					       "Disabled",
					       NULL);

		ratbagd_profile_notify_dirty(bus, profile);
	}

	return 0;
}

static int
ratbagd_profile_is_disabled(sd_bus *bus,
			    const char *path,
			    const char *interface,
			    const char *property,
			    sd_bus_message *reply,
			    void *userdata,
			    sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	int disabled = !ratbag_profile_is_enabled(profile->lib_profile);

	CHECK_CALL(sd_bus_message_append(reply, "b", disabled));

	return 0;
}

static int
ratbagd_profile_set_name(sd_bus *bus,
			 const char *path,
			 const char *interface,
			 const char *property,
			 sd_bus_message *m,
			 void *userdata,
			 sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	char *name;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "s", &name));

	r = ratbag_profile_set_name(profile->lib_profile, name);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       profile->path,
					       RATBAGD_NAME_ROOT ".Profile",
					       "Name",
					       NULL);

		ratbagd_profile_notify_dirty(bus, profile);
	}

	return 0;
}

static int
ratbagd_profile_get_name(sd_bus *bus,
			 const char *path,
			 const char *interface,
			 const char *property,
			 sd_bus_message *reply,
			 void *userdata,
			 sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	const char *name = ratbag_profile_get_name(profile->lib_profile);
	_cleanup_free_ char *utf8 = NULL;

	if (name) {
		if (u8_check((const uint8_t*)name, strlen(name)) == NULL)
			utf8 = strdup(name);
		else
			utf8 = (char*)u8_strconv_from_encoding(name,
							       "ISO-8859-1",
							       iconveh_question_mark);
		if (!utf8)
			utf8 = (char*)strdup_ascii_only(name);
	} else {
		utf8 = strdup("");
	}


	CHECK_CALL(sd_bus_message_append(reply, "s", utf8));

	return 0;
}

static int
ratbagd_profile_get_capabilities(sd_bus *bus,
				 const char *path,
				 const char *interface,
				 const char *property,
				 sd_bus_message *reply,
				 void *userdata,
				 sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbag_profile *lib_profile = profile->lib_profile;
	enum ratbag_profile_capability cap;
	enum ratbag_profile_capability caps[] = {
		RATBAG_PROFILE_CAP_SET_DEFAULT,
		RATBAG_PROFILE_CAP_DISABLE,
	};
	size_t i;

	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "u"));

	for (i = 0; i < ELEMENTSOF(caps); i++) {
		cap = caps[i];
		if (ratbag_profile_has_capability(lib_profile, cap)) {
			CHECK_CALL(sd_bus_message_append(reply, "u", cap));
		}
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
}

static int
ratbagd_profile_get_report_rate(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbag_profile *lib_profile = profile->lib_profile;
	int rate;

	rate = ratbag_profile_get_report_rate(lib_profile);
	verify_unsigned_int(rate);
	return sd_bus_message_append(reply, "u", rate);
}

static int
ratbagd_profile_get_angle_snapping(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *reply,
				   void *userdata,
				   sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbag_profile *lib_profile = profile->lib_profile;
	int value;

	value = ratbag_profile_get_angle_snapping(lib_profile);
	return sd_bus_message_append(reply, "i", value);
}

static int
ratbagd_profile_get_debounce(sd_bus *bus,
			     const char *path,
			     const char *interface,
			     const char *property,
			     sd_bus_message *reply,
			     void *userdata,
			     sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbag_profile *lib_profile = profile->lib_profile;
	int value;

	value = ratbag_profile_get_debounce(lib_profile);
	return sd_bus_message_append(reply, "i", value);
}

static int
ratbagd_profile_get_report_rates(sd_bus *bus,
				 const char *path,
				 const char *interface,
				 const char *property,
				 sd_bus_message *reply,
				 void *userdata,
				 sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbag_profile *lib_profile = profile->lib_profile;
	unsigned int rates[8];
	unsigned int nrates = ARRAY_LENGTH(rates);
	int r;

	r = sd_bus_message_open_container(reply, 'a', "u");
	if (r < 0)
		return r;

	nrates = ratbag_profile_get_report_rate_list(lib_profile,
						     rates, nrates);
	assert(nrates <= ARRAY_LENGTH(rates));

	for (unsigned int i = 0; i < nrates; i++) {
		verify_unsigned_int(rates[i]);
		r = sd_bus_message_append(reply, "u", rates[i]);
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int
ratbagd_profile_get_debounces(sd_bus *bus,
			      const char *path,
			      const char *interface,
			      const char *property,
			      sd_bus_message *reply,
			      void *userdata,
			      sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbag_profile *lib_profile = profile->lib_profile;
	unsigned int debounces[8];
	unsigned int ndebounces = ARRAY_LENGTH(debounces);
	int r;

	r = sd_bus_message_open_container(reply, 'a', "u");
	if (r < 0)
		return r;

	ndebounces = ratbag_profile_get_debounce_list(lib_profile, debounces, ndebounces);
	assert(ndebounces <= ARRAY_LENGTH(debounces));

	for (unsigned int i = 0; i < ndebounces; i++) {
		verify_unsigned_int(debounces[i]);
		r = sd_bus_message_append(reply, "u", debounces[i]);
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int
ratbagd_profile_set_report_rate(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *m,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	unsigned int rate;
	int r;

	r = sd_bus_message_read(m, "u", &rate);
	if (r < 0)
		return r;

	/* basic sanity check */
	if (rate < 125) {
		rate = 125;
	} else if (rate > 8000) {
		rate = 8000;
	}

	r = ratbag_profile_set_report_rate(profile->lib_profile, rate);
	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       profile->path,
					       RATBAGD_NAME_ROOT ".Profile",
					       "ReportRate",
					       NULL);

		ratbagd_profile_notify_dirty(bus, profile);
	}

	return 0;
}

static int
ratbagd_profile_set_angle_snapping(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *m,
				   void *userdata,
				   sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	int value;
	int r;

	r = sd_bus_message_read(m, "i", &value);
	if (r < 0)
		return r;

	r = ratbag_profile_set_angle_snapping(profile->lib_profile, value);
	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       profile->path,
					       RATBAGD_NAME_ROOT ".Profile",
					       "AngleSnapping",
					       NULL);

		ratbagd_profile_notify_dirty(bus, profile);
	}

	return 0;
}

static int
ratbagd_profile_set_debounce(sd_bus *bus,
			     const char *path,
			     const char *interface,
			     const char *property,
			     sd_bus_message *m,
			     void *userdata,
			     sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	int value;
	int r;

	r = sd_bus_message_read(m, "i", &value);
	if (r < 0)
		return r;

	r = ratbag_profile_set_debounce(profile->lib_profile, value);
	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       profile->path,
					       RATBAGD_NAME_ROOT ".Profile",
					       "Debounce",
					       NULL);

		ratbagd_profile_notify_dirty(bus, profile);
	}

	return 0;
}

const sd_bus_vtable ratbagd_profile_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_WRITABLE_PROPERTY("Name", "s",
				 ratbagd_profile_get_name,
				 ratbagd_profile_set_name, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Disabled", "b",
				 ratbagd_profile_is_disabled,
				 ratbagd_profile_set_disabled, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_profile, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Capabilities", "au", ratbagd_profile_get_capabilities, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Resolutions", "ao", ratbagd_profile_get_resolutions, 0, 0),
	SD_BUS_PROPERTY("Buttons", "ao", ratbagd_profile_get_buttons, 0, 0),
	SD_BUS_PROPERTY("Leds", "ao", ratbagd_profile_get_leds, 0, 0),
	SD_BUS_PROPERTY("IsActive", "b", ratbagd_profile_is_active, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("IsDirty", "b", ratbagd_profile_is_dirty, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("ReportRate", "u",
				 ratbagd_profile_get_report_rate,
				 ratbagd_profile_set_report_rate, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("AngleSnapping", "i",
				 ratbagd_profile_get_angle_snapping,
				 ratbagd_profile_set_angle_snapping, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Debounce", "i",
				 ratbagd_profile_get_debounce,
				 ratbagd_profile_set_debounce, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ReportRates", "au", ratbagd_profile_get_report_rates, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Debounces", "au", ratbagd_profile_get_debounces, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_METHOD("SetActive", "", "u", ratbagd_profile_set_active, SD_BUS_VTABLE_UNPRIVILEGED),
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
				    ratbagd_device_get_sysname(device),
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
			log_error("%s: failed to allocate resolution: %m\n",
				  ratbagd_device_get_sysname(device));
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
			log_error("%s: failed to allocate button: %m\n",
				  ratbagd_device_get_sysname(device));
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
			log_error("%s: failed to allocate led: %m\n",
				  ratbagd_device_get_sysname(device));
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
				    ratbagd_device_get_sysname(device),
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
		log_error("%s: failed to register resolutions: %m\n",
			  ratbagd_device_get_sysname(device));
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
				    ratbagd_device_get_sysname(device),
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
		log_error("%s: failed to register buttons: %m\n",
			  ratbagd_device_get_sysname(device));
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
				    ratbagd_device_get_sysname(device),
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
		log_error("%s: failed to register leds: %m\n",
			  ratbagd_device_get_sysname(device));
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

int ratbagd_for_each_button_signal(sd_bus *bus,
				       struct ratbagd_profile *profile,
				       int (*func)(sd_bus *bus,
						   struct ratbagd_button *button))
{
	int rc = 0;

	for (size_t i = 0; rc == 0 && i < profile->n_buttons; i++)
		rc = func(bus, profile->buttons[i]);

	return rc;
}

int ratbagd_for_each_led_signal(sd_bus *bus,
				       struct ratbagd_profile *profile,
				       int (*func)(sd_bus *bus,
						   struct ratbagd_led *button))
{
	int rc = 0;

	for (size_t i = 0; rc == 0 && i < profile->n_leds; i++)
		rc = func(bus, profile->leds[i]);

	return rc;
}


int ratbagd_profile_resync(sd_bus *bus,
			    struct ratbagd_profile *profile)
{
	ratbagd_for_each_resolution_signal(bus, profile, ratbagd_resolution_resync);
	ratbagd_for_each_button_signal(bus, profile, ratbagd_button_resync);
	ratbagd_for_each_led_signal(bus, profile, ratbagd_led_resync);

	return sd_bus_emit_properties_changed(bus,
					      profile->path,
					      RATBAGD_NAME_ROOT ".Profile",
					      "Resolutions",
					      "Buttons",
					      "Leds",
					      "IsActive",
					      NULL);
}

int ratbagd_profile_notify_dirty(sd_bus *bus,
				 struct ratbagd_profile *profile)
{
	(void)sd_bus_emit_properties_changed(bus,
					     profile->path,
					     RATBAGD_NAME_ROOT ".Profile",
					     "IsDirty",
					     NULL);

	return 0;
}
