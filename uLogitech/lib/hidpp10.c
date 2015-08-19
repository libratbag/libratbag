/*
 * HID++ 1.0 library.
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

#include <linux/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <debug.h>
#include <hidpp10.h>

#define MIN(a,b)                 \
    ({ __typeof__ (a) _a = (a);  \
       __typeof__ (b) _b = (b);  \
       _a < _b ? _a : _b;        \
     })

#if DEBUG_LVL > 0
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
#endif

const char *device_types[0xFF] = {
	[0x00] = "Unknown",
	[0x01] = "Keyboard",
	[0x02] = "Mouse",
	[0x03] = "Numpad",
	[0x04] = "Presenter",
	[0x05 ... 0x07] = "Reserved for future",
	[0x06] = "ERR_ALREADY_EXISTS",
	[0x07] = "ERR_BUSY",
	[0x08] = "Trackball",
	[0x09] = "Touchpad",
	[0x0A ... 0xFE] = NULL,
};


static int hidpp10_write_command(int fd, __u8 *cmd, int size) {
	int res = write(fd, cmd, size);

	if (res == size)
		return 0;

	if (res < 0) {
		printf("Error: %d\n", errno);
		perror("write");
	} else {
		errno = ENOMEM;
		printf("write: %d were written instead of %d.\n", res, size);
		perror("write");
	}

	return res;
}

int hidpp10_request_command(int fd, union hidpp10_message *msg) {
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

	pr_dbg("sending: "); pr_buffer(msg->data, SHORT_MESSAGE_LENGTH);
#if DEBUG_LVL > 1
	pr_dbg("  expected_header:	"); pr_buffer(expected_header.data, SHORT_MESSAGE_LENGTH);
	pr_dbg("  expected_error_recv:	"); pr_buffer(expected_error_recv.data, SHORT_MESSAGE_LENGTH);
	pr_dbg("  expected_error_dev:	"); pr_buffer(expected_error_dev.data, SHORT_MESSAGE_LENGTH);
#endif

	/* Send the message to the Device */
	ret = hidpp10_write_command(fd, msg->data, SHORT_MESSAGE_LENGTH);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = read(fd, read_buffer.data, LONG_MESSAGE_LENGTH);
#if DEBUG_LVL > 2
		printf(" *** received: "); pr_buffer(read_buffer.data, ret);
#endif
		/* actual answer */
		if (!memcmp(read_buffer.data, expected_header.data, 4))
			break;

		/* error */
		if (!memcmp(read_buffer.data, expected_error_recv.data, 5) ||
		    !memcmp(read_buffer.data, expected_error_dev.data, 5)) {
			hidpp_err = read_buffer.msg.parameters[1];
			pr_dbg("    HID++ error from the %s (%d): %s (%02x)\n",
				read_buffer.msg.device_idx == RECEIVER_IDX ? "receiver" : "device",
				read_buffer.msg.device_idx,
				hidpp_errors[hidpp_err] ? hidpp_errors[hidpp_err] : "Undocumented error code",
				hidpp_err);
			break;
		}
	} while (ret > 0);

	if (ret < 0) {
		printf("    USB error: %d\n", errno);
		perror("write");
		goto out_err;
	}

	if (!hidpp_err) {
		pr_dbg("    received: "); pr_buffer(read_buffer.data, ret);
		/* copy the answer for the caller */
		*msg = read_buffer;
	}

	ret = hidpp_err;

out_err:
	return ret;
}

int hidpp10_toggle_individual_feature(int fd, struct unifying_device *dev, int feature_bit_r0, int feature_bit_r2) {
	unsigned idx = dev->index;
	union hidpp10_message mode = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, GET_REGISTER_REQ);
	int res;

	/* first read the current values */
	res = hidpp10_request_command(fd, &mode);
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
	res = hidpp10_request_command(fd, &mode);
	return res;
}

int hidpp10_open_lock(int fd) {
	union hidpp10_message open_lock = CMD_DEVICE_CONNECTION_DISCONNECTION(0x00, CONNECT_DEVICES_OPEN_LOCK, 0x08);

	return hidpp10_request_command(fd, &open_lock);
}

int hidpp10_disconnect(int fd, int idx) {
	union hidpp10_message disconnect = CMD_DEVICE_CONNECTION_DISCONNECTION(idx + 1, CONNECT_DEVICES_DISCONNECT, 0x00);

	return hidpp10_request_command(fd, &disconnect);
}

void hidpp10_list_devices(int fd) {
	struct unifying_device dev;
	int i, res;

	for (i = 0; i < 6; ++i) {
		res = hidpp10_get_device_from_idx(fd, i, &dev);
		if (res)
			continue;

		printf("[%d] %s	%s (Wireless PID: %04x)\n", i, device_types[dev.device_type] ? device_types[dev.device_type] : "", dev.name, dev.wpid);
	}
}

static int hidpp10_get_device_info(int fd, struct unifying_device *dev) {
	unsigned idx = dev->index;
	union hidpp10_message pairing_information = CMD_PAIRING_INFORMATION(idx, DEVICE_PAIRING_INFORMATION);
	union hidpp10_message device_name = CMD_PAIRING_INFORMATION(idx, DEVICE_NAME);
	union hidpp10_message firmware_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_NAME_AND_VERSION(1));
	union hidpp10_message build_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_BUILD_NUMBER(1));
	int name_size;
	int res;

	res = hidpp10_request_command(fd, &pairing_information);
	if (res)
		return -1;

	dev->report_interval = pairing_information.msg.string[2];
	dev->wpid = (pairing_information.msg.string[3] << 8) |
			pairing_information.msg.string[4];
	dev->device_type = pairing_information.msg.string[7];

	res = hidpp10_request_command(fd, &device_name);
	if (res)
		return -1;

	name_size = device_name.msg.string[1];
	memcpy(dev->name, &device_name.msg.string[2], sizeof(device_name.msg.string));
	dev->name[MIN(name_size, sizeof(dev->name) - 1)] = '\0';

	/*
	 * This may fail on some devices
	 * => we can not retrieve their FW version through HID++ 1.0.
	 */
	res = hidpp10_request_command(fd, &firmware_information);
	if (res)
		return 0;

	dev->fw_major = firmware_information.msg.string[1];
	dev->fw_minor = firmware_information.msg.string[2];

	res = hidpp10_request_command(fd, &build_information);
	if (res)
		return 0;

	dev->build = (build_information.msg.string[1] << 8) |
			build_information.msg.string[2];

	return 0;
}

int hidpp10_get_device_from_wpid(int fd, __u16 wpid, struct unifying_device *dev) {
	int i, res;

	for (i = 0; i < 6; i++) {
		res = hidpp10_get_device_from_idx(fd, i, dev);
		if (res)
			continue;

		if (dev->wpid == wpid)
			break;

		res = -1;
	}

	return res;
}

int hidpp10_get_device_from_idx(int fd, int idx, struct unifying_device *dev) {
	dev->index = idx;
	return hidpp10_get_device_info(fd, dev);
}
