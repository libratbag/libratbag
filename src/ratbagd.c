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
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "ratbagd.h"
#include "shared-macro.h"

static bool verbose = false;

void log_verbose(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);

	if (verbose)
		vprintf(fmt, args);
	va_end(args);
}

void log_error(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	fprintf(stderr, "%s error: ", program_invocation_short_name);
	vfprintf(stderr, fmt, args);
	va_end(args);
}

static int ratbagd_find_device(sd_bus *bus,
			       const char *path,
			       const char *interface,
			       void *userdata,
			       void **found,
			       sd_bus_error *error)
{
	_cleanup_(freep) char *name = NULL;
	struct ratbagd *ctx = userdata;
	struct ratbagd_device *device;
	int r;

	r = sd_bus_path_decode_many(path,
				    "/org/freedesktop/ratbag1/device/%",
				    &name);
	if (r <= 0)
		return r;

	device = ratbagd_device_lookup(ctx, name);
	if (!device)
		return 0;

	*found = device;
	return 1;
}

static int ratbagd_list_devices(sd_bus *bus,
				const char *path,
				void *userdata,
				char ***paths,
				sd_bus_error *error)
{
	struct ratbagd *ctx = userdata;
	struct ratbagd_device *device;
	char **devices, **pos;

	devices = calloc(ctx->n_devices + 1, sizeof(char *));
	if (!devices)
		return -ENOMEM;

	pos = devices;

	RATBAGD_DEVICE_FOREACH(device, ctx) {
		*pos = strdup(ratbagd_device_get_path(device));
		if (!*pos)
			goto error;
		++pos;
	}

	*pos = NULL;
	*paths = devices;
	return 1;

error:
	for (pos = devices; *pos; ++pos)
		free(*pos);
	free(devices);
	return -ENOMEM;
}

static int ratbagd_get_devices(sd_bus *bus,
			       const char *path,
			       const char *interface,
			       const char *property,
			       sd_bus_message *reply,
			       void *userdata,
			       sd_bus_error *error)
{
	struct ratbagd *ctx = userdata;
	struct ratbagd_device *device;
	int r;

	r = sd_bus_message_open_container(reply, 'a', "o");
	if (r < 0)
		return r;

	RATBAGD_DEVICE_FOREACH(device, ctx) {
		r = sd_bus_message_append(reply,
					  "o",
					  ratbagd_device_get_path(device));
		if (r < 0)
			return r;
	}

	return sd_bus_message_close_container(reply);
}

static const sd_bus_vtable ratbagd_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Devices", "ao", ratbagd_get_devices, 0, 0),
	SD_BUS_SIGNAL("DeviceNew", "o", 0),
	SD_BUS_SIGNAL("DeviceRemoved", "o", 0),
	SD_BUS_VTABLE_END,
};

static void ratbagd_process_device(struct ratbagd *ctx,
				   struct udev_device *udevice)
{
	struct ratbag_device *lib_device;
	struct ratbagd_device *device;
	const char *name;
	int r;

	/*
	 * TODO: libratbag should provide some mechanism to allow
	 *       device-grouping, just like libinput does. If multiple input
	 *       devices belong to the same virtual device, we should not add
	 *       it multiple times. Instead, libratbag should group them and
	 *       provide *us* a unique name that identifies the group, rather
	 *       than taking a random input-device as tag.
	 */

	name = udev_device_get_sysname(udevice);
	if (!name || !startswith(name, "event"))
		return;

	device = ratbagd_device_lookup(ctx, name);

	if (streq_ptr("remove", udev_device_get_action(udevice))) {
		/* device was removed, unlink it and destroy our context */
		if (device) {
			(void) sd_bus_emit_signal(ctx->bus,
						  "/org/freedesktop/ratbag1",
						  "org.freedesktop.ratbag1.Manager",
						  "DeviceRemoved",
						  "o",
						  ratbagd_device_get_path(device));
			ratbagd_device_unlink(device);
			ratbagd_device_free(device);
		}
	} else if (device) {
		/* device already known, refresh our view of the device */
	} else {
		enum ratbag_error_code error;

		/* device unknown, create new one and link it */
		error = ratbag_device_new_from_udev_device(ctx->lib_ctx,
							   udevice,
							   &lib_device);
		if (error != RATBAG_SUCCESS)
			return; /* unsupported device */

		r = ratbagd_device_new(&device, ctx, name, lib_device);

		/* the ratbagd_device takes its own reference, drop ours */
		ratbag_device_unref(lib_device);

		if (r < 0) {
			log_error("Cannot track device '%s'\n", name);
			return;
		}

		ratbagd_device_link(device);
		(void) sd_bus_emit_signal(ctx->bus,
					  "/org/freedesktop/ratbag1",
					  "org.freedesktop.ratbag1.Manager",
					  "DeviceNew",
					  "o",
					  ratbagd_device_get_path(device));
	}
}

static int ratbagd_monitor_event(sd_event_source *source,
				 int fd,
				 uint32_t mask,
				 void *userdata)
{
	struct ratbagd *ctx = userdata;
	struct udev_device *udevice;

	udevice = udev_monitor_receive_device(ctx->monitor);
	if (!udevice)
		return 0;

	ratbagd_process_device(ctx, udevice);
	udev_device_unref(udevice);

	return 0;
}

static int ratbagd_lib_open_restricted(const char *path,
				       int flags,
				       void *userdata)
{
	return open(path, flags, 0);
}

static void ratbagd_lib_close_restricted(int fd, void *userdata)
{
	safe_close(fd);
}

static const struct ratbag_interface ratbagd_lib_interface = {
	.open_restricted	= ratbagd_lib_open_restricted,
	.close_restricted	= ratbagd_lib_close_restricted,
};

static struct ratbagd *ratbagd_free(struct ratbagd *ctx)
{
	if (!ctx)
		return NULL;

	ctx->bus = sd_bus_flush_close_unref(ctx->bus);
	ctx->monitor_source = sd_event_source_unref(ctx->monitor_source);
	ctx->monitor = udev_monitor_unref(ctx->monitor);
	ctx->lib_ctx = ratbag_unref(ctx->lib_ctx);
	ctx->event = sd_event_unref(ctx->event);

	assert(!ctx->device_map.root);
	assert(!ctx->lib_ctx); /* ratbag returns non-NULL if still pinned */

	return mfree(ctx);
}

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagd *, ratbagd_free);

static int ratbagd_init_monitor(struct ratbagd *ctx)
{
	struct udev *udev;
	int r;

	udev = udev_new();
	if (!udev)
		return -ENOMEM;

	ctx->monitor = udev_monitor_new_from_netlink(udev, "udev");

	/* we don't need the context to stay around; drop it */
	udev_unref(udev);

	if (!ctx->monitor)
		return -ENOMEM;

	r = udev_monitor_filter_add_match_subsystem_devtype(ctx->monitor,
							    "input",
							    NULL);
	if (r < 0)
		return r;

	r = udev_monitor_enable_receiving(ctx->monitor);
	if (r < 0)
		return r;

	r = sd_event_add_io(ctx->event,
			    &ctx->monitor_source,
			    udev_monitor_get_fd(ctx->monitor),
			    EPOLLIN,
			    ratbagd_monitor_event,
			    ctx);
	if (r < 0)
		return r;

	return 0;
}

static int ratbagd_new(struct ratbagd **out)
{
	_cleanup_(ratbagd_freep) struct ratbagd *ctx = NULL;
	int r;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	r = sd_event_default(&ctx->event);
	if (r < 0)
		return r;

	r = sd_event_set_watchdog(ctx->event, true);
	if (r < 0)
		return r;

	ctx->lib_ctx = ratbag_create_context(&ratbagd_lib_interface, ctx);
	if (!ctx->lib_ctx)
		return -ENOMEM;

	if (verbose)
		ratbag_log_set_priority(ctx->lib_ctx,
					RATBAG_LOG_PRIORITY_DEBUG);

	r = ratbagd_init_monitor(ctx);
	if (r < 0)
		return r;

	r = sd_bus_open_system(&ctx->bus);
	if (r < 0)
		return r;

	r = sd_bus_add_object_vtable(ctx->bus,
				     NULL,
				     "/org/freedesktop/ratbag1",
				     "org.freedesktop.ratbag1.Manager",
				     ratbagd_vtable,
				     ctx);
	if (r < 0)
		return r;

	r = sd_bus_add_fallback_vtable(ctx->bus,
				       NULL,
				       "/org/freedesktop/ratbag1/device",
				       "org.freedesktop.ratbag1.Device",
				       ratbagd_device_vtable,
				       ratbagd_find_device,
				       ctx);
	if (r < 0)
		return r;

	r = sd_bus_add_node_enumerator(ctx->bus,
				       NULL,
				       "/org/freedesktop/ratbag1/device",
				       ratbagd_list_devices,
				       ctx);
	if (r < 0)
		return r;

	r = sd_bus_request_name(ctx->bus, "org.freedesktop.ratbag1", 0);
	if (r < 0)
		return r;

	r = sd_bus_attach_event(ctx->bus, ctx->event, 0);
	if (r < 0)
		return r;

	*out = ctx;
	ctx = NULL;
	return 0;
}

static int ratbagd_run_enumerate(struct ratbagd *ctx)
{
	struct udev_list_entry *list, *iter;
	struct udev_enumerate *e;
	struct udev *udev;
	int r;

	udev = udev_monitor_get_udev(ctx->monitor);

	e = udev_enumerate_new(udev);
	if (!e)
		return -ENOMEM;

	r = udev_enumerate_add_match_subsystem(e, "input");
	if (r < 0)
		goto exit;

	r = udev_enumerate_add_match_is_initialized(e);
	if (r < 0)
		goto exit;

	r = udev_enumerate_scan_devices(e);
	if (r < 0)
		goto exit;

	list = udev_enumerate_get_list_entry(e);
	udev_list_entry_foreach(iter, list) {
		struct udev_device *udevice;
		const char *p;

		p = udev_list_entry_get_name(iter);
		udevice = udev_device_new_from_syspath(udev, p);
		if (udevice)
			ratbagd_process_device(ctx, udevice);
		udev_device_unref(udevice);
	}

	r = 0;

exit:
	udev_enumerate_unref(e);
	return r;
}

static int ratbagd_run(struct ratbagd *ctx)
{
	int r;

	/*
	 * TODO: We should support exit-on-idle and bus-activation. Note that
	 *       we don't store any state on our own, hence, all we have to do
	 *       is to make sure we advertise device add/remove events via the
	 *       bus (in case there is a listener).
	 *       To track such events, we should store devices we already
	 *       advertised in /run/ratbagd/devices/ and read it out on
	 *       activation.
	 *       Note that this feature requires udev-activation, which might
	 *       not be possible, yet.
	 */

	r = ratbagd_run_enumerate(ctx);
	if (r < 0)
		return r;

	return sd_event_loop(ctx->event);
}

int main(int argc, char *argv[])
{
	struct ratbagd *ctx = NULL;
	int r;

	if (argc > 1) {
		if (streq(argv[1], "--verbose")) {
			verbose = true;
		} else {
			fprintf(stderr, "Usage: %s [--verbose]\n",
				program_invocation_short_name);
			r = -EINVAL;
			goto exit;
		}
	}

	r = ratbagd_new(&ctx);
	if (r < 0)
		goto exit;

	r = ratbagd_run(ctx);

exit:
	ratbagd_free(ctx);

	if (r < 0) {
		errno = -r;
		log_error("Failed: %m\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
