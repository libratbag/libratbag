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
	enum ratbag_button_type type = RATBAG_BUTTON_TYPE_UNKNOWN;

	/* FIXME: this is valid for the G500s only */
	switch (button->index) {
	case 0: type = RATBAG_BUTTON_TYPE_LEFT; break;
	case 1: type = RATBAG_BUTTON_TYPE_MIDDLE; break;
	case 2: type = RATBAG_BUTTON_TYPE_RIGHT; break;
	case 3: type = RATBAG_BUTTON_TYPE_THUMB; break;
	case 4: type = RATBAG_BUTTON_TYPE_THUMB2; break;
	case 5: type = RATBAG_BUTTON_TYPE_THUMB3; break;
	case 6: type = RATBAG_BUTTON_TYPE_WHEEL_LEFT; break;
	case 7: type = RATBAG_BUTTON_TYPE_WHEEL_RIGHT; break;
	case 8: type = RATBAG_BUTTON_TYPE_RESOLUTION_UP; break;
	case 9: type = RATBAG_BUTTON_TYPE_RESOLUTION_DOWN; break;
	case 10:
	case 11:
	case 12: /* these don't actually exist on the device */
		type = RATBAG_BUTTON_TYPE_UNKNOWN; break;
	default:
		break;
	}

	button->type = type;

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
}

static int
hidpp10drv_write_button(struct ratbag_button *button,
			const struct ratbag_button_action *action)
{
	return -ENOTSUP;
}

static int
hidpp10drv_has_capability(const struct ratbag_device *device,
			  enum ratbag_device_capability cap)
{
	switch (cap) {
	case RATBAG_DEVICE_CAP_NONE:
		abort();
		break;
	case RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE:
	case RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION:
	case RATBAG_DEVICE_CAP_BUTTON_KEY:
	case RATBAG_DEVICE_CAP_BUTTON_MACROS:
	case RATBAG_DEVICE_CAP_DEFAULT_PROFILE:
		return (device->num_profiles > 1);
	}
	return 0;
}

static int
hidpp10drv_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	return -ENOTSUP;
}

static int
hidpp10drv_set_default_profile(struct ratbag_device *device, unsigned int index)
{
	return -ENOTSUP;
}

static void
hidpp10drv_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct hidpp10drv_data *drv_data;
	struct hidpp10_device *hidpp10;
	struct hidpp10_profile p;
	struct ratbag_resolution *res;
	int rc;
	unsigned int i;
	uint16_t xres, yres;
	int8_t idx;

	drv_data = ratbag_get_drv_data(device);
	hidpp10 = drv_data->dev;
	rc = hidpp10_get_profile(hidpp10, index, &p);
	if (rc)
		return;

	rc = hidpp10_get_current_profile(hidpp10, &idx);
	if (rc == 0 && (unsigned int)idx == profile->index)
		profile->is_active = true;

	rc = hidpp10_get_current_resolution(hidpp10, &xres, &yres);
	if (rc)
		xres = 0xffff;

	profile->resolution.num_modes = p.num_dpi_modes;
	for (i = 0; i < p.num_dpi_modes; i++) {
		res = ratbag_resolution_init(profile, i,
					     p.dpi_modes[i].xres,
					     p.dpi_modes[i].yres,
					     p.refresh_rate);
		ratbag_resolution_set_cap(res,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		if (profile->is_active &&
		    res->dpi_x == xres &&
		    res->dpi_y == yres)
			res->is_active = true;
		if (i == p.default_dpi_mode)
			res->is_default = true;
	}
}

static int
hidpp10drv_write_profile(struct ratbag_profile *profile)
{
	return -ENOTSUP;
}

static int
hidpp10drv_fill_from_profile(struct ratbag_device *device, struct hidpp10_device *dev)
{
	int rc;
	struct hidpp10_profile profile;

	/* We don't know the HID++1.0 requests to query for buttons, etc.
	 * and the get_current_profile request is garbage. So get profile 1
	 * and fill the device information in from that.
	 */
	rc = hidpp10_get_profile(dev, 0, &profile);
	if (rc)
		return rc;

	ratbag_device_init_profiles(device, HIDPP10_NUM_PROFILES, profile.num_buttons);

	return 0;
}

static int
hidpp10drv_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, REPORT_ID_SHORT);
}

static void
hidpp10_log(void *userdata, enum hidpp_log_priority priority, const char *format, va_list args)
{
	struct ratbag_device *device = userdata;

	log_msg_va(device->ratbag, priority, format, args);
}

static int
hidpp10drv_probe(struct ratbag_device *device)
{
	int rc;
	struct hidpp10drv_data *drv_data = NULL;
	struct hidpp10_device *dev = NULL;
	struct hidpp_device base;

	rc = ratbag_find_hidraw(device, hidpp10drv_test_hidraw);
	if (rc == -ENODEV) {
		return rc;
	} else if (rc) {
		log_error(device->ratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		rc = -ENODEV;
		goto err;
	}

	drv_data = zalloc(sizeof(*drv_data));
	base.log_handler = hidpp10_log;
	base.userdata = device;
	base.hidraw_fd = device->hidraw.fd;

	/* We can treat all devices as wired devices here. If we talk to the
	 * correct hidraw device the kernel adjusts the device index for us,
	 * so even for unifying receiver devices we can just 0x00 as device
	 * index.
	 */
	dev = hidpp10_device_new_from_idx(&base, WIRED_DEVICE_IDX);

	if (!dev) {
		log_error(device->ratbag,
			  "Failed to get HID++1.0 device for %s\n",
			  device->name);
		goto err;
	}

	drv_data->dev = dev;
	ratbag_set_drv_data(device, drv_data);

	if (hidpp10drv_fill_from_profile(device, dev)) {
		/* Fall back to something that every mouse has */
		struct ratbag_profile *profile;

		ratbag_device_init_profiles(device, 1, 3);
		profile = ratbag_device_get_profile(device, 0);
		profile->is_active = true;
		profile->is_default = true;
		ratbag_profile_unref(profile);
	}

	return 0;
err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	if (dev)
		hidpp10_device_destroy(dev);

	return rc;
}

static void
hidpp10drv_remove(struct ratbag_device *device)
{
	struct hidpp10drv_data *drv_data;
	struct hidpp10_device *dev;

	ratbag_close_hidraw(device);

	drv_data = ratbag_get_drv_data(device);
	dev = drv_data->dev;

	hidpp10_device_destroy(dev);

	free(drv_data);
}

struct ratbag_driver hidpp10_driver = {
	.name = "Logitech HID++1.0",
	.id = "hidpp10",
	.probe = hidpp10drv_probe,
	.remove = hidpp10drv_remove,
	.read_profile = hidpp10drv_read_profile,
	.write_profile = hidpp10drv_write_profile,
	.set_active_profile = hidpp10drv_set_current_profile,
	.set_default_profile = hidpp10drv_set_default_profile,
	.has_capability = hidpp10drv_has_capability,
	.read_button = hidpp10drv_read_button,
	.write_button = hidpp10drv_write_button,
};
