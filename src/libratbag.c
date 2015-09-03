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
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "libratbag-private.h"
#include "libratbag-util.h"

static void
ratbag_default_log_func(struct ratbag *ratbag,
			enum ratbag_log_priority priority,
			const char *format, va_list args)
{
	const char *prefix;

	switch(priority) {
	case RATBAG_LOG_PRIORITY_RAW: prefix = "raw"; break;
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

void
log_buffer(struct ratbag *ratbag,
	enum ratbag_log_priority priority,
	const char *header,
	uint8_t *buf, size_t len)
{
	char *output_buf;
	char *sep = "";
	unsigned int i, n;
	unsigned int buf_len;

	if (ratbag->log_handler &&
	    ratbag->log_priority > priority)
		return;

	buf_len = header ? strlen(header) : 0;
	buf_len += len * 3;
	buf_len += 1; /* terminating '\0' */

	output_buf = zalloc(buf_len);
	n = 0;
	if (header)
		n += sprintf(output_buf, "%s", header);

	for (i = 0; i < len; ++i) {
		n += sprintf(&output_buf[n], "%s%02x", sep, buf[i] & 0xFF);
		sep = " ";
	}

	log_msg(ratbag, priority, "%s\n", output_buf);

	free(output_buf);
}

LIBRATBAG_EXPORT void
ratbag_log_set_priority(struct ratbag *ratbag,
			enum ratbag_log_priority priority)
{
	switch (priority) {
	case RATBAG_LOG_PRIORITY_RAW:
	case RATBAG_LOG_PRIORITY_DEBUG:
	case RATBAG_LOG_PRIORITY_INFO:
	case RATBAG_LOG_PRIORITY_ERROR:
		break;
	default:
		log_bug_client(ratbag,
			       "Invalid log priority %d. Using INFO instead\n",
			       priority);
		return;
	}
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
udev_device_from_devnode(struct ratbag *ratbag, int fd)
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
udev_find_hidraw(struct ratbag_device *device)
{
	struct ratbag *ratbag = device->ratbag;
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	struct udev_device *udev_device;
	const char *path, *sysname;
	struct udev_device *hid_udev;
	struct udev_device *hidraw_udev = NULL;
	struct udev *udev = ratbag->udev;

	hid_udev = udev_device_get_parent_with_subsystem_devtype(device->udev_device, "hid", NULL);

	if (!hid_udev)
		return NULL;

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
		if (!strneq("hidraw", sysname, 6)) {
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
ratbag_device_init_hidraw(struct ratbag_device *device,
			struct udev_device *udev_device)
{
	struct udev_device *hidraw_udev;
	int rc = -ENODEV;

	hidraw_udev = udev_find_hidraw(device);
	if (!hidraw_udev)
		goto out;

	device->udev_hidraw = udev_device_ref(hidraw_udev);

	log_debug(device->ratbag,
		  "%s is device '%s'.\n",
		  device->name,
		  udev_device_get_devnode(device->udev_hidraw));

	rc = 0;
	udev_device_unref(hidraw_udev);
out:
	return rc;
}

static struct ratbag_device*
ratbag_device_new(struct ratbag *ratbag, struct udev_device *udev_device,
		  const char *name, const struct input_id *id)
{
	struct ratbag_device *device = NULL;

	device = zalloc(sizeof(*device));
	device->name = strdup(name);

	if (!name) {
		free(device);
		return NULL;
	}

	device->ratbag = ratbag_ref(ratbag);
	device->hidraw_fd = -1;
	device->refcount = 1;
	device->udev_device = udev_device_ref(udev_device);

	device->ids = *id;
	list_init(&device->profiles);


	return device;
}

static void
ratbag_device_destroy(struct ratbag_device *device)
{
	struct ratbag_profile *profile, *next;

	if (!device)
		return;

	if (device->driver && device->driver->remove)
		device->driver->remove(device);

	/* the profiles are created during probe(), we should unref them */
	list_for_each_safe(profile, next, &device->profiles, link)
		ratbag_profile_unref(profile);

	if (device->udev_device)
		udev_device_unref(device->udev_device);
	if (device->udev_hidraw)
		udev_device_unref(device->udev_hidraw);

	if (device->hidraw_fd >= 0)
		close(device->hidraw_fd);

	ratbag_unref(device->ratbag);
	free(device->name);
	free(device);
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
ratbag_find_driver(struct ratbag_device *device, const struct input_id *dev_id)
{
	struct ratbag *ratbag = device->ratbag;
	struct ratbag_driver *driver;
	const struct ratbag_id *matching_id;
	struct ratbag_id matched_id;
	int rc;

	list_for_each(driver, &ratbag->drivers, link) {
		log_debug(ratbag, "trying driver '%s'\n", driver->name);
		matching_id = driver->table_ids;
		do {
			if (ratbag_match_id(dev_id, &matching_id->id)) {
				matched_id.id = *dev_id;
				matched_id.data = matching_id->data;
				matched_id.svg_filename = matching_id->svg_filename;
				device->driver = driver;
				rc = driver->probe(device, matched_id);
				if (rc == 0) {
					log_debug(ratbag, "driver match found\n");
					device->svg_name = matching_id->svg_filename;
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
	char *name = NULL;
	struct input_id id;

	if (!ratbag) {
		fprintf(stderr, "ratbag is NULL\n");
		return NULL;
	}


	if (get_product_id(udev_device, &id) != 0)
		goto out_err;

	name = get_device_name(udev_device);
	if (!name) {
		errno = ENOMEM;
		goto out_err;
	}

	device = ratbag_device_new(ratbag, udev_device, name, &id);
	free(name);

	if (!device)
		goto out_err;

	rc = ratbag_device_init_hidraw(device, udev_device);
	if (rc)
		goto out_err;

	driver = ratbag_find_driver(device, &device->ids);
	if (!driver) {
		errno = ENOTSUP;
		goto out_err;
	}

	return device;

out_err:
	ratbag_device_destroy(device);

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

	ratbag_device_destroy(device);

	return NULL;
}

LIBRATBAG_EXPORT const char *
ratbag_device_get_name(const struct ratbag_device* device)
{
	return device->name;
}

LIBRATBAG_EXPORT const char *
ratbag_device_get_svg_name(const struct ratbag_device* device)
{
	return device->svg_name;
}

static void
ratbag_register_driver(struct ratbag *ratbag, struct ratbag_driver *driver)
{
	if (!driver->name) {
		log_bug_libratbag(ratbag, "Driver is missing name\n");
		return;
	}

	if (!driver->probe || !driver->remove || !driver->table_ids) {
		log_bug_libratbag(ratbag, "Driver %s is incomplete.\n");
		return;
	}
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
	ratbag->log_priority = RATBAG_LOG_PRIORITY_INFO;

	ratbag_register_driver(ratbag, &etekcity_driver);
	ratbag_register_driver(ratbag, &hidpp20_driver);
	ratbag_register_driver(ratbag, &hidpp10_driver);

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

static struct ratbag_button *
ratbag_create_button(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_button *button;

	button = zalloc(sizeof(*button));
	button->refcount = 1;
	button->profile = profile;
	button->index = index;

	list_insert(&profile->buttons, &button->link);

	if (device->driver->read_button)
		device->driver->read_button(button);

	return button;
}

static int
ratbag_profile_init_buttons(struct ratbag_profile *profile, unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		ratbag_create_button(profile, i);

	profile->device->num_buttons = count;

	return 0;
}

static struct ratbag_profile *
ratbag_create_profile(struct ratbag_device *device,
		      unsigned int index,
		      unsigned int num_buttons)
{
	struct ratbag_profile *profile;
	unsigned i;

	profile = zalloc(sizeof(*profile));
	profile->refcount = 1;
	profile->device = device;
	profile->index = index;

	list_insert(&device->profiles, &profile->link);
	list_init(&profile->buttons);

	for (i = 0; i < MAX_RESOLUTIONS; i++)
		ratbag_resolution_init(profile, i, 0, 0, 0);
	profile->resolution.num_modes = 1;

	assert(device->driver->read_profile);
	device->driver->read_profile(profile, index);

	ratbag_profile_init_buttons(profile, num_buttons);

	return profile;
}

int
ratbag_device_init_profiles(struct ratbag_device *device,
			    unsigned int num_profiles,
			    unsigned int num_buttons)
{
	unsigned int i;

	for (i = 0; i < num_profiles; i++) {
		ratbag_create_profile(device, i, num_buttons);
	}

	device->num_profiles = num_profiles;

	return 0;
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
	struct ratbag_button *button, *next;

	if (profile == NULL)
		return NULL;

	assert(profile->refcount > 0);
	profile->refcount--;
	if (profile->refcount > 0)
		return profile;

	/* the buttons are created by the profile, so we clean them up */
	list_for_each_safe(button, next, &profile->buttons, link)
		ratbag_button_unref(button);

	list_remove(&profile->link);
	free(profile);

	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_device_get_profile_by_index(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_profile *profile;

	if (index >= ratbag_device_get_num_profiles(device))
		return NULL;

	list_for_each(profile, &device->profiles, link) {
		if (profile->index == index)
			return ratbag_profile_ref(profile);
	}

	log_bug_libratbag(device->ratbag, "Profile %d not found\n", index);

	return NULL;
}

LIBRATBAG_EXPORT int
ratbag_profile_is_active(struct ratbag_profile *profile)
{
	return profile->is_active;

	/* FIXME: should be read on startup so we can just do the above */
#if 0
	int current_profile;

	assert(device->driver->get_active_profile);
	current_profile = device->driver->get_active_profile(device);
	if (current_profile < 0) {
		errno = -current_profile;
		return NULL;
	}

	return ratbag_device_get_profile_by_index(device, current_profile);
#endif
}

LIBRATBAG_EXPORT int
ratbag_profile_is_default(struct ratbag_profile *profile)
{
	/* FIXME: unclear if any device actually supports this function */

	return profile->is_default;
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
			     enum ratbag_device_capability cap)
{
	assert(device->driver->has_capability);
	return device->driver->has_capability(device, cap);
}

LIBRATBAG_EXPORT int
ratbag_profile_set_active(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_profile *p;
	int rc;

	assert(device->driver->write_profile);
	rc = device->driver->write_profile(profile);
	if (rc)
		return rc;

	if (ratbag_device_has_capability(device, RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE)) {
		assert(device->driver->set_active_profile);
		rc = device->driver->set_active_profile(device, profile->index);
	}

	if (rc)
		return rc;

	list_for_each(p, &device->profiles, link) {
		p->is_active = false;
	}
	profile->is_active = true;
	return rc;
}

LIBRATBAG_EXPORT int
ratbag_profile_set_default(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_profile *p;
	int rc;

	/* FIXME: unclear if any device actually supports this function */

	assert(device->driver->write_profile);
	rc = device->driver->write_profile(profile);
	if (rc)
		return rc;

	if (ratbag_device_has_capability(device, RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE)) {
		assert(device->driver->set_active_profile);
		rc = device->driver->set_default_profile(device, profile->index);
	}

	if (rc)
		return rc;

	list_for_each(p, &device->profiles, link) {
		p->is_default = false;
	}
	profile->is_default = true;
	return rc;
}

LIBRATBAG_EXPORT int
ratbag_profile_get_num_resolutions(struct ratbag_profile *profile)
{
	return profile->resolution.num_modes;
}

LIBRATBAG_EXPORT struct ratbag_resolution *
ratbag_profile_get_resolution(struct ratbag_profile *profile, unsigned int idx)
{
	struct ratbag_resolution *res;
	int max = ratbag_profile_get_num_resolutions(profile);

	if (max < 0 || idx >= (unsigned int)max)
		return NULL;

	res = &profile->resolution.modes[idx];

	return ratbag_resolution_ref(res);
}

LIBRATBAG_EXPORT struct ratbag_resolution *
ratbag_resolution_ref(struct ratbag_resolution *resolution)
{
	resolution->refcount++;
	return resolution;
}

LIBRATBAG_EXPORT struct ratbag_resolution *
ratbag_resolution_unref(struct ratbag_resolution *resolution)
{
	if (resolution == NULL)
		return NULL;

	assert(resolution->refcount > 0);
	resolution->refcount--;
	if (resolution->refcount > 0)
		return resolution;

	/* Resolution is a fixed list of structs, no freeing required */

	return NULL;
}

LIBRATBAG_EXPORT int
ratbag_resolution_has_capability(struct ratbag_resolution *resolution,
				 enum ratbag_resolution_capability cap)
{
	switch (cap) {
	case RATBAG_RESOLUTION_CAP_INDIVIDUAL_REPORT_RATE:
	case RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION:
		break;
	default:
		return 0;
	}

	return !!(resolution->capabilities & (1 << cap));
}

LIBRATBAG_EXPORT int
ratbag_resolution_set_dpi(struct ratbag_resolution *resolution,
			  unsigned int dpi)
{
	struct ratbag_profile *profile = resolution->profile;
	resolution->dpi_x = dpi;
	resolution->dpi_y = dpi;

	assert(profile->device->driver->write_resolution_dpi);
	return profile->device->driver->write_resolution_dpi(resolution, dpi, dpi);
}

LIBRATBAG_EXPORT int
ratbag_resolution_set_dpi_xy(struct ratbag_resolution *resolution,
			     unsigned int x, unsigned int y)
{
	struct ratbag_profile *profile = resolution->profile;

	if (!ratbag_resolution_has_capability(resolution,
					      RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION))
		return -1;

	if ((x == 0 && y != 0) || (x != 0 && y == 0))
		return -1;

	assert(profile->device->driver->write_resolution_dpi);
	return profile->device->driver->write_resolution_dpi(resolution, x, y);
}

LIBRATBAG_EXPORT int
ratbag_resolution_set_report_rate(struct ratbag_resolution *resolution,
				  unsigned int hz)
{
	resolution->hz = hz;
	/* FIXME: call into the driver */
	return 0;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_dpi(struct ratbag_resolution *resolution)
{
	return resolution->dpi_x;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_dpi_x(struct ratbag_resolution *resolution)
{
	return resolution->dpi_x;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_dpi_y(struct ratbag_resolution *resolution)
{
	return resolution->dpi_y;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_report_rate(struct ratbag_resolution *resolution)
{
	return resolution->hz;
}

LIBRATBAG_EXPORT int
ratbag_resolution_is_active(const struct ratbag_resolution *resolution)
{
	return resolution->is_active;
}

LIBRATBAG_EXPORT int
ratbag_resolution_set_active(struct ratbag_resolution *resolution)
{
	resolution->is_active = true;
	/* FIXME: call into the driver */
	return 0;
}


LIBRATBAG_EXPORT int
ratbag_resolution_is_default(const struct ratbag_resolution *resolution)
{
	return resolution->is_default;
}

LIBRATBAG_EXPORT int
ratbag_resolution_set_default(struct ratbag_resolution *resolution)
{
	resolution->is_default = true;
	/* FIXME: call into the driver */
	return 0;
}

LIBRATBAG_EXPORT struct ratbag_button*
ratbag_profile_get_button_by_index(struct ratbag_profile *profile,
				   unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_button *button;

	if (index >= ratbag_device_get_num_buttons(device))
		return NULL;

	list_for_each(button, &profile->buttons, link) {
		if (button->index == index)
			return ratbag_button_ref(button);
	}

	log_bug_libratbag(device->ratbag, "Button %d, profile %d not found\n",
			  index, profile->index);

	return NULL;
}

LIBRATBAG_EXPORT enum ratbag_button_type
ratbag_button_get_type(struct ratbag_button *button)
{
	return button->type;
}

LIBRATBAG_EXPORT enum ratbag_button_action_type
ratbag_button_get_action_type(struct ratbag_button *button)
{
	return button->action.type;
}

LIBRATBAG_EXPORT int
ratbag_button_has_action_type(struct ratbag_button *button,
			      enum ratbag_button_action_type action_type)
{
	switch (action_type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		break;
	default:
		return 0;
	}

	return !!(button->action_caps & (1 << action_type));
}

LIBRATBAG_EXPORT unsigned int
ratbag_button_get_button(struct ratbag_button *button)
{
	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_BUTTON)
		return 0;

	return button->action.action.button;
}

LIBRATBAG_EXPORT int
ratbag_button_set_button(struct ratbag_button *button, unsigned int btn)
{
	struct ratbag_button_action action;
	int rc;

	if (!button->profile->device->driver->write_button)
		return -ENOTSUP;

	action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
	action.action.button = btn;

	rc = button->profile->device->driver->write_button(button, &action);

	return rc;
}

LIBRATBAG_EXPORT enum ratbag_button_action_special
ratbag_button_get_special(struct ratbag_button *button)
{
	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_SPECIAL)
		return 0;

	return button->action.action.button;
}

LIBRATBAG_EXPORT int
ratbag_button_set_special(struct ratbag_button *button,
			  enum ratbag_button_action_special act)
{
	struct ratbag_button_action action;
	int rc;

	/* FIXME: range checks */

	if (!button->profile->device->driver->write_button)
		return -1;

	action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
	action.action.special = act;

	rc = button->profile->device->driver->write_button(button, &action);

	return rc;
}

LIBRATBAG_EXPORT unsigned int
ratbag_button_get_key(struct ratbag_button *button,
		      unsigned int *modifiers,
		      size_t *sz)
{
	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_KEY)
		return 0;

	/* FIXME: modifiers */
	*sz = 0;
	return button->action.action.key.key;
}

LIBRATBAG_EXPORT int
ratbag_button_set_key(struct ratbag_button *button,
		      unsigned int key,
		      unsigned int *modifiers,
		      size_t sz)
{
	struct ratbag_button_action action;
	int rc;

	/* FIXME: range checks */

	if (!button->profile->device->driver->write_button)
		return -1;

	/* FIXME: modifiers */

	action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
	action.action.key.key = key;
	rc = button->profile->device->driver->write_button(button, &action);

	return rc;
}

LIBRATBAG_EXPORT int
ratbag_button_disable(struct ratbag_button *button)
{
	struct ratbag_button_action action;
	int rc;

	if (!button->profile->device->driver->write_button)
		return -1;

	action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
	rc = button->profile->device->driver->write_button(button, &action);

	return rc;
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

LIBRATBAG_EXPORT void
ratbag_set_user_data(struct ratbag *ratbag, void *userdata)
{
	ratbag->userdata = userdata;
}

LIBRATBAG_EXPORT void
ratbag_device_set_user_data(struct ratbag_device *ratbag_device, void *userdata)
{
	ratbag_device->userdata = userdata;
}

LIBRATBAG_EXPORT void
ratbag_profile_set_user_data(struct ratbag_profile *ratbag_profile, void *userdata)
{
	ratbag_profile->userdata = userdata;
}

LIBRATBAG_EXPORT void
ratbag_button_set_user_data(struct ratbag_button *ratbag_button, void *userdata)
{
	ratbag_button->userdata = userdata;
}

LIBRATBAG_EXPORT void
ratbag_resolution_set_user_data(struct ratbag_resolution *ratbag_resolution, void *userdata)
{
	ratbag_resolution->userdata = userdata;
}

LIBRATBAG_EXPORT void*
ratbag_get_user_data(const struct ratbag *ratbag)
{
	return ratbag->userdata;
}

LIBRATBAG_EXPORT void*
ratbag_device_get_user_data(const struct ratbag_device *ratbag_device)
{
	return ratbag_device->userdata;
}

LIBRATBAG_EXPORT void*
ratbag_profile_get_user_data(const struct ratbag_profile *ratbag_profile)
{
	return ratbag_profile->userdata;
}

LIBRATBAG_EXPORT void*
ratbag_button_get_user_data(const struct ratbag_button *ratbag_button)
{
	return ratbag_button->userdata;
}

LIBRATBAG_EXPORT void*
ratbag_resolution_get_user_data(const struct ratbag_resolution *ratbag_resolution)
{
	return ratbag_resolution->userdata;
}
