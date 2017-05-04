/*
 * liblur - Logitech Unifying Receiver access library
 *
 * Copyright 2015 Red Hat, Inc
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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @struct lur_receiver
 *
 * A handle for accessing Logitech Unifying Receivers.
 *
 * This struct is refcounted, use lur_device_ref() and lur_device_unref().
 */
struct lur_receiver;

/**
 * @struct lur_device
 *
 * A handle for accessing devices paired with a lur_receiver.
 *
 * This struct is refcounted, use lur_device_ref() and lur_device_unref().
 */
struct lur_device;

enum lur_device_type {
	LUR_DEVICE_TYPE_UNKNOWN		= 0x00,
	LUR_DEVICE_TYPE_KEYBOARD	= 0x01,
	LUR_DEVICE_TYPE_MOUSE		= 0x02,
	LUR_DEVICE_TYPE_NUMPAD		= 0x03,
	LUR_DEVICE_TYPE_PRESENTER	= 0x04,
	LUR_DEVICE_TYPE_TRACKBALL	= 0x08,
	LUR_DEVICE_TYPE_TOUCHPAD	= 0x09,
};

const char *
lur_device_get_name(struct lur_device *dev);

uint16_t
lur_device_get_vendor_id(struct lur_device *dev);

uint16_t
lur_device_get_product_id(struct lur_device *dev);

enum lur_device_type
lur_device_get_type(struct lur_device *dev);

uint32_t
lur_device_get_serial(struct lur_device *dev);

/**
 * Disconnect this device from the receiver it is currently paired with.
 *
 * @param lur A valid receiver object
 * @return 0 on success or nonzero on error
 */
int
lur_device_disconnect(struct lur_device *dev);

/**
 * Returns non-zero if a device with the given vid/pid is a Logitech
 * Unifying Receiver device.
 *
 * @param vid The vendor ID
 * @param pid The product ID
 *
 * @return non-zero if the device is a unifying receiver, zero otherwise
 */
int
lur_is_receiver(uint16_t vid, uint16_t pid);

/**
 * Creates a new Logitech Unifying Receiver object from the file descriptor.
 * The fd must be a hidraw device, opened in O_RDWR.
 *
 * The returned struct has a refcount of at least 1, use lur_device_unref()
 * to release resources associated with it.
 * It is the caller's responsibility to close the fd after the
 * resources associated with this object have been freed.
 *
 * @param fd An O_RDWR file descriptor pointing to a /dev/hidraw node
 * @param userdata Caller-specific data
 * @param lur Set to the lur object on success, otherwise unmodified
 *
 * @return 0 on success or a negative errno on error
 * @retval -EINVAL The fd does not point to a lur receiver
 *
 * @note liblur does not have OOM handling. If an allocation fails, liblur
 * will simply abort()
 */
int
lur_receiver_new_from_hidraw(int fd, void *userdata, struct lur_receiver **lur);

/**
 * Enumerate devices currently paired with the given receiver.
 *
 * liblur does not have a device detection mechanism, it is recommend that a
 * caller monitors udev for hidraw devices being added and removed. When
 * such devices appear or disappear, a call to lur_receiver_enumerate()
 * yields the new list of devices. If no new unifying devices are available,
 * the returned list is identical to the list returned in the last call to
 * lur_receiver_enumerate(). Otherwise, the diff between the two lists
 * indicate the set of newly added and/or removed devices.
 *
 * The devices returned have a refcount of at least 1, use
 * lur_device_unref(). Repeated calls to this function do not increase the
 * devices' refcount.
 *
 * @param lur A valid receiver object
 * @param[out] devices An array of devices paired with this receiver. Use
 * free() to free the array, and lur_device_unref() to destroy each device.
 *
 * @return The number of devices returned, or -1 on error.
 */
int
lur_receiver_enumerate(struct lur_receiver *lur,
		       struct lur_device ***devices);

/**
 * Allow devices to be paired with this receiver for the given timeout.
 * Once 'open', the receiver will pair with a currently disconnected
 * device.
 *
 * @param lur A valid receiver object
 * @param timeout The time in seconds the receiver should accept new
 * pairings. The value 0 uses the receiver's default value (usually 30s).
 *
 * @return 0 on success or nonzero on error
 */
int
lur_receiver_open(struct lur_receiver *lur, uint8_t timeout);

/**
 * If a receiver is currently accepting devices, stop doing so. If the
 * receiver is not currently accepting devices, this function has no effect
 * and returns success.
 *
 * @param lur A valid receiver object
 * @return 0 on success or nonzero on error
 */
int
lur_receiver_close(struct lur_receiver *lur);

/**
 * Return the file descriptor used to initialize this receiver.
 *
 * @param lur A valid receiver object
 * @return The file descriptor passed into lur_receiver_new_from_hidraw
 */
int
lur_receiver_get_fd(struct lur_receiver* lur);

/**
 * Add a reference to the context. A context is destroyed whenever the
 * reference count reaches 0. See @ref lur_unref.
 *
 * @param lur A valid receiver object
 * @return The passed context
 */
struct lur_receiver *
lur_receiver_ref(struct lur_receiver *lur);

/**
 * Dereference the context. After this, the context may have been
 * destroyed, if the last reference was dereferenced. If so, the context is
 * invalid and may not be interacted with.
 *
 * @param lur A valid receiver object
 * @retval NULL
 */
struct lur_receiver *
lur_receiver_unref(struct lur_receiver *lur);

/**
 * Set caller-specific data associated with this object. liblur does
 * not manage, look at, or modify this data. The caller must ensure the
 * data is valid.
 *
 * Setting userdata overrides the one provided to
 * lur_receiver_new_from_hidraw().
 *
 * @param lur A valid receiver object
 * @param userdata Caller-specific data passed to the various callback
 * interfaces.
 */
void
lur_receiver_set_user_data(struct lur_receiver *lur, void *userdata);

/**
 * Get the caller-specific data associated with this object, if any.
 *
 * @param lur A valid receiver object
 * @return The caller-specific data previously assigned in
 * lur_receiver_new_from_hidraw (or lur_receiver_set_user_data()).
 */
void*
lur_receiver_get_user_data(const struct lur_receiver *lur);

/**
 * Add a reference to the device. A device is destroyed whenever the
 * reference count reaches 0. See @ref lur_unref.
 *
 * @param lur A valid device object
 * @return The passed device
 */
struct lur_device *
lur_device_ref(struct lur_device *dev);

/**
 * Dereference the device. After this, the device may have been
 * destroyed, if the last reference was dereferenced. If so, the device is
 * invalid and may not be interacted with.
 *
 * @param lur A valid device object
 * @retval NULL
 */
struct lur_device *
lur_device_unref(struct lur_device *dev);

/**
 * Set caller-specific data associated with this object. liblur does
 * not manage, look at, or modify this data. The caller must ensure the
 * data is valid.
 *
 * @param dev A valid device object
 * @param userdata Caller-specific data
 */
void
lur_device_set_user_data(struct lur_device *dev, void *userdata);

/**
 * Get the caller-specific data associated with this object, if any.
 *
 * @param dev A valid device object
 * @return The caller-specific data previously assigned in
 * lur_device_set_user_data().
 */
void*
lur_device_get_user_data(const struct lur_device *dev);

#ifdef __cplusplus
}
#endif
