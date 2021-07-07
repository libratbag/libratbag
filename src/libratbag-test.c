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

#include <linux/input.h>
#include <stdio.h>

#include "libratbag-private.h"
#include "libratbag-util.h"
#include "libratbag-test.h"

extern struct ratbag_driver test_driver;

static inline void
ratbag_register_test_drivers(struct ratbag *ratbag)
{
	struct ratbag_driver *driver;

	/* Don't use a static variable here, otherwise the CK_FORK=no case
	 * will fail */
	list_for_each(driver, &ratbag->drivers, link) {
		if (streq(driver->name, test_driver.name))
			return;
	}

	ratbag_register_driver(ratbag, &test_driver);
}

LIBRATBAG_EXPORT struct ratbag_device*
ratbag_device_new_test_device(struct ratbag *ratbag,
			      const struct ratbag_test_device *test_device)
{
	struct ratbag_device* device = NULL;
#if BUILD_TESTS

	struct input_id id = {
		.bustype = 0x00,
		.vendor = 0x00,
		.product = 0x00,
		.version = 0x00,
	};

	ratbag_register_test_drivers(ratbag);

	if (getenv("RATBAG_TEST") == NULL) {
		fprintf(stderr, "RATBAG_TEST environment variable not set\n");
		abort();
	}

	device = ratbag_device_new(ratbag, NULL, "Test device", &id);

	device->devicetype = TYPE_MOUSE;

	if (!ratbag_assign_driver(device, &device->ids, test_device)) {
		ratbag_device_destroy(device);
		return NULL;
	}
#endif

	return device;
}
