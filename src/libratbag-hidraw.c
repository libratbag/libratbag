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
ratbag_open_hidraw(struct ratbag *ratbag)
{
	struct hidraw_devinfo info;
	int fd, res;

	if (!ratbag->udev_hidraw)
		return -EINVAL;

	fd = open(udev_device_get_devnode(ratbag->udev_hidraw), O_RDWR);
	if (fd < 0)
		goto err;

	/* Get Raw Info */
	res = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (res < 0) {
		log_error(ratbag->libratbag,
			  "error while getting info from device");
		goto err;
	}
	/* Check if the device actually matches the input node */
	if (info.bustype != ratbag->ids.bustype ||
	    info.vendor != ratbag->ids.vendor ||
	    info.product != ratbag->ids.product)
		goto err;

	ratbag->hidraw_fd = fd;

	return 0;

err:
	if (fd >= 0)
		close(fd);
	return -errno;
}

int ratbag_hidraw_raw_request(struct ratbag *ratbag, unsigned char reportnum,
		__u8 *buf, size_t len, unsigned char rtype, int reqtype)
{
	char tmp_buf[HID_MAX_BUFFER_SIZE];
	int rc;

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || ratbag->hidraw_fd < 0)
		return -EINVAL;

	if (rtype != HID_FEATURE_REPORT)
		return -ENOTSUP;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		memset(tmp_buf, 0, len);
		tmp_buf[0] = reportnum;

		rc = ioctl(ratbag->hidraw_fd, HIDIOCGFEATURE(len), tmp_buf);
		if (rc < 0)
			return -errno;

		memcpy(buf, tmp_buf, rc);
		return rc;
	case HID_REQ_SET_REPORT:
		buf[0] = reportnum;
		rc = ioctl(ratbag->hidraw_fd, HIDIOCSFEATURE(len), buf);
		if (rc < 0)
			return -errno;

		return rc;
	}

	return -EINVAL;
}
