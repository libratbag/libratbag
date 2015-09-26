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

/*
 * Devices
 */

extern const sd_bus_vtable ratbagd_device_vtable[];

int ratbagd_device_new(struct ratbagd_device **out,
		       struct ratbagd *ctx,
		       const char *name,
		       struct ratbag_device *lib_device);
struct ratbagd_device *ratbagd_device_free(struct ratbagd_device *device);
bool ratbagd_device_linked(struct ratbagd_device *device);
void ratbagd_device_link(struct ratbagd_device *device);
void ratbagd_device_unlink(struct ratbagd_device *device);

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd_device *, ratbagd_device_free);

struct ratbagd_device *ratbagd_device_lookup(struct ratbagd *ctx,
					     const char *name);
int ratbagd_device_list(struct ratbagd *ctx, char ***paths);

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
