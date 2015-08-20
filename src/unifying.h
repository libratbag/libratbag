/*
 * Logitech Unifying Receiver library - headers file.
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

#ifndef UNIFYING_H
#define UNIFYING_H

#define USB_VENDOR_ID_LOGITECH			(__u32)0x046d
#define USB_DEVICE_ID_UNIFYING_RECEIVER		(__s16)0xc52b
#define USB_DEVICE_ID_UNIFYING_RECEIVER_2	(__s16)0xc532

struct unifying_device {
	unsigned index;
	char name[15];
	__u16 wpid;
	__u8 report_interval;
	__u8 device_type;
	__u8 fw_major;
	__u8 fw_minor;
	__u16 build;
};

int unifying_open_receiver(const char *hidraw);

/**
 * Scans all /dev/hidraw*, open the first unifying receiver.
 *
 * @return The hidraw device file descriptor of the first unifying receiver.
 */
int unifying_find_receiver(void);

#endif /* UNIFYING_H */
