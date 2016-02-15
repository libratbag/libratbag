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

	sd_bus_slot *resolution_vtable_slot;
	sd_bus_slot *resolution_enum_slot;
	unsigned int n_resolutions;
	struct ratbagd_resolution **resolutions;
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
	unsigned int index;
	int r;

	r = sd_bus_path_decode_many(path,
				    "/org/freedesktop/ratbag1/resolution/%/p%/r%",
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

static int ratbagd_profile_get_resolution_by_index(sd_bus_message *m,
						   void *userdata,
						   sd_bus_error *error)
{
	struct ratbagd_profile *profile = userdata;
	struct ratbagd_resolution *resolution;
	unsigned int index;
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
	SD_BUS_PROPERTY("ActiveResolution", "u", ratbagd_profile_get_active_resolution, 0, 0),
	SD_BUS_PROPERTY("DefaultResolution", "u", ratbagd_profile_get_default_resolution, 0, 0),
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
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	unsigned int i;
	int r;

	assert(out);
	assert(lib_profile);

	profile = calloc(1, sizeof(*profile));
	if (!profile)
		return -ENOMEM;

	profile->lib_profile = lib_profile;
	profile->index = index;

	sprintf(index_buffer, "p%u", index);
	r = sd_bus_path_encode_many(&profile->path,
				    "/org/freedesktop/ratbag1/profile/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);
	if (r < 0)
		return r;

	profile->n_resolutions = ratbag_profile_get_num_resolutions(profile->lib_profile);
	profile->resolutions = calloc(profile->n_resolutions, sizeof(*profile->resolutions));
	if (!profile->resolutions)
		return -ENOMEM;

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
			fprintf(stderr,
				"Cannot allocate resolution for '%s': %m\n",
				ratbagd_device_get_name(device));
		}
	}

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

	resolutions = calloc(profile->n_resolutions + 1, sizeof(char *));
	if (!resolutions)
		return -ENOMEM;

	for (i = 0; i < profile->n_resolutions; ++i) {
		resolution = profile->resolutions[i];
		if (!resolution)
			continue;

		resolutions[i] = strdup(ratbagd_resolution_get_path(resolution));
		if (!resolutions[i])
			goto error;
	}

	resolutions[i] = NULL;
	*paths = resolutions;
	return 1;

error:
	for (i = 0; resolutions[i]; ++i)
		free(resolutions[i]);
	free(resolutions);
	return -ENOMEM;
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
				    "/org/freedesktop/ratbag1/resolution/%/%",
				    ratbagd_device_get_name(device),
				    index_buffer);


	if (r >= 0) {
		r = sd_bus_add_fallback_vtable(bus,
					       &profile->resolution_vtable_slot,
					       prefix,
					       "org.freedesktop.ratbag1.Resolution",
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
		fprintf(stderr,
			"Cannot register resolutions for '%s': %m\n",
			ratbagd_device_get_name(device));
	}

	return 0;
}
