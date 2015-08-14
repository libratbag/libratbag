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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-util.h"

struct libratbag {
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

void
ratbag_device_init(struct ratbag *rb, int fd)
{
	rb->evdev_fd = fd;
	rb->refcount = 1;
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
ratbag_find_driver(struct libratbag *libratbag, const struct input_id *dev_id)
{
	struct ratbag_driver *driver;
	const struct ratbag_id *matching_id;

	list_for_each(driver, &libratbag->drivers, link) {
		log_debug(libratbag, "testing against %s\n", driver->name);
		matching_id = driver->table_ids;
		do {
			if (ratbag_match_id(dev_id, &matching_id->id)) {
				return driver;
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
	struct input_id ids;
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

	rc = ioctl(fd, EVIOCGID, &ids);
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

	driver = ratbag_find_driver(libratbag, &ids);
	if (!driver) {
		errno = ENOTSUP;
		goto out_err;
	}

	ratbag->driver = driver;

	return ratbag;

out_err:
	if (ratbag)
		free(ratbag->name);
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

	free(libratbag);

	return NULL;
}
