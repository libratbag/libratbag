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
#include "libratbag.h"

#define RECEIVER_IDX				0xFF

#define REPORT_ID_SHORT				0x10
#define REPORT_ID_LONG				0x11

#define SHORT_MESSAGE_LENGTH			7
#define LONG_MESSAGE_LENGTH			20

#define SET_REGISTER_REQ			0x80
#define SET_REGISTER_RSP			0x80
#define GET_REGISTER_REQ			0x81
#define GET_REGISTER_RSP			0x81
#define SET_LONG_REGISTER_REQ			0x82
#define SET_LONG_REGISTER_RSP			0x82
#define GET_LONG_REGISTER_REQ			0x83
#define GET_LONG_REGISTER_RSP			0x83
#define __ERROR_MSG				0x8F

#define __CMD_HIDPP_NOTIFICATIONS		0x00
#define FEATURE_BIT_R0_CONSUMER_SPECIFIC_CONTROL	0
#define FEATURE_BIT_R0_POWER_KEYS			1
#define FEATURE_BIT_R0_VERTICAL_SCROLL			2
#define FEATURE_BIT_R0_MOUSE_EXTRA_BUTTONS		3
#define FEATURE_BIT_R0_BATTERY_STATUS			4
#define FEATURE_BIT_R0_HORIZONTAL_SCROLL		5
#define FEATURE_BIT_R0_F_LOCK_STATUS			6
#define FEATURE_BIT_R0_NUMPAD_NUMERIC_KEYS		7
#define FEATURE_BIT_R2_3D_GESTURES			0

#define __CMD_ENABLE_INDIVIDUAL_FEATURES	0x01
#define FEATURE_BIT_R0_SPECIAL_BUTTON_FUNCTION		1
#define FEATURE_BIT_R0_ENHANCED_KEY_USAGE		2
#define FEATURE_BIT_R0_FAST_FORWARD_REWIND		3
#define FEATURE_BIT_R0_SCROLLING_ACCELERATION		6
#define FEATURE_BIT_R0_BUTTONS_CONTROL_THE_RESOLUTION	7
#define FEATURE_BIT_R2_INHIBIT_LOCK_KEY_SOUND		0
#define FEATURE_BIT_R2_3D_ENGINE			2
#define FEATURE_BIT_R2_HOST_SW_CONTROLS_LEDS		3

#define __CMD_DEVICE_CONNECTION_DISCONNECTION	0xB2
#define CONNECT_DEVICES_OPEN_LOCK			1
#define CONNECT_DEVICES_CLOSE_LOCK			2
#define CONNECT_DEVICES_DISCONNECT			3

#define __CMD_PAIRING_INFORMATION		0xB5
#define DEVICE_PAIRING_INFORMATION			0x20
#define DEVICE_EXTENDED_PAIRING_INFORMATION		0x30
#define DEVICE_NAME					0x40

#define __CMD_DEVICE_FIRMWARE_INFORMATION	0xF1
#define FIRMWARE_INFO_ITEM_FW_NAME_AND_VERSION(MCU)	((MCU - 1) << 4 | 0x01)
#define FIRMWARE_INFO_ITEM_FW_BUILD_NUMBER(MCU)		((MCU - 1) << 4 | 0x02)
#define FIRMWARE_INFO_ITEM_HW_VERSION(MCU)		((MCU - 1) << 4 | 0x03)
#define FIRMWARE_INFO_ITEM_BOOTLOADER_VERSION(MCU)	((MCU - 1) << 4 | 0x04)

#define __CMD_LED_STATUS			0x51

#define __CMD_CURRENT_RESOLUTION		0x63

#define __CMD_OPTICAL_SENSOR_SETTINGS		0x61

#define __CMD_USB_REFRESH_RATE			0x64

#define CMD_PAIRING_INFORMATION(idx, type)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = RECEIVER_IDX, \
		.sub_id = GET_LONG_REGISTER_REQ, \
		.address = __CMD_PAIRING_INFORMATION, \
		.parameters = {type + idx - 1, 0x00, 0x00 }, \
	} \
}

#define CMD_DEVICE_FIRMWARE_INFORMATION(idx, fw_info_item)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = GET_REGISTER_REQ, \
		.address = __CMD_DEVICE_FIRMWARE_INFORMATION, \
		.parameters = {fw_info_item, 0x00, 0x00 }, \
	} \
}

#define CMD_HIDPP_NOTIFICATIONS(idx, sub)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_HIDPP_NOTIFICATIONS, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

#define CMD_ENABLE_INDIVIDUAL_FEATURES(idx, sub)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_ENABLE_INDIVIDUAL_FEATURES, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

#define CMD_DEVICE_CONNECTION_DISCONNECTION(idx, cmd, timeout)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = RECEIVER_IDX, \
		.sub_id = SET_REGISTER_REQ, \
		.address = __CMD_DEVICE_CONNECTION_DISCONNECTION, \
		.parameters = {cmd, idx - 1, timeout }, \
	} \
}

#define CMD_LED_STATUS(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_LED_STATUS, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

#define CMD_CURRENT_RESOLUTION(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_CURRENT_RESOLUTION, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

#define CMD_USB_REFRESH_RATE(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_USB_REFRESH_RATE, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

#define CMD_OPTICAL_SENSOR_SETTINGS(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_OPTICAL_SENSOR_SETTINGS, \
		.parameters = {0x00, 0x00, 0x00}, \
	} \
}

#define ERR_SUCCESS				0x00
#define ERR_INVALID_SUBID			0x01
#define ERR_INVALID_ADDRESS			0x02
#define ERR_INVALID_VALUE			0x03
#define ERR_CONNECT_FAIL			0x04
#define ERR_TOO_MANY_DEVICES			0x05
#define ERR_ALREADY_EXISTS			0x06
#define ERR_BUSY				0x07
#define ERR_UNKNOWN_DEVICE			0x08
#define ERR_RESOURCE_ERROR			0x09
#define ERR_REQUEST_UNAVAILABLE			0x0A
#define ERR_INVALID_PARAM_VALUE			0x0B
#define ERR_WRONG_PIN_CODE			0x0C

#define ERROR_MSG(__hidpp_msg, idx)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = __ERROR_MSG, \
		.address = __hidpp_msg->msg.sub_id, \
		.parameters = {__hidpp_msg->msg.address, 0x00, 0x00 }, \
	} \
}

struct hidpp10_device  {
	unsigned index;
	char name[15];
	uint16_t wpid;
	uint8_t report_interval;
	uint8_t device_type;
	uint8_t fw_major;
	uint8_t fw_minor;
	uint8_t build;
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
};

union hidpp10_message {
	struct _hidpp10_message msg;
	uint8_t data[LONG_MESSAGE_LENGTH];
};

/* Note:
 * for all of these functions that takes a struct hidpp10_device * as parameter,
 * if you are already talking to the hidraw node associated to the actual device,
 * you can pass NULL instead (there is no need to retrieve it through
 * hidpp10_get_device_from_wpid first)
 */
int hidpp10_request_command(struct ratbag_device *device, union hidpp10_message *msg);
int hidpp10_toggle_individual_feature(struct ratbag_device *device, struct hidpp10_device *dev,
				      int feature_bit_r0, int feature_bit_r2);
int hidpp10_open_lock(struct ratbag_device *device);
int hidpp10_disconnect(struct ratbag_device *device, int idx);
void hidpp10_list_devices(struct ratbag_device *device);
int hidpp10_get_device_from_wpid(struct ratbag_device *device, uint16_t wpid, struct hidpp10_device *dev);
int hidpp10_get_device_from_idx(struct ratbag_device *device, int idx, struct hidpp10_device *dev);
#endif /* HIDPP_10_H */
