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

#ifndef LIBRATBAG_PRIVATE_H
#define LIBRATBAG_PRIVATE_H

#include <linux/input.h>

#include "libratbag.h"
#include "libratbag-util.h"

#define USB_VENDOR_ID_ETEKCITY			0x1ea7
#define USB_DEVICE_ID_ETEKCITY_SCROLL_ALPHA	0x4011

#define BUS_ANY					0xffff
#define VENDOR_ANY				0xffff
#define PRODUCT_ANY				0xffff
#define VERSION_ANY				0xffff

struct ratbag_driver;

struct ratbag {
	char *name;
	struct udev_device *udev_device;
	struct udev_device *udev_hidraw;
	int evdev_fd;
	int hidraw_fd;
	int refcount;
	struct input_id ids;
	struct ratbag_driver *driver;
	struct libratbag *libratbag;

	unsigned num_profiles;
	struct list profiles;

	unsigned num_buttons;
};

struct ratbag_id {
	struct input_id id;
	unsigned long data;
};

struct ratbag_driver {
	char *name;
	const struct ratbag_id *table_ids;

	int (*probe)(struct ratbag *ratbag, const struct ratbag_id id);
	void (*read_profile)(struct ratbag_profile *profile, unsigned int index);
	int (*write_profile)(struct ratbag_profile *profile);
	int (*get_active_profile)(struct ratbag *ratbag);
	int (*set_active_profile)(struct ratbag *ratbag, unsigned int index);
	int (*has_capability)(const struct ratbag *ratbag, enum ratbag_capability cap);

	/* private */
	struct list link;
};

struct ratbag_profile {
	int refcount;
	struct list link;
	unsigned index;
	struct ratbag *ratbag;
};

void ratbag_device_init(struct ratbag *rb, int fd);

void
log_msg_va(struct libratbag *libratbag,
	   enum libratbag_log_priority priority,
	   const char *format,
	   va_list args);
void log_msg(struct libratbag *libratbag,
	enum libratbag_log_priority priority,
	const char *format, ...);

#define log_debug(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug_kernel(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define log_bug_libratbag(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_ERROR, "libratbag bug: " __VA_ARGS__)
#define log_bug_client(li_, ...) log_msg((li_), LIBRATBAG_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)

/* list of all supported drivers */
struct ratbag_driver etekcity_driver;

#endif /* LIBRATBAG_PRIVATE_H */

