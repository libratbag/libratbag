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
	struct ratbag_device *lib_device;
};

#define ratbagd_device_from_node(_ptr) \
		rbnode_of((_ptr), struct ratbagd_device, node)

static int ratbagd_device_find(sd_bus *bus,
			       const char *path,
			       const char *interface,
			       void *userdata,
			       void **found,
			       sd_bus_error *error)
{
	_cleanup_(freep) char *name = NULL;
	struct ratbagd *ctx = userdata;
	struct ratbagd_device *device;
	int r;

	/* decodes the suffix into @name, returns 1 if valid */
	r = sd_bus_path_decode(path, "/org/freedesktop/ratbag1/device", &name);
	if (r <= 0)
		return r;

	device = ratbagd_find_device(ctx, name);
	if (!device)
		return 0;

	*found = device;
	return 1;
}

static int ratbagd_device_list(sd_bus *bus,
			       const char *path,
			       void *userdata,
			       char ***nodes,
			       sd_bus_error *error)
{
	struct ratbagd *ctx = userdata;
	struct ratbagd_device *device;
	char **devices, **pos;
	RBNode *node;
	int r;

	devices = calloc(ctx->n_devices + 1, sizeof(char *));
	if (!devices)
		return -ENOMEM;

	pos = devices;

	for (node = rbtree_first(&ctx->device_map);
	     node;
	     node = rbnode_next(node)) {
		device = ratbagd_device_from_node(node);

		r = sd_bus_path_encode("/org/freedesktop/ratbag1/device",
				       device->name,
				       pos++);
		if (r < 0)
			goto error;
	}

	*pos = NULL;

	*nodes = devices;
	return 1;

error:
	for (pos = devices; *pos; ++pos)
		free(*pos);
	free(devices);
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

static const sd_bus_vtable ratbagd_device_vtable[] = {
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

	*out = device;
	device = NULL;
	return 0;
}

struct ratbagd_device *ratbagd_device_free(struct ratbagd_device *device)
{
	if (!device)
		return NULL;

	assert(!ratbagd_device_linked(device));

	device->lib_device = ratbag_device_unref(device->lib_device);
	device->name = mfree(device->name);

	assert(!device->lib_device); /* ratbag yields !NULL if still pinned */

	return mfree(device);
}

bool ratbagd_device_linked(struct ratbagd_device *device)
{
	return device && rbnode_linked(&device->node);
}

void ratbagd_device_link(struct ratbagd_device *device)
{
	_cleanup_(freep) char *path = NULL;
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

	/* send out object-manager notification */
	r = sd_bus_path_encode("/org/freedesktop/ratbag1/device",
			       device->name,
			       &path);
	if (r >= 0)
		r = sd_bus_emit_object_added(device->ctx->bus, path);
	if (r < 0) {
		errno = -r;
		fprintf(stderr,
			"Cannot send device notification for '%s': %m\n",
			device->name);
	}
}

void ratbagd_device_unlink(struct ratbagd_device *device)
{
	_cleanup_(freep) char *path = NULL;
	int r;

	if (!ratbagd_device_linked(device))
		return;

	/* send out object-manager notification */
	r = sd_bus_path_encode("/org/freedesktop/ratbag1/device",
			       device->name,
			       &path);
	if (r >= 0)
		r = sd_bus_emit_object_removed(device->ctx->bus, path);
	if (r < 0) {
		errno = -r;
		fprintf(stderr,
			"Cannot send device notification for '%s': %m\n",
			device->name);
	}

	/* unlink from context */
	--device->ctx->n_devices;
	rbtree_remove(&device->ctx->device_map, &device->node);
	rbnode_init(&device->node);
}

int ratbagd_init_device(struct ratbagd *ctx)
{
	int r;

	assert(ctx);

	/*
	 * Called by the context initializer after the context was prepared but
	 * not run, yet. No need to cleanup anything if an error is returned,
	 * as long it is teared down automatically on destruction.
	 */

	r = sd_bus_add_fallback_vtable(ctx->bus,
				       NULL,
				       "/org/freedesktop/ratbag1/device",
				       "org.freedesktop.ratbag1.Device",
				       ratbagd_device_vtable,
				       ratbagd_device_find,
				       ctx);
	if (r < 0)
		return r;

	r = sd_bus_add_node_enumerator(ctx->bus,
				       NULL,
				       "/org/freedesktop/ratbag1/device",
				       ratbagd_device_list,
				       ctx);
	if (r < 0)
		return r;

	return 0;
}

struct ratbagd_device *ratbagd_find_device(struct ratbagd *ctx,
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
