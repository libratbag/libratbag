/*
 * Copyright © 2026 Quintavalle Pietro
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

#include "driver-hitscan.h"

#include "libratbag-data.h"
#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "shared-macro.h"

/*
 * Transport: every command is a SET_REPORT (Output, Report ID 8) sent
 * to interface 1. Responses arrive as a plain Interrupt IN report on a
 * separate endpoint, not via GET_FEATURE -- this device does not
 * implement GET_FEATURE at all.
 */
enum hitscan_report_id {
	HITSCAN_REPORT_ID_CMD = 0x8,
} __attribute__((packed));
_Static_assert(sizeof(enum hitscan_report_id) == sizeof(uint8_t), "Invalid size");

/*
 * Opcode 0x7 is shared across DPI, report rate, debounce, and button
 * assignment; each is distinguished by the sub-header bytes at
 * offsets 4-5, not by the opcode. See the individual hitscan_set_*()
 * functions for each sub-header value.
 */
enum hitscan_command_id {
	HITSCAN_CMD_FIRMWARE_VERSION = 0x1,
	HITSCAN_CMD_SET_DPI = 0x7,
} __attribute__((packed));
_Static_assert(sizeof(enum hitscan_command_id) == sizeof(uint8_t), "Invalid size");

/* dpi = (raw + 1) * 50, i.e. 50-12800 DPI in steps of 50. */
#define HITSCAN_DPI_STEP 50
#define HITSCAN_DPI_MIN HITSCAN_DPI_STEP
#define HITSCAN_DPI_MAX (256 * HITSCAN_DPI_STEP)

/* Every packet is exactly 17 bytes: report id + opcode + 14 bytes of
 * payload + checksum, regardless of opcode. */
#define HITSCAN_CMD_SIZE 17

/* The firmware-version response carries a constant 3-byte trailer
 * (offsets 10-12, e.g. "32 01 02") identifying model/major/minor,
 * stored here as a 6-char hex string. */
#define HITSCAN_FW_VERSION_LEN 6

/* Checksum: the sum of all 17 bytes (including the checksum byte
 * itself) always equals 0x55 mod 256. The checksum's position within
 * the packet varies by opcode/sub-command. */
#define HITSCAN_CHECKSUM_TARGET 0x55

static uint8_t
hitscan_checksum_byte_for(const uint8_t *buf, size_t len, size_t checksum_index)
{
	uint8_t sum = 0;

	for (size_t i = 0; i < len; i++) {
		if (i == checksum_index)
			continue;
		sum += buf[i];
	}

	return (uint8_t)(HITSCAN_CHECKSUM_TARGET - sum);
}

static bool
hitscan_checksum_is_valid(const uint8_t *buf, size_t len)
{
	uint8_t sum = 0;

	for (size_t i = 0; i < len; i++)
		sum += buf[i];

	return sum == HITSCAN_CHECKSUM_TARGET;
}

struct hitscan_data {
	char fw_version[HITSCAN_FW_VERSION_LEN + 1];
};

/*
 * Fills in the checksum and writes buffer as an Output report (this
 * device does not support SET_FEATURE for these commands).
 *
 * @return 0 on success or a negative errno.
 */
static int
hitscan_query_write(struct ratbag_device *device, uint8_t buffer[], unsigned int buffer_length,
		    size_t checksum_index)
{
	int rc = 0;

	buffer[checksum_index] = hitscan_checksum_byte_for(buffer, buffer_length, checksum_index);

	/*
	 * Unlike the feature-report calls, ratbag_hidraw_output_report()
	 * already validates the write length internally and returns
	 * plain 0 on full success (not the byte count).
	 */
	rc = ratbag_hidraw_output_report(device, buffer, buffer_length);
	if (rc < 0) {
		log_error(device->ratbag, "Error while writing data: %s (%d)\n", strerror(-rc), rc);
		return rc;
	}

	return 0;
}

/*
 * Writes a command via hitscan_query_write(), then reads the matching
 * response back from the interrupt IN endpoint (a plain hidraw
 * read(), not HIDIOCGFEATURE).
 *
 * @return 0 on success or a negative errno.
 */
static int
hitscan_query_read(struct ratbag_device *device, uint8_t buffer[], unsigned int buffer_length)
{
	int rc = 0;

	const uint8_t report_id = buffer[0];
	const uint8_t query_command = buffer[1];

	/* Read-style commands (e.g. firmware version) put the checksum
	 * in the last byte. */
	rc = hitscan_query_write(device, buffer, buffer_length, buffer_length - 1);
	if (rc < 0)
		return rc;

	memset(buffer, 0, buffer_length);

	rc = ratbag_hidraw_read_input_report(device, buffer, buffer_length, NULL);
	if (rc < 0)
		return rc;
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Unexpected amount of received data: %d (instead of %u)\n", rc, buffer_length);
		return -EIO;
	}

	if (!hitscan_checksum_is_valid(buffer, buffer_length)) {
		log_error(device->ratbag, "Invalid checksum in response (last byte %#x)\n", buffer[buffer_length - 1]);
		return -EIO;
	}

	if (buffer[0] != report_id || buffer[1] != query_command) {
		log_error(device->ratbag, "Could not do a read query with command %#x, got response for command %#x instead\n", query_command, buffer[1]);
		return -EIO;
	}

	return 0;
}

static int
hitscan_get_fw_version(struct ratbag_device *device, char out[HITSCAN_FW_VERSION_LEN + 1])
{
	int rc = 0;

	uint8_t buf[HITSCAN_CMD_SIZE] = {
		HITSCAN_REPORT_ID_CMD,		/* byte 0: report id (8)     */
		HITSCAN_CMD_FIRMWARE_VERSION,	/* byte 1: opcode (1)        */
		0x00, 0x00, 0x00,		/* bytes 2-4: unused         */
		0x08,				/* byte 5: constant for this opcode */
		0x00, 0x00, 0x00, 0x00,	/* bytes 6-9: unused in request */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* bytes 10-15: unused in request */
		0x00,				/* byte 16: checksum, filled by write */
	};

	rc = hitscan_query_read(device, buf, sizeof(buf));
	if (rc < 0) {
		log_error(device->ratbag, "Couldn't read firmware version: %s (%d)\n", strerror(-rc), rc);
		return rc;
	}

	snprintf(out, HITSCAN_FW_VERSION_LEN + 1, "%02x%02x%02x", buf[10], buf[11], buf[12]);

	return 0;
}

static int
hitscan_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, HITSCAN_REPORT_ID_CMD);
}

static int
hitscan_probe(struct ratbag_device *device)
{
	int rc = 0;
	struct hitscan_data *drv_data = NULL;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	rc = ratbag_find_hidraw(device, hitscan_test_hidraw);
	if (rc)
		goto err;

	rc = hitscan_get_fw_version(device, drv_data->fw_version);
	if (rc)
		goto err;

	ratbag_device_set_firmware_version(device, drv_data->fw_version);
	log_debug(device->ratbag, "Firmware version: %s\n", drv_data->fw_version);

	{
		_cleanup_profile_ struct ratbag_profile *profile = NULL;
		_cleanup_resolution_ struct ratbag_resolution *resolution = NULL;
		const unsigned int rates[] = { 125, 250, 500, 1000 };
		/* Only 0 and 1ms are verified against real hardware; the
		 * device likely supports more. */
		const unsigned int debounces[] = { 0, 1 };
		unsigned int dpis[256];

		ratbag_device_init_profiles(device,
					    1 /* num_profiles */,
					    1 /* num_resolutions */,
					    5 /* num_buttons */,
					    0 /* num_leds -- device has none */);

		profile = ratbag_device_get_profile(device, 0);
		profile->is_active = true;
		ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));
		profile->hz = 1000;
		ratbag_profile_set_debounce_list(profile, debounces, ARRAY_LENGTH(debounces));
		profile->debounce = 0;

		resolution = ratbag_profile_get_resolution(profile, 0);
		resolution->is_active = true;
		resolution->is_default = true;

		for (unsigned int i = 0; i < ARRAY_LENGTH(dpis); i++)
			dpis[i] = (i + 1) * HITSCAN_DPI_STEP;
		ratbag_resolution_set_dpi_list(resolution, dpis, ARRAY_LENGTH(dpis));
		ratbag_resolution_set_resolution(resolution, 800, 800);

		/* Key remapping/macros are not supported by this driver
		 * yet, so only BUTTON and NONE (disable) are registered. */
		struct ratbag_button *button;
		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
		}
	}

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

/*
 * Sets DPI:
 *   08 07 00 00 0c 04 [raw] [raw] 00 [chk] 00 00 00 00 00 00 e1
 * Byte 16 is a fixed constant for this sub-command; the checksum is
 * at byte 9 instead. The device echoes the packet back as its
 * acknowledgment.
 */
static int
hitscan_set_dpi(struct ratbag_device *device, unsigned int dpi)
{
	int rc = 0;

	if (dpi < HITSCAN_DPI_MIN || dpi > HITSCAN_DPI_MAX || (dpi % HITSCAN_DPI_STEP) != 0) {
		log_error(device->ratbag, "Invalid DPI value %u\n", dpi);
		return -EINVAL;
	}

	uint8_t raw = (uint8_t)((dpi / HITSCAN_DPI_STEP) - 1);
	uint8_t buf[HITSCAN_CMD_SIZE] = {
		HITSCAN_REPORT_ID_CMD, HITSCAN_CMD_SET_DPI,
		0x00, 0x00,
		0x0c, 0x04,
		raw, raw,
		0x00,
		0x00, /* checksum, computed below at index 9 */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xe1, /* fixed trailer for this sub-command */
	};

	rc = hitscan_query_write(device, buf, sizeof(buf), 9);
	if (rc < 0) {
		log_error(device->ratbag, "Couldn't set DPI to %u: %s (%d)\n", dpi, strerror(-rc), rc);
		return rc;
	}

	return 0;
}

/*
 * Sets report rate:
 *   08 07 00 00 00 02 [raw] [chk] 00 00 00 00 00 00 00 00 ef
 * Same opcode as DPI, distinguished by the sub-header bytes 4-5
 * (0x00,0x02 here vs DPI's 0x0c,0x04). raw = 1000/hz.
 */
static int
hitscan_set_report_rate(struct ratbag_device *device, unsigned int hz)
{
	int rc = 0;

	if (hz == 0 || (1000 % hz) != 0) {
		log_error(device->ratbag, "Invalid report rate %u\n", hz);
		return -EINVAL;
	}

	uint8_t raw = (uint8_t)(1000 / hz);
	uint8_t buf[HITSCAN_CMD_SIZE] = {
		HITSCAN_REPORT_ID_CMD, HITSCAN_CMD_SET_DPI, /* opcode 0x07, shared with DPI */
		0x00, 0x00,
		0x00, 0x02, /* sub-header: report rate, vs DPI's 0x0c,0x04 */
		raw,
		0x00, /* checksum, computed below */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0xef, /* fixed trailer for this sub-command */
	};

	rc = hitscan_query_write(device, buf, sizeof(buf), 7);
	if (rc < 0) {
		log_error(device->ratbag, "Couldn't set report rate to %u: %s (%d)\n", hz, strerror(-rc), rc);
		return rc;
	}

	return 0;
}

/*
 * Sets debounce time:
 *   08 07 00 00 a9 02 [ms] [chk] 00 00 00 00 00 00 00 00 46
 * Same opcode family as report rate (byte 5 = 0x02), distinguished by
 * the byte 4 sub-identifier (0xa9 vs rate's 0x00). The value is the
 * debounce time in milliseconds directly, no scaling. Only 0 and 1ms
 * have been tested against real hardware.
 */
static int
hitscan_set_debounce(struct ratbag_device *device, unsigned int ms)
{
	int rc = 0;

	if (ms > 255) {
		log_error(device->ratbag, "Invalid debounce time %u\n", ms);
		return -EINVAL;
	}

	uint8_t buf[HITSCAN_CMD_SIZE] = {
		HITSCAN_REPORT_ID_CMD, HITSCAN_CMD_SET_DPI, /* opcode 0x07 */
		0x00, 0x00,
		0xa9, 0x02, /* sub-header: debounce */
		(uint8_t)ms,
		0x00, /* checksum, computed below */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x46, /* fixed trailer for this sub-command */
	};

	rc = hitscan_query_write(device, buf, sizeof(buf), 7);
	if (rc < 0) {
		log_error(device->ratbag, "Couldn't set debounce time to %ums: %s (%d)\n", ms, strerror(-rc), rc);
		return rc;
	}

	return 0;
}

/*
 * Action codes for plain mouse-button assignment: left=0x60,
 * right=0x64, middle=0x68, back=0x70, forward=0x6c. Not sequential by
 * ratbag button index (4 and 5 are swapped relative to a linear
 * formula), hence a lookup table rather than arithmetic.
 */
static const uint8_t hitscan_button_action_codes[] = {
	[1] = 0x60, /* left */
	[2] = 0x64, /* right */
	[3] = 0x68, /* middle */
	[4] = 0x70, /* back */
	[5] = 0x6c, /* forward */
};

/*
 * Sets a button's action:
 *   08 07 00 00 [action] 04 [enabled] [bitmask] 00 [derived] 00 00 00 00 00 00 [chk]
 * where derived = 0x55 - enabled - bitmask. bitmask is the button's
 * bit position (1 << index); enabled is 1 for an assigned action, 0
 * for disabled.
 */
static int
hitscan_set_button(struct ratbag_device *device, unsigned int button_index,
		   const struct ratbag_button_action *action)
{
	int rc = 0;
	uint8_t bitmask = (uint8_t)(1u << button_index);
	uint8_t enabled = 1;
	uint8_t action_code = 0;

	if (action->type == RATBAG_BUTTON_ACTION_TYPE_NONE) {
		enabled = 0;
	} else if (action->type == RATBAG_BUTTON_ACTION_TYPE_BUTTON &&
		   action->action.button >= 1 && action->action.button <= 5) {
		action_code = hitscan_button_action_codes[action->action.button];
	} else {
		log_error(device->ratbag, "Unsupported button action type %d for button %u\n",
			  action->type, button_index);
		return -ENOTSUP;
	}

	uint8_t derived = (uint8_t)(HITSCAN_CHECKSUM_TARGET - enabled - bitmask);
	uint8_t buf[HITSCAN_CMD_SIZE] = {
		HITSCAN_REPORT_ID_CMD, HITSCAN_CMD_SET_DPI, /* opcode 0x07 */
		0x00, 0x00,
		action_code, 0x04, /* sub-header: button assignment */
		enabled,
		bitmask,
		0x00,
		derived,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, /* checksum, computed below */
	};

	rc = hitscan_query_write(device, buf, sizeof(buf), sizeof(buf) - 1);
	if (rc < 0) {
		log_error(device->ratbag, "Couldn't set button %u: %s (%d)\n", button_index, strerror(-rc), rc);
		return rc;
	}

	return 0;
}

/*
 * Signals the device to persist pending changes to non-volatile
 * memory; without this, changes apply live but do not survive a power
 * cycle.
 */
static int
hitscan_commit_pending(struct ratbag_device *device)
{
	uint8_t buf[HITSCAN_CMD_SIZE] = {
		HITSCAN_REPORT_ID_CMD, 0x02,
		0x00, 0x00, 0x00,
		0x01, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, /* checksum, computed below */
	};

	return hitscan_query_write(device, buf, sizeof(buf), sizeof(buf) - 1);
}

static int
hitscan_commit(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_profile *profile;
	bool wrote_something = false;

	ratbag_device_for_each_profile(device, profile) {
		struct ratbag_resolution *resolution;
		struct ratbag_button *button;

		ratbag_profile_for_each_resolution(profile, resolution) {
			if (!resolution->dirty)
				continue;

			rc = hitscan_set_dpi(device, resolution->dpi_x);
			if (rc < 0)
				return rc;
			wrote_something = true;
		}

		if (profile->rate_dirty) {
			rc = hitscan_set_report_rate(device, profile->hz);
			if (rc < 0)
				return rc;
			wrote_something = true;
		}

		if (profile->debounce_dirty) {
			rc = hitscan_set_debounce(device, (unsigned int)profile->debounce);
			if (rc < 0)
				return rc;
			wrote_something = true;
		}

		ratbag_profile_for_each_button(profile, button) {
			if (!button->dirty)
				continue;

			rc = hitscan_set_button(device, button->index, &button->action);
			if (rc < 0)
				return rc;
			wrote_something = true;
		}
	}

	if (wrote_something) {
		rc = hitscan_commit_pending(device);
		if (rc < 0) {
			log_error(device->ratbag, "Couldn't send commit signal: %s (%d)\n", strerror(-rc), rc);
			return rc;
		}
	}

	return 0;
}

static void
hitscan_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver hitscan_driver = {
	.name = "Hitscan",
	.id = "hitscan",
	.probe = hitscan_probe,
	.remove = hitscan_remove,
	.commit = hitscan_commit,
};
