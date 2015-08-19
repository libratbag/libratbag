/*
 * HID++ 1.0 library - headers file.
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

#include <debug.h>
#include <unifying.h>

#ifndef HIDPP_10_H
#define HIDPP_10_H

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

#define CMD_PAIRING_INFORMATION(idx, type)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = RECEIVER_IDX, \
		.sub_id = GET_LONG_REGISTER_REQ, \
		.address = __CMD_PAIRING_INFORMATION, \
		.parameters = {type + idx, 0x00, 0x00 }, \
	} \
}

#define CMD_DEVICE_FIRMWARE_INFORMATION(idx, fw_info_item)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx + 1, \
		.sub_id = GET_REGISTER_REQ, \
		.address = __CMD_DEVICE_FIRMWARE_INFORMATION, \
		.parameters = {fw_info_item, 0x00, 0x00 }, \
	} \
}

#define CMD_ENABLE_INDIVIDUAL_FEATURES(idx, sub)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx + 1, \
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
		.parameters = {cmd, idx, timeout }, \
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

struct _hidpp10_message {
	__u8 report_id;
	__u8 device_idx;
	__u8 sub_id;
	__u8 address;
	union {
		__u8 parameters[3];
		__u8 string[16];
	};
};

union hidpp10_message {
	struct _hidpp10_message msg;
	__u8 data[LONG_MESSAGE_LENGTH];
};


int hidpp10_request_command(int fd, union hidpp10_message *msg);
int hidpp10_toggle_individual_feature(int fd, struct unifying_device *dev, int feature_bit_r0, int feature_bit_r2);
int hidpp10_open_lock(int fd);
int hidpp10_disconnect(int fd, int idx);
void hidpp10_list_devices(int fd);
int hidpp10_get_device_from_wpid(int fd, __u16 wpid, struct unifying_device *dev);
int hidpp10_get_device_from_idx(int fd, int idx, struct unifying_device *dev);
#endif /* HIDPP_10_H */
