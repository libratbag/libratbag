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

	sd_bus_slot *profile_vtable_slot;
	sd_bus_slot *profile_enum_slot;
};

const sd_bus_vtable ratbagd_resolution_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_resolution, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("XResolution", "u", NULL, offsetof(struct ratbagd_resolution, xres), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("YResolution", "u", NULL, offsetof(struct ratbagd_resolution, yres), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ReportRate", "u", NULL, offsetof(struct ratbagd_resolution, rate), SD_BUS_VTABLE_PROPERTY_CONST),
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

	/* we cannot assume the resolution is gone, so set NULL explicitly */
	ratbag_resolution_unref(resolution->lib_resolution);
	resolution->lib_resolution = NULL;

	return mfree(resolution);
}
