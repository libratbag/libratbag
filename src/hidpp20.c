/*
 * HID++ 2.0 library.
 *
 * Copyright 2015 Benjamin Tissoires <benjamin.tissoires@gmail.com>
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

/*
 * Based on the HID++ 2.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hidpp20.h"
#include "libratbag.h"
#include "libratbag-hidraw.h"
#include "libratbag-util.h"
#include "libratbag-private.h"

static int
hidpp20_write_command(struct ratbag_device *device, uint8_t *cmd, int size)
{
	int res = ratbag_hidraw_output_report(device, cmd, size);

	if (res == 0)
		return 0;

	if (res < 0)
		log_error(device->ratbag, "Error: %d\n", errno);

	return res;
}

int
hidpp20_request_command(struct ratbag_device *device, union hidpp20_message *msg)
{
	struct ratbag *ratbag = device->ratbag;
	union hidpp20_message read_buffer;
	int ret;
	uint8_t hidpp_err = 0;
	size_t msg_len;

	msg_len = msg->msg.report_id == REPORT_ID_SHORT ? SHORT_MESSAGE_LENGTH : LONG_MESSAGE_LENGTH;

	log_buf_debug(ratbag, "sending: ", msg->data, msg_len);

	/* Send the message to the Device */
	ret = hidpp20_write_command(device, msg->data, msg_len);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = ratbag_hidraw_read_input_report(device, read_buffer.data, LONG_MESSAGE_LENGTH);
		log_buf_debug(ratbag, " *** received: ", read_buffer.data, ret);

		if (read_buffer.msg.report_id != REPORT_ID_SHORT &&
		    read_buffer.msg.report_id != REPORT_ID_LONG)
			continue;

		/* actual answer */
		if (read_buffer.msg.sub_id == msg->msg.sub_id &&
		    read_buffer.msg.address == msg->msg.address)
			break;

		/* error */
		if (read_buffer.msg.sub_id == __ERROR_MSG &&
		    (read_buffer.msg.parameters[0] & 0x0f) == (read_buffer.msg.address & 0x0f)) {
			hidpp_err = read_buffer.msg.parameters[1];
			log_error(ratbag,
				"    HID++ error from the device (%d): %s (%02x)\n",
				read_buffer.msg.device_idx,
				hidpp_errors[hidpp_err] ? hidpp_errors[hidpp_err] : "Undocumented error code",
				hidpp_err);
			break;
		}
	} while (ret > 0);

	if (ret < 0) {
		log_error(ratbag, "    USB error: %d\n", errno);
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

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT					0x0000
#define HIDPP_PAGE_ROOT_IDX				0x00

#define CMD_ROOT_GET_FEATURE				0x08
#define CMD_ROOT_GET_PROTOCOL_VERSION			0x18

int
hidpp_root_get_feature(struct ratbag_device *device,
		       uint16_t feature,
		       uint8_t *feature_index,
		       uint8_t *feature_type,
		       uint8_t *feature_version)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = 0xff,
		.msg.sub_id = HIDPP_PAGE_ROOT_IDX,
		.msg.address = CMD_ROOT_GET_FEATURE,
		.msg.parameters[0] = feature >> 8,
		.msg.parameters[1] = feature & 0xff,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*feature_index = msg.msg.parameters[0];
	*feature_type = msg.msg.parameters[1];
	*feature_version = msg.msg.parameters[2];

	log_debug(device->ratbag, "feature 0x%04x is at 0x%02x\n", feature, *feature_index);
	return 0;
}

int
hidpp20_root_get_protocol_version(struct ratbag_device *device,
				  unsigned *major,
				  unsigned *minor)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = 0xff,
		.msg.sub_id = HIDPP_PAGE_ROOT_IDX,
		.msg.address = CMD_ROOT_GET_PROTOCOL_VERSION,
	};

	rc = hidpp20_request_command(device, &msg);

	if (rc == ERR_INVALID_SUBID) {
		*major = 1;
		*minor = 0;
		return 0;
	}

	if (rc == 0) {
		*major = msg.msg.parameters[0];
		*minor = msg.msg.parameters[1];
	}

	return rc;
}

/* -------------------------------------------------------------------------- */
/* 0x0001: Feature Set                                                        */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_FEATURE_SET				0x0001

#define CMD_FEATURE_SET_GET_COUNT			0x08
#define CMD_FEATURE_SET_GET_FEATURE_ID			0x18

static int
hidpp20_feature_set_get_count(struct ratbag_device *device, uint8_t reg)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = 0xff,
		.msg.sub_id = reg,
		.msg.address = CMD_FEATURE_SET_GET_COUNT,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[0];
}

static int
hidpp20_feature_set_get_feature_id(struct ratbag_device *device,
				   uint8_t reg,
				   uint8_t feature_index,
				   uint16_t *feature,
				   uint8_t *type)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = 0xff,
		.msg.sub_id = reg,
		.msg.address = CMD_FEATURE_SET_GET_FEATURE_ID,
		.msg.parameters[0] = feature_index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*feature = (msg.msg.parameters[0] << 8) | msg.msg.parameters[1];
	*type = msg.msg.parameters[2];

	return 0;
}

int hidpp20_feature_set_get(struct ratbag_device *device,
			    struct hidpp20_feature **feature_list)
{
	uint8_t feature_index, feature_type, feature_version;
	struct hidpp20_feature *flist;
	int rc;
	uint8_t feature_count;
	unsigned int i;

	rc = hidpp_root_get_feature(device,
				    HIDPP_PAGE_FEATURE_SET,
				    &feature_index,
				    &feature_type,
				    &feature_version);
	if (rc)
		return rc;

	rc = hidpp20_feature_set_get_count(device, feature_index);
	if (rc < 0)
		return rc;

	feature_count = (uint8_t)rc;

	if (!feature_count) {
		*feature_list = NULL;
		return 0;
	}

	flist = zalloc(feature_count * sizeof(struct hidpp20_feature));
	if (!flist)
		return -ENOMEM;

	for (i = 0; i < feature_count; i++) {
		rc = hidpp20_feature_set_get_feature_id(device,
							feature_index,
							i,
							&flist[i].feature,
							&flist[i].type);
		if (rc)
			goto err;
	}

	*feature_list = flist;
	return feature_count;
err:
	free(flist);
	return rc;
}
