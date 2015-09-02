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


#ifndef LIBRATBAG_TEST_H
#define LIBRATBAG_TEST_H

#include "libratbag.h"

#define RATBAG_TEST_MAX_PROFILES 12
#define RATBAG_TEST_MAX_BUTTONS 25
#define RATBAG_TEST_MAX_RESOLUTIONS 8

struct ratbag_test_button {
};

struct ratbag_test_resolution {
	int xres, yres;
	int hz;
	bool active;
	bool dflt;
	uint32_t caps;
};

struct ratbag_test_profile {
	unsigned int num_resolutions;
	struct ratbag_test_button buttons[RATBAG_TEST_MAX_BUTTONS];
	struct ratbag_test_resolution resolutions[RATBAG_TEST_MAX_RESOLUTIONS];
	bool active;
	bool dflt;
};

struct ratbag_test_device {
	unsigned int num_profiles;
	unsigned int num_buttons;
	struct ratbag_test_profile profiles[RATBAG_TEST_MAX_PROFILES];
};

struct ratbag_device* ratbag_device_new_test_device(struct ratbag *ratbag,
						    struct ratbag_test_device *test_device);

#endif /* LIBRATBAG_TEST_H */
