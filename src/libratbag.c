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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <limits.h>

#include "usb-ids.h"
#include "libratbag-private.h"
#include "libratbag-util.h"
#include "libratbag-data.h"

static enum ratbag_error_code
error_code(enum ratbag_error_code code)
{
	switch(code) {
	case RATBAG_SUCCESS:
	case RATBAG_ERROR_DEVICE:
	case RATBAG_ERROR_CAPABILITY:
	case RATBAG_ERROR_VALUE:
	case RATBAG_ERROR_SYSTEM:
	case RATBAG_ERROR_IMPLEMENTATION:
		break;
	default:
		assert(!"Invalid error code. This is a library bug.");
	}
	return code;
}

static void
ratbag_profile_destroy(struct ratbag_profile *profile);
static void
ratbag_button_destroy(struct ratbag_button *button);
static void
ratbag_led_destroy(struct ratbag_led *led);
static void
ratbag_resolution_destroy(struct ratbag_resolution *resolution);

static void
ratbag_default_log_func(struct ratbag *ratbag,
			enum ratbag_log_priority priority,
			const char *format, va_list args)
{
	const char *prefix;
	FILE *out = stdout;

	switch(priority) {
	case RATBAG_LOG_PRIORITY_RAW:
		prefix = "raw";
		break;
	case RATBAG_LOG_PRIORITY_DEBUG:
		prefix = "debug";
		break;
	case RATBAG_LOG_PRIORITY_INFO:
		prefix = "info";
		break;
	case RATBAG_LOG_PRIORITY_ERROR:
		prefix = "error";
		out = stderr;
		break;
	default:
		prefix="<invalid priority>";
		break;
	}

	fprintf(out, "ratbag %s: ", prefix);
	vfprintf(out, format, args);
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
	const uint8_t *buf, size_t len)
{
	_cleanup_free_ char *output_buf = NULL;
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
		n += snprintf_safe(output_buf, buf_len - n, "%s", header);

	for (i = 0; i < len; ++i) {
		n += snprintf_safe(&output_buf[n], buf_len - n, "%s%02x", sep, buf[i] & 0xFF);
		sep = " ";
	}

	log_msg(ratbag, priority, "%s\n", output_buf);
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
		priority = RATBAG_LOG_PRIORITY_INFO;
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

struct ratbag_device*
ratbag_device_new(struct ratbag *ratbag, struct udev_device *udev_device,
		  const char *name, const struct input_id *id)
{
	struct ratbag_device *device = NULL;

	device = zalloc(sizeof(*device));
	device->name = strdup_safe(name);
	device->ratbag = ratbag_ref(ratbag);
	device->refcount = 1;
	device->udev_device = udev_device_ref(udev_device);
	device->ids = *id;
	device->data = ratbag_device_data_new_for_id(ratbag, id);

	if (device->data != NULL)
		device->devicetype = ratbag_device_data_get_device_type(device->data);

	list_init(&device->profiles);

	list_insert(&ratbag->devices, &device->link);

	return device;
}

void
ratbag_device_destroy(struct ratbag_device *device)
{
	struct ratbag_profile *profile, *next;

	if (!device)
		return;

	/* if we get to the point where the device is destroyed, profiles,
	 * buttons, etc. are at a refcount of 0, so we can destroy
	 * everything */
	if (device->driver && device->driver->remove)
		device->driver->remove(device);

	list_for_each_safe(profile, next, &device->profiles, link)
		ratbag_profile_destroy(profile);

	if (device->udev_device)
		udev_device_unref(device->udev_device);

	list_remove(&device->link);

	ratbag_unref(device->ratbag);
	ratbag_device_data_unref(device->data);
	free(device->name);
	free(device->firmware_version);
	free(device);
}

static inline bool
ratbag_sanity_check_device(struct ratbag_device *device)
{
	struct ratbag *ratbag = device->ratbag;
	struct ratbag_profile *profile = NULL;
	bool has_active = false;
	unsigned int nres;
	bool rc = false;

	/* arbitrary number: max 16 profiles, does any mouse have more? but
	 * since we have num_profiles unsigned, it also checks for
	 * accidental negative */
	if (device->num_profiles == 0 || device->num_profiles > 16) {
		log_bug_libratbag(ratbag,
				  "%s: invalid number of profiles (%d)\n",
				  device->name,
				  device->num_profiles);
		goto out;
	}

	ratbag_device_for_each_profile(device, profile) {
		struct ratbag_resolution *resolution;
		unsigned int vals[300];
		unsigned int nvals = ARRAY_LENGTH(vals);

		/* Allow max 1 active profile */
		if (profile->is_active) {
			if (has_active) {
				log_bug_libratbag(ratbag,
						  "%s: multiple active profiles\n",
						  device->name);
				goto out;
			}
			has_active = true;
		}

		nres = ratbag_profile_get_num_resolutions(profile);
		if (nres > 16) {
				log_bug_libratbag(ratbag,
						  "%s: invalid number of resolutions (%d)\n",
						  device->name,
						  nres);
				goto out;
		}

		ratbag_profile_for_each_resolution(profile, resolution) {
			nvals = ratbag_resolution_get_dpi_list(resolution, vals, nvals);
			if (nvals == 0) {
				log_bug_libratbag(ratbag,
						  "%s: invalid dpi list\n",
						  device->name);
				goto out;
			}

		}

		nvals = ratbag_profile_get_report_rate_list(profile, vals, nvals);
		if (nvals == 0) {
			log_bug_libratbag(ratbag,
					  "%s: invalid report rate list\n",
					  device->name);
			goto out;
		}

		if (profile->dirty) {
			log_bug_libratbag(ratbag,
					  "%s: profile is dirty while probing\n",
					  device->name);
			/* Don't bail yet, as we may have some drivers that do this. */
		}
	}

	/* Require 1 active profile */
	if (!has_active) {
		log_bug_libratbag(ratbag,
				  "%s: no profile set as active profile\n",
				  device->name);
		goto out;
	}

	rc = true;

out:
	return rc;
}

static inline bool
ratbag_try_driver(struct ratbag_device *device,
		   const struct input_id *dev_id,
		   const char *driver_name,
		   const struct ratbag_test_device *test_device)
{
	struct ratbag *ratbag = device->ratbag;
	struct ratbag_driver *driver;
	int rc;

	list_for_each(driver, &ratbag->drivers, link) {
		if (streq(driver->id, driver_name)) {
			device->driver = driver;
			break;
		}
	}

	if (!device->driver) {
		log_error(ratbag, "%s: driver '%s' does not exist\n",
			  device->name, driver_name);
		goto error;
	}

	if (test_device)
		rc = device->driver->test_probe(device, test_device);
	else
		rc = device->driver->probe(device);
	if (rc == 0) {
		if (!ratbag_sanity_check_device(device)) {
			goto error;
		} else {
			log_debug(ratbag,
				  "driver match found: %s\n",
				  device->driver->name);
			return true;
		}
	}

	if (rc != -ENODEV)
		log_error(ratbag, "%s: error opening hidraw node (%s)\n",
			  device->name, strerror(-rc));

	device->driver = NULL;
error:
	return false;
}

bool
ratbag_assign_driver(struct ratbag_device *device,
		     const struct input_id *dev_id,
		     const struct ratbag_test_device *test_device)
{
	const char *driver_name;

	if (!test_device) {
		driver_name = ratbag_device_data_get_driver(device->data);
	} else {
		log_debug(device->ratbag, "This is a test device\n");
		driver_name = "test_driver";
	}

	log_debug(device->ratbag, "device assigned driver %s\n", driver_name);
	return ratbag_try_driver(device, dev_id, driver_name, test_device);
}

static char *
get_device_name(struct udev_device *device)
{
	const char *prop;

	prop = udev_prop_value(device, "HID_NAME");

	return strdup_safe(prop);
}

static inline int
get_product_id(struct udev_device *device, struct input_id *id)
{
	const char *product;
	struct input_id ids  = {0};
	int rc;

	product = udev_prop_value(device, "HID_ID");
	if (!product)
		return -1;

	rc = sscanf(product, "%hx:%hx:%hx", &ids.bustype, &ids.vendor, &ids.product);
	if (rc != 3)
		return -1;

	*id = ids;
	return 0;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_device_new_from_udev_device(struct ratbag *ratbag,
				   struct udev_device *udev_device,
				   struct ratbag_device **device_out)
{
	struct ratbag_device *device = NULL;
	enum ratbag_error_code error = RATBAG_ERROR_DEVICE;
	_cleanup_free_ char *name = NULL;
	struct input_id id;

	assert(ratbag != NULL);
	assert(udev_device != NULL);
	assert(device_out != NULL);

	if (get_product_id(udev_device, &id) != 0)
		goto out_err;

	if ((name = get_device_name(udev_device)) == 0)
		goto out_err;

	log_debug(ratbag, "New device: %s\n", name);

	device = ratbag_device_new(ratbag, udev_device, name, &id);
	if (!device || !device->data)
		goto out_err;

	if (!ratbag_assign_driver(device, &device->ids, NULL))
		goto out_err;

	error = RATBAG_SUCCESS;

out_err:

	if (error != RATBAG_SUCCESS)
		ratbag_device_destroy(device);
	else
		*device_out = device;

	return error_code(error);
}

LIBRATBAG_EXPORT struct ratbag_device *
ratbag_device_ref(struct ratbag_device *device)
{
	assert(device->refcount < INT_MAX);

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
	if (device->refcount == 0)
		ratbag_device_destroy(device);

	return NULL;
}

LIBRATBAG_EXPORT const char *
ratbag_device_get_name(const struct ratbag_device* device)
{
	return device->name;
}

LIBRATBAG_EXPORT enum ratbag_device_type
ratbag_device_get_device_type(const struct ratbag_device *device)
{
	return device->devicetype;
}

LIBRATBAG_EXPORT const char *
ratbag_device_get_bustype(const struct ratbag_device *device)
{
	switch (device->ids.bustype) {
	case BUS_USB:
		return "usb";
	case BUS_BLUETOOTH:
		return "bluetooth";
	default:
		return NULL;
	}
}

LIBRATBAG_EXPORT uint32_t
ratbag_device_get_vendor_id(const struct ratbag_device *device)
{
	return device->ids.vendor;
}

LIBRATBAG_EXPORT uint32_t
ratbag_device_get_product_id(const struct ratbag_device *device)
{
	return device->ids.product;
}

LIBRATBAG_EXPORT uint32_t
ratbag_device_get_product_version(const struct ratbag_device *device)
{
	/* change this when we have a need for it, i.e. when we start supporting devices
	 * where the USB ID gets reused */
	return 0;
}

void
ratbag_register_driver(struct ratbag *ratbag, struct ratbag_driver *driver)
{
	if (!driver->name) {
		log_bug_libratbag(ratbag, "Driver is missing name\n");
		return;
	}

	if (!driver->probe || !driver->remove) {
		log_bug_libratbag(ratbag, "Driver %s is incomplete.\n", driver->name);
		return;
	}
	list_insert(&ratbag->drivers, &driver->link);
}

LIBRATBAG_EXPORT struct ratbag *
ratbag_create_context(const struct ratbag_interface *interface,
		      void *userdata)
{
	struct ratbag *ratbag;

	assert(interface != NULL);
	assert(interface->open_restricted != NULL);
	assert(interface->close_restricted != NULL);

	ratbag = zalloc(sizeof(*ratbag));
	ratbag->refcount = 1;
	ratbag->interface = interface;
	ratbag->userdata = userdata;

	list_init(&ratbag->drivers);
	list_init(&ratbag->devices);
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
	ratbag_register_driver(ratbag, &logitech_g300_driver);
	ratbag_register_driver(ratbag, &logitech_g600_driver);
	ratbag_register_driver(ratbag, &marsgaming_driver);
	ratbag_register_driver(ratbag, &roccat_driver);
	ratbag_register_driver(ratbag, &roccat_kone_pure_driver);
	ratbag_register_driver(ratbag, &roccat_emp_driver);
	ratbag_register_driver(ratbag, &gskill_driver);
	ratbag_register_driver(ratbag, &steelseries_driver);
	ratbag_register_driver(ratbag, &asus_driver);
	ratbag_register_driver(ratbag, &sinowealth_driver);
	ratbag_register_driver(ratbag, &sinowealth_nubwo_driver);
	ratbag_register_driver(ratbag, &openinput_driver);

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
	if (ratbag->refcount == 0) {
		ratbag->udev = udev_unref(ratbag->udev);
		free(ratbag);
	}

	return NULL;
}

static struct ratbag_button *
ratbag_create_button(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_button *button;

	button = zalloc(sizeof(*button));
	button->refcount = 0;
	button->profile = profile;
	button->index = index;

	list_append(&profile->buttons, &button->link);

	return button;
}

static struct ratbag_led *
ratbag_create_led(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_led *led;

	led = zalloc(sizeof(*led));
	led->refcount = 0;
	led->profile = profile;
	led->index = index;
	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;

	list_append(&profile->leds, &led->link);

	return led;
}

LIBRATBAG_EXPORT bool
ratbag_profile_has_capability(const struct ratbag_profile *profile,
			      enum ratbag_profile_capability cap)
{
	if (cap == RATBAG_PROFILE_CAP_NONE || cap >= MAX_CAP)
		abort();

	return long_bit_is_set(profile->capabilities, cap);
}

static inline void
ratbag_create_resolution(struct ratbag_profile *profile, int index)
{
	struct ratbag_resolution *res;

	res = zalloc(sizeof(*res));
	res->refcount = 0;
	res->profile = profile;
	res->index = index;

	list_append(&profile->resolutions, &res->link);

	profile->num_resolutions++;
}


static struct ratbag_profile *
ratbag_create_profile(struct ratbag_device *device,
		      unsigned int index,
		      unsigned int num_resolutions,
		      unsigned int num_buttons,
		      unsigned int num_leds)
{
	struct ratbag_profile *profile;
	unsigned i;

	profile = zalloc(sizeof(*profile));
	profile->refcount = 0;
	profile->device = device;
	profile->index = index;
	profile->num_resolutions = 0;
	profile->is_enabled = true;
	profile->name = NULL;
	profile->angle_snapping = -1;
	profile->debounce = -1;

	list_append(&device->profiles, &profile->link);
	list_init(&profile->buttons);
	list_init(&profile->leds);
	list_init(&profile->resolutions);

	profile->device->num_buttons = num_buttons;
	profile->device->num_leds = num_leds;

	for (i = 0; i < num_resolutions; i++)
		ratbag_create_resolution(profile, i);

	for (i = 0; i < num_buttons; i++)
		ratbag_create_button(profile, i);

	for (i = 0; i < num_leds; i++)
		ratbag_create_led(profile, i);

	return profile;
}

int
ratbag_device_init_profiles(struct ratbag_device *device,
			    unsigned int num_profiles,
			    unsigned int num_resolutions,
			    unsigned int num_buttons,
			    unsigned int num_leds)
{
	unsigned int i;

	for (i = 0; i < num_profiles; i++) {
		ratbag_create_profile(device, i, num_resolutions, num_buttons, num_leds);
	}

	device->num_profiles = num_profiles;

	return 0;
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_profile_ref(struct ratbag_profile *profile)
{
	assert(profile->refcount < INT_MAX);

	ratbag_device_ref(profile->device);
	profile->refcount++;
	return profile;
}

static void
ratbag_profile_destroy(struct ratbag_profile *profile)
{
	struct ratbag_button *button, *b_next;
	struct ratbag_led *led, *l_next;
	struct ratbag_resolution *res, *r_next;

	/* if we get to the point where the profile is destroyed, buttons,
	 * resolutions , etc. are at a refcount of 0, so we can destroy
	 * everything */
	list_for_each_safe(button, b_next, &profile->buttons, link)
		ratbag_button_destroy(button);

	list_for_each_safe(led, l_next, &profile->leds, link)
		ratbag_led_destroy(led);

	list_for_each_safe(res, r_next, &profile->resolutions, link)
		ratbag_resolution_destroy(res);

	free(profile->name);

	list_remove(&profile->link);
	free(profile);
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_profile_unref(struct ratbag_profile *profile)
{
	if (profile == NULL)
		return NULL;

	assert(profile->refcount > 0);
	profile->refcount--;

	ratbag_device_unref(profile->device);

	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_profile *
ratbag_device_get_profile(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_profile *profile;

	if (index >= ratbag_device_get_num_profiles(device)) {
		log_bug_client(device->ratbag, "Requested invalid profile %d\n", index);
		return NULL;
	}

	list_for_each(profile, &device->profiles, link) {
		if (profile->index == index)
			return ratbag_profile_ref(profile);
	}

	log_bug_libratbag(device->ratbag, "Profile %d not found\n", index);

	return NULL;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_profile_set_enabled(struct ratbag_profile *profile, bool enabled)
{
	if (!ratbag_profile_has_capability(profile, RATBAG_PROFILE_CAP_DISABLE))
		return RATBAG_ERROR_CAPABILITY;

	if (profile->is_active && !enabled)
		return RATBAG_ERROR_VALUE;

	profile->is_enabled = enabled;
	profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT bool
ratbag_profile_is_active(const struct ratbag_profile *profile)
{
	return !!profile->is_active;
}

LIBRATBAG_EXPORT bool
ratbag_profile_is_dirty(const struct ratbag_profile *profile)
{
	return !!profile->dirty;
}

LIBRATBAG_EXPORT bool
ratbag_profile_is_enabled(const struct ratbag_profile *profile)
{
	return !!profile->is_enabled;
}

LIBRATBAG_EXPORT unsigned int
ratbag_device_get_num_profiles(const struct ratbag_device *device)
{
	return device->num_profiles;
}

LIBRATBAG_EXPORT unsigned int
ratbag_device_get_num_buttons(const struct ratbag_device *device)
{
	return device->num_buttons;
}

LIBRATBAG_EXPORT unsigned int
ratbag_device_get_num_leds(const struct ratbag_device *device)
{
	return device->num_leds;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_device_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;
	int rc;

	if (device->driver->commit == NULL) {
		log_error(device->ratbag,
			  "Trying to commit with a driver that doesn't support committing\n");
		return RATBAG_ERROR_CAPABILITY;
	}

	rc = device->driver->commit(device);
	if (rc)
		return RATBAG_ERROR_DEVICE;

	list_for_each(profile, &device->profiles, link) {
		profile->dirty = false;

		profile->angle_snapping_dirty = false;
		profile->debounce_dirty = false;
		profile->rate_dirty = false;

		list_for_each(button, &profile->buttons, link)
			button->dirty = false;

		list_for_each(led, &profile->leds, link)
			led->dirty = false;

		list_for_each(resolution, &profile->resolutions, link)
			resolution->dirty = false;

		/* TODO: think if this should be moved into `driver-commit`. */
		if (profile->is_active_dirty && profile->is_active) {
			if (device->driver->set_active_profile == NULL)
				return RATBAG_ERROR_IMPLEMENTATION;

			rc = device->driver->set_active_profile(device, profile->index);
			if (rc)
				return RATBAG_ERROR_DEVICE;
		}
		profile->is_active_dirty = false;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_profile_set_active(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_profile *p;

	if (!profile->is_enabled)
		return RATBAG_ERROR_VALUE;

	if (device->num_profiles == 1)
		return RATBAG_SUCCESS;

	list_for_each(p, &device->profiles, link) {
		if (p->is_active) {
			p->is_active = false;
			p->is_active_dirty = true;
			p->dirty = true;
		}
	}

	profile->is_active = true;
	profile->is_active_dirty = true;
	profile->dirty = true;
	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT unsigned int
ratbag_profile_get_num_resolutions(const struct ratbag_profile *profile)
{
	return profile->num_resolutions;
}

LIBRATBAG_EXPORT struct ratbag_resolution *
ratbag_profile_get_resolution(struct ratbag_profile *profile, unsigned int idx)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_resolution *res;
	unsigned max = ratbag_profile_get_num_resolutions(profile);

	if (idx >= max) {
		log_bug_client(profile->device->ratbag,
			       "Requested invalid resolution %d\n", idx);
		return NULL;
	}

	ratbag_profile_for_each_resolution(profile, res) {
		if (res->index == idx)
			return ratbag_resolution_ref(res);
	}

	log_bug_libratbag(device->ratbag, "Resolution %d, profile %d not found\n",
			  idx, profile->index);

	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_resolution *
ratbag_resolution_ref(struct ratbag_resolution *resolution)
{
	assert(resolution->refcount < INT_MAX);

	ratbag_profile_ref(resolution->profile);
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

	ratbag_profile_unref(resolution->profile);

	return NULL;
}

LIBRATBAG_EXPORT bool
ratbag_resolution_has_capability(const struct ratbag_resolution *resolution,
				 enum ratbag_resolution_capability cap)
{
	assert(cap <= RATBAG_RESOLUTION_CAP_DISABLE);

	return !!(resolution->capabilities & (1 << cap));
}

static inline bool
resolution_has_dpi(const struct ratbag_resolution *resolution,
		   unsigned int dpi)
{
	for (size_t i = 0; i < resolution->ndpis; i++) {
		if (dpi == resolution->dpis[i])
			return true;
	}

	return false;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_resolution_set_dpi(struct ratbag_resolution *resolution,
			  unsigned int dpi)
{
	struct ratbag_profile *profile = resolution->profile;

	if (!resolution_has_dpi(resolution, dpi))
		return RATBAG_ERROR_VALUE;

	if (resolution->dpi_x != dpi || resolution->dpi_y != dpi) {
		resolution->dpi_x = dpi;
		resolution->dpi_y = dpi;
		resolution->dirty = true;
		profile->dirty = true;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_resolution_set_dpi_xy(struct ratbag_resolution *resolution,
			     unsigned int x, unsigned int y)
{
	struct ratbag_profile *profile = resolution->profile;

	if (!ratbag_resolution_has_capability(resolution,
					      RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION))
		return RATBAG_ERROR_CAPABILITY;

	if ((x == 0 && y != 0) || (x != 0 && y == 0))
		return RATBAG_ERROR_VALUE;

	if (!resolution_has_dpi(resolution, x) || !resolution_has_dpi(resolution, y))
		return RATBAG_ERROR_VALUE;

	if (resolution->dpi_x != x || resolution->dpi_y != y) {
		resolution->dpi_x = x;
		resolution->dpi_y = y;
		resolution->dirty = true;
		profile->dirty = true;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_profile_set_report_rate(struct ratbag_profile *profile,
			       unsigned int hz)
{
	if (profile->hz != hz) {
		profile->hz = hz;
		profile->dirty = true;
		profile->rate_dirty = true;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_profile_set_angle_snapping(struct ratbag_profile *profile,
				  int value)
{
	if (profile->angle_snapping != value) {
		profile->angle_snapping = value;
		profile->dirty = true;
		profile->angle_snapping_dirty = true;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_profile_set_debounce(struct ratbag_profile *profile,
			    int value)
{
	if (profile->debounce != value) {
		profile->debounce = value;
		profile->dirty = true;
		profile->debounce_dirty = true;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_dpi(const struct ratbag_resolution *resolution)
{
	return resolution->dpi_x;
}

LIBRATBAG_EXPORT size_t
ratbag_resolution_get_dpi_list(const struct ratbag_resolution *resolution,
			       unsigned int *resolutions,
			       size_t nres)
{
	_Static_assert(sizeof(*resolutions) == sizeof(*resolution->dpis), "type mismatch");

	assert(nres > 0);

	if (resolution->ndpis == 0)
		return 0;

	memcpy(resolutions, resolution->dpis,
	       sizeof(unsigned int) * min(nres, resolution->ndpis));

	return resolution->ndpis;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_dpi_x(const struct ratbag_resolution *resolution)
{
	return resolution->dpi_x;
}

LIBRATBAG_EXPORT int
ratbag_resolution_get_dpi_y(const struct ratbag_resolution *resolution)
{
	return resolution->dpi_y;
}

LIBRATBAG_EXPORT int
ratbag_profile_get_report_rate(const struct ratbag_profile *profile)
{
	return profile->hz;
}

LIBRATBAG_EXPORT int
ratbag_profile_get_angle_snapping(const struct ratbag_profile *profile)
{
	return profile->angle_snapping;
}

LIBRATBAG_EXPORT int
ratbag_profile_get_debounce(const struct ratbag_profile *profile)
{
	return profile->debounce;
}

LIBRATBAG_EXPORT size_t
ratbag_profile_get_debounce_list(const struct ratbag_profile *profile,
				 unsigned int *debounces,
				 size_t ndebounces)
{
	_Static_assert(sizeof(*debounces) == sizeof(*profile->debounces), "type mismatch");

	assert(ndebounces > 0);

	if (profile->ndebounces == 0)
		return 0;

	memcpy(debounces, profile->debounces,
	       sizeof(unsigned int) * min(ndebounces, profile->ndebounces));

	return profile->ndebounces;
}

LIBRATBAG_EXPORT size_t
ratbag_profile_get_report_rate_list(const struct ratbag_profile *profile,
				    unsigned int *rates,
				    size_t nrates)
{
	_Static_assert(sizeof(*rates) == sizeof(*profile->rates), "type mismatch");

	assert(nrates > 0);

	if (profile->nrates == 0)
		return 0;

	memcpy(rates, profile->rates,
	       sizeof(unsigned int) * min(nrates, profile->nrates));

	return profile->nrates;
}

LIBRATBAG_EXPORT bool
ratbag_resolution_is_active(const struct ratbag_resolution *resolution)
{
	return !!resolution->is_active;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_resolution_set_active(struct ratbag_resolution *resolution)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_resolution *res;

	if (resolution->is_disabled) {
		log_error(profile->device->ratbag, "%s: setting the active resolution to a disabled resolution is not allowed\n", profile->device->name);
		return RATBAG_ERROR_VALUE;
	}

	ratbag_profile_for_each_resolution(profile, res)
		res->is_active = false;

	resolution->is_active = true;
	resolution->dirty = true;
	profile->dirty = true;
	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT bool
ratbag_resolution_is_default(const struct ratbag_resolution *resolution)
{
	return !!resolution->is_default;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_resolution_set_default(struct ratbag_resolution *resolution)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_resolution *other;

	if (resolution->is_disabled) {
		log_error(profile->device->ratbag, "%s: setting the default resolution to a disabled resolution is not allowed\n", profile->device->name);
		return RATBAG_ERROR_VALUE;
	}

	/* Unset the default on the other resolutions */
	ratbag_profile_for_each_resolution(profile, other) {
		if (other == resolution || !other->is_default)
			continue;

		other->is_default = false;
		resolution->dirty = true;
		profile->dirty = true;
	}

	if (!resolution->is_default) {
		resolution->is_default = true;
		resolution->dirty = true;
		profile->dirty = true;
	}

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT bool
ratbag_resolution_is_disabled(const struct ratbag_resolution *resolution)
{
	return !!resolution->is_disabled;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_resolution_set_disabled(struct ratbag_resolution *resolution, bool disable)
{
	struct ratbag_profile *profile = resolution->profile;

	if (!ratbag_resolution_has_capability(resolution, RATBAG_RESOLUTION_CAP_DISABLE))
		return RATBAG_ERROR_CAPABILITY;

	if (disable) {
		if (resolution->is_active) {
			log_error(profile->device->ratbag, "%s: disabling the active resolution is not allowed\n", profile->device->name);
			return RATBAG_ERROR_VALUE;
		}
		if (resolution->is_default) {
			log_error(profile->device->ratbag, "%s: disabling the default resolution is not allowed\n", profile->device->name);
			return RATBAG_ERROR_VALUE;
		}
	}

	resolution->is_disabled = disable;
	resolution->dirty = true;
	profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT struct ratbag_button*
ratbag_profile_get_button(struct ratbag_profile *profile,
				   unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_button *button;

	if (index >= ratbag_device_get_num_buttons(device)) {
		log_bug_client(device->ratbag, "Requested invalid button %d\n", index);
		return NULL;
	}

	list_for_each(button, &profile->buttons, link) {
		if (button->index == index)
			return ratbag_button_ref(button);
	}

	log_bug_libratbag(device->ratbag, "Button %d, profile %d not found\n",
			  index, profile->index);

	return NULL;
}

LIBRATBAG_EXPORT enum ratbag_button_action_type
ratbag_button_get_action_type(const struct ratbag_button *button)
{
	return button->action.type;
}

LIBRATBAG_EXPORT bool
ratbag_button_has_action_type(const struct ratbag_button *button,
			      enum ratbag_button_action_type action_type)
{
	switch (action_type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
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
ratbag_button_get_button(const struct ratbag_button *button)
{
	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_BUTTON)
		return 0;

	return button->action.action.button;
}

void
ratbag_button_set_action(struct ratbag_button *button,
			 const struct ratbag_button_action *action)
{
	struct ratbag_macro *macro = button->action.macro;

	button->action = *action;
	if (action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		button->action.macro = macro;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_button_set_button(struct ratbag_button *button, unsigned int btn)
{
	struct ratbag_button_action action = {0};

	if (!ratbag_button_has_action_type(button,
					   RATBAG_BUTTON_ACTION_TYPE_BUTTON))
		return RATBAG_ERROR_CAPABILITY;

	action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
	action.action.button = btn;

	ratbag_button_set_action(button, &action);
	button->dirty = true;
	button->profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_button_action_special
ratbag_button_get_special(const struct ratbag_button *button)
{
	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_SPECIAL)
		return RATBAG_BUTTON_ACTION_SPECIAL_INVALID;

	return button->action.action.special;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_button_set_special(struct ratbag_button *button,
			  enum ratbag_button_action_special act)
{
	struct ratbag_button_action action = {0};

	/* FIXME: range checks */

	if (!ratbag_button_has_action_type(button,
					   RATBAG_BUTTON_ACTION_TYPE_SPECIAL))
		return RATBAG_ERROR_CAPABILITY;

	action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
	action.action.special = act;

	ratbag_button_set_action(button, &action);
	button->dirty = true;
	button->profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT unsigned int
ratbag_button_get_key(const struct ratbag_button *button)
{
	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_KEY)
		return 0;

	return button->action.action.key;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_button_set_key(struct ratbag_button *button, unsigned int key)
{
	struct ratbag_button_action action = {0};

	/* FIXME: range checks */

	if (!ratbag_button_has_action_type(button,
					   RATBAG_BUTTON_ACTION_TYPE_KEY))
		return RATBAG_ERROR_CAPABILITY;

	action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
	action.action.key = key;

	ratbag_button_set_action(button, &action);
	button->dirty = true;
	button->profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_button_disable(struct ratbag_button *button)
{
	if (!ratbag_button_has_action_type(button,
					   RATBAG_BUTTON_ACTION_TYPE_NONE))
		return RATBAG_ERROR_CAPABILITY;

	struct ratbag_button_action action = {0};

	action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;

	ratbag_button_set_action(button, &action);
	button->dirty = true;
	button->profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT struct ratbag_button *
ratbag_button_ref(struct ratbag_button *button)
{
	assert(button->refcount < INT_MAX);

	ratbag_profile_ref(button->profile);
	button->refcount++;
	return button;
}

LIBRATBAG_EXPORT struct ratbag_led *
ratbag_led_ref(struct ratbag_led *led)
{
	assert(led->refcount < INT_MAX);

	ratbag_profile_ref(led->profile);
	led->refcount++;
	return led;
}

static void
ratbag_button_destroy(struct ratbag_button *button)
{
	list_remove(&button->link);
	if (button->action.macro) {
		free(button->action.macro->name);
		free(button->action.macro->group);
		free(button->action.macro);
	}
	free(button);
}

static void
ratbag_led_destroy(struct ratbag_led *led)
{
	list_remove(&led->link);
	free(led);
}

static void
ratbag_resolution_destroy(struct ratbag_resolution *res)
{
	list_remove(&res->link);
	free(res);
}

LIBRATBAG_EXPORT struct ratbag_button *
ratbag_button_unref(struct ratbag_button *button)
{
	if (button == NULL)
		return NULL;

	assert(button->refcount > 0);
	button->refcount--;

	ratbag_profile_unref(button->profile);

	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_led *
ratbag_led_unref(struct ratbag_led *led)
{
	if (led == NULL)
		return NULL;

	assert(led->refcount > 0);
	led->refcount--;

	ratbag_profile_unref(led->profile);

	return NULL;
}

LIBRATBAG_EXPORT enum ratbag_led_mode
ratbag_led_get_mode(const struct ratbag_led *led)
{
	return led->mode;
}

LIBRATBAG_EXPORT bool
ratbag_led_has_mode(const struct ratbag_led *led,
		    enum ratbag_led_mode mode)
{
	assert(mode <= RATBAG_LED_BREATHING);

	if (mode == RATBAG_LED_OFF)
		return 1;

	return !!(led->modes & (1 << mode));
}

LIBRATBAG_EXPORT struct ratbag_color
ratbag_led_get_color(const struct ratbag_led *led)
{
	return led->color;
}

LIBRATBAG_EXPORT int
ratbag_led_get_effect_duration(const struct ratbag_led *led)
{
	return led->ms;
}

LIBRATBAG_EXPORT unsigned int
ratbag_led_get_brightness(const struct ratbag_led *led)
{
	return led->brightness;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_led_set_mode(struct ratbag_led *led, enum ratbag_led_mode mode)
{
	led->mode = mode;
	led->dirty = true;
	led->profile->dirty = true;
	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_led_set_color(struct ratbag_led *led, struct ratbag_color color)
{
	led->color = color;
	led->dirty = true;
	led->profile->dirty = true;
	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_led_colordepth
ratbag_led_get_colordepth(const struct ratbag_led *led)
{
	return led->colordepth;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_led_set_effect_duration(struct ratbag_led *led, unsigned int ms)
{
	led->ms = ms;
	led->dirty = true;
	led->profile->dirty = true;
	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_led_set_brightness(struct ratbag_led *led, unsigned int brightness)
{
	led->brightness = brightness;
	led->dirty = true;
	led->profile->dirty = true;
	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT struct ratbag_led *
ratbag_profile_get_led(struct ratbag_profile *profile,
		       unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_led *led;

	if (index >= ratbag_device_get_num_leds(device)) {
		log_bug_client(device->ratbag, "Requested invalid led %d\n", index);
		return NULL;
	}

	list_for_each(led, &profile->leds, link) {
		if (led->index == index)
			return ratbag_led_ref(led);
	}

	log_bug_libratbag(device->ratbag, "Led %d, profile %d not found\n",
			  index, profile->index);

	return NULL;
}

LIBRATBAG_EXPORT const char *
ratbag_profile_get_name(const struct ratbag_profile *profile)
{
	return profile->name;
}

LIBRATBAG_EXPORT int
ratbag_profile_set_name(struct ratbag_profile *profile,
			const char *name)
{
	char *name_copy;

	if (!profile->name)
		return RATBAG_ERROR_CAPABILITY;

	name_copy = strdup_safe(name);
	if (profile->name)
		free(profile->name);

	profile->name = name_copy;
	profile->dirty = true;

	return 0;
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

LIBRATBAG_EXPORT const char*
ratbag_device_get_firmware_version(const struct ratbag_device *ratbag_device)
{
	return ratbag_device->firmware_version;
}

LIBRATBAG_EXPORT void
ratbag_device_set_firmware_version(struct ratbag_device *device, const char* fw)
{
	free(device->firmware_version);
	device->firmware_version = strdup_safe(fw);
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

LIBRATBAG_EXPORT struct ratbag_button_macro *
ratbag_button_get_macro(struct ratbag_button *button)
{
	struct ratbag_button_macro *macro;

	if (button->action.type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return NULL;

	macro = ratbag_button_macro_new(button->action.macro->name);
	memcpy(macro->macro.events,
	       button->action.macro->events,
	       sizeof(macro->macro.events));

	return macro;
}

void
ratbag_button_copy_macro(struct ratbag_button *button,
			 const struct ratbag_button_macro *macro)
{
	if (!button->action.macro)
		button->action.macro = zalloc(sizeof(struct ratbag_macro));
	else {
		free(button->action.macro->name);
		free(button->action.macro->group);
		memset(button->action.macro, 0, sizeof(struct ratbag_macro));
	}

	button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
	memcpy(button->action.macro->events,
	       macro->macro.events,
	       sizeof(macro->macro.events));
	button->action.macro->name = strdup_safe(macro->macro.name);
	button->action.macro->group = strdup_safe(macro->macro.group);
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_button_set_macro(struct ratbag_button *button,
			const struct ratbag_button_macro *macro)
{
	if (!ratbag_button_has_action_type(button,
					   RATBAG_BUTTON_ACTION_TYPE_MACRO))
		return RATBAG_ERROR_CAPABILITY;

	ratbag_button_copy_macro(button, macro);
	button->dirty = true;
	button->profile->dirty = true;

	return RATBAG_SUCCESS;
}

LIBRATBAG_EXPORT enum ratbag_error_code
ratbag_button_macro_set_event(struct ratbag_button_macro *m,
			      unsigned int index,
			      enum ratbag_macro_event_type type,
			      unsigned int data)
{
	struct ratbag_macro *macro = &m->macro;

	if (index >= MAX_MACRO_EVENTS)
		return RATBAG_ERROR_VALUE;

	switch (type) {
	case RATBAG_MACRO_EVENT_KEY_PRESSED:
	case RATBAG_MACRO_EVENT_KEY_RELEASED:
		macro->events[index].type = type;
		macro->events[index].event.key = data;
		break;
	case RATBAG_MACRO_EVENT_WAIT:
		macro->events[index].type = type;
		macro->events[index].event.timeout = data;
		break;
	case RATBAG_MACRO_EVENT_NONE:
		macro->events[index].type = type;
		break;
	default:
		return RATBAG_ERROR_VALUE;
	}

	return 0;
}

LIBRATBAG_EXPORT enum ratbag_macro_event_type
ratbag_button_macro_get_event_type(const struct ratbag_button_macro *macro,
				   unsigned int index)
{
	if (index >= MAX_MACRO_EVENTS)
		return RATBAG_MACRO_EVENT_INVALID;

	return macro->macro.events[index].type;
}

LIBRATBAG_EXPORT int
ratbag_button_macro_get_event_key(const struct ratbag_button_macro *m,
				  unsigned int index)
{
	const struct ratbag_macro *macro = &m->macro;

	if (index >= MAX_MACRO_EVENTS)
		return 0;

	if (macro->events[index].type != RATBAG_MACRO_EVENT_KEY_PRESSED &&
	    macro->events[index].type != RATBAG_MACRO_EVENT_KEY_RELEASED)
		return -EINVAL;

	return macro->events[index].event.key;
}

LIBRATBAG_EXPORT int
ratbag_button_macro_get_event_timeout(const struct ratbag_button_macro *m,
				      unsigned int index)
{
	const struct ratbag_macro *macro = &m->macro;

	if (index >= MAX_MACRO_EVENTS)
		return 0;

	if (macro->events[index].type != RATBAG_MACRO_EVENT_WAIT)
		return 0;

	return macro->events[index].event.timeout;
}

LIBRATBAG_EXPORT unsigned int
ratbag_button_macro_get_num_events(const struct ratbag_button_macro *macro)
{
	return MAX_MACRO_EVENTS;
}

LIBRATBAG_EXPORT const char *
ratbag_button_macro_get_name(const struct ratbag_button_macro *macro)
{
	return macro->macro.name;
}

static void
ratbag_button_macro_destroy(struct ratbag_button_macro *macro)
{
	assert(macro->refcount == 0);
	free(macro->macro.name);
	free(macro->macro.group);
	free(macro);
}

LIBRATBAG_EXPORT struct ratbag_button_macro *
ratbag_button_macro_ref(struct ratbag_button_macro *macro)
{
	assert(macro->refcount < INT_MAX);

	macro->refcount++;
	return macro;
}

LIBRATBAG_EXPORT struct ratbag_button_macro *
ratbag_button_macro_unref(struct ratbag_button_macro *macro)
{
	if (macro == NULL)
		return NULL;

	assert(macro->refcount > 0);
	macro->refcount--;
	if (macro->refcount == 0)
		ratbag_button_macro_destroy(macro);

	return NULL;
}

LIBRATBAG_EXPORT struct ratbag_button_macro *
ratbag_button_macro_new(const char *name)
{
	struct ratbag_button_macro *macro;

	macro = zalloc(sizeof *macro);
	macro->refcount = 1;
	macro->macro.name = strdup_safe(name);

	return macro;
}

struct ratbag_modifier_mapping {
	unsigned int modifier_mask;
	unsigned int key;
};

struct ratbag_modifier_mapping ratbag_modifier_mapping[] = {
	{ MODIFIER_LEFTCTRL, KEY_LEFTCTRL },
	{ MODIFIER_LEFTSHIFT, KEY_LEFTSHIFT },
	{ MODIFIER_LEFTALT, KEY_LEFTALT },
	{ MODIFIER_LEFTMETA, KEY_LEFTMETA },
	{ MODIFIER_RIGHTCTRL, KEY_RIGHTCTRL },
	{ MODIFIER_RIGHTSHIFT, KEY_RIGHTSHIFT },
	{ MODIFIER_RIGHTALT, KEY_RIGHTALT },
	{ MODIFIER_RIGHTMETA, KEY_RIGHTMETA },
};

int
ratbag_button_macro_new_from_keycode(struct ratbag_button *button,
				     unsigned int key,
				     unsigned int modifiers)
{
	struct ratbag_button_macro *macro;
	struct ratbag_modifier_mapping *mapping;
	int i;

	macro = ratbag_button_macro_new("key");
	i = 0;

	ARRAY_FOR_EACH(ratbag_modifier_mapping, mapping) {
		if (modifiers & mapping->modifier_mask)
			ratbag_button_macro_set_event(macro,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_PRESSED,
						      mapping->key);
	}

	ratbag_button_macro_set_event(macro,
				      i++,
				      RATBAG_MACRO_EVENT_KEY_PRESSED,
				      key);
	ratbag_button_macro_set_event(macro,
				      i++,
				      RATBAG_MACRO_EVENT_KEY_RELEASED,
				      key);

	ARRAY_FOR_EACH(ratbag_modifier_mapping, mapping) {
		if (modifiers & mapping->modifier_mask)
			ratbag_button_macro_set_event(macro,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_RELEASED,
						      mapping->key);
	}

	ratbag_button_copy_macro(button, macro);
	ratbag_button_macro_unref(macro);

	return 0;
}

int
ratbag_action_macro_num_keys(const struct ratbag_button_action *action)
{
	const struct ratbag_macro *macro = action->macro;
	int count = 0;
	for (int i = 0; i < MAX_MACRO_EVENTS; i++) {
		struct ratbag_macro_event event = macro->events[i];
		if (event.type == RATBAG_MACRO_EVENT_NONE ||
		    event.type == RATBAG_MACRO_EVENT_INVALID) {
			break;
		}
		if (ratbag_key_is_modifier(event.event.key)) {
			continue;
		}
		if (event.type == RATBAG_MACRO_EVENT_KEY_PRESSED) {
			count += 1;
		}
	}
	return count;
}

int
ratbag_action_keycode_from_macro(const struct ratbag_button_action *action,
				 unsigned int *key_out,
				 unsigned int *modifiers_out)
{
	const struct ratbag_macro *macro = action->macro;
	unsigned int key = KEY_RESERVED;
	unsigned int modifiers = 0;
	unsigned int i;

	if (!macro || action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return -EINVAL;

	if (macro->events[0].type == RATBAG_MACRO_EVENT_NONE)
		return -EINVAL;

	if (ratbag_action_macro_num_keys(action) != 1)
		return -EINVAL;

	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		struct ratbag_macro_event event;

		event = macro->events[i];
		switch (event.type) {
		case RATBAG_MACRO_EVENT_INVALID:
			return -EINVAL;
		case RATBAG_MACRO_EVENT_NONE:
			return 0;
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			switch(event.event.key) {
			case KEY_LEFTCTRL: modifiers |= MODIFIER_LEFTCTRL; break;
			case KEY_LEFTSHIFT: modifiers |= MODIFIER_LEFTSHIFT; break;
			case KEY_LEFTALT: modifiers |= MODIFIER_LEFTALT; break;
			case KEY_LEFTMETA: modifiers |= MODIFIER_LEFTMETA; break;
			case KEY_RIGHTCTRL: modifiers |= MODIFIER_RIGHTCTRL; break;
			case KEY_RIGHTSHIFT: modifiers |= MODIFIER_RIGHTSHIFT; break;
			case KEY_RIGHTALT: modifiers |= MODIFIER_RIGHTALT; break;
			case KEY_RIGHTMETA: modifiers |= MODIFIER_RIGHTMETA; break;
			default:
				if (key != KEY_RESERVED)
					return -EINVAL;

				key = event.event.key;
			}
			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			switch(event.event.key) {
			case KEY_LEFTCTRL: modifiers &= ~MODIFIER_LEFTCTRL; break;
			case KEY_LEFTSHIFT: modifiers &= ~MODIFIER_LEFTSHIFT; break;
			case KEY_LEFTALT: modifiers &= ~MODIFIER_LEFTALT; break;
			case KEY_LEFTMETA: modifiers &= ~MODIFIER_LEFTMETA; break;
			case KEY_RIGHTCTRL: modifiers &= ~MODIFIER_RIGHTCTRL; break;
			case KEY_RIGHTSHIFT: modifiers &= ~MODIFIER_RIGHTSHIFT; break;
			case KEY_RIGHTALT: modifiers &= ~MODIFIER_RIGHTALT; break;
			case KEY_RIGHTMETA: modifiers &= ~MODIFIER_RIGHTMETA; break;
			default:
				if (event.event.key != key)
					return -EINVAL;

				*key_out = key;
				*modifiers_out = modifiers;
				return 1;
			}
		case RATBAG_MACRO_EVENT_WAIT:
			break;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}
