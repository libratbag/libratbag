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

#include <hidpp10.h>

#include <libratbag-util.h>
#include <libratbag-hidraw.h>
#include <libratbag-private.h>

const char *hidpp_errors[0xFF] = {
	[0x00] = "ERR_SUCCESS",
	[0x01] = "ERR_INVALID_SUBID",
	[0x02] = "ERR_INVALID_ADDRESS",
	[0x03] = "ERR_INVALID_VALUE",
	[0x04] = "ERR_CONNECT_FAIL",
	[0x05] = "ERR_TOO_MANY_DEVICES",
	[0x06] = "ERR_ALREADY_EXISTS",
	[0x07] = "ERR_BUSY",
	[0x08] = "ERR_UNKNOWN_DEVICE",
	[0x09] = "ERR_RESOURCE_ERROR",
	[0x0A] = "ERR_REQUEST_UNAVAILABLE",
	[0x0B] = "ERR_INVALID_PARAM_VALUE",
	[0x0C] = "ERR_WRONG_PIN_CODE",
	[0x0D ... 0xFE] = NULL,
};

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


static int hidpp10_write_command(struct ratbag_device *device, __u8 *cmd, int size) {
	int res = ratbag_hidraw_output_report(device, cmd, size);

	if (res == 0)
		return 0;

	if (res < 0)
		log_error(device->ratbag, "Error: %s (%d)\n", strerror(-res), -res);

	return res;
}

int hidpp10_request_command(struct ratbag_device *device, union hidpp10_message *msg) {
	struct ratbag *ratbag = device->ratbag;
	union hidpp10_message read_buffer;
	union hidpp10_message expected_header;
	union hidpp10_message expected_error_recv = ERROR_MSG(msg, RECEIVER_IDX);
	union hidpp10_message expected_error_dev = ERROR_MSG(msg, msg->msg.device_idx);
	int ret;
	__u8 hidpp_err = 0;

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

int hidpp10_toggle_individual_feature(struct ratbag_device *device, struct hidpp10_device *dev, int feature_bit_r0, int feature_bit_r2) {
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

int hidpp10_open_lock(struct ratbag_device *device) {
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

static int hidpp10_get_device_info(struct ratbag_device *device, struct hidpp10_device *dev) {
	unsigned idx = dev->index;
	union hidpp10_message pairing_information = CMD_PAIRING_INFORMATION(idx, DEVICE_PAIRING_INFORMATION);
	union hidpp10_message device_name = CMD_PAIRING_INFORMATION(idx, DEVICE_NAME);
	union hidpp10_message firmware_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_NAME_AND_VERSION(1));
	union hidpp10_message build_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_BUILD_NUMBER(1));
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

	/*
	 * This may fail on some devices
	 * => we can not retrieve their FW version through HID++ 1.0.
	 */
	res = hidpp10_request_command(device, &firmware_information);
	if (res)
		return 0;

	dev->fw_major = firmware_information.msg.string[1];
	dev->fw_minor = firmware_information.msg.string[2];

	res = hidpp10_request_command(device, &build_information);
	if (res)
		return 0;

	dev->build = (build_information.msg.string[1] << 8) |
			build_information.msg.string[2];

	return 0;
}

int hidpp10_get_device_from_wpid(struct ratbag_device *device, uint16_t wpid, struct hidpp10_device *dev) {
	int i, res;

	for (i = 0; i < 6; i++) {
		res = hidpp10_get_device_from_idx(device, i, dev);
		if (res)
			continue;

		if (dev->wpid == wpid)
			break;

		res = -1;
	}

	return res;
}

int hidpp10_get_device_from_idx(struct ratbag_device *device, int idx, struct hidpp10_device *dev) {
	dev->index = idx;
	return hidpp10_get_device_info(device, dev);
}
