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
#include <stdbool.h>

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
	bool led[4];
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
int hidpp10_open_lock(struct ratbag_device *device);
int hidpp10_disconnect(struct ratbag_device *device, int idx);
void hidpp10_list_devices(struct ratbag_device *device);
int hidpp10_get_device_from_wpid(struct ratbag_device *device, uint16_t wpid, struct hidpp10_device *dev);
int hidpp10_get_device_from_idx(struct ratbag_device *device, int idx, struct hidpp10_device *dev);

/* -------------------------------------------------------------------------- */
/* 0x01: Enable Individual Features                                           */
/* -------------------------------------------------------------------------- */
#define FEATURE_BIT_R0_SPECIAL_BUTTON_FUNCTION		1
#define FEATURE_BIT_R0_ENHANCED_KEY_USAGE		2
#define FEATURE_BIT_R0_FAST_FORWARD_REWIND		3
#define FEATURE_BIT_R0_SCROLLING_ACCELERATION		6
#define FEATURE_BIT_R0_BUTTONS_CONTROL_THE_RESOLUTION	7
#define FEATURE_BIT_R2_INHIBIT_LOCK_KEY_SOUND		0
#define FEATURE_BIT_R2_3D_ENGINE			2
#define FEATURE_BIT_R2_HOST_SW_CONTROLS_LEDS		3

int
hidpp10_get_individual_features(struct ratbag_device *device,
				struct hidpp10_device *dev,
				uint8_t *feature_bit_r0,
				uint8_t *feature_bit_r2);

int
hidpp10_set_individual_feature(struct ratbag_device *device,
			       struct hidpp10_device *dev,
			       uint8_t feature_bit_r0,
			       uint8_t feature_bit_r2);

/* -------------------------------------------------------------------------- */
/* 0x51: LED Status                                                           */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_led_status(struct ratbag_device *device,
		       struct hidpp10_device *dev,
		       bool led[4]);
int
hidpp10_set_led_status(struct ratbag_device *device,
		       struct hidpp10_device *dev,
		       const bool led[4]);

/* -------------------------------------------------------------------------- */
/* 0x63: Current Resolution                                                   */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_current_resolution(struct ratbag_device *device,
			       struct hidpp10_device *dev,
			       uint16_t *xres, uint16_t *yres);
/* -------------------------------------------------------------------------- */
/* 0xF1: Device Firmware Information                                          */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_firmare_information(struct ratbag_device *device,
				struct hidpp10_device *dev,
				uint8_t *major, uint8_t *minor, uint8_t *build_number);

/* -------------------------------------------------------------------------- */
/* 0xB5: Pairing Information                                                  */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_pairing_information(struct ratbag_device *device,
				struct hidpp10_device *dev,
				uint8_t *report_interval,
				uint16_t *wpid,
				uint8_t *device_type);
int
hidpp10_get_pairing_information_device_name(struct ratbag_device *device,
					    struct hidpp10_device *dev,
					    char *name,
					    size_t *name_sz);
#endif /* HIDPP_10_H */
