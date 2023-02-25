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


#pragma once

#include <stdint.h>

#include "libratbag.h"

#define RATBAG_TEST_MAX_PROFILES 12
#define RATBAG_TEST_MAX_BUTTONS 25
#define RATBAG_TEST_MAX_RESOLUTIONS 8
#define RATBAG_TEST_MAX_LEDS 8

struct ratbag_test_macro_event {
	enum ratbag_macro_event_type type;
	unsigned int value;
};

struct ratbag_test_button {
	enum ratbag_button_action_type action_type;
	union {
		int button;
		int key;
		enum ratbag_button_action_special special;
		struct ratbag_test_macro_event macro[24];
	};
};

struct ratbag_test_resolution {
	int xres, yres;
	bool active;
	bool dflt;
	bool disabled;
	uint32_t caps[10];

	int dpi_min, dpi_max;
};

struct ratbag_test_color {
	unsigned short red;
	unsigned short green;
	unsigned short blue;
};

struct ratbag_test_led {
	enum ratbag_led_mode mode;
	struct ratbag_test_color color;
	unsigned int ms;
	unsigned int brightness;
};

struct ratbag_test_profile {
	char *name;
	struct ratbag_test_button buttons[RATBAG_TEST_MAX_BUTTONS];
	struct ratbag_test_resolution resolutions[RATBAG_TEST_MAX_RESOLUTIONS];
	struct ratbag_test_led leds[RATBAG_TEST_MAX_LEDS];
	bool active;
	bool dflt;
	bool disabled;
	uint32_t caps[10];

	int hz;
	unsigned int report_rates[5];
};

struct ratbag_test_device {
	unsigned int num_profiles;
	unsigned int num_resolutions;
	unsigned int num_buttons;
	unsigned int num_leds;
	struct ratbag_test_profile profiles[RATBAG_TEST_MAX_PROFILES];
	void (*destroyed)(struct ratbag_device *device, void *data);
	void *destroyed_data;
};

struct ratbag_device* ratbag_device_new_test_device(struct ratbag *ratbag,
						    const struct ratbag_test_device *test_device);

