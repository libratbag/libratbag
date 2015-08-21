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

#include "hidpp10.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define HIDPP_CAP_RESOLUTION_2200			(1 << 0)
#define HIDPP_CAP_SWITCHABLE_RESOLUTION_2201		(1 << 1)
#define HIDPP_CAP_BUTTON_KEY_1b04			(1 << 2)

struct hidpp10drv_data {
	unsigned proto_major;
	unsigned proto_minor;
	unsigned long capabilities;
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

static int
hidpp10drv_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct hidpp10drv_data *drv_data;

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

	ratbag_set_drv_data(device, drv_data);

	device->num_profiles = 1;
	device->num_buttons = 8;

	return rc;
}

static void
hidpp10drv_remove(struct ratbag_device *device)
{
	free(ratbag_get_drv_data(device));
}

#define USB_VENDOR_ID_LOGITECH			0x046d

static const struct ratbag_id hidpp10drv_table[] = {
	/* M570 */
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_LOGITECH,
		 .product = 0x1028,
		 .version = VERSION_ANY },
	 .data = 1,
	},

	/* G500s */
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_LOGITECH,
		 .product = 0xc24e,
		 .version = VERSION_ANY },
	 .data = 1,
	},
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
