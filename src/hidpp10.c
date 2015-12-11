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
#include <poll.h>
#include <unistd.h>
#include <limits.h>
#include <math.h>

#include "hidpp10.h"

#include "libratbag-util.h"
#include "libratbag-hidraw.h"

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
	[0x06] = "Reserved for future",
	[0x07] = "Reserved for future",
	[0x08] = "Trackball",
	[0x09] = "Touchpad",
	[0x0A ... 0xFE] = NULL,
};

int
hidpp10_build_dpi_table_from_list(struct hidpp10_device *dev,
				  const char *str_list)
{
	unsigned int i, count, index;
	int nread, dpi = 0;
	char c;
	/*
	 * str_list is in the form:
	 * "0;200;400;600;800;1000;1200"
	 */

	/* first, count how many elements do we have in the table */
	count = 1;
	i = 0;
	while (str_list[i] != 0) {
		c = str_list[i++];
		if (c == ';')
			count++;
	}

	index = 0;

	/* check that the max raw value fits in a uint8_t */
	if (count + 0x80 - 1> 0xff)
		return -EINVAL;

	dev->dpi_count = count;
	dev->dpi_table = zalloc(count * sizeof(*dev->dpi_table));

	while (*str_list != 0 && index < count) {
		if (*str_list == ';') {
			str_list++;
			continue;
		}

		nread = 0;
		sscanf(str_list, "%d%n", &dpi, &nread);
		if (!nread || dpi < 0)
			goto err;

		dev->dpi_table[index].raw_value = index + 0x80;
		dev->dpi_table[index].dpi = dpi;

		str_list += nread;
		index++;
	}

	if (index != count)
		goto err;

	/* mark the state as invalid */
	dev->profile_count = 0;

	return 0;

err:
	dev->dpi_count = 0;
	free(dev->dpi_table);
	dev->dpi_table = NULL;
	return -EINVAL;
}

int
hidpp10_build_dpi_table_from_dpi_info(struct hidpp10_device *dev,
				      const char *str_dpi)
{
	float min, max, step;
	unsigned raw_max, i;
	int rc;
	/*
	 * str_list is in the form:
	 * "MIN:MAX@STEP"
	 */

	rc = sscanf(str_dpi, "%f:%f@%f", &min, &max, &step);
	if (rc != 3)
		return -EINVAL;

	raw_max = (max - min) / step;
	if (raw_max > 0xff)
		return -EINVAL;

	dev->dpi_count = raw_max + 1;
	dev->dpi_table = zalloc((raw_max + 1) * sizeof(*dev->dpi_table));

	for (i = 1; i <= raw_max; i++) {
		dev->dpi_table[i].raw_value = i;
		dev->dpi_table[i].dpi = round((min + step * i) / 25.0f) * 25;
	}

	/* mark the state as invalid */
	dev->profile_count = 0;

	return 0;
}


static int
hidpp10_request_command(struct hidpp10_device *dev, union hidpp10_message *msg)
{
	union hidpp10_message read_buffer;
	union hidpp10_message expected_header;
	union hidpp10_message expected_error_dev = ERROR_MSG(msg, msg->msg.device_idx);
	int ret;
	uint8_t hidpp_err = 0;
	int command_size;

	switch (msg->msg.report_id) {
	case REPORT_ID_SHORT:
		command_size = SHORT_MESSAGE_LENGTH;
		break;
	case REPORT_ID_LONG:
		command_size = LONG_MESSAGE_LENGTH;
		break;
	default:
		abort();
	}

	/* create the expected header */
	expected_header = *msg;

	/* response message length doesn't depend on request length */
	hidpp_log_buf_raw(&dev->base, "  expected_header:		?? ", &expected_header.data[1], 3);
	hidpp_log_buf_raw(&dev->base, "  expected_error_dev:	", expected_error_dev.data, SHORT_MESSAGE_LENGTH);

	/* Send the message to the Device */
	ret = hidpp_write_command(&dev->base, msg->data, command_size);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = hidpp_read_response(&dev->base, read_buffer.data, LONG_MESSAGE_LENGTH);

		/* Wait and retry if the USB timed out */
		if (ret == -ETIMEDOUT) {
			msleep(10);
			ret = hidpp_read_response(&dev->base, read_buffer.data, LONG_MESSAGE_LENGTH);
		}

		/* Overwrite the return device index with ours. The kernel
		 * sets our device index on write, but gives us the real
		 * device index on reply. Overwrite it with our index so the
		 * messages are easier to check and compare.
		 */
		read_buffer.msg.device_idx = msg->msg.device_idx;

		/* actual answer */
		if (!memcmp(&read_buffer.data[1], &expected_header.data[1], 3))
			break;

		/* error */
		if (!memcmp(read_buffer.data, expected_error_dev.data, 5)) {
			hidpp_err = read_buffer.msg.parameters[1];
			hidpp_log_raw(&dev->base,
				"    HID++ error from the %s (%d): %s (%02x)\n",
				read_buffer.msg.device_idx == HIDPP_RECEIVER_IDX ? "receiver" : "device",
				read_buffer.msg.device_idx,
				hidpp_errors[hidpp_err] ? hidpp_errors[hidpp_err] : "Undocumented error code",
				hidpp_err);
			break;
		}
	} while (ret > 0);

	if (ret < 0) {
		hidpp_log_error(&dev->base, "    USB error: %s (%d)\n", strerror(-ret), -ret);
		perror("write");
		goto out_err;
	}

	if (!hidpp_err) {
		hidpp_log_buf_raw(&dev->base, "  received: ", read_buffer.data, ret);
		/* copy the answer for the caller */
		*msg = read_buffer;
	}

	ret = hidpp_err;

out_err:
	return ret;
}
/* -------------------------------------------------------------------------- */
/* HID++ 1.0 commands 10                                                      */
/* -------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------- */
/* 0x00: Enable HID++ Notifications                                           */
/* -------------------------------------------------------------------------- */

#define __CMD_HIDPP_NOTIFICATIONS		0x00

#define CMD_HIDPP_NOTIFICATIONS(idx, sub)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_HIDPP_NOTIFICATIONS, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_hidpp_notifications(struct hidpp10_device *dev,
				uint32_t *reporting_flags)
{
	unsigned idx = dev->index;
	union hidpp10_message notifications = CMD_HIDPP_NOTIFICATIONS(idx, GET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching HID++ notifications\n");

	res = hidpp10_request_command(dev, &notifications);
	if (res)
		return res;

	*reporting_flags = notifications.msg.parameters[0];
	*reporting_flags |= (notifications.msg.parameters[0] & 0x1F) << 8;
	*reporting_flags |= (notifications.msg.parameters[2] & 0x7 ) << 16;

	return res;
}

int
hidpp10_set_hidpp_notifications(struct hidpp10_device *dev,
				uint32_t reporting_flags)
{
	unsigned idx = dev->index;
	union hidpp10_message notifications = CMD_HIDPP_NOTIFICATIONS(idx, SET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Setting HID++ notifications\n");

	notifications.msg.parameters[0] = reporting_flags & 0xFF;
	notifications.msg.parameters[1] = (reporting_flags >> 8) & 0x1F;
	notifications.msg.parameters[2] = (reporting_flags >> 16) & 0x7;

	res = hidpp10_request_command(dev, &notifications);

	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x01: Enable Individual Features                                           */
/* -------------------------------------------------------------------------- */

#define __CMD_ENABLE_INDIVIDUAL_FEATURES	0x01

#define CMD_ENABLE_INDIVIDUAL_FEATURES(idx, sub)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_ENABLE_INDIVIDUAL_FEATURES, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_individual_features(struct hidpp10_device *dev,
				uint32_t *feature_mask)
{
	unsigned idx = dev->index;
	union hidpp10_message features = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, GET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching individual features\n");

	res = hidpp10_request_command(dev, &features);
	if (res)
		return res;

	*feature_mask = features.msg.parameters[0];
	/* bits 0 and 4-7 are reserved */
	*feature_mask |= (features.msg.parameters[1] & 0x0E) << 8;
	/* bits 6-7 are reserved */
	*feature_mask |= (features.msg.parameters[2] & 0x3F) << 16;

	return 0;
}

int
hidpp10_set_individual_features(struct hidpp10_device *dev,
				uint32_t feature_mask)
{
	unsigned idx = HIDPP_RECEIVER_IDX;
	union hidpp10_message mode = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, SET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Setting individual features\n");

	mode.msg.device_idx = dev->index;

	mode.msg.parameters[0] = feature_mask & 0xFF;
	mode.msg.parameters[1] = (feature_mask >> 8) & 0x0E;
	mode.msg.parameters[2] = (feature_mask >> 16) & 0x3F;

	res = hidpp10_request_command(dev, &mode);

	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x07: Battery status                                                       */
/* -------------------------------------------------------------------------- */
#define __CMD_BATTERY_STATUS			0x07

#define CMD_BATTERY_STATUS(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_BATTERY_STATUS, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_battery_status(struct hidpp10_device *dev,
			   enum hidpp10_battery_level *level,
			   enum hidpp10_battery_charge_state *charge_state,
			   uint8_t *low_threshold_in_percent)
{
	unsigned idx = dev->index;
	union hidpp10_message battery = CMD_BATTERY_STATUS(idx, GET_REGISTER_REQ);
	int res;

	res = hidpp10_request_command(dev, &battery);

	*level = battery.msg.parameters[0];
	*charge_state = battery.msg.parameters[1];
	*low_threshold_in_percent = battery.msg.parameters[2];

	if (*low_threshold_in_percent >= 7) {
		/* reserved value, we just silently truncate it to 0 */
		*low_threshold_in_percent = 0;
	}

	*low_threshold_in_percent *= 5; /* in 5% increments */

	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x0D: Battery mileage                                                      */
/* -------------------------------------------------------------------------- */
#define __CMD_BATTERY_MILEAGE			0x0D

#define CMD_BATTERY_MILEAGE(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_BATTERY_MILEAGE, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_battery_mileage(struct hidpp10_device *dev,
			    uint8_t *level_in_percent,
			    uint32_t *max_seconds,
			    enum hidpp10_battery_charge_state *state)
{
	unsigned idx = dev->index;
	union hidpp10_message battery = CMD_BATTERY_MILEAGE(idx, GET_REGISTER_REQ);
	int res;
	int max;

	res = hidpp10_request_command(dev, &battery);

	*level_in_percent = battery.msg.parameters[0] & 0x7F;

	max = battery.msg.parameters[1];
	max |= (battery.msg.parameters[2] & 0xF) << 8;

	switch((battery.msg.parameters[2] & 0x30) >> 4) {
	case 0x03: /* days */
		max *= 24;

		/* fallthrough */
	case 0x02: /* hours */
		max *= 60;

		/* fallthrough */
	case 0x01: /* min */
		max *= 60;
		break;
	case 0x00: /* seconds */
		break;
	}

	*max_seconds = max;

	switch(battery.msg.parameters[2] >> 6) {
	case 0x00:
		*state = HIDPP10_BATTERY_CHARGE_STATE_NOT_CHARGING;
		break;
	case 0x01:
		*state = HIDPP10_BATTERY_CHARGE_STATE_CHARGING;
		break;
	case 0x02:
		*state = HIDPP10_BATTERY_CHARGE_STATE_CHARGING_COMPLETE;
		break;
	case 0x03:
		*state = HIDPP10_BATTERY_CHARGE_STATE_CHARGING_ERROR;
		break;
	}

	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x0F: Profile queries                                                      */
/* -------------------------------------------------------------------------- */
#define __CMD_PROFILE				0x0F

#define PROFILE_TYPE_INDEX			0x00
#define PROFILE_TYPE_ADDRESS			0x01
#define PROFILE_TYPE_EEPROM			0xEE
#define PROFILE_TYPE_FACTORY			0xFF

#define CMD_PROFILE(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_PROFILE, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

struct _hidpp10_dpi_mode_8 {
	uint8_t xres;
	uint8_t yres;
	uint8_t led1:4;
	uint8_t led2:4;
	uint8_t led3:4;
	uint8_t led4:4;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_dpi_mode_8) == 4, "Invalid size");

struct _hidpp10_dpi_mode_16 {
	uint16_t xres;
	uint16_t yres;
	uint8_t led1:4;
	uint8_t led2:4;
	uint8_t led3:4;
	uint8_t led4:4;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_dpi_mode_16) == 6, "Invalid size");

union _hidpp10_button_binding {
	struct {
		uint8_t type;
		uint8_t pad;
		uint8_t pad1;
	} any;
	struct {
		uint8_t type; /* 0x81 */
		uint16_t button_flags;
	} __attribute__((packed)) button;
	struct {
		uint8_t type; /* 0x82 */
		uint8_t modifier_flags;
		uint8_t key;
	} keyboard_keys;
	struct {
		uint8_t type; /* 0x83 */
		uint16_t flags;
	} __attribute__((packed))  special;
	struct {
		uint8_t type; /* 0x84 */
		uint16_t consumer_control;
	} __attribute__((packed))  consumer_control;
	struct {
		uint8_t type; /* 0x8F */
		uint8_t zero0;
		uint8_t zero1;
	} disabled;
} __attribute__((packed));
_Static_assert(sizeof(union _hidpp10_button_binding) == 3, "Invalid size");

struct _hidpp10_profile_500 {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t unknown;
	struct _hidpp10_dpi_mode_16 dpi_modes[PROFILE_NUM_DPI_MODES];
	uint8_t angle_correction;
	uint8_t default_dpi_mode;
	uint8_t unknown2[2];
	uint8_t usb_refresh_rate;
	union _hidpp10_button_binding buttons[PROFILE_NUM_BUTTONS];
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_profile_500) == 78, "Invalid size");

static const uint8_t _hidpp10_profile_700_unknown1[3] = { 0x80, 0x01, 0x10 };
static const uint8_t _hidpp10_profile_700_unknown2[10] = { 0x01, 0x2c, 0x02, 0x58, 0x64, 0xff, 0xbc, 0x00, 0x09, 0x31 };
static const uint8_t _hidpp10_profile_700_unknown3[5] = { 0x4c, 0x47, 0x53, 0x30, 0x32 };

struct _hidpp10_profile_700 {
	struct _hidpp10_dpi_mode_8 dpi_modes[PROFILE_NUM_DPI_MODES];
	uint8_t default_dpi_mode;
	uint8_t unknown1[3];
	uint8_t usb_refresh_rate;
	uint8_t unknown2[10];
	union _hidpp10_button_binding buttons[PROFILE_NUM_BUTTONS];
	uint8_t unknown3[5];
	uint16_t name[23];
	uint16_t macro_names[11][17];
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_profile_700) == 499, "Invalid size");

union _hidpp10_profile_data {
	struct _hidpp10_profile_500 profile_500;
	struct _hidpp10_profile_700 profile_700;
	uint8_t data[HIDPP10_PAGE_SIZE];
};
_Static_assert((sizeof(union _hidpp10_profile_data) % 16) == 0, "Invalid size");

static uint8_t
hidpp10_get_dpi_mapping(struct hidpp10_device *dev, unsigned int value)
{
	struct hidpp10_dpi_mapping *m;
	unsigned int i, delta, min_delta;
	uint8_t result = 0;

	if (!dev->dpi_table)
		return value / 50;

	m = dev->dpi_table;

	min_delta = INT_MAX;

	for (i = 0; i < dev->dpi_count; i++) {
		delta = abs(value - m[i].dpi);
		if (delta < min_delta) {
			result = m[i].raw_value;
			min_delta = delta;
		}
	}

	return result;
}

static unsigned int
hidpp10_get_dpi_value(struct hidpp10_device *dev, uint8_t raw_value)
{
	struct hidpp10_dpi_mapping *m;
	unsigned int i;

	if (!dev->dpi_table)
		return raw_value * 50;

	m = dev->dpi_table;

	for (i = 0; i < dev->dpi_count; i++) {
		if (raw_value == m[i].raw_value)
			return m[i].dpi;
	}

	return 0;
}

int
hidpp10_get_profile_directory(struct hidpp10_device *dev,
			      struct hidpp10_directory *out,
			      size_t nelems)
{
	unsigned int i;
	int res;
	uint8_t bytes[HIDPP10_PAGE_SIZE] = { 0 };
	struct hidpp10_directory *directory = (struct hidpp10_directory *)bytes;
	size_t count;

	if (dev->profile_type == HIDPP10_PROFILE_UNKNOWN) {
		hidpp_log_debug(&dev->base, "no profile type given\n");
		return 0;
	}

	if (dev->profile_directory) {
		count = 0;
		while (dev->profile_directory[count].page) {
			count++;
		}

		goto out;
	}

	hidpp_log_raw(&dev->base, "Fetching the profiles' directory\n");

	res = hidpp10_read_page(dev, 0x01, bytes);
	if (res)
		return res;

	count = 0;
	/* assume 16 profiles max */
	for (i = 0; i < 16; i++) {
		if (directory[i].page == 0xFF)
			break;
		count++;
	}

	dev->profile_directory = zalloc((count + 1) * sizeof(struct hidpp10_directory));
	memcpy(dev->profile_directory, directory, count  * sizeof(struct hidpp10_directory));

out:
	if (dev->profile_count != count) {
		if (dev->profiles)
			free(dev->profiles);
		dev->profiles = zalloc(count * sizeof(struct hidpp10_profile));
		dev->profile_count = count;
	}
	count = min(count, nelems);
	memcpy(out, dev->profile_directory, count  * sizeof(out[0]));

	return count;
}

int
hidpp10_get_current_profile(struct hidpp10_device *dev, int8_t *current_profile)
{
	unsigned idx = dev->index;
	union hidpp10_message profile = CMD_PROFILE(idx, GET_REGISTER_REQ);
	int res;
	unsigned i;
	int8_t type, page, offset;
	struct hidpp10_directory directory[16]; /* completely random profile count */
	int count = 0;

	hidpp_log_raw(&dev->base, "Fetching the profiles' directory\n");

	count = hidpp10_get_profile_directory(dev, directory,
					    ARRAY_LENGTH(directory));
	if (count < 0)
		return count;

	hidpp_log_raw(&dev->base, "Fetching current profile\n");

	res = hidpp10_request_command(dev, &profile);
	if (res)
		return res;

	type = profile.msg.parameters[0];
	page = profile.msg.parameters[1];
	switch (type) {
	case PROFILE_TYPE_INDEX:
		*current_profile = page;
		/* If the profile exceeds the directory length, default to
		 * the first */
		if (*current_profile > count)
			*current_profile = 0;
		return 0;
	case PROFILE_TYPE_ADDRESS:
		offset = profile.msg.parameters[2];
		for (i = 0; i < ARRAY_LENGTH(directory) && directory[i].page < 32; i++) {
			if (page == directory[i].page &&
			    offset == directory[i].offset) {
				*current_profile = i;
				return 0;
			}
		}
		hidpp_log_error(&dev->base,
			  "unable to find the profile at (%d,%d) in the directory\n",
			  page, offset);
		break;
	default:
		hidpp_log_error(&dev->base,
			  "Unexpected value: %02x\n",
			  type);
	}

	return -ENAVAIL;
}

static int
hidpp10_set_internal_current_profile(struct hidpp10_device *dev,
				     int16_t current_profile,
				     uint8_t profile_type)
{
	unsigned idx = dev->index;
	union hidpp10_message profile = CMD_PROFILE(idx, SET_REGISTER_REQ);
	unsigned i;
	int8_t page, offset;
	struct hidpp10_directory directory[16]; /* completely random profile count */
	int count = 0;

	hidpp_log_raw(&dev->base, "Fetching the profiles' directory\n");

	count = hidpp10_get_profile_directory(dev, directory,
					    ARRAY_LENGTH(directory));
	if (count < 0)
		return count;

	hidpp_log_raw(&dev->base, "Setting current profile\n");

	profile.msg.parameters[0] = profile_type;

	switch (profile_type) {
	case PROFILE_TYPE_INDEX:
		if (current_profile > count)
			return -EINVAL;
		profile.msg.parameters[1] = current_profile & 0xFF;
		break;
	case PROFILE_TYPE_ADDRESS:
		page = current_profile >> 8;
		offset = current_profile & 0xFF;
		for (i = 0; i < ARRAY_LENGTH(directory) && directory[i].page < 32; i++) {
			if (page == directory[i].page &&
			    offset == directory[i].offset) {
				/* found the address in the directory */
				break;
			}
		}
		if (i >= ARRAY_LENGTH(directory) || directory[i].page >= 32) {
			hidpp_log_error(&dev->base,
					"unable to find the profile at (%d,%d) in the directory\n",
					page, offset);
			return -EINVAL;
		}
		profile.msg.parameters[1] = page;
		profile.msg.parameters[2] = offset;
		break;
	case PROFILE_TYPE_FACTORY:
		break;
	default:
		hidpp_log_error(&dev->base,
			  "Unexpected value: %02x\n",
			  profile_type);
		return -EINVAL;
	}

	return hidpp10_request_command(dev, &profile);
}

int
hidpp10_set_current_profile(struct hidpp10_device *dev, int16_t current_profile)
{
	return hidpp10_set_internal_current_profile(dev,
						    current_profile,
						    PROFILE_TYPE_INDEX);
}

static void
hidpp10_fill_dpi_modes_8(struct hidpp10_device *dev,
			 struct hidpp10_profile *profile,
			 struct _hidpp10_dpi_mode_8 *dpi_list,
			 unsigned int count)
{
	unsigned int i;

	profile->num_dpi_modes = count;
	for (i = 0; i < count; i++) {
		struct _hidpp10_dpi_mode_8 *dpi = &dpi_list[i];

		profile->dpi_modes[i].xres = hidpp10_get_dpi_value(dev, dpi->xres);
		profile->dpi_modes[i].yres = hidpp10_get_dpi_value(dev, dpi->yres);

		profile->dpi_modes[i].led[0] = dpi->led1 == 0x2;
		profile->dpi_modes[i].led[1] = dpi->led2 == 0x2;
		profile->dpi_modes[i].led[2] = dpi->led3 == 0x2;
		profile->dpi_modes[i].led[3] = dpi->led4 == 0x2;
	}
}

static void
hidpp10_write_dpi_modes_8(struct hidpp10_device *dev,
			  struct hidpp10_profile *profile,
			  struct _hidpp10_dpi_mode_8 *dpi_list,
			  unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct _hidpp10_dpi_mode_8 *dpi = &dpi_list[i];

		dpi->xres = hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].xres);
		dpi->yres = hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].yres);

		dpi->led1 = profile->dpi_modes[i].led[0] ? 0x02 : 0x01;
		dpi->led2 = profile->dpi_modes[i].led[1] ? 0x02 : 0x01;
		dpi->led3 = profile->dpi_modes[i].led[2] ? 0x02 : 0x01;
		dpi->led4 = profile->dpi_modes[i].led[3] ? 0x02 : 0x01;
	}
}

static void
hidpp10_fill_dpi_modes_16(struct hidpp10_device *dev,
			  struct hidpp10_profile *profile,
			  struct _hidpp10_dpi_mode_16 *dpi_list,
			  unsigned int count)
{
	unsigned int i;

	profile->num_dpi_modes = count;
	for (i = 0; i < count; i++) {
		uint8_t *be; /* in big endian */
		struct _hidpp10_dpi_mode_16 *dpi = &dpi_list[i];

		be = (uint8_t*)&dpi->xres;
		profile->dpi_modes[i].xres = hidpp10_get_dpi_value(dev, hidpp_get_unaligned_be_u16(be));
		be = (uint8_t*)&dpi->yres;
		profile->dpi_modes[i].yres = hidpp10_get_dpi_value(dev, hidpp_get_unaligned_be_u16(be));

		profile->dpi_modes[i].led[0] = dpi->led1 == 0x2;
		profile->dpi_modes[i].led[1] = dpi->led2 == 0x2;
		profile->dpi_modes[i].led[2] = dpi->led3 == 0x2;
		profile->dpi_modes[i].led[3] = dpi->led4 == 0x2;
	}
}

static void
hidpp10_write_dpi_modes_16(struct hidpp10_device *dev,
			   struct hidpp10_profile *profile,
			   struct _hidpp10_dpi_mode_16 *dpi_list,
			   unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		uint8_t *be; /* in big endian */
		struct _hidpp10_dpi_mode_16 *dpi = &dpi_list[i];

		be = (uint8_t*)&dpi->xres;
		hidpp_set_unaligned_be_u16(be, hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].xres));
		be = (uint8_t*)&dpi->yres;
		hidpp_set_unaligned_be_u16(be, hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].yres));

		dpi->led1 = profile->dpi_modes[i].led[0] ? 0x02 : 0x01;
		dpi->led2 = profile->dpi_modes[i].led[1] ? 0x02 : 0x01;
		dpi->led3 = profile->dpi_modes[i].led[2] ? 0x02 : 0x01;
		dpi->led4 = profile->dpi_modes[i].led[3] ? 0x02 : 0x01;
	}
}

static void
hidpp10_fill_buttons(struct hidpp10_device *dev,
		     struct hidpp10_profile *profile,
		     union _hidpp10_button_binding *buttons,
		     unsigned int count)
{
	unsigned int i;

	profile->num_buttons = count;
	for (i = 0; i < count; i++) {
		union _hidpp10_button_binding *b = &buttons[i];
		union hidpp10_button *button = &profile->buttons[i];

		button->any.type = b->any.type;

		switch (b->any.type) {
		case PROFILE_BUTTON_TYPE_BUTTON:
			button->button.button = ffs(hidpp_le_u16_to_cpu(b->button.button_flags));
			break;
		case PROFILE_BUTTON_TYPE_KEYS:
			button->keys.modifier_flags = b->keyboard_keys.modifier_flags;
			button->keys.key = b->keyboard_keys.key;
			break;
		case PROFILE_BUTTON_TYPE_SPECIAL:
			button->special.special = hidpp_le_u16_to_cpu(b->special.flags);
			break;
		case PROFILE_BUTTON_TYPE_CONSUMER_CONTROL:
			button->consumer_control.consumer_control =
				  hidpp_be_u16_to_cpu(b->consumer_control.consumer_control);
			break;
		case PROFILE_BUTTON_TYPE_DISABLED:
			break;
		}
	}
}

static void
hidpp10_write_buttons(struct hidpp10_device *dev,
		      struct hidpp10_profile *profile,
		      union _hidpp10_button_binding *buttons,
		      unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		union _hidpp10_button_binding *button = &buttons[i];
		union hidpp10_button *b = &profile->buttons[i];

		button->any.type = b->any.type;

		switch (b->any.type) {
		case PROFILE_BUTTON_TYPE_BUTTON:
			button->button.button_flags = hidpp_cpu_to_le_u16(1U << (b->button.button - 1));
			break;
		case PROFILE_BUTTON_TYPE_KEYS:
			button->keyboard_keys.modifier_flags = b->keys.modifier_flags;
			button->keyboard_keys.key = b->keys.key;
			break;
		case PROFILE_BUTTON_TYPE_SPECIAL:
			button->special.flags = hidpp_cpu_to_le_u16(b->special.special);
			break;
		case PROFILE_BUTTON_TYPE_CONSUMER_CONTROL:
			button->consumer_control.consumer_control =
					hidpp_cpu_to_be_u16(b->consumer_control.consumer_control);
			break;
		case PROFILE_BUTTON_TYPE_DISABLED:
			break;
		}
	}
}

static void
hidpp10_uchar16_to_uchar8(uint8_t *dst, uint16_t *src, size_t len)
{
	unsigned i;

	for (i = 0; i < len; i++)
		dst[i] = hidpp_le_u16_to_cpu(src[i]) & 0xFF;
}

static void
hidpp10_uchar8_to_uchar16(uint16_t *dst, uint8_t *src, size_t len)
{
	unsigned i;

	for (i = 0; i < len; i++)
		dst[i] = hidpp_cpu_to_le_u16(src[i]);
}

int
hidpp10_get_profile(struct hidpp10_device *dev, int8_t number, struct hidpp10_profile *profile_return)
{
	uint8_t page_data[HIDPP10_PAGE_SIZE];
	union _hidpp10_profile_data *data = (union _hidpp10_profile_data *)page_data;
	struct _hidpp10_profile_500 *p500 = &data->profile_500;
	struct _hidpp10_profile_700 *p700 = &data->profile_700;
	size_t i;
	int res;
	struct hidpp10_profile *profile;
	struct hidpp10_directory directory[16]; /* completely random profile count */
	union _hidpp10_button_binding *buttons;
	int count = 0;
	uint8_t page;

	/* Page 0 is RAM
	 * Page 1 is the profile directory
	 * Page 2-31 are Flash
	 * -> profiles are stored in the Flash
	 *
	 * For now we assume that number refers to the index in the profile
	 * directory.
	 */

	hidpp_log_raw(&dev->base, "Fetching profile %d\n", number);

	count = hidpp10_get_profile_directory(dev, directory,
					    ARRAY_LENGTH(directory));
	if (count < 0)
		return count;

	if (count == 0 || dev->profile_type == HIDPP10_PROFILE_UNKNOWN)
		return -ENOTSUP;

	if (number >= count) {
		hidpp_log_error(&dev->base, "Profile number %d not in the directory.\n", number);
		return -EINVAL;
	}

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
		buttons = p500->buttons;
		break;
	case HIDPP10_PROFILE_G700:
		buttons = p700->buttons;
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
	}

	profile = &dev->profiles[number];
	if (!profile->initialized) {

		page = directory[number].page;
		res = hidpp10_read_page(dev, page, page_data);
		if (res)
			return res;

		switch (dev->profile_type) {
		case HIDPP10_PROFILE_G500:
			profile->red = p500->red;
			profile->green = p500->green;
			profile->blue = p500->blue;
			profile->angle_correction = p500->angle_correction;
			profile->default_dpi_mode = p500->default_dpi_mode;
			profile->refresh_rate = p500->usb_refresh_rate ? 1000/p500->usb_refresh_rate : 0;

			hidpp10_fill_dpi_modes_16(dev, profile, p500->dpi_modes, PROFILE_NUM_DPI_MODES);
			hidpp10_fill_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
			break;
		case HIDPP10_PROFILE_G700:
			profile->default_dpi_mode = p700->default_dpi_mode;
			profile->refresh_rate = p700->usb_refresh_rate ? 1000/p700->usb_refresh_rate : 0;

			hidpp10_fill_dpi_modes_8(dev, profile, p700->dpi_modes, PROFILE_NUM_DPI_MODES);
			hidpp10_fill_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
			hidpp10_uchar16_to_uchar8(profile->name, p700->name, ARRAY_LENGTH(p700->name));
			hidpp_log_raw(&dev->base, "profile %d is named '%s'\n", number, profile->name);
			for (i = 0; i < ARRAY_LENGTH(p700->macro_names); i++) {
				hidpp10_uchar16_to_uchar8(profile->macro_names[i], p700->macro_names[i], ARRAY_LENGTH(p700->macro_names[i]));
				if (profile->macro_names[i][0])
					hidpp_log_raw(&dev->base, "macro %d of profile %d is named: '%s'\n", (unsigned)i, number, profile->macro_names[i]);
			}
			break;
		default:
			hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
		}
		profile->initialized = 1;

		hidpp_log_buf_raw(&dev->base,
				  "+++++++++++++++++++ Profile data: +++++++++++++++++ \n",
				  data->data, 78);
	}

	hidpp_log_raw(&dev->base, "Profile %d:\n", number);
	for (i = 0; i < 5; i++) {
		hidpp_log_raw(&dev->base,
			"DPI mode: %dx%d dpi\n",
			profile->dpi_modes[i].xres,
			profile->dpi_modes[i].yres);
		hidpp_log_raw(&dev->base,
			"LED status: 1:%s 2:%s 3:%s 4:%s\n",
			(profile->dpi_modes[i].led[0] & 0x2) ? "on" : "off",
			(profile->dpi_modes[i].led[1] & 0x2) ? "on" : "off",
			(profile->dpi_modes[i].led[2] & 0x2) ? "on" : "off",
			(profile->dpi_modes[i].led[3] & 0x2) ? "on" : "off");
	}
	hidpp_log_raw(&dev->base, "Angle correction: %d\n", profile->angle_correction);
	hidpp_log_raw(&dev->base, "Default DPI mode: %d\n", profile->default_dpi_mode);
	hidpp_log_raw(&dev->base, "Refresh rate: %d\n", profile->refresh_rate);
	for (i = 0; i < 13; i++) {
		union hidpp10_button *button = &profile->buttons[i];
		switch (button->any.type) {
		case PROFILE_BUTTON_TYPE_BUTTON:
			hidpp_log_raw(&dev->base,
				"Button %zd: button %d\n",
				i,
				button->button.button);
			break;
		case PROFILE_BUTTON_TYPE_KEYS:
			hidpp_log_raw(&dev->base,
				"Button %zd: key %d modifier %x\n",
				i,
				button->keys.key,
				button->keys.modifier_flags);
			break;
		case PROFILE_BUTTON_TYPE_SPECIAL:
			hidpp_log_raw(&dev->base,
				"Button %zd: special %x\n",
				i,
				button->special.special);
			break;
		case PROFILE_BUTTON_TYPE_CONSUMER_CONTROL:
			hidpp_log_raw(&dev->base,
				"Button %zd: consumer: %x\n",
				i,
				button->consumer_control.consumer_control);
			break;
		case PROFILE_BUTTON_TYPE_DISABLED:
			hidpp_log_raw(&dev->base, "Button %zd: disabled\n", i);
			break;
		default:
			/* FIXME: this is the page number for the macro,
			 * followed by a 1-byte offset */
			break ;
		}
	}

	*profile_return = *profile;

	return 0;
}

static const enum ratbag_button_action_special hidpp10_profiles_specials[] = {
	[0x00] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
	[0x01] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,
	[0x02] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,
	[0x03] = RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL,
	[0x04] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,

	[0x05 ... 0x07] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,

	[0x08] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,
	[0x09] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,

	[0x0a ... 0x0f] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,

	[0x10] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP,
	[0x11] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,

	[0x12 ... 0x1f] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,

	[0x20] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN,

	[0x21 ... 0xff] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
};

enum ratbag_button_action_special
hidpp10_onboard_profiles_get_special(uint8_t code)
{
	return hidpp10_profiles_specials[code];
}

uint8_t
hidpp10_onboard_profiles_get_code_from_special(enum ratbag_button_action_special special)
{
	uint8_t i = 0;

	while (++i) {
		if (hidpp10_profiles_specials[i] == special)
			return i;
	}

	return RATBAG_BUTTON_ACTION_SPECIAL_INVALID;
}

int
hidpp10_set_profile(struct hidpp10_device *dev, int8_t number, struct hidpp10_profile *profile)
{
	uint8_t page_data[HIDPP10_PAGE_SIZE];
	union _hidpp10_profile_data *data = (union _hidpp10_profile_data *)page_data;
	struct _hidpp10_profile_500 *p500 = &data->profile_500;
	struct _hidpp10_profile_700 *p700 = &data->profile_700;
	int res;
	struct hidpp10_directory directory[16]; /* completely random profile count */
	union _hidpp10_button_binding *buttons;
	int count = 0;
	unsigned i;
	uint16_t crc;

	hidpp_log_raw(&dev->base, "Fetching profile %d\n", number);

	count = hidpp10_get_profile_directory(dev, directory,
					    ARRAY_LENGTH(directory));
	if (count < 0)
		return count;

	if (count == 0 || dev->profile_type == HIDPP10_PROFILE_UNKNOWN)
		return -ENOTSUP;

	if (number >= count) {
		hidpp_log_error(&dev->base, "Profile number %d not in the directory.\n", number);
		return -EINVAL;
	}

	memset(page_data, 0xff, sizeof(page_data));

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
		buttons = p500->buttons;
		break;
	case HIDPP10_PROFILE_G700:
		buttons = p700->buttons;
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
	}


	/* First, fill out the unknown fields with the constants or the current
	 * values when we are not sure. */
	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
		/* we do not know the actual values of the remaining field right now
		 * so pre-fill with the current data */
		res = hidpp10_read_page(dev, directory[number].page, page_data);
		if (res)
			return res;
		break;
	case HIDPP10_PROFILE_G700:
		memcpy(p700->unknown1, _hidpp10_profile_700_unknown1, sizeof(p700->unknown1));
		memcpy(p700->unknown2, _hidpp10_profile_700_unknown2, sizeof(p700->unknown2));
		memcpy(p700->unknown3, _hidpp10_profile_700_unknown3, sizeof(p700->unknown3));
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
	}

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
		p500->red = profile->red;
		p500->green = profile->green;
		p500->blue = profile->blue;
		p500->angle_correction = profile->angle_correction;
		p500->default_dpi_mode = profile->default_dpi_mode;
		p500->usb_refresh_rate = 1000/profile->refresh_rate;

		hidpp10_write_dpi_modes_16(dev, profile, p500->dpi_modes, PROFILE_NUM_DPI_MODES);
		hidpp10_write_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
		break;
	case HIDPP10_PROFILE_G700:
		p700->default_dpi_mode = profile->default_dpi_mode;
		p700->usb_refresh_rate = 1000 / profile->refresh_rate;

		hidpp10_write_dpi_modes_8(dev, profile, p700->dpi_modes, PROFILE_NUM_DPI_MODES);
		hidpp10_write_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
		hidpp10_uchar8_to_uchar16(p700->name, profile->name, sizeof(p700->name));
		for (i = 0; i < ARRAY_LENGTH(p700->macro_names); i++) {
			hidpp10_uchar8_to_uchar16(p700->macro_names[i], profile->macro_names[i], ARRAY_LENGTH(p700->macro_names[i]));
		}
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
	}

	crc = hidpp_crc_ccitt(page_data, HIDPP10_PAGE_SIZE - 2);
	hidpp_set_unaligned_be_u16(&page_data[HIDPP10_PAGE_SIZE - 2], crc);

	for (i = 0; i < HIDPP10_PAGE_SIZE / 16; i++) {
		hidpp_log_buf_error(&dev->base, "new profile: ", page_data + i * 16, 16);
	}

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x51: LED Status                                                           */
/* -------------------------------------------------------------------------- */

#define __CMD_LED_STATUS			0x51

#define CMD_LED_STATUS(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_LED_STATUS, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_led_status(struct hidpp10_device *dev,
		       enum hidpp10_led_status led[6])
{
	unsigned idx = dev->index;
	union hidpp10_message led_status = CMD_LED_STATUS(idx, GET_REGISTER_REQ);
	uint8_t *status = led_status.msg.parameters;
	int res;

	hidpp_log_raw(&dev->base, "Fetching LED status\n");

	res = hidpp10_request_command(dev, &led_status);
	if (res)
		return res;

	led[0] = status[0] & 0xF;
	led[1] = (status[0] >> 4) & 0xF;
	led[2] = status[1] & 0xF;
	led[3] = (status[1] >> 4) & 0xF;
	led[4] = status[2] & 0xF;
	led[5] = (status[2] >> 4) & 0xF;

	return 0;
}

int
hidpp10_set_led_status(struct hidpp10_device *dev,
		       const enum hidpp10_led_status led[6])
{
	unsigned idx = dev->index;
	union hidpp10_message led_status = CMD_LED_STATUS(idx, SET_REGISTER_REQ);
	uint8_t *status = led_status.msg.parameters;
	int res;
	int i;

	hidpp_log_raw(&dev->base, "Setting LED status\n");

	for (i = 0; i < 6; i++) {
		switch (led[i]) {
			case HIDPP10_LED_STATUS_NO_CHANGE:
			case HIDPP10_LED_STATUS_OFF:
			case HIDPP10_LED_STATUS_ON:
			case HIDPP10_LED_STATUS_BLINK:
			case HIDPP10_LED_STATUS_HEARTBEAT:
			case HIDPP10_LED_STATUS_SLOW_ON:
			case HIDPP10_LED_STATUS_SLOW_OFF:
				break;
			default:
				abort();
		}
	}

	/* each led is 4-bits, 0x1 == off, 0x2 == on */
	status[0] = led[0] | (led[1] << 4);
	status[1] = led[2] | (led[3] << 4);
	status[2] = led[4] | (led[5] << 4);

	res = hidpp10_request_command(dev, &led_status);
	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x54: LED Intensity                                                        */
/* -------------------------------------------------------------------------- */

#define __CMD_LED_INTENSITY			0x54

#define CMD_LED_INTENSITY(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_LED_INTENSITY, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_led_intensity(struct hidpp10_device *dev,
			  uint8_t led_intensity_in_percent[6])
{
	unsigned idx = dev->index;
	union hidpp10_message led_intensity = CMD_LED_INTENSITY(idx, GET_REGISTER_REQ);
	uint8_t *intensity = led_intensity.msg.parameters;
	int res;

	hidpp_log_raw(&dev->base, "Fetching LED intensity\n");

	res = hidpp10_request_command(dev, &led_intensity);
	if (res)
		return res;

	led_intensity_in_percent[0] = 10 * ((intensity[0]     ) & 0xF);
	led_intensity_in_percent[1] = 10 * ((intensity[0] >> 4) & 0xF);
	led_intensity_in_percent[2] = 10 * ((intensity[1]     ) & 0xF);
	led_intensity_in_percent[3] = 10 * ((intensity[1] >> 4) & 0xF);
	led_intensity_in_percent[4] = 10 * ((intensity[2]     ) & 0xF);
	led_intensity_in_percent[5] = 10 * ((intensity[2] >> 4) & 0xF);

	return 0;
}

int
hidpp10_set_led_intensity(struct hidpp10_device *dev,
			  const uint8_t led_intensity_in_percent[6])
{
	unsigned idx = dev->index;
	union hidpp10_message led_intensity = CMD_LED_INTENSITY(idx, SET_REGISTER_REQ);
	uint8_t *intensity = led_intensity.msg.parameters;
	int res;

	hidpp_log_raw(&dev->base, "Setting LED intensity\n");

	intensity[0]  = led_intensity_in_percent[0]/10 & 0xF;
	intensity[0] |= (led_intensity_in_percent[1]/10 & 0xF) << 4;
	intensity[1]  = led_intensity_in_percent[2]/10 & 0xF;
	intensity[1] |= (led_intensity_in_percent[3]/10 & 0xF) << 4;
	intensity[2]  = led_intensity_in_percent[4]/10 & 0xF;
	intensity[2] |= (led_intensity_in_percent[5]/10 & 0xF) << 4;

	res = hidpp10_request_command(dev, &led_intensity);
	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x57: LED Color                                                            */
/* -------------------------------------------------------------------------- */

#define __CMD_LED_COLOR				0x57

#define CMD_LED_COLOR(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_LED_COLOR, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_led_color(struct hidpp10_device *dev,
		      uint8_t *red,
		      uint8_t *green,
		      uint8_t *blue)
{
	unsigned idx = dev->index;
	union hidpp10_message led_color = CMD_LED_COLOR(idx, GET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching LED color\n");

	res = hidpp10_request_command(dev, &led_color);
	if (res)
		return res;

	*red = led_color.msg.parameters[0];
	*green = led_color.msg.parameters[1];
	*blue = led_color.msg.parameters[2];

	return 0;
}

int
hidpp10_set_led_color(struct hidpp10_device *dev,
		      uint8_t red,
		      uint8_t green,
		      uint8_t blue)
{
	unsigned idx = dev->index;
	union hidpp10_message led_color = CMD_LED_COLOR(idx, SET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Setting LED color\n");

	led_color.msg.parameters[0] = red;
	led_color.msg.parameters[1] = green;
	led_color.msg.parameters[2] = blue;

	res = hidpp10_request_command(dev, &led_color);
	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x61: Optical Sensor Settings                                              */
/* -------------------------------------------------------------------------- */
#define __CMD_OPTICAL_SENSOR_SETTINGS		0x61

#define CMD_OPTICAL_SENSOR_SETTINGS(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_OPTICAL_SENSOR_SETTINGS, \
		.parameters = {0x00, 0x00, 0x00}, \
	} \
}

int
hidpp10_get_optical_sensor_settings(struct hidpp10_device *dev,
				    uint8_t *surface_reflectivity)
{
	unsigned idx = dev->index;
	union hidpp10_message sensor = CMD_OPTICAL_SENSOR_SETTINGS(idx, GET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching optical sensor settings\n");

	res = hidpp10_request_command(dev, &sensor);
	if (res)
		return res;

	*surface_reflectivity = sensor.msg.parameters[0];

	/* Don't know what the other values are */

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0x63: Current Resolution                                                   */
/* -------------------------------------------------------------------------- */
#define __CMD_CURRENT_RESOLUTION		0x63

#define CMD_CURRENT_RESOLUTION(id, idx, sub) { \
	.msg = { \
		.report_id = id, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_CURRENT_RESOLUTION, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_current_resolution(struct hidpp10_device *dev,
			       uint16_t *xres,
			       uint16_t *yres)
{
	unsigned idx = dev->index;
	union hidpp10_message resolution = CMD_CURRENT_RESOLUTION(REPORT_ID_SHORT, idx, GET_LONG_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching current resolution\n");

	res = hidpp10_request_command(dev, &resolution);
	if (res)
		return res;

	/* resolution is in 50dpi multiples */
	*xres = hidpp10_get_dpi_value(dev, hidpp_get_unaligned_le_u16(&resolution.data[4]));
	*yres = hidpp10_get_dpi_value(dev, hidpp_get_unaligned_le_u16(&resolution.data[6]));

	return 0;
}

int
hidpp10_set_current_resolution(struct hidpp10_device *dev,
			       uint16_t xres,
			       uint16_t yres)
{
	unsigned idx = dev->index;
	union hidpp10_message resolution = CMD_CURRENT_RESOLUTION(REPORT_ID_LONG, idx, SET_LONG_REGISTER_REQ);

	hidpp_set_unaligned_le_u16(&resolution.data[4], hidpp10_get_dpi_mapping(dev, xres));
	hidpp_set_unaligned_le_u16(&resolution.data[6], hidpp10_get_dpi_mapping(dev, yres));

	return hidpp10_request_command(dev, &resolution);
}

/* -------------------------------------------------------------------------- */
/* 0x64: USB Refresh Rate                                                     */
/* -------------------------------------------------------------------------- */
#define __CMD_USB_REFRESH_RATE			0x64

#define CMD_USB_REFRESH_RATE(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_USB_REFRESH_RATE, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_usb_refresh_rate(struct hidpp10_device *dev,
			     uint16_t *rate)
{
	unsigned idx = dev->index;
	union hidpp10_message refresh = CMD_USB_REFRESH_RATE(idx, GET_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching USB refresh rate\n");

	res = hidpp10_request_command(dev, &refresh);
	if (res)
		return res;

	*rate = 1000/refresh.msg.parameters[0];

	return 0;
}

int
hidpp10_set_usb_refresh_rate(struct hidpp10_device *dev,
			     uint16_t rate)
{
	unsigned idx = dev->index;
	union hidpp10_message refresh = CMD_USB_REFRESH_RATE(idx, GET_REGISTER_REQ);

	hidpp_log_raw(&dev->base, "Setting USB refresh rate\n");

	refresh.msg.parameters[0] = 1000/rate;

	return hidpp10_request_command(dev, &refresh);
}

/* -------------------------------------------------------------------------- */
/* 0xA2: Read Sector                                                          */
/* -------------------------------------------------------------------------- */
#define __CMD_READ_MEMORY			0xA2

#define CMD_READ_MEMORY(idx, page, offset) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = GET_LONG_REGISTER_REQ, \
		.address = __CMD_READ_MEMORY, \
		.parameters = {page, offset, 0x00 }, \
	} \
}

int
hidpp10_read_memory(struct hidpp10_device *dev, uint8_t page, uint16_t offset,
		    uint8_t bytes[16])
{
	unsigned idx = dev->index;
	union hidpp10_message readmem = CMD_READ_MEMORY(idx, page, offset / 2);
	int res;

	if (offset % 2 != 0) {
		hidpp_log_error(&dev->base, "Reading memory with odd offset is not supported.\n");
		return -EINVAL;
	}

	if (page > HIDPP10_MAX_PAGE_NUMBER)
		return -EINVAL;

	hidpp_log_raw(&dev->base, "Reading memory page %d, offset %#x\n", page, offset);

	res = hidpp10_request_command(dev, &readmem);
	if (res)
		return res;

	memcpy(bytes, readmem.msg.string, sizeof(readmem.msg.string));

	return 0;
}

int
hidpp10_read_page(struct hidpp10_device *dev, uint8_t page,
		  uint8_t bytes[HIDPP10_PAGE_SIZE])
{
	unsigned int i;
	int res;
	uint16_t crc, read_crc;

	for (i = 0; i < HIDPP10_PAGE_SIZE; i += 16) {
		res = hidpp10_read_memory(dev, page, i, bytes + i);
		if (res < 0)
			return res;
	}

	crc = hidpp_crc_ccitt(bytes, HIDPP10_PAGE_SIZE - 2);
	read_crc = hidpp_get_unaligned_be_u16(&bytes[HIDPP10_PAGE_SIZE - 2]);

	if (crc != read_crc)
		return -EILSEQ; /* return illegal sequence */

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0xB2: Device Connection and Disconnection (Pairing)                        */
/* -------------------------------------------------------------------------- */

#define __CMD_DEVICE_CONNECTION_DISCONNECTION	0xB2
#define CONNECT_DEVICES_OPEN_LOCK			1
#define CONNECT_DEVICES_CLOSE_LOCK			2
#define CONNECT_DEVICES_DISCONNECT			3

#define CMD_DEVICE_CONNECTION_DISCONNECTION(idx, cmd, timeout)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = HIDPP_RECEIVER_IDX, \
		.sub_id = SET_REGISTER_REQ, \
		.address = __CMD_DEVICE_CONNECTION_DISCONNECTION, \
		.parameters = {cmd, idx - 1, timeout }, \
	} \
}

int
hidpp10_open_lock(struct hidpp10_device *device, uint8_t timeout)
{
	union hidpp10_message open_lock = CMD_DEVICE_CONNECTION_DISCONNECTION(0x00,
									      CONNECT_DEVICES_OPEN_LOCK,
									      timeout);

	return hidpp10_request_command(device, &open_lock);
}


int
hidpp10_close_lock(struct hidpp10_device *device)
{
	union hidpp10_message open_lock = CMD_DEVICE_CONNECTION_DISCONNECTION(0x00,
									      CONNECT_DEVICES_CLOSE_LOCK,
									      0);

	return hidpp10_request_command(device, &open_lock);
}

int hidpp10_disconnect(struct hidpp10_device *device, int idx) {
	union hidpp10_message disconnect = CMD_DEVICE_CONNECTION_DISCONNECTION(idx + 1, CONNECT_DEVICES_DISCONNECT, 0x00);

	return hidpp10_request_command(device, &disconnect);
}

/* -------------------------------------------------------------------------- */
/* 0xB5: Pairing Information                                                  */
/* -------------------------------------------------------------------------- */

#define __CMD_PAIRING_INFORMATION		0xB5
#define DEVICE_PAIRING_INFORMATION			0x20
#define DEVICE_EXTENDED_PAIRING_INFORMATION		0x30
#define DEVICE_NAME					0x40

#define CMD_PAIRING_INFORMATION(idx, type)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = HIDPP_RECEIVER_IDX, \
		.sub_id = GET_LONG_REGISTER_REQ, \
		.address = __CMD_PAIRING_INFORMATION, \
		.parameters = {type + idx - 1, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_pairing_information(struct hidpp10_device *dev,
				uint8_t *report_interval,
				uint16_t *wpid,
				uint8_t *device_type)
{
	unsigned int idx = dev->index;
	union hidpp10_message pairing_information = CMD_PAIRING_INFORMATION(idx, DEVICE_PAIRING_INFORMATION);
	int res;

	hidpp_log_raw(&dev->base, "Fetching pairing information\n");

	res = hidpp10_request_command(dev, &pairing_information);
	if (res)
		return -1;

	*report_interval = pairing_information.msg.string[2];
	*wpid = hidpp_get_unaligned_be_u16(&pairing_information.msg.string[3]);
	*device_type = pairing_information.msg.string[7];

	return 0;
}

int
hidpp10_get_pairing_information_device_name(struct hidpp10_device *dev,
					    char *name,
					    size_t *name_size)
{
	unsigned int idx = dev->index;
	union hidpp10_message device_name = CMD_PAIRING_INFORMATION(idx, DEVICE_NAME);
	int res;

	hidpp_log_raw(&dev->base, "Fetching device name\n");

	res = hidpp10_request_command(dev, &device_name);
	if (res)
		return -1;
	*name_size = min(*name_size, device_name.msg.string[1] + 1U);
	strncpy_safe(name, (char*)&device_name.msg.string[2], *name_size);

	return 0;
}

int
hidpp10_get_extended_pairing_information(struct hidpp10_device *dev,
					 uint32_t *serial)
{
	unsigned int idx = dev->index;
	union hidpp10_message info = CMD_PAIRING_INFORMATION(idx, DEVICE_EXTENDED_PAIRING_INFORMATION);
	int res;

	hidpp_log_raw(&dev->base, "Fetching extended pairing information\n");

	res = hidpp10_request_command(dev, &info);
	if (res)
		return -1;

	*serial = hidpp_get_unaligned_be_u16(&info.msg.string[2]) << 16;
	*serial |= hidpp_get_unaligned_be_u16(&info.msg.string[4]);

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0xF1: Device Firmware Information                                          */
/* -------------------------------------------------------------------------- */

#define __CMD_DEVICE_FIRMWARE_INFORMATION	0xF1
#define FIRMWARE_INFO_ITEM_FW_NAME_AND_VERSION(MCU)	((MCU - 1) << 4 | 0x01)
#define FIRMWARE_INFO_ITEM_FW_BUILD_NUMBER(MCU)		((MCU - 1) << 4 | 0x02)
#define FIRMWARE_INFO_ITEM_HW_VERSION(MCU)		((MCU - 1) << 4 | 0x03)
#define FIRMWARE_INFO_ITEM_BOOTLOADER_VERSION(MCU)	((MCU - 1) << 4 | 0x04)

#define CMD_DEVICE_FIRMWARE_INFORMATION(idx, fw_info_item)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = GET_REGISTER_REQ, \
		.address = __CMD_DEVICE_FIRMWARE_INFORMATION, \
		.parameters = {fw_info_item, 0x00, 0x00 }, \
	} \
}

int
hidpp10_get_firmare_information(struct hidpp10_device *dev,
				uint8_t *major_out,
				uint8_t *minor_out,
				uint8_t *build_out)
{
	unsigned idx = dev->index;
	union hidpp10_message firmware_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_NAME_AND_VERSION(1));
	union hidpp10_message build_information = CMD_DEVICE_FIRMWARE_INFORMATION(idx, FIRMWARE_INFO_ITEM_FW_BUILD_NUMBER(1));
	int res;
	uint8_t maj, min, build;

	hidpp_log_raw(&dev->base, "Fetching firmware information\n");

	/*
	 * This may fail on some devices
	 * => we can not retrieve their FW version through HID++ 1.0.
	 */
	res = hidpp10_request_command(dev, &firmware_information);
	if (res)
		return res;
	maj = firmware_information.msg.string[1];
	min = firmware_information.msg.string[2];

	res = hidpp10_request_command(dev, &build_information);
	if (res)
		return res;
	build = hidpp_get_unaligned_be_u16(&build_information.msg.string[1]);

	*major_out = maj;
	*minor_out = min;
	*build_out = build;

	return 0;
}

/* -------------------------------------------------------------------------- */
/* general device handling                                                    */
/* -------------------------------------------------------------------------- */
static int
hidpp10_get_device_info(struct hidpp10_device *dev)
{
	uint32_t feature_mask, notifications;
	uint8_t reflect;
	int i;
	uint16_t xres, yres;
	uint16_t refresh_rate;
	enum hidpp10_led_status led[6];
	int8_t current_profile;
	struct hidpp10_profile profiles[HIDPP10_NUM_PROFILES];
	int count;
	struct hidpp10_directory directory[16];

	hidpp10_get_individual_features(dev, &feature_mask);
	hidpp10_get_hidpp_notifications(dev, &notifications);

	hidpp10_get_current_resolution(dev, &xres, &yres);
	hidpp10_get_led_status(dev, led);
	hidpp10_get_usb_refresh_rate(dev, &refresh_rate);

	hidpp10_get_optical_sensor_settings(dev, &reflect);

	hidpp10_get_current_profile(dev, &current_profile);

	count = hidpp10_get_profile_directory(dev, directory,
					      ARRAY_LENGTH(directory));
	for (i = 0; i < count && i < HIDPP10_NUM_PROFILES; i++)
		hidpp10_get_profile(dev, i, &profiles[i]);

	return 0;
}

struct hidpp10_device*
hidpp10_device_new(const struct hidpp_device *base, int idx,
		   enum hidpp10_profile_type type)
{
	struct hidpp10_device *dev;

	dev = zalloc(sizeof(*dev));

	dev->index = idx;
	dev->base = *base;
	dev->profile_type = type;

	if (hidpp10_get_device_info(dev) != 0) {
		hidpp10_device_destroy(dev);
		dev = NULL;
	}

	return dev;
}

void
hidpp10_device_destroy(struct hidpp10_device *dev)
{
	if (dev->dpi_table)
		free(dev->dpi_table);
	if (dev->profile_directory)
		free(dev->profile_directory);
	if (dev->profiles)
		free(dev->profiles);
	free(dev);
}
