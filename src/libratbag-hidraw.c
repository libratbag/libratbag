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
#define HID_COLLECTION		0b10100000
#define HID_USAGE_PAGE		0b00000100
#define HID_USAGE		0b00001000

#define HID_PHYSICAL		0
#define HID_APPLICATION		1
#define HID_LOGICAL		2

static int
ratbag_hidraw_parse_report_descriptor(struct ratbag_device *device)
{
	int rc, desc_size = 0;
	struct ratbag_hidraw *hidraw = &device->hidraw;
	struct hidraw_report_descriptor report_desc = {0};
	unsigned int i, j;
	unsigned int usage_page, usage;

	hidraw->num_reports = 0;

	rc = ioctl(hidraw->fd, HIDIOCGRDESCSIZE, &desc_size);
	if (rc < 0)
		return rc;

	report_desc.size = desc_size;
	rc = ioctl(hidraw->fd, HIDIOCGRDESC, &report_desc);
	if (rc < 0)
		return rc;

	i = 0;
	usage_page = 0;
	usage = 0;
	while (i < report_desc.size) {
		uint8_t value = report_desc.value[i];
		uint8_t hid = value & 0xfc;
		uint8_t size = value & 0x3;
		unsigned content = 0;

		if (size == 3)
			size = 4;

		if (i + size >= report_desc.size)
			return -EPROTO;

		for (j = 0; j < size; j++)
			content |= report_desc.value[i + j + 1] << (j * 8);

		switch (hid) {
		case HID_REPORT_ID:
			if (hidraw->reports) {
				log_debug(device->ratbag, "report ID %02x\n", content);
				hidraw->reports[hidraw->num_reports].report_id = content;
				hidraw->reports[hidraw->num_reports].usage_page = usage_page;
				hidraw->reports[hidraw->num_reports].usage = usage;
			}
			hidraw->num_reports++;
			break;
		case HID_COLLECTION:
			if (content == HID_APPLICATION &&
			    hidraw->reports &&
			    !hidraw->num_reports &&
			    !hidraw->reports[0].report_id) {
				hidraw->reports[hidraw->num_reports].usage_page = usage_page;
				hidraw->reports[hidraw->num_reports].usage = usage;
			}
			break;
		case HID_USAGE_PAGE:
			usage_page = content;
			break;
		case HID_USAGE:
			usage = content;
			break;
		}

		i += 1 + size;
	}

	return 0;
}

static int
ratbag_open_hidraw_node(struct ratbag_device *device, struct udev_device *hidraw_udev)
{
	struct hidraw_devinfo info;
	struct ratbag_device *tmp_device;
	int fd, res;
	const char *devnode;
	const char *sysname;
	size_t reports_size;

	device->hidraw.fd = -1;

	sysname = udev_device_get_sysname(hidraw_udev);
	if (!strneq("hidraw", sysname, 6))
		return -ENODEV;

	list_for_each(tmp_device, &device->ratbag->devices, link) {
		if (tmp_device->hidraw.sysname &&
		    streq(tmp_device->hidraw.sysname, sysname)) {
			return -ENODEV;
		}
	}

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

	if (device->hidraw.num_reports)
		reports_size = device->hidraw.num_reports * sizeof(struct ratbag_hid_report);
	else
		reports_size = sizeof(struct ratbag_hid_report);

	device->hidraw.reports = zalloc(reports_size);
	ratbag_hidraw_parse_report_descriptor(device);

	device->hidraw.sysname = strdup_safe(sysname);
	return 0;

err:
	if (fd >= 0)
		ratbag_close_fd(device, fd);
	return -errno;
}

static int
ratbag_find_hidraw_node(struct ratbag_device *device,
			int (*match)(struct ratbag_device *device),
			int use_usb_parent)
{
	struct ratbag *ratbag = device->ratbag;
	struct udev_enumerate *e;
	struct udev_list_entry *entry;
	const char *path;
	struct udev_device *hid_udev;
	struct udev_device *parent_udev;
	struct udev *udev = ratbag->udev;
	int rc = -ENODEV;
	int matched;

	assert(match);

	hid_udev = udev_device_get_parent_with_subsystem_devtype(device->udev_device, "hid", NULL);

	if (!hid_udev)
		return -ENODEV;

	if (use_usb_parent && device->ids.bustype == BUS_USB) {
		/* using the parent usb_device to match siblings */
		parent_udev = udev_device_get_parent(hid_udev);
		if (!streq("uhid", udev_device_get_sysname(parent_udev)))
			parent_udev = udev_device_get_parent_with_subsystem_devtype(hid_udev,
										    "usb",
										    "usb_device");
	} else {
		parent_udev = hid_udev;
	}

	e = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(e, "hidraw");
	udev_enumerate_add_match_parent(e, parent_udev);
	udev_enumerate_scan_devices(e);
	udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(e)) {
		_cleanup_udev_device_unref_ struct udev_device *udev_device;

		path = udev_list_entry_get_name(entry);
		udev_device = udev_device_new_from_syspath(udev, path);
		if (!udev_device)
			continue;

		rc = ratbag_open_hidraw_node(device, udev_device);
		if (rc)
			goto skip;

		matched = match(device);
		if (matched == 1) {
			rc = 0;
			goto out;
		}

skip:
		ratbag_close_hidraw(device);
		rc = -ENODEV;
	}

out:
	udev_enumerate_unref(e);

	return rc;
}

int
ratbag_find_hidraw(struct ratbag_device *device, int (*match)(struct ratbag_device *device))
{
	return ratbag_find_hidraw_node(device, match, true);
}

static int
hidraw_match_all(struct ratbag_device *device)
{
	return 1;
}

int
ratbag_open_hidraw(struct ratbag_device *device)
{
	return ratbag_find_hidraw_node(device, hidraw_match_all, false);
}

static struct ratbag_hid_report *
ratbag_hidraw_get_report(struct ratbag_device *device, unsigned int report_id)
{
	unsigned i;

	if (report_id == 0) {
		if (device->hidraw.reports[0].report_id == report_id)
			return &device->hidraw.reports[0];
		else
			return NULL;
	}

	for (i = 0; i < device->hidraw.num_reports; i++) {
		if (device->hidraw.reports[i].report_id == report_id)
			return &device->hidraw.reports[i];
	}

	return NULL;
}


int
ratbag_hidraw_has_report(struct ratbag_device *device, unsigned int report_id)
{
	return ratbag_hidraw_get_report(device, report_id) != NULL;
}

unsigned int
ratbag_hidraw_get_usage_page(struct ratbag_device *device, unsigned int report_id)
{
	struct ratbag_hid_report *report;

	report = ratbag_hidraw_get_report(device, report_id);

	if (!report)
		return 0;

	return report->usage_page;
}

unsigned int
ratbag_hidraw_get_usage(struct ratbag_device *device, unsigned int report_id)
{
	struct ratbag_hid_report *report;

	report = ratbag_hidraw_get_report(device, report_id);

	if (!report)
		return 0;

	return report->usage;
}

void
ratbag_close_hidraw(struct ratbag_device *device)
{
	if (device->hidraw.fd < 0)
		return;

	if (device->hidraw.sysname) {
		free(device->hidraw.sysname);
		device->hidraw.sysname = NULL;
	}

	ratbag_close_fd(device, device->hidraw.fd);
	device->hidraw.fd = -1;

	if (device->hidraw.reports) {
		free(device->hidraw.reports);
		device->hidraw.reports = NULL;
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
