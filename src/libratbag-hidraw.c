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
#include <poll.h>
#include <libudev.h>
#include <linux/hidraw.h>
#include <string.h>

#include "libratbag-hidraw.h"
#include "libratbag-private.h"

/* defined in include/linux.hid.h in the kernel, but not exported */
#ifndef HID_MAX_BUFFER_SIZE
#define HID_MAX_BUFFER_SIZE	4096		/* 4kb */
#endif

#define HID_REPORT_ID		0b10000100

static int
ratbag_hidraw_parse_report_descriptor(struct ratbag_device *device)
{
	int rc, desc_size = 0;
	struct ratbag_hidraw *hidraw = &device->hidraw;
	struct hidraw_report_descriptor report_desc = {0};
	unsigned int i, j;

	hidraw->num_report_ids = 0;

	rc = ioctl(device->hidraw.fd, HIDIOCGRDESCSIZE, &desc_size);
	if (rc < 0)
		return rc;

	report_desc.size = desc_size;
	rc = ioctl(device->hidraw.fd, HIDIOCGRDESC, &report_desc);
	if (rc < 0)
		return rc;

	i = 0;
	while (i < report_desc.size) {
		uint8_t value = report_desc.value[i];
		uint8_t hid = value & 0xfc;
		uint8_t size = value & 0x3;

		if (size == 3)
			size = 4;

		if (i + size >= report_desc.size)
			return -EPROTO;

		if (hid == HID_REPORT_ID) {
			unsigned report_id = 0;

			for (j = 0; j < size; j++) {
				report_id |= report_desc.value[i + j + 1] << ((size - j - 1) * 8);
				if (hidraw->report_ids) {
					log_debug(device->ratbag, "report ID %02x\n", report_id);
					hidraw->report_ids[hidraw->num_report_ids] = report_id;
				}
				hidraw->num_report_ids++;
			}
		}

		i += 1 + size;
	}

	return 0;
}

static struct udev_device *
udev_find_hidraw(struct ratbag_device *device)
{
	struct ratbag *ratbag = device->ratbag;
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	struct udev_device *udev_device;
	const char *path, *sysname;
	struct udev_device *hid_udev;
	struct udev_device *hidraw_udev = NULL;
	struct udev *udev = ratbag->udev;

	hid_udev = udev_device_get_parent_with_subsystem_devtype(device->udev_device, "hid", NULL);

	if (!hid_udev)
		return NULL;

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_add_match_parent(e, hid_udev);
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		path = udev_list_entry_get_name(entry);
		udev_device = udev_device_new_from_syspath(udev, path);
		if (!udev_device)
			continue;

		sysname = udev_device_get_sysname(udev_device);
		if (!strneq("hidraw", sysname, 6)) {
			udev_device_unref(udev_device);
			continue;
		}

		hidraw_udev = udev_device_ref(udev_device);

		udev_device_unref(udev_device);
		goto out;
	}

out:
	udev_enumerate_unref(e);

	return hidraw_udev;
}

int
ratbag_open_hidraw(struct ratbag_device *device)
{
	struct hidraw_devinfo info;
	struct udev_device *hidraw_udev;
	int fd, res;
	const char *devnode;

	device->hidraw.fd = -1;

	hidraw_udev = udev_find_hidraw(device);
	if (!hidraw_udev)
		return -ENODEV;

	devnode = udev_device_get_devnode(hidraw_udev);
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

	/* check basic matching between the hidraw node and the ratbag device */
	if (info.bustype != device->ids.bustype ||
	    (info.vendor & 0xFFFF )!= device->ids.vendor ||
	    (info.product & 0xFFFF) != device->ids.product) {
		errno = ENODEV;
		goto err;
	}

	log_debug(device->ratbag,
		  "%s is device '%s'.\n",
		  device->name,
		  udev_device_get_devnode(hidraw_udev));

	device->hidraw.fd = fd;

	/* parse first to count the number of reports */
	res = ratbag_hidraw_parse_report_descriptor(device);
	if (res) {
		log_error(device->ratbag,
			  "Error while parsing the report descriptor: '%s' (%d)\n",
			  strerror(-res),
			  res);
		device->hidraw.fd = -1;
		goto err;
	}

	if (device->hidraw.num_report_ids) {
		device->hidraw.report_ids = zalloc(device->hidraw.num_report_ids *
							sizeof(uint8_t));
		ratbag_hidraw_parse_report_descriptor(device);
	}

	udev_device_unref(hidraw_udev);
	return 0;

err:
	udev_device_unref(hidraw_udev);
	if (fd >= 0)
		ratbag_close_fd(device, fd);
	return -errno;
}

int
ratbag_hidraw_has_report(struct ratbag_device *device, uint8_t report_id)
{
	unsigned i;

	for (i = 0; i < device->hidraw.num_report_ids; i++) {
		if (device->hidraw.report_ids[i] == report_id)
			return 1;
	}

	return 0;
}

void
ratbag_close_hidraw(struct ratbag_device *device)
{
	if (device->hidraw.fd < 0)
		return;

	ratbag_close_fd(device, device->hidraw.fd);
	device->hidraw.fd = -1;

	if (device->hidraw.report_ids) {
		free(device->hidraw.report_ids);
		device->hidraw.report_ids = NULL;
	}
}

int
ratbag_hidraw_raw_request(struct ratbag_device *device, unsigned char reportnum,
			  uint8_t *buf, size_t len, unsigned char rtype, int reqtype)
{
	uint8_t tmp_buf[HID_MAX_BUFFER_SIZE];
	int rc;

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || device->hidraw.fd < 0)
		return -EINVAL;

	if (rtype != HID_FEATURE_REPORT)
		return -ENOTSUP;

	switch (reqtype) {
	case HID_REQ_GET_REPORT:
		memset(tmp_buf, 0, len);
		tmp_buf[0] = reportnum;

		rc = ioctl(device->hidraw.fd, HIDIOCGFEATURE(len), tmp_buf);
		if (rc < 0)
			return -errno;

		log_buf_raw(device->ratbag, "feature get:   ", tmp_buf, (unsigned)rc);

		memcpy(buf, tmp_buf, rc);
		return rc;
	case HID_REQ_SET_REPORT:
		buf[0] = reportnum;

		log_buf_raw(device->ratbag, "feature set:   ", buf, len);
		rc = ioctl(device->hidraw.fd, HIDIOCSFEATURE(len), buf);
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

	if (len < 1 || len > HID_MAX_BUFFER_SIZE || !buf || device->hidraw.fd < 0)
		return -EINVAL;

	log_buf_raw(device->ratbag, "output report: ", buf, len);

	rc = write(device->hidraw.fd, buf, len);

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
	struct pollfd fds;

	if (len < 1 || !buf || device->hidraw.fd < 0)
		return -EINVAL;

	fds.fd = device->hidraw.fd;
	fds.events = POLLIN;

	rc = poll(&fds, 1, 1000);
	if (rc == -1)
		return -errno;

	if (rc == 0)
		return -ETIMEDOUT;

	rc = read(device->hidraw.fd, buf, len);

	if (rc > 0)
		log_buf_raw(device->ratbag, "input report:  ", buf, rc);

	return rc >= 0 ? rc : -errno;
}
