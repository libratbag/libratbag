/*
 * Copyright © 2015 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "config.h"
#include <assert.h>
#include <errno.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-util.h"

static void
ratbag_default_log_func(struct ratbag *ratbag,
			enum ratbag_log_priority priority,
			const char *format, va_list args)
{
	const char *prefix;

	switch(priority) {
	case RATBAG_LOG_PRIORITY_DEBUG: prefix = "debug"; break;
	case RATBAG_LOG_PRIORITY_INFO: prefix = "info"; break;
	case RATBAG_LOG_PRIORITY_ERROR: prefix = "error"; break;
	default: prefix="<invalid priority>"; break;
	}

	fprintf(stderr, "ratbag %s: ", prefix);
	vfprintf(stderr, format, args);
}

void
log_msg_va(struct ratbag *ratbag,
	   enum ratbag_log_priority priority,
	   const char *format,
	   va_list args)
{
	if (ratbag->log_handler &&
	    ratbag->log_priority <= priority)
		ratbag->log_handler(ratbag, priority, format, args);
}

void
log_msg(struct ratbag *ratbag,
	enum ratbag_log_priority priority,
	const char *format, ...)
{
	va_list args;

	va_start(args, format);
	log_msg_va(ratbag, priority, format, args);
	va_end(args);
}

LIBRATBAG_EXPORT void
ratbag_log_set_priority(struct ratbag *ratbag,
			enum ratbag_log_priority priority)
{
	ratbag->log_priority = priority;
}

LIBRATBAG_EXPORT enum ratbag_log_priority
ratbag_log_get_priority(const struct ratbag *ratbag)
{
	return ratbag->log_priority;
}

LIBRATBAG_EXPORT void
ratbag_log_set_handler(struct ratbag *ratbag,
		       ratbag_log_handler log_handler)
{
	ratbag->log_handler = log_handler;
}


static inline struct udev_device *
udev_device_from_devnode(struct ratbag *ratbag,
			 struct ratbag_device *device,
			 int fd)
{
	struct udev_device *dev;
	struct stat st;
	size_t count = 0;
	struct udev *udev = ratbag->udev;

	if (fstat(fd, &st) < 0)
		return NULL;

	dev = udev_device_new_from_devnum(ratbag->udev, 'c', st.st_rdev);

	while (dev && !udev_device_get_is_initialized(dev)) {
		udev_device_unref(dev);
		msleep(10);
		dev = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

		count++;
		if (count > 50) {
			log_bug_libratbag(ratbag,
					  "udev device never initialized\n");
			break;
		}
	}

	return dev;
}

static struct udev_device *
udev_find_hidraw(struct ratbag *ratbag, struct ratbag_device *device)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	struct udev_device *udev_device;
	const char *path, *sysname;
	struct udev_device *hid_udev;
	struct udev_device *hidraw_udev = NULL;
	struct udev *udev = ratbag->udev;

	hid_udev = udev_device_get_parent_with_subsystem_devtype(device->udev_device, "hid", NULL);

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_add_match_parent(e, hid_udev);
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		udev_device = udev_device_new_from_syspath(udev, path);
		if (!udev_device)
			continue;

		sysname = udev_device_get_sysname(udev_device);
		if (strncmp("hidraw", sysname, 6) != 0) {
			udev_device_unref(udev_device);
			continue;
		}

		hidraw_udev = udev_device_ref(udev_device);

		udev_device_unref(udev_device);
		goto out;
	}

out:
	udev_enumerate_unref(e);

	return hidraw_udev;
}

static int
ratbag_device_init_udev(struct ratbag *ratbag, struct ratbag_device *device,
			struct udev_device *udev_device)
{
	struct udev_device *hidraw_udev;
	int rc = -ENODEV;

	device->udev_device = udev_device_ref(udev_device);

	hidraw_udev = udev_find_hidraw(ratbag, device);
	if (!hidraw_udev)
		goto out;

	device->udev_hidraw = udev_device_ref(hidraw_udev);

	log_debug(ratbag,
		  "%s is device '%s'.\n",
		  device->name,
		  udev_device_get_devnode(device->udev_hidraw));

	rc = 0;
	udev_device_unref(hidraw_udev);
out:
	return rc;
}

static void
ratbag_device_init(struct ratbag_device *rb)
{
	rb->hidraw_fd = -1;
	rb->refcount = 1;
	list_init(&rb->profiles);
}

static inline bool
ratbag_match_id(const struct input_id *dev_id, const struct input_id *match_id)
{
	return (match_id->bustype == BUS_ANY || match_id->bustype == dev_id->bustype) &&
		(match_id->vendor == VENDOR_ANY || match_id->vendor == dev_id->vendor) &&
		(match_id->product == PRODUCT_ANY || match_id->product == dev_id->product) &&
		(match_id->version == VERSION_ANY || match_id->version == dev_id->version);
}

static struct ratbag_driver *
ratbag_find_driver(struct ratbag *ratbag, struct ratbag_device *device,
		const struct input_id *dev_id)
{
	struct ratbag_driver *driver;
	const struct ratbag_id *matching_id;
	struct ratbag_id matched_id;
	int rc;

	list_for_each(driver, &ratbag->drivers, link) {
		log_debug(ratbag, "trying driver '%s'\n", driver->name);
		matching_id = driver->table_ids;
		do {
			if (ratbag_match_id(dev_id, &matching_id->id)) {
				assert(driver->probe);
				matched_id.id = *dev_id;
				matched_id.data = matching_id->data;
				device->driver = driver;
				rc = driver->probe(device, matched_id);
				if (rc == 0) {
					log_debug(ratbag, "driver match found\n");
					return driver;
				}

				device->driver = NULL;

				if (rc != -ENODEV)
					return NULL;
			}
			matching_id++;
		} while (matching_id->id.bustype != 0 ||
			 matching_id->id.vendor != 0 ||
			 matching_id->id.product != 0 ||
			 matching_id->id.version != 0);
	}

	return NULL;
}

static inline char*
get_device_name(struct udev_device *device)
{
	const char *prop;

	prop = udev_prop_value(device, "NAME");
	if (!prop)
		return NULL;

	/* udev name is inclosed by " */
	return strndup(&prop[1], strlen(prop) - 2);
}

static inline int
get_product_id(struct udev_device *device, struct input_id *id)
{
	const char *product;
	struct input_id ids;
	int rc;

	product = udev_prop_value(device, "PRODUCT");
	if (!product)
		return -1;

	rc = sscanf(product, "%hx/%hx/%hx/%hx", &ids.bustype,
		    &ids.vendor, &ids.product, &ids.version);
	if (rc != 4)
		return -1;

	*id = ids;
	return 0;
}

LIBRATBAG_EXPORT struct ratbag_device*
ratbag_device_new_from_udev_device(struct ratbag *ratbag,
				   struct udev_device *udev_device)
{
	int rc;
	struct ratbag_device *device = NULL;
	struct ratbag_driver *driver;

	if (!ratbag) {
		fprintf(stderr, "ratbag is NULL\n");
		return NULL;
	}

	device = zalloc(sizeof(*device));
	if (!device)
		return NULL;

	device->ratbag = ratbag_ref(ratbag);
	if (get_product_id(udev_device, &device->ids) != 0)
		goto out_err;
	free(device->name);
	device->name = get_device_name(udev_device);
	if (!device->name) {
		errno = ENOMEM;
		goto out_err;
	}

	ratbag_device_init(device);
	rc = ratbag_device_init_udev(ratbag, device, udev_device);
	if (rc)
		goto out_err;

	driver = ratbag_find_driver(ratbag, device, &device->ids);
	if (!driver) {
		errno = ENOTSUP;
		goto err_udev;
	}

	return device;

err_udev:
	udev_device_unref(device->udev_device);
	udev_device_unref(device->udev_hidraw);
out_err:
	if (device) {
		free(device->name);
		device->ratbag = ratbag_unref(device->ratbag);
	}
	free(device);
	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_device *
ratbag_device_ref(struct ratbag_device *device)
{
	device->refcount++;
	return device;
}

LIBRATBAG_EXPORT struct ratbag_device *
ratbag_device_unref(struct ratbag_device *device)
{
	if (device == NULL)
		return NULL;

	assert(device->refcount > 0);
	device->refcount--;
	if (device->refcount > 0)
		return device;

	if (device->driver->remove)
		device->driver->remove(device);

	udev_device_unref(device->udev_device);
	udev_device_unref(device->udev_hidraw);

	if (device->hidraw_fd >= 0)
		close(device->hidraw_fd);

	device->ratbag = ratbag_unref(device->ratbag);
	free(device->name);
	free(device);
	return NULL;
}

LIBRATBAG_EXPORT const char *
ratbag_device_get_name(const struct ratbag_device* device)
{
	return device->name;
}

static void
ratbag_register_driver(struct ratbag *ratbag, struct ratbag_driver *driver)
{
	list_insert(&ratbag->drivers, &driver->link);
}

LIBRATBAG_EXPORT struct ratbag *
ratbag_create_context(const struct ratbag_interface *interface,
			 void *userdata)
{
	struct ratbag *ratbag;

	if (interface == NULL ||
	    interface->open_restricted == NULL ||
	    interface->close_restricted == NULL)
		return NULL;

	ratbag = zalloc(sizeof(*ratbag));
	if (!ratbag)
		return NULL;

	ratbag->refcount = 1;
	ratbag->interface = interface;
	ratbag->userdata = userdata;

	list_init(&ratbag->drivers);
	ratbag->udev = udev_new();
	if (!ratbag->udev) {
		free(ratbag);
		return NULL;
	}

	ratbag->log_handler = ratbag_default_log_func;
	ratbag->log_priority = RATBAG_LOG_PRIORITY_DEBUG;

	ratbag_register_driver(ratbag, &etekcity_driver);

	return ratbag;
}

LIBRATBAG_EXPORT struct ratbag *
ratbag_ref(struct ratbag *ratbag)
{
	ratbag->refcount++;
	return ratbag;
}

LIBRATBAG_EXPORT struct ratbag *
ratbag_unref(struct ratbag *ratbag)
{
	if (ratbag == NULL)
		return NULL;

	assert(ratbag->refcount > 0);
	ratbag->refcount--;
	if (ratbag->refcount > 0)
		return ratbag;

	ratbag->udev = udev_unref(ratbag->udev);
	free(ratbag);

	return NULL;
}

static struct ratbag_profile *
ratbag_create_profile(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_profile *profile;

	profile = zalloc(sizeof(*profile));
	if (!profile)
		return NULL;

	profile->refcount = 1;
	profile->device = device;
	profile->index = index;

	list_insert(&device->profiles, &profile->link);
	list_init(&profile->buttons);

	assert(device->driver->read_profile);
	device->driver->read_profile(profile, index);

	return profile;
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_profile_ref(struct ratbag_profile *profile)
{
	profile->refcount++;
	return profile;
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_profile_unref(struct ratbag_profile *profile)
{
	if (profile == NULL)
		return NULL;

	assert(profile->refcount > 0);
	profile->refcount--;
	if (profile->refcount > 0)
		return profile;

	list_remove(&profile->link);
	free(profile);

	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_device_get_profile_by_index(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_profile *profile;

	list_for_each(profile, &device->profiles, link) {
		if (profile->index == index) {
			assert(device->driver->read_profile);
			device->driver->read_profile(profile, index);
			return ratbag_profile_ref(profile);
		}
	}

	return ratbag_create_profile(device, index);
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_device_get_active_profile(struct ratbag_device *device)
{
	int current_profile;

	assert(device->driver->get_active_profile);
	current_profile = device->driver->get_active_profile(device);
	if (current_profile < 0) {
		errno = -current_profile;
		return NULL;
	}

	return ratbag_device_get_profile_by_index(device, current_profile);
}

LIBRATBAG_EXPORT unsigned int
ratbag_device_get_num_profiles(struct ratbag_device *device)
{
	return device->num_profiles;
}

LIBRATBAG_EXPORT unsigned int
ratbag_device_get_num_buttons(struct ratbag_device *device)
{
	return device->num_buttons;
}

LIBRATBAG_EXPORT int
ratbag_device_has_capability(const struct ratbag_device *device,
			     enum ratbag_capability cap)
{
	assert(device->driver->has_capability);
	return device->driver->has_capability(device, cap);
}

LIBRATBAG_EXPORT int
ratbag_device_set_active_profile(struct ratbag_device *device,
				 struct ratbag_profile *profile)
{
	int rc;

	assert(device->driver->write_profile);
	rc = device->driver->write_profile(profile);
	if (rc)
		return rc;

	assert(device->driver->set_active_profile);
	return device->driver->set_active_profile(device, profile->index);
}

LIBRATBAG_EXPORT int
ratbag_profile_get_resolution_dpi(const struct ratbag_profile *profile)
{
	return 0;
}

LIBRATBAG_EXPORT int
ratbag_profile_get_report_rate_hz(const struct ratbag_profile *profile)
{
	return 0;
}

static struct ratbag_button *
ratbag_create_button(struct ratbag_device *device, struct ratbag_profile *profile,
		unsigned int index)
{
	struct ratbag_button *button;

	button = zalloc(sizeof(*button));
	if (!button)
		return NULL;

	button->refcount = 1;
	button->device = device;
	button->profile = profile;
	button->index = index;

	if (profile)
		list_insert(&profile->buttons, &button->link);

	if (device->driver->read_button)
		device->driver->read_button(device, profile, button);

	return button;
}

LIBRATBAG_EXPORT struct ratbag_button*
ratbag_profile_get_button_by_index(struct ratbag_profile *profile,
				   unsigned int index)
{
	struct ratbag_button *button;

	list_for_each(button, &profile->buttons, link) {
		if (button->index == index) {
			if (profile->device->driver->read_button)
				profile->device->driver->read_button(profile->device, profile, button);
			return ratbag_button_ref(button);
		}
	}

	return ratbag_create_button(profile->device, profile, index);
}

LIBRATBAG_EXPORT enum ratbag_button_type
ratbag_button_get_type(struct ratbag_button *button)
{
	return RATBAG_BUTTON_TYPE_UNKNOWN;
}

LIBRATBAG_EXPORT struct ratbag_button *
ratbag_button_ref(struct ratbag_button *button)
{
	button->refcount++;
	return button;
}

LIBRATBAG_EXPORT struct ratbag_button *
ratbag_button_unref(struct ratbag_button *button)
{
	if (button == NULL)
		return NULL;

	assert(button->refcount > 0);
	button->refcount--;
	if (button->refcount > 0)
		return button;

	list_remove(&button->link);
	free(button);

	return NULL;
}
