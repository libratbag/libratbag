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

struct ratbagd_resolution {
	struct ratbag_resolution *lib_resolution;
	unsigned int index;
	char *path;
	unsigned int xres, yres;
	unsigned int rate;
};

static int ratbagd_resolution_set_report_rate(sd_bus_message *m,
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
		resolution->rate = rate;
	}
	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_resolution_set_resolution(sd_bus_message *m,
					     void *userdata,
					     sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	unsigned int xres, yres;
	int r;

	r = sd_bus_message_read(m, "uu", &xres, &yres);
	if (r < 0)
		return r;

	r = ratbag_resolution_set_dpi_xy(resolution->lib_resolution,
					 xres, yres);
	if (r == 0) {
		resolution->xres = xres;
		resolution->yres = yres;
	}

	(void) sd_bus_emit_signal(sd_bus_message_get_bus(m),
				  "/org/freedesktop/ratbag1",
				  "/org.freedesktop.ratbag1.Resolution",
				  "ActiveResolutionChanged",
				  "u",
				  resolution->index);

	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_resolution_set_default(sd_bus_message *m,
					  void *userdata,
					  sd_bus_error *error)
{
	struct ratbagd_resolution *resolution = userdata;
	int r;

	r = ratbag_resolution_set_default(resolution->lib_resolution);

	(void) sd_bus_emit_signal(sd_bus_message_get_bus(m),
				  "/org/freedesktop/ratbag1",
				  "/org.freedesktop.ratbag1.Resolution",
				  "DefaultResolutionChanged",
				  "u",
				  resolution->index);

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

const sd_bus_vtable ratbagd_resolution_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_resolution, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Capabilities", "au", ratbagd_resolution_get_capabilities, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("XResolution", "u", NULL, offsetof(struct ratbagd_resolution, xres), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("YResolution", "u", NULL, offsetof(struct ratbagd_resolution, yres), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ReportRate", "u", NULL, offsetof(struct ratbagd_resolution, rate), SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_METHOD("SetReportRate", "u", "u", ratbagd_resolution_set_report_rate, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetResolution", "uu", "u", ratbagd_resolution_set_resolution, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetDefault", "", "u", ratbagd_resolution_set_default, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("ActiveResolutionChanged", "u", 0),
	SD_BUS_SIGNAL("DefaultResolutionChanged", "u", 0),
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

	resolution->lib_resolution = lib_resolution;
	resolution->index = index;
	resolution->xres = ratbag_resolution_get_dpi_x(lib_resolution);
	resolution->yres = ratbag_resolution_get_dpi_y(lib_resolution);
	resolution->rate = ratbag_resolution_get_report_rate(lib_resolution);

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(resolution_buffer, "r%u", index);
	r = sd_bus_path_encode_many(&resolution->path,
				    "/org/freedesktop/ratbag1/resolution/%/%/%",
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

bool ratbagd_resolution_is_active(struct ratbagd_resolution *resolution)
{
	assert(resolution);
	return ratbag_resolution_is_active(resolution->lib_resolution) != 0;
}

bool ratbagd_resolution_is_default(struct ratbagd_resolution *resolution)
{
	assert(resolution);
	return ratbag_resolution_is_default(resolution->lib_resolution) != 0;
}
