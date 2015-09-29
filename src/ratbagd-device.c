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
				    "/org/freedesktop/ratbag1/device/%/profile/%",
				    NULL,
				    &name);
	if (r <= 0)
		return r;

	r = safe_atou(name, &index);
	if (r < 0)
		return 0;

	if (index >= device->n_profiles)
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
	char index_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	char **profiles;
	unsigned int i;
	int r;

	profiles = calloc(device->n_profiles + 1, sizeof(char *));
	if (!profiles)
		return -ENOMEM;

	for (i = 0; i < device->n_profiles; ++i) {
		sprintf(index_buffer, "%u", i);
		r = sd_bus_path_encode_many(&profiles[i],
					    "/org/freedesktop/ratbag1/device/%/profile/%",
					    device->name,
					    index_buffer);
		if (r < 0)
			goto error;
	}

	profiles[i] = NULL;
	*paths = profiles;
	return 1;

error:
	for (i = 0; profiles[i]; ++i)
		free(profiles[i]);
	free(profiles);
	return r;
}

static int ratbagd_device_get_description(sd_bus *bus,
					  const char *path,
					  const char *interface,
					  const char *property,
					  sd_bus_message *reply,
					  void *userdata,
					  sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	const char *description;

	description = ratbag_device_get_name(device->lib_device);
	if (!description)
		return -ENODATA;

	return sd_bus_message_append(reply, "s", description);
}

const sd_bus_vtable ratbagd_device_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Id", "s", NULL, offsetof(struct ratbagd_device, name), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Description", "s", ratbagd_device_get_description, 0, SD_BUS_VTABLE_PROPERTY_CONST),
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

	for (i = 0; i < device->n_profiles; ++i) {
		profile = ratbag_device_get_profile(device->lib_device, i);
		if (!profile)
			continue;

		r = ratbagd_profile_new(&device->profiles[i], profile, i);
		if (r < 0) {
			errno = -r;
			fprintf(stderr,
				"Cannot allocate profile for '%s': %m\n",
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

const char *ratbagd_device_get_path(struct ratbagd_device *device)
{
	assert(device);
	return device->path;
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
	r = asprintf(&prefix, "%s/profile", device->path);
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
		fprintf(stderr,
			"Cannot register profile interfaces for '%s': %m\n",
			device->name);
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
