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

#include <assert.h>
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

unsigned int
hidpp10_dpi_table_get_max_dpi(struct hidpp10_device *dev)
{
	struct hidpp10_dpi_mapping *dpi;

	assert(dev->dpi_count > 0);

	/* We assume a sorted list */
	dpi = &dev->dpi_table[dev->dpi_count - 1];

	return dpi->dpi;
}

unsigned int
hidpp10_dpi_table_get_min_dpi(struct hidpp10_device *dev)
{
	struct hidpp10_dpi_mapping *dpi;

	assert(dev->dpi_count > 0);

	/* We assume a sorted list, index 0 is always 0 */
	dpi = &dev->dpi_table[1];

	return dpi->dpi;
}

int
hidpp10_build_dpi_table_from_list(struct hidpp10_device *dev,
				  const struct dpi_list *list)
{
	size_t i;

	if (list->nentries + 0x80 - 1> 0xff)
		goto err;

	dev->dpi_count = list->nentries;
	dev->dpi_table = zalloc(list->nentries * sizeof(*dev->dpi_table));
	dev->dpi_table_is_range = false;

	for (i = 0; i < list->nentries; i++) {
		dev->dpi_table[i].raw_value = i + 0x80;
		dev->dpi_table[i].dpi = list->entries[i];
	}

	return 0;
err:
	dev->dpi_count = 0;
	free(dev->dpi_table);
	dev->dpi_table = NULL;
	return -EINVAL;
}

int
hidpp10_build_dpi_table_from_dpi_info(struct hidpp10_device *dev,
				      const struct dpi_range *range)
{
	unsigned raw_max, i;


	raw_max = (range->max - range->min) / range->step;
	if (raw_max > 0xff)
		return -EINVAL;

	dev->dpi_count = raw_max + 1;
	dev->dpi_table = zalloc((raw_max + 1) * sizeof(*dev->dpi_table));
	dev->dpi_table_is_range = true;

	for (i = 1; i <= raw_max; i++) {
		dev->dpi_table[i].raw_value = i;
		dev->dpi_table[i].dpi = round((range->min + range->step * i) / 25.0f) * 25;
	}

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
	_cleanup_free_ char *rxdata = NULL, *txdata = NULL;

	switch (msg->msg.report_id) {
	case REPORT_ID_SHORT:
		command_size = SHORT_MESSAGE_LENGTH;
		break;
	case REPORT_ID_LONG:
		command_size = LONG_MESSAGE_LENGTH;
		break;
	default:
		hidpp_log_error(&dev->base, "Incorrect message report id: %02x\n", msg->msg.report_id);
		return -EINVAL;
	}

	/* create the expected header */
	expected_header = *msg;

	txdata = hidpp_buffer_to_string(&msg->data[4], command_size - 4);
	hidpp_log_raw(&dev->base, "hidpp10 tx:  %02x | %02x | %02x | %02x | %s\n",
		      msg->msg.report_id,
		      msg->msg.device_idx,
		      msg->msg.sub_id,
		      msg->msg.address,
		      txdata);

	/* response message length doesn't depend on request length */
#if 0
	hidpp_log_buf_raw(&dev->base, "  expected_header:		?? ", &expected_header.data[1], 3);
	hidpp_log_buf_raw(&dev->base, "  expected_error_dev:	", expected_error_dev.data, SHORT_MESSAGE_LENGTH);
#endif

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
				hidpp10_errors[hidpp_err] ? hidpp10_errors[hidpp_err] : "Undocumented error code",
				hidpp_err);
			break;
		}
	} while (ret > 0);

	if (ret < 0) {
		hidpp_log_error(&dev->base, "    USB error: %s (%d)\n", strerror(-ret), -ret);
		perror("write");
		goto out_err;
	}

	rxdata = hidpp_buffer_to_string(&read_buffer.data[4], ret - 4);
	hidpp_log_raw(&dev->base, "hidpp10 rx:  %02x | %02x | %02x | %02x | %s\n",
		      read_buffer.msg.report_id,
		      read_buffer.msg.device_idx,
		      read_buffer.msg.sub_id,
		      read_buffer.msg.address,
		      rxdata);

	if (!hidpp_err) {
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

	hidpp_log_raw(&dev->base, "Fetching HID++ notifications (%#02x)\n",
		      __CMD_HIDPP_NOTIFICATIONS);

	res = hidpp10_request_command(dev, &notifications);
	if (res)
		return res;

	*reporting_flags = notifications.msg.parameters[0];
	*reporting_flags |= (notifications.msg.parameters[1] & 0x1F) << 8;
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

	hidpp_log_raw(&dev->base, "Setting HID++ notifications (%#02x\n",
		      __CMD_HIDPP_NOTIFICATIONS);

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

	hidpp_log_raw(&dev->base, "Fetching individual features (%#02x)\n",
		      __CMD_ENABLE_INDIVIDUAL_FEATURES);

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

	hidpp_log_raw(&dev->base, "Setting individual features (%#02x)\n",
		      __CMD_ENABLE_INDIVIDUAL_FEATURES);

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
	uint8_t res;
	uint8_t led1:4;
	uint8_t led2:4;
	uint8_t led3:4;
	uint8_t led4:4;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_dpi_mode_8) == 3, "Invalid size");

struct _hidpp10_dpi_mode_8_dual {
	uint8_t xres;
	uint8_t yres;
	uint8_t led1:4;
	uint8_t led2:4;
	uint8_t led3:4;
	uint8_t led4:4;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_dpi_mode_8_dual) == 4, "Invalid size");

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
	struct {
		uint8_t page;
		uint8_t offset;
		uint8_t zero;
	} __attribute__((packed)) macro;
} __attribute__((packed));
_Static_assert(sizeof(union _hidpp10_button_binding) == 3, "Invalid size");

union _hidpp10_profile_metadata {
	struct {
		uint8_t marker[5];
		uint8_t padding[420];
	} __attribute__((packed)) any;
	struct {
		uint8_t marker[5]; /* { 'L', 'G', 'S', '0', '2' } */
		uint16_t name[23];
		uint16_t macro_names[11][17];
	} __attribute__((packed)) lgs02;
} __attribute__((packed));

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
	union _hidpp10_profile_metadata metadata;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_profile_500) == 503, "Invalid size");

static const uint8_t _hidpp10_profile_700_unknown1[3] = { 0x80, 0x01, 0x10 };
static const uint8_t _hidpp10_profile_700_unknown2[10] = { 0x01, 0x2c, 0x02, 0x58, 0x64, 0xff, 0xbc, 0x00, 0x09, 0x31 };

struct _hidpp10_profile_700 {
	struct _hidpp10_dpi_mode_8_dual dpi_modes[PROFILE_NUM_DPI_MODES];
	uint8_t default_dpi_mode;
	uint8_t unknown1[3];
	uint8_t usb_refresh_rate;
	uint8_t unknown2[10];
	union _hidpp10_button_binding buttons[PROFILE_NUM_BUTTONS];
	union _hidpp10_profile_metadata metadata;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_profile_700) == 499, "Invalid size");

struct _hidpp10_profile_9 {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t unknown1;
	struct _hidpp10_dpi_mode_8 dpi_modes[PROFILE_NUM_DPI_MODES];
	uint8_t default_dpi_mode;
	uint8_t unknown2[2];
	uint8_t usb_refresh_rate;
	union _hidpp10_button_binding buttons[PROFILE_NUM_BUTTONS_G9];
	uint8_t unknown3[3];
	union _hidpp10_profile_metadata metadata;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_profile_500) == 503, "Invalid size");

union _hidpp10_profile_data {
	struct _hidpp10_profile_500 profile_500;
	struct _hidpp10_profile_700 profile_700;
	struct _hidpp10_profile_9 profile_9;
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
		delta = abs((int)value - (int)m[i].dpi);
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

static int
hidpp10_write_profile_directory(struct hidpp10_device *dev)
{
	unsigned int i, index;
	int res;
	uint8_t bytes[HIDPP10_PAGE_SIZE];
	struct hidpp10_directory *directory = (struct hidpp10_directory *)bytes;
	uint16_t crc;

	if (dev->profile_type == HIDPP10_PROFILE_UNKNOWN) {
		hidpp_log_debug(&dev->base, "no profile type given\n");
		return 0;
	}

	memset(bytes, 0xff, sizeof(bytes));

	index = 0;
	for (i = 0; i < dev->profile_count; i++) {
		if (!dev->profiles[i].enabled)
			continue;

		directory[index].page = dev->profiles[i].page;
		directory[index].offset = dev->profiles[i].offset;
		directory[index].led_mask = ((0b111 << index) >> 2) & 0b111;
		++index;
	}

	crc = hidpp_crc_ccitt(bytes, HIDPP10_PAGE_SIZE - 2);
	set_unaligned_be_u16(&bytes[HIDPP10_PAGE_SIZE - 2], crc);

	res = hidpp10_send_hot_payload(dev,
				       0x00, 0x0000, /* destination: RAM */
				       bytes,
				       HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	res = hidpp10_erase_memory(dev, 0x01);
	if (res < 0)
		return res;

	res = hidpp10_write_flash(dev,
				  0x00, 0x0000,
				  0x01, 0x0000,
				  HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	res = hidpp10_send_hot_payload(dev,
				       0x00, 0x0000, /* destination: RAM */
				       bytes + HIDPP10_PAGE_SIZE / 2,
				       HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	res = hidpp10_write_flash(dev,
				  0x00, 0x0000,
				  0x01, HIDPP10_PAGE_SIZE / 2,
				  HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	return 0;
}

static int
hidpp10_read_profile_directory(struct hidpp10_device *dev)
{
	unsigned int i;
	int res;
	uint8_t bytes[HIDPP10_PAGE_SIZE] = { 0 };
	struct hidpp10_directory *directory = (struct hidpp10_directory *)bytes;
	unsigned int count;

	if (dev->profile_type == HIDPP10_PROFILE_UNKNOWN) {
		hidpp_log_debug(&dev->base, "no profile type given\n");
		return 0;
	}

	hidpp_log_raw(&dev->base, "Fetching the profiles' directory\n");

	res = hidpp10_read_page(dev, 0x01, bytes);
	if (res)
		return res;

	count = 0;
	for (i = 0; i < dev->profile_count; i++) {
		if (directory[i].page == 0xFF)
			break;
		dev->profiles[i].page = directory[i].page;
		dev->profiles[i].offset = directory[i].offset;
		dev->profiles[i].enabled = true;
		count++;
	}

	for (i = count; i < dev->profile_count; i++)
		dev->profiles[i].enabled = false;

	return count;
}

int
hidpp10_get_current_profile(struct hidpp10_device *dev, uint8_t *current_profile)
{
	unsigned idx = dev->index;
	union hidpp10_message profile = CMD_PROFILE(idx, GET_REGISTER_REQ);
	int res;
	unsigned i;
	uint8_t type, page, offset;

	hidpp_log_raw(&dev->base, "Fetching current profile (%#02x)\n",
		      __CMD_PROFILE);

	res = hidpp10_request_command(dev, &profile);
	if (res) {
		/* Profiles not supported */
		hidpp_log_debug(&dev->base, "Profiles not supported\n");
		*current_profile = 0;
		return 0;
	}

	type = profile.msg.parameters[0];
	page = profile.msg.parameters[1];
	switch (type) {
	case PROFILE_TYPE_INDEX:
		*current_profile = page;
		/* If the profile exceeds the directory length, default to
		 * the first */
		if (*current_profile > dev->profile_count)
			*current_profile = 0;
		return 0;
	case PROFILE_TYPE_ADDRESS:
		offset = profile.msg.parameters[2];
		for (i = 0; i < dev->profile_count; i++) {
			if (page == dev->profiles[i].page &&
			    offset == dev->profiles[i].offset) {
				*current_profile = i;
				return 0;
			}
		}
		hidpp_log_error(&dev->base,
			  "unable to find the profile at (%d,%d) in the directory\n",
			  page, offset);
		break;
	case PROFILE_TYPE_FACTORY:
		/* Factory profile is selected and profile switching is
		 * disabled. Let's switch to the first profile because the
		 * factory profile doesn't help anywone */
		res = hidpp10_set_current_profile(dev, 0);
		if (res == 0) {
			hidpp_log_info(&dev->base, "switched from factory profile to 0\n");
			*current_profile = 0;
			return 0;
		}

		hidpp_log_error(&dev->base,
				"current profile is factory profile but switching to 0 failed.\n");
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
				     uint16_t current_profile,
				     uint8_t profile_type)
{
	unsigned idx = dev->index;
	union hidpp10_message profile = CMD_PROFILE(idx, SET_REGISTER_REQ);
	int8_t page, offset;

	hidpp_log_raw(&dev->base, "Setting current profile (%#02x)\n",
		      __CMD_PROFILE);

	profile.msg.parameters[0] = profile_type;

	switch (profile_type) {
	case PROFILE_TYPE_INDEX:
		if (current_profile > dev->profile_count)
			return -EINVAL;
		profile.msg.parameters[1] = current_profile & 0xFF;
		break;
	case PROFILE_TYPE_ADDRESS:
		page = current_profile >> 8;
		offset = current_profile & 0xFF;
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
hidpp10_set_current_profile(struct hidpp10_device *dev, uint16_t current_profile)
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

		profile->dpi_modes[i].xres = hidpp10_get_dpi_value(dev, dpi->res);
		profile->dpi_modes[i].yres = hidpp10_get_dpi_value(dev, dpi->res);

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

		dpi->res = hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].xres);

		dpi->led1 = profile->dpi_modes[i].led[0] ? 0x02 : 0x01;
		dpi->led2 = profile->dpi_modes[i].led[1] ? 0x02 : 0x01;
		dpi->led3 = profile->dpi_modes[i].led[2] ? 0x02 : 0x01;
		dpi->led4 = profile->dpi_modes[i].led[3] ? 0x02 : 0x01;
	}
}

static void
hidpp10_fill_dpi_modes_8_dual(struct hidpp10_device *dev,
			      struct hidpp10_profile *profile,
			      struct _hidpp10_dpi_mode_8_dual *dpi_list,
			      unsigned int count)
{
	unsigned int i;

	profile->num_dpi_modes = count;
	for (i = 0; i < count; i++) {
		struct _hidpp10_dpi_mode_8_dual *dpi = &dpi_list[i];

		profile->dpi_modes[i].xres = hidpp10_get_dpi_value(dev, dpi->xres);
		profile->dpi_modes[i].yres = hidpp10_get_dpi_value(dev, dpi->yres);

		profile->dpi_modes[i].led[0] = dpi->led1 == 0x2;
		profile->dpi_modes[i].led[1] = dpi->led2 == 0x2;
		profile->dpi_modes[i].led[2] = dpi->led3 == 0x2;
		profile->dpi_modes[i].led[3] = dpi->led4 == 0x2;
	}
}

static void
hidpp10_write_dpi_modes_8_dual(struct hidpp10_device *dev,
			       struct hidpp10_profile *profile,
			       struct _hidpp10_dpi_mode_8_dual *dpi_list,
			       unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		struct _hidpp10_dpi_mode_8_dual *dpi = &dpi_list[i];

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
		profile->dpi_modes[i].xres = hidpp10_get_dpi_value(dev, get_unaligned_be_u16(be));
		be = (uint8_t*)&dpi->yres;
		profile->dpi_modes[i].yres = hidpp10_get_dpi_value(dev, get_unaligned_be_u16(be));

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
		set_unaligned_be_u16(be, hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].xres));
		be = (uint8_t*)&dpi->yres;
		set_unaligned_be_u16(be, hidpp10_get_dpi_mapping(dev, profile->dpi_modes[i].yres));

		dpi->led1 = profile->dpi_modes[i].led[0] ? 0x02 : 0x01;
		dpi->led2 = profile->dpi_modes[i].led[1] ? 0x02 : 0x01;
		dpi->led3 = profile->dpi_modes[i].led[2] ? 0x02 : 0x01;
		dpi->led4 = profile->dpi_modes[i].led[3] ? 0x02 : 0x01;
	}
}

static int
hidpp10_onboard_profiles_macro_next(struct hidpp10_device *device,
				    uint8_t memory[32],
				    uint16_t *index,
				    union hidpp10_macro_data *macro)
{
	int rc = 0;
	unsigned int step = 0;

	if (*index >= 32 - sizeof(union hidpp10_macro_data)) {
		hidpp_log_error(&device->base, "error while parsing macro.\n");
		return -EFAULT;
	}

	memcpy(macro, &memory[*index], sizeof(union hidpp10_macro_data));

	switch (macro->any.type) {
	case HIDPP10_MACRO_NOOP:
		/* fallthrough */
	case HIDPP10_MACRO_WAIT_FOR_BUTTON_RELEASE:
		/* fallthrough */
	case HIDPP10_MACRO_REPEAT_UNTIL_BUTTON_RELEASE:
		/* fallthrough */
	case HIDPP10_MACRO_REPEAT:
		step = 1;
		rc = -EAGAIN;
		break;
	case HIDPP10_MACRO_KEY_PRESS:
		/* fallthrough */
	case HIDPP10_MACRO_KEY_RELEASE:
		/* fallthrough */
	case HIDPP10_MACRO_MOD_PRESS:
		/* fallthrough */
	case HIDPP10_MACRO_MOD_RELEASE:
		/* fallthrough */
	case HIDPP10_MACRO_MOUSE_WHEEL:
		step = 2;
		rc = -EAGAIN;
		break;
	case HIDPP10_MACRO_MOUSE_BUTTON_PRESS:
		/* fallthrough */
	case HIDPP10_MACRO_MOUSE_BUTTON_RELEASE:
		/* fallthrough */
	case HIDPP10_MACRO_KEY_CONSUMER_CONTROL:
		/* fallthrough */
	case HIDPP10_MACRO_DELAY:
		/* fallthrough */
	case HIDPP10_MACRO_JUMP:
		/* fallthrough */
	case HIDPP10_MACRO_JUMP_IF_PRESSED:
		step = 3;
		rc = -EAGAIN;
		break;
	case HIDPP10_MACRO_MOUSE_POINTER_MOVE:
		/* fallthrough */
	case HIDPP10_MACRO_JUMP_IF_RELEASED_TIMEOUT:
		step = 5;
		rc = -EAGAIN;
		break;

	case HIDPP10_MACRO_END:
		return 0;
	default:
		if (macro->any.type >= 0x80 && macro->any.type <= 0xFE) {
			step = 1;
			rc = -EAGAIN;
		} else {
			hidpp_log_error(&device->base, "unknown tag: 0x%02x\n", macro->any.type);
			return -EFAULT;
		}
	}

	if ((*index + step) & 0xF0)
		/* the next item will be on the following chunk */
		return -ENOMEM;

	*index += step;

	return rc;
}

static int
hidpp10_onboard_profiles_read_macro(struct hidpp10_device *device,
				    uint8_t page, uint8_t offset,
				    union hidpp10_macro_data **return_macro)
{
	uint8_t memory[HIDPP10_PAGE_SIZE];
	union hidpp10_macro_data *macro = NULL;
	unsigned count = 0;
	unsigned index = 0;
	uint16_t mem_index = 0;
	int rc = -ENOMEM;

	do {
		if (count == index) {
			union hidpp10_macro_data *tmp;

			count += 32;
			/* manual realloc to have the memory zero-initialized */
			tmp = zalloc(count * sizeof(union hidpp10_macro_data));
			if (macro) {
				memcpy(tmp, macro, (count - 32) * sizeof(union hidpp10_macro_data));
				free(macro);
			}
			macro = tmp;
		}

		if (rc == -ENOMEM) {
			if ((unsigned) offset + mem_index > 0xff)
				goto out_err;

			offset += mem_index;
			if (offset & 0x01)
				offset--;
			rc = hidpp10_read_memory(device, page, offset, memory);
			if (rc)
				goto out_err;
			mem_index &= 0x01;

			hidpp_log_buf_raw(&device->base, "-> ", memory + mem_index, 16 - mem_index);
		}

		rc = hidpp10_onboard_profiles_macro_next(device,
							 memory,
							 &mem_index,
							 &macro[index]);
		if (rc == -EFAULT)
			goto out_err;

		if (rc != -ENOMEM) {
			if (macro[index].any.type == HIDPP10_MACRO_JUMP) {
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
	free(macro);

	return rc;
}

static int
hidpp10_onboard_profiles_parse_macro(struct hidpp10_device *device,
				     uint8_t page, uint8_t offset,
				     union hidpp10_macro_data **return_macro)
{
	union hidpp10_macro_data *m, *macro = NULL;
	unsigned i, count = 0;
	int rc;

	hidpp_log_raw(&device->base, "*** macro starts at (0x%02x, 0x%04x) ***\n", page, offset);

	rc = hidpp10_onboard_profiles_read_macro(device, page, offset, &macro);
	if (rc < 0) {
		hidpp_log_raw(&device->base, "hidpp10: failed to read macro\n");
		return rc;
	}

	count = rc;

	for (i = 0; i < count; i++) {
		m = &macro[i];
		switch (m->any.type) {

		case HIDPP10_MACRO_NOOP:
			hidpp_log_raw(&device->base, "noop\n");
			break;
		case HIDPP10_MACRO_WAIT_FOR_BUTTON_RELEASE:
			hidpp_log_raw(&device->base, "wait for button release\n");
			break;
		case HIDPP10_MACRO_REPEAT_UNTIL_BUTTON_RELEASE:
			hidpp_log_raw(&device->base, "repeat from beginning until button release\n");
			break;
		case HIDPP10_MACRO_REPEAT:
			hidpp_log_raw(&device->base, "repeat from beginning\n");
			break;
		case HIDPP10_MACRO_KEY_PRESS:
			hidpp_log_raw(&device->base, "key press: %02x\n", m->key.key);
			break;
		case HIDPP10_MACRO_KEY_RELEASE:
			hidpp_log_raw(&device->base, "key release: %02x\n", m->key.key);
			break;
		case HIDPP10_MACRO_MOD_PRESS:
			hidpp_log_raw(&device->base, "modifier press: %02x\n", m->modifier.key);
			break;
		case HIDPP10_MACRO_MOD_RELEASE:
			hidpp_log_raw(&device->base, "modifier release: %02x\n", m->modifier.key);
			break;
		case HIDPP10_MACRO_MOUSE_WHEEL:
			hidpp_log_raw(&device->base, "mouse wheel: %+d\n", m->wheel.value);
			break;
		case HIDPP10_MACRO_MOUSE_BUTTON_PRESS:
			m->button.flags = ffs(hidpp_le_u16_to_cpu(m->button.flags));
			hidpp_log_raw(&device->base, "mouse button press: %d\n", m->button.flags);
			break;
		case HIDPP10_MACRO_MOUSE_BUTTON_RELEASE:
			m->button.flags = ffs(hidpp_le_u16_to_cpu(m->button.flags));
			hidpp_log_raw(&device->base, "mouse button release: %d\n", m->button.flags);
			break;
		case HIDPP10_MACRO_KEY_CONSUMER_CONTROL:
			m->consumer_control.key = hidpp_be_u16_to_cpu(m->consumer_control.key);
			hidpp_log_raw(&device->base, "switched to consumer control: 0x%04x\n", m->consumer_control.key);
			break;
		case HIDPP10_MACRO_DELAY:
			m->delay.time = hidpp_be_u16_to_cpu(m->delay.time);
			hidpp_log_raw(&device->base, "delay: %0.03f\n", m->delay.time/1000.0);
			break;
		case HIDPP10_MACRO_JUMP:
			/* should be skipped by hidpp10_onboard_profiles_read_macro */
			hidpp_log_raw(&device->base, "jump to: (0x%02x, 0x%02x)\n", m->jump.page, m->jump.offset);
			break;
		case HIDPP10_MACRO_JUMP_IF_PRESSED:
			hidpp_log_raw(&device->base, "conditional jump to: (0x%02x, 0x%02x)\n", m->jump.page, m->jump.offset);
			break;
		case HIDPP10_MACRO_MOUSE_POINTER_MOVE:
			break;
		case HIDPP10_MACRO_JUMP_IF_RELEASED_TIMEOUT:
			m->jump_timeout.timeout = hidpp_be_u16_to_cpu(m->jump_timeout.timeout);
			hidpp_log_raw(&device->base, "conditional jump to: (0x%02x, 0x%02x) if released within %0.03f msecs.\n", m->jump_timeout.page, m->jump_timeout.offset, m->jump_timeout.timeout / 1000.0);
			break;
		case HIDPP10_MACRO_END:
			break;
		default:
			if (m->any.type >= 0x80 && m->any.type <= 0x9F) {
				m->delay.time = 8 + (m->any.type - 0x80) * 4;
				m->any.type = HIDPP10_MACRO_DELAY;
				hidpp_log_raw(&device->base, "short delay: %0.03f\n", m->delay.time/1000.0);
			} else if (m->any.type >= 0xA0 && m->any.type <= 0xBF) {
				m->delay.time = 132 + (m->any.type - 0x9F) * 8;
				m->any.type = HIDPP10_MACRO_DELAY;
				hidpp_log_raw(&device->base, "short delay: %0.03f\n", m->delay.time/1000.0);
			} else if (m->any.type >= 0xC0 && m->any.type <= 0xDF) {
				m->delay.time = 388 + (m->any.type - 0xBF) * 16;
				m->any.type = HIDPP10_MACRO_DELAY;
				hidpp_log_raw(&device->base, "short delay: %0.03f\n", m->delay.time/1000.0);
			} else if (m->any.type >= 0xE0 && m->any.type <= 0xFE) {
				m->delay.time = 900 + (m->any.type - 0xDF) * 32;
				m->any.type = HIDPP10_MACRO_DELAY;
				hidpp_log_raw(&device->base, "short delay: %0.03f\n", m->delay.time/1000.0);
			} else {
				hidpp_log_error(&device->base, "unknown tag: 0x%02x\n", m->any.type);
			}
		}
	}

	hidpp_log_raw(&device->base, "*** end of macro ***\n");
	*return_macro = macro;

	return 0;
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
		default:
			/* macros */
			button->macro.page = b->macro.page;
			button->macro.offset = b->macro.offset;
			button->macro.address = i;
			if (profile->macros[i]) {
				free(profile->macros[i]);
				profile->macros[i] = NULL;
			}
			hidpp10_onboard_profiles_parse_macro(dev,
							     b->macro.page,
							     b->macro.offset * 2,
							     &profile->macros[i]);
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
		default:
			/* macros */
			button->macro.page = b->macro.page;
			button->macro.offset = b->macro.offset;
			button->macro.zero = 0;
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

static void
hidpp10_profile_parse_names(struct hidpp10_device *dev, struct hidpp10_profile *profile,
			    uint8_t number,
			    union _hidpp10_profile_metadata *metadata)
{
	unsigned i;

	if (strneq((char *)metadata->any.marker, "LGS02", 5)) {
		hidpp10_uchar16_to_uchar8(profile->name,
					  metadata->lgs02.name,
					  ARRAY_LENGTH(metadata->lgs02.name));
		hidpp_log_raw(&dev->base, "profile %d is named '%s'\n", number, profile->name);
		for (i = 0; i < ARRAY_LENGTH(metadata->lgs02.macro_names); i++) {
			hidpp10_uchar16_to_uchar8(profile->macro_names[i],
						  metadata->lgs02.macro_names[i],
						  ARRAY_LENGTH(metadata->lgs02.macro_names[i]));
			if (profile->macro_names[i][0])
				hidpp_log_raw(&dev->base,
					      "macro %d of profile %d is named: '%s'\n",
					      (unsigned)i,
					      number,
					      profile->macro_names[i]);
		}
	} else {
		snprintf((char *)profile->name, sizeof(profile->name) - 1, "Profile %d", number + 1);
	}
}

static void
hidpp10_profile_set_names(struct hidpp10_device *dev, struct hidpp10_profile *profile,
			  uint8_t number,
			  union _hidpp10_profile_metadata *metadata)
{
	unsigned i;

	memcpy(metadata->lgs02.marker, "LGS02", sizeof(metadata->lgs02.marker));
	hidpp10_uchar8_to_uchar16(metadata->lgs02.name,
				  profile->name,
				  ARRAY_LENGTH(metadata->lgs02.name));
	for (i = 0; i < ARRAY_LENGTH(metadata->lgs02.macro_names); i++) {
		hidpp10_uchar8_to_uchar16(metadata->lgs02.macro_names[i],
					  profile->macro_names[i],
					  ARRAY_LENGTH(metadata->lgs02.macro_names[i]));
	}
}

static int
hidpp10_read_profile(struct hidpp10_device *dev, uint8_t number)
{
	uint8_t page_data[HIDPP10_PAGE_SIZE];
	union _hidpp10_profile_data *data = (union _hidpp10_profile_data *)page_data;
	struct _hidpp10_profile_500 *p500 = &data->profile_500;
	struct _hidpp10_profile_700 *p700 = &data->profile_700;
	struct _hidpp10_profile_9 *p9 = &data->profile_9;
	size_t i;
	int res;
	struct hidpp10_profile *profile;
	union _hidpp10_button_binding *buttons;

	/* Page 0 is RAM
	 * Page 1 is the profile directory
	 * Page 2-31 are Flash
	 * -> profiles are stored in the Flash
	 *
	 * For now we assume that number refers to the index in the profile
	 * directory.
	 */

	hidpp_log_raw(&dev->base, "Fetching profile %d\n", number);

	if (dev->profile_type == HIDPP10_PROFILE_UNKNOWN)
		return -ENOTSUP;

	if (number >= dev->profile_count) {
		hidpp_log_error(&dev->base, "Profile number %d is not supported.\n", number);
		return -EINVAL;
	}

	profile = &dev->profiles[number];
	if (!profile->page) {
		unsigned long pages = 0xffff;

		/* pages 0 and 1 are ROM and directory so they are reserved */
		long_clear_bit(&pages, 0);
		long_clear_bit(&pages, 1);

		for (size_t i = 0; i < dev->profile_count; i++) {
			uint8_t page = dev->profiles[i].page;

			assert(page < sizeof(pages) * 8);
			long_clear_bit(&pages, page);
		}

		profile->page = ffsl(pages) - 1;
	}

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
		buttons = p500->buttons;
		break;
	case HIDPP10_PROFILE_G700:
		buttons = p700->buttons;
		break;
	case HIDPP10_PROFILE_G9:
		buttons = p9->buttons;
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
		return -EINVAL;
	}

	if (!profile->initialized) {
		uint8_t page = profile->page;
		res = hidpp10_read_page(dev, page, page_data);
		if (res == -EILSEQ) {
			/*
			 * if the CRC is wrong, the mouse still handles the
			 * profile. Warn the user.
			 */
			if (profile->enabled)
				hidpp_log_info(&dev->base,
					      "Profile %d has a wrong CRC, assuming valid.\n",
					      number);
			res = 0;
		}
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
			hidpp10_profile_parse_names(dev, profile, number, &p500->metadata);
			hidpp10_fill_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
			break;
		case HIDPP10_PROFILE_G700:
			profile->default_dpi_mode = p700->default_dpi_mode;
			profile->refresh_rate = p700->usb_refresh_rate ? 1000/p700->usb_refresh_rate : 0;

			hidpp10_fill_dpi_modes_8_dual(dev, profile, p700->dpi_modes, PROFILE_NUM_DPI_MODES);
			hidpp10_profile_parse_names(dev, profile, number, &p700->metadata);
			hidpp10_fill_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
			break;
		case HIDPP10_PROFILE_G9:
			profile->red = p9->red;
			profile->green = p9->green;
			profile->blue = p9->blue;
			profile->default_dpi_mode = p9->default_dpi_mode;
			profile->refresh_rate = p9->usb_refresh_rate ? 1000/p9->usb_refresh_rate : 0;

			hidpp10_fill_dpi_modes_8(dev, profile, p9->dpi_modes, PROFILE_NUM_DPI_MODES);
			hidpp10_profile_parse_names(dev, profile, number, &p9->metadata);
			hidpp10_fill_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS_G9);
			break;
		default:
			hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
		}
		profile->initialized = 1;

		hidpp_log_raw(&dev->base,
			      "+++++++++++++++++++ Profile data: +++++++++++++++++ \n");
		for (size_t x = 0; x < 78; x += 8) {
			hidpp_log_buf_raw(&dev->base, NULL, &data->data[x], min(8, 78 - x));
		}
		hidpp_log_raw(&dev->base,
			      "+++++++++++++++++++ Profile data end +++++++++++++++++ \n");

	}

	hidpp_log_raw(&dev->base, "Profile %d:\n", number);
	for (i = 0; i < 5; i++) {
		hidpp_log_raw(&dev->base,
			"  DPI mode: %dx%d dpi\n",
			profile->dpi_modes[i].xres,
			profile->dpi_modes[i].yres);
		hidpp_log_raw(&dev->base,
			"  LED status: 1:%s 2:%s 3:%s 4:%s\n",
			profile->dpi_modes[i].led[0] ? "on" : "off",
			profile->dpi_modes[i].led[1] ? "on" : "off",
			profile->dpi_modes[i].led[2] ? "on" : "off",
			profile->dpi_modes[i].led[3] ? "on" : "off");
	}
	hidpp_log_raw(&dev->base, "  Angle correction: %d\n", profile->angle_correction);
	hidpp_log_raw(&dev->base, "  Default DPI mode: %d\n", profile->default_dpi_mode);
	hidpp_log_raw(&dev->base, "  Refresh rate: %d\n", profile->refresh_rate);
	for (i = 0; i < 13; i++) {
		union hidpp10_button *button = &profile->buttons[i];
		switch (button->any.type) {
		case PROFILE_BUTTON_TYPE_BUTTON:
			hidpp_log_raw(&dev->base,
				"  Button %zd: button %d\n",
				i,
				button->button.button);
			break;
		case PROFILE_BUTTON_TYPE_KEYS:
			hidpp_log_raw(&dev->base,
				"  Button %zd: key %d modifier %x\n",
				i,
				button->keys.key,
				button->keys.modifier_flags);
			break;
		case PROFILE_BUTTON_TYPE_SPECIAL:
			hidpp_log_raw(&dev->base,
				"  Button %zd: special %x\n",
				i,
				button->special.special);
			break;
		case PROFILE_BUTTON_TYPE_CONSUMER_CONTROL:
			hidpp_log_raw(&dev->base,
				"  Button %zd: consumer: %x\n",
				i,
				button->consumer_control.consumer_control);
			break;
		case PROFILE_BUTTON_TYPE_DISABLED:
			hidpp_log_raw(&dev->base, "  Button %zd: disabled\n", i);
			break;
		default:
			/* FIXME: this is the page number for the macro,
			 * followed by a 1-byte offset */
			break ;
		}
	}

	return 0;
}

int
hidpp10_get_profile(struct hidpp10_device *dev, uint8_t number, struct hidpp10_profile *profile_return)
{
	if (dev->profile_type == HIDPP10_PROFILE_UNKNOWN)
		return -ENOTSUP;

	if (number >= dev->profile_count) {
		hidpp_log_error(&dev->base, "Profile number %d is not supported.\n", number);
		return -EINVAL;
	}

	*profile_return = dev->profiles[number];
	return 0;
}

static const enum ratbag_button_action_special hidpp10_profiles_specials[] = {
	[0x00] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
	[0x01] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,
	[0x02] = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,
	[0x03] = RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL,
	[0x04] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,
	[0x05] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,

	[0x06 ... 0x07] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,

	[0x08] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,
	[0x09] = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN,

	[0x0a ... 0x0f] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,

	[0x10] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP,
	[0x11] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,

	[0x12 ... 0x1f] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,

	[0x20] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN,
	[0x21] = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_DOWN,

	[0x22 ... 0xff] = RATBAG_BUTTON_ACTION_SPECIAL_INVALID,
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
hidpp10_set_profile(struct hidpp10_device *dev, uint8_t number, struct hidpp10_profile *profile)
{
	uint8_t page_data[HIDPP10_PAGE_SIZE];
	union _hidpp10_profile_data *data = (union _hidpp10_profile_data *)page_data;
	struct _hidpp10_profile_500 *p500 = &data->profile_500;
	struct _hidpp10_profile_700 *p700 = &data->profile_700;
	struct _hidpp10_profile_9 *p9 = &data->profile_9;
	int res;
	union _hidpp10_button_binding *buttons;
	uint16_t crc;
	uint8_t page;

	hidpp_log_raw(&dev->base, "Fetching profile %d\n", number);

	if (dev->profile_type == HIDPP10_PROFILE_UNKNOWN)
		return -ENOTSUP;

	if (number >= dev->profile_count) {
		hidpp_log_error(&dev->base, "Profile number %d is incorrect.\n", number);
		return -EINVAL;
	}

	/* something went wrong */
	if (!profile->page)
		return -ENOTSUP;

	memset(page_data, 0xff, sizeof(page_data));

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
		buttons = p500->buttons;
		break;
	case HIDPP10_PROFILE_G700:
		buttons = p700->buttons;
		break;
	case HIDPP10_PROFILE_G9:
		buttons = p9->buttons;
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
		return -ENOTSUP;
	}


	/* First, fill out the unknown fields with the constants or the current
	 * values when we are not sure. */
	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G500:
	case HIDPP10_PROFILE_G9:
		/* we do not know the actual values of the remaining field right now
		 * so pre-fill with the current data */
		res = hidpp10_read_page(dev, profile->page, page_data);
		if (res)
			return res;
		break;
	case HIDPP10_PROFILE_G700:
		memcpy(p700->unknown1, _hidpp10_profile_700_unknown1, sizeof(p700->unknown1));
		memcpy(p700->unknown2, _hidpp10_profile_700_unknown2, sizeof(p700->unknown2));
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
		return -ENOTSUP;
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
		hidpp10_profile_set_names(dev, profile, number, &p500->metadata);
		break;
	case HIDPP10_PROFILE_G700:
		p700->default_dpi_mode = profile->default_dpi_mode;
		p700->usb_refresh_rate = profile->refresh_rate ? 1000 / profile->refresh_rate : 0;

		hidpp10_write_dpi_modes_8_dual(dev, profile, p700->dpi_modes, PROFILE_NUM_DPI_MODES);
		hidpp10_write_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
		hidpp10_profile_set_names(dev, profile, number, &p700->metadata);
		break;
	case HIDPP10_PROFILE_G9:
		p9->red = profile->red;
		p9->green = profile->green;
		p9->blue = profile->blue;
		p9->default_dpi_mode = profile->default_dpi_mode;
		p9->usb_refresh_rate = 1000 / profile->refresh_rate;

		hidpp10_write_dpi_modes_8(dev, profile, p9->dpi_modes, PROFILE_NUM_DPI_MODES);
		hidpp10_write_buttons(dev, profile, buttons, PROFILE_NUM_BUTTONS);
		hidpp10_profile_set_names(dev, profile, number, &p9->metadata);
		break;
	default:
		hidpp_log_error(&dev->base, "This should never happen, complain to your maintainer.\n");
		return -ENOTSUP;
	}

	crc = hidpp_crc_ccitt(page_data, HIDPP10_PAGE_SIZE - 2);
	set_unaligned_be_u16(&page_data[HIDPP10_PAGE_SIZE - 2], crc);

	/*
	 * writing the data in several steps to prevent shroedinger state
	 * if the device is unplugged while uploading the data:
	 * - first disable the current profile by using the factory one
	 *   (this prevents the user to change the current profile by pressing
	 *    a button)
	 * - then upload in RAM half of the data
	 * - erase the portion of the flash we are overwriting
	 * - write the uploaded data to the flash
	 * - upload the rest
	 * - write the uploaded data to the flash
	 * - switch to the new profile
	 */
	res = hidpp10_set_internal_current_profile(dev, 0, PROFILE_TYPE_FACTORY);
	if (res < 0)
		return res;

	if (profile->enabled != dev->profiles[number].enabled) {
		dev->profiles[number].enabled = profile->enabled;
		res = hidpp10_write_profile_directory(dev);
		if (res < 0)
			return res;
	}

	res = hidpp10_send_hot_payload(dev,
				       0x00, 0x0000, /* destination: RAM */
				       page_data,
				       HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	page = profile->page;
	/* according to the spec, a profile can have an offset.
	 * For all the devices we know, they all start at 0x0000 */
	res = hidpp10_erase_memory(dev, page);
	if (res < 0)
		return res;

	res = hidpp10_write_flash(dev,
				  0x00, 0x0000,
				  page, 0x0000,
				  HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	res = hidpp10_send_hot_payload(dev,
				       0x00, 0x0000, /* destination: RAM */
				       page_data + HIDPP10_PAGE_SIZE / 2,
				       HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	res = hidpp10_write_flash(dev,
				  0x00, 0x0000,
				  page, HIDPP10_PAGE_SIZE / 2,
				  HIDPP10_PAGE_SIZE / 2);
	if (res < 0)
		return res;

	res = hidpp10_set_internal_current_profile(dev, number, PROFILE_TYPE_INDEX);
	if (res < 0)
		return res;

	dev->profiles[number] = *profile;
	return res;
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

	hidpp_log_raw(&dev->base, "Fetching LED status (%#02x)\n",
		      __CMD_LED_STATUS);

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

	hidpp_log_raw(&dev->base, "Setting LED status (%#02x)\n",
		      __CMD_LED_STATUS);

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
				hidpp_log_error(&dev->base, "Incorrect LED status: %02x\n", led[i]);
				return -EINVAL;
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

	hidpp_log_raw(&dev->base, "Fetching LED intensity (%#02x)\n",
		      __CMD_LED_INTENSITY);

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

	hidpp_log_raw(&dev->base, "Setting LED intensity (%#02x)\n",
		      __CMD_LED_INTENSITY);

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

	hidpp_log_raw(&dev->base, "Fetching LED color (%#02x)\n",
		      __CMD_LED_COLOR);

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

	hidpp_log_raw(&dev->base, "Setting LED color (%#02x)\n",
		      __CMD_LED_COLOR);

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

	hidpp_log_raw(&dev->base, "Fetching optical sensor settings (%#02x)\n",
		      __CMD_OPTICAL_SENSOR_SETTINGS);

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
	union hidpp10_message resolution = CMD_CURRENT_RESOLUTION(REPORT_ID_SHORT, idx, GET_REGISTER_REQ);
	union hidpp10_message resolution_long = CMD_CURRENT_RESOLUTION(REPORT_ID_SHORT, idx, GET_LONG_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Fetching current resolution (%#02x)\n",
		      __CMD_CURRENT_RESOLUTION);

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G9:
		res = hidpp10_request_command(dev, &resolution);
		if (res)
			return res;
		/* resolution is in 50dpi multiples */
		*xres = *yres = hidpp10_get_dpi_value(dev, get_unaligned_le_u16(&resolution.data[4]));
		break;
        default:
		res = hidpp10_request_command(dev, &resolution_long);
		if (res)
			return res;
		/* resolution is in 50dpi multiples */
		*xres = hidpp10_get_dpi_value(dev, get_unaligned_le_u16(&resolution_long.data[4]));
		*yres = hidpp10_get_dpi_value(dev, get_unaligned_le_u16(&resolution_long.data[6]));
        }

	return 0;
}

int
hidpp10_set_current_resolution(struct hidpp10_device *dev,
			       uint16_t xres,
			       uint16_t yres)
{
	unsigned idx = dev->index;
	union hidpp10_message resolution = CMD_CURRENT_RESOLUTION(REPORT_ID_SHORT, idx, SET_REGISTER_REQ);
	union hidpp10_message resolution_long = CMD_CURRENT_RESOLUTION(REPORT_ID_LONG, idx, SET_LONG_REGISTER_REQ);
	int res;

	hidpp_log_raw(&dev->base, "Setting current resolution (%#02x)\n",
		      __CMD_CURRENT_RESOLUTION);

	switch (dev->profile_type) {
	case HIDPP10_PROFILE_G9:
		resolution.data[4] = hidpp10_get_dpi_mapping(dev, xres);
		res = hidpp10_request_command(dev, &resolution);
		break;
        default:
		set_unaligned_le_u16(&resolution_long.data[4], hidpp10_get_dpi_mapping(dev, xres));
		set_unaligned_le_u16(&resolution_long.data[6], hidpp10_get_dpi_mapping(dev, yres));

		res = hidpp10_request_command(dev, &resolution_long);
		break;
        }

	return res;
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

	hidpp_log_raw(&dev->base, "Fetching USB refresh rate (%#02x)\n",
		      __CMD_USB_REFRESH_RATE);

	res = hidpp10_request_command(dev, &refresh);
	if (res)
		return res;

	if (!refresh.msg.parameters[0])
		return -EINVAL;

	*rate = 1000/refresh.msg.parameters[0];

	return 0;
}

int
hidpp10_set_usb_refresh_rate(struct hidpp10_device *dev,
			     uint16_t rate)
{
	unsigned idx = dev->index;
	union hidpp10_message refresh = CMD_USB_REFRESH_RATE(idx, GET_REGISTER_REQ);

	hidpp_log_raw(&dev->base, "Setting USB refresh rate (%#02x)\n",
		      __CMD_USB_REFRESH_RATE);

	refresh.msg.parameters[0] = 1000/rate;

	return hidpp10_request_command(dev, &refresh);
}

/* -------------------------------------------------------------------------- */
/* 0xA0: Generic Memory Management                                            */
/* -------------------------------------------------------------------------- */
#define __CMD_GENERIC_MEMORY_MANAGEMENT			0xA0

#define CMD_ERASE_MEMORY(idx, page) { \
	.msg = { \
		.report_id = REPORT_ID_LONG, \
		.device_idx = idx, \
		.sub_id = SET_LONG_REGISTER_REQ, \
		.address = __CMD_GENERIC_MEMORY_MANAGEMENT, \
		.string = {0x02, 0x00, \
			   0x00, 0x00, 0x00, 0x00, \
			   page, 0x00, 0x00, 0x00,\
			   0x00, 0x00, 0x00, 0x00}, \
	} \
}

#define CMD_WRITE_FLASH(idx, src_page, src_woffset, dst_page, dst_woffset, size) { \
	.msg = { \
		.report_id = REPORT_ID_LONG, \
		.device_idx = idx, \
		.sub_id = SET_LONG_REGISTER_REQ, \
		.address = __CMD_GENERIC_MEMORY_MANAGEMENT, \
		.string = {0x03, 0x00, \
			   src_page, src_woffset, 0x00, 0x00, \
			   dst_page, dst_woffset, 0x00, 0x00,\
			   size >> 8, size & 0xFF}, \
	} \
}

int
hidpp10_erase_memory(struct hidpp10_device *dev, uint8_t page)
{
	unsigned idx = dev->index;
	union hidpp10_message erase = CMD_ERASE_MEMORY(idx, page);

	hidpp_log_raw(&dev->base, "Erasing page 0x%02x\n", page);

	return hidpp10_request_command(dev, &erase);
}

int
hidpp10_write_flash(struct hidpp10_device *dev,
		    uint8_t src_page,
		    uint16_t src_offset,
		    uint8_t dst_page,
		    uint16_t dst_offset,
		    uint16_t size)
{
	unsigned idx = dev->index;
	union hidpp10_message copy = CMD_WRITE_FLASH(idx,
						     src_page, src_offset / 2,
						     dst_page, dst_offset / 2,
						     size);

	if ((src_offset % 2 != 0) || (dst_offset % 2 != 0)) {
		hidpp_log_error(&dev->base, "Accessing memory with odd offset is not supported.\n");
		return -EINVAL;
	}

	hidpp_log_raw(&dev->base, "Copying %d bytes from (0x%02x,0x%04x) to (0x%02x,0x%04x)\n",
		      size,
		      src_page, src_offset,
		      dst_page, dst_offset);

	return hidpp10_request_command(dev, &copy);
}

/* -------------------------------------------------------------------------- */
/* 0x9x: HOT payload                                                          */
/* 0xA1: HOT Control Register                                                 */
/* -------------------------------------------------------------------------- */
#define __CMD_HOT_CONTROL			0xA1

#define CMD_HOT_RESET(idx) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = SET_REGISTER_REQ, \
		.address = __CMD_HOT_CONTROL, \
		.parameters = {0x01, 0x00, 0x00 }, \
	} \
}

#define HOT_NOTIFICATION			0x50
#define HOT_WRITE				0x92
#define HOT_CONTINUE				0x93

static int
hidpp10_hot_ctrl_reset(struct hidpp10_device *dev)
{
	unsigned idx = dev->index;
	union hidpp10_message ctrl_reset = CMD_HOT_RESET(idx);

	return hidpp10_request_command(dev, &ctrl_reset);
}

static int
hidpp10_hot_request_command(struct hidpp10_device *dev, uint8_t data[LONG_MESSAGE_LENGTH])
{
	uint8_t read_buffer[LONG_MESSAGE_LENGTH] = {0};
	int ret;
	uint8_t id = data[3];

	if ((data[0] != REPORT_ID_LONG) ||
	    ((data[2] != HOT_WRITE) && (data[2] != HOT_CONTINUE)))
		return -EINVAL;

	/* Send the message to the Device */
	ret = hidpp_write_command(&dev->base, data, LONG_MESSAGE_LENGTH);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = hidpp_read_response(&dev->base, read_buffer, LONG_MESSAGE_LENGTH);

		/* Wait and retry if the USB timed out */
		if (ret == -ETIMEDOUT) {
			msleep(10);
			ret = hidpp_read_response(&dev->base, read_buffer, LONG_MESSAGE_LENGTH);
		}

		/* actual answer */
		if (read_buffer[2] == HOT_NOTIFICATION)
			break;
	} while (ret > 0);

	if (ret < 0) {
		hidpp_log_error(&dev->base, "    USB error: %s (%d)\n", strerror(-ret), -ret);
		perror("write");
		goto out_err;
	}

	if (read_buffer[4] != id) {
		ret = -EPROTO;
		hidpp_log_error(&dev->base, "    Protocol error: ids do not match.\n");
		perror("write");
		goto out_err;
	}

	ret = 0;

out_err:
	return ret;
}

struct hot_header {
	uint8_t id;
	uint8_t page;
	uint8_t offset;
	uint16_t zero;
	uint16_t size;
	uint16_t zero1;
} __attribute__ ((__packed__));

static int
hidpp10_send_hot_chunk(struct hidpp10_device *dev,
		       uint8_t index,
		       bool first,
		       uint8_t dst_page,
		       uint16_t dst_offset,
		       uint8_t *data,
		       unsigned size)
{
	struct hot_header header = {0};
	uint8_t buffer[LONG_MESSAGE_LENGTH] = {0};
	unsigned offset = 0;
	unsigned count;
	int res;

	buffer[offset++] = REPORT_ID_LONG;
	buffer[offset++] = dev->index;

	if (first) {
		if (dst_offset % 2 != 0) {
			hidpp_log_error(&dev->base, "Writing memory with odd offset is not supported.\n");
			return -EINVAL;
		}
		buffer[offset++] = HOT_WRITE;
		buffer[offset++] = index;
		header.id = 0x01;
		header.page = dst_page;
		header.offset = dst_offset / 2;
		header.size = hidpp_cpu_to_be_u16(size);
		memcpy(&buffer[offset], &header, sizeof(header));
		offset += sizeof(header);
	} else {
		buffer[offset++] = HOT_CONTINUE;
		buffer[offset++] = index;
	}

	count = min(LONG_MESSAGE_LENGTH - offset, size);
	if (count <= 0)
		return -EINVAL;

	memcpy(&buffer[offset], data, count);

	res = hidpp10_hot_request_command(dev, buffer);
	if (res < 0)
		return res;

	return count;
}

int
hidpp10_send_hot_payload(struct hidpp10_device *dev,
			 uint8_t dst_page,
			 uint16_t dst_offset,
			 uint8_t *data,
			 unsigned size)
{
	bool first = true;
	unsigned int count = 0;
	unsigned int index = 0;
	int res;

	res = hidpp10_hot_ctrl_reset(dev);
	if (res < 0)
		return res;

	do {
		res = hidpp10_send_hot_chunk(dev, index, first,
					     dst_page, dst_offset,
					     data + count,
					     size - count);
		if (res < 0)
			return res;

		first = false;
		count += res;
		index++;
	} while (size > count);

	return 0;
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
	read_crc = get_unaligned_be_u16(&bytes[HIDPP10_PAGE_SIZE - 2]);

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
	*wpid = get_unaligned_be_u16(&pairing_information.msg.string[3]);
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

	*serial = get_unaligned_be_u32(&info.msg.string[1]);

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
hidpp10_get_firmware_information(struct hidpp10_device *dev,
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
	build = get_unaligned_be_u16(&build_information.msg.string[1]);

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
	uint16_t xres, yres;
	uint16_t refresh_rate;
	enum hidpp10_led_status led[6];
	uint8_t current_profile;

	hidpp10_get_individual_features(dev, &feature_mask);
	hidpp10_get_hidpp_notifications(dev, &notifications);

	hidpp10_get_current_resolution(dev, &xres, &yres);
	hidpp10_get_led_status(dev, led);
	hidpp10_get_usb_refresh_rate(dev, &refresh_rate);

	hidpp10_get_optical_sensor_settings(dev, &reflect);

	return hidpp10_get_current_profile(dev, &current_profile);
}

int
hidpp10_device_new(const struct hidpp_device *base,
		   int idx,
		   enum hidpp10_profile_type type,
		   unsigned int profile_count,
		   struct hidpp10_device **out)
{
	int rc;
	struct hidpp10_device *dev;

	dev = zalloc(sizeof(*dev));

	dev->index = idx;
	dev->base = *base;
	dev->profile_type = type;
	dev->profile_count = profile_count;
	dev->profiles = zalloc(dev->profile_count * sizeof(struct hidpp10_profile));

	if ((rc = hidpp10_get_device_info(dev)) != 0) {
		hidpp10_device_destroy(dev);
		dev = NULL;
	}

	*out = dev;

	return rc;
}

int
hidpp10_device_read_profiles(struct hidpp10_device *dev)
{
	unsigned int i;

	hidpp10_read_profile_directory(dev);

	for (i = 0; i < dev->profile_count && i < HIDPP10_NUM_PROFILES; i++)
		hidpp10_read_profile(dev, i);

	return 0;
}

void
hidpp10_device_destroy(struct hidpp10_device *dev)
{
	union hidpp10_macro_data **macro;
	unsigned i;

	free(dev->dpi_table);
	for (i = 0; i < dev->profile_count; i++) {
		ARRAY_FOR_EACH(dev->profiles[i].macros, macro) {
			if (*macro) {
				free(*macro);
				*macro = NULL;
			}
		}
	}

	free(dev->profiles);
	free(dev);
}
