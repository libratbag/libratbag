#pragma once

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
#include <libratbag.h>
#include <libudev.h>
#include <stdbool.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "shared-macro.h"
#include <rbtree/shared-rbtree.h>

#ifndef RATBAG_DBUS_INTERFACE
#define RATBAG_DBUS_INTERFACE	"ratbag1"
#else
#define RATBAG_DEVELOPER_EDITION
#endif

#define RATBAGD_OBJ_ROOT "/org/freedesktop/" RATBAG_DBUS_INTERFACE
#define RATBAGD_NAME_ROOT "org.freedesktop." RATBAG_DBUS_INTERFACE

struct ratbagd;
struct ratbagd_device;
struct ratbagd_profile;
struct ratbagd_resolution;
struct ratbagd_button;
struct ratbagd_led;

void log_info(const char *fmt, ...) _printf_(1, 2);
void log_verbose(const char *fmt, ...) _printf_(1, 2);
void log_error(const char *fmt, ...) _printf_(1, 2);

#define CHECK_CALL(_call) \
	do { \
		int _r = _call; \
		if (_r < 0) { \
			log_error("%s: '%s' failed with: %s\n", __func__, #_call, strerror(-_r)); \
			return _r; \
		} \
	} while (0)


/*
 * Profiles
 */

extern const sd_bus_vtable ratbagd_profile_vtable[];

int ratbagd_profile_new(struct ratbagd_profile **out,
			struct ratbagd_device *device,
			struct ratbag_profile *lib_profile,
			unsigned int index);
struct ratbagd_profile *ratbagd_profile_free(struct ratbagd_profile *profile);
const char *ratbagd_profile_get_path(struct ratbagd_profile *profile);
bool ratbagd_profile_is_default(struct ratbagd_profile *profile);
unsigned int ratbagd_profile_get_index(struct ratbagd_profile *profile);
int ratbagd_profile_register_resolutions(struct sd_bus *bus,
					 struct ratbagd_device *device,
					 struct ratbagd_profile *profile);
int ratbagd_profile_register_buttons(struct sd_bus *bus,
				     struct ratbagd_device *device,
				     struct ratbagd_profile *profile);
int ratbagd_profile_register_leds(struct sd_bus *bus,
				  struct ratbagd_device *device,
				  struct ratbagd_profile *profile);

int ratbagd_for_each_profile_signal(sd_bus *bus,
				    struct ratbagd_device *device,
				    int (*func)(sd_bus *bus,
						struct ratbagd_profile *profile));
int ratbagd_for_each_resolution_signal(sd_bus *bus,
				       struct ratbagd_profile *profile,
				       int (*func)(sd_bus *bus,
						   struct ratbagd_resolution *resolution));
int ratbagd_for_each_button_signal(sd_bus *bus,
				   struct ratbagd_profile *profile,
				   int (*func)(sd_bus *bus,
					       struct ratbagd_button *button));
int ratbagd_for_each_led_signal(sd_bus *bus,
				struct ratbagd_profile *profile,
				int (*func)(sd_bus *bus,
					    struct ratbagd_led *led));
int ratbagd_profile_resync(sd_bus *bus, struct ratbagd_profile *profile);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_profile *, ratbagd_profile_free);

/*
 * Resolutions
 */
extern const sd_bus_vtable ratbagd_resolution_vtable[];

int ratbagd_resolution_new(struct ratbagd_resolution **out,
			   struct ratbagd_device *device,
			   struct ratbagd_profile *profile,
			   struct ratbag_resolution *lib_resolution,
			   unsigned int index);
struct ratbagd_resolution *ratbagd_resolution_free(struct ratbagd_resolution *resolution);
const char *ratbagd_resolution_get_path(struct ratbagd_resolution *resolution);
int ratbagd_resolution_resync(sd_bus *bus, struct ratbagd_resolution *resolution);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_resolution *, ratbagd_resolution_free);

/*
 * Buttons
 */
extern const sd_bus_vtable ratbagd_button_vtable[];

int ratbagd_button_new(struct ratbagd_button **out,
		       struct ratbagd_device *device,
		       struct ratbagd_profile *profile,
		       struct ratbag_button *lib_button,
		       unsigned int index);
struct ratbagd_button *ratbagd_button_free(struct ratbagd_button *button);
const char *ratbagd_button_get_path(struct ratbagd_button *button);
int ratbagd_button_resync(sd_bus *bus, struct ratbagd_button *button);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_button *, ratbagd_button_free);

/*
 * Leds
 */
extern const sd_bus_vtable ratbagd_led_vtable[];

int ratbagd_led_new(struct ratbagd_led **out,
		    struct ratbagd_device *device,
		    struct ratbagd_profile *profile,
		    struct ratbag_led *lib_led,
		    unsigned int index);
struct ratbagd_led *ratbagd_led_free(struct ratbagd_led *led);
const char *ratbagd_led_get_path(struct ratbagd_led *led);
int ratbagd_led_resync(sd_bus *bus, struct ratbagd_led *led);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_led *, ratbagd_led_free);

/*
 * Devices
 */

extern const sd_bus_vtable ratbagd_device_vtable[];

int ratbagd_device_new(struct ratbagd_device **out,
		       struct ratbagd *ctx,
		       const char *sysname,
		       struct ratbag_device *lib_device);
struct ratbagd_device *ratbagd_device_ref(struct ratbagd_device *device);
struct ratbagd_device *ratbagd_device_unref(struct ratbagd_device *device);
const char *ratbagd_device_get_sysname(struct ratbagd_device *device);
const char *ratbagd_device_get_path(struct ratbagd_device *device);
unsigned int ratbagd_device_get_num_buttons(struct ratbagd_device *device);
unsigned int ratbagd_device_get_num_leds(struct ratbagd_device *device);
int ratbagd_device_resync(struct ratbagd_device *device, sd_bus *bus);

bool ratbagd_device_linked(struct ratbagd_device *device);
void ratbagd_device_link(struct ratbagd_device *device);
void ratbagd_device_unlink(struct ratbagd_device *device);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_device *, ratbagd_device_unref);

struct ratbagd_device *ratbagd_device_lookup(struct ratbagd *ctx,
					     const char *name);
struct ratbagd_device *ratbagd_device_first(struct ratbagd *ctx);
struct ratbagd_device *ratbagd_device_next(struct ratbagd_device *device);

#define RATBAGD_DEVICE_FOREACH(_device, _ctx)		\
	for ((_device) = ratbagd_device_first(_ctx);	\
	     (_device);					\
	     (_device) = ratbagd_device_next(_device))

#define RATBAGD_DEVICE_FOREACH_SAFE(_device, _safe, _ctx)	\
	for (_device = ratbagd_device_first(_ctx),		\
	     _safe = (_device) ? ratbagd_device_next(_device) : NULL; \
	     (_device);						\
	     _device = (_safe),				\
	     _safe = (_safe) ? ratbagd_device_next(_safe) : NULL)

/* Verify that _val is not -1. This traps DBus API errors where we end up
 * sending a valid-looking index across and then fail on the other side.
 *
 * do {} while(0) so we can terminate with a ; without the compiler
 * complaining about an empty statement;
 * */
#define verify_unsigned_int(_val) \
	do { if ((int)_val == -1) { \
		log_error("%s:%d - %s: expected unsigned int, got -1\n", __FILE__, __LINE__, __func__); \
		return -EINVAL; \
	} } while(0)

/*
 * Context
 */

struct ratbagd {
	int api_version;

	sd_event *event;
	struct ratbag *lib_ctx;
	struct udev_monitor *monitor;
	sd_event_source *timeout_source;
	sd_event_source *monitor_source;
	sd_bus *bus;

	RBTree device_map;
	size_t n_devices;

	const char **themes; /* NULL-terminated */
};

typedef void (*ratbagd_callback_t)(void *userdata);

void ratbagd_schedule_task(struct ratbagd *ctx,
			   ratbagd_callback_t callback,
			   void *userdata);

int ratbagd_profile_notify_dirty(sd_bus *bus,
				 struct ratbagd_profile *profile);
