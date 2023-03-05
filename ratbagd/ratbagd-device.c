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
#include <rbtree/shared-rbtree.h>

#include "libratbag-util.h"

struct ratbagd_device {
	unsigned int refcount;

	struct ratbagd *ctx;
	RBNode node;
	char *sysname;
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
	unsigned int index = 0;
	int r;

	r = sd_bus_path_decode_many(path,
				    RATBAGD_OBJ_ROOT "/profile/%/p%",
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

	profiles = zalloc((device->n_profiles + 1) * sizeof(char *));

	for (i = 0; i < device->n_profiles; ++i) {
		profile = device->profiles[i];
		if (!profile)
			continue;

		profiles[i] = strdup_safe(ratbagd_profile_get_path(profile));
	}

	profiles[i] = NULL;
	*paths = profiles;
	return 1;
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
		log_error("%s: failed to fetch name\n",
			  ratbagd_device_get_sysname(device));
		name = "";
	}

	CHECK_CALL(sd_bus_message_append(reply, "s", name));

	return 0;
}

static int ratbagd_device_get_device_type(sd_bus *bus,
					  const char *path,
					  const char *interface,
					  const char *property,
					  sd_bus_message *reply,
					  void *userdata,
					  sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	enum ratbag_device_type devicetype;

	devicetype = ratbag_device_get_device_type(device->lib_device);
	if (!devicetype) {
		log_error("%s: device type unspecified\n",
			  ratbagd_device_get_sysname(device));
	}

	CHECK_CALL(sd_bus_message_append(reply, "u", devicetype));

	return 0;
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

	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "o"));

	for (i = 0; i < device->n_profiles; ++i) {
		profile = device->profiles[i];
		if (!profile)
			continue;

		CHECK_CALL(sd_bus_message_append(reply,
						 "o",
						 ratbagd_profile_get_path(profile)));
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
}

static void ratbagd_device_commit_pending(void *data)
{
	struct ratbagd_device *device = data;
	int r;

	r = ratbag_device_commit(device->lib_device);
	if (r)
		log_error("error committing device (%d)\n", r);
	if (r < 0)
		ratbagd_device_resync(device, device->ctx->bus);

	ratbagd_for_each_profile_signal(device->ctx->bus,
					device,
					ratbagd_profile_notify_dirty);

	ratbagd_device_unref(device);
}

static int ratbagd_device_commit(sd_bus_message *m,
				 void *userdata,
				 sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;

	ratbagd_schedule_task(device->ctx,
			      ratbagd_device_commit_pending,
			      ratbagd_device_ref(device));

	CHECK_CALL(sd_bus_reply_method_return(m, "u", 0));

	return 0;
}

static int
ratbagd_device_get_model(sd_bus *bus,
			 const char *path,
			 const char *interface,
			 const char *property,
			 sd_bus_message *reply,
			 void *userdata,
			 sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbag_device *lib_device = device->lib_device;
	const char *bustype = ratbag_device_get_bustype(lib_device);
	uint32_t vid = ratbag_device_get_vendor_id(lib_device),
		 pid = ratbag_device_get_product_id(lib_device),
		 version = ratbag_device_get_product_version(lib_device);
	char model[64];

	if (!bustype)
		return sd_bus_message_append(reply, "s", "unknown");

	snprintf(model, sizeof(model), "%s:%04x:%04x:%d",
		 bustype, vid, pid, version);

	return sd_bus_message_append(reply, "s", model);
}

static int
ratbagd_device_get_firmware_version(sd_bus *bus,
				    const char *path,
				    const char *interface,
				    const char *property,
				    sd_bus_message *reply,
				    void *userdata,
				    sd_bus_error *error)
{
	struct ratbagd_device *device = userdata;
	struct ratbag_device *lib_device = device->lib_device;
	const char* version = ratbag_device_get_firmware_version(lib_device);

	return sd_bus_message_append(reply, "s", version);
}

const sd_bus_vtable ratbagd_device_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Model", "s", ratbagd_device_get_model, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("DeviceType", "u", ratbagd_device_get_device_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Name", "s", ratbagd_device_get_device_name, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("FirmwareVersion", "s", ratbagd_device_get_firmware_version, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Profiles", "ao", ratbagd_device_get_profiles, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_METHOD("Commit", "", "u", ratbagd_device_commit, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_SIGNAL("Resync", "", 0),
	SD_BUS_VTABLE_END,
};

int ratbagd_device_new(struct ratbagd_device **out,
		       struct ratbagd *ctx,
		       const char *sysname,
		       struct ratbag_device *lib_device)
{
	_cleanup_(ratbagd_device_unrefp) struct ratbagd_device *device = NULL;
	struct ratbag_profile *profile;
	unsigned int i;
	int r;

	assert(out);
	assert(ctx);
	assert(sysname);

	device = zalloc(sizeof(*device));
	device->refcount = 1;
	device->ctx = ctx;
	rbnode_init(&device->node);
	device->lib_device = ratbag_device_ref(lib_device);

	device->sysname = strdup_safe(sysname);

	r = sd_bus_path_encode(RATBAGD_OBJ_ROOT "/device",
			       device->sysname,
			       &device->path);
	if (r < 0)
		return r;


	device->n_profiles = ratbag_device_get_num_profiles(device->lib_device);
	device->profiles = zalloc(device->n_profiles * sizeof(*device->profiles));

	log_info("%s: \"%s\", %d profiles\n",
		 sysname,
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
			log_error("%s: failed to allocate profile: %m\n",
				  device->sysname);
		}
	}

	*out = device;
	device = NULL;
	return 0;
}

static void ratbagd_device_free(struct ratbagd_device *device)
{
	unsigned int i;

	if (!device)
		return;

	assert(!ratbagd_device_linked(device));

	for (i = 0; i < device->n_profiles; ++i)
		device->profiles[i] = ratbagd_profile_free(device->profiles[i]);

	device->profiles = mfree(device->profiles);
	device->lib_device = ratbag_device_unref(device->lib_device);
	device->path = mfree(device->path);
	device->sysname = mfree(device->sysname);

	assert(!device->lib_device); /* ratbag yields !NULL if still pinned */

	mfree(device);
}

struct ratbagd_device *ratbagd_device_ref(struct ratbagd_device *device)
{
	assert(device->refcount > 0);

	++device->refcount;
	return device;
}

struct ratbagd_device *ratbagd_device_unref(struct ratbagd_device *device)
{
	assert(device->refcount > 0);

	--device->refcount;
	if (device->refcount == 0)
		ratbagd_device_free(device);

	return NULL;
}

const char *ratbagd_device_get_sysname(struct ratbagd_device *device)
{
	assert(device);
	return device->sysname;
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

unsigned int ratbagd_device_get_num_leds(struct ratbagd_device *device)
{
	assert(device);
	return ratbag_device_get_num_leds(device->lib_device);
}

int ratbagd_device_resync(struct ratbagd_device *device, sd_bus *bus)
{
	assert(device);
	assert(bus);

	ratbagd_for_each_profile_signal(bus, device,
					ratbagd_profile_resync);

	return sd_bus_emit_signal(bus,
				  device->path,
				  RATBAGD_NAME_ROOT ".Device",
				  "Resync",
				  NULL);
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
		v = strcmp(device->sysname, iter->sysname);

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
				    RATBAGD_OBJ_ROOT "/profile/%",
				    device->sysname);
	if (r >= 0) {
		r = sd_bus_add_fallback_vtable(device->ctx->bus,
					       &device->profile_vtable_slot,
					       prefix,
					       RATBAGD_NAME_ROOT ".Profile",
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
		log_error("%s: failed to register profile interfaces: %m\n",
			  device->sysname);
		return;
	}

	for (i = 0; i < device->n_profiles; i++) {
		r = ratbagd_profile_register_resolutions(device->ctx->bus,
							 device,
							 device->profiles[i]);
		if (r < 0) {
			log_error("%s: failed to register resolutions: %m\n",
				  device->sysname);
		}

		r = ratbagd_profile_register_buttons(device->ctx->bus,
						     device,
						     device->profiles[i]);
		if (r < 0) {
			log_error("%s: failed to register buttons: %m\n",
				  device->sysname);
		}

		r = ratbagd_profile_register_leds(device->ctx->bus,
						  device,
						  device->profiles[i]);
		if (r < 0) {
			log_error("%s: failed to register leds: %m\n",
				  device->sysname);
		}
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
		v = strcmp(name, device->sysname);
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

int ratbagd_for_each_profile_signal(sd_bus *bus,
				    struct ratbagd_device *device,
				    int (*func)(sd_bus *bus,
						struct ratbagd_profile *profile))
{
	int rc = 0;

	for (size_t i = 0; rc == 0 && i < device->n_profiles; i++)
		rc = func(bus, device->profiles[i]);

	return rc;
}
