/*
 * Copyright © 2024-2026 Anten Skrabec
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

#include "config.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

/* Report IDs for the receiver interface (usage page 0xFFC1) */
#define LEMOKEY_REPORT_ID_LONG_OUT	0xB3	/* 63 bytes data (64 with report ID) */
#define LEMOKEY_REPORT_ID_LONG_IN	0xB4	/* 63 bytes data */
#define LEMOKEY_REPORT_ID_SHORT_OUT	0xB5	/* 20 bytes data (21 with report ID) */
#define LEMOKEY_REPORT_ID_SHORT_IN	0xB6	/* 20 bytes data */

#define LEMOKEY_LONG_REPORT_SIZE	64	/* including report ID byte */
#define LEMOKEY_SHORT_REPORT_SIZE	21	/* including report ID byte */

/* Receiver commands (protocol reverse-engineered from launcher.lemokey.com) */
#define LEMOKEY_CMD_CONN_INFO		0x02
#define LEMOKEY_CMD_STATUS		0x03
#define LEMOKEY_CMD_VERSION		0x04
#define LEMOKEY_CMD_DEVICE_STATE	0x06
#define LEMOKEY_CMD_SET_DPI		0x40
#define LEMOKEY_CMD_SET_RATE		0x41
#define LEMOKEY_CMD_SET_FEATURES	0x42
#define LEMOKEY_CMD_SET_DEBOUNCE	0x43

/* Offsets in the 0x06 (DEVICE_STATE) response, including report ID at byte 0 */
#define LEMOKEY_STATE_RATE_DPI		3	/* high nibble = rate level, low = DPI level */
#define LEMOKEY_STATE_DPI_X_BASE	6	/* 5 × LE-16 DPI X values at [6..15] */
/*
 * Bit layout of the features byte at offset 16:
 *   bits 0-1: LOD (lift-off distance)
 *   bit 2:    angle snapping
 *   bit 3:    line (wave/ripple)
 *   bit 4:    motion sync
 *   bit 6:    scroll direction
 */
#define LEMOKEY_STATE_FEATURES		16
#define LEMOKEY_STATE_DPI_COUNT		17	/* number of DPI levels */
#define LEMOKEY_STATE_DEBOUNCE		18

/* ACK marker in response byte 1 */
#define LEMOKEY_ACK			0xE4

/* Usage page for the receiver/mouse config interface */
#define LEMOKEY_USAGE_PAGE		0xFFC1

#define LEMOKEY_MAX_DPI_LEVELS		5

#define LEMOKEY_DPI_MIN			50
#define LEMOKEY_DPI_MAX			30000

struct lemokey_data {
	uint8_t rate_level;	/* 0=8000, 1=4000, 2=2000, 3=1000, 4=500, 5=250 */
	uint8_t dpi_level;	/* current DPI level index */
	uint8_t debounce;	/* debounce time in ms */
	uint8_t angle_snapping;	/* 0=off, nonzero=on */
	uint8_t dpi_count;
	uint16_t dpi_x[LEMOKEY_MAX_DPI_LEVELS];
	uint16_t dpi_y[LEMOKEY_MAX_DPI_LEVELS];
};

static unsigned int
lemokey_level_to_hz(uint8_t level)
{
	if (level > 5)
		return 250;
	return 8000 / (1 << level);
}

static uint8_t
lemokey_hz_to_level(unsigned int hz)
{
	for (uint8_t level = 0; level <= 5; level++) {
		if (lemokey_level_to_hz(level) <= hz)
			return level;
	}
	return 5;
}

static bool
lemokey_filter_long_response(uint8_t *buf, size_t len)
{
	return len >= 2 && buf[0] == LEMOKEY_REPORT_ID_LONG_IN;
}

static int
lemokey_send_long(struct ratbag_device *device,
		  uint8_t cmd, uint8_t param,
		  uint8_t *response, size_t response_len)
{
	int rc;
	uint8_t buf[LEMOKEY_LONG_REPORT_SIZE];

	memset(buf, 0, sizeof(buf));
	buf[0] = LEMOKEY_REPORT_ID_LONG_OUT;
	buf[1] = cmd;
	buf[2] = param;

	rc = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	if (rc < 0) {
		log_error(device->ratbag,
			  "Failed to send cmd 0x%02x: %s\n",
			  cmd, strerror(-rc));
		return rc;
	}

	if (!response)
		return 0;

	rc = ratbag_hidraw_read_input_report(device, response, response_len,
					     lemokey_filter_long_response);
	if (rc < 0) {
		log_error(device->ratbag,
			  "Failed to read response for cmd 0x%02x: %s\n",
			  cmd, strerror(-rc));
		return rc;
	}

	if (response[1] != cmd) {
		log_error(device->ratbag,
			  "Unexpected response: expected cmd 0x%02x, got 0x%02x\n",
			  cmd, response[1]);
		return -EPROTO;
	}

	return 0;
}

static int
lemokey_read_device_state(struct ratbag_device *device)
{
	struct lemokey_data *drv_data = ratbag_get_drv_data(device);
	uint8_t resp[LEMOKEY_LONG_REPORT_SIZE] = {0};
	int rc;

	rc = lemokey_send_long(device, LEMOKEY_CMD_DEVICE_STATE, 0x00,
			       resp, sizeof(resp));
	if (rc)
		return rc;

	drv_data->rate_level = (resp[LEMOKEY_STATE_RATE_DPI] >> 4) & 0x0F;
	if (drv_data->rate_level > 5)
		drv_data->rate_level = 5;

	drv_data->dpi_level = resp[LEMOKEY_STATE_RATE_DPI] & 0x0F;
	drv_data->debounce = resp[LEMOKEY_STATE_DEBOUNCE];
	drv_data->angle_snapping = resp[LEMOKEY_STATE_FEATURES] & 0x04 ? 1 : 0;

	drv_data->dpi_count = resp[LEMOKEY_STATE_DPI_COUNT];
	if (drv_data->dpi_count > LEMOKEY_MAX_DPI_LEVELS)
		drv_data->dpi_count = LEMOKEY_MAX_DPI_LEVELS;
	if (drv_data->dpi_count == 0)
		drv_data->dpi_count = 1;

	if (drv_data->dpi_level >= drv_data->dpi_count)
		drv_data->dpi_level = 0;

	for (unsigned int i = 0; i < drv_data->dpi_count; i++) {
		unsigned int off = LEMOKEY_STATE_DPI_X_BASE + i * 2;
		drv_data->dpi_x[i] = resp[off] | (resp[off + 1] << 8);
		drv_data->dpi_y[i] = drv_data->dpi_x[i];
	}

	/* Ensure at least one DPI level has a sane value */
	if (drv_data->dpi_x[0] == 0) {
		drv_data->dpi_x[0] = 800;
		drv_data->dpi_y[0] = 800;
	}

	log_debug(device->ratbag,
		  "Device state: rate=%d(%dHz) dpi_level=%d dpi_count=%d "
		  "dpi=[%d %d %d %d %d] debounce=%d angle=%d\n",
		  drv_data->rate_level,
		  lemokey_level_to_hz(drv_data->rate_level),
		  drv_data->dpi_level, drv_data->dpi_count,
		  drv_data->dpi_x[0], drv_data->dpi_x[1],
		  drv_data->dpi_x[2], drv_data->dpi_x[3],
		  drv_data->dpi_x[4],
		  drv_data->debounce,
		  drv_data->angle_snapping);

	return 0;
}

static int
lemokey_read_version(struct ratbag_device *device)
{
	uint8_t resp[LEMOKEY_LONG_REPORT_SIZE] = {0};
	int rc;

	rc = lemokey_send_long(device, LEMOKEY_CMD_VERSION, 0x01,
			       resp, sizeof(resp));
	if (rc)
		return rc;

	unsigned int ver_len = resp[2];
	if (ver_len == 0 || ver_len > 20)
		return 0;

	char version[21];
	memcpy(version, resp + 3, ver_len);
	version[ver_len] = '\0';

	ratbag_device_set_firmware_version(device, version);
	log_debug(device->ratbag, "Firmware version: %s\n", version);

	return 0;
}

static bool
lemokey_filter_short_response(uint8_t *buf, size_t len)
{
	return len >= 2 && buf[0] == LEMOKEY_REPORT_ID_SHORT_IN;
}

static int
lemokey_send_short(struct ratbag_device *device,
		   const uint8_t *data, size_t data_len,
		   uint8_t *response, size_t response_len)
{
	int rc;
	uint8_t buf[LEMOKEY_SHORT_REPORT_SIZE];
	size_t copy_len = min(data_len, sizeof(buf) - 1);

	memset(buf, 0, sizeof(buf));
	buf[0] = LEMOKEY_REPORT_ID_SHORT_OUT;
	memcpy(buf + 1, data, copy_len);

	rc = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	if (rc < 0) {
		log_error(device->ratbag,
			  "Failed to send short cmd 0x%02x: %s\n",
			  data[0], strerror(-rc));
		return rc;
	}

	if (!response)
		return 0;

	rc = ratbag_hidraw_read_input_report(device, response, response_len,
					     lemokey_filter_short_response);
	if (rc < 0) {
		log_error(device->ratbag,
			  "Failed to read short response: %s\n",
			  strerror(-rc));
		return rc;
	}

	return 0;
}

static int
lemokey_check_connection(struct ratbag_device *device)
{
	uint8_t data[20];
	uint8_t resp[LEMOKEY_SHORT_REPORT_SIZE] = {0};
	int rc;

	memset(data, 0, sizeof(data));
	data[0] = LEMOKEY_CMD_STATUS;

	rc = lemokey_send_short(device, data, sizeof(data), resp, sizeof(resp));
	if (rc)
		return rc;

	if (resp[1] != LEMOKEY_CMD_STATUS || resp[2] != 0x01) {
		log_error(device->ratbag,
			  "Mouse not connected (status: 0x%02x)\n", resp[2]);
		return -ENODEV;
	}

	log_debug(device->ratbag, "Mouse connected\n");
	return 0;
}

static int
lemokey_write_rate(struct ratbag_device *device, uint8_t level)
{
	uint8_t data[20];
	uint8_t resp[LEMOKEY_SHORT_REPORT_SIZE] = {0};
	int rc;

	memset(data, 0, sizeof(data));
	data[0] = LEMOKEY_CMD_SET_RATE;
	data[1] = level;
	data[2] = level;
	/* data[3..9]: rate gear index table, sent as-is to the device */
	data[3] = 0x00;
	data[4] = 0x01;
	data[5] = 0x02;
	data[6] = 0x03;
	data[7] = 0x04;
	data[8] = 0x05;
	data[9] = 0x06;

	rc = lemokey_send_short(device, data, sizeof(data), resp, sizeof(resp));
	if (rc)
		return rc;

	if (resp[1] != LEMOKEY_ACK || resp[3] != LEMOKEY_CMD_SET_RATE) {
		log_error(device->ratbag,
			  "Rate change not acknowledged: resp[1]=0x%02x resp[3]=0x%02x\n",
			  resp[1], resp[3]);
		return -EPROTO;
	}

	log_debug(device->ratbag,
		  "Polling rate set to level %d (%d Hz)\n",
		  level, lemokey_level_to_hz(level));

	return 0;
}

static int
lemokey_write_angle_snapping(struct ratbag_device *device, int enable)
{
	uint8_t data[20];
	uint8_t resp[LEMOKEY_SHORT_REPORT_SIZE] = {0};
	int rc;

	memset(data, 0, sizeof(data));
	data[0] = LEMOKEY_CMD_SET_FEATURES;
	data[9] = enable ? 2 : 0;

	rc = lemokey_send_short(device, data, sizeof(data), resp, sizeof(resp));
	if (rc)
		return rc;

	if (resp[1] != LEMOKEY_ACK) {
		log_error(device->ratbag,
			  "Angle snapping change not acknowledged\n");
		return -EPROTO;
	}

	return 0;
}

static int
lemokey_write_debounce(struct ratbag_device *device, int debounce_ms)
{
	uint8_t data[20];
	uint8_t resp[LEMOKEY_SHORT_REPORT_SIZE] = {0};
	int rc;

	memset(data, 0, sizeof(data));
	data[0] = LEMOKEY_CMD_SET_DEBOUNCE;
	data[1] = debounce_ms & 0xFF;

	rc = lemokey_send_short(device, data, sizeof(data), resp, sizeof(resp));
	if (rc)
		return rc;

	if (resp[1] != LEMOKEY_ACK) {
		log_error(device->ratbag,
			  "Debounce change not acknowledged\n");
		return -EPROTO;
	}

	return 0;
}

static int
lemokey_write_dpi(struct ratbag_device *device,
		  struct ratbag_profile *profile)
{
	struct lemokey_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *resolution;
	uint8_t data[20];
	uint8_t resp[LEMOKEY_SHORT_REPORT_SIZE] = {0};
	uint8_t active_level = 0;
	uint8_t count = 0;
	uint16_t packed_dpi[LEMOKEY_MAX_DPI_LEVELS] = {0};
	int rc;

	memset(data, 0, sizeof(data));
	data[0] = LEMOKEY_CMD_SET_DPI;

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (count >= LEMOKEY_MAX_DPI_LEVELS)
			break;
		if (resolution->dpi_x == 0)
			continue;

		unsigned int off = 4 + count * 2;
		data[off] = resolution->dpi_x & 0xFF;
		data[off + 1] = (resolution->dpi_x >> 8) & 0xFF;
		packed_dpi[count] = resolution->dpi_x;

		if (resolution->is_active)
			active_level = count;
		count++;
	}

	if (count == 0) {
		log_error(device->ratbag, "No valid DPI levels to write\n");
		return -EINVAL;
	}

	data[1] = active_level;
	data[2] = active_level;
	data[3] = active_level;
	data[14] = count;

	rc = lemokey_send_short(device, data, sizeof(data), resp, sizeof(resp));
	if (rc)
		return rc;

	if (resp[1] != LEMOKEY_ACK) {
		log_error(device->ratbag,
			  "DPI change not acknowledged: resp[1]=0x%02x\n",
			  resp[1]);
		return -EPROTO;
	}

	drv_data->dpi_level = active_level;
	drv_data->dpi_count = count;
	for (unsigned int i = 0; i < count; i++) {
		drv_data->dpi_x[i] = packed_dpi[i];
		drv_data->dpi_y[i] = packed_dpi[i];
	}

	log_debug(device->ratbag,
		  "DPI set: active=%d count=%d\n", active_level, count);

	return 0;
}

static int
lemokey_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_get_usage_page(device, LEMOKEY_REPORT_ID_LONG_OUT) == LEMOKEY_USAGE_PAGE;
}

static int
lemokey_probe(struct ratbag_device *device)
{
	struct lemokey_data *drv_data;
	int rc;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	rc = ratbag_find_hidraw(device, lemokey_test_hidraw);
	if (rc)
		goto err;

	rc = lemokey_check_connection(device);
	if (rc) {
		log_error(device->ratbag,
			  "Mouse not connected to receiver\n");
		goto err_hidraw;
	}

	lemokey_read_version(device);

	rc = lemokey_read_device_state(device);
	if (rc) {
		log_error(device->ratbag,
			  "Failed to read device state: %s\n", strerror(-rc));
		goto err_hidraw;
	}

	unsigned int num_res = drv_data->dpi_count;

	ratbag_device_init_profiles(device,
				    1,
				    num_res,
				    0, /* buttons: not yet implemented */
				    0); /* LEDs: not yet implemented */

	struct ratbag_profile *profile;
	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		unsigned int rates[] = { 250, 500, 1000, 2000, 4000, 8000 };
		ratbag_profile_set_report_rate_list(profile, rates,
						    ARRAY_LENGTH(rates));
		profile->hz = lemokey_level_to_hz(drv_data->rate_level);

		profile->angle_snapping = drv_data->angle_snapping;

		unsigned int debounces[] = { 0, 1, 2, 4, 6, 8, 10, 12 };
		ratbag_profile_set_debounce_list(profile, debounces,
						 ARRAY_LENGTH(debounces));
		profile->debounce = drv_data->debounce;

		struct ratbag_resolution *resolution;
		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list_from_range(resolution,
								  LEMOKEY_DPI_MIN,
								  LEMOKEY_DPI_MAX);

			if (resolution->index < drv_data->dpi_count) {
				ratbag_resolution_set_resolution(resolution,
								 drv_data->dpi_x[resolution->index],
								 drv_data->dpi_x[resolution->index]);
				resolution->is_active =
					(resolution->index == drv_data->dpi_level);
				resolution->is_default = resolution->is_active;
			}
		}
	}

	return 0;

err_hidraw:
	ratbag_close_hidraw(device);
err:
	ratbag_set_drv_data(device, NULL);
	free(drv_data);
	return rc;
}

static void
lemokey_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

static int
lemokey_commit(struct ratbag_device *device)
{
	struct lemokey_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_profile *profile;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		if (profile->rate_dirty) {
			uint8_t new_level = lemokey_hz_to_level(profile->hz);

			if (new_level != drv_data->rate_level) {
				rc = lemokey_write_rate(device, new_level);
				if (rc)
					return rc;
				drv_data->rate_level = new_level;
			}
		}

		if (profile->angle_snapping_dirty) {
			rc = lemokey_write_angle_snapping(device,
							  profile->angle_snapping);
			if (rc)
				return rc;
			drv_data->angle_snapping = profile->angle_snapping;
		}

		if (profile->debounce_dirty) {
			rc = lemokey_write_debounce(device, profile->debounce);
			if (rc)
				return rc;
			drv_data->debounce = profile->debounce;
		}

		{
			struct ratbag_resolution *resolution;
			bool dpi_dirty = false;

			ratbag_profile_for_each_resolution(profile, resolution) {
				if (resolution->dirty) {
					dpi_dirty = true;
					break;
				}
			}

			if (dpi_dirty) {
				rc = lemokey_write_dpi(device, profile);
				if (rc)
					return rc;
			}
		}
	}

	return 0;
}

struct ratbag_driver lemokey_driver = {
	.name = "Lemokey",
	.id = "lemokey",
	.probe = lemokey_probe,
	.remove = lemokey_remove,
	.commit = lemokey_commit,
};
