/*
 * M705 button 6 toggler.
 *
 * Copyright 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013 Red Hat, Inc
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

#include <linux/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp20.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define HIDPP_CAP_RESOLUTION_2200			(1 << 0)

struct unifying_data {
	unsigned proto_major;
	unsigned proto_minor;
	unsigned long capabilities;
};

static void
unifying_read_button(struct ratbag_button *button)
{
}

static int
unifying_write_button(struct ratbag_button *button)
{
	return -1;
}

static int
unifying_has_capability(const struct ratbag_device *device, enum ratbag_capability cap)
{
	return 0;
}

static int
unifying_current_profile(struct ratbag_device *device)
{
	return -1;
}

static int
unifying_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	return -1;
}

static void
unifying_read_profile(struct ratbag_profile *profile, unsigned int index)
{
}

static int
unifying_write_profile(struct ratbag_profile *profile)
{
	return -1;
}

static int
unifying_init_feature(struct ratbag_device *device, uint16_t feature)
{
	struct unifying_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	switch (feature) {
	case 0x0000:
	case 0x0001:
		/* these features are mandatory and already handled */
		break;
	case 0x2200: {
		uint16_t resolution;
		uint8_t flags;

		drv_data->capabilities |= HIDPP_CAP_RESOLUTION_2200;
		rc = hidpp20_mousepointer_get_mousepointer_info(device, &resolution, &flags);
		if (rc)
			return rc;
		log_info(device->ratbag, "device is at %d dpi\n", resolution);
		break;
	}
	default:
		log_info(device->ratbag, "unknown feature 0x%04x\n", feature);
	}
	return 0;
}

static int
unifying_20_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	struct hidpp20_feature *feature_list;
	int rc, i;

	rc = hidpp20_feature_set_get(device, &feature_list);
	if (rc < 0)
		return rc;

	if (rc > 0) {
		log_debug(device->ratbag, "'%s' has %d features\n", ratbag_device_get_name(device), rc);
		for (i = 0; i < rc; i++) {
			log_debug(device->ratbag, "0x%04x\n", feature_list[i].feature);
			unifying_init_feature(device, feature_list[i].feature);
		}
	}

	free(feature_list);

	return 0;

}

static int
unifying_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct unifying_data *drv_data;

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

	drv_data->proto_major = 1;
	drv_data->proto_minor = 0;

	rc = hidpp20_root_get_protocol_version(device, &drv_data->proto_major, &drv_data->proto_minor);
	if (rc) {
		/* communication error, best to ignore the device */
		rc = -EINVAL;
		goto err;
	}

	log_debug(device->ratbag, "'%s' is using protocol v%d.%d\n", ratbag_device_get_name(device), drv_data->proto_major, drv_data->proto_minor);

	ratbag_set_drv_data(device, drv_data);

	if (drv_data->proto_major >= 2) {
		rc = unifying_20_probe(device, id);
		if (rc)
			goto err;
	}

	device->num_profiles = 1;
	device->num_buttons = 8;

	return rc;
err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static void
unifying_remove(struct ratbag_device *device)
{
	free(ratbag_get_drv_data(device));
}

static const struct ratbag_id unifying_table[] = {
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_LOGITECH,
		 .product = USB_DEVICE_ID_LOGITECH_M705,
		 .version = VERSION_ANY },
	 .data = 1,
	},
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_LOGITECH,
		 .product = USB_DEVICE_ID_LOGITECH_M570,
		 .version = VERSION_ANY },
	 .data = 1,
	},
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_LOGITECH,
		 .product = USB_DEVICE_ID_LOGITECH_MX_MASTER,
		 .version = VERSION_ANY },
	 .data = 1,
	},

	{ },
};

struct ratbag_driver logitech_unifying_driver = {
	.name = "Logitech Unifying Receiver",
	.table_ids = unifying_table,
	.probe = unifying_probe,
	.remove = unifying_remove,
	.read_profile = unifying_read_profile,
	.write_profile = unifying_write_profile,
	.get_active_profile = unifying_current_profile,
	.set_active_profile = unifying_set_current_profile,
	.has_capability = unifying_has_capability,
	.read_button = unifying_read_button,
	.write_button = unifying_write_button,
};
