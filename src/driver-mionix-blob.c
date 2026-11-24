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
 */

#include "driver-mionix-blob.h"

static const unsigned int dpi_offsets[MIONIX_NUM_RESOLUTIONS] = { 68, 77, 98 };

static const unsigned int colour_offsets[MIONIX_NUM_COLOUR_RECORDS] = {
	39, 43, 49, 53, 57, 61
};

static uint16_t
le16(const uint8_t *blob, unsigned int at)
{
	return (uint16_t)(blob[at] | (blob[at + 1] << 8));
}

bool
mionix_blob_is_valid(const uint8_t *blob, size_t len)
{
	unsigned int i;

	if (len != MIONIX_BLOB_LEN)
		return false;

	if (blob[MIONIX_OFF_MAGIC] != 0x08)
		return false;

	if (blob[MIONIX_OFF_DPI_COUNT] != 0x03)
		return false;

	for (i = 0; i < MIONIX_NUM_RESOLUTIONS; i++) {
		uint16_t x = le16(blob, dpi_offsets[i]);
		uint16_t y = le16(blob, dpi_offsets[i] + 2);

		/*
		 * The permissive range, not the advertised one: see the comment
		 * on MIONIX_DPI_VALID_MIN. A profile another tool wrote at 100
		 * DPI is unusual, not corrupt, and refusing it here would keep
		 * the whole device out of libratbag.
		 */
		if (x < MIONIX_DPI_VALID_MIN || x > MIONIX_DPI_VALID_MAX)
			return false;
		if (y < MIONIX_DPI_VALID_MIN || y > MIONIX_DPI_VALID_MAX)
			return false;
	}

	return true;
}

void
mionix_blob_get_led(const uint8_t *blob, unsigned int index,
		    struct mionix_led *out)
{
	unsigned int base;

	if (index >= MIONIX_NUM_LEDS) {
		out->red = out->green = out->blue = 0;
		return;
	}

	base = colour_offsets[index];
	out->red = blob[base];
	out->green = blob[base + 1];
	out->blue = blob[base + 2];
}

uint8_t
mionix_blob_get_mode(const uint8_t *blob)
{
	/*
	 * The mode byte is global: the firmware reads the first record's
	 * mode for the whole device and ignores the other five.
	 */
	return blob[colour_offsets[0] + 3];
}

void
mionix_blob_get_resolution(const uint8_t *blob, unsigned int index,
			   uint16_t *x, uint16_t *y)
{
	unsigned int base;

	if (index >= MIONIX_NUM_RESOLUTIONS) {
		*x = *y = 0;
		return;
	}

	base = dpi_offsets[index];
	*x = le16(blob, base);
	*y = le16(blob, base + 2);
}

unsigned int
mionix_blob_get_report_rate(const uint8_t *blob)
{
	switch (blob[MIONIX_OFF_POLLING]) {
	case 1: return 1000;
	case 2: return 500;
	case 4: return 250;
	case 8: return 125;
	default: return 0;
	}
}

void
mionix_blob_get_name(const uint8_t *blob, char *out, size_t outlen)
{
	size_t i;
	size_t n;

	if (outlen == 0)
		return;

	n = outlen - 1;
	if (n > MIONIX_NAME_LEN)
		n = MIONIX_NAME_LEN;

	/* the field is zero-padded, not space-padded */
	for (i = 0; i < n; i++)
		out[i] = (char)blob[MIONIX_OFF_NAME + i];
	out[n] = '\0';
}

uint8_t
mionix_blob_get_active_dpi(const uint8_t *blob)
{
	return blob[MIONIX_OFF_ACTIVE_DPI];
}

uint8_t
mionix_blob_get_angle_snapping(const uint8_t *blob)
{
	return blob[MIONIX_OFF_ANGLE_SNAPPING];
}

void
mionix_blob_get_button(const uint8_t *blob, unsigned int index,
		       struct mionix_button *out)
{
	unsigned int base;

	if (index >= MIONIX_NUM_BUTTONS) {
		out->type = MIONIX_BTN_TYPE_DISABLED;
		out->param = 0;
		return;
	}

	base = MIONIX_OFF_BUTTON_TABLE + index * MIONIX_BUTTON_ENTRY_LEN;
	out->type = blob[base];
	out->param = blob[base + 1];
}

void
mionix_blob_set_led(uint8_t *blob, unsigned int index,
		    const struct mionix_led *led)
{
	unsigned int base;

	if (index >= MIONIX_NUM_LEDS)
		return;

	base = colour_offsets[index];
	blob[base] = led->red;
	blob[base + 1] = led->green;
	blob[base + 2] = led->blue;
}

void
mionix_blob_set_mode(uint8_t *blob, uint8_t mode)
{
	unsigned int i;

	/*
	 * The firmware reads the mode globally from the first record, but
	 * write all six: relying on [42] alone is an assumption about
	 * undocumented firmware.
	 */
	for (i = 0; i < MIONIX_NUM_COLOUR_RECORDS; i++)
		blob[colour_offsets[i] + 3] = mode;
}

void
mionix_blob_set_resolution(uint8_t *blob, unsigned int index,
			   uint16_t x, uint16_t y)
{
	unsigned int base;

	if (index >= MIONIX_NUM_RESOLUTIONS)
		return;

	base = dpi_offsets[index];
	blob[base] = (uint8_t)(x & 0xFF);
	blob[base + 1] = (uint8_t)(x >> 8);
	blob[base + 2] = (uint8_t)(y & 0xFF);
	blob[base + 3] = (uint8_t)(y >> 8);
}

void
mionix_blob_set_report_rate(uint8_t *blob, unsigned int hz)
{
	uint8_t divisor;

	switch (hz) {
	case 1000: divisor = 1; break;
	case 500:  divisor = 2; break;
	case 250:  divisor = 4; break;
	case 125:  divisor = 8; break;
	default:   return;	/* leave the stored value alone */
	}

	blob[MIONIX_OFF_POLLING] = divisor;
}

void
mionix_blob_set_angle_snapping(uint8_t *blob, uint8_t value)
{
	/*
	 * Reject rather than clamp, as mionix_blob_set_report_rate() does for
	 * an unsupported rate: a value the hardware has no level for is a
	 * caller bug, and silently substituting a different level would be
	 * written to flash as if it had been asked for.
	 */
	if (value > MIONIX_ANGLE_SNAPPING_MAX)
		return;		/* leave the stored value alone */

	blob[MIONIX_OFF_ANGLE_SNAPPING] = value;
}

void
mionix_blob_set_button(uint8_t *blob, unsigned int index,
		       const struct mionix_button *btn)
{
	unsigned int base;

	if (index >= MIONIX_NUM_BUTTONS)
		return;

	base = MIONIX_OFF_BUTTON_TABLE + index * MIONIX_BUTTON_ENTRY_LEN;
	blob[base] = btn->type;
	blob[base + 1] = btn->param;
}

void
mionix_blob_prepare_write(uint8_t *blob, uint8_t profile_index)
{
	blob[0] = 0x61;
	blob[MIONIX_OFF_CMD] = 0x08;
	blob[MIONIX_OFF_PROFILE_INDEX] = profile_index;
	/* the full-blob form carries 0 here; the short terminator form carries 1 */
	blob[MIONIX_OFF_TERMINATOR_FLAG] = 0x00;
	blob[MIONIX_OFF_PROFILE_INDEX_DUP] = profile_index;
}
