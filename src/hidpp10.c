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

#define ERROR_MSG(__hidpp_msg, idx)	{ \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = __ERROR_MSG, \
		.address = __hidpp_msg->msg.sub_id, \
		.parameters = {__hidpp_msg->msg.address, 0x00, 0x00 }, \
	} \
}

static inline uint16_t
hidpp10_get_unaligned_u16le(uint8_t *buf)
{
	return (buf[1] << 8) | buf[0];
}

static inline uint16_t
hidpp10_get_unaligned_u16(uint8_t *buf)
{
	return (buf[0] << 8) | buf[1];
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

static int
hidpp10_write_command(struct hidpp10_device *dev, uint8_t *cmd, int size)
{
	struct ratbag_device *device = dev->ratbag_device;
	int res = ratbag_hidraw_output_report(device, cmd, size);

	if (res == 0)
		return 0;

	if (res < 0)
		log_error(device->ratbag, "Error: %s (%d)\n", strerror(-res), -res);

	return res;
}

int
hidpp10_request_command(struct hidpp10_device *dev, union hidpp10_message *msg)
{
	struct ratbag_device *device = dev->ratbag_device;
	struct ratbag *ratbag = device->ratbag;
	union hidpp10_message read_buffer;
	union hidpp10_message expected_header;
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

	log_buf_raw(ratbag, "sending: ", msg->data, SHORT_MESSAGE_LENGTH);
	log_buf_raw(ratbag, "  expected_header:	", expected_header.data, SHORT_MESSAGE_LENGTH);
	log_buf_raw(ratbag, "  expected_error_dev:	", expected_error_dev.data, SHORT_MESSAGE_LENGTH);

	/* Send the message to the Device */
	ret = hidpp10_write_command(dev, msg->data, SHORT_MESSAGE_LENGTH);
	if (ret)
		goto out_err;

	/*
	 * Now read the answers from the device:
	 * loop until we get the actual answer or an error code.
	 */
	do {
		ret = ratbag_hidraw_read_input_report(device, read_buffer.data, LONG_MESSAGE_LENGTH);

		/* Overwrite the return device index with ours. The kernel
		 * sets our device index on write, but gives us the real
		 * device index on reply. Overwrite it with our index so the
		 * messages are easier to check and compare.
		 */
		read_buffer.msg.device_idx = msg->msg.device_idx;

		log_buf_raw(ratbag, " *** received: ", read_buffer.data, ret);


		/* actual answer */
		if (!memcmp(read_buffer.data, expected_header.data, 4))
			break;

		/* error */
		if (!memcmp(read_buffer.data, expected_error_dev.data, 5)) {
			hidpp_err = read_buffer.msg.parameters[1];
			log_raw(ratbag,
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
		log_buf_raw(ratbag, "    received: ", read_buffer.data, ret);
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

static int
hidpp10_read_memory(struct hidpp10_device *dev, uint8_t page, uint16_t offset,
		    uint8_t bytes[16]);

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
				uint8_t *reporting_flags_r0,
				uint8_t *reporting_flags_r2)
{
	unsigned idx = dev->index;
	union hidpp10_message notifications = CMD_HIDPP_NOTIFICATIONS(idx, GET_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Fetching HID++ notifications\n");

	res = hidpp10_request_command(dev, &notifications);
	if (res)
		return res;

	*reporting_flags_r0 = notifications.msg.parameters[0];
	*reporting_flags_r2 = notifications.msg.parameters[2];

	return res;
}

int
hidpp10_set_hidpp_notifications(struct hidpp10_device *dev,
				uint8_t reporting_flags_r0,
				uint8_t reporting_flags_r2)
{
	unsigned idx = dev->index;
	union hidpp10_message notifications = CMD_HIDPP_NOTIFICATIONS(idx, SET_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Setting HID++ notifications\n");

	notifications.msg.parameters[0] = reporting_flags_r0;
	notifications.msg.parameters[2] = reporting_flags_r2;

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
				uint8_t *feature_bit_r0,
				uint8_t *feature_bit_r2)
{
	unsigned idx = dev->index;
	union hidpp10_message features = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, GET_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Fetching individual features\n");

	res = hidpp10_request_command(dev, &features);
	if (res)
		return res;

	*feature_bit_r0 = features.msg.parameters[0];
	*feature_bit_r2 = features.msg.parameters[2];

	return 0;
}

int
hidpp10_set_individual_feature(struct hidpp10_device *dev,
			       uint8_t feature_bit_r0,
			       uint8_t feature_bit_r2)
{
	unsigned idx = RECEIVER_IDX;
	union hidpp10_message mode = CMD_ENABLE_INDIVIDUAL_FEATURES(idx, SET_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Setting individual features\n");

	mode.msg.device_idx = dev->index;

	mode.msg.parameters[0] = feature_bit_r0;
	mode.msg.parameters[1] = feature_bit_r2;

	res = hidpp10_request_command(dev, &mode);

	return res;
}

/* -------------------------------------------------------------------------- */
/* 0x0F: Profile queries                                                      */
/* -------------------------------------------------------------------------- */
#define __CMD_PROFILE				0x0F

#define CMD_PROFILE(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = sub, \
		.address = __CMD_PROFILE, \
		.parameters = {0x00, 0x00, 0x00 }, \
	} \
}

struct _hidpp10_dpi_mode {
	uint16_t xres;
	uint16_t yres;
	uint8_t led1:4;
	uint8_t led2:4;
	uint8_t led3:4;
	uint8_t led4:4;
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_dpi_mode) == 6, "Invalid size");

union _hidpp10_button_binding {
	struct {
		uint8_t type;
		uint8_t pad;
		uint8_t pad1;
	} any;
	struct {
		uint8_t type; /* 0x81 */
		uint8_t button_flags_lsb;
		uint8_t button_flags_msb;
	} button;
	struct {
		uint8_t type; /* 0x82 */
		uint8_t modifier_flags;
		uint8_t key;
	} keyboard_keys;
	struct {
		uint8_t type; /* 0x83 */
		uint8_t flags1;
		uint8_t flags2;
	} special;
	struct {
		uint8_t type; /* 0x84 */
		uint8_t consumer_control1;
		uint8_t consumer_control2;
	} consumer_control;
	struct {
		uint8_t type; /* 0x8F */
		uint8_t zero0;
		uint8_t zero1;
	} disabled;
} __attribute__((packed));
_Static_assert(sizeof(union _hidpp10_button_binding) == 3, "Invalid size");

struct _hidpp10_profile {
	uint8_t unknown[4];
	struct _hidpp10_dpi_mode dpi_modes[PROFILE_NUM_DPI_MODES];
	uint8_t angle_correction;
	uint8_t default_dpi_mode;
	uint8_t unknown2[2];
	uint8_t usb_refresh_rate;
	union _hidpp10_button_binding buttons[PROFILE_NUM_BUTTONS];
} __attribute__((packed));
_Static_assert(sizeof(struct _hidpp10_profile) == 78, "Invalid size");

union _hidpp10_profile_data {
	struct _hidpp10_profile profile;
	uint8_t data[((sizeof(struct _hidpp10_profile) + 15)/16)*16];
};
_Static_assert((sizeof(union _hidpp10_profile_data) % 16) == 0, "Invalid size");

int
hidpp10_get_current_profile(struct hidpp10_device *dev, int8_t *current_profile)
{
	unsigned idx = dev->index;
	union hidpp10_message profile = CMD_PROFILE(idx, GET_REGISTER_REQ);
	int res;
	int8_t page;

	log_raw(dev->ratbag_device->ratbag, "Fetching current profile\n");

	res = hidpp10_request_command(dev, &profile);
	if (res)
		return res;

	page = profile.msg.parameters[0]; /* FIXME: my mouse is always 0 */
	*current_profile = page;

	/* FIXME: my mouse appears to be on page 5, but with the offset of
	 * 3, it's actually profile 2. not sure how to  change this */
	*current_profile = 2;

	return 0;
}

int
hidpp10_get_profile(struct hidpp10_device *dev, int8_t number, struct hidpp10_profile *profile_return)
{
	struct ratbag *ratbag = dev->ratbag_device->ratbag;
	union _hidpp10_profile_data data;
	struct _hidpp10_profile *p = &data.profile;
	size_t i;
	int res;
	struct hidpp10_profile profile;

	/* FIXME: profile offset appears to be 3, 1 and 2 are garbage */
	number += 3;

	log_raw(dev->ratbag_device->ratbag, "Fetching profile %d\n", number);

	for (i = 0; i < sizeof(data); i += 16) {
		res = hidpp10_read_memory(dev, number, i,  &data.data[i]);
		if (res)
			return res;
	}

	profile.angle_correction = p->angle_correction;
	profile.default_dpi_mode = p->default_dpi_mode;
	profile.refresh_rate = p->usb_refresh_rate ? 1000/p->usb_refresh_rate : 0;

	profile.num_dpi_modes = PROFILE_NUM_DPI_MODES;
	for (i = 0; i < PROFILE_NUM_DPI_MODES; i++) {
		uint8_t *be; /* in big endian */
		struct _hidpp10_dpi_mode *dpi = &p->dpi_modes[i];

		be = (uint8_t*)&dpi->xres;
		profile.dpi_modes[i].xres = hidpp10_get_unaligned_u16(be) * 50;
		be = (uint8_t*)&dpi->yres;
		profile.dpi_modes[i].yres = hidpp10_get_unaligned_u16(be) * 50;

		profile.dpi_modes[i].led[0] = dpi->led1 == 0x2;
		profile.dpi_modes[i].led[1] = dpi->led2 == 0x2;
		profile.dpi_modes[i].led[2] = dpi->led3 == 0x2;
		profile.dpi_modes[i].led[3] = dpi->led4 == 0x2;
	}

	profile.num_buttons = PROFILE_NUM_BUTTONS;
	for (i = 0; i < PROFILE_NUM_BUTTONS; i++) {
		union _hidpp10_button_binding *b = &p->buttons[i];
		union hidpp10_button *button = &profile.buttons[i];

		button->any.type = b->any.type;

		switch (b->any.type) {
		case PROFILE_BUTTON_TYPE_BUTTON:
			button->button.button =
				ffs(hidpp10_get_unaligned_u16le(&b->button.button_flags_lsb));
			break;
		case PROFILE_BUTTON_TYPE_KEYS:
			button->keys.modifier_flags = b->keyboard_keys.modifier_flags;
			button->keys.key = b->keyboard_keys.key;
			break;
		case PROFILE_BUTTON_TYPE_SPECIAL:
			button->special.special = ffs(hidpp10_get_unaligned_u16le(&b->special.flags1));
			break;
		case PROFILE_BUTTON_TYPE_CONSUMER_CONTROL:
			button->consumer_control.consumer_control =
				  hidpp10_get_unaligned_u16(&b->consumer_control.consumer_control1);
			break;
		case PROFILE_BUTTON_TYPE_DISABLED:
			break;
		}

	}

	log_buf_raw(ratbag,
		    "+++++++++++++++++++ Profile data: +++++++++++++++++ \n",
		    data.data, 78);

	log_raw(ratbag, "Profile %d:\n", number);
	for (i = 0; i < 5; i++) {
		log_raw(ratbag,
			"DPI mode: %dx%d dpi\n",
			profile.dpi_modes[i].xres,
			profile.dpi_modes[i].yres);
	        log_raw(ratbag,
			"LED status: 1:%s 2:%s 3:%s 4:%s\n",
			(p->dpi_modes[i].led1 & 0x2) ? "on" : "off",
			(p->dpi_modes[i].led2 & 0x2) ? "on" : "off",
			(p->dpi_modes[i].led3 & 0x2) ? "on" : "off",
			(p->dpi_modes[i].led4 & 0x2) ? "on" : "off");
	}
	log_raw(ratbag, "Angle correction: %d\n", profile.angle_correction);
	log_raw(ratbag, "Default DPI mode: %d\n", profile.default_dpi_mode);
	log_raw(ratbag, "Refresh rate: %d\n", profile.refresh_rate);
	for (i = 0; i < 13; i++) {
		union _hidpp10_button_binding *button = &p->buttons[i];
		switch (button->any.type) {
		case 0x81:
			log_raw(ratbag,
				"Button %d: button %d\n",
				i,
				ffs(hidpp10_get_unaligned_u16le(&button->button.button_flags_lsb)));
			break;
		case 0x82:
			log_raw(ratbag,
				"Button %d: key %d modifier %x\n",
				i,
				button->keyboard_keys.key,
				button->keyboard_keys.modifier_flags);
			break;
		case 0x83:
			log_raw(ratbag,
				"Button %d: special %x\n",
				i,
				ffs(hidpp10_get_unaligned_u16le(&button->special.flags1)));
			break;
		case 0x84:
			log_raw(ratbag,
				"Button %d: consumer: %x\n",
				i,
				hidpp10_get_unaligned_u16(&button->consumer_control.consumer_control1));
			break;
		case 0x8F:
			log_raw(ratbag, "Button %d: disabled\n", i);
			break;
		default:
			/* FIXME: this is the page number for the macro,
			 * followed by a 1-byte offset */
			break ;
		}
	}

	*profile_return = profile;

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
		       bool led[4])
{
	unsigned idx = dev->index;
	union hidpp10_message led_status = CMD_LED_STATUS(idx, GET_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Fetching LED status\n");

	res = hidpp10_request_command(dev, &led_status);
	if (res)
		return res;

	/* each led is 4-bits, 0x1 == off, 0x2 == on */
	led[0] = !!(led_status.msg.parameters[0] & 0x02); /* running man logo */
	led[1] = !!(led_status.msg.parameters[0] & 0x20); /* lowest */
	led[2] = !!(led_status.msg.parameters[1] & 0x02); /* middle */
	led[3] = !!(led_status.msg.parameters[1] & 0x20); /* highest */

	return 0;
}

int
hidpp10_set_led_status(struct hidpp10_device *dev,
		       const bool led[4])
{
	unsigned idx = dev->index;
	union hidpp10_message led_status = CMD_LED_STATUS(idx, SET_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Setting LED status\n");

	/* each led is 4-bits, 0x1 == off, 0x2 == on */
	led_status.msg.parameters[0] |= led[0] ? 0x02 : 0x01; /* running man logo */
	led_status.msg.parameters[0] |= led[1] ? 0x20 : 0x10; /* lowest */
	led_status.msg.parameters[1] |= led[2] ? 0x02 : 0x01; /* middle */
	led_status.msg.parameters[1] |= led[3] ? 0x20 : 0x10; /* highest */

	res = hidpp10_request_command(dev, &led_status);
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

	log_raw(dev->ratbag_device->ratbag, "Fetching optical sensor settings\n");

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

#define CMD_CURRENT_RESOLUTION(idx, sub) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
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
	union hidpp10_message resolution = CMD_CURRENT_RESOLUTION(idx, GET_LONG_REGISTER_REQ);
	int res;

	log_raw(dev->ratbag_device->ratbag, "Fetching current resolution\n");

	res = hidpp10_request_command(dev, &resolution);
	if (res)
		return res;

	/* resolution is in 50dpi multiples */
	*xres = hidpp10_get_unaligned_u16le(&resolution.data[4]) * 50;
	*yres = hidpp10_get_unaligned_u16le(&resolution.data[6]) * 50;

	return 0;
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

	log_raw(dev->ratbag_device->ratbag, "Fetching USB refresh rate\n");

	res = hidpp10_request_command(dev, &refresh);
	if (res)
		return res;

	*rate = 1000/refresh.msg.parameters[0];

	return 0;
}

/* -------------------------------------------------------------------------- */
/* 0xA2: Reading memory                                                       */
/* -------------------------------------------------------------------------- */
#define __CMD_READ_MEMORY			0xA2

#define CMD_READ_MEMORY(idx, page, offset) { \
	.msg = { \
		.report_id = REPORT_ID_SHORT, \
		.device_idx = idx, \
		.sub_id = GET_LONG_REGISTER_REQ, \
		.address = __CMD_READ_MEMORY, \
		.parameters = {page, offset & 0xff, offset >> 8 }, \
	} \
}

static int
hidpp10_read_memory(struct hidpp10_device *dev, uint8_t page, uint16_t offset,
		    uint8_t bytes[16])
{
	unsigned idx = dev->index;
	union hidpp10_message readmem = CMD_READ_MEMORY(idx, page, offset/2);
	int res;

	log_raw(dev->ratbag_device->ratbag,
		"Reading memory page %d, offset %#x\n",
		page, offset);

	res = hidpp10_request_command(dev, &readmem);
	if (res)
		return res;

	memcpy(bytes, readmem.msg.string, sizeof(readmem.msg.string));

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
		.device_idx = RECEIVER_IDX, \
		.sub_id = SET_REGISTER_REQ, \
		.address = __CMD_DEVICE_CONNECTION_DISCONNECTION, \
		.parameters = {cmd, idx - 1, timeout }, \
	} \
}

int
hidpp10_open_lock(struct hidpp10_device *device)
{
	union hidpp10_message open_lock = CMD_DEVICE_CONNECTION_DISCONNECTION(0x00, CONNECT_DEVICES_OPEN_LOCK, 0x08);

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
		.device_idx = RECEIVER_IDX, \
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

	log_raw(dev->ratbag_device->ratbag, "Fetching pairing information\n");

	res = hidpp10_request_command(dev, &pairing_information);
	if (res)
		return -1;

	*report_interval = pairing_information.msg.string[2];
	*wpid = hidpp10_get_unaligned_u16(&pairing_information.msg.string[3]);
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

	log_raw(dev->ratbag_device->ratbag, "Fetching device name\n");

	res = hidpp10_request_command(dev, &device_name);
	if (res)
		return -1;
	*name_size = min(*name_size, device_name.msg.string[1]);
	strncpy_safe(name, (char*)&device_name.msg.string[2], *name_size);

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

	log_raw(dev->ratbag_device->ratbag, "Fetching firmware information\n");

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
	build = hidpp10_get_unaligned_u16(&build_information.msg.string[1]);

	*major_out = maj;
	*minor_out = min;
	*build_out = build;

	return 0;
}

void hidpp10_list_devices(struct ratbag_device *device) {
	struct hidpp10_device *dev;
	int i;

	for (i = 0; i < 6; ++i) {
		dev = hidpp10_device_new_from_idx(device, i);
		if (!dev)
			continue;

		log_info(device->ratbag, "[%d] %s	%s (Wireless PID: %04x)\n",
			 i,
			 device_types[dev->device_type] ? device_types[dev->device_type] : "",
			 dev->name, dev->wpid);
		hidpp10_device_destroy(dev);
	}

}

/* -------------------------------------------------------------------------- */
/* general device handling                                                    */
/* -------------------------------------------------------------------------- */
static int
hidpp10_get_device_info(struct hidpp10_device *dev)
{
	uint8_t f1, f2;
	uint8_t reflect;
	int i;

	hidpp10_get_individual_features(dev, &f1, &f2);
	hidpp10_get_hidpp_notifications(dev, &f1, &f2);

	hidpp10_get_current_resolution(dev, &dev->xres, &dev->yres);
	hidpp10_get_led_status(dev, dev->led);
	hidpp10_get_usb_refresh_rate(dev, &dev->refresh_rate);

	hidpp10_get_optical_sensor_settings(dev, &reflect);

	hidpp10_get_current_profile(dev, &dev->current_profile);

	for (i = 0; i < HIDPP10_NUM_PROFILES; i++)
		hidpp10_get_profile(dev, i, &dev->profiles[i]);

	return 0;
}

static struct hidpp10_device*
hidpp10_device_new(struct ratbag_device *device, int index)
{
	struct hidpp10_device *dev;

	dev = zalloc(sizeof(*dev));
	if (!dev)
		return NULL;

	dev->index = index;
	dev->ratbag_device = ratbag_device_ref(device);

	return dev;
}

struct hidpp10_device*
hidpp10_device_new_from_wpid(struct ratbag_device* device, uint16_t wpid)
{
	struct hidpp10_device *dev;
	int i;

	for (i = 1; i < 7; i++) {
		dev = hidpp10_device_new_from_idx(device, i);
		if (!dev)
			continue;

		if (dev->wpid == wpid)
			return dev;

		hidpp10_device_destroy(dev);
	}

	return NULL;
}

struct hidpp10_device*
hidpp10_device_new_from_idx(struct ratbag_device *device, int idx)
{
	struct hidpp10_device *dev;

	dev = hidpp10_device_new(device, idx);
	if (!dev)
		return NULL;

	dev->index = idx;
	if (hidpp10_get_device_info(dev) != 0) {
		hidpp10_device_destroy(dev);
		dev = NULL;
	}

	return dev;
}

void
hidpp10_device_destroy(struct hidpp10_device *dev)
{
	ratbag_device_unref(dev->ratbag_device);
	free(dev);
}
