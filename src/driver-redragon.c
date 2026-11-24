/*
 * Copyright © 2026 Kevin Joyce
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
#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define REDRAGON_REPORT_ID 8
#define REDRAGON_REPORT_SIZE 16

#define REDRAGON_CMD_WRITE_DPI 7

#define REDRAGON_NUM_PROFILES 1
#define REDRAGON_NUM_DPI 5
#define REDRAGON_BUTTON_MAX 8

static uint8_t
redragon_dpi_to_raw(unsigned int dpi)
{
	if (dpi <= 100) return 0;
	if (dpi <= 400) {
		return (uint8_t)((dpi - 100) * 4 / 300);
	}
	if (dpi <= 800) {
		return (uint8_t)(4 + (dpi - 400) * 6 / 400);
	}
	if (dpi <= 1600) {
		return (uint8_t)(10 + (dpi - 800) * 8 / 800);
	}
	if (dpi <= 3200) {
		return (uint8_t)(18 + (dpi - 1600) * 20 / 1600);
	}
	if (dpi >= 16000) return 189;
	return (uint8_t)(38 + (dpi - 3200) * 151 / 12800);
}

static unsigned int __attribute__((unused))
redragon_raw_to_dpi(uint8_t raw)
{
	unsigned int dpi;
	if (raw == 0) return 100;
	if (raw <= 4) {
		dpi = 100 + raw * 300 / 4;
	} else if (raw <= 10) {
		dpi = 400 + (raw - 4) * 400 / 6;
	} else if (raw <= 18) {
		dpi = 800 + (raw - 10) * 800 / 8;
	} else if (raw <= 38) {
		dpi = 1600 + (raw - 18) * 1600 / 20;
	} else if (raw >= 189) {
		return 16000;
	} else {
		dpi = 3200 + (raw - 38) * 12800 / 151;
	}
	/* Round to nearest 50 DPI */
	return ((dpi + 25) / 50) * 50;
}

static int
redragon_write_resolution(struct ratbag_resolution *resolution)
{
	struct ratbag_device *device = resolution->profile->device;
	uint8_t buf[REDRAGON_REPORT_SIZE] = {0};
	uint8_t offset;
	int rc;

	switch (resolution->index) {
	case 0: offset = 0x0c; break;
	case 1: offset = 0x10; break;
	case 2: offset = 0x14; break;
	case 3: offset = 0x18; break;
	case 4: offset = 0x1c; break;
	default: return -EINVAL;
	}

	buf[0] = REDRAGON_REPORT_ID;
	buf[1] = REDRAGON_CMD_WRITE_DPI;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = offset;
	buf[5] = 0x04;
	buf[6] = redragon_dpi_to_raw(resolution->dpi_x);
	buf[7] = redragon_dpi_to_raw(resolution->dpi_y);
	buf[8] = 0x00;
	buf[9] = (0x55 - (buf[6] + buf[7])) & 0xff;

	rc = ratbag_hidraw_set_feature_report(device, REDRAGON_REPORT_ID, buf, sizeof(buf));
	if (rc < 0) {
		log_error(device->ratbag, "Error writing DPI preset %d: %s (%d)\n",
				  resolution->index, strerror(-rc), rc);
		return rc;
	}

	return 0;
}

static int
redragon_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->dirty) {
			rc = redragon_write_resolution(resolution);
			if (rc)
				return rc;
			resolution->dirty = false;
		}
	}

	return 0;
}

static int
redragon_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	int rc;
	unsigned int dpis[300];
	unsigned int num_dpis = 0;

	rc = ratbag_open_hidraw(device);
	if (rc)
		return rc;

	if (!ratbag_hidraw_has_report(device, REDRAGON_REPORT_ID)) {
		ratbag_close_hidraw(device);
		return -ENODEV;
	}

	ratbag_device_init_profiles(device,
								REDRAGON_NUM_PROFILES,
								REDRAGON_NUM_DPI,
								REDRAGON_BUTTON_MAX,
								0);

	profile = ratbag_device_get_profile(device, 0);
	profile->is_active = true;

	/* Available DPI list: 100 to 16000 in steps of 100 */
	for (unsigned int dpi = 100; dpi <= 16000; dpi += 100) {
		dpis[num_dpis++] = dpi;
	}

	unsigned int default_dpis[] = { 400, 800, 1600, 3200, 16000 };

	ratbag_profile_for_each_resolution(profile, resolution) {
		resolution->dpi_x = default_dpis[resolution->index];
		resolution->dpi_y = default_dpis[resolution->index];
		ratbag_resolution_set_dpi_list(resolution, dpis, num_dpis);
		resolution->is_active = (resolution->index == 0);
	}

	unsigned int report_rates[] = { 125, 250, 500, 1000 };
	ratbag_profile_set_report_rate_list(profile, report_rates,
					    ARRAY_LENGTH(report_rates));
	profile->hz = 1000;

	return 0;
}

static void
redragon_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

static int
redragon_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		if (profile->dirty) {
			rc = redragon_write_profile(profile);
			if (rc)
				return rc;
			profile->dirty = false;
		}
	}

	return 0;
}

struct ratbag_driver redragon_driver = {
	.name = "Redragon M801P",
	.id = "redragon",
	.probe = redragon_probe,
	.remove = redragon_remove,
	.commit = redragon_commit,
};
