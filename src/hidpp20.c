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

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "hidpp20.h"
#include "libratbag.h"
#include "libratbag-hidraw.h"
#include "libratbag-util.h"
#include "libratbag-private.h"

const char*
hidpp20_feature_get_name(uint16_t feature)
{
	static char numeric[8];
	const char *str;
#define CASE_RETURN_STRING(a) case a: return #a; break

	switch(feature)
	{
	CASE_RETURN_STRING(HIDPP_PAGE_ROOT);
	CASE_RETURN_STRING(HIDPP_PAGE_FEATURE_SET);
	CASE_RETURN_STRING(HIDPP_PAGE_DEVICE_INFO);
	CASE_RETURN_STRING(HIDPP_PAGE_BATTERY_LEVEL_STATUS);
	CASE_RETURN_STRING(HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS);
	CASE_RETURN_STRING(HIDPP_PAGE_SPECIAL_KEYS_BUTTONS);
	CASE_RETURN_STRING(HIDPP_PAGE_MOUSE_POINTER_BASIC);
	CASE_RETURN_STRING(HIDPP_PAGE_ADJUSTABLE_DPI);
	CASE_RETURN_STRING(HIDPP_PAGE_ADJUSTABLE_REPORT_RATE);
	CASE_RETURN_STRING(HIDPP_PAGE_COLOR_LED_EFFECTS);
	CASE_RETURN_STRING(HIDPP_PAGE_ONBOARD_PROFILES);
	CASE_RETURN_STRING(HIDPP_PAGE_MOUSE_BUTTON_SPY);
	default:
		sprintf_safe(numeric, "%#4x", feature);
		str = numeric;
		break;
	}

#undef CASE_RETURN_STRING
	return str;
}

static int
hidpp20_request_command_allow_error(struct hidpp20_device *device, union hidpp20_message *msg,
				    bool allow_error)
{
	union hidpp20_message read_buffer;
	int ret;
	uint8_t hidpp_err = 0;
	size_t msg_len;

	/* msg->address is 4 MSB: subcommand, 4 LSB: 4-bit SW identifier so
	 * the device knows who to respond to. kernel uses 0x1 */
	const int DEVICE_SW_ID = 0x8;

	if (msg->msg.address & 0xf) {
		hidpp_log_raw(&device->base, "hidpp20 error: sw address is already set\n");
		return -EINVAL;
	}
	msg->msg.address |= DEVICE_SW_ID;

	msg_len = msg->msg.report_id == REPORT_ID_SHORT ? SHORT_MESSAGE_LENGTH : LONG_MESSAGE_LENGTH;

	/* Send the message to the Device */
	ret = hidpp_write_command(&device->base, msg->data, msg_len);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = hidpp_read_response(&device->base, read_buffer.data, LONG_MESSAGE_LENGTH);

		/* Wait and retry if the USB timed out */
		if (ret == -ETIMEDOUT) {
			msleep(10);
			ret = hidpp_read_response(&device->base, read_buffer.data, LONG_MESSAGE_LENGTH);
		}

		if (read_buffer.msg.report_id != REPORT_ID_SHORT &&
		    read_buffer.msg.report_id != REPORT_ID_LONG)
			continue;

		/* actual answer */
		if (read_buffer.msg.sub_id == msg->msg.sub_id &&
		    read_buffer.msg.address == msg->msg.address)
			break;

		/* error */
		if ((read_buffer.msg.sub_id == __ERROR_MSG ||
		     read_buffer.msg.sub_id == 0xff) &&
		    read_buffer.msg.address == msg->msg.sub_id &&
		    read_buffer.msg.parameters[0] == msg->msg.address) {
			hidpp_err = read_buffer.msg.parameters[1];
			if (allow_error)
				hidpp_log_debug(&device->base,
						"    HID++ error from the device (%d): %s (%02x)\n",
						read_buffer.msg.device_idx,
						hidpp_errors[hidpp_err] ? hidpp_errors[hidpp_err] : "Undocumented error code",
						hidpp_err);
			else
				hidpp_log_error(&device->base,
						"    HID++ error from the device (%d): %s (%02x)\n",
						read_buffer.msg.device_idx,
						hidpp_errors[hidpp_err] ? hidpp_errors[hidpp_err] : "Undocumented error code",
						hidpp_err);
			break;
		}
	} while (ret > 0);

	if (ret < 0) {
		hidpp_log_error(&device->base, "    USB error: %s (%d)\n", strerror(-ret), -ret);
		perror("write");
		goto out_err;
	}

	if (!hidpp_err) {
		/* copy the answer for the caller */
		*msg = read_buffer;
	}

	ret = hidpp_err;

out_err:
	return ret;
}

int
hidpp20_request_command(struct hidpp20_device *device, union hidpp20_message *msg)
{
	return hidpp20_request_command_allow_error(device, msg, false);
}

static inline uint16_t
hidpp20_get_unaligned_u16(uint8_t *buf)
{
	return (buf[0] << 8) | buf[1];
}

static inline uint16_t
hidpp20_get_unaligned_be_u16(uint8_t *buf)
{
	return (buf[1] << 8) | buf[0];
}

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT_IDX				0x00

#define CMD_ROOT_GET_FEATURE				0x00
#define CMD_ROOT_GET_PROTOCOL_VERSION			0x10

/**
 * Returns the feature index or 0x00 if it is not found.
 */
static uint8_t
hidpp_root_get_feature_idx(struct hidpp20_device *device,
			   uint16_t feature)
{
	unsigned i;

	/* error or not, we should not ask for feature 0 */
	if (feature == 0x0000)
		return 0;

	/* feature 0x0000 is always at 0 */
	for (i = 1; i < device->feature_count; i++) {
		if (device->feature_list[i].feature == feature)
			return i;
	}

	return 0;
}


int
hidpp_root_get_feature(struct hidpp20_device *device,
		       uint16_t feature,
		       uint8_t *feature_index,
		       uint8_t *feature_type,
		       uint8_t *feature_version)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
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

	hidpp_log_raw(&device->base, "feature 0x%04x is at 0x%02x\n", feature, *feature_index);
	return 0;
}

int
hidpp20_root_get_protocol_version(struct hidpp20_device *device,
				  unsigned *major,
				  unsigned *minor)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = HIDPP_PAGE_ROOT_IDX,
		.msg.address = CMD_ROOT_GET_PROTOCOL_VERSION,
	};

	rc = hidpp20_request_command_allow_error(device, &msg, true);

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

#define CMD_FEATURE_SET_GET_COUNT			0x00
#define CMD_FEATURE_SET_GET_FEATURE_ID			0x10

static int
hidpp20_feature_set_get_count(struct hidpp20_device *device, uint8_t reg)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_FEATURE_SET_GET_COUNT,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[0];
}

static int
hidpp20_feature_set_get_feature_id(struct hidpp20_device *device,
				   uint8_t reg,
				   uint8_t feature_index,
				   uint16_t *feature,
				   uint8_t *type)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_FEATURE_SET_GET_FEATURE_ID,
		.msg.parameters[0] = feature_index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*feature = hidpp20_get_unaligned_u16(msg.msg.parameters);
	*type = msg.msg.parameters[2];

	return 0;
}

/**
 * allocates the list of features.
 *
 * returns 0 or a negative error
 */
static int
hidpp20_feature_set_get(struct hidpp20_device *device)
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

	if (!feature_count)
		return -ENOTSUP;

	flist = zalloc(feature_count * sizeof(struct hidpp20_feature));

	for (i = 0; i < feature_count; i++) {
		rc = hidpp20_feature_set_get_feature_id(device,
							feature_index,
							i,
							&flist[i].feature,
							&flist[i].type);
		if (rc)
			goto err;
	}

	device->feature_list = flist;
	device->feature_count = feature_count;

	return 0;
err:
	free(flist);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* 0x1000: Battery level status                                               */
/* -------------------------------------------------------------------------- */

#define CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_LEVEL_STATUS	0x00
#define CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_CAPABILITY		0x10

int
hidpp20_batterylevel_get_battery_level(struct hidpp20_device *device,
				       uint16_t *level,
				       uint16_t *next_level)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_BATTERY_LEVEL_STATUS_GET_BATTERY_LEVEL_STATUS,
	};
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_BATTERY_LEVEL_STATUS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*level = msg.msg.parameters[0];
	*next_level = msg.msg.parameters[1];

	return msg.msg.parameters[2];
}

/* -------------------------------------------------------------------------- */
/* 0x1b00: KBD reprogrammable keys and mouse buttons                          */
/* -------------------------------------------------------------------------- */

#define CMD_KBD_REPROGRAMMABLE_KEYS_GET_COUNT		0x00
#define CMD_KBD_REPROGRAMMABLE_KEYS_GET_CTRL_ID_INFO	0x10

static int
hidpp20_kbd_reprogrammable_keys_get_count(struct hidpp20_device *device, uint8_t reg)
{
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_KBD_REPROGRAMMABLE_KEYS_GET_COUNT,
	};
	int rc;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[0];
}

static int
hidpp20_kbd_reprogrammable_keys_get_info(struct hidpp20_device *device,
					 uint8_t reg,
					 struct hidpp20_control_id *control)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_KBD_REPROGRAMMABLE_KEYS_GET_CTRL_ID_INFO,
		.msg.parameters[0] = control->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->control_id = hidpp20_get_unaligned_u16(&msg.msg.parameters[0]);
	control->task_id = hidpp20_get_unaligned_u16(&msg.msg.parameters[2]);
	control->flags = msg.msg.parameters[4];

	return 0;
}

int
hidpp20_kbd_reprogrammable_keys_get_controls(struct hidpp20_device *device,
					     struct hidpp20_control_id **controls_list)
{
	uint8_t feature_index;
	struct hidpp20_control_id *c_list, *control;
	uint8_t num_controls;
	unsigned i;
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS);
	if (feature_index == 0)
		return -ENOTSUP;

	rc = hidpp20_kbd_reprogrammable_keys_get_count(device, feature_index);
	if (rc < 0)
		return rc;

	num_controls = rc;
	if (num_controls == 0) {
		*controls_list = NULL;
		return 0;
	}

	c_list = zalloc(num_controls * sizeof(struct hidpp20_control_id));

	for (i = 0; i < num_controls; i++) {
		control = &c_list[i];
		control->index = i;
		rc = hidpp20_kbd_reprogrammable_keys_get_info(device,
							      feature_index,
							      control);
		if (rc)
			goto err;

		/* 0x1b00 and 0x1b04 have the same control/task id mappings.
		 * I hope */
		hidpp_log_raw(&device->base,
			      "control %d: cid: '%s' (%d) tid: '%s' (%d) flags: 0x%02x\n",
			      control->index,
			      hidpp20_1b04_get_logical_mapping_name(control->control_id),
			      control->control_id,
			      hidpp20_1b04_get_physical_mapping_name(control->task_id),
			      control->task_id,
			      control->flags);
	}

	*controls_list = c_list;
	return num_controls;
err:
	free(c_list);
	return rc;
}

/* -------------------------------------------------------------------------- */
/* 0x1b04: Special keys and mouse buttons                                     */
/* -------------------------------------------------------------------------- */

#define CMD_SPECIAL_KEYS_BUTTONS_GET_COUNT		0x00
#define CMD_SPECIAL_KEYS_BUTTONS_GET_INFO		0x10
#define CMD_SPECIAL_KEYS_BUTTONS_GET_REPORTING		0x20
#define CMD_SPECIAL_KEYS_BUTTONS_SET_REPORTING		0x30

static int
hidpp20_special_keys_buttons_get_count(struct hidpp20_device *device, uint8_t reg)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_SPECIAL_KEYS_BUTTONS_GET_COUNT,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[0];
}

static int
hidpp20_special_keys_buttons_get_info(struct hidpp20_device *device,
				    uint8_t reg,
				    struct hidpp20_control_id *control)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_SPECIAL_KEYS_BUTTONS_GET_INFO,
		.msg.parameters[0] = control->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->control_id = hidpp20_get_unaligned_u16(&msg.msg.parameters[0]);
	control->task_id = hidpp20_get_unaligned_u16(&msg.msg.parameters[2]);
	control->flags = msg.msg.parameters[4];
	control->position = msg.msg.parameters[5];
	control->group = msg.msg.parameters[6];
	control->group_mask = msg.msg.parameters[7];
	control->raw_XY = msg.msg.parameters[8] & 0x01;

	return 0;
}


static int
hidpp20_special_keys_buttons_get_reporting(struct hidpp20_device *device,
					   uint8_t reg,
					   struct hidpp20_control_id *control)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_SPECIAL_KEYS_BUTTONS_GET_REPORTING,
		.msg.parameters[0] = control->control_id >> 8,
		.msg.parameters[1] = control->control_id & 0xff,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->reporting.remapped = hidpp20_get_unaligned_u16(&msg.msg.parameters[3]);
	control->reporting.raw_XY = !!(msg.msg.parameters[2] & 0x10);
	control->reporting.persist = !!(msg.msg.parameters[2] & 0x04);
	control->reporting.divert = !!(msg.msg.parameters[2] & 0x01);

	return 0;
}

int hidpp20_special_key_mouse_get_controls(struct hidpp20_device *device,
					   struct hidpp20_control_id **controls_list)
{
	uint8_t feature_index;
	struct hidpp20_control_id *c_list, *control;
	uint8_t num_controls;
	unsigned i;
	int rc;


	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_SPECIAL_KEYS_BUTTONS);
	if (feature_index == 0)
		return -ENOTSUP;

	rc = hidpp20_special_keys_buttons_get_count(device, feature_index);
	if (rc < 0)
		return rc;

	num_controls = rc;
	if (num_controls == 0) {
		*controls_list = NULL;
		return 0;
	}

	c_list = zalloc(num_controls * sizeof(struct hidpp20_control_id));

	for (i = 0; i < num_controls; i++) {
		control = &c_list[i];
		control->index = i;
		rc = hidpp20_special_keys_buttons_get_info(device,
							   feature_index,
							   control);
		if (rc)
			goto err;

		rc = hidpp20_special_keys_buttons_get_reporting(device,
								feature_index,
								control);
		if (rc)
			goto err;

		hidpp_log_raw(&device->base,
			      "control %d: cid: '%s' (%d) tid: '%s' (%d) flags: 0x%02x pos: %d group: %d gmask: 0x%02x raw_XY: %s\n"
			      "      reporting: raw_xy: %s persist: %s divert: %s remapped: '%s' (%d)\n",
			      control->index,
			      hidpp20_1b04_get_logical_mapping_name(control->control_id),
			      control->control_id,
			      hidpp20_1b04_get_physical_mapping_name(control->task_id),
			      control->task_id,
			      control->flags,
			      control->position,
			      control->group,
			      control->group_mask,
			      control->raw_XY ? "yes" : "no",
			      control->reporting.raw_XY ? "yes" : "no",
			      control->reporting.persist ? "yes" : "no",
			      control->reporting.divert ? "yes" : "no",
			      hidpp20_1b04_get_logical_mapping_name(control->reporting.remapped),
			      control->reporting.remapped);
	}

	*controls_list = c_list;
	return num_controls;
err:
	free(c_list);
	return rc;
}

int
hidpp20_special_key_mouse_set_control(struct hidpp20_device *device,
				      struct hidpp20_control_id *control)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_SPECIAL_KEYS_BUTTONS_SET_REPORTING,
		.msg.parameters[0] = control->control_id >> 8,
		.msg.parameters[1] = control->control_id & 0xff,
		.msg.parameters[2] = 0x00,
		.msg.parameters[3] = control->reporting.remapped >> 8,
		.msg.parameters[4] = control->reporting.remapped & 0xff,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_SPECIAL_KEYS_BUTTONS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;
	if (control->reporting.divert)
		msg.msg.parameters[2] |= 0x03;
	if (control->reporting.persist)
		msg.msg.parameters[2] |= 0x0c;
	if (control->reporting.raw_XY)
		msg.msg.parameters[2] |= 0x20;

	return hidpp20_request_command(device, &msg);
}

/* -------------------------------------------------------------------------- */
/* 0x2200: Mouse Pointer Basic Optical Sensors                                */
/* -------------------------------------------------------------------------- */

#define CMD_MOUSE_POINTER_BASIC_GET_INFO		0x00

int
hidpp20_mousepointer_get_mousepointer_info(struct hidpp20_device *device,
					   uint16_t *resolution,
					   uint8_t *flags)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_MOUSE_POINTER_BASIC_GET_INFO,
	};
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_MOUSE_POINTER_BASIC);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*resolution = hidpp20_get_unaligned_u16(msg.msg.parameters);
	*flags = msg.msg.parameters[2];

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x2201: Adjustable DPI                                                     */
/* -------------------------------------------------------------------------- */

#define CMD_ADJUSTABLE_DPI_GET_SENSOR_COUNT		0x00
#define CMD_ADJUSTABLE_DPI_GET_SENSOR_DPI_LIST		0x10
#define CMD_ADJUSTABLE_DPI_GET_SENSOR_DPI		0x20
#define CMD_ADJUSTABLE_DPI_SET_SENSOR_DPI		0x30

static int
hidpp20_adjustable_dpi_get_count(struct hidpp20_device *device, uint8_t reg)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_ADJUSTABLE_DPI_GET_SENSOR_COUNT,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[0];
}

static int
hidpp20_adjustable_dpi_get_dpi_list(struct hidpp20_device *device,
				    uint8_t reg,
				    struct hidpp20_sensor *sensor)
{
	int rc;
	unsigned i = 1, dpi_index = 0;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_ADJUSTABLE_DPI_GET_SENSOR_DPI_LIST,
		.msg.parameters[0] = sensor->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	sensor->dpi_min = 0xffff;

	sensor->index = msg.msg.parameters[0];
	while (i < LONG_MESSAGE_LENGTH - 4U &&
	       hidpp20_get_unaligned_u16(&msg.msg.parameters[i]) != 0) {
		uint16_t value = hidpp20_get_unaligned_u16(&msg.msg.parameters[i]);

		if (value > 0xe000) {
			sensor->dpi_steps = value - 0xe000;
		} else {
			sensor->dpi_min = value < sensor->dpi_min ? value : sensor->dpi_min;
			sensor->dpi_max = value > sensor->dpi_max ? value : sensor->dpi_max;
			sensor->dpi_list[dpi_index++] = value;
		}
		assert(sensor->dpi_list[dpi_index] == 0x0000);
		i += 2;
	}

	return 0;
}


static int
hidpp20_adjustable_dpi_get_dpi(struct hidpp20_device *device,
			       uint8_t reg,
			       struct hidpp20_sensor *sensor)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_ADJUSTABLE_DPI_GET_SENSOR_DPI,
		.msg.parameters[0] = sensor->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	sensor->dpi = hidpp20_get_unaligned_u16(&msg.msg.parameters[1]);
	sensor->default_dpi = hidpp20_get_unaligned_u16(&msg.msg.parameters[3]);

	return 0;
}

int hidpp20_adjustable_dpi_get_sensors(struct hidpp20_device *device,
				       struct hidpp20_sensor **sensors_list)
{
	uint8_t feature_index;
	struct hidpp20_sensor *s_list, *sensor;
	uint8_t num_sensors;
	unsigned i;
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_DPI);
	if (feature_index == 0)
		return -ENOTSUP;

	rc = hidpp20_adjustable_dpi_get_count(device, feature_index);
	if (rc < 0)
		return rc;

	num_sensors = rc;
	if (num_sensors == 0) {
		*sensors_list = NULL;
		return 0;
	}

	s_list = zalloc(num_sensors * sizeof(struct hidpp20_sensor));

	for (i = 0; i < num_sensors; i++) {
		sensor = &s_list[i];
		sensor->index = i;
		rc = hidpp20_adjustable_dpi_get_dpi_list(device,
							 feature_index,
							 sensor);
		if (rc)
			goto err;

		rc = hidpp20_adjustable_dpi_get_dpi(device, feature_index, sensor);
		if (rc)
			goto err;

		hidpp_log_raw(&device->base,
			      "sensor %d: current dpi: %d (default: %d) min: %d max: %d steps: %d\n",
			      sensor->index,
			      sensor->dpi,
			      sensor->default_dpi,
			      sensor->dpi_min,
			      sensor->dpi_max,
			      sensor->dpi_steps);
	}

	*sensors_list = s_list;
	return num_sensors;
err:
	free(s_list);
	return rc;
}

int hidpp20_adjustable_dpi_set_sensor_dpi(struct hidpp20_device *device,
					  struct hidpp20_sensor *sensor, uint16_t dpi)
{
	uint8_t feature_index;
	uint16_t returned_parameters;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_ADJUSTABLE_DPI_SET_SENSOR_DPI,
		.msg.parameters[0] = sensor->index,
		.msg.parameters[1] = dpi >> 8,
		.msg.parameters[2] = dpi & 0xff,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_DPI);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	returned_parameters = hidpp20_get_unaligned_u16(&msg.msg.parameters[1]);

	/* version 0 of the protocol does not echo the parameters */
	if (returned_parameters != dpi && returned_parameters)
		return -EIO;

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x8100 - Onboard Profiles                                                  */
/* -------------------------------------------------------------------------- */

#define CMD_ONBOARD_PROFILES_GET_PROFILES_DESCR		0x00
#define CMD_ONBOARD_PROFILES_SET_CURRENT_PROFILE	0x30
#define CMD_ONBOARD_PROFILES_GET_CURRENT_PROFILE	0x40
#define CMD_ONBOARD_PROFILES_MEMORY_READ		0x50
#define CMD_ONBOARD_PROFILES_MEMORY_ADDR_WRITE		0x60
#define CMD_ONBOARD_PROFILES_MEMORY_WRITE		0x70
#define CMD_ONBOARD_PROFILES_MEMORY_WRITE_END		0x80
#define CMD_ONBOARD_PROFILES_GET_CURRENT_DPI_INDEX	0xb0
#define CMD_ONBOARD_PROFILES_SET_CURRENT_DPI_INDEX	0xc0

static int
hidpp20_onboard_profiles_read_memory(struct hidpp20_device *device,
				     uint16_t page,
				     uint16_t section,
				     uint8_t *result)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_READ,
		.msg.parameters[0] = page >> 8,
		.msg.parameters[1] = page & 0xFF,
		.msg.parameters[2] = section >> 8,
		.msg.parameters[3] = section & 0xFF,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	/* msg.msg.parameters is guaranteed to have a size >= 16 */
	memcpy(result, msg.msg.parameters, 16);

	return 0;
}

int
hidpp20_onboard_profiles_get_current_profile(struct hidpp20_device *device,
					     struct hidpp20_profiles *profiles_list)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_GET_CURRENT_PROFILE,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[1];
}

static int
hidpp20_onboard_profiles_initialize(struct hidpp20_device *device,
				    unsigned profile_count,
				    struct hidpp20_profiles *profiles_list)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_GET_PROFILES_DESCR,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	profiles_list->num_buttons = msg.msg.parameters[5] <= 16 ? msg.msg.parameters[5] : 16;
	profiles_list->num_profiles = profile_count;
	/* FIXME: actually retrieve the correct values */
	profiles_list->num_modes = 5;

	return 0;
}

int
hidpp20_onboard_profiles_allocate(struct hidpp20_device *device,
				  struct hidpp20_profiles **profiles_list)
{
	unsigned i;
	int rc;
	uint8_t data[16] = {0};
	struct hidpp20_profiles *profiles;
	unsigned profile_count = 0;

	rc = hidpp20_onboard_profiles_read_memory(device,
						  0x0000,
						  0x0000,
						  data);
	if (rc < 0)
		return rc;

	profiles = zalloc(sizeof(struct hidpp20_profiles));

	for (i = 0; i < 3; i++) {
		uint8_t *d = data + 4 * i;

		if (d[0] == 0xFF && d[1] == 0xFF)
			break;

		profile_count++;
		profiles->profiles[i].index = d[1];
		profiles->profiles[i].enabled = d[2];
	}

	hidpp20_onboard_profiles_initialize(device,
					    profile_count,
					    profiles);

	*profiles_list = profiles;

	return profile_count;
}

int hidpp20_onboard_profiles_read(struct hidpp20_device *device,
				  unsigned int index,
				  struct hidpp20_profiles *profiles_list)
{
	uint8_t data[16] = {0};
	struct hidpp20_profile *profile = &profiles_list->profiles[index];
	unsigned i;
	int rc;

	if (index >= profiles_list->num_profiles)
		return -EINVAL;

	rc = hidpp20_onboard_profiles_read_memory(device,
						  index + 1,
						  0x0000,
						  data);
	if (rc < 0)
		return rc;

	profile->report_rate = 1000 / max(1, data[0]);
	profile->default_dpi = data[1];
	profile->switched_dpi = data[2];

	for (i = 0; i < 5; i++) {
		profile->dpi[i] = hidpp20_get_unaligned_be_u16(&data[2 * i + 3]);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* generic hidpp20 device operations                                          */
/* -------------------------------------------------------------------------- */

struct hidpp20_device *
hidpp20_device_new(const struct hidpp_device *base, unsigned int idx)
{
	struct hidpp20_device *dev;
	int rc;

	dev = zalloc(sizeof(*dev));

	dev->index = idx;
	dev->base = *base;

	dev->proto_major = 1;
	dev->proto_minor = 0;

	rc = hidpp20_root_get_protocol_version(dev, &dev->proto_major, &dev->proto_minor);
	if (rc) {
		/* communication error, best to ignore the device */
		goto err;
	}

	rc = hidpp20_feature_set_get(dev);
	if (rc < 0)
		goto err;

	return dev;
err:
	free(dev);
	return NULL;
}

void
hidpp20_device_destroy(struct hidpp20_device *device)
{
	free(device->feature_list);
	free(device);
}
