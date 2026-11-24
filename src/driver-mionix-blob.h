/*
 * Copyright © 2026 Fawaz Alghzawi
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
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Mionix Castor profile blob codec.
 *
 * Pure byte manipulation, no libratbag types and no I/O, so it can be
 * unit-tested against captured frames without hardware.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIONIX_BLOB_LEN			1041
#define MIONIX_NUM_PROFILES		5
#define MIONIX_NUM_RESOLUTIONS		3
#define MIONIX_NUM_BUTTONS		6
#define MIONIX_NUM_LEDS			2

/* Offsets within the blob. See castty research/PROTOCOL.md. */
#define MIONIX_OFF_CMD			1
#define MIONIX_OFF_PROFILE_INDEX	5
#define MIONIX_OFF_TERMINATOR_FLAG	6
#define MIONIX_OFF_PROFILE_INDEX_DUP	16
#define MIONIX_OFF_NAME			17
#define MIONIX_NAME_LEN			10
#define MIONIX_OFF_MAGIC			34	/* constant 0x08 */
#define MIONIX_OFF_POLLING		37
#define MIONIX_OFF_ACTIVE_DPI		66
#define MIONIX_OFF_ANGLE_SNAPPING	88
#define MIONIX_OFF_DPI_COUNT		102	/* constant 0x03 */
#define MIONIX_OFF_BUTTON_TABLE		117
#define MIONIX_BUTTON_ENTRY_LEN		7

/*
 * Two DPI ranges, deliberately different: advertise conservatively, accept
 * generously.
 *
 * MIONIX_DPI_MIN/MAX is what the driver offers libratbag. 400-9150 is the range
 * castty's research/PROTOCOL.md confirmed in captures ("Confirmed values range
 * 400-9150, always multiples of 50"), so it is what we know the vendor software
 * itself produces.
 *
 * MIONIX_DPI_VALID_MIN/MAX is what mionix_blob_is_valid() will believe. The
 * validator's job is to reject the all-zero warm-up buffer and obvious garbage,
 * not to police values -- a blob is not corrupt merely because another tool set
 * a DPI we would not offer. castty permits 100-10000 (src/hardware/profile.rs,
 * DPI_MIN/DPI_MAX: the Castor's rated maximum, wider than the captured range),
 * and a profile written by it must still read back, or every profile fails
 * validation, probe returns -ENODEV, and the mouse silently does not appear in
 * libratbag at all. Zero is still rejected, which is all the warm-up case needs.
 */
#define MIONIX_DPI_MIN			400
#define MIONIX_DPI_MAX			9150
#define MIONIX_DPI_VALID_MIN		100
#define MIONIX_DPI_VALID_MAX		10000

/*
 * Angle snapping is a level, not a flag; 0 is off, which is the factory value.
 * libratbag's AngleSnapping is a boolean (see driver-mionix.c), so the driver
 * needs a level to enable at when it is asked to turn snapping on and the
 * device is sitting at 0. The vendor software exposes the level directly and so
 * has no "on" default of its own; the midpoint of the usable 1-15 range is the
 * least surprising choice.
 */
#define MIONIX_ANGLE_SNAPPING_MAX	15
#define MIONIX_ANGLE_SNAPPING_ON	8

#define MIONIX_NUM_COLOUR_RECORDS	6

#define MIONIX_ANIM_SOLID		0x1
#define MIONIX_ANIM_BLINKING		0x2
#define MIONIX_ANIM_PULSATING		0x3
#define MIONIX_ANIM_BREATHING		0x4

#define MIONIX_MODE_ANIMATION(b)	((b) & 0x0F)
#define MIONIX_MODE_RAINBOW(b)		((((b) >> 4) & 0x0F) == 1)
#define MIONIX_MODE_MAKE(anim, rainbow)	((uint8_t)((anim) | ((rainbow) ? 0x10 : 0x00)))

#define MIONIX_BTN_TYPE_MOUSE		0x00
#define MIONIX_BTN_TYPE_SCROLL		0x01
#define MIONIX_BTN_TYPE_KEY		0x02
#define MIONIX_BTN_TYPE_MACRO		0x03
#define MIONIX_BTN_TYPE_PROFILE	0x08
#define MIONIX_BTN_TYPE_DPI		0x09
#define MIONIX_BTN_TYPE_DISABLED	0xFF

#define MIONIX_SWITCH_UP		0xF0
#define MIONIX_SWITCH_ROLL		0xF1
#define MIONIX_SWITCH_DOWN		0xF2

struct mionix_led {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct mionix_button {
	uint8_t type;
	uint8_t param;
};

bool mionix_blob_is_valid(const uint8_t *blob, size_t len);
void mionix_blob_get_led(const uint8_t *blob, unsigned int index,
			 struct mionix_led *out);
uint8_t mionix_blob_get_mode(const uint8_t *blob);
void mionix_blob_get_resolution(const uint8_t *blob, unsigned int index,
				uint16_t *x, uint16_t *y);
unsigned int mionix_blob_get_report_rate(const uint8_t *blob);
void mionix_blob_get_name(const uint8_t *blob, char *out, size_t outlen);
uint8_t mionix_blob_get_active_dpi(const uint8_t *blob);
uint8_t mionix_blob_get_angle_snapping(const uint8_t *blob);
void mionix_blob_get_button(const uint8_t *blob, unsigned int index,
			    struct mionix_button *out);
void mionix_blob_set_led(uint8_t *blob, unsigned int index,
			 const struct mionix_led *led);
void mionix_blob_set_mode(uint8_t *blob, uint8_t mode);
void mionix_blob_set_resolution(uint8_t *blob, unsigned int index,
				uint16_t x, uint16_t y);
void mionix_blob_set_report_rate(uint8_t *blob, unsigned int hz);
void mionix_blob_set_angle_snapping(uint8_t *blob, uint8_t value);
void mionix_blob_set_button(uint8_t *blob, unsigned int index,
			    const struct mionix_button *btn);
void mionix_blob_prepare_write(uint8_t *blob, uint8_t profile_index);
