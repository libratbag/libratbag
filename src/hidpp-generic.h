/*
 * HID++ generic definitions
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
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#include <stdint.h>
#include <stddef.h>

#ifndef HIDPP_GENERIC_H
#define HIDPP_GENERIC_H

#define RECEIVER_IDX				0xFF
#define WIRED_DEVICE_IDX			0x00

#define REPORT_ID_SHORT				0x10
#define REPORT_ID_LONG				0x11

#define SHORT_MESSAGE_LENGTH			7
#define LONG_MESSAGE_LENGTH			20

#define SET_REGISTER_REQ			0x80
#define SET_REGISTER_RSP			0x80
#define GET_REGISTER_REQ			0x81
#define GET_REGISTER_RSP			0x81
#define SET_LONG_REGISTER_REQ			0x82
#define SET_LONG_REGISTER_RSP			0x82
#define GET_LONG_REGISTER_REQ			0x83
#define GET_LONG_REGISTER_RSP			0x83
#define __ERROR_MSG				0x8F

#define ERR_SUCCESS				0x00
#define ERR_INVALID_SUBID			0x01
#define ERR_INVALID_ADDRESS			0x02
#define ERR_INVALID_VALUE			0x03
#define ERR_CONNECT_FAIL			0x04
#define ERR_TOO_MANY_DEVICES			0x05
#define ERR_ALREADY_EXISTS			0x06
#define ERR_BUSY				0x07
#define ERR_UNKNOWN_DEVICE			0x08
#define ERR_RESOURCE_ERROR			0x09
#define ERR_REQUEST_UNAVAILABLE			0x0A
#define ERR_INVALID_PARAM_VALUE			0x0B
#define ERR_WRONG_PIN_CODE			0x0C

struct hidpp_device {
	int hidraw_fd;
};

extern const char *hidpp_errors[0xFF];

const char *
hidpp20_1b04_get_physical_mapping_name(uint16_t value);

enum ratbag_button_type
hidpp20_1b04_get_physical_mapping(uint16_t value);

const char *
hidpp20_1b04_get_logical_mapping_name(uint16_t value);

const struct ratbag_button_action *
hidpp20_1b04_get_logical_mapping(uint16_t value);

uint16_t
hidpp20_1b04_get_logical_control_id(const struct ratbag_button_action *action);

int
hidpp_write_command(struct hidpp_device *dev, uint8_t *cmd, int size);

int
hidpp_read_response(struct hidpp_device *dev, uint8_t *buf, size_t size);

#endif /* HIDPP_GENERIC_H */
