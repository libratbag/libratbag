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
	};

	hidpp_set_unaligned_be_u16(&msg.msg.parameters[0], feature);

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

	*feature = hidpp_get_unaligned_be_u16(msg.msg.parameters);
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

	control->control_id = hidpp_get_unaligned_be_u16(&msg.msg.parameters[0]);
	control->task_id = hidpp_get_unaligned_be_u16(&msg.msg.parameters[2]);
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

	control->control_id = hidpp_get_unaligned_be_u16(&msg.msg.parameters[0]);
	control->task_id = hidpp_get_unaligned_be_u16(&msg.msg.parameters[2]);
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
	};

	hidpp_set_unaligned_be_u16(&msg.msg.parameters[0], control->control_id);

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->reporting.remapped = hidpp_get_unaligned_be_u16(&msg.msg.parameters[3]);
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
	};

	hidpp_set_unaligned_be_u16(&msg.msg.parameters[0], control->control_id);
	hidpp_set_unaligned_be_u16(&msg.msg.parameters[3], control->reporting.remapped);

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

	*resolution = hidpp_get_unaligned_be_u16(msg.msg.parameters);
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
	       hidpp_get_unaligned_be_u16(&msg.msg.parameters[i]) != 0) {
		uint16_t value = hidpp_get_unaligned_be_u16(&msg.msg.parameters[i]);

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

	sensor->dpi = hidpp_get_unaligned_be_u16(&msg.msg.parameters[1]);
	sensor->default_dpi = hidpp_get_unaligned_be_u16(&msg.msg.parameters[3]);

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
	};

	hidpp_set_unaligned_be_u16(&msg.msg.parameters[1], dpi);

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_DPI);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	returned_parameters = hidpp_get_unaligned_be_u16(&msg.msg.parameters[1]);

	/* version 0 of the protocol does not echo the parameters */
	if (returned_parameters != dpi && returned_parameters)
		return -EIO;

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x8100 - Onboard Profiles                                                  */
/* -------------------------------------------------------------------------- */

#define CMD_ONBOARD_PROFILES_GET_PROFILES_DESCR		0x00
#define CMD_ONBOARD_PROFILES_SET_ONBOARD_MODE		0x10
#define CMD_ONBOARD_PROFILES_GET_ONBOARD_MODE		0x20
#define CMD_ONBOARD_PROFILES_SET_CURRENT_PROFILE	0x30
#define CMD_ONBOARD_PROFILES_GET_CURRENT_PROFILE	0x40
#define CMD_ONBOARD_PROFILES_MEMORY_READ		0x50
#define CMD_ONBOARD_PROFILES_MEMORY_ADDR_WRITE		0x60
#define CMD_ONBOARD_PROFILES_MEMORY_WRITE		0x70
#define CMD_ONBOARD_PROFILES_MEMORY_WRITE_END		0x80
#define CMD_ONBOARD_PROFILES_GET_CURRENT_DPI_INDEX	0xb0
#define CMD_ONBOARD_PROFILES_SET_CURRENT_DPI_INDEX	0xc0

#define HIDPP20_PAGE_SIZE		(16*16)
#define HIDPP20_PROFILE_SIZE		HIDPP20_PAGE_SIZE
#define HIDPP20_BUTTON_HID		0x80

#define HIDPP20_MODE_NO_CHANGE				0x00
#define HIDPP20_ONBOARD_MODE				0x01
#define HIDPP20_HOST_MODE				0x02

#define HIDPP20_ONBOARD_PROFILES_MEMORY_TYPE_G402	0x01
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G402	0x01
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G303	0x02
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G900	0x03
#define HIDPP20_ONBOARD_PROFILES_MACRO_TYPE_G402	0x01

struct hidpp20_onboard_profiles_info {
	uint8_t memory_model_id;
	uint8_t profile_format_id;
	uint8_t macro_format_id;
	uint8_t profile_count;
	uint8_t profile_count_oob;
	uint8_t button_count;
	uint8_t sector_count;
	uint16_t sector_size;
	uint8_t mechanical_layout;
	uint8_t various_info;
	uint8_t reserved[5];
} __attribute__((packed));
_Static_assert(sizeof(struct hidpp20_onboard_profiles_info) == 16, "Invalid size");

int
hidpp20_onboard_profiles_read_memory(struct hidpp20_device *device,
				     uint8_t read_rom,
				     uint8_t page,
				     uint8_t section,
				     uint8_t result[16])
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_READ,
		.msg.parameters[0] = read_rom,
		.msg.parameters[1] = page,
		.msg.parameters[2] = 0,
		.msg.parameters[3] = section,
	};

	if (read_rom > 1)
		return -EINVAL;

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

static int
hidpp20_onboard_profiles_write_start(struct hidpp20_device *device,
				     uint8_t page,
				     uint8_t section)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_ADDR_WRITE,
		.msg.parameters[0] = 0x00,
		.msg.parameters[1] = page,
		.msg.parameters[2] = 0,
		.msg.parameters[3] = section,
		.msg.parameters[4] = 0x01,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

static int
hidpp20_onboard_profiles_write_end(struct hidpp20_device *device)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_WRITE_END,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

static int
hidpp20_onboard_profiles_write_data(struct hidpp20_device *device,
				    uint8_t page,
				    uint8_t section,
				    uint8_t *data,
				    size_t len)
{
	uint8_t feature_index;
	int transfered = 0;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_WRITE,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_onboard_profiles_write_start(device, page, section);
	if (rc)
		return rc;

	while (len >= 16) {
		msg.msg.address = CMD_ONBOARD_PROFILES_MEMORY_WRITE;
		memcpy(msg.msg.parameters, data, 16);

		rc = hidpp20_request_command(device, &msg);
		if (rc)
			return rc;

		len -= 16;
		data += 16;
		transfered += 16;
	}

	rc = hidpp20_onboard_profiles_write_end(device);
	if (rc)
		return rc;

	return transfered;
}

static int
hidpp20_onboard_profiles_write_page(struct hidpp20_device *device,
				    uint8_t page,
				    uint8_t data[HIDPP20_PAGE_SIZE])
{
	uint16_t crc;

	crc = hidpp_crc_ccitt(data, HIDPP20_PAGE_SIZE - 2);
	hidpp_set_unaligned_be_u16(&data[HIDPP20_PROFILE_SIZE - 2], crc);

	return hidpp20_onboard_profiles_write_data(device, page, 0x00, data, HIDPP20_PAGE_SIZE);
}

static int
hidpp20_onboard_profiles_get_onboard_mode(struct hidpp20_device *device)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_GET_ONBOARD_MODE,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return msg.msg.parameters[0];
}

static int
hidpp20_onboard_profiles_set_onboard_mode(struct hidpp20_device *device,
					  uint8_t onboard_mode)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_SET_ONBOARD_MODE,
		.msg.parameters[1] = onboard_mode,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

int
hidpp20_onboard_profiles_get_current_profile(struct hidpp20_device *device)
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

int
hidpp20_onboard_profiles_set_current_profile(struct hidpp20_device *device,
					     uint8_t index)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_SET_CURRENT_PROFILE,
		.msg.parameters[1] = index + 1,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

static int
hidpp20_onboard_profiles_initialize(struct hidpp20_device *device,
				    struct hidpp20_profiles **profiles_list)
{
	uint8_t feature_index;
	int rc;
	struct hidpp20_profiles *profiles;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_GET_PROFILES_DESCR,
	};
	struct hidpp20_onboard_profiles_info *info;
	int onboard_mode;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	info = (struct hidpp20_onboard_profiles_info *)msg.msg.parameters;

	if (info->memory_model_id != HIDPP20_ONBOARD_PROFILES_MEMORY_TYPE_G402) {
		hidpp_log_error(&device->base,
				"Memory layout not supported: 0x%02x.\n",
				info->memory_model_id);
		return -ENOTSUP;
	}

	if ((info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G402) &&
	    (info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G303) &&
	    (info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G900)) {
		hidpp_log_error(&device->base,
				"Profile layout not supported: 0x%02x.\n",
				info->profile_format_id);
		return -ENOTSUP;
	}

	if (info->macro_format_id != HIDPP20_ONBOARD_PROFILES_MACRO_TYPE_G402) {
		hidpp_log_error(&device->base,
				"Macro format not supported: 0x%02x.\n",
				info->macro_format_id);
		return -ENOTSUP;
	}

	info->sector_size = hidpp_be_u16_to_cpu(info->sector_size);
	if (info->sector_size != HIDPP20_PAGE_SIZE) {
		hidpp_log_error(&device->base,
				"Unsupported sector size: %d.\n",
				info->sector_size);
		return -ENOTSUP;
	}

	rc = hidpp20_onboard_profiles_get_onboard_mode(device);
	if (rc < 0)
		return rc;

	onboard_mode = rc;
	if (onboard_mode != HIDPP20_ONBOARD_MODE) {
		hidpp_log_raw(&device->base,
			      "not on the correct mode: %d.\n",
			      onboard_mode);
		rc = hidpp20_onboard_profiles_set_onboard_mode(device,
							       HIDPP20_ONBOARD_MODE);
		if (rc < 0)
			return rc;
	}

	profiles = zalloc(sizeof(struct hidpp20_profiles) +
			  info->profile_count * sizeof(struct hidpp20_profile));

	profiles->num_profiles = info->profile_count;
	profiles->num_rom_profiles = info->profile_count_oob;
	profiles->num_buttons = msg.msg.parameters[5] <= 16 ? msg.msg.parameters[5] : 16;
	profiles->num_modes = HIDPP20_DPI_COUNT;
	profiles->has_g_shift = (info->mechanical_layout & 0x03) == 2;
	profiles->has_dpi_shift = (info->mechanical_layout & 0x0c) == 2;
	switch(info->various_info & 0x07) {
	case 1:
		profiles->corded = 1;
		break;
	case 2:
		profiles->wireless = 1;
		break;
	case 4:
		profiles->corded = 1;
		profiles->wireless = 1;
		break;
	}

	*profiles_list = profiles;

	return 0;
}

static int
hidpp20_onboard_profiles_macro_next(struct hidpp20_device *device,
				    uint8_t memory[32],
				    uint16_t *index,
				    union hidpp20_macro_data *macro)
{
	int rc = 0;
	unsigned int step = 1;

	if (*index >= 32 - sizeof(union hidpp20_macro_data)) {
		hidpp_log_error(&device->base, "error while parsing macro.\n");
		return -EFAULT;
	}

	memcpy(macro, &memory[*index], sizeof(union hidpp20_macro_data));

	switch (macro->any.type) {
	case HIDPP20_MACRO_DELAY:
		/* fallthrough */
	case HIDPP20_MACRO_KEY_PRESS:
		/* fallthrough */
	case HIDPP20_MACRO_KEY_RELEASE:
		/* fallthrough */
	case HIDPP20_MACRO_JUMP:
		step = 3;
		rc = -EAGAIN;
		break;
	case HIDPP20_MACRO_NOOP:
		step = 1;
		rc = -EAGAIN;
		break;
	case HIDPP20_MACRO_END:
		step = 1;
		return 0;
	default:
		hidpp_log_error(&device->base, "unknown tag: 0x%02x\n", macro->any.type);
		rc = -EFAULT;
	}

	if ((*index + step) & 0xF0)
		/* the next item will be on the following chunk */
		return -ENOMEM;

	*index += step;

	return rc;
}

static int
hidpp20_onboard_profiles_read_macro(struct hidpp20_device *device,
				    uint8_t page, uint8_t offset,
				    union hidpp20_macro_data **return_macro)
{
	uint8_t memory[HIDPP20_PAGE_SIZE];
	union hidpp20_macro_data *macro = NULL;
	unsigned count = 0;
	unsigned index = 0;
	uint16_t mem_index = 0;
	int rc = -ENOMEM;

	do {
		if (count == index) {
			union hidpp20_macro_data *tmp;

			count += 32;
			/* manual realloc to have the memory zero-initialized */
			tmp = zalloc(count * sizeof(union hidpp20_macro_data));
			if (macro) {
				memcpy(tmp, macro, (count - 32) * sizeof(union hidpp20_macro_data));
				free(macro);
			}
			macro = tmp;
		}

		if (rc == -ENOMEM) {
			offset += mem_index;
			rc = hidpp20_onboard_profiles_read_memory(device,
								  0,
								  page,
								  offset,
								  memory);
			if (rc)
				goto out_err;

			mem_index = 0;
		}

		rc = hidpp20_onboard_profiles_macro_next(device,
							 memory,
							 &mem_index,
							 &macro[index]);
		if (rc == -EFAULT)
			goto out_err;

		if (rc != -ENOMEM) {
			if (macro[index].any.type == HIDPP20_MACRO_JUMP) {
				page = macro[index].jump.page;
				offset = macro[index].jump.offset;
				mem_index = 0;
				/* no need to store the jump in memory */
				index--;
				/* force memory fetching */
				rc = -ENOMEM;
			}

			index++;
		}
	} while (rc);

	*return_macro = macro;
	return index;

out_err:
	if (macro)
		free(macro);

	return rc;
}

static int
hidpp20_onboard_profiles_parse_macro(struct hidpp20_device *device,
				     uint8_t page, uint8_t offset,
				     union hidpp20_macro_data **return_macro)
{
	union hidpp20_macro_data *m, *macro = NULL;
	unsigned i, count = 0;
	int rc;

	rc = hidpp20_onboard_profiles_read_macro(device, page, offset, &macro);
	if (rc < 0)
		return rc;

	count = rc;

	for (i = 0; i < count; i++) {
		m = &macro[i];
		switch (m->any.type) {
		case HIDPP20_MACRO_DELAY:
			m->delay.time = hidpp_be_u16_to_cpu(m->delay.time);
			break;
		case HIDPP20_MACRO_KEY_PRESS:
			break;
		case HIDPP20_MACRO_KEY_RELEASE:
			break;
		case HIDPP20_MACRO_JUMP:
			break;
		case HIDPP20_MACRO_END:
			break;
		case HIDPP20_MACRO_NOOP:
			break;
		default:
			hidpp_log_error(&device->base, "unknown tag: 0x%02x\n", m->any.type);
		}
	}

	*return_macro = macro;

	return 0;
}

static unsigned int
hidpp20_onboard_profiles_compute_dict_size(struct hidpp20_device *device,
					   struct hidpp20_profiles *profiles)
{
	unsigned p, num_offset;

	num_offset = 0;
	p = profiles->num_profiles;
	while (p) {
		p >>= 2;
		num_offset += 16;
	}

	return num_offset;
}

int
hidpp20_onboard_profiles_allocate(struct hidpp20_device *device,
				  struct hidpp20_profiles **profiles_list)
{
	unsigned i, offset, num_offset;
	int rc;
	uint8_t data[HIDPP20_PAGE_SIZE] = {0};
	struct hidpp20_profiles *profiles = NULL;

	rc = hidpp20_onboard_profiles_initialize(device,
						 &profiles);
	if (rc < 0)
		return rc;

	assert(profiles);

	num_offset = hidpp20_onboard_profiles_compute_dict_size(device,
								profiles);

	for (offset = 0; offset < num_offset; offset += 16) {
		rc = hidpp20_onboard_profiles_read_memory(device,
							  0x00,
							  0x00,
							  offset,
							  data + offset);
		if (rc < 0)
			return rc;
	}

	for (i = 0; i < profiles->num_profiles; i++) {
		uint8_t *d = data + offset + 4 * i;

		if (d[0] == 0xFF && d[1] == 0xFF)
			break;

		profiles->profiles[i].index = d[1];
		profiles->profiles[i].enabled = d[2];
	}

	*profiles_list = profiles;

	return profiles->num_profiles;
}

void
hidpp20_onboard_profiles_destroy(struct hidpp20_device *device,
				 struct hidpp20_profiles *profiles_list)
{
	struct hidpp20_profile *profile;
	union hidpp20_macro_data **macro;
	unsigned i;

	if (!profiles_list)
		return;

	for (i = 0; i < profiles_list->num_profiles; i++) {
		profile = &profiles_list->profiles[i];

		ARRAY_FOR_EACH(profile->macros, macro) {
			if (*macro)
				free(*macro);
		}
	}

	free(profiles_list);
}

static int
hidpp20_onboard_profiles_find_and_read_profile(struct hidpp20_device *device,
					       unsigned int index,
					       uint8_t *data,
					       unsigned int num_rom_profiles)
{
	int rc;
	unsigned i;
	uint16_t crc, read_crc;

	rc = hidpp20_onboard_profiles_read_memory(device,
						  0,
						  index + 1,
						  0x00,
						  data);
	if (rc < 0)
		return rc;

	if (data[0] > 0) {
		for (i = 1; i < HIDPP20_PROFILE_SIZE / 0x10; i++) {
			rc = hidpp20_onboard_profiles_read_memory(device,
								  0,
								  index + 1,
								  i * 0x10,
								  data + i * 0x10);
			if (rc < 0)
				return rc;
		}

		crc = hidpp_crc_ccitt(data, HIDPP20_PROFILE_SIZE - 2);
		read_crc = hidpp_get_unaligned_be_u16(&data[HIDPP20_PROFILE_SIZE - 2]);

		if (crc == read_crc)
			return 0;
	}

	/* something went wrong, the mouse is using the factory profile in ROM */
	if (num_rom_profiles == 0)
		return -EINVAL;

	if (index >= num_rom_profiles)
		index = num_rom_profiles - 1;

	for (i = 0; i < HIDPP20_PROFILE_SIZE / 0x10; i++) {
		rc = hidpp20_onboard_profiles_read_memory(device,
							  1,
							  index + 1,
							  i * 0x10,
							  data + i * 0x10);
		if (rc != 0)
			return rc;
	}

	return 0;
}

static int
hidpp20_onboard_profiles_enable_profile(struct hidpp20_device *device,
					unsigned int index,
					struct hidpp20_profiles *profiles_list)
{
	unsigned int i, buffer_index = 0;
	uint8_t data[HIDPP20_PROFILE_SIZE] = {0};

	if (profiles_list->profiles[index].enabled)
		return 0;

	profiles_list->profiles[index].index = index + 1;
	profiles_list->profiles[index].enabled = 0x01;

	for (i = 0; i < profiles_list->num_profiles; i++) {
		data[buffer_index++] = 0x00;
		data[buffer_index++] = i + 1;
		data[buffer_index++] = profiles_list->profiles[i].enabled;
		data[buffer_index++] = 0x00;
	}

	data[buffer_index++] = 0xFF;
	data[buffer_index++] = 0xFF;

	data[buffer_index++] = 0x00;
	data[buffer_index++] = 0x00;

	memset(data + buffer_index, 0xff, sizeof(data) - buffer_index);

	hidpp_log_buf_debug(&device->base,
			   "dictionary: ",
			   data,
			   hidpp20_onboard_profiles_compute_dict_size(device,
								      profiles_list));

	return hidpp20_onboard_profiles_write_page(device, 0x00, data);
}

union hidpp20_internal_profile {
	uint8_t data[HIDPP20_PROFILE_SIZE];
	struct {
		uint8_t report_rate;
		uint8_t default_dpi;
		uint8_t switched_dpi;
		uint16_t dpi[5];
		struct {
			uint8_t red;
			uint8_t green;
			uint8_t blue;
		} profile_color;
		uint8_t power_mode;
		uint8_t angle_snapping;
		uint8_t reserved[14];
		union hidpp20_button_binding buttons[16];
		union hidpp20_button_binding alternate_buttons[16];
		union {
			char txt[16 * 3];
			uint8_t raw[16 * 3];
		} name;
		uint8_t logo_effect[11]; /* G303 only */
		uint8_t side_effects[11]; /* G303 only */
		uint8_t free[24];
		uint16_t crc;
	} __attribute__((packed)) profile;
};
_Static_assert(sizeof(union hidpp20_internal_profile) == HIDPP20_PROFILE_SIZE, "Invalid size");

static void
hidpp20_buttons_to_cpu(struct hidpp20_device *device,
		       struct hidpp20_profile *profile,
		       union hidpp20_button_binding *buttons,
		       unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		union hidpp20_button_binding *b = &buttons[i];
		union hidpp20_button_binding *button = &profile->buttons[i];

		button->any.type = b->any.type;

		switch (b->any.type) {
		case HIDPP20_BUTTON_HID_TYPE:
			button->subany.subtype = b->subany.subtype;
			switch (b->subany.subtype) {
			case HIDPP20_BUTTON_HID_TYPE_MOUSE:
				button->button.buttons = ffs(hidpp_be_u16_to_cpu(b->button.buttons));
				break;
			case HIDPP20_BUTTON_HID_TYPE_KEYBOARD:
				button->keyboard_keys.modifier_flags = b->keyboard_keys.modifier_flags;
				button->keyboard_keys.key = b->keyboard_keys.key;
				break;
			case HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL:
				button->consumer_control.consumer_control =
					hidpp_be_u16_to_cpu(b->consumer_control.consumer_control);
				break;
			}
			break;
		case HIDPP20_BUTTON_SPECIAL:
			button->special.special = b->special.special;
			break;
		case HIDPP20_BUTTON_MACRO:
			if (profile->macros[i]) {
				free(profile->macros[i]);
				profile->macros[i] = NULL;
			}
			hidpp20_onboard_profiles_parse_macro(device,
							     b->macro.page,
							     b->macro.offset,
							     &profile->macros[i]);

			/* the actual page is stored in the 'zero' field */
			button->macro.page = i;
			button->macro.offset = b->macro.offset;
			button->macro.zero = b->macro.page;
			break;
		case HIDPP20_BUTTON_DISABLED:
			break;
		default:
			break;
		}
	}
}

static void
hidpp20_buttons_from_cpu(struct hidpp20_profile *profile,
			 union hidpp20_button_binding *buttons,
			 unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		union hidpp20_button_binding *button = &buttons[i];
		union hidpp20_button_binding *b = &profile->buttons[i];

		button->any.type = b->any.type;

		switch (b->any.type) {
		case HIDPP20_BUTTON_HID_TYPE:
			button->subany.subtype = b->subany.subtype;
			switch (b->subany.subtype) {
			case HIDPP20_BUTTON_HID_TYPE_MOUSE:
				button->button.buttons = hidpp_cpu_to_be_u16(1U << (b->button.buttons - 1));
				break;
			case HIDPP20_BUTTON_HID_TYPE_KEYBOARD:
				button->keyboard_keys.modifier_flags = b->keyboard_keys.modifier_flags;
				button->keyboard_keys.key = b->keyboard_keys.key;
				break;
			case HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL:
				button->consumer_control.type = HIDPP20_BUTTON_HID_TYPE;
				button->consumer_control.subtype = HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL;
				button->consumer_control.consumer_control =
					hidpp_cpu_to_be_u16(b->consumer_control.consumer_control);
				break;
			}
			break;
		case HIDPP20_BUTTON_SPECIAL:
			button->special.special = b->special.special;
			break;
		case HIDPP20_BUTTON_DISABLED:
			break;
		case HIDPP20_BUTTON_MACRO:
			/* the actual page is stored in the 'zero' field */
			button->macro.page = b->macro.zero;
			button->macro.offset = b->macro.offset;
			button->macro.zero = 0;
			break;
		default:
			break;
		}
	}
}

int hidpp20_onboard_profiles_read(struct hidpp20_device *device,
				  unsigned int index,
				  struct hidpp20_profiles *profiles_list)
{
	union hidpp20_internal_profile pdata = {0};
	uint8_t *data = pdata.data;
	struct hidpp20_profile *profile = &profiles_list->profiles[index];
	unsigned i;
	int rc;

	if (index >= profiles_list->num_profiles)
		return -EINVAL;

	rc = hidpp20_onboard_profiles_find_and_read_profile(device,
							    index,
							    data,
							    profiles_list->num_rom_profiles);
	if (rc != 0)
		return rc;

	profile->report_rate = 1000 / max(1, pdata.profile.report_rate);
	profile->default_dpi = pdata.profile.default_dpi;
	profile->switched_dpi = pdata.profile.switched_dpi;

	for (i = 0; i < 5; i++) {
		profile->dpi[i] = hidpp_get_unaligned_le_u16(&data[2 * i + 3]);
	}

	hidpp20_buttons_to_cpu(device, profile, pdata.profile.buttons, profiles_list->num_buttons);

	memcpy(profile->name, pdata.profile.name.txt, sizeof(profile->name));
	/* force terminating '\0' */
	profile->name[sizeof(profile->name) - 1] = '\0';

	/* check if we are using the default name or not */
	for (i = 0; i < sizeof(profile->name); i++) {
		if (pdata.profile.name.raw[i] != 0xff)
			break;
	}
	if (i == sizeof(profile->name))
		memset(profile->name, 0, sizeof(profile->name));

	return 0;
}

int hidpp20_onboard_profiles_write(struct hidpp20_device *device,
				   unsigned int index,
				   struct hidpp20_profiles *profiles_list)
{
	union hidpp20_internal_profile pdata = {0};
	uint8_t *data = pdata.data;
	struct hidpp20_profile *profile = &profiles_list->profiles[index];
	unsigned i;
	int rc;

	if (index >= profiles_list->num_profiles)
		return -EINVAL;

	rc = hidpp20_onboard_profiles_find_and_read_profile(device,
							    index,
							    data,
							    profiles_list->num_rom_profiles);
	if (rc < 0)
		return rc;

	rc = hidpp20_onboard_profiles_enable_profile(device, index, profiles_list);
	if (rc < 0)
		return rc;

	pdata.profile.report_rate = 1000 / profile->report_rate;
	pdata.profile.default_dpi = profile->default_dpi;
	pdata.profile.switched_dpi = profile->switched_dpi;

	for (i = 0; i < 5; i++) {
		pdata.profile.dpi[i] = hidpp_cpu_to_le_u16(profile->dpi[i]);
	}

	hidpp20_buttons_from_cpu(profile, pdata.profile.buttons, profiles_list->num_buttons);

	memcpy(pdata.profile.name.txt, profile->name, sizeof(profile->name));

	rc = hidpp20_onboard_profiles_write_page(device, index + 1, data);
	if (rc < 0)
		return rc;

	if (rc != sizeof(pdata.data))
		return -EIO;

	return 0;
}

static const enum ratbag_button_action_special hidpp20_profiles_specials[] = {
	[0x00] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
	[0x01] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,
	[0x02] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,
	[0x03] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,
	[0x04] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,
	[0x05] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT,
	[0x06] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,
	[0x07] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE,
	[0x08] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
	[0x09] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
	[0x0a] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,
	[0x0b] = RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE,
	[0x0c] = RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL,

	[0x0d ... 0xff] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
};

enum ratbag_button_action_special
hidpp20_onboard_profiles_get_special(uint8_t code)
{
	return hidpp20_profiles_specials[code];
}

uint8_t
hidpp20_onboard_profiles_get_code_from_special(enum ratbag_button_action_special special)
{
	uint8_t i = 0;

	while (++i) {
		if (hidpp20_profiles_specials[i] == special)
			return i;
	}

	return RATBAG_BUTTON_ACTION_SPECIAL_INVALID;
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

	if (dev->proto_major < 2)
		goto err;

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
