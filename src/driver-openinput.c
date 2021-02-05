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

static unsigned int report_rates[] = { 125, 250, 500, 750, 1000 };

struct openinput_drv_data {
	unsigned int num_profiles;
	unsigned int num_resolutions;
	unsigned int num_buttons;
	unsigned int num_leds;
};

static void
openinput_read_profile(struct ratbag_profile *profile)
{
	ratbag_profile_set_report_rate_list(profile, report_rates, sizeof(*report_rates));
	profile->is_active = true;
}

static int
openinput_probe(struct ratbag_device *device)
{
	struct openinput_drv_data *drv_data;
	struct ratbag_profile *profile;

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
