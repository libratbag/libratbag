/*
 * Logitech Unifying Receiver library - headers file.
 *
 * Copyright 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
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
