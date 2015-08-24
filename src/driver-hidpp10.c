/*
 * Copyright 2013-2015 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013-2015 Red Hat, Inc
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

/*
 * Based on the HID++ 1.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

/*
 * for this driver to work, you need a kernel >= v3.19 or one which contains
 * 925f0f3ed24f98b40c28627e74ff3e7f9d1e28bc ("HID: logitech-dj: allow transfer
 * of HID++ reports from/to the correct dj device")
 */

#include "config.h"

#define USB_VENDOR_ID_LOGITECH			0x046d

#include <linux/types.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp10.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

struct hidpp10drv_data {
	struct hidpp10_device *dev;
};

static void
hidpp10drv_read_button(struct ratbag_button *button)
{
}

static int
hidpp10drv_write_button(struct ratbag_button *button)
{
	return -1;
}

static int
hidpp10drv_has_capability(const struct ratbag_device *device, enum ratbag_capability cap)
{
	return 0;
}

static int
hidpp10drv_current_profile(struct ratbag_device *device)
{
	return -1;
}

static int
hidpp10drv_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	return -1;
}

static void
hidpp10drv_read_profile(struct ratbag_profile *profile, unsigned int index)
{
}

static int
hidpp10drv_write_profile(struct ratbag_profile *profile)
{
	return -1;
}

static bool
hidpp10drv_is_unifying_receiver(struct ratbag_device *device)
{
	struct udev_device *hidraw;
	struct input_id id;
	const char *product;
	int rc;

	hidraw = device->udev_hidraw;
	/* Note: PRODUCT on the hid device is missing the bustype! */
	product = udev_prop_value(hidraw, "PRODUCT");
	rc = sscanf(product, "%hx/%hx/%hx", &id.vendor, &id.product, &id.version);
	if (rc != 3)
		return false;

	if (id.vendor == USB_VENDOR_ID_LOGITECH &&
	    (id.product == 0xc52b || id.product == 0xc532))
		return true;

	return false;
}

static int
hidpp10drv_find_unifying_hidraw(struct ratbag_device *device)
{
	struct udev_device *receiver, *hid, *hidraw;
	struct udev *udev;
	struct udev_enumerate *e;
	struct udev_list_entry *entry;

	hidraw = device->udev_hidraw;

	/* the receiver device is two up */
	hid = udev_device_get_parent_with_subsystem_devtype(hidraw, "hid", NULL);
	receiver = udev_device_get_parent_with_subsystem_devtype(hid, "hid", NULL);
	if (!receiver)
		return -1; /* should not happen */

	udev = udev_device_get_udev(hidraw);
	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_add_match_parent(e, receiver);
	udev_enumerate_scan_devices(e);

	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		const char *path, *sysname;

		path = udev_list_entry_get_name(entry);
		hidraw = udev_device_new_from_syspath(udev, path);
		if (!hidraw)
			continue;

		sysname = udev_device_get_sysname(hidraw);
		if (!strneq("hidraw", sysname, 6)) {
			udev_device_unref(hidraw);
			continue;
		}

		/* The receiver has multiple hidraw devices, we only want
		 * the one where the direct parent is the receiver */
		hid = udev_device_get_parent_with_subsystem_devtype(hidraw, "hid", NULL);
		if (!streq(udev_device_get_syspath(hid), udev_device_get_syspath(receiver)))
			continue;

		ratbag_device_set_hidraw_device(device, hidraw);

		udev_device_unref(hidraw);
		goto out;
	}

out:
	udev_enumerate_unref(e);

	return 0;
}

static int
hidpp10drv_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct hidpp10drv_data *drv_data;
	struct hidpp10_device dev;
	bool is_unifying_receiver;

	/* check if the device is a unifying receiver first so we can update
	 * the hidraw path before we open it */
	is_unifying_receiver = hidpp10drv_is_unifying_receiver(device);
	if (is_unifying_receiver)
		hidpp10drv_find_unifying_hidraw(device);

	rc = ratbag_open_hidraw(device);
	if (rc) {
		log_error(device->ratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	if (!drv_data)
		return -ENODEV;

	if (is_unifying_receiver)
		rc = hidpp10_get_device_from_wpid(device,
						  id.id.product,
						  &dev);
	else /* wired devices are 0 */
		rc = hidpp10_get_device_from_idx(device, 0, &dev);

	if (rc) {
		log_error(device->ratbag,
			  "Failed to get HID++1.0 device for %s\n",
			  device->name);
		goto err;
	}

	ratbag_set_drv_data(device, drv_data);

	device->num_profiles = 1;
	device->num_buttons = 8;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);

	return rc;
}

static void
hidpp10drv_remove(struct ratbag_device *device)
{
	free(ratbag_get_drv_data(device));
}

#define LOGITECH_DEVICE(_bus, _pid)		\
	{ .bustype = (_bus),			\
	  .vendor = USB_VENDOR_ID_LOGITECH,	\
	  .product = (_pid),			\
	  .version = VERSION_ANY },

static const struct ratbag_id hidpp10drv_table[] = {
	/* M705 */
	{ .id = LOGITECH_DEVICE(BUS_USB, 0x101b) },

	/* M570 */
	{ .id = LOGITECH_DEVICE(BUS_USB, 0x1028) },

	/* G500s */
	{ .id = LOGITECH_DEVICE(BUS_USB, 0xc24e) },

	{ },
};

struct ratbag_driver hidpp10_driver = {
	.name = "Logitech HID++1.0",
	.table_ids = hidpp10drv_table,
	.probe = hidpp10drv_probe,
	.remove = hidpp10drv_remove,
	.read_profile = hidpp10drv_read_profile,
	.write_profile = hidpp10drv_write_profile,
	.get_active_profile = hidpp10drv_current_profile,
	.set_active_profile = hidpp10drv_set_current_profile,
	.has_capability = hidpp10drv_has_capability,
	.read_button = hidpp10drv_read_button,
	.write_button = hidpp10drv_write_button,
};
