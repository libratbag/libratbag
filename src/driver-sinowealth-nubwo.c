/*
 * Copyright © 2020 Pipat Saengow
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
#include "libratbag-enums.h"
#include "libratbag-hidraw.h"
#include "libratbag-private.h"
#include "libratbag-util.h"
#include "shared-macro.h"
#include <stdint.h>
#include <string.h>

#define SINOWEALTHNUBWO_PERF_CMD_REPORTID 0x02
#define SINOWEALTHNUBWO_AESTHETIC_CMD_REPORTID 0x03
#define SINOWEALTHNUBWO_GET_FIRMWARE_CMD_REPORTID 0x04

#define SINOWEALTHNUBWO_GET_FIRMWARE_MSGSIZE 256
#define SINOWEALTHNUBWO_GET_FIRMWARE_MSGOFFSET 48

#define SINOWEALTHNUBWO_PERF_CMD_MSGSIZE 16

// Actually more but I only implemented one
#define SINOWEALTHNUBWO_NUM_PROFILES 1
#define SINOWEALTHNUBWO_NUM_RESOLUTIONS 1
// Actually 8 but I am not going to implement macros.
#define SINOWEALTHNUBWO_NUM_BUTTONS 0
#define SINOWEALTHNUBWO_NUM_LEDS 1

//Magic set_feature that must be called before requesting firmware string
static uint8_t PREFIRMWARE_QUERY_MSG[] = {0x02, 0x01, 0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static unsigned int REPORT_RATES[] = { 125, 250, 333, 500, 1000 };
static uint8_t REPORT_RATES_ENCODED[] = {0x08, 0x04, 0x03, 0x02, 0x01};
static uint8_t REPORT_RATES_CMD[] = {0x02, 0x06, 0xbb, 0xaa, 0x28, 0x00, 0x01, 0x00};

static unsigned int DPILIST[] = { 1000, 2000, 3000, 5000, 15000};
static uint8_t DPI_ENCODED[] = {0x04, 0x03, 0x02, 0x01, 0x00};
static uint8_t DPI_CMD[] = { 0x02, 0x06, 0xbb, 0xaa, 0x32, 0x00, 0x01, 0x00 };

enum
sinowealthnubwo_color_mode {
	SINOWEALTHNUBWO_COLOR_OFF = 0x00,
	SINOWEALTHNUBWO_COLOR_ON = 0x01,
	SINOWEALTHNUBWO_COLOR_BREATHING = 0x02,
	SINOWEALTHNUBWO_COLOR_COLOR_SHIFT = 0x03,
	SINOWEALTHNUBWO_COLOR_SPECTRUM = 0x04,
	SINOWEALTHNUBWO_COLOR_MARQUEE = 0x05,
};

struct
sinowealthnubwo_aesthetic_report {
	uint8_t report_id; //0x03
	uint8_t cmd[7];
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t color_mode;
	uint8_t padzero; // 0x00
	uint8_t brightness; // 0x01 to 0x03
	uint8_t tempo; // 0x05 0x03 0x01
	uint8_t padzero2[16*3+1];
} __attribute__((packed));
// FIXME: there is a missing static assert of size here.

static uint8_t AESTHETIC_CMD[] = {0x06, 0xbb, 0xaa, 0x2a, 0x00, 0x0a, 0x00};

static int
sinowealthnubwo_test_hidraw(struct ratbag_device *device)
{
	return
		ratbag_hidraw_has_report(device, SINOWEALTHNUBWO_AESTHETIC_CMD_REPORTID)
		&& ratbag_hidraw_has_report(device, SINOWEALTHNUBWO_PERF_CMD_REPORTID)
		&& ratbag_hidraw_has_report(device, SINOWEALTHNUBWO_GET_FIRMWARE_CMD_REPORTID);
}

static int
sinowealth_get_firmware_string(struct ratbag_device *device, char **output)
{
	uint8_t buff[SINOWEALTHNUBWO_GET_FIRMWARE_MSGSIZE + 1] = {0}; //Purposefully overalloacate to prevent buffer overrun
	int size;

	size = ratbag_hidraw_set_feature_report(device, SINOWEALTHNUBWO_PERF_CMD_REPORTID, PREFIRMWARE_QUERY_MSG, ARRAY_LENGTH(PREFIRMWARE_QUERY_MSG));
	if (size < 0) {
		log_error(device->ratbag, "Error while sending pre-firmware request message: %d\n", size);
		return size;
	}

	size = ratbag_hidraw_get_feature_report(device, SINOWEALTHNUBWO_GET_FIRMWARE_CMD_REPORTID, buff, SINOWEALTHNUBWO_GET_FIRMWARE_MSGSIZE);
	if (size < 0) {
		return size;
	}
	if (size != SINOWEALTHNUBWO_GET_FIRMWARE_MSGSIZE) {
		log_error(device->ratbag ,"Firmware report reply size mismatch expected %d got %d\n", SINOWEALTHNUBWO_GET_FIRMWARE_MSGSIZE, size);
		return -EIO;
	}

	*output = strdup_ascii_only((char *) buff + SINOWEALTHNUBWO_GET_FIRMWARE_MSGOFFSET);
	return 0;
}

static int
sinowealthnubwo_probe(struct ratbag_device *device)
{
	int error;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

	error = ratbag_find_hidraw(device, sinowealthnubwo_test_hidraw);
	if (error)
		return error;

	char *fwstr;
	error = sinowealth_get_firmware_string(device, &fwstr);
	if (error)
		return error;
	log_info(device->ratbag, "Firmware: %s\n", fwstr);
	free(fwstr);

	ratbag_device_init_profiles(device,
			SINOWEALTHNUBWO_NUM_PROFILES,
			SINOWEALTHNUBWO_NUM_RESOLUTIONS,
			SINOWEALTHNUBWO_NUM_BUTTONS,
			SINOWEALTHNUBWO_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);
		ratbag_profile_set_report_rate_list(profile, REPORT_RATES, ARRAY_LENGTH(REPORT_RATES));
		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list(resolution, DPILIST, ARRAY_LENGTH(DPILIST));
			resolution->dpi_x = resolution->dpi_y = DPILIST[ARRAY_LENGTH(DPILIST)-1];
			resolution->is_active = true;
			resolution->is_default = true;
		}
		ratbag_profile_for_each_led(profile, led) {
			led->mode = RATBAG_LED_OFF;
			led->color.red = led->color.green = led->color.blue = 0xFF;
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
			//Actually more
		}
	}

	return 0;
}

static uint8_t
encode_dpi(unsigned int dpi)
{
	for (size_t i = 0; i < ARRAY_LENGTH(DPILIST); i++)
		if (DPILIST[i] == dpi)
			return DPI_ENCODED[i];
	return DPI_ENCODED[0];
}

static uint8_t
encode_report_rate(unsigned int reportrate)
{
	for (size_t i = 0; i < ARRAY_LENGTH(REPORT_RATES); i++)
		if (REPORT_RATES[i] == reportrate)
			return REPORT_RATES_ENCODED[i];
	return REPORT_RATES_ENCODED[0];
}

static int
sinowealthnubwo_set_dpi(struct ratbag_device *device, int dpi)
{
	uint8_t buf[SINOWEALTHNUBWO_PERF_CMD_MSGSIZE] = {0};
	memcpy(buf, DPI_CMD, ARRAY_LENGTH(DPI_CMD));
	buf[ARRAY_LENGTH(DPI_CMD)] = encode_dpi(dpi);
	int error = ratbag_hidraw_set_feature_report(device, SINOWEALTHNUBWO_PERF_CMD_REPORTID, buf, SINOWEALTHNUBWO_PERF_CMD_MSGSIZE);
	if (error < 0)
		return error;
	return 0;
}

static int
sinowealthnubwo_set_report_rate(struct ratbag_device *device, int reportrate)
{
	uint8_t buf[SINOWEALTHNUBWO_PERF_CMD_MSGSIZE] = {0};
	memcpy(buf, REPORT_RATES_CMD, ARRAY_LENGTH(REPORT_RATES_CMD));
	buf[ARRAY_LENGTH(REPORT_RATES_CMD)] = encode_report_rate(reportrate);
	int error = ratbag_hidraw_set_feature_report(device, SINOWEALTHNUBWO_PERF_CMD_REPORTID, buf, SINOWEALTHNUBWO_PERF_CMD_MSGSIZE);
	if (error < 0)
		return error;
	return 0;
}

static enum sinowealthnubwo_color_mode encode_color(enum ratbag_led_mode mode)
{
	switch (mode) {
	case RATBAG_LED_OFF:
		return SINOWEALTHNUBWO_COLOR_OFF;
	case RATBAG_LED_ON:
		return SINOWEALTHNUBWO_COLOR_ON;
	case RATBAG_LED_CYCLE:
		return SINOWEALTHNUBWO_COLOR_MARQUEE;
	case RATBAG_LED_BREATHING:
		return SINOWEALTHNUBWO_COLOR_BREATHING;
	default:
		return SINOWEALTHNUBWO_COLOR_OFF;
	}
}

static uint8_t normalize_duration(int duration)
{
	const int MAX_DURATION = 10000;
	const uint8_t avail_dura[] = {0x01, 0x03, 0x05};
	const int selected = (duration * ((int) ARRAY_LENGTH(avail_dura)) - 1) / MAX_DURATION;
	return avail_dura[selected];
}

static uint8_t normalize_brightness(int brightness)
{
	const int MAX_BRIGHTNESS = 255;
	return 1 + (brightness * 3 - 1) / MAX_BRIGHTNESS;
}

static int
sinowealthnubwo_set_aesthetic(struct ratbag_device *device, struct ratbag_led *led) {
	struct sinowealthnubwo_aesthetic_report report = {0};
	report.report_id = SINOWEALTHNUBWO_AESTHETIC_CMD_REPORTID;
	memcpy(report.cmd, AESTHETIC_CMD, ARRAY_LENGTH(AESTHETIC_CMD));
	report.r = led->color.red;
	report.g = led->color.green;
	report.b = led->color.blue;
	report.color_mode = encode_color(led->mode);
	report.tempo = normalize_duration(led->ms);
	report.brightness = normalize_brightness(led->brightness);

	int size = ratbag_hidraw_set_feature_report(device, SINOWEALTHNUBWO_AESTHETIC_CMD_REPORTID, (uint8_t *) &report, sizeof(report));
	if (size < 0)
		return size;
	return 0;
}

static int
sinowealthnubwo_write_profile(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	int error;

	log_debug(device->ratbag, "Writing updates\n");

	log_debug(device->ratbag, "Setting report rate\n");
	error = sinowealthnubwo_set_report_rate(device, profile->hz);
	if (error)
		return error;

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dirty)
			continue;
		log_debug(device->ratbag, "Setting DPI\n");
		error = sinowealthnubwo_set_dpi(device, resolution->dpi_x);
		if (error)
			return error;
	}

	ratbag_profile_for_each_led(profile, led) {
		if (!led->dirty)
			continue;
		log_debug(device->ratbag, "Setting aesthetic\n");
		error = sinowealthnubwo_set_aesthetic(device, led);
		if (error)
			return error;
	}
	return 0;
}

static int
sinowealthnubwo_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty) continue;

		int error = sinowealthnubwo_write_profile(device, profile);
		if (error)
			return error;
	}
	return 0;
}

static void
sinowealthnubwo_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

struct ratbag_driver sinowealth_nubwo_driver = {
	.name = "Sinowealth Nubwo",
	.id = "sinowealth_nubwo",
	.probe = sinowealthnubwo_probe,
	.remove = sinowealthnubwo_remove,
	.commit = sinowealthnubwo_commit,
};
