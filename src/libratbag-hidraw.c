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

#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <string.h>

#include "libratbag-hidraw.h"
#include "libratbag-private.h"

/* defined in include/linux.hid.h in the kernel, but not exported */
#ifndef HID_MAX_BUFFER_SIZE
#define HID_MAX_BUFFER_SIZE	4096		/* 4kb */
#endif

int
ratbag_open_hidraw(struct ratbag_device *device)
{
	struct hidraw_devinfo info;
	int fd, res;
	const char *devnode;
	struct input_id hid_id;

	if (!device->udev_hidraw)
		return -EINVAL;

	devnode = udev_device_get_devnode(device->udev_hidraw);
	fd = ratbag_open_path(device, devnode, O_RDWR);
	if (fd < 0)
		goto err;

	/* Get Raw Info */
	res = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (res < 0) {
		log_error(device->ratbag,
			  "error while getting info from device");
		goto err;
	}

	hid_id.bustype = info.bustype;
	hid_id.vendor = info.vendor;
	hid_id.product = info.product;

	/* Check if the device actually matches the input node */
	if (hid_id.bustype != device->ids.bustype ||
	    hid_id.vendor != device->ids.vendor ||
	    hid_id.product != device->ids.product) {
		errno = EINVAL;
		goto err;
	}

	device->hidraw_fd = fd;

	return 0;

err:
	if (fd >= 0)
		ratbag_close_fd(device, fd);
	return -errno;
}

int
ratbag_hidraw_raw_request(struct ratbag_device *device, unsigned char reportnum,
			  uint8_t *buf, size_t len, unsigned char rtype, int reqtype)
{
	char tmp_buf[HID_MAX_BUFFER_SIZE];
	int rc;

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || device->hidraw_fd < 0)
		return -EINVAL;

	if (rtype != HID_FEATURE_REPORT)
		return -ENOTSUP;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		memset(tmp_buf, 0, len);
		tmp_buf[0] = reportnum;

		rc = ioctl(device->hidraw_fd, HIDIOCGFEATURE(len), tmp_buf);
		if (rc < 0)
			return -errno;

		memcpy(buf, tmp_buf, rc);
		return rc;
	case HID_REQ_SET_REPORT:
		buf[0] = reportnum;
		rc = ioctl(device->hidraw_fd, HIDIOCSFEATURE(len), buf);
		if (rc < 0)
			return -errno;

		return rc;
	}

	return -EINVAL;
}

int
ratbag_hidraw_output_report(struct ratbag_device *device, uint8_t *buf, size_t len)
{
	int rc;

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || device->hidraw_fd < 0)
		return -EINVAL;

	rc = write(device->hidraw_fd, buf, len);

	if (rc < 0)
		return -errno;

	if (rc != (int)len)
		return -EIO;

	return 0;
}

int
ratbag_hidraw_read_input_report(struct ratbag_device *device, uint8_t *buf, size_t len)
{
	int rc;

	if (len < 1 || !buf || device->hidraw_fd < 0)
		return -EINVAL;

	rc = read(device->hidraw_fd, buf, len);
	return rc >= 0 ? rc : -errno;
}
