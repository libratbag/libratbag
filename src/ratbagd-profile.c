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

#include <assert.h>
#include <errno.h>
#include <libratbag.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "ratbagd.h"
#include "shared-macro.h"

struct ratbagd_profile {
	struct ratbag_profile *lib_profile;
	unsigned int index;
	char *path;
};

static int ratbagd_profile_get_resolutions(sd_bus *bus,
					   const char *path,
					   const char *interface,
					   const char *property,
					   sd_bus_message *reply,
					   void *userdata,
					   sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	unsigned int i, n_resolutions;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "a{sv}");
	if (r < 0)
		return r;

	n_resolutions = ratbag_profile_get_num_resolutions(profile->lib_profile);
	for (i = 0; i < n_resolutions; ++i) {
		struct ratbag_resolution *resolution;
		bool cap_separate_xy_resolution;
		unsigned int dpi_x, dpi_y, report_rate;

		resolution = ratbag_profile_get_resolution(profile->lib_profile, i);
		if (!resolution)
			continue;

		cap_separate_xy_resolution = ratbag_resolution_has_capability(resolution,
							RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		report_rate = ratbag_resolution_get_report_rate(resolution);
		if (cap_separate_xy_resolution) {
			dpi_x = ratbag_resolution_get_dpi_x(resolution);
			dpi_y = ratbag_resolution_get_dpi_y(resolution);
		} else {
			dpi_x = ratbag_resolution_get_dpi(resolution);
		}

		r = sd_bus_message_open_container(reply, 'a', "{sv}");
		if (r < 0)
			return r;

		if (cap_separate_xy_resolution) {
			r = sd_bus_message_append(reply, "{sv}",
						  "dpi-x", "u", dpi_x);
			if (r < 0)
				return r;

			r = sd_bus_message_append(reply, "{sv}",
						  "dpi-y", "u", dpi_y);
			if (r < 0)
				return r;
		} else {
			r = sd_bus_message_append(reply, "{sv}",
						  "dpi", "u", dpi_x);
			if (r < 0)
				return r;
		}

		r = sd_bus_message_append(reply, "{sv}",
					  "report-rate", "u", report_rate);
		if (r < 0)
			return r;

		r = sd_bus_message_close_container(reply);
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_profile_get_active_resolution(sd_bus *bus,
						 const char *path,
						 const char *interface,
						 const char *property,
						 sd_bus_message *reply,
						 void *userdata,
						 sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	unsigned int i, n_resolutions, k = 0;

	n_resolutions = ratbag_profile_get_num_resolutions(profile->lib_profile);
	for (i = 0; i < n_resolutions; ++i) {
		struct ratbag_resolution *resolution;

		resolution = ratbag_profile_get_resolution(profile->lib_profile, i);
		if (!resolution)
			continue;
		if (!ratbag_resolution_is_active(resolution)) {
			++k;
			continue;
		}

		return sd_bus_message_append(reply, "u", k);
	}

	return sd_bus_message_append(reply, "u", (unsigned int)-1);
}

static int ratbagd_profile_get_default_resolution(sd_bus *bus,
						  const char *path,
						  const char *interface,
						  const char *property,
						  sd_bus_message *reply,
						  void *userdata,
						  sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	unsigned int i, n_resolutions, k = 0;

	n_resolutions = ratbag_profile_get_num_resolutions(profile->lib_profile);
	for (i = 0; i < n_resolutions; ++i) {
		struct ratbag_resolution *resolution;

		resolution = ratbag_profile_get_resolution(profile->lib_profile, i);
		if (!resolution)
			continue;
		if (!ratbag_resolution_is_default(resolution)) {
			++k;
			continue;
		}

		return sd_bus_message_append(reply, "u", k);
	}

	return sd_bus_message_append(reply, "u", (unsigned int)-1);
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

        return sd_bus_reply_method_return(m, "u",
                                          ratbag_profile_set_active(profile->lib_profile));
}

const sd_bus_vtable ratbagd_profile_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_profile, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Resolutions", "aa{sv}", ratbagd_profile_get_resolutions, 0, 0),
	SD_BUS_PROPERTY("ActiveResolution", "u", ratbagd_profile_get_active_resolution, 0, 0),
	SD_BUS_PROPERTY("DefaultResolution", "u", ratbagd_profile_get_default_resolution, 0, 0),
	SD_BUS_METHOD("SetActive", "", "u", ratbagd_profile_set_active, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int ratbagd_profile_new(struct ratbagd_profile **out,
			struct ratbagd_device *device,
			struct ratbag_profile *lib_profile,
			unsigned int index)
{
	_cleanup_(ratbagd_profile_freep) struct ratbagd_profile *profile = NULL;
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	assert(out);
	assert(lib_profile);

	profile = calloc(1, sizeof(*profile));
	if (!profile)
		return -ENOMEM;

	profile->lib_profile = lib_profile;
	profile->index = index;

	sprintf(index_buffer, "%u", index);
	r = sd_bus_path_encode_many(&profile->path,
				    "/org/freedesktop/ratbag1/profile/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);
	if (r < 0)
		return r;

	*out = profile;
	profile = NULL;
	return 0;
}

struct ratbagd_profile *ratbagd_profile_free(struct ratbagd_profile *profile)
{
	if (!profile)
		return NULL;

	profile->path = mfree(profile->path);

	/* we cannot assume the profile is gone, so set NULL explicitly */
	ratbag_profile_unref(profile->lib_profile);
	profile->lib_profile = NULL;

	return mfree(profile);
}

const char *ratbagd_profile_get_path(struct ratbagd_profile *profile)
{
	assert(profile);
	return profile->path;
}

bool ratbagd_profile_is_active(struct ratbagd_profile *profile)
{
	assert(profile);
	return ratbag_profile_is_active(profile->lib_profile) != 0;
}
