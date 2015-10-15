/*
 * Copyright © 2015 Red Hat, Inc.
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

#include "libratbag-private.h"
#include "libratbag-test.h"

static int
test_has_capability(const struct ratbag_device *device,
		    enum ratbag_device_capability cap)
{
	switch (cap) {
	case RATBAG_DEVICE_CAP_NONE:
		abort();
		break;
	case RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION:
	case RATBAG_DEVICE_CAP_BUTTON_KEY:
	case RATBAG_DEVICE_CAP_BUTTON_MACROS:
		return (device->num_profiles > 1);
	default:
		return 0;
	}
	return 0;
}

static void
test_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(profile->device);
	struct ratbag_test_profile *p;
	struct ratbag_test_resolution *r;
	unsigned int i;

	assert(index < d->num_profiles);

	p = &d->profiles[index];
	profile->resolution.num_modes = p->num_resolutions;
	for (i = 0; i < p->num_resolutions; i++) {
		struct ratbag_resolution *res;

		r = &p->resolutions[i];
		res = ratbag_resolution_init(profile, i, r->xres, r->yres, r->hz);
		res->is_active = r->active;
		res->is_default = r->dflt;
		res->capabilities = r->caps;
		res->hz = r->hz;
		assert(res);
	}

	profile->is_active = p->active;
	profile->is_default = p->dflt;
}

static int
test_write_profile(struct ratbag_profile *profile)
{
	/* check if the device is still valid */
	assert(ratbag_get_drv_data(profile->device) != NULL);
	return 0;
}

static int
test_fake_probe(struct ratbag_device *device)
{
	return -ENODEV;
}

static int
test_probe(struct ratbag_device *device, void *data)
{
	struct ratbag_test_device *test_device = data;

	ratbag_set_drv_data(device, test_device);
	ratbag_device_init_profiles(device,
				    test_device->num_profiles,
				    test_device->num_buttons);
	return 0;
}

static void
test_remove(struct ratbag_device *device)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(device);

	/* remove must be called only once */
	assert(d != NULL);
	if (d->destroyed)
		d->destroyed(device, d->destroyed_data);
	ratbag_set_drv_data(device, NULL);
}

struct ratbag_driver test_driver = {
	.name = "Test driver",
	.id = "test_driver",
	.probe = test_fake_probe,
	.test_probe = test_probe,
	.remove = test_remove,
	.read_profile = test_read_profile,
	.write_profile = test_write_profile,
	.set_active_profile = NULL,
	.has_capability = test_has_capability,
	.read_button = NULL,
	.write_button = NULL,
	.write_resolution_dpi = NULL,
};
