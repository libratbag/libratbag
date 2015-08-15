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
#include <fcntl.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-util.h"

struct libratbag {
	struct udev *udev;
	struct list drivers;

	int refcount;
	libratbag_log_handler log_handler;
	enum libratbag_log_priority log_priority;
};

static void
libratbag_default_log_func(struct libratbag *libratbag,
			  enum libratbag_log_priority priority,
			  const char *format, va_list args)
{
	const char *prefix;

	switch(priority) {
	case LIBRATBAG_LOG_PRIORITY_DEBUG: prefix = "debug"; break;
	case LIBRATBAG_LOG_PRIORITY_INFO: prefix = "info"; break;
	case LIBRATBAG_LOG_PRIORITY_ERROR: prefix = "error"; break;
	default: prefix="<invalid priority>"; break;
	}

	fprintf(stderr, "libratbag %s: ", prefix);
	vfprintf(stderr, format, args);
}

void
log_msg_va(struct libratbag *libratbag,
	   enum libratbag_log_priority priority,
	   const char *format,
	   va_list args)
{
	if (libratbag->log_handler &&
	    libratbag->log_priority <= priority)
		libratbag->log_handler(libratbag, priority, format, args);
}

void
log_msg(struct libratbag *libratbag,
	enum libratbag_log_priority priority,
	const char *format, ...)
{
	va_list args;

	va_start(args, format);
	log_msg_va(libratbag, priority, format, args);
	va_end(args);
}

static inline struct udev_device *
udev_device_from_devnode(struct libratbag *libratbag,
			 struct ratbag *ratbag,
			 int fd)
{
	struct udev_device *dev;
	struct stat st;
	size_t count = 0;
	struct udev *udev = libratbag->udev;

	if (fstat(fd, &st) < 0)
		return NULL;

	dev = udev_device_new_from_devnum(libratbag->udev, 'c', st.st_rdev);

	while (dev && !udev_device_get_is_initialized(dev)) {
		udev_device_unref(dev);
		msleep(10);
		dev = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

		count++;
		if (count > 50) {
			log_bug_libratbag(libratbag,
					  "udev device never initialized\n");
			break;
		}
	}

	return dev;
}

static struct udev_device *
udev_find_hidraw(struct libratbag *libratbag, struct ratbag *ratbag)
{
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	struct udev_device *device;
	const char *path, *sysname;
	struct udev_device *hid_udev;
	struct udev_device *hidraw_udev = NULL;
	struct udev *udev = libratbag->udev;

	hid_udev = udev_device_get_parent_with_subsystem_devtype(ratbag->udev_device, "hid", NULL);

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_add_match_parent(e, hid_udev);
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		device = udev_device_new_from_syspath(udev, path);
		if (!device)
			continue;

		sysname = udev_device_get_sysname(device);
		if (strncmp("hidraw", sysname, 6) != 0) {
			udev_device_unref(device);
			continue;
		}

		hidraw_udev = udev_device_ref(device);

		udev_device_unref(device);
		goto out;
	}

out:
	udev_enumerate_unref(e);

	return hidraw_udev;
}

static int
ratbag_device_init_udev(struct libratbag *libratbag, struct ratbag *ratbag, int fd)
{
	struct udev_device *udev_device;
	struct udev_device *hidraw_udev;
	int rc = -ENODEV;

	udev_device = udev_device_from_devnode(libratbag, ratbag, fd);
	if (!udev_device) {
		log_bug_client(libratbag, "Invalid path %s\n", fd);
		return -ENODEV;
	}
	ratbag->udev_device = udev_device_ref(udev_device);

	hidraw_udev = udev_find_hidraw(libratbag, ratbag);
	if (!hidraw_udev)
		goto out;

	ratbag->udev_hidraw = udev_device_ref(hidraw_udev);

	log_debug(libratbag,
		  "%s is associated to '%s'.\n",
		  udev_device_get_devnode(ratbag->udev_hidraw), "the device");

	rc = 0;
	udev_device_unref(hidraw_udev);
out:
	udev_device_unref(udev_device);
	return rc;
}

void
ratbag_device_init(struct ratbag *rb, int fd)
{
	rb->evdev_fd = fd;
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
ratbag_find_driver(struct libratbag *libratbag, struct ratbag *ratbag,
		const struct input_id *dev_id)
{
	struct ratbag_driver *driver;
	const struct ratbag_id *matching_id;
	struct ratbag_id matched_id;
	int rc;

	list_for_each(driver, &libratbag->drivers, link) {
		log_debug(libratbag, "testing against %s\n", driver->name);
		matching_id = driver->table_ids;
		do {
			if (ratbag_match_id(dev_id, &matching_id->id)) {
				assert(driver->probe);
				matched_id.id = *dev_id;
				matched_id.data = matching_id->data;
				ratbag->driver = driver;
				rc = driver->probe(ratbag, matched_id);
				if (rc == 0)
					return driver;

				ratbag->driver = NULL;

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

LIBRATBAG_EXPORT struct ratbag*
ratbag_new_from_fd(struct libratbag *libratbag, int fd)
{
	int rc;
	struct ratbag *ratbag = NULL;
	struct ratbag_driver *driver;
	char buf[256];

	if (!libratbag) {
		fprintf(stderr, "libratbag is NULL\n");
		return NULL;
	}

	ratbag = zalloc(sizeof(*ratbag));
	if (!ratbag)
		return NULL;

	ratbag->libratbag = libratbag_ref(libratbag);

	rc = ioctl(fd, EVIOCGID, &ratbag->ids);
	if (rc < 0)
		goto out_err;

	memset(buf, 0, sizeof(buf));
	rc = ioctl(fd, EVIOCGNAME(sizeof(buf) - 1), buf);
	if (rc < 0)
		goto out_err;

	free(ratbag->name);
	ratbag->name = strdup(buf);
	if (!ratbag->name) {
		errno = ENOMEM;
		goto out_err;
	}

	ratbag_device_init(ratbag, fd);
	rc = ratbag_device_init_udev(libratbag, ratbag, fd);
	if (rc)
		goto out_err;

	driver = ratbag_find_driver(libratbag, ratbag, &ratbag->ids);
	if (!driver) {
		errno = ENOTSUP;
		goto err_udev;
	}

	return ratbag;

err_udev:
	udev_device_unref(ratbag->udev_device);
	udev_device_unref(ratbag->udev_hidraw);
out_err:
	if (ratbag) {
		free(ratbag->name);
		ratbag->libratbag = libratbag_unref(ratbag->libratbag);
	}
	free(ratbag);
	return NULL;
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
	udev_device_unref(ratbag->udev_device);
	udev_device_unref(ratbag->udev_hidraw);

	if (ratbag->hidraw_fd >= 0)
		close(ratbag->hidraw_fd);

	ratbag->libratbag = libratbag_unref(ratbag->libratbag);
	free(ratbag->name);
	free(ratbag);
	return NULL;
}

LIBRATBAG_EXPORT const char *
ratbag_get_name(const struct ratbag* ratbag)
{
	return ratbag->name;
}

static void
ratbag_register_driver(struct libratbag *libratbag, struct ratbag_driver *driver)
{
	list_insert(&libratbag->drivers, &driver->link);
}

LIBRATBAG_EXPORT struct libratbag *
libratbag_create_context(void)
{
	struct libratbag *libratbag;

	libratbag = zalloc(sizeof(*libratbag));
	if (!libratbag)
		return NULL;

	libratbag->refcount = 1;

	list_init(&libratbag->drivers);
	libratbag->udev = udev_new();
	if (!libratbag->udev) {
		free(libratbag);
		return NULL;
	}

	libratbag->log_handler = libratbag_default_log_func;
	libratbag->log_priority = LIBRATBAG_LOG_PRIORITY_DEBUG;

	ratbag_register_driver(libratbag, &etekcity_driver);

	return libratbag;
}

LIBRATBAG_EXPORT struct libratbag *
libratbag_ref(struct libratbag *libratbag)
{
	libratbag->refcount++;
	return libratbag;
}

LIBRATBAG_EXPORT struct libratbag *
libratbag_unref(struct libratbag *libratbag)
{
	if (libratbag == NULL)
		return NULL;

	assert(libratbag->refcount > 0);
	libratbag->refcount--;
	if (libratbag->refcount > 0)
		return libratbag;

	libratbag->udev = udev_unref(libratbag->udev);
	free(libratbag);

	return NULL;
}

static struct ratbag_profile *
ratbag_create_profile(struct ratbag *ratbag, unsigned int index)
{
	struct ratbag_profile *profile;

	profile = zalloc(sizeof(*profile));
	if (!profile)
		return NULL;

	profile->refcount = 1;
	profile->ratbag = ratbag;
	profile->index = index;

	list_insert(&ratbag->profiles, &profile->link);

	assert(ratbag->driver->read_profile);
	ratbag->driver->read_profile(profile, index);

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
ratbag_get_profile_by_index(struct ratbag *ratbag, unsigned int index)
{
	struct ratbag_profile *profile;

	list_for_each(profile, &ratbag->profiles, link) {
		if (profile->index == index) {
			assert(ratbag->driver->read_profile);
			ratbag->driver->read_profile(profile, index);
			return ratbag_profile_ref(profile);
		}
	}

	return ratbag_create_profile(ratbag, index);
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_get_active_profile(struct ratbag *ratbag)
{
	int current_profile;

	assert(ratbag->driver->get_active_profile);
	current_profile = ratbag->driver->get_active_profile(ratbag);
	if (current_profile < 0) {
		errno = -current_profile;
		return NULL;
	}

	return ratbag_get_profile_by_index(ratbag, current_profile);
}
