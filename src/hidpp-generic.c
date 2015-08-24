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

#include "config.h"

#include "hidpp-generic.h"

#include <stddef.h>

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
