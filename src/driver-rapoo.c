/*
 * Rapoo Mouse Driver
 *
 * For notes about the protocol see:
 * https://gist.github.com/akvadrako/f334d36099802da2f80cb2b8b150b892
 *
 * Copyright © 2024 Devin Bayer
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

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag-private.h"
#include "libratbag-util.h"
#include "libratbag-data.h"
#include "shared-macro.h"
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define RAPOO_SETTING_REPORTID 0xba
#define RAPOO_SETTING_BODYSIZE 32

#define RAPOO_DONGLE_PRODUCT_ID 0x1225
#define RAPOO_TARGET_DONGLE 0xa5
#define RAPOO_TARGET_USB 0xff

#define RAPOO_STATUS_GOOD 0x01
#define RAPOO_STATUS_WAIT 0x02

union report {
	// for hidraw write(), the first byte is used as the report ID
	// and is kept in the request data
	struct {
		uint8_t report_id;
		uint8_t target;
		uint8_t body[0];
	} __attribute__((__packed__)) out;

	uint8_t raw[RAPOO_SETTING_BODYSIZE];
};

static int
test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, RAPOO_SETTING_REPORTID);
}

static int
write_report(struct ratbag_device *device, union report *buf)
{
	int rc;

	rc = ratbag_hidraw_output_report(device, (void *)buf, sizeof(*buf));
	if (rc < 0)
		return rc;
	log_debug(device->ratbag, "rapoo: polling for result\n");

	// wait up to 2 seconds for a response
	for(int i = 0; i < 20; ++i) {
		rc = ratbag_hidraw_raw_request(device, RAPOO_SETTING_REPORTID, (void *)buf, sizeof(*buf), HID_INPUT_REPORT, HID_REQ_GET_REPORT);
		if (rc < 0)
			return rc;
		if (buf->raw[0] != RAPOO_STATUS_WAIT)
			break;
		msleep(100);
	}
	log_debug(device->ratbag, "rapoo: result %d\n", buf->raw[0]);

	if(buf->raw[0] == RAPOO_STATUS_GOOD)
		return 0;

	log_error(device->ratbag, "rapoo: invalid status: %d\n", buf->raw[0]);
	return -EINVAL;
}

// convert hex string to binary string
static void
from_hex(const char *hex, uint8_t *out, size_t size)
{
	size_t out_i = 0;

	for(int i = 0; hex[i]; ++i) {
		if(hex[i] == ' ')
			continue;

		assert(isxdigit(hex[i]));
		assert(isxdigit(hex[i+1]));
		assert(out_i < size);

		out[out_i] = strtol((char[]){ hex[i], hex[i+1], 0 }, NULL, 16);
		out_i++;
		i++; // skip an extra byte
	}
}

static union report
prepare_buffer(struct ratbag_device *device, const char *msg)
{
	union report buf = {0};
	buf.out.report_id = RAPOO_SETTING_REPORTID;

	if(device->ids.product == RAPOO_DONGLE_PRODUCT_ID)
		buf.out.target = RAPOO_TARGET_DONGLE;
	else
		buf.out.target = RAPOO_TARGET_USB;

	from_hex(msg, buf.out.body, sizeof(buf));
	return buf;
}

#define RAPOO_DPI_SET_BYTE 8
#define RAPOO_DPI_GET_BYTE 4

// offsets:                       2    4    6    8    10
const char RAPOO_DPI_SET_BODY[] = "a504 9808 0000 dd00 0201";
const char RAPOO_DPI_GET_BODY[] = "a404 9808";

static unsigned int
read_dpi(struct ratbag_device *device, const unsigned int *dpilist, int size)
{
	union report buf = prepare_buffer(device, RAPOO_DPI_GET_BODY);

	log_debug(device->ratbag, "rapoo: reading dpi\n");
	int rc = write_report(device, &buf);
	if(rc < 0)
		return rc;

	int idx = buf.raw[RAPOO_DPI_GET_BYTE];
	if(idx >= size) {
		log_error(device->ratbag, "rapoo: invalid dpi index %d\n", idx);
		return 0;
	}

	unsigned int dpi = dpilist[idx];

	log_debug(device->ratbag, "rapoo: dpi index %d dpi %d\n", idx, dpi);
	return dpi;
}

const unsigned int RATE_LIST[] = {  125,  250,  500, 1000, 2000, 4000, 8000 };
const uint8_t RATE_KEYS[] = { 0x08, 0x04, 0x02, 0x01, 0x84, 0x82, 0x81 };

#define RAPOO_RATE_SET_BYTE 10
#define RAPOO_RATE_GET_BYTE 6

// offsets:                        2    4    6    8    10
const char RAPOO_RATE_SET_BODY[] = "a504 8008 0000 01ff ddff";
const char RAPOO_RATE_GET_BODY[] = "a404 8008";

static int
read_rate_hz(struct ratbag_device *device)
{
	union report buf = prepare_buffer(device, RAPOO_RATE_GET_BODY);

	log_debug(device->ratbag, "rapoo: reading rate\n");
	int rc = write_report(device, &buf);
	if(rc < 0)
		return rc;

	int rate_key = buf.raw[RAPOO_RATE_GET_BYTE];
	int hz = -1;
	for(size_t i = 0; i < ARRAY_LENGTH(RATE_LIST); ++i) {
		if (rate_key == RATE_KEYS[i])
			hz = RATE_LIST[i];
	}

	log_debug(device->ratbag, "rapoo: rate key %d hz %d\n", rate_key, hz);
	return hz;
}

static int
rapoo_probe(struct ratbag_device *device)
{
	int rc;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;

	rc = ratbag_find_hidraw(device, test_hidraw);
	if (rc)
		return rc;

	// read firmware version
	union report buf = prepare_buffer(device, "a300");
	rc = write_report(device, &buf);
	if(rc < 0)
		return rc;

	char fw[10] = {0};
	snprintf(fw, sizeof(fw) - 1, "%d", buf.raw[1]);
	ratbag_device_set_firmware_version(device, fw);

	log_debug(device->ratbag, "rapoo: found %s fw %s\n", device->hidraw[0].sysname, fw);

	ratbag_device_init_profiles(device, 1, 1, 0, 0);
	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		ratbag_profile_for_each_resolution(profile, resolution) {
			resolution->is_active = true;
			resolution->is_default = true;

			const struct dpi_list *dpilist = ratbag_device_data_rapoo_get_dpi_list(device->data);
			ratbag_resolution_set_dpi_list(resolution,
				  (unsigned int *)dpilist->entries,
				  dpilist->nentries);

			int dpi = read_dpi(device, resolution->dpis, resolution->ndpis);
			resolution->dpi_x = dpi;
			resolution->dpi_y = dpi;
		}

		ratbag_profile_set_report_rate_list(profile, RATE_LIST, ARRAY_LENGTH(RATE_LIST));
		profile->hz = read_rate_hz(device);
	}

	return 0;
}


static int
set_dpi(struct ratbag_resolution *resolution)
{
	struct ratbag_device *device = resolution->profile->device;
	union report buf = prepare_buffer(device, RAPOO_DPI_SET_BODY);
	int idx = -1;

	for (size_t i = 0; i < resolution->ndpis; i++) {
		if (resolution->dpis[i] == resolution->dpi_x)
			idx = i;
	}

	if(idx < 0) {
		log_error(device->ratbag, "rapoo: invalid dpi: %d\n", resolution->dpi_x);
		return -1;
	}

	buf.raw[RAPOO_DPI_SET_BYTE] = idx;
	return write_report(device, &buf);
}

static int
set_rate(struct ratbag_profile *profile)
{
	union report buf = prepare_buffer(profile->device, RAPOO_RATE_SET_BODY);

	int key = -1;
	for(size_t i = 0; i < ARRAY_LENGTH(RATE_LIST); ++i) {
		if (profile->hz == RATE_LIST[i])
			key = RATE_KEYS[i];
	}

	if(key < 0) {
		log_error(profile->device->ratbag, "rapoo: invalid rate: %d\n", profile->hz);
		return -1;
	}

	buf.raw[RAPOO_RATE_SET_BYTE] = key;
	return write_report(profile->device, &buf);
}


static int
rapoo_commit(struct ratbag_device *device)
{
	int rc;
	struct ratbag_resolution *resolution;
	struct ratbag_profile *profile;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty) continue;

		ratbag_profile_for_each_resolution(profile, resolution) {
			if (!resolution->dirty) continue;

			rc = set_dpi(resolution);
			if (rc) return rc;
		}

		rc = set_rate(profile);
		if (rc) return rc;
	}
	return 0;
}

static void
rapoo_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

struct ratbag_driver rapoo_driver = {
	.name = "Rapoo VT0Pro",
	.id = "rapoo",
	.probe = rapoo_probe,
	.remove = rapoo_remove,
	.commit = rapoo_commit,
};
