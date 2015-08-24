/*
 * HID++ 1.0 library.
 *
 * Copyright 2013-2015 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013-2015 Red Hat, Inc
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

#include "config.h"

#include <linux/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp10.h"

#include "libratbag-util.h"
#include "libratbag-hidraw.h"
#include "libratbag-private.h"

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

#define ERROR_MSG(__hidpp_msg, idx)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = __ERROR_MSG, \
		.address = __hidpp_msg->msg.sub_id, \
		.parameters = {__hidpp_msg->msg.address, 0x00, 0x00 }, \
	} \
}

const char *device_types[0xFF] = {
	[0x00] = "Unknown",
	[0x01] = "Keyboard",
	[0x02] = "Mouse",
	[0x03] = "Numpad",
	[0x04] = "Presenter",
	[0x05] = "Reserved for future",
	[0x06] = "ERR_ALREADY_EXISTS",
	[0x07] = "ERR_BUSY",
	[0x08] = "Trackball",
	[0x09] = "Touchpad",
	[0x0A ... 0xFE] = NULL,
};

static inline uint16_t
hidpp10_get_unaligned_u16le(uint8_t *buf)
{
	return (buf[1] << 8) | buf[0];
}

static int
hidpp10_write_command(struct ratbag_device *device, uint8_t *cmd, int size)
{
	int res = ratbag_hidraw_output_report(device, cmd, size);

	if (res == 0)
		return 0;

	if (res < 0)
		log_error(device->ratbag, "Error: %s (%d)\n", strerror(-res), -res);

	return res;
}

int
hidpp10_request_command(struct ratbag_device *device, union hidpp10_message *msg)
{
	struct ratbag *ratbag = device->ratbag;
	union hidpp10_message read_buffer;
	union hidpp10_message expected_header;
	union hidpp10_message expected_error_recv = ERROR_MSG(msg, RECEIVER_IDX);
	union hidpp10_message expected_error_dev = ERROR_MSG(msg, msg->msg.device_idx);
	int ret;
	uint8_t hidpp_err = 0;

	/* create the expected header */
	expected_header = *msg;
	switch (msg->msg.sub_id) {
	case SET_REGISTER_REQ:
		expected_header.msg.report_id = REPORT_ID_SHORT;
		break;
	case GET_REGISTER_REQ:
		expected_header.msg.report_id = REPORT_ID_SHORT;
		break;
	case SET_LONG_REGISTER_REQ:
		expected_header.msg.report_id = REPORT_ID_LONG;
		break;
	case GET_LONG_REGISTER_REQ:
		expected_header.msg.report_id = REPORT_ID_LONG;
		break;
	}

	log_buf_debug(ratbag, "sending: ", msg->data, SHORT_MESSAGE_LENGTH);
	log_buf_debug(ratbag, "  expected_header:	", expected_header.data, SHORT_MESSAGE_LENGTH);
	log_buf_debug(ratbag, "  expected_error_recv:	", expected_error_recv.data, SHORT_MESSAGE_LENGTH);
	log_buf_debug(ratbag, "  expected_error_dev:	", expected_error_dev.data, SHORT_MESSAGE_LENGTH);

	/* Send the message to the Device */
	ret = hidpp10_write_command(device, msg->data, SHORT_MESSAGE_LENGTH);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = ratbag_hidraw_read_input_report(device, read_buffer.data, LONG_MESSAGE_LENGTH);
		log_buf_debug(ratbag, " *** received: ", read_buffer.data, ret);
		/* actual answer */
		if (!memcmp(read_buffer.data, expected_header.data, 4))
			break;

		/* error */
		if (!memcmp(read_buffer.data, expected_error_recv.data, 5) ||
		    !memcmp(read_buffer.data, expected_error_dev.data, 5)) {
			hidpp_err = read_buffer.msg.parameters[1];
			log_error(ratbag,
				"    HID++ error from the %s (%d): %s (%02x)\n",
				read_buffer.msg.device_idx == RECEIVER_IDX ? "receiver" : "device",
				read_buffer.msg.device_idx,
				hidpp_errors[hidpp_err] ? hidpp_errors[hidpp_err] : "Undocumented error code",
				hidpp_err);
			break;
		}
	} while (ret > 0);

	if (ret < 0) {
		log_error(ratbag, "    USB error: %s (%d)\n", strerror(-ret), -ret);
		perror("write");
		goto out_err;
	}

	if (!hidpp_err) {
		log_buf_debug(ratbag, "    received: ", read_buffer.data, ret);
		/* copy the answer for the caller */
		*msg = read_buffer;
	}

	ret = hidpp_err;

out_err:
	return ret;
}

int
hidpp10_toggle_individual_feature(struct ratbag_device *device, struct hidpp10_device *dev,
				  int feature_bit_r0, int feature_bit_r2)
{
	unsigned idx = RECEIVER_IDX;
	union hidpp10_message mode = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, GET_REGISTER_REQ);
	int res;

	if (dev)
		mode.msg.device_idx = dev->index;

	/* first read the current values */
	res = hidpp10_request_command(device, &mode);
	if (res)
		return -1;

	/* toggle bits of r0 */
	if (feature_bit_r0 >= 0)
		mode.msg.parameters[0] ^= 1U << feature_bit_r0;

	/* toggle bits of r2 */
	if (feature_bit_r2 >= 0)
		mode.msg.parameters[2] ^= 1U << feature_bit_r2;

	/* now write back the change */
	mode.msg.sub_id = SET_REGISTER_REQ;
	res = hidpp10_request_command(device, &mode);
	return res;
}

int
hidpp10_open_lock(struct ratbag_device *device)
{
	union hidpp10_message open_lock = CMD_DEVICE_CONNECTION_DISCONNECTION(0x00, CONNECT_DEVICES_OPEN_LOCK, 0x08);

	return hidpp10_request_command(device, &open_lock);
}

int hidpp10_disconnect(struct ratbag_device *device, int idx) {
	union hidpp10_message disconnect = CMD_DEVICE_CONNECTION_DISCONNECTION(idx + 1, CONNECT_DEVICES_DISCONNECT, 0x00);

	return hidpp10_request_command(device, &disconnect);
}

void hidpp10_list_devices(struct ratbag_device *device) {
	struct hidpp10_device dev;
	int i, res;

	for (i = 0; i < 6; ++i) {
		res = hidpp10_get_device_from_idx(device, i, &dev);
		if (res)
			continue;

		log_info(device->ratbag, "[%d] %s	%s (Wireless PID: %04x)\n", i, device_types[dev.device_type] ? device_types[dev.device_type] : "", dev.name, dev.wpid);
	}
}

static int
hidpp10_get_pairing_information(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned int idx = dev->index;
	union hidpp10_message pairing_information = CMD_PAIRING_INFORMATION(idx, DEVICE_PAIRING_INFORMATION);
	union hidpp10_message device_name = CMD_PAIRING_INFORMATION(idx, DEVICE_NAME);
	size_t name_size;
	int res;

	res = hidpp10_request_command(device, &pairing_information);
	if (res)
		return -1;

	dev->report_interval = pairing_information.msg.string[2];
	dev->wpid = (pairing_information.msg.string[3] << 8) |
			pairing_information.msg.string[4];
	dev->device_type = pairing_information.msg.string[7];

	res = hidpp10_request_command(device, &device_name);
	if (res)
		return -1;
	name_size = device_name.msg.string[1];
	memcpy(dev->name, &device_name.msg.string[2], sizeof(device_name.msg.string));
	dev->name[min(name_size, sizeof(dev->name) - 1)] = '\0';

	return 0;
}

static int
hidpp10_get_firmare_information(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message firmware_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_NAME_AND_VERSION(1));
	union hidpp10_message build_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_BUILD_NUMBER(1));
	int res;

	/*
	 * This may fail on some devices
	 * => we can not retrieve their FW version through HID++ 1.0.
	 */
	res = hidpp10_request_command(device, &firmware_information);
	if (res == 0) {
		dev->fw_major = firmware_information.msg.string[1];
		dev->fw_minor = firmware_information.msg.string[2];
	}

	res = hidpp10_request_command(device, &build_information);
	if (res == 0) {
		dev->build = (build_information.msg.string[1] << 8) |
				build_information.msg.string[2];
	}

	return 0;
}

static int
hidpp10_get_individual_features(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message features = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, GET_REGISTER_REQ);
	int res;

	res = hidpp10_request_command(device, &features);
	if (res == 0) {
		/* do something */
	}

	return res;
}

static int
hidpp10_get_hidpp_notifications(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message notifications = CMD_HIDPP_NOTIFICATIONS(idx, GET_REGISTER_REQ);
	int res;

	res = hidpp10_request_command(device, &notifications);
	if (res == 0) {
		/* do something */
	}

	return res;
}

static int
hidpp10_get_current_resolution(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message resolution = CMD_CURRENT_RESOLUTION(idx, GET_LONG_REGISTER_REQ);
	int res;
	int xres, yres;

	res = hidpp10_request_command(device, &resolution);
	if (res)
		return res;

	/* resolution is in 50dpi multiples */
	xres = hidpp10_get_unaligned_u16le(&resolution.data[4]) * 50;
	yres = hidpp10_get_unaligned_u16le(&resolution.data[6]) * 50;

	log_debug(device->ratbag,
		  "Resolution is %dx%ddpi\n", xres, yres);

	return 0;
}

static int
hidpp10_get_led_status(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message led_status = CMD_LED_STATUS(idx, GET_REGISTER_REQ);
	int res;
	int led[4];

	res = hidpp10_request_command(device, &led_status);
	if (res)
		return res;

	/* each led is 4-bits, 0x1 == off, 0x2 == on */
	led[0] = !!(led_status.msg.parameters[0] & 0x02); /* running man logo */
	led[1] = !!(led_status.msg.parameters[0] & 0x20); /* lowest */
	led[2] = !!(led_status.msg.parameters[1] & 0x02); /* middle */
	led[3] = !!(led_status.msg.parameters[1] & 0x20); /* highest */

	log_debug(device->ratbag,
		  "LED status: 1:%s 2:%s 3:%s 4:%s\n",
		  led[0] ? "on" : "off",
		  led[1] ? "on" : "off",
		  led[2] ? "on" : "off",
		  led[3] ? "on" : "off");

	return 0;
}

static int
hidpp10_get_optical_sensor_settings(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message sensor = CMD_OPTICAL_SENSOR_SETTINGS(idx, GET_REGISTER_REQ);
	int res;

	res = hidpp10_request_command(device, &sensor);
	if (res)
		return res;

	/* Don't know what the return value is here */

	return 0;
}

static int
hidpp10_get_usb_refresh_rate(struct ratbag_device *device, struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message refresh = CMD_USB_REFRESH_RATE(idx, GET_REGISTER_REQ);
	int res;
	int rate;

	res = hidpp10_request_command(device, &refresh);
	if (res)
		return res;

	rate = 1000/refresh.msg.parameters[0];

	log_debug(device->ratbag, "Refresh rate: %dHz\n", rate);

	return 0;
}

static int
hidpp10_get_device_info(struct ratbag_device *device, struct hidpp10_device *dev)
{
	hidpp10_get_pairing_information(device, dev);
	hidpp10_get_firmare_information(device, dev);

	hidpp10_get_individual_features(device, dev);
	hidpp10_get_hidpp_notifications(device, dev);

	hidpp10_get_current_resolution(device, dev);
	hidpp10_get_led_status(device, dev);
	hidpp10_get_optical_sensor_settings(device, dev);
	hidpp10_get_usb_refresh_rate(device, dev);

	return 0;
}

int
hidpp10_get_device_from_wpid(struct ratbag_device *device, uint16_t wpid, struct hidpp10_device *dev)
{
	int i, res;

	for (i = 1; i < 7; i++) {
		res = hidpp10_get_device_from_idx(device, i, dev);
		if (res)
			continue;

		if (dev->wpid == wpid)
			break;

		res = -1;
	}

	return res;
}

int
hidpp10_get_device_from_idx(struct ratbag_device *device, int idx, struct hidpp10_device *dev)
{
	dev->index = idx;
	return hidpp10_get_device_info(device, dev);
}
