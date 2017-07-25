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
#include "ratbagd.h"
#include "shared-macro.h"
#include "libratbag-util.h"

struct ratbagd_resolution {
	struct ratbagd_profile *profile;
	struct ratbag_resolution *lib_resolution;
	unsigned int index;
	char *path;
};

static int
ratbagd_resolution_set_report_rate(sd_bus *bus,
				   const char *path,
				   const char *interface,
				   const char *property,
				   sd_bus_message *m,
				   void *userdata,
				   sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	unsigned int rate;
	int r;

	r = sd_bus_message_read(m, "u", &rate);
	if (r < 0)
		return r;

	/* basic sanity check */
	if (rate > 5000 || rate % 100)
		return 0;

	r = ratbag_resolution_set_report_rate(resolution->lib_resolution,
					      rate);
	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       resolution->path,
					       RATBAGD_NAME_ROOT ".Resolution",
					       "ReportRate",
					       NULL);
	}

	return r;
}

static int ratbagd_resolution_default_signal_cb(sd_bus *bus,
						struct ratbagd_resolution *resolution)
{
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;

	/* FIXME: we should cache is default and only send the signal for
	 * those resolutions where it changed */

	(void) sd_bus_emit_signal(bus,
				  resolution->path,
				  RATBAGD_NAME_ROOT ".Resolution",
				  "IsDefault",
				  "b",
				  ratbag_resolution_is_default(lib_resolution));

	return 0;
}

static int ratbagd_resolution_set_default(sd_bus_message *m,
					  void *userdata,
					  sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	int r;

	r = ratbag_resolution_set_default(resolution->lib_resolution);
	if (r < 0)
		return r;

	ratbagd_for_each_resolution_signal(sd_bus_message_get_bus(m),
					   resolution->profile,
					   ratbagd_resolution_default_signal_cb);

	return sd_bus_reply_method_return(m, "u", r);
}

static int
ratbagd_resolution_get_capabilities(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    const char *property,
				    sd_bus_message *reply,
				    void *userdata,
				    sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	enum ratbag_device_capability cap;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "u");
	if (r < 0)
		return r;

	cap = RATBAG_RESOLUTION_CAP_INDIVIDUAL_REPORT_RATE;
	if (ratbag_resolution_has_capability(lib_resolution, cap)) {
		r = sd_bus_message_append(reply, "u", cap);
		if (r < 0)
			return r;
	}

	cap = RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION;
	if (ratbag_resolution_has_capability(lib_resolution, cap)) {
		r = sd_bus_message_append(reply, "u", cap);
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int
ratbagd_resolution_is_active(sd_bus *bus,
			     const char *path,
			     const char *interface,
			     const char *property,
			     sd_bus_message *reply,
			     void *userdata,
			     sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	bool is_active;

	is_active = !!ratbag_resolution_is_active(lib_resolution);

	return sd_bus_message_append(reply, "b", is_active);
}

static int
ratbagd_resolution_is_default(sd_bus *bus,
			     const char *path,
			     const char *interface,
			     const char *property,
			     sd_bus_message *reply,
			     void *userdata,
			     sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	bool is_default;

	is_default = !!ratbag_resolution_is_default(lib_resolution);

	return sd_bus_message_append(reply, "b", is_default);
}

static int
ratbagd_resolution_get_resolution(sd_bus *bus,
				  const char *path,
				  const char *interface,
				  const char *property,
				  sd_bus_message *reply,
				  void *userdata,
				  sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	int xres, yres;

	xres = ratbag_resolution_get_dpi_x(lib_resolution);
	yres = ratbag_resolution_get_dpi_y(lib_resolution);

	return sd_bus_message_append(reply, "(uu)", xres, yres);
}

static int
ratbagd_resolution_set_resolution(sd_bus *bus,
				  const char *path,
				  const char *interface,
				  const char *property,
				  sd_bus_message *m,
				  void *userdata,
				  sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	const enum ratbag_device_capability cap = RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION;
	int xres, yres;
	int r;

	r = sd_bus_message_read(m, "(uu)", &xres, &yres);
	if (r < 0)
		return r;

	if (!ratbag_resolution_has_capability(lib_resolution, cap)) {
		r = ratbag_resolution_set_dpi(resolution->lib_resolution, xres);
	} else {
		r = ratbag_resolution_set_dpi_xy(resolution->lib_resolution,
						 xres, yres);
	}

	if (r == 0) {
		sd_bus *bus = sd_bus_message_get_bus(m);
		sd_bus_emit_properties_changed(bus,
					       resolution->path,
					       RATBAGD_NAME_ROOT ".Resolution",
					       "Resolution",
					       NULL);
	}

	return r;
}

static int
ratbagd_resolution_get_rate(sd_bus *bus,
			    const char *path,
			    const char *interface,
			    const char *property,
			    sd_bus_message *reply,
			    void *userdata,
			    sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	int rate;

	rate = ratbag_resolution_get_report_rate(lib_resolution);
	return sd_bus_message_append(reply, "u", rate);
}

static int
ratbagd_resolution_get_minimum(sd_bus *bus,
			       const char *path,
			       const char *interface,
			       const char *property,
			       sd_bus_message *reply,
			       void *userdata,
			       sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	int min;

	min = ratbag_resolution_get_dpi_minimum(lib_resolution);
	return sd_bus_message_append(reply, "u", min);
}

static int
ratbagd_resolution_get_maximum(sd_bus *bus,
			       const char *path,
			       const char *interface,
			       const char *property,
			       sd_bus_message *reply,
			       void *userdata,
			       sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	struct ratbag_resolution *lib_resolution = resolution->lib_resolution;
	int max;

	max = ratbag_resolution_get_dpi_maximum(lib_resolution);
	return sd_bus_message_append(reply, "u", max);
}

const sd_bus_vtable ratbagd_resolution_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_resolution, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Capabilities", "au", ratbagd_resolution_get_capabilities, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("IsActive", "b", ratbagd_resolution_is_active, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("IsDefault", "b", ratbagd_resolution_is_default, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Resolution", "(uu)",
				 ratbagd_resolution_get_resolution,
				 ratbagd_resolution_set_resolution, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("ReportRate", "u",
				 ratbagd_resolution_get_rate,
				 ratbagd_resolution_set_report_rate, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("Maximum", "u", ratbagd_resolution_get_maximum, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Minimum", "u", ratbagd_resolution_get_minimum, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_METHOD("SetDefault", "", "u", ratbagd_resolution_set_default, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int ratbagd_resolution_new(struct ratbagd_resolution **out,
			   struct ratbagd_device *device,
			   struct ratbagd_profile *profile,
			   struct ratbag_resolution *lib_resolution,
			   unsigned int index)
{
	_cleanup_(ratbagd_resolution_freep) struct ratbagd_resolution *resolution = NULL;
	char profile_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1],
	     resolution_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	assert(out);
	assert(lib_resolution);

	resolution = calloc(1, sizeof(*resolution));
	if (!resolution)
		return -ENOMEM;

	resolution->profile = profile;
	resolution->lib_resolution = lib_resolution;
	resolution->index = index;

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(resolution_buffer, "r%u", index);
	r = sd_bus_path_encode_many(&resolution->path,
				    RATBAGD_OBJ_ROOT "/resolution/%/%/%",
				    ratbagd_device_get_name(device),
				    profile_buffer,
				    resolution_buffer);
	if (r < 0)
		return r;

	*out = resolution;
	resolution = NULL;
	return 0;
}

const char *ratbagd_resolution_get_path(struct ratbagd_resolution *resolution)
{
	assert(resolution);
	return resolution->path;
}

struct ratbagd_resolution *ratbagd_resolution_free(struct ratbagd_resolution *resolution)
{
	if (!resolution)
		return NULL;

	resolution->path = mfree(resolution->path);
	resolution->lib_resolution = ratbag_resolution_unref(resolution->lib_resolution);

	return mfree(resolution);
}
