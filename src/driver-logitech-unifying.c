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

#include "config.h"

#include <linux/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp10.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

struct unifying_data {
	struct hidpp10_device *dev;
};

static void
unifying_read_button(struct ratbag_button *button)
{
}

static int
unifying_write_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->device;
	struct unifying_data *data = ratbag_get_drv_data(device);
	struct hidpp10_device *dev = data->dev;

	/* FIXME: this only toggles button 6 */

	/* M705 with FW RR 17.01 - build 0017 */
	if (dev->fw_major == 0x17 &&
	    dev->fw_minor == 0x01 &&
	    dev->build == 0x0015)
		hidpp10_toggle_individual_feature(device,
						  dev,
						  FEATURE_BIT_R0_SPECIAL_BUTTON_FUNCTION,
						  -1);
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
unifying_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct unifying_data *drv_data;
	struct hidpp10_device dev;

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

	rc = hidpp10_get_device_from_wpid(device,
					  id.id.product,
					  &dev);
	if (rc) {
		log_error(device->ratbag,
			  "Failed to get HID++1.0 device for %s\n",
			  device->name);
		goto err;
	}

	ratbag_set_drv_data(device, drv_data);

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
