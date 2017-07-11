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

#pragma once

#include <linux/input.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "libratbag.h"
#include "libratbag-util.h"
#include "libratbag-hidraw.h"

static inline void
cleanup_device(struct ratbag_device **d)
{
	ratbag_device_unref(*d);
}

static inline void
cleanup_profile(struct ratbag_profile **p)
{
	ratbag_profile_unref(*p);
}

static inline void
cleanup_resolution(struct ratbag_resolution **r)
{
	ratbag_resolution_unref(*r);
}

#define _cleanup_device_ _cleanup_(cleanup_device)
#define _cleanup_profile_ _cleanup_(cleanup_profile)
#define _cleanup_resolution_ _cleanup_(cleanup_resolution)

#define BUS_ANY					0xffff
#define VENDOR_ANY				0xffff
#define PRODUCT_ANY				0xffff
#define VERSION_ANY				0xffff

/* This struct is used by the test suite only */
struct ratbag_test_device;

struct ratbag_driver;
struct ratbag_button_action;

struct ratbag {
	const struct ratbag_interface *interface;
	void *userdata;

	struct udev *udev;
	struct list drivers;
	struct list devices;

	int refcount;
	ratbag_log_handler log_handler;
	enum ratbag_log_priority log_priority;
};

#define MAX_CAP 1000

struct ratbag_device {
	char *name;
	void *userdata;

	struct udev_device *udev_device;
	struct ratbag_hidraw hidraw;
	int refcount;
	struct input_id ids;
	struct ratbag_driver *driver;
	struct ratbag *ratbag;
	unsigned long capabilities[NLONGS(MAX_CAP)];

	unsigned num_profiles;
	struct list profiles;

	unsigned num_buttons;
	unsigned num_leds;

	void *drv_data;

	struct list link;
};

/**
 * struct ratbag_driver - user space driver for a ratbag device
 */
struct ratbag_driver {
	/** A human-readable name of the driver */
	char *name;

	/** The id of the driver used to match with RATBAG_DRIVER in udev */
	char *id;

	/**
	 * Callback called while trying to open a device by libratbag.
	 * This function should decide whether or not this driver will
	 * handle the given device.
	 *
	 * Return -ENODEV to ignore the device and let other drivers
	 * probe the device. Any other error code will stop the probing.
	 */
	int (*probe)(struct ratbag_device *device);

	/**
	 * Callback called right before the struct ratbag_device is
	 * unref-ed.
	 *
	 * In this callback, the extra memory allocated in probe should
	 * be freed.
	 */
	void (*remove)(struct ratbag_device *device);

	/**
	 * Callback called when the driver should write any profiles that
	 * were modified back to the device.
	 *
	 * Both profile and button structs have a dirty variable that can
	 * be used to tell whether or not they've actually changed since
	 * the last commit. In order to reduce the amount of time
	 * committing takes, drivers should use this information to avoid
	 * writing back profiles and buttons that haven't actually changed.
	 */
	int (*commit)(struct ratbag_device *device);

	/**
	 * Callback called when a read profile is requested by the
	 * caller of the library.
	 *
	 * The driver should probe here the device for the requested
	 * profile and populate the related information.
	 * There is no need to populate the various struct ratbag_button
	 * as they are allocated when the user needs it.
	 */
	void (*read_profile)(struct ratbag_profile *profile, unsigned int index);

	/*
	 * FIXME: This function is deprecated and should not be removed. Once
	 * we've updated all the device drivers to stop using it we'll remove
	 * it. Look at commit() instead.
	 */
	int (*write_profile)(struct ratbag_profile *profile);

	/**
	 * Called to mark a previously writen profile as active.
	 *
	 * There should be no need to write the profile here, a
	 * .write_profile() call is issued before calling this.
	 */
	int (*set_active_profile)(struct ratbag_device *device, unsigned int index);

	/**
	 * For the given button, fill in the struct ratbag_button
	 * with the available information.
	 *
	 * For devices with profiles, profile will not be NULL. For
	 * device without, profile will be set to NULL.
	 *
	 * For devices with profile, there should not be the need to
	 * actually read the profile from the device. The caller
	 * should make sure that the profile is up to date.
	 */
	void (*read_button)(struct ratbag_button *button);

	/*
	 * FIXME: This function is deprecated and should not be removed. Once
	 * we've updated all the device drivers to stop using it we'll remove
	 * it. Look at commit() instead.
	 */
	int (*write_button)(struct ratbag_button *button,
			    const struct ratbag_button_action *action);

	/*
	 * FIXME: This function is deprecated and should not be removed. Once
	 * we've updated all the device drivers to stop using it we'll remove
	 * it. Look at commit() instead.
	 */
	int (*write_resolution_dpi)(struct ratbag_resolution *resolution,
				    int dpi_x, int dpi_y);

	/**
	 * For the given led, fill in the struct ratbag_led
	 * with the available information.
	 */
	void (*read_led)(struct ratbag_led *led);

	/*
	 * FIXME: This function is deprecated and should be removed. Once
	 * we've updated all the device drivers to stop using it we'll remove
	 * it. Look at commit() instead.
	 */
	int (*write_led)(struct ratbag_led *led, enum ratbag_led_mode mode,
			 struct ratbag_color color, unsigned int hz,
			 unsigned int brightness);

	/* private */
	int (*test_probe)(struct ratbag_device *device, void *data);

	struct list link;
};

struct ratbag_resolution {
	struct ratbag_profile *profile;
	int refcount;
	void *userdata;
	unsigned int dpi_max;	/**< max resolution in dpi */
	unsigned int dpi_min;	/**< min resolution in dpi */
	unsigned int dpi_x;	/**< x resolution in dpi */
	unsigned int dpi_y;	/**< y resolution in dpi */
	unsigned int hz;	/**< report rate in Hz */
	bool is_active;
	bool is_default;
	uint32_t capabilities;
};

struct ratbag_led {
	int refcount;
	void *userdata;
	struct list link;
	struct ratbag_profile *profile;
	unsigned index;
	enum ratbag_led_type type;
	enum ratbag_led_mode mode;
	struct ratbag_color color;
	unsigned int hz;              /**< rate of action in hz */
	unsigned int brightness;      /**< brightness of the LED */
	bool dirty;
};

struct ratbag_profile {
	int refcount;
	void *userdata;

	struct list link;
	unsigned index;
	struct ratbag_device *device;
	struct list buttons;
	void *drv_data;
	void *user_data;
	struct {
		struct ratbag_resolution *modes;
		unsigned int num_modes;
	} resolution;
	struct list leds;

	bool is_active;		/**< profile is the currently active one */
	bool is_enabled;
	bool dirty;       /**< profile changed since last commit */
};

#define BUTTON_ACTION_NONE \
 { .type = RATBAG_BUTTON_ACTION_TYPE_NONE }
#define BUTTON_ACTION_BUTTON(num_) \
 { .type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, \
	.action.button = num_ }
#define BUTTON_ACTION_SPECIAL(sp_) \
 { .type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL, \
	.action.special = sp_ }
#define BUTTON_ACTION_KEY(k_) \
 { .type = RATBAG_BUTTON_ACTION_TYPE_KEY, \
	.action.key.key = k_ }
#define BUTTON_ACTION_MACRO \
 { .type = RATBAG_BUTTON_ACTION_TYPE_MACRO, \
	/* FIXME: add the macro keys */ }

struct ratbag_macro_event {
	enum ratbag_macro_event_type type;
	union ratbag_macro_evnt {
		unsigned int key;
		unsigned int timeout;
	} event;
};

#define MAX_MACRO_EVENTS 256
struct ratbag_macro {
	char *name;
	char *group;
	struct ratbag_macro_event events[MAX_MACRO_EVENTS];
};

struct ratbag_button_macro {
	int refcount;
	struct ratbag_macro macro;
};

struct ratbag_button_action {
	enum ratbag_button_action_type type;
	union ratbag_btn_action {
		unsigned int button; /* action_type == button */
		enum ratbag_button_action_special special; /* action_type == special */
		struct {
			unsigned int key; /* action_type == key */
			/* FIXME: modifiers */
		} key;
	} action;
	struct ratbag_macro *macro; /* dynamically allocated, so kept aside */
};

struct ratbag_button {
	int refcount;
	void *userdata;
	struct list link;
	struct ratbag_profile *profile;
	unsigned index;
	enum ratbag_button_type type;
	struct ratbag_button_action action;
	uint32_t action_caps;
	bool dirty; /* changed since last commit to device */
};

static inline void
ratbag_button_enable_action_type(struct ratbag_button *button,
				 enum ratbag_button_action_type type)
{
	button->action_caps |= 1 << type;
}

static inline int
ratbag_open_path(struct ratbag_device *device, const char *path, int flags)
{
	struct ratbag *ratbag = device->ratbag;

	return ratbag->interface->open_restricted(path, flags, ratbag->userdata);
}

static inline void
ratbag_close_fd(struct ratbag_device *device, int fd)
{
	struct ratbag *ratbag = device->ratbag;

	return ratbag->interface->close_restricted(fd, ratbag->userdata);
}

static inline void
ratbag_set_drv_data(struct ratbag_device *device, void *drv_data)
{
	device->drv_data = drv_data;
}

static inline void *
ratbag_get_drv_data(struct ratbag_device *device)
{
	return device->drv_data;
}

int
ratbag_device_init_profiles(struct ratbag_device *device,
			    unsigned int num_profiles,
			    unsigned int num_resolutions,
			    unsigned int num_buttons,
			    unsigned int num_leds);

void
ratbag_device_set_capability(struct ratbag_device *device,
			     enum ratbag_device_capability cap);

static inline void
ratbag_profile_set_drv_data(struct ratbag_profile *profile, void *drv_data)
{
	profile->drv_data = drv_data;
}

static inline void *
ratbag_profile_get_drv_data(struct ratbag_profile *profile)
{
	return profile->drv_data;
}

static inline int
ratbag_button_action_match(const struct ratbag_button_action *action,
			   const struct ratbag_button_action *match)
{
	if (action->type != match->type)
		return 0;

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		return match->action.button == action->action.button;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		return match->action.key.key == action->action.key.key;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		return match->action.special == action->action.special;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		return 1;
	default:
		break;
	}

	return 0;
}

static inline struct ratbag_resolution *
ratbag_resolution_init(struct ratbag_profile *profile, int index,
		       int dpi_x, int dpi_y, int hz)
{
	struct ratbag_resolution *res = &profile->resolution.modes[index];

	res->profile = profile;
	res->dpi_x = dpi_x;
	res->dpi_y = dpi_y;
	res->hz = hz;
	res->is_active = false;
	res->is_default = false;
	res->capabilities = 0;
	res->dpi_min = 0;
	res->dpi_max = 0;

	return res;
}

static inline void
ratbag_resolution_set_range(struct ratbag_resolution *res, int min, int max)
{
	res->dpi_min = min;
	res->dpi_max = max;
}

static inline void
ratbag_resolution_set_cap(struct ratbag_resolution *res,
			  enum ratbag_resolution_capability cap)
{
	assert(cap <= RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

	res->capabilities = (1 << cap);
}

void
log_msg_va(struct ratbag *ratbag,
	   enum ratbag_log_priority priority,
	   const char *format,
	   va_list args)
	LIBRATBAG_ATTRIBUTE_PRINTF(3, 0);
void log_msg(struct ratbag *ratbag,
	enum ratbag_log_priority priority,
	const char *format, ...)
	LIBRATBAG_ATTRIBUTE_PRINTF(3, 4);
void
log_buffer(struct ratbag *ratbag,
	enum ratbag_log_priority priority,
	const char *header,
	uint8_t *buf, size_t len);

#define log_raw(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_RAW, __VA_ARGS__)
#define log_debug(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define log_info(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_INFO, __VA_ARGS__)
#define log_error(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define log_bug_kernel(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define log_bug_libratbag(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, "libratbag bug: " __VA_ARGS__)
#define log_bug_client(li_, ...) log_msg((li_), RATBAG_LOG_PRIORITY_ERROR, "client bug: " __VA_ARGS__)
#define log_buf_raw(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_RAW, h_, buf_, len_)
#define log_buf_debug(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_DEBUG, h_, buf_, len_)
#define log_buf_info(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_INFO, h_, buf_, len_)
#define log_buf_error(li_, h_, buf_, len_) log_buffer(li_, RATBAG_LOG_PRIORITY_ERROR, h_, buf_, len_)

/* list of all supported drivers */
struct ratbag_driver etekcity_driver;
struct ratbag_driver hidpp20_driver;
struct ratbag_driver hidpp10_driver;
struct ratbag_driver logitech_g300_driver;
struct ratbag_driver roccat_driver;
struct ratbag_driver gskill_driver;

struct ratbag_device*
ratbag_device_new(struct ratbag *ratbag, struct udev_device *udev_device,
		  const char *name, const struct input_id *id);
void
ratbag_device_destroy(struct ratbag_device *device);

const char *
ratbag_device_get_udev_property(const struct ratbag_device* device,
				const char *name);

bool
ratbag_assign_driver(struct ratbag_device *device,
		     const struct input_id *dev_id,
		     struct ratbag_test_device *test_device);

void
ratbag_register_driver(struct ratbag *ratbag, struct ratbag_driver *driver);

void
ratbag_button_copy_macro(struct ratbag_button *button,
			 const struct ratbag_button_macro *macro);

