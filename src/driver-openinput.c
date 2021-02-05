/*
 * Copyright © 2021 Filipe Laíns
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


/* reports */
#define OI_REPORT_SHORT		0x20
#define OI_REPORT_LONG		0x21
#define OI_REPORT_SHORT_SIZE	8
#define OI_REPORT_LONG_SIZE	32

#define OI_REPORT_MAX_SIZE	OI_REPORT_LONG_SIZE

/* protocol function pages */
#define OI_PAGE_INFO			0x00
#define OI_PAGE_GIMMICKS		0xFD
#define OI_PAGE_DEBUG			0xFE
#define OI_PAGE_ERROR			0xFF

/* info page (0x00) functions */
#define OI_FUNCTION_VERSION 0x00
#define OI_FUNCTION_FW_INFO 0x01


static unsigned int report_rates[] = { 125, 250, 500, 750, 1000 };

struct openinput_drv_data {
	unsigned int num_profiles;
	unsigned int num_resolutions;
	unsigned int num_buttons;
	unsigned int num_leds;
};

struct oi_report_t {
	uint8_t id;
	uint8_t function_page;
	uint8_t function;
	uint8_t data[29];
} __attribute__((__packed__));

static size_t
openinput_get_report_size(unsigned int report)
{
	switch (report) {
	case OI_REPORT_SHORT:
		return OI_REPORT_SHORT_SIZE;
	case OI_REPORT_LONG:
		return OI_REPORT_LONG_SIZE;
	default:
		return 0;
	}
}

static int
openinput_send_report(struct ratbag_device *device, struct oi_report_t *report)
{
	int ret;
	uint8_t buffer[OI_REPORT_MAX_SIZE];
	size_t size = openinput_get_report_size(report->id);

	memcpy(buffer, report, size);

	ret = ratbag_hidraw_output_report(device, buffer, size);
	if (ret < 0) {
		log_error(device->ratbag, "openinput: failed to send data to device (%s)\n",
			  strerror(-ret));
		return ret;
	}

	ret = ratbag_hidraw_read_input_report(device, buffer, OI_REPORT_MAX_SIZE);
	if (ret < 0) {
		log_error(device->ratbag, "openinput: failed to read data from device (%s)\n",
			  strerror(-ret));
		return ret;
	}

	memcpy(report, buffer, openinput_get_report_size(buffer[0]));

	// TODO: check for error

	return 0;
}

static void
openinput_read_profile(struct ratbag_profile *profile)
{
	ratbag_profile_set_report_rate_list(profile, report_rates, sizeof(*report_rates));
	profile->is_active = true;
}

static int
openinput_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, OI_REPORT_SHORT);
}

static int
openinput_probe(struct ratbag_device *device)
{
	int ret;
	struct openinput_drv_data *drv_data;
	struct ratbag_profile *profile;

	ret = ratbag_find_hidraw(device, openinput_test_hidraw);
	if (ret)
		return ret;

	drv_data = zalloc(sizeof(*drv_data));

	drv_data->num_profiles = 1;

	ratbag_set_drv_data(device, drv_data);
	ratbag_device_init_profiles(device,
				    drv_data->num_profiles,
				    drv_data->num_resolutions,
				    drv_data->num_buttons,
				    drv_data->num_leds);

	ratbag_device_for_each_profile(device, profile)
		openinput_read_profile(profile);

	return 0;
}

static void
openinput_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver openinput_driver = {
	.name = "openinput",
	.id = "openinput",
	.probe = openinput_probe,
	.remove = openinput_remove,
};
