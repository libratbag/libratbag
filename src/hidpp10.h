/*
 * HID++ 1.0 library - headers file.
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

#ifndef HIDPP_10_H
#define HIDPP_10_H

#include <stdint.h>

#include "hidpp-generic.h"
#include "libratbag.h"

struct hidpp10_device  {
	unsigned index;
	char name[15];
	uint16_t wpid;
	uint8_t report_interval;
	uint8_t device_type;
	uint8_t fw_major;
	uint8_t fw_minor;
	uint8_t build;
	uint16_t xres, yres;
	uint16_t refresh_rate;
};

struct _hidpp10_message {
	uint8_t report_id;
	uint8_t device_idx;
	uint8_t sub_id;
	uint8_t address;
	union {
		uint8_t parameters[3];
		uint8_t string[16];
	};
} __attribute__((packed));

union hidpp10_message {
	struct _hidpp10_message msg;
	uint8_t data[LONG_MESSAGE_LENGTH];
};

int hidpp10_request_command(struct ratbag_device *device, union hidpp10_message *msg);
int hidpp10_toggle_individual_feature(struct ratbag_device *device, struct hidpp10_device *dev,
				      int feature_bit_r0, int feature_bit_r2);
int hidpp10_open_lock(struct ratbag_device *device);
int hidpp10_disconnect(struct ratbag_device *device, int idx);
void hidpp10_list_devices(struct ratbag_device *device);
int hidpp10_get_device_from_wpid(struct ratbag_device *device, uint16_t wpid, struct hidpp10_device *dev);
int hidpp10_get_device_from_idx(struct ratbag_device *device, int idx, struct hidpp10_device *dev);

/* -------------------------------------------------------------------------- */
/* 0xF1: Device Firmware Information                                          */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_firmare_information(struct ratbag_device *device,
				struct hidpp10_device *dev,
				uint8_t *major, uint8_t *minor, uint8_t *build_number);

#endif /* HIDPP_10_H */
