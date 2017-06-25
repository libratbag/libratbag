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
#include "shared-rbtree.h"

struct ratbagd;
struct ratbagd_device;
struct ratbagd_profile;
struct ratbagd_resolution;
struct ratbagd_button;
struct ratbagd_led;

void log_verbose(const char *fmt, ...) _printf_(1, 2);
void log_error(const char *fmt, ...) _printf_(1, 2);

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
bool ratbagd_profile_is_active(struct ratbagd_profile *profile);
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
bool ratbagd_resolution_is_active(struct ratbagd_resolution *resolution);
bool ratbagd_resolution_is_default(struct ratbagd_resolution *resolution);

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

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_led *, ratbagd_led_free);

/*
 * Devices
 */

extern const sd_bus_vtable ratbagd_device_vtable[];

int ratbagd_device_new(struct ratbagd_device **out,
		       struct ratbagd *ctx,
		       const char *name,
		       struct ratbag_device *lib_device);
struct ratbagd_device *ratbagd_device_free(struct ratbagd_device *device);
const char *ratbagd_device_get_name(struct ratbagd_device *device);
const char *ratbagd_device_get_path(struct ratbagd_device *device);
unsigned int ratbagd_device_get_num_buttons(struct ratbagd_device *device);
unsigned int ratbagd_device_get_num_leds(struct ratbagd_device *device);

bool ratbagd_device_linked(struct ratbagd_device *device);
void ratbagd_device_link(struct ratbagd_device *device);
void ratbagd_device_unlink(struct ratbagd_device *device);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_device *, ratbagd_device_free);

struct ratbagd_device *ratbagd_device_lookup(struct ratbagd *ctx,
					     const char *name);
struct ratbagd_device *ratbagd_device_first(struct ratbagd *ctx);
struct ratbagd_device *ratbagd_device_next(struct ratbagd_device *device);

#define RATBAGD_DEVICE_FOREACH(_device, _ctx)		\
	for ((_device) = ratbagd_device_first(_ctx);	\
	     (_device);					\
	     (_device) = ratbagd_device_next(_device))

/*
 * Context
 */

struct ratbagd {
	sd_event *event;
	struct ratbag *lib_ctx;
	struct udev_monitor *monitor;
	sd_event_source *monitor_source;
	sd_bus *bus;

	RBTree device_map;
	size_t n_devices;
};
