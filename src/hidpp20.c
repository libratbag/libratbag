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

#include "config.h"

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

	switch(feature)
	{
	CASE_RETURN_STRING(HIDPP_PAGE_ROOT);
	CASE_RETURN_STRING(HIDPP_PAGE_FEATURE_SET);
	CASE_RETURN_STRING(HIDPP_PAGE_DEVICE_INFO);
	CASE_RETURN_STRING(HIDPP_PAGE_DEVICE_NAME);
	CASE_RETURN_STRING(HIDPP_PAGE_RESET);
	CASE_RETURN_STRING(HIDPP_PAGE_BATTERY_LEVEL_STATUS);
	CASE_RETURN_STRING(HIDPP_PAGE_BATTERY_VOLTAGE);
	CASE_RETURN_STRING(HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS);
	CASE_RETURN_STRING(HIDPP_PAGE_SPECIAL_KEYS_BUTTONS);
	CASE_RETURN_STRING(HIDPP_PAGE_WIRELESS_DEVICE_STATUS);
	CASE_RETURN_STRING(HIDPP_PAGE_MOUSE_POINTER_BASIC);
	CASE_RETURN_STRING(HIDPP_PAGE_ADJUSTABLE_DPI);
	CASE_RETURN_STRING(HIDPP_PAGE_ADJUSTABLE_REPORT_RATE);
	CASE_RETURN_STRING(HIDPP_PAGE_COLOR_LED_EFFECTS);
	CASE_RETURN_STRING(HIDPP_PAGE_RGB_EFFECTS);
	CASE_RETURN_STRING(HIDPP_PAGE_ONBOARD_PROFILES);
	CASE_RETURN_STRING(HIDPP_PAGE_MOUSE_BUTTON_SPY);
	default:
		sprintf_safe(numeric, "%#4x", feature);
		str = numeric;
		break;
	}

	return str;
}

const char*
hidpp20_sw_led_control_get_mode_string(const enum hidpp20_led_sw_ctrl_led_mode mode)
{
	static char numeric[8];
	const char* str;

	switch (mode)
	{
	CASE_RETURN_STRING(HIDPP20_LED_MODE_OFF);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_ON);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_BLINK);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_RAMP_UP);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_RAMP_DOWN);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_BREATHING);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_HEARTBEAT);
	CASE_RETURN_STRING(HIDPP20_LED_MODE_TRAVEL);
	default:
		sprintf_safe(numeric, "%#4x", mode);
		str = numeric;
		break;
	}

	return str;
}

const char*
hidpp20_get_quirk_string(enum hidpp20_quirk quirk)
{
	switch (quirk) {
	CASE_RETURN_STRING(HIDPP20_QUIRK_NONE);
	CASE_RETURN_STRING(HIDPP20_QUIRK_G305);
	CASE_RETURN_STRING(HIDPP20_QUIRK_G602);
	}

	abort();
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

	/* some mice don't support short reports */
	if (msg->msg.report_id == REPORT_ID_SHORT && !(device->base.supported_report_types & HIDPP_REPORT_SHORT))
		msg->msg.report_id = REPORT_ID_LONG;

	/* sanity check */
	if (msg->msg.report_id == REPORT_ID_LONG && !(device->base.supported_report_types & HIDPP_REPORT_LONG)) {
		hidpp_log_error(&device->base, "hidpp20: trying to use unsupported report type\n");
		return -EINVAL;
	}

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
						hidpp20_errors[hidpp_err] ? hidpp20_errors[hidpp_err] : "Undocumented error code",
						hidpp_err);
			else
				hidpp_log_error(&device->base,
						"    HID++ error from the device (%d): %s (%02x)\n",
						read_buffer.msg.device_idx,
						hidpp20_errors[hidpp_err] ? hidpp20_errors[hidpp_err] : "Undocumented error code",
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
	int ret = hidpp20_request_command_allow_error(device, msg, false);

	return ret > 0 ? -EPROTO : ret;
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = HIDPP_PAGE_ROOT_IDX,
		.msg.address = CMD_ROOT_GET_FEATURE,
	};

	set_unaligned_be_u16(&msg.msg.parameters[0], feature);

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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = HIDPP_PAGE_ROOT_IDX,
		.msg.address = CMD_ROOT_GET_PROTOCOL_VERSION,
	};

	rc = hidpp20_request_command_allow_error(device, &msg, true);

	if (rc == HIDPP10_ERR_INVALID_SUBID) {
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
		.msg.report_id = REPORT_ID_SHORT,
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_FEATURE_SET_GET_FEATURE_ID,
		.msg.parameters[0] = feature_index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*feature = get_unaligned_be_u16(msg.msg.parameters);
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

	/* feature set count does not include the root feature as documented here:
	 * https://6xq.net/git/lars/lshidpp.git/plain/doc/logitech_hidpp_2.0_specification_draft_2012-06-04.pdf
	 **/
	feature_count = ((uint8_t)rc) + 1;

	if (feature_count == 1)
		return -ENOTSUP;

	flist = zalloc((feature_count + 1) * sizeof(struct hidpp20_feature));

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
		.msg.report_id = REPORT_ID_SHORT,
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
/* 0x1001: Battery voltage                                                    */
/* -------------------------------------------------------------------------- */

#define CMD_BATTERY_VOLTAGE_GET_BATTERY_VOLTAGE 0x00
#define CMD_BATTERY_VOLTAGE_GET_SHOW_BATTERY_STATUS 0x10

int
hidpp20_batteryvoltage_get_battery_voltage(struct hidpp20_device *device,
					   uint16_t *voltage)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_BATTERY_VOLTAGE_GET_BATTERY_VOLTAGE,
	};
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_BATTERY_VOLTAGE);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*voltage = get_unaligned_be_u16(msg.msg.parameters);

	return msg.msg.parameters[2];
}

/* -------------------------------------------------------------------------- */
/* 0x1300: Non-RGB led support                                                */
/* -------------------------------------------------------------------------- */

#define CMD_LED_SW_CONTROL_GET_LED_COUNT 0x00
#define CMD_LED_SW_CONTROL_GET_LED_INFO 0x10
#define CMD_LED_SW_CONTROL_GET_SW_CTRL 0x20
#define CMD_LED_SW_CONTROL_SET_SW_CTRL 0x30
#define CMD_LED_SW_CONTROL_GET_LED_STATE 0x40
#define CMD_LED_SW_CONTROL_SET_LED_STATE 0x50
#define CMD_LED_SW_CONTROL_GET_NV_CONFIG 0x60

static bool hidpp20_led_sw_control_check_state(uint16_t state)
{
	switch (state)
	{
	case HIDPP20_LED_MODE_ON:
	case HIDPP20_LED_MODE_OFF:
	case HIDPP20_LED_MODE_BLINK:
	case HIDPP20_LED_MODE_TRAVEL:
	case HIDPP20_LED_MODE_RAMP_UP:
	case HIDPP20_LED_MODE_RAMP_DOWN:
	case HIDPP20_LED_MODE_HEARTBEAT:
	case HIDPP20_LED_MODE_BREATHING:
		return true;
	}

	return false;
}

int hidpp20_led_sw_control_read_leds(struct hidpp20_device* device,
		struct hidpp20_led_sw_ctrl_led_info** info_list)
{
	int rc;
	struct hidpp20_led_sw_ctrl_led_info *i_list, *info;
	unsigned i;
	uint8_t num_infos;

	rc = hidpp20_led_sw_control_get_led_count(device);

	if (rc < 0)
		return rc;

	num_infos = rc;

	if(rc == 0) {
		*info_list = NULL;
		return 0;
	}

	i_list = zalloc(rc * sizeof(struct hidpp20_led_sw_ctrl_led_info));

	for (i = 0; i < num_infos; i++) {
		info = &i_list[i];
		info->index = i;

		rc = hidpp20_led_sw_control_get_led_info(device, i, info);

		if (rc != 0)
			goto err;

		hidpp_log_raw(&device->base, "non-color led %d: type: %d supports: %d\n",
					  info->index,
					  info->type,
					  info->caps);
	}

	*info_list = i_list;
	return num_infos;

err:
	free(i_list);
	return rc;
}

int hidpp20_led_sw_control_get_led_count(struct hidpp20_device* device)
{
	union hidpp20_message msg = {
		.msg = {
			.report_id = REPORT_ID_SHORT,
			.address = CMD_LED_SW_CONTROL_GET_LED_COUNT,
			.device_idx = device->index,
		},
	};

	uint8_t feature_idx;

	feature_idx = hidpp_root_get_feature_idx(device, HIDPP_PAGE_LED_SW_CONTROL);
	if (feature_idx == 0) {
		return -ENOTSUP;
	}

	msg.msg.sub_id = feature_idx;

	if (hidpp20_request_command(device, &msg)) {
		return -ENOTSUP;
	}

	return msg.msg.parameters[0];
}

int hidpp20_led_sw_control_get_led_info(struct hidpp20_device* device,
		 uint8_t led_idx,
		 struct hidpp20_led_sw_ctrl_led_info *info)
{
	union hidpp20_message msg = {
		.msg = {
			.report_id = REPORT_ID_SHORT,
			.address = CMD_LED_SW_CONTROL_GET_LED_INFO,
			.device_idx = device->index,
		},
	};
	struct hidpp20_led_sw_ctrl_led_info *params;
	uint8_t feature_idx;

	feature_idx = hidpp_root_get_feature_idx(device, HIDPP_PAGE_LED_SW_CONTROL);
	if (feature_idx == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_idx;
	msg.msg.parameters[0] = led_idx;

	if (hidpp20_request_command(device, &msg))
		// Only error possible is an invalid index, which means the led doesn't exist
		return -ENOENT;

	params = (struct hidpp20_led_sw_ctrl_led_info*) msg.msg.parameters;
	params->caps = hidpp_be_u16_to_cpu(params->caps);

	*info = *params;

	return 0;
}

bool hidpp20_led_sw_control_get_sw_ctrl(struct hidpp20_device* device)
{
    uint8_t feature_idx;
	int rc;
	union hidpp20_message msg = {
		.msg = {
			.report_id = REPORT_ID_SHORT,
			.address = CMD_LED_SW_CONTROL_GET_SW_CTRL,
			.device_idx = device->index,
		},
	};

	feature_idx = hidpp_root_get_feature_idx(device, HIDPP_PAGE_LED_SW_CONTROL);

	if (feature_idx == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_idx;

	rc = hidpp20_request_command(device, &msg);

	if (rc) {
		return -ENOTSUP;
	}

	return msg.msg.parameters[0];
}

int hidpp20_led_sw_control_set_sw_ctrl(struct hidpp20_device* device, bool ctrl)
{
	uint8_t feature_idx;
	union hidpp20_message msg = {
		.msg = {
			.report_id = REPORT_ID_SHORT,
			.address = CMD_LED_SW_CONTROL_SET_SW_CTRL,
			.device_idx = device->index,
		},
	};

	feature_idx = hidpp_root_get_feature_idx(device, HIDPP_PAGE_LED_SW_CONTROL);

	if (feature_idx == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_idx;
	msg.msg.parameters[0] = ctrl;

	if (hidpp20_request_command(device, &msg))
		return -EINVAL;

	return 0;
}

int hidpp20_led_sw_control_get_led_state(struct hidpp20_device* device,
		 uint8_t led_idx,
		 struct hidpp20_led_sw_ctrl_led_state *out)
{
	int rc;
	uint8_t feature_idx;
	union hidpp20_message msg = {
		.msg = {
			.report_id = REPORT_ID_SHORT,
			.address = CMD_LED_SW_CONTROL_GET_LED_STATE,
			.device_idx = device->index,
		}
	};
	struct hidpp20_led_sw_ctrl_led_state *state;

	feature_idx = hidpp_root_get_feature_idx(device, HIDPP_PAGE_LED_SW_CONTROL);

	if (feature_idx == 0) {
		return -ENOTSUP;
	}

	msg.msg.sub_id = feature_idx;

	msg.msg.parameters[0] = led_idx;

	rc = hidpp20_request_command(device, &msg);

	if (rc)
		return -ENOENT;

	state = (struct hidpp20_led_sw_ctrl_led_state*) msg.msg.parameters;

	// This field has to be stored in little-endian
	state->mode = hidpp_be_u16_to_cpu(state->mode);
	if (state->mode == HIDPP20_LED_MODE_BREATHING) {
		// Only parameters that is reported by these LEDs is brightness when breathing
		state->breathing.brightness = hidpp_be_u16_to_cpu(state->breathing.brightness);
	}

	*out = *state;

	return 0;
}

int hidpp20_led_sw_control_set_led_state(struct hidpp20_device* device,
		const struct hidpp20_led_sw_ctrl_led_state *state)
{
	int rc;
	uint8_t feature_idx;
	union hidpp20_message msg = {
		.msg = {
			.report_id = REPORT_ID_LONG,
			.address = CMD_LED_SW_CONTROL_SET_LED_STATE,
			.device_idx = device->index,
		}
	};

	feature_idx = hidpp_root_get_feature_idx(device, HIDPP_PAGE_LED_SW_CONTROL);

	if (feature_idx == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_idx;

	if (!hidpp20_led_sw_control_check_state(state->mode))
		return -EINVAL;

	msg.msg.parameters[0] = state->index;
	set_unaligned_be_u16(&msg.msg.parameters[1], state->mode);
	set_unaligned_be_u16(&msg.msg.parameters[3], state->blink.index);
	set_unaligned_be_u16(&msg.msg.parameters[5], state->blink.on_time);
	set_unaligned_be_u16(&msg.msg.parameters[7], state->blink.off_time);

	rc = hidpp20_request_command(device, &msg);

	if (rc)
		return -EINVAL;

	return 0;
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
		.msg.report_id = REPORT_ID_SHORT,
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_KBD_REPROGRAMMABLE_KEYS_GET_CTRL_ID_INFO,
		.msg.parameters[0] = control->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->control_id = get_unaligned_be_u16(&msg.msg.parameters[0]);
	control->task_id = get_unaligned_be_u16(&msg.msg.parameters[2]);
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
/* 0x8070 - Color LED effects                                                 */
/* -------------------------------------------------------------------------- */

#define CMD_COLOR_LED_EFFECTS_GET_INFO 0x00
#define CMD_COLOR_LED_EFFECTS_GET_ZONE_INFO 0x10
#define CMD_COLOR_LED_EFFECTS_GET_ZONE_EFFECT_INFO 0x20
#define CMD_COLOR_LED_EFFECTS_SET_ZONE_EFFECT 0x30
#define CMD_COLOR_LED_EFFECTS_GET_ZONE_EFFECT 0xe0

int
hidpp20_color_led_effects_get_info(struct hidpp20_device *device,
				   struct hidpp20_color_led_info *info)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_COLOR_LED_EFFECTS_GET_INFO,
	};
	uint8_t feature_index;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_COLOR_LED_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*info = *(struct hidpp20_color_led_info *)msg.msg.parameters;
	device->led_ext_caps = info->ext_caps;

	return 0;
}

int
hidpp20_color_led_effects_get_zone_info(struct hidpp20_device *device,
					uint8_t reg,
					struct hidpp20_color_led_zone_info *info)
{
	int rc;
	struct hidpp20_color_led_zone_info *msg_info;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_COLOR_LED_EFFECTS_GET_ZONE_INFO,
		.msg.parameters[0] = info->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	msg_info = (struct hidpp20_color_led_zone_info *)msg.msg.parameters;
	info->location = hidpp_be_u16_to_cpu(msg_info->location);
	info->num_effects = msg_info->num_effects;
	info->persistency_caps = msg_info->persistency_caps;

	return 0;
}

int
hidpp20_color_led_effects_get_zone_infos(struct hidpp20_device *device,
					 struct hidpp20_color_led_zone_info **infos_list)
{
	uint8_t feature_index;
	struct hidpp20_color_led_zone_info *i_list, *info;
	struct hidpp20_color_led_info ledinfo = {0};
	uint8_t num_infos;
	unsigned i;
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_COLOR_LED_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	rc = hidpp20_color_led_effects_get_info(device, &ledinfo);
	if (rc < 0)
		return rc;

	num_infos = ledinfo.zone_count;
	if (num_infos == 0) {
		*infos_list = NULL;
		return 0;
	}

	i_list = zalloc(num_infos * sizeof(struct hidpp20_color_led_zone_info));

	for (i = 0; i < num_infos; i++) {
		info = &i_list[i];
		info->index = i;
		rc = hidpp20_color_led_effects_get_zone_info(device, feature_index, info);
		if (rc)
			goto err;

		hidpp_log_raw(&device->base,
			      "led_info %d: location: %d type %s num_effects: %d persistency_caps: 0x%02x\n",
			      info->index,
			      info->location,
			      hidpp20_led_get_location_mapping_name(info->location),
			      info->num_effects,
			      info->persistency_caps);
	}

	*infos_list = i_list;
	return num_infos;
err:
	free(i_list);
	return rc;
}

int
hidpp20_color_led_effect_get_zone_effect_info(struct hidpp20_device *device,
					      uint8_t zone_index,
					      uint8_t zone_effect_index,
					      struct hidpp20_color_led_zone_effect_info *info)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.address = CMD_COLOR_LED_EFFECTS_GET_ZONE_EFFECT_INFO,
		.msg.device_idx = device->index,
		.msg.parameters[0] = zone_index,
		.msg.parameters[1] = zone_effect_index,
	};
	int rc;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_COLOR_LED_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	info->zone_index = msg.msg.parameters[0];
	info->zone_effect_index = msg.msg.parameters[1];

	info->effect_id = get_unaligned_be_u16(&msg.msg.parameters[2]);
	info->effect_caps = get_unaligned_be_u16(&msg.msg.parameters[4]);
	info->effect_period = get_unaligned_be_u16(&msg.msg.parameters[6]);

	return 0;
}

int
hidpp20_color_led_effects_set_zone_effect(struct hidpp20_device *device,
					  uint8_t zone_index,
					  struct hidpp20_led led)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.address = CMD_COLOR_LED_EFFECTS_SET_ZONE_EFFECT,
		.msg.device_idx = device->index,
		.msg.parameters[0] = zone_index,
		.msg.parameters[12] = 1, /* write to RAM and flash */
	};
	int rc;
	struct hidpp20_internal_led *internal_led = (struct hidpp20_internal_led*) &msg.msg.parameters[1];

	hidpp20_onboard_profiles_write_led(internal_led, &led);

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_COLOR_LED_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

int
hidpp20_color_led_effects_get_zone_effect(struct hidpp20_device *device,
					  uint8_t zone_index,
					  struct hidpp20_led *led)
{
	uint8_t feature_index;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.address = CMD_COLOR_LED_EFFECTS_GET_ZONE_EFFECT,
		.msg.device_idx = device->index,
		.msg.parameters[0] = zone_index,
	};
	struct hidpp20_internal_led *internal_led;
	int rc;

	/* hidpp20_color_led_effects_get_info() must be called first to set the capabilities */
	if (!(device->led_ext_caps & HIDPP20_COLOR_LED_INFO_EXT_CAP_HAS_ZONE_EFFECT))
		return -ENOTSUP;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_COLOR_LED_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	if (msg.msg.parameters[0] != zone_index)
		return -EPROTO;

	internal_led = (struct hidpp20_internal_led*) &msg.msg.parameters[1];

	hidpp20_onboard_profiles_read_led(led, *internal_led);

	hidpp_log_debug(&device->base, "zone %u has effect %u\n", zone_index, led->mode);

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x8071: RGB Effects                                                        */
/* -------------------------------------------------------------------------- */

#define CMD_RGB_EFFECTS_GET_INFO				0x00
#define CMD_RGB_EFFECTS_SET_RGB_CLUSTER_EFFECT			0x10
#define CMD_RGB_EFFECTS_SET_MULTI_LED_RGB_CLUSTER_PATTERN	0x20
#define CMD_RGB_EFFECTS_MANAGE_NV_CONFIG			0x30
#define CMD_RGB_EFFECTS_MANAGE_RGB_LED_BIN_INFO			0x40
#define CMD_RGB_EFFECTS_MANAGE_SW_CONTROL			0x50
#define CMD_RGB_EFFECTS_SET_EFFECT_SYNC_CORRECTION		0x60
#define CMD_RGB_EFFECTS_MANAGE_RGB_POWER_MODE_CONFIG		0x70
#define CMD_RGB_EFFECTS_MANAGE_RGB_POWER_MODE			0x80

int
hidpp20_rgb_effects_get_device_info(struct hidpp20_device *device,
				    struct hidpp20_rgb_device_info *info)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_RGB_EFFECTS_GET_INFO,
		.msg.parameters[0] = HIDPP20_RGB_EFFECTS_INDEX_ALL,
		.msg.parameters[1] = HIDPP20_RGB_EFFECTS_INDEX_ALL,
		.msg.parameters[2] = HIDPP20_RGB_EFFECTS_TOI_GENERAL,
	};
	uint8_t feature_index;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_RGB_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	info->cluster_index = msg.msg.parameters[0];
	info->effect_index = msg.msg.parameters[1];
	info->cluster_count = msg.msg.parameters[2];

	info->nv_caps = get_unaligned_be_u16(&msg.msg.parameters[3]);
	info->ext_caps = get_unaligned_be_u16(&msg.msg.parameters[4]);

	return 0;
}

int
hidpp20_rgb_effects_get_cluster_info(struct hidpp20_device *device,
				     uint8_t cluster_index,
				     struct hidpp20_rgb_cluster_info *info)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_RGB_EFFECTS_GET_INFO,
		.msg.parameters[0] = cluster_index,
		.msg.parameters[1] = HIDPP20_RGB_EFFECTS_INDEX_ALL,
		.msg.parameters[2] = HIDPP20_RGB_EFFECTS_TOI_GENERAL,
	};
	uint8_t feature_index;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_RGB_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	info->index = msg.msg.parameters[0];
	info->effect_index = msg.msg.parameters[1];

	info->location = get_unaligned_be_u16(&msg.msg.parameters[2]);
	info->num_effects = msg.msg.parameters[4];
	info->persistency_caps = msg.msg.parameters[5];

	return 0;
}

int
hidpp20_rgb_effects_get_cluster_infos(struct hidpp20_device *device,
				      struct hidpp20_rgb_cluster_info **infos_list)
{
	struct hidpp20_rgb_cluster_info *i_list, *info;
	struct hidpp20_rgb_device_info device_info = {0};
	uint8_t num_infos;
	unsigned i;
	int rc;

	rc = hidpp20_rgb_effects_get_device_info(device, &device_info);
	if (rc < 0)
		return rc;

	num_infos = device_info.cluster_count;
	if (num_infos == 0) {
		*infos_list = NULL;
		return 0;
	}

	i_list = zalloc(num_infos * sizeof(struct hidpp20_rgb_cluster_info));

	for (i = 0; i < num_infos; i++) {
		info = &i_list[i];
		info->index = i;
		rc = hidpp20_rgb_effects_get_cluster_info(device, i, info);
		if (rc)
			goto err;

		hidpp_log_raw(&device->base,
			      "cluster_info %d: location: %d type %s num_effects: %d persistency_caps: 0x%02x\n",
			      info->index,
			      info->location,
			      hidpp20_led_get_location_mapping_name(info->location),
			      info->num_effects,
			      info->persistency_caps);
	}

	*infos_list = i_list;
	return num_infos;
err:
	free(i_list);
	return rc;
}

int
hidpp20_rgb_effects_get_effect_info(struct hidpp20_device *device,
				    uint8_t cluster_index,
				    uint8_t effect_index,
				    struct hidpp20_rgb_effect_info *info)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_RGB_EFFECTS_GET_INFO,
		.msg.parameters[0] = cluster_index,
		.msg.parameters[1] = effect_index,
		.msg.parameters[2] = HIDPP20_RGB_EFFECTS_TOI_GENERAL,
	};
	uint8_t feature_index;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_RGB_EFFECTS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	info->cluster_index = msg.msg.parameters[0];
	info->effect_index = msg.msg.parameters[1];

	info->effect_id = get_unaligned_be_u16(&msg.msg.parameters[2]);
	info->capabilities = get_unaligned_be_u16(&msg.msg.parameters[4]);
	info->effect_period = get_unaligned_be_u16(&msg.msg.parameters[6]);

	return 0;
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
		.msg.report_id = REPORT_ID_SHORT,
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_SPECIAL_KEYS_BUTTONS_GET_INFO,
		.msg.parameters[0] = control->index,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->control_id = get_unaligned_be_u16(&msg.msg.parameters[0]);
	control->task_id = get_unaligned_be_u16(&msg.msg.parameters[2]);
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_SPECIAL_KEYS_BUTTONS_GET_REPORTING,
	};

	set_unaligned_be_u16(&msg.msg.parameters[0], control->control_id);

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	control->reporting.remapped = get_unaligned_be_u16(&msg.msg.parameters[3]);
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
	uint8_t num_controls, real_num_controls = 0;
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

	hidpp_log_debug(&device->base, "device has %d buttons\n", num_controls);

	c_list = zalloc(num_controls * sizeof(struct hidpp20_control_id));

	for (i = 0; i < num_controls; i++) {
		control = &c_list[real_num_controls];
		control->index = i;
		rc = hidpp20_special_keys_buttons_get_info(device,
							   feature_index,
							   control);
		if (rc) {
			hidpp_log_error(&device->base,
				"error getting button info for control %d, ignoring\n", i);
			continue;
		}

		rc = hidpp20_special_keys_buttons_get_reporting(device,
								feature_index,
								control);
		if (rc) {
			hidpp_log_error(&device->base,
				"error getting button reporting for control %d, ignoring\n", i);
			continue;
		}

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

		real_num_controls++;
	}
	*controls_list = realloc(c_list, real_num_controls * sizeof(struct hidpp20_control_id));
	return real_num_controls;
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

	set_unaligned_be_u16(&msg.msg.parameters[0], control->control_id);
	set_unaligned_be_u16(&msg.msg.parameters[3], control->reporting.remapped);

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_SPECIAL_KEYS_BUTTONS);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;
	msg.msg.parameters[2] |= 0x02;
	if (control->reporting.divert)
		msg.msg.parameters[2] |= 0x01;
	msg.msg.parameters[2] |= 0x08;
	if (control->reporting.persist)
		msg.msg.parameters[2] |= 0x04;
	msg.msg.parameters[2] |= 0x20;
	if (control->reporting.raw_XY)
		msg.msg.parameters[2] |= 0x10;

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
		.msg.report_id = REPORT_ID_SHORT,
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

	*resolution = get_unaligned_be_u16(msg.msg.parameters);
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
		.msg.report_id = REPORT_ID_SHORT,
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_ADJUSTABLE_DPI_GET_SENSOR_DPI_LIST,
		.msg.parameters[0] = sensor->index,
	};

	if (device->quirk == HIDPP20_QUIRK_G602) {
		msg.msg.parameters[0] = 1;
		i = 0;
	}

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	sensor->dpi_min = 0xffff;

	sensor->index = msg.msg.parameters[0];
	while (i < LONG_MESSAGE_LENGTH - 4U &&
	       get_unaligned_be_u16(&msg.msg.parameters[i]) != 0) {
		uint16_t value = get_unaligned_be_u16(&msg.msg.parameters[i]);

		if (device->quirk == HIDPP20_QUIRK_G602 && i == 2)
			value += 0xe000;

		if (value > 0xe000) {
			sensor->dpi_steps = value - 0xe000;
		} else {
			sensor->dpi_min = min(value, sensor->dpi_min);
			sensor->dpi_max = max(value, sensor->dpi_max);
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
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = reg,
		.msg.address = CMD_ADJUSTABLE_DPI_GET_SENSOR_DPI,
		.msg.parameters[0] = sensor->index,
	};

	if (device->quirk == HIDPP20_QUIRK_G602)
		msg.msg.parameters[0] = 1;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	sensor->dpi = get_unaligned_be_u16(&msg.msg.parameters[1]);
	sensor->default_dpi = get_unaligned_be_u16(&msg.msg.parameters[3]);

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
	return rc > 0 ? -EPROTO : rc;
}

int hidpp20_adjustable_dpi_set_sensor_dpi(struct hidpp20_device *device,
					  struct hidpp20_sensor *sensor, uint16_t dpi)
{
	uint8_t feature_index;
	uint16_t returned_parameters;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ADJUSTABLE_DPI_SET_SENSOR_DPI,
		.msg.parameters[0] = sensor->index,
	};

	if (device->quirk == HIDPP20_QUIRK_G602)
		msg.msg.parameters[0] = 1;

	set_unaligned_be_u16(&msg.msg.parameters[1], dpi);

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_DPI);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	returned_parameters = get_unaligned_be_u16(&msg.msg.parameters[1]);

	/* version 0 of the protocol does not echo the parameters */
	if (returned_parameters != dpi && returned_parameters)
		return -EIO;

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x8060 - Adjustable Report Rate                                            */
/* -------------------------------------------------------------------------- */

#define CMD_ADJUSTABLE_REPORT_RATE_GET_REPORT_RATE_LIST 0x00
#define CMD_ADJUSTABLE_REPORT_RATE_GET_REPORT_RATE	0x10
#define CMD_ADJUSTABLE_REPORT_RATE_SET_REPORT_RATE	0x20

int hidpp20_adjustable_report_rate_get_report_rate_list(struct hidpp20_device *device,
							uint8_t *bitflags_ms)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ADJUSTABLE_REPORT_RATE_GET_REPORT_RATE_LIST,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_REPORT_RATE);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*bitflags_ms = msg.msg.parameters[0];

	return 0;
}

int hidpp20_adjustable_report_rate_get_report_rate(struct hidpp20_device *device,
						   uint8_t *rate_ms)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ADJUSTABLE_REPORT_RATE_GET_REPORT_RATE,
		.msg.parameters[0] = 0,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_REPORT_RATE);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	*rate_ms = msg.msg.parameters[0];

	return 0;
}

int hidpp20_adjustable_report_rate_set_report_rate(struct hidpp20_device *device,
						   uint8_t rate_ms)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ADJUSTABLE_REPORT_RATE_SET_REPORT_RATE,
		.msg.parameters[0] = rate_ms,
	};

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ADJUSTABLE_REPORT_RATE);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

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

#define HIDPP20_PROFILE_SIZE		256
#define HIDPP20_BUTTON_HID		0x80

#define HIDPP20_MODE_NO_CHANGE				0x00
#define HIDPP20_ONBOARD_MODE				0x01
#define HIDPP20_HOST_MODE				0x02

#define HIDPP20_ONBOARD_PROFILES_MEMORY_TYPE_G402	0x01
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G402	0x01
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G303	0x02
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G900	0x03
#define HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G915	0x04
#define HIDPP20_ONBOARD_PROFILES_MACRO_TYPE_G402	0x01

#define HIDPP20_USER_PROFILES_G402			0x0000
#define HIDPP20_ROM_PROFILES_G402			0x0100

#define HIDPP20_PROFILE_DIR_END				0xFFFF
#define HIDPP20_PROFILE_DIR_ENABLED			2

union hidpp20_internal_profile {
	uint8_t data[HIDPP20_PROFILE_SIZE];
	struct {
		uint8_t report_rate;
		uint8_t default_dpi;
		uint8_t switched_dpi;
		uint16_t dpi[5];
		struct hidpp20_color profile_color;
		uint8_t power_mode;
		uint8_t angle_snapping;
		uint8_t reserved[10];
		uint16_t powersave_timeout;
		uint16_t poweroff_timeout;
		union hidpp20_button_binding buttons[16];
		union hidpp20_button_binding alternate_buttons[16];
		union {
			char txt[16 * 3];
			uint8_t raw[16 * 3];
		} name;
		struct hidpp20_internal_led leds[2]; /* G303, g502, g900 only */
		struct hidpp20_internal_led alt_leds[2];
		uint8_t free[2];
		uint16_t crc;
	} __attribute__((packed)) profile;
};
_Static_assert(sizeof(union hidpp20_internal_profile) == HIDPP20_PROFILE_SIZE, "Invalid size");

int
hidpp20_onboard_profiles_get_profiles_desc(struct hidpp20_device *device,
					   struct hidpp20_onboard_profiles_info *info)
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

	*info = *(struct hidpp20_onboard_profiles_info *)msg.msg.parameters;

	info->sector_size = hidpp_be_u16_to_cpu(info->sector_size);

	return 0;
}

int
hidpp20_onboard_profiles_read_sector(struct hidpp20_device *device,
				     uint16_t sector,
				     uint16_t sector_size,
				     uint8_t *data)
{
	uint16_t offset;
	uint8_t feature_index;
	int rc, count;
	union hidpp20_message buf;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_READ,
	};

	hidpp_log_debug(&device->base, "Reading sector 0x%04x\n", sector);

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	msg.msg.sub_id = feature_index;
	set_unaligned_be_u16(&msg.msg.parameters[0], sector);

	count = sector_size;

	for (offset = 0; offset < sector_size; offset += 16) {
		/*
		 * the firmware replies with an ERR_INVALID_ARGUMENT error
		 * if we try to read past sector_size - 16, so when we are left with
		 * less than 16 bytes to read we need to read from sector_size - 16
		 */
		offset = (sector_size - offset < 16) ? sector_size - 16 : offset;
		set_unaligned_be_u16(&msg.msg.parameters[2], offset);
		buf = msg;
		rc = hidpp20_request_command(device, &buf);
		if (rc)
			return rc;

		/* msg.msg.parameters is guaranteed to have a size >= 16 */
		memcpy(data + offset, buf.msg.parameters, 16);

		/*
		 * no need to check for count >= 0:
		 * if count is negative, then offset will be greater than
		 * sector_size, thus stopping the loop.
		 */
		count -= 16;
	}

	return 0;
}

static bool
hidpp20_onboard_profiles_is_sector_valid(struct hidpp20_device *device,
					 uint16_t sector_size,
					 uint8_t *data)
{
	uint16_t crc, read_crc;

	crc = hidpp_crc_ccitt(data, sector_size - 2);
	read_crc = get_unaligned_be_u16(&data[sector_size - 2]);

	if (crc != read_crc)
		hidpp_log_debug(&device->base, "Invalid CRC (%04x != %04x)\n", read_crc, crc);

	return crc == read_crc;
}

static int
hidpp20_onboard_profiles_write_start(struct hidpp20_device *device,
				     uint16_t sector,
				     uint16_t sub_address,
				     uint16_t count,
				     uint8_t feature_index)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = feature_index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_ADDR_WRITE,
	};

	set_unaligned_be_u16(&msg.msg.parameters[0], sector);
	set_unaligned_be_u16(&msg.msg.parameters[2], sub_address);
	set_unaligned_be_u16(&msg.msg.parameters[4], count);

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

static int
hidpp20_onboard_profiles_write_end(struct hidpp20_device *device,
				   uint8_t feature_index)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.sub_id = feature_index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_WRITE_END,
	};

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

static int
hidpp20_onboard_profiles_write_data(struct hidpp20_device *device,
				    uint8_t *data,
				    uint8_t feature_index)
{
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_LONG,
		.msg.device_idx = device->index,
		.msg.sub_id = feature_index,
		.msg.address = CMD_ONBOARD_PROFILES_MEMORY_WRITE,
	};

	memcpy(msg.msg.parameters, data, 16);

	rc = hidpp20_request_command(device, &msg);
	if (rc)
		return rc;

	return 0;
}

int
hidpp20_onboard_profiles_write_sector(struct hidpp20_device *device,
				      uint16_t sector,
				      uint16_t sector_size,
				      uint8_t *data,
				      bool write_crc)
{
	uint8_t feature_index;
	uint16_t crc;
	int rc, transferred;

	feature_index = hidpp_root_get_feature_idx(device,
						   HIDPP_PAGE_ONBOARD_PROFILES);
	if (feature_index == 0)
		return -ENOTSUP;

	if (write_crc) {
		crc = hidpp_crc_ccitt(data, sector_size - 2);
		set_unaligned_be_u16(&data[sector_size - 2], crc);
	}

	rc = hidpp20_onboard_profiles_write_start(device,
						  sector,
						  0,
						  sector_size,
						  feature_index);
	if (rc)
		return rc;

	for (transferred = 0; transferred < sector_size; transferred += 16) {
		rc = hidpp20_onboard_profiles_write_data(device, data, feature_index);
		if (rc)
			return rc;
		data += 16;
	}

	rc = hidpp20_onboard_profiles_write_end(device, feature_index);
	if (rc)
		return rc;

	return 0;
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
hidpp20_onboard_profiles_set_current_dpi_index(struct hidpp20_device *device,
					       uint8_t index)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_SET_CURRENT_DPI_INDEX,
		.msg.parameters[0] = index,
	};

	if (index > 4)
		return -EINVAL;

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
hidpp20_onboard_profiles_get_current_dpi_index(struct hidpp20_device *device)
{
	uint8_t feature_index;
	int rc;
	union hidpp20_message msg = {
		.msg.report_id = REPORT_ID_SHORT,
		.msg.device_idx = device->index,
		.msg.address = CMD_ONBOARD_PROFILES_GET_CURRENT_DPI_INDEX,
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

static bool
hidpp20_onboard_profiles_validate(struct hidpp20_device *device,
				  struct hidpp20_onboard_profiles_info *info)
{
	if (info->memory_model_id != HIDPP20_ONBOARD_PROFILES_MEMORY_TYPE_G402) {
		hidpp_log_error(&device->base,
				"Memory layout not supported: 0x%02x.\n",
				info->memory_model_id);
		return false;
	}

	if ((info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G402) &&
	    (info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G303) &&
	    (info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G900) &&
	    (info->profile_format_id != HIDPP20_ONBOARD_PROFILES_PROFILE_TYPE_G915)) {
		hidpp_log_error(&device->base,
				"Profile layout not supported: 0x%02x.\n",
				info->profile_format_id);
		return false;
	}

	if (info->macro_format_id != HIDPP20_ONBOARD_PROFILES_MACRO_TYPE_G402) {
		hidpp_log_error(&device->base,
				"Macro format not supported: 0x%02x.\n",
				info->macro_format_id);
		return false;
	}

	return true;
}

int
hidpp20_onboard_profiles_allocate(struct hidpp20_device *device,
				  struct hidpp20_profiles **profiles_list)
{
	struct hidpp20_onboard_profiles_info info = { 0 };
	struct hidpp20_profiles *profiles;
	int onboard_mode;
	int rc;

	rc = hidpp20_onboard_profiles_get_profiles_desc(device, &info);
	if (rc)
		return rc;

	if (!hidpp20_onboard_profiles_validate(device, &info))
		return -ENOTSUP;

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

	profiles = zalloc(sizeof(struct hidpp20_profiles));
	profiles->profiles = zalloc(info.profile_count * sizeof(struct hidpp20_profile));
	profiles->sector_size = info.sector_size;
	profiles->sector_count = info.sector_count;
	profiles->num_profiles = info.profile_count;
	profiles->num_rom_profiles = info.profile_count_oob;
	profiles->num_buttons = min(info.button_count, 16);
	profiles->num_modes = HIDPP20_DPI_COUNT;
	profiles->num_leds = HIDPP20_LED_COUNT;
	profiles->has_g_shift = (info.mechanical_layout & 0x03) == 0x02;
	profiles->has_dpi_shift = ((info.mechanical_layout & 0x0c) >> 2) == 0x02;
	switch(info.various_info & 0x07) {
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
				    struct hidpp20_profiles *profiles,
				    uint8_t page, uint8_t offset,
				    union hidpp20_macro_data **return_macro)
{
	_cleanup_free_ uint8_t *memory = NULL;
	union hidpp20_macro_data *macro = NULL;
	unsigned count = 0;
	unsigned index = 0;
	uint16_t mem_index = offset;
	int rc = -ENOMEM;

	memory = hidpp20_onboard_profiles_allocate_sector(profiles);

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
			rc = hidpp20_onboard_profiles_read_sector(device,
								  page,
								  profiles->sector_size,
								  memory);
			if (rc)
				goto out_err;
		}

		rc = hidpp20_onboard_profiles_macro_next(device,
							 memory,
							 &mem_index,
							 &macro[index]);
		if (rc == -EFAULT)
			goto out_err;
		if (rc == -ENOMEM) {
			mem_index = 0;
			page++;
		} else if (macro[index].any.type == HIDPP20_MACRO_JUMP) {
			page = macro[index].jump.page;
			offset = macro[index].jump.offset;
			mem_index = offset;
			/* no need to store the jump in memory */
			index--;
			/* force memory fetching */
			rc = -ENOMEM;
		} else {
			index++;
		}
	} while (rc);

	*return_macro = macro;
	return index;

out_err:
	free(macro);

	return rc;
}

static int
hidpp20_onboard_profiles_parse_macro(struct hidpp20_device *device,
				     struct hidpp20_profiles *profiles,
				     uint8_t page, uint8_t offset,
				     union hidpp20_macro_data **return_macro)
{
	union hidpp20_macro_data *m, *macro = NULL;
	unsigned i, count = 0;
	int rc;

	rc = hidpp20_onboard_profiles_read_macro(device, profiles, page, offset, &macro);
	if (rc <= 0)
		return rc;

	count = rc;

	for (i = 0; i < count; i++) {
		m = &macro[i];
		assert(m != NULL);
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
hidpp20_onboard_profiles_compute_dict_size(const struct hidpp20_device *device,
					   const struct hidpp20_profiles *profiles)
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

void
hidpp20_onboard_profiles_destroy(struct hidpp20_profiles *profiles_list)
{
	struct hidpp20_profile *profile;
	union hidpp20_macro_data **macro;
	unsigned i;

	if (!profiles_list)
		return;

	for (i = 0; i < profiles_list->num_profiles; i++) {
		profile = &profiles_list->profiles[i];

		ARRAY_FOR_EACH(profile->macros, macro) {
			free(*macro);
		}
	}

	free(profiles_list->profiles);
	free(profiles_list);
}

static int
hidpp20_onboard_profiles_write_dict(struct hidpp20_device *device,
				    struct hidpp20_profiles *profiles_list)
{
	unsigned int i, buffer_index = 0;
	uint16_t sector_size = profiles_list->sector_size;
	_cleanup_free_ uint8_t *data = NULL;
	int rc;

	data = hidpp20_onboard_profiles_allocate_sector(profiles_list);

	for (i = 0; i < profiles_list->num_profiles; i++) {
		data[buffer_index++] = 0x00;
		data[buffer_index++] = i + 1;
		data[buffer_index++] = !!profiles_list->profiles[i].enabled;
		data[buffer_index++] = 0x00;
	}

	data[buffer_index++] = 0xFF;
	data[buffer_index++] = 0xFF;

	data[buffer_index++] = 0x00;
	data[buffer_index++] = 0x00;

	memset(data + buffer_index, 0xff, sector_size - buffer_index);

	hidpp_log_buf_raw(&device->base,
			   "dictionary: ",
			   data,
			   hidpp20_onboard_profiles_compute_dict_size(device,
								      profiles_list));

	rc = hidpp20_onboard_profiles_write_sector(device,
						   0x0000,
						   sector_size,
						   data,
						   true);
	if (rc)
		hidpp_log_error(&device->base, "failed to write profile dictionary\n");

	return rc;
}

static void
hidpp20_buttons_to_cpu(struct hidpp20_device *device,
		       struct hidpp20_profiles *profiles,
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
			button->special.profile = b->special.profile;
			break;
		case HIDPP20_BUTTON_MACRO:
			if (profile->macros[i]) {
				free(profile->macros[i]);
				profile->macros[i] = NULL;
			}
			hidpp20_onboard_profiles_parse_macro(device,
							     profiles,
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
			memcpy(b, button, sizeof(*b));
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
			button->special.profile = b->special.profile;
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
			memcpy(b, button, sizeof(*b));
			break;
		}
	}
}

void
hidpp20_onboard_profiles_read_led(struct hidpp20_led *led,
				  struct hidpp20_internal_led internal_led)
{
	uint16_t period = 0;
	uint8_t brightness = 0;

	led->mode = (enum hidpp20_led_mode)internal_led.mode;

	switch (led->mode) {
	case HIDPP20_LED_CYCLE:
		period = hidpp_be_u16_to_cpu(internal_led.effect.cycle.period_or_speed);
		brightness = internal_led.effect.cycle.intensity;
		if (brightness == 0)
			brightness = 100;
		break;
	case HIDPP20_LED_STARLIGHT:
		led->color = internal_led.effect.starlight.color_sky;
		led->extra_color = internal_led.effect.starlight.color_star;
		break;
	case HIDPP20_LED_BREATHING:
		period = hidpp_be_u16_to_cpu(internal_led.effect.breath.period_or_speed);
		brightness = internal_led.effect.breath.intensity;
		if (brightness == 0)
			brightness = 100;
		led->color = internal_led.effect.breath.color;
		break;
	case HIDPP20_LED_RIPPLE:
		period = hidpp_be_u16_to_cpu(internal_led.effect.breath.period_or_speed);
		led->color = internal_led.effect.ripple.color;
		break;
	case HIDPP20_LED_ON:
		led->color = internal_led.effect.fixed.color;
		break;
	case HIDPP20_LED_OFF:
		break;
	default:
		memcpy(led->original, &internal_led, sizeof(internal_led));
		break;
	}

	led->period = period;
	led->brightness = brightness;
}

static int
hidpp20_onboard_profiles_parse_profile(struct hidpp20_device *device,
				       struct hidpp20_profiles *profiles_list,
				       unsigned index,
				       bool check_crc)
{
	union hidpp20_internal_profile *pdata;
	struct hidpp20_profile *profile = &profiles_list->profiles[index];
	uint16_t sector = profile->address;
	_cleanup_free_ uint8_t *data = NULL;
	unsigned i;
	int rc;

	if (index >= profiles_list->num_profiles)
		return -EINVAL;

	data = hidpp20_onboard_profiles_allocate_sector(profiles_list);
	pdata = (union hidpp20_internal_profile *)data;

	rc = hidpp20_onboard_profiles_read_sector(device,
						  sector,
						  profiles_list->sector_size,
						  data);
	if (rc < 0)
		return rc;

	if (check_crc) {
		if (!hidpp20_onboard_profiles_is_sector_valid(device,
							      profiles_list->sector_size,
							      data)) {
			return -EAGAIN;
		}
	}

	profile->report_rate = 1000 / max(1, pdata->profile.report_rate);
	profile->default_dpi = pdata->profile.default_dpi;
	profile->switched_dpi = pdata->profile.switched_dpi;

	profile->powersave_timeout = pdata->profile.powersave_timeout;
	profile->poweroff_timeout = pdata->profile.poweroff_timeout;

	for (i = 0; i < 5; i++) {
		profile->dpi[i] = get_unaligned_le_u16(&data[2 * i + 3]);
	}

	for (i = 0; i < profiles_list->num_leds; i++)
	{
		hidpp20_onboard_profiles_read_led(&profile->leds[i], pdata->profile.leds[i]);
		hidpp20_onboard_profiles_read_led(&profile->alt_leds[i], pdata->profile.alt_leds[i]);
	}

	hidpp20_buttons_to_cpu(device, profiles_list, profile, pdata->profile.buttons, profiles_list->num_buttons);

	memcpy(profile->name, pdata->profile.name.txt, sizeof(profile->name));
	/* force terminating '\0' */
	profile->name[sizeof(profile->name) - 1] = '\0';

	/* check if we are using the default name or not */
	for (i = 0; i < sizeof(profile->name); i++) {
		if (pdata->profile.name.raw[i] != 0xff)
			break;
	}
	if (i == sizeof(profile->name))
		memset(profile->name, 0, sizeof(profile->name));

	return 0;
}

int
hidpp20_onboard_profiles_initialize(struct hidpp20_device *device,
				    struct hidpp20_profiles *profiles)
{
	_cleanup_free_ uint8_t *data = NULL;
	int rc;
	unsigned i;
	uint16_t addr;
	bool crc_valid;
	bool read_userdata = true;

	assert(profiles);

	for (i = 0; i < profiles->num_profiles; i++) {
		profiles->profiles[i].address = 0;
		profiles->profiles[i].enabled = 0;
	}

	data = hidpp20_onboard_profiles_allocate_sector(profiles);

	rc = hidpp20_onboard_profiles_read_sector(device,
						  HIDPP20_USER_PROFILES_G402,
						  profiles->sector_size,
						  data);

	if (rc && device->quirk == HIDPP20_QUIRK_G305) {
		/* The G305 has a bug where it throws an ERR_INVALID_ARGUMENT
		   if the sector has not been written to yet. If this happens
		   we will read the ROM profiles.*/
		read_userdata = false;
		goto read_profiles;
	}

	if (rc < 0)
		return rc; // ignore_clang_sa_mem_leak

	crc_valid = hidpp20_onboard_profiles_is_sector_valid(device,
							     profiles->sector_size,
							     data);
	if (crc_valid) {
		for (i = 0; i < profiles->num_profiles; i++) {
			uint8_t *d = data + 4 * i;

			addr = get_unaligned_be_u16(d);

			if(addr == HIDPP20_PROFILE_DIR_END)
				break;

			profiles->profiles[i].address = addr;

			/* profile address sanity check */
			if (profiles->profiles[i].address != (HIDPP20_USER_PROFILES_G402 | (i + 1)))
				hidpp_log_info(&device->base,
					       "profile %d: error in the address: 0x%04x instead of 0x%04x\n",
					       i + 1,
					       profiles->profiles[i].address,
					       HIDPP20_USER_PROFILES_G402 | (i + 1));

			profiles->profiles[i].enabled = !!d[HIDPP20_PROFILE_DIR_ENABLED];
		}
	} else {
		hidpp_log_debug(&device->base, "Profile directory has an invalid CRC... Reading ROM profiles.\n");

		read_userdata = false;
	}

read_profiles:
	for (i = 0; i < profiles->num_profiles; i++) {
		if (read_userdata) {
			hidpp_log_debug(&device->base, "Parsing profile %u\n", i);
			rc = hidpp20_onboard_profiles_parse_profile(device,
								    profiles,
								    i,
								    true);

			/* on fail to read the user profile fallback to the default profile */
			if (rc)
				hidpp_log_debug(&device->base, "Profile %u is bad. Falling back to the ROM settings.\n", i);
			else
				continue;
		}

		/* the number of rom profiles can be different than the number of user profiles
		   so we if there are not enough rom profiles to populate all the user profiles
		   we just use the first rom profile */
		if (i + 1 > profiles->num_rom_profiles)
			profiles->profiles[i].address = HIDPP20_ROM_PROFILES_G402 + 1;
		else
			profiles->profiles[i].address = HIDPP20_ROM_PROFILES_G402 + i + 1;

		rc = hidpp20_onboard_profiles_parse_profile(device,
							profiles,
							i,
							false);
		if (rc < 0)
			return rc;
	}

	return profiles->num_profiles;
}

void
hidpp20_onboard_profiles_write_led(struct hidpp20_internal_led *internal_led,
				   struct hidpp20_led *led)
{
	uint16_t period = led->period;
	uint8_t brightness = led->brightness;

	memset(internal_led, 0, sizeof(*internal_led));

	internal_led->mode = (uint8_t)led->mode;

	switch (led->mode) {
	case HIDPP20_LED_CYCLE:
		internal_led->effect.cycle.period_or_speed = hidpp_cpu_to_be_u16(period);
		if (brightness < 100)
			internal_led->effect.cycle.intensity = brightness;
		else
			internal_led->effect.cycle.intensity = 0;
		break;
	case HIDPP20_LED_STARLIGHT:
		internal_led->effect.starlight.color_sky = led->color;
		internal_led->effect.starlight.color_star = led->extra_color;
		break;
	case HIDPP20_LED_BREATHING:
		internal_led->effect.breath.color = led->color;
		internal_led->effect.breath.period_or_speed = hidpp_cpu_to_be_u16(period);
		if (brightness < 100)
			internal_led->effect.breath.intensity = brightness;
		else
			internal_led->effect.breath.intensity = 0;
		break;
	case HIDPP20_LED_RIPPLE:
		internal_led->effect.ripple.color = led->color;
		internal_led->effect.ripple.period = hidpp_cpu_to_be_u16(period);
		break;
	case HIDPP20_LED_ON:
		internal_led->effect.fixed.color = led->color;
		internal_led->effect.fixed.effect = 0;
		break;
	case HIDPP20_LED_OFF:
		break;
	default:
		memcpy(internal_led, led->original, sizeof(*internal_led));
		break;
	}
}

static int
hidpp20_onboard_profiles_write_profile(struct hidpp20_device *device,
				       struct hidpp20_profiles *profiles_list,
				       unsigned int index)
{
	union hidpp20_internal_profile *pdata;
	_cleanup_free_ uint8_t *data = NULL;
	uint16_t sector_size = profiles_list->sector_size;
	uint16_t sector = index + 1;
	struct hidpp20_profile *profile = &profiles_list->profiles[index];
	unsigned i;
	int rc;

	if (index >= profiles_list->num_profiles)
		return -EINVAL;

	data = hidpp20_onboard_profiles_allocate_sector(profiles_list);
	pdata = (union hidpp20_internal_profile *)data;

	memset(data, 0xff, profiles_list->sector_size);

	pdata->profile.report_rate = 1000 / profile->report_rate;
	pdata->profile.default_dpi = profile->default_dpi;
	pdata->profile.switched_dpi = profile->switched_dpi;

	pdata->profile.powersave_timeout = profile->powersave_timeout;
	pdata->profile.poweroff_timeout = profile->poweroff_timeout;

	for (i = 0; i < 5; i++) {
		pdata->profile.dpi[i] = hidpp_cpu_to_le_u16(profile->dpi[i]);
	}

	for (i = 0; i < profiles_list->num_leds; i++)
	{
		hidpp20_onboard_profiles_write_led(&pdata->profile.leds[i], &profile->leds[i]);
		/* we write the current led instead of the stored value */
		hidpp20_onboard_profiles_write_led(&pdata->profile.alt_leds[i], &profile->leds[i]);
	}

	hidpp20_buttons_from_cpu(profile, pdata->profile.buttons, profiles_list->num_buttons);

	memcpy(pdata->profile.name.txt, profile->name, sizeof(profile->name));

	rc = hidpp20_onboard_profiles_write_sector(device, sector, sector_size, data, true);
	if (rc < 0) {
		hidpp_log_error(&device->base, "failed to write profile\n");
		return rc;
	}

	return 0;
}

int
hidpp20_onboard_profiles_commit(struct hidpp20_device *device,
				struct hidpp20_profiles *profiles_list)
{
	struct hidpp20_profile *profile;
	unsigned int i;
	bool enabled_profile = false;
	int rc;

	for (i = 0; i < profiles_list->num_profiles; i++) {
		profile = &profiles_list->profiles[i];

		if (profile->enabled) {
			rc = hidpp20_onboard_profiles_write_profile(device,
								    profiles_list,
								    i);
			if (rc < 0)
				return rc;

			enabled_profile = true;
		}
	}

	if (!enabled_profile) {
		if (profiles_list->num_profiles > 0) {
			profiles_list->profiles[0].enabled = 1;
			rc = hidpp20_onboard_profiles_write_profile(device,
			                                            profiles_list,
			                                            0);
			if (rc < 0)
				return rc;
		}
	}

	return hidpp20_onboard_profiles_write_dict(device, profiles_list);
}

static const enum ratbag_button_action_special hidpp20_profiles_specials[] = {
	[0x00] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
	[0x01] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,
	[0x02] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,
	[0x03] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,
	[0x04] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,
	[0x05] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,
	[0x06] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT,
	[0x07] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE,
	[0x08] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP,
	[0x09] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN,
	[0x0a] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,
	[0x0b] = RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE,

	[0x0c ... 0xff] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
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
hidpp20_device_new(const struct hidpp_device *base, unsigned int idx, struct hidpp_hid_report *reports, unsigned int num_reports)
{
	struct hidpp20_device *dev;
	int rc;

	dev = zalloc(sizeof(*dev));

	dev->index = idx;
	dev->base = *base;

	dev->proto_major = 1;
	dev->proto_minor = 0;

	dev->led_ext_caps = 0;

	hidpp_get_supported_report_types(&(dev->base), reports, num_reports);

	if (!(dev->base.supported_report_types & HIDPP_REPORT_SHORT) &&
	    !(dev->base.supported_report_types & HIDPP_REPORT_LONG))
	    goto err;

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
