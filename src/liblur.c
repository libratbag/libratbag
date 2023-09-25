/*
 * Copyright 2015 Red Hat, Inc
 *
 * liblur - Logitech Unifying Receiver access library
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

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <linux/hidraw.h>

#include "usb-ids.h"
#include "hidpp10.h"
#include "libratbag-util.h"
#include "liblur.h"

#define _EXPORT_ __attribute__ ((visibility("default")))
#define MAX_DEVICES 6

static inline void
cleanup_hidpp10_device_destroy(struct hidpp10_device **hidpp10_device) {
	if (*hidpp10_device)
		hidpp10_device_destroy(*hidpp10_device);
}
#define _cleanup_hidpp10_device_destroy_ _cleanup_(cleanup_hidpp10_device_destroy)

struct lur_receiver {
	int refcount;
	int fd;
	void *userdata;

	struct hidpp10_device *hidppdev;

	struct list devices;
};

struct lur_device {
	struct lur_receiver *receiver;
	int refcount;
	void *userdata;

	char *name;
	uint16_t vid, pid;
	uint32_t serial;
	enum lur_device_type type;

	int hidppidx;

	struct list node;

	bool present; /* used during re-enumeration */
};

_EXPORT_ const char *
lur_device_get_name(struct lur_device *dev)
{
	return dev->name;
}

_EXPORT_ uint16_t
lur_device_get_vendor_id(struct lur_device *dev)
{
	return dev->vid;
}


_EXPORT_ uint16_t
lur_device_get_product_id(struct lur_device *dev)
{
	return dev->pid;
}

_EXPORT_ enum lur_device_type
lur_device_get_type(struct lur_device *dev)
{
	return dev->type;
}

_EXPORT_ uint32_t
lur_device_get_serial(struct lur_device *dev)
{
	return dev->serial;
}

_EXPORT_ int
lur_device_disconnect(struct lur_device *dev)
{
	int rc;

	rc = hidpp10_disconnect(dev->receiver->hidppdev, dev->hidppidx);
	if (rc == 0) {
		list_remove(&dev->node);
		list_init(&dev->node);
	}

	return rc;
}

_EXPORT_ int
lur_is_receiver(uint16_t vid, uint16_t pid)
{
	return (vid == USB_VENDOR_ID_LOGITECH && (pid == 0xc52b || pid == 0xc532));
}

static bool
hidraw_is_receiver(int fd)
{
	struct hidraw_devinfo info;
	int rc;

	rc = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (rc < 0)
		return false;
	if (!lur_is_receiver(info.vendor, info.product))
		return false;

	return true;
}

static int
hidpp10_init(int fd, struct hidpp10_device **out)
{
	struct hidpp_device base;

	hidpp_device_init(&base, fd);

	return hidpp10_device_new(&base, HIDPP_RECEIVER_IDX,
				  HIDPP10_PROFILE_UNKNOWN, 1, out);
}

_EXPORT_ int
lur_receiver_new_from_hidraw(int fd, void *userdata, struct lur_receiver **out)
{
	int rc;
	struct lur_receiver *receiver;

	if (!hidraw_is_receiver(fd))
		return -EINVAL;

	receiver = zalloc(sizeof(*receiver));
	receiver->refcount = 1;
	receiver->fd = fd;
	receiver->userdata = userdata;
	list_init(&receiver->devices);

	rc = hidpp10_init(fd, &receiver->hidppdev);
	if (rc)
		goto error;

	*out = receiver;
	return 0;

error:
	free(receiver);
	return rc;
}

_EXPORT_ int
lur_receiver_enumerate(struct lur_receiver *lur,
		       struct lur_device ***devices_out)
{
	int i;
	int ndevices = 0;
	struct hidpp_device base;
	struct lur_device *dev, *tmp;
	int rc;
	struct lur_device **devices;

	hidpp_device_init(&base, lur->fd);

	list_for_each(dev, &lur->devices, node)
		dev->present = false;

	for (i = 0; i < MAX_DEVICES; i++) {
		_cleanup_hidpp10_device_destroy_ struct hidpp10_device *d = NULL;
		size_t name_size = 64;
		char name[name_size];
		uint8_t interval, type;
		uint16_t wpid;
		uint32_t serial;
		bool is_new_device = true;

		rc = hidpp10_device_new(&base, i, HIDPP10_PROFILE_UNKNOWN, 1, &d);
		if (rc)
			continue;

		rc = hidpp10_get_pairing_information_device_name(d, name, &name_size);
		if (rc)
			continue;

		rc = hidpp10_get_pairing_information(d, &interval, &wpid, &type);
		if (rc)
			continue;

		rc = hidpp10_get_extended_pairing_information(d, &serial);
		if (rc)
			continue;

		/* check if we have the device already in the list */
		list_for_each(dev, &lur->devices, node) {
			if (dev->pid == wpid &&
			    dev->type == type &&
			    dev->serial == serial &&
			    streq(dev->name, name)) {
				/* index may have changed, doesn't make it a
				 * new device, just update it */
				dev->hidppidx = i;
				dev->present = true;
				is_new_device = false;
				break;
			}
		}

		if (is_new_device) {
			dev = zalloc(sizeof *dev);
			dev->receiver = lur;
			lur_receiver_ref(lur);
			dev->refcount = 1;
			dev->name = strdup_safe(name);
			dev->vid = USB_VENDOR_ID_LOGITECH;
			dev->pid = wpid;
			dev->type = type;
			dev->serial = serial;
			dev->hidppidx = i;
			dev->present = true;
			list_insert(&lur->devices, &dev->node);
		}
	}

	devices = zalloc(MAX_DEVICES * sizeof(*devices));

	/* Now drop all devices that disappeared */
	list_for_each_safe(dev, tmp, &lur->devices, node) {
		if (dev->present) {
			devices[ndevices++] = dev;
		} else {
			list_remove(&dev->node);
			list_init(&dev->node);
			lur_device_unref(dev);
		}
	}

	*devices_out = devices;

	return ndevices;
}

_EXPORT_ int
lur_receiver_open(struct lur_receiver *lur, uint8_t timeout)
{
	return !!hidpp10_open_lock(lur->hidppdev, timeout);
}

_EXPORT_ int
lur_receiver_close(struct lur_receiver *lur)
{
	return 0;
}

_EXPORT_ int
lur_receiver_get_fd(struct lur_receiver *lur)
{
	return lur->fd;
}

_EXPORT_ struct lur_receiver *
lur_receiver_ref(struct lur_receiver *lur)
{
	assert(lur->refcount > 0);
	lur->refcount++;

	return lur;
}

_EXPORT_ struct lur_receiver *
lur_receiver_unref(struct lur_receiver *lur)
{
	if (lur == NULL)
		return NULL;

	assert(lur->refcount > 0);
	lur->refcount--;

	if (lur->refcount > 0)
		return NULL;

	/* when we get here, all the devices have already been removed from
	 * the receiver */

	hidpp10_device_destroy(lur->hidppdev);
	free(lur);

	return NULL;
}

_EXPORT_ void
lur_receiver_set_user_data(struct lur_receiver *lur, void *userdata)
{
	lur->userdata = userdata;
}

_EXPORT_ void*
lur_receiver_get_user_data(const struct lur_receiver *lur)
{
	return lur->userdata;
}

_EXPORT_ struct lur_device *
lur_device_ref(struct lur_device *dev)
{
	assert(dev->refcount > 0);
	dev->refcount++;

	return dev;
}

_EXPORT_ struct lur_device *
lur_device_unref(struct lur_device *dev)
{
	if (dev == NULL)
		return NULL;

	assert(dev->refcount > 0);
	dev->refcount--;

	if (dev->refcount > 0)
		return NULL;

	list_remove(&dev->node);
	lur_receiver_unref(dev->receiver);
	free(dev->name);
	free(dev);

	return NULL;
}

_EXPORT_ void
lur_device_set_user_data(struct lur_device *dev, void *userdata)
{
	dev->userdata = userdata;
}

_EXPORT_ void*
lur_device_get_user_data(const struct lur_device *dev)
{
	return dev->userdata;
}
