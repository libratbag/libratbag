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
#include <limits.h>
#include <libratbag.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "ratbagd.h"
#include "shared-macro.h"
#include "shared-rbtree.h"

struct ratbagd_device {
	struct ratbagd *ctx;
	RBNode node;
	char *name;
	char *path;
	struct ratbag_device *lib_device;

	sd_bus_slot *profile_vtable_slot;
	sd_bus_slot *profile_enum_slot;
	unsigned int n_profiles;
	struct ratbagd_profile **profiles;
};

#define ratbagd_device_from_node(_ptr) \
		rbnode_of((_ptr), struct ratbagd_device, node)

static int ratbagd_device_find_profile(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       void *userdata,
				       void **found,
				       sd_bus_error *error)
{
	_cleanup_(freep) char *name = NULL;
	struct ratbagd_device *device = userdata;
	unsigned int index;
	int r;

	r = sd_bus_path_decode_many(path,
				    "/org/freedesktop/ratbag1/profile/%/p%",
				    NULL,
				    &name);
	if (r <= 0)
		return r;

	r = safe_atou(name, &index);
	if (r < 0)
		return 0;

	if (index >= device->n_profiles || !device->profiles[index])
		return 0;

	*found = device->profiles[index];
	return 1;
}

static int ratbagd_device_list_profiles(sd_bus *bus,
					const char *path,
					void *userdata,
					char ***paths,
					sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbagd_profile *profile;
	char **profiles;
	unsigned int i;

	profiles = calloc(device->n_profiles + 1, sizeof(char *));
	if (!profiles)
		return -ENOMEM;

	for (i = 0; i < device->n_profiles; ++i) {
		profile = device->profiles[i];
		if (!profile)
			continue;

		profiles[i] = strdup(ratbagd_profile_get_path(profile));
		if (!profiles[i])
			goto error;
	}

	profiles[i] = NULL;
	*paths = profiles;
	return 1;

error:
	for (i = 0; profiles[i]; ++i)
		free(profiles[i]);
	free(profiles);
	return -ENOMEM;
}

static int ratbagd_device_get_device_name(sd_bus *bus,
					  const char *path,
					  const char *interface,
					  const char *property,
					  sd_bus_message *reply,
					  void *userdata,
					  sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	const char *name;

	name = ratbag_device_get_name(device->lib_device);
	if (!name) {
		log_error("Unable to fetch name for %s\n",
			  ratbagd_device_get_name(device));
		name = "";
	}

	return sd_bus_message_append(reply, "s", name);
}

static int ratbagd_device_get_svg(sd_bus *bus,
				  const char *path,
				  const char *interface,
				  const char *property,
				  sd_bus_message *reply,
				  void *userdata,
				  sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	const char *svg;

	svg = ratbag_device_get_svg_name(device->lib_device);
	if (!svg) {
		log_error("Unable to fetch SVG for %s\n",
			  ratbagd_device_get_name(device));
		svg = "";
	}

	return sd_bus_message_append(reply, "s", svg);
}

static int ratbagd_device_get_svg_path(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *reply,
				       void *userdata,
				       sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	char svg_path[PATH_MAX] = {0};
	const char *svg;

	svg = ratbag_device_get_svg_name(device->lib_device);
	if (!svg) {
		log_error("Unable to fetch SVG for %s\n",
			  ratbagd_device_get_name(device));
		goto out;
	}

	sprintf(svg_path, "%s/%s", LIBRATBAG_DATA_DIR, svg);

out:
	return sd_bus_message_append(reply, "s", svg_path);
}

static int ratbagd_device_get_profiles(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *reply,
				       void *userdata,
				       sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbagd_profile *profile;
	unsigned int i;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "o");
	if (r < 0)
		return r;

	for (i = 0; i < device->n_profiles; ++i) {
		profile = device->profiles[i];
		if (!profile)
			continue;

		r = sd_bus_message_append(reply,
					  "o",
					  ratbagd_profile_get_path(profile));
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static int ratbagd_device_get_active_profile(sd_bus *bus,
					     const char *path,
					     const char *interface,
					     const char *property,
					     sd_bus_message *reply,
					     void *userdata,
					     sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbagd_profile *profile;
	unsigned int i;

	for (i = 0; i < device->n_profiles; ++i) {
		profile = device->profiles[i];
		if (!profile)
			continue;
		if (!ratbagd_profile_is_active(profile))
			continue;

		return sd_bus_message_append(reply, "u", i);
	}

	log_error("Unable to find active profile for %s\n", device->name);
	return sd_bus_message_append(reply, "u", 0);
}

static int ratbagd_device_get_profile_by_index(sd_bus_message *m,
					       void *userdata,
					       sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbagd_profile *profile;
	unsigned int index;
	int r;

	r = sd_bus_message_read(m, "u", &index);
	if (r < 0)
		return r;

	if (index >= device->n_profiles || !device->profiles[index])
		return -ENXIO;

	profile = device->profiles[index];
	return sd_bus_reply_method_return(m, "o",
					  ratbagd_profile_get_path(profile));
}

static int
ratbagd_device_get_capabilities(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbag_device *lib_device = device->lib_device;
	enum ratbag_device_capability cap;
	enum ratbag_device_capability caps[] = {
		RATBAG_DEVICE_CAP_QUERY_CONFIGURATION,
		RATBAG_DEVICE_CAP_RESOLUTION,
		RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION,
		RATBAG_DEVICE_CAP_PROFILE,
		RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE,
		RATBAG_DEVICE_CAP_DISABLE_PROFILE,
		RATBAG_DEVICE_CAP_DEFAULT_PROFILE,
		RATBAG_DEVICE_CAP_BUTTON,
		RATBAG_DEVICE_CAP_BUTTON_KEY,
		RATBAG_DEVICE_CAP_BUTTON_MACROS,
		RATBAG_DEVICE_CAP_LED,
	};
	int r;
	size_t i;

	r = sd_bus_message_open_container(reply, 'a', "u");
	if (r < 0)
		return r;

	for (i = 0; i < ELEMENTSOF(caps); i++) {
		cap = caps[i];
		if (ratbag_device_has_capability(lib_device, cap)) {
			r = sd_bus_message_append(reply, "u", cap);
			if (r < 0)
				return r;
		}
	}

	return sd_bus_message_close_container(reply);
}

const sd_bus_vtable ratbagd_device_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Id", "s", NULL, offsetof(struct ratbagd_device, name), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Capabilities", "au", ratbagd_device_get_capabilities, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Name", "s", ratbagd_device_get_device_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Svg", "s", ratbagd_device_get_svg, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("SvgPath", "s", ratbagd_device_get_svg_path, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Profiles", "ao", ratbagd_device_get_profiles, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("ActiveProfile", "u", ratbagd_device_get_active_profile, 0, 0),
	SD_BUS_METHOD("GetProfileByIndex", "u", "o", ratbagd_device_get_profile_by_index, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int ratbagd_device_new(struct ratbagd_device **out,
		       struct ratbagd *ctx,
		       const char *name,
		       struct ratbag_device *lib_device)
{
	_cleanup_(ratbagd_device_freep) struct ratbagd_device *device = NULL;
	struct ratbag_profile *profile;
	unsigned int i;
	int r;

	assert(out);
	assert(ctx);
	assert(name);

	device = calloc(1, sizeof(*device));
	if (!device)
		return -ENOMEM;

	device->ctx = ctx;
	rbnode_init(&device->node);
	device->lib_device = ratbag_device_ref(lib_device);

	device->name = strdup(name);
	if (!device->name)
		return -ENOMEM;

	r = sd_bus_path_encode("/org/freedesktop/ratbag1/device",
			       device->name,
			       &device->path);
	if (r < 0)
		return r;


	device->n_profiles = ratbag_device_get_num_profiles(device->lib_device);
	device->profiles = calloc(device->n_profiles, sizeof(*device->profiles));
	if (!device->profiles)
		return -ENOMEM;

	log_verbose("%s: \"%s\", %d profiles\n",
		    name,
		    ratbag_device_get_name(lib_device),
		    device->n_profiles);

	for (i = 0; i < device->n_profiles; ++i) {
		profile = ratbag_device_get_profile(device->lib_device, i);
		if (!profile)
			continue;

		r = ratbagd_profile_new(&device->profiles[i],
					device,
					profile,
					i);
		if (r < 0) {
			errno = -r;
			log_error("Cannot allocate profile for '%s': %m\n",
				  device->name);
		}
	}

	*out = device;
	device = NULL;
	return 0;
}

struct ratbagd_device *ratbagd_device_free(struct ratbagd_device *device)
{
	unsigned int i;

	if (!device)
		return NULL;

	assert(!ratbagd_device_linked(device));

	for (i = 0; i < device->n_profiles; ++i)
		device->profiles[i] = ratbagd_profile_free(device->profiles[i]);

	device->profiles = mfree(device->profiles);
	device->lib_device = ratbag_device_unref(device->lib_device);
	device->path = mfree(device->path);
	device->name = mfree(device->name);

	assert(!device->lib_device); /* ratbag yields !NULL if still pinned */

	return mfree(device);
}

const char *ratbagd_device_get_name(struct ratbagd_device *device)
{
	assert(device);
	return device->name;
}

const char *ratbagd_device_get_path(struct ratbagd_device *device)
{
	assert(device);
	return device->path;
}

unsigned int ratbagd_device_get_num_buttons(struct ratbagd_device *device)
{
	assert(device);
	return ratbag_device_get_num_buttons(device->lib_device);
}

bool ratbagd_device_linked(struct ratbagd_device *device)
{
	return device && rbnode_linked(&device->node);
}

void ratbagd_device_link(struct ratbagd_device *device)
{
	_cleanup_(freep) char *prefix = NULL;
	struct ratbagd_device *iter;
	RBNode **node, *parent;
	int r, v;
	unsigned int i;

	assert(device);
	assert(!ratbagd_device_linked(device));

	/* find place to link it to */
	parent = NULL;
	node = &device->ctx->device_map.root;
	while (*node) {
		parent = *node;
		iter = ratbagd_device_from_node(parent);
		v = strcmp(device->name, iter->name);

		/* if there's a duplicate, the caller screwed up */
		assert(v != 0);

		if (v < 0)
			node = &parent->left;
		else /* if (v > 0) */
			node = &parent->right;
	}

	/* link into context */
	rbtree_add(&device->ctx->device_map, parent, node, &device->node);
	++device->ctx->n_devices;

	/* register profile interfaces */
	r = sd_bus_path_encode_many(&prefix,
				    "/org/freedesktop/ratbag1/profile/%",
				    device->name);
	if (r >= 0) {
		r = sd_bus_add_fallback_vtable(device->ctx->bus,
					       &device->profile_vtable_slot,
					       prefix,
					       "org.freedesktop.ratbag1.Profile",
					       ratbagd_profile_vtable,
					       ratbagd_device_find_profile,
					       device);
		if (r >= 0)
			r = sd_bus_add_node_enumerator(device->ctx->bus,
						       &device->profile_enum_slot,
						       prefix,
						       ratbagd_device_list_profiles,
						       device);
	}
	if (r < 0) {
		errno = -r;
		log_error("Cannot register profile interfaces for '%s': %m\n",
			  device->name);
		return;
	}

	for (i = 0; i < device->n_profiles; i++) {
		r = ratbagd_profile_register_resolutions(device->ctx->bus,
							 device,
							 device->profiles[i]);
		if (r < 0) {
			log_error("Cannot register resolutions for '%s': %m\n",
				  device->name);
		}

		r = ratbagd_profile_register_buttons(device->ctx->bus,
						     device,
						     device->profiles[i]);
	}
}

void ratbagd_device_unlink(struct ratbagd_device *device)
{
	if (!ratbagd_device_linked(device))
		return;

	device->profile_enum_slot = sd_bus_slot_unref(device->profile_enum_slot);
	device->profile_vtable_slot = sd_bus_slot_unref(device->profile_vtable_slot);

	/* unlink from context */
	--device->ctx->n_devices;
	rbtree_remove(&device->ctx->device_map, &device->node);
	rbnode_init(&device->node);
}

struct ratbagd_device *ratbagd_device_lookup(struct ratbagd *ctx,
					     const char *name)
{
	struct ratbagd_device *device;
	RBNode *node;
	int v;

	assert(ctx);
	assert(name);

	node = ctx->device_map.root;
	while (node) {
		device = ratbagd_device_from_node(node);
		v = strcmp(name, device->name);
		if (!v)
			return device;
		else if (v < 0)
			node = node->left;
		else /* if (v > 0) */
			node = node->right;
	}

	return NULL;
}

struct ratbagd_device *ratbagd_device_first(struct ratbagd *ctx)
{
	struct RBNode *node;

	assert(ctx);

	node = rbtree_first(&ctx->device_map);
	return node ? ratbagd_device_from_node(node) : NULL;
}

struct ratbagd_device *ratbagd_device_next(struct ratbagd_device *device)
{
	struct RBNode *node;

	assert(device);

	node = rbnode_next(&device->node);
	return node ? ratbagd_device_from_node(node) : NULL;
}
