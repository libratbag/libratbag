/*
 * Copyright © 2026 Kaloian Kozlev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "config.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-enums.h"
#include "libratbag-hidraw.h"
#include "libratbag-private.h"
#include "libratbag-util.h"

/*
 * Fury does not publish the Tanto T4 protocol. These report IDs and packet
 * layouts were determined by recording the official Windows application while
 * changing one setting at a time:
 *
 *   0x04 - DPI stages and the currently active stage
 *   0x06 - USB polling rate
 *   0x08 - programmable button assignments
 *
 * The mouse advertises these as HID feature reports. Sending a feature report
 * stores a complete settings block, rather than changing only one field.
 */
#define TANTO_REPORT_DPI 0x04
#define TANTO_REPORT_RATE 0x06
#define TANTO_REPORT_BUTTONS 0x08
#define TANTO_NUM_RESOLUTIONS 7
#define TANTO_NUM_BUTTONS 6

/*
 * The device does not return its current configuration on Linux: GET_FEATURE
 * times out. We therefore keep local copies of the last settings known to this
 * driver and modify those copies before sending complete reports to the mouse.
 */
struct tanto_data {
	uint8_t dpi[52];
	uint8_t buttons[59];
};

/* Values exposed to ratbagctl/Piper. */
static const unsigned int tanto_rates[] = { 125, 250, 500, 1000 };
static const unsigned int tanto_default_dpi[] = {
	800, 1600, 2400, 3200, 4800, 7200, 12000,
};

/*
 * Report 0x08 contains eighteen three-byte entries even though the mouse has
 * only six configurable physical buttons. A synchronized video/USB capture
 * showed that UI buttons 1..6 map to protocol entries 1,2,3,7,8,4. The array
 * is zero-based because it is used as an offset into the C report buffer.
 */
static const uint8_t tanto_button_entries[] = { 0, 1, 2, 6, 7, 3 };

/*
 * Complete default reports captured from the official software. Some bytes
 * are still unknown, so the driver starts with these safe templates and only
 * replaces fields whose meanings have been confirmed. This prevents a DPI or
 * button change from accidentally zeroing unrelated onboard settings.
 */
static const uint8_t tanto_default_dpi_report[52] = {
	0x04, 0x38, 0x01, 0x00, 0x01, 0x7f, 0x00, 0x00,
	0x12, 0x25, 0x38, 0x4b, 0x70, 0xa9, 0x8d, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
	0x02, 0xff, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00,
	0x00, 0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00,
	0xff, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	0xff, 0x01, 0x11, 0xd5,
};

static const uint8_t tanto_default_button_report[59] = {
	0x08, 0x3b, 0x01, 0x02, 0x00, 0x00, 0x03, 0x00,
	0x00, 0x04, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x3c,
	0x00, 0x00, 0x0d, 0x00, 0x00, 0x06, 0x00, 0x00,
	0x05, 0x00, 0x00, 0x3c, 0x00, 0x00, 0x01, 0x00,
	0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01,
	0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
	0x01, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x09, 0x00,
	0x00, 0x00, 0xc0,
};

/*
 * The PAW3311 sensor does not store the displayed DPI value directly. The
 * official application translates each supported DPI value into this one-byte
 * sensor code. The table was recovered from the vendor executable and checked
 * against USB captures for 800/850 and 1600/1650 DPI.
 */
static const uint8_t tanto_dpi_codes[220] = {
	1,2,3,4,5,6,8,9,10,11,12,14,15,16,17,18,19,21,22,23,
	24,25,27,28,29,30,31,32,34,35,36,37,38,39,41,42,43,44,45,47,
	48,49,50,51,52,54,55,56,57,58,59,61,62,63,64,65,67,68,69,70,
	71,72,74,75,76,77,78,79,81,82,83,84,85,87,88,89,90,91,92,94,
	95,96,97,98,99,101,102,103,104,105,107,108,109,110,111,112,114,115,116,117,
	118,119,121,122,123,124,125,127,128,129,130,131,132,134,135,136,137,138,139,141,
	142,143,144,145,147,148,149,150,151,152,154,155,156,157,158,159,161,162,163,164,
	165,167,168,169,170,171,172,174,175,176,177,178,179,181,182,183,184,185,187,188,
	189,190,191,192,194,195,196,197,198,199,201,202,203,204,205,207,208,209,210,211,
	212,214,215,216,217,218,219,221,222,223,224,225,227,228,229,230,231,232,234,235,
	118,119,121,122,123,124,125,127,128,129,130,131,132,134,135,136,137,138,139,141,
};

static void
tanto_checksum(uint8_t *report, size_t checksum_offset)
{
	unsigned int sum = 0;

	/*
	 * Reports 0x04 and 0x08 end with a two-byte, big-endian sum. The fixed
	 * three-byte header is excluded, matching the official application.
	 */
	for (size_t i = 3; i < checksum_offset; i++)
		sum += report[i];
	report[checksum_offset] = (sum >> 8) & 0xff;
	report[checksum_offset + 1] = sum & 0xff;
}

static int
tanto_test_hidraw(struct ratbag_device *device)
{
	/*
	 * A Tanto appears as several /dev/hidraw nodes. Select the configuration
	 * interface by requiring all three reports used by this backend.
	 */
	return ratbag_hidraw_has_report(device, TANTO_REPORT_DPI) &&
	       ratbag_hidraw_has_report(device, TANTO_REPORT_RATE) &&
	       ratbag_hidraw_has_report(device, TANTO_REPORT_BUTTONS);
}

static int
tanto_set_feature(struct ratbag_device *device, uint8_t *report, size_t size)
{
	/* report[0] is both the HID report ID and the first transmitted byte. */
	int rc = ratbag_hidraw_set_feature_report(device, report[0], report, size);

	/* A short transfer is not success: the mouse requires the whole report. */
	if (rc < 0)
		return rc;
	return rc == (int)size ? 0 : -EIO;
}

static int
tanto_encode_dpi(unsigned int dpi, uint8_t *code, uint8_t *high_range)
{
	unsigned int index;

	/* The UI uses 50-DPI steps through 10000 and 100-DPI steps above it. */
	if (dpi < 200 || dpi > 12000)
		return -EINVAL;
	if (dpi <= 10000) {
		if (dpi % 50)
			return -EINVAL;
		index = dpi / 50;
	} else {
		if (dpi % 100)
			return -EINVAL;
		index = dpi / 100 + 100;
	}
	/* Values above 10000 also require a separate per-stage flag. */
	*code = tanto_dpi_codes[index - 1];
	*high_range = index > 200;
	return 0;
}

static void
tanto_init_button(struct ratbag_button *button, unsigned int index)
{
	/*
	 * Since settings cannot be read back, expose the known factory assignments.
	 * Both side buttons map to mouse buttons 5 and 4 respectively.
	 */
	static const struct ratbag_button_action defaults[] = {
		{ .type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 1 },
		{ .type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 2 },
		{ .type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 3 },
		{ .type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 5 },
		{ .type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 4 },
		{ .type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
		  .action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP },
	};

	/* Limit clients to the action families this backend can safely encode. */
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_set_action(button, &defaults[index]);
}

static int
tanto_probe(struct ratbag_device *device)
{
	struct tanto_data *data;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	int rc;

	/* Find and open the one HID interface that carries configuration reports. */
	rc = ratbag_find_hidraw(device, tanto_test_hidraw);
	if (rc)
		return rc;

	/* Initialize the local report images used for later read-modify-write. */
	data = zalloc(sizeof(*data));
	memcpy(data->dpi, tanto_default_dpi_report, sizeof(data->dpi));
	memcpy(data->buttons, tanto_default_button_report, sizeof(data->buttons));
	ratbag_set_drv_data(device, data);

	/* The vendor UI exposes one profile, seven DPI stages and six buttons. */
	ratbag_device_init_profiles(device, 1, TANTO_NUM_RESOLUTIONS,
				    TANTO_NUM_BUTTONS, 0);
	profile = ratbag_device_get_profile(device, 0);
	profile->is_active = true;
	profile->is_enabled = true;
	profile->hz = 1000;
	/* Be explicit that displayed values are defaults, not device readback. */
	ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
	ratbag_profile_set_report_rate_list(profile, tanto_rates,
					    ARRAY_LENGTH(tanto_rates));

	ratbag_profile_for_each_resolution(profile, resolution) {
		resolution->dpi_x = resolution->dpi_y =
			tanto_default_dpi[resolution->index];
		/* The captured factory profile used stage 2 (1600 DPI). */
		resolution->is_active = resolution->index == 1;
		resolution->is_default = resolution->index == 1;
		ratbag_resolution_set_dpi_list_from_range(resolution, 200, 12000);
		ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);
	}

	ratbag_profile_for_each_button(profile, button)
		tanto_init_button(button, button->index);

	return 0;
}

static int
tanto_write_rate(struct ratbag_device *device, unsigned int hz)
{
	uint8_t interval;
	uint8_t report[] = { 0x06, 0x09, 0x01, 0, 0, 0, 0, 0, 0 };

	if (hz != 125 && hz != 250 && hz != 500 && hz != 1000)
		return -EINVAL;
	/*
	 * The packet stores the report interval in milliseconds and its one's
	 * complement. Example: 500 Hz -> interval 2 -> bytes 02 fd.
	 */
	interval = 1000 / hz;
	report[3] = interval;
	report[4] = interval ^ 0xff;
	return tanto_set_feature(device, report, sizeof(report));
}

static int
tanto_write_dpi(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct tanto_data *data = ratbag_get_drv_data(device);
	struct ratbag_resolution *resolution;
	uint8_t enabled = 0;
	unsigned int active = 0;
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		uint8_t code, high_range;

		rc = tanto_encode_dpi(resolution->dpi_x, &code, &high_range);
		if (rc)
			return rc;
		/* The hardware supports one DPI value per stage, not separate X/Y. */
		if (resolution->dpi_x != resolution->dpi_y)
			return -EINVAL;
		data->dpi[8 + resolution->index] = code;
		data->dpi[16 + resolution->index] = high_range;
		/* Bit N in byte 5 enables or disables DPI stage N. */
		if (!resolution->is_disabled)
			enabled |= 1 << resolution->index;
		/* The active stage is stored later as a one-based number. */
		if (resolution->is_active)
			active = resolution->index;
	}
	/* Reject an unusable profile or an active stage that was disabled. */
	if (!enabled || !(enabled & (1 << active)))
		return -EINVAL;
	data->dpi[5] = enabled;
	data->dpi[24] = active + 1;
	tanto_checksum(data->dpi, 50);
	return tanto_set_feature(device, data->dpi, sizeof(data->dpi));
}

static int
tanto_encode_button(const struct ratbag_button_action *action, uint8_t out[3])
{
	/* Every programmable-button action occupies exactly three bytes. */
	memset(out, 0, 3);
	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		out[0] = 0x01;
		return 0;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		/* Standard mouse buttons 1..5 are encoded as values 2..6. */
		if (action->action.button < 1 || action->action.button > 5)
			return -ENOTSUP;
		out[0] = action->action.button + 1;
		return 0;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		/* 0x0d is "DPI in cycle" in the Fury application. */
		if (action->action.special == RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) {
			out[0] = 0x0d;
			return 0;
		}
		return -ENOTSUP;
	default:
		return -ENOTSUP;
	}
}

static int
tanto_write_buttons(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct tanto_data *data = ratbag_get_drv_data(device);
	struct ratbag_button *button;
	bool dirty = false;

	ratbag_profile_for_each_button(profile, button) {
		uint8_t encoded[3];
		unsigned int offset;
		int rc;

		/* Preserve unchanged physical and unknown protocol entries. */
		if (!button->dirty)
			continue;
		rc = tanto_encode_button(&button->action, encoded);
		if (rc)
			return rc;
		offset = 3 + tanto_button_entries[button->index] * 3;
		memcpy(&data->buttons[offset], encoded, sizeof(encoded));
		dirty = true;
	}
	/* Avoid sending a button report when only another setting changed. */
	if (!dirty)
		return 0;
	tanto_checksum(data->buttons, 57);
	return tanto_set_feature(device, data->buttons, sizeof(data->buttons));
}

static int
tanto_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile = ratbag_device_get_profile(device, 0);
	struct ratbag_resolution *resolution;
	bool dpi_dirty = false;
	int rc;

	/* libratbag calls commit after the client marks profile data as changed. */
	if (!profile->dirty)
		return 0;

	/* Polling rate has its own small report and is safe to send independently. */
	rc = tanto_write_rate(device, profile->hz);
	if (rc)
		return rc;
	ratbag_profile_for_each_resolution(profile, resolution)
		dpi_dirty |= resolution->dirty;
	/* DPI and button reports are sent only when their fields were edited. */
	if (dpi_dirty) {
		rc = tanto_write_dpi(device, profile);
		if (rc)
			return rc;
	}
	return tanto_write_buttons(device, profile);
}

static void
tanto_remove(struct ratbag_device *device)
{
	/* Release both the driver's private report images and the hidraw handle. */
	free(ratbag_get_drv_data(device));
	ratbag_set_drv_data(device, NULL);
	ratbag_close_hidraw(device);
}

struct ratbag_driver tanto_driver = {
	/* Registration record used by libratbag's generic driver dispatcher. */
	.name = "Fury Tanto T4",
	.id = "tanto",
	.probe = tanto_probe,
	.remove = tanto_remove,
	.commit = tanto_commit,
};
