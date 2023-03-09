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

#include <config.h>

#include <check.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/resource.h>

#include "libratbag-private.h"
#include "libratbag.h"
#include "libratbag-util.h"
#include "libratbag-test.h"

static void
device_destroyed(struct ratbag_device *device, void *data)
{
	int *count = data;

	if (!data)
		return;

	++*count;
}

/* A pre-setup sane device. Use for sanity testing by toggling the various
 * error conditions.
 */
const struct ratbag_test_device sane_device = {
	.num_profiles = 3,
	.num_resolutions = 3,
	.num_buttons = 1,
	.num_leds = 2,
	.profiles = {
		{
		.resolutions = {
			{ .xres = 100, .yres = 200,
				.dpi_min = 100, .dpi_max = 5000 },
			{ .xres = 200, .yres = 300 },
			{ .xres = 300, .yres = 400 },
		},
		.leds = {
			{ 0 },
		},
		.active = true,
		.dflt = false,
		.report_rates = { 500, 1000 },
		.hz = 1000,
		},
		{
		.resolutions = {
			{ .xres = 1100, .yres = 1200 },
			{ .xres = 1200, .yres = 1300 },
			{ .xres = 1300, .yres = 1400 },
		},
		.active = false,
		.dflt = true,
		.hz = 2000,
		},
		{
		.resolutions = {
			{ .xres = 2100, .yres = 2200 },
			{ .xres = 2200, .yres = 2300 },
			{ .xres = 2300, .yres = 2400 },
		},
		.leds = {
			{
				.mode = RATBAG_LED_ON,
				.color = { .red = 255, .green = 0,	.blue = 0 },
				.ms = 1000,
				.brightness = 20,
			},
			{
				.mode = RATBAG_LED_CYCLE,
				.color = { .red = 255, .green = 255, .blue = 0 },
				.ms = 333,
				.brightness = 40,
			}
		},
		.active = false,
		.dflt = false,
		.hz = 3000,
		},
	},
	.destroyed = device_destroyed,
	.destroyed_data = NULL,
};

static int
open_restricted(const char *path, int flags, void *user_data)
{
	/* for test devices we don't expect this to be called */
	ck_abort();
	return 0;
}

static void
close_restricted(int fd, void *user_data)
{
	/* for test devices we don't expect this to be called */
	ck_abort();
}

struct ratbag_interface abort_iface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

START_TEST(device_init)
{
	struct ratbag *r;
	struct ratbag_device *d;
	int nprofiles, nbuttons, nleds;
	struct ratbag_test_device td = sane_device;
	int device_freed_count = 0;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);
	nbuttons = ratbag_device_get_num_buttons(d);
	ck_assert_int_eq(nbuttons, 1);
	nleds = ratbag_device_get_num_leds(d);
	ck_assert_int_eq(nleds, 2);

	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

#define ref_unref_test(T, a) \
 { \
	 int i; \
	 struct T *tmp = NULL; \
 \
	 for (i = 0; i <= 256; i++) { \
		 tmp = T##_ref(a); \
		 ck_assert(tmp == a); \
	 } \
	 for (i = 0; i <= 256; i++) { \
		 tmp = T##_unref(a); \
		 ck_assert(tmp == NULL); \
	 } \
	 for (i = 0; i <= 256; i++) { \
		 tmp = T##_ref(a); \
		 ck_assert(tmp == a); \
		 tmp = T##_unref(a); \
		 ck_assert(tmp == NULL); \
	 } \
 }

START_TEST(device_ref_unref)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	ratbag_unref(r);

	ref_unref_test(ratbag_device, d);

	ratbag_device_unref(d);
}
END_TEST

START_TEST(device_free_context_before_device)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	r = ratbag_unref(r);
	ck_assert(r == NULL);

	d = ratbag_device_unref(d);
	ck_assert(d == NULL);
}
END_TEST

START_TEST(device_profiles)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	int nprofiles;
	int i;
	bool is_active;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	for (i = 0; i < nprofiles; i++) {
		p = ratbag_device_get_profile(d, i);
		ck_assert(p != NULL);

		is_active = ratbag_profile_is_active(p);
		ck_assert_int_eq(is_active, (i == 0));
		ratbag_profile_unref(p);
	}
	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_profiles_activate_disabled)
{
	int rc;
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;

	struct ratbag_test_device td = sane_device;

	td.profiles[0].active = true;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 0);

	ratbag_profile_set_cap(p, RATBAG_PROFILE_CAP_DISABLE);

	rc = ratbag_profile_set_enabled(p, false);
	ck_assert_int_eq(rc, RATBAG_ERROR_VALUE);

	ratbag_profile_unref(p);

	ratbag_unref(r);
}
END_TEST

START_TEST(device_profiles_disable_active)
{
	int rc;
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;

	struct ratbag_test_device td = sane_device;

	td.profiles[1].disabled = true;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 1);

	ratbag_profile_set_cap(p, RATBAG_PROFILE_CAP_DISABLE);

	rc = ratbag_profile_set_active(p);
	ck_assert_int_eq(rc, RATBAG_ERROR_VALUE);

	ratbag_profile_unref(p);

	ratbag_unref(r);
}
END_TEST

START_TEST(device_profiles_ref_unref)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;

	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	p = ratbag_device_get_profile(d, 1);

	ratbag_unref(r);
	ratbag_device_unref(d);

	ref_unref_test(ratbag_profile, p);

	ratbag_profile_unref(p);
}
END_TEST

START_TEST(device_profiles_num_0)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_test_device td = sane_device;

	td.num_profiles = 0;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d == NULL);

	ratbag_unref(r);
}
END_TEST

START_TEST(device_profiles_multiple_active)
{
	struct ratbag *r;
	struct ratbag_device *d;

	struct ratbag_test_device td = sane_device;

	td.profiles[0].active = true;
	td.profiles[1].active = true;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d == NULL);

	ratbag_unref(r);
}
END_TEST

START_TEST(device_profiles_get_invalid)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	int nprofiles;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	p = ratbag_device_get_profile(d, nprofiles);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile(d, nprofiles + 1);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile(d, -1);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile(d, INT_MAX);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile(d, UINT_MAX);
	ck_assert(p == NULL);

	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_resolutions)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_resolution *res;
	int nprofiles, nresolutions;
	int i, j;
	int xres, yres, rate;
	int device_freed_count = 0;
	bool is_active;

	struct ratbag_test_device td = {
		.num_profiles = 3,
		.num_resolutions = 3,
		.num_buttons = 1,
		.profiles = {
			{
			.resolutions = {
				{ .xres = 100, .yres = 200,
					.dpi_min = 50, .dpi_max = 5000 },
				{ .xres = 200, .yres = 300, .active = true },
				{ .xres = 300, .yres = 400 },
			},
			.active = true,
			.hz = 1000,
			.report_rates = { 500, 1000 },
			},
			{
			.resolutions = {
				{ .xres = 1100, .yres = 1200 },
				{ .xres = 1200, .yres = 1300, .active = true },
				{ .xres = 1300, .yres = 1400 },
			},
			.hz = 2000,
			},
			{
			.resolutions = {
				{ .xres = 2100, .yres = 2200 },
				{ .xres = 2200, .yres = 2300, .active = true },
				{ .xres = 2300, .yres = 2400 },
			},
			.hz = 3000,
			},
		},
		.destroyed = device_destroyed,
		.destroyed_data = &device_freed_count,
	};

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	for (i = 0; i < nprofiles; i++) {
		p = ratbag_device_get_profile(d, i);
		nresolutions = ratbag_profile_get_num_resolutions(p);
		ck_assert_int_eq(nresolutions, 3);

		rate = ratbag_profile_get_report_rate(p);

		for (j = 0; j < nresolutions; j++) {
			unsigned int dpis[200];
			int ndpis = ARRAY_LENGTH(dpis);

			res = ratbag_profile_get_resolution(p, j);

			xres = ratbag_resolution_get_dpi_x(res);
			yres = ratbag_resolution_get_dpi_y(res);
			is_active = ratbag_resolution_is_active(res);

			ndpis = ratbag_resolution_get_dpi_list(res, dpis, ndpis);
			ck_assert_int_lt(ndpis, ARRAY_LENGTH(dpis));
			ck_assert_int_gt(ndpis, 20);

			ck_assert_int_eq(xres, i * 1000 + (j + 1) * 100);
			ck_assert_int_eq(yres, i * 1000 + (j + 1) * 100 + 100);
			ck_assert_int_eq(xres, ratbag_resolution_get_dpi(res));
			ck_assert_int_eq(is_active, (j == 1));

			ck_assert_int_eq(dpis[0], 50);
			ck_assert_int_eq(dpis[ndpis - 1], 5000);

			ck_assert_int_eq(rate, (i + 1) * 1000);

			ratbag_resolution_unref(res);
		}

		ratbag_profile_unref(p);
	}
	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_resolutions_ref_unref)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_resolution *res;

	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	p = ratbag_device_get_profile(d, 1);
	res = ratbag_profile_get_resolution(p, 0);

	ratbag_unref(r);
	ratbag_device_unref(d);
	ratbag_profile_unref(p);

	ref_unref_test(ratbag_resolution, res);

	ratbag_resolution_unref(res);
}
END_TEST

START_TEST(device_resolutions_num_0)
{
	struct ratbag *r;
	struct ratbag_device *d;

	struct ratbag_test_device td = {
		.num_profiles = 1,
		.num_buttons = 1,
		.num_resolutions = 0, /* failure trigger */
		.profiles = {
			{
			.active = true,
			},
		},
	};

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d == NULL);
	ratbag_unref(r);
}
END_TEST

START_TEST(device_freed_before_profile)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	int is_active, rc;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;
	td.profiles[0].active = false;
	td.profiles[1].active = true;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 0);
	ck_assert(p != NULL);
	is_active = ratbag_profile_is_active(p);
	ck_assert_int_eq(is_active, 0);

	d = ratbag_device_unref(d);
	/* a ref to d is still kept through p */
	ck_assert(d == NULL);

	rc = ratbag_profile_set_active(p);
	ck_assert_int_eq(rc, 0);

	is_active = ratbag_profile_is_active(p);
	ck_assert_int_eq(is_active, 1);

	ratbag_profile_unref(p);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_and_profile_freed_before_button)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_button *b;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 0);
	ck_assert(p != NULL);

	d = ratbag_device_unref(d);
	/* a ref to d is still kept through p */
	ck_assert(d == NULL);

	b = ratbag_profile_get_button(p, 0);
	ck_assert(b != NULL);

	p = ratbag_profile_unref(p);
	/* a ref to p is still kept through b */
	ck_assert(p == NULL);

	/* FIXME: should probably call something for the button */
	b = ratbag_button_unref(b);
	ck_assert(b == NULL);

	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_and_profile_freed_before_resolution)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_resolution *res;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 0);
	ck_assert(p != NULL);

	d = ratbag_device_unref(d);
	/* a ref to d is still kept through p */
	ck_assert(d == NULL);

	res = ratbag_profile_get_resolution(p, 0);
	ck_assert(res != NULL);

	p = ratbag_profile_unref(p);
	/* a ref to p is still kept through res */
	ck_assert(p == NULL);

	/* FIXME: should probably call something for the resolution */
	res = ratbag_resolution_unref(res);
	ck_assert(res == NULL);

	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_and_profile_and_button_freed_before_resolution)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_resolution *res;
	struct ratbag_button *b;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 0);
	ck_assert(p != NULL);

	d = ratbag_device_unref(d);
	/* a ref to d is still kept through p, so d can not be NULL */
	ck_assert(d == NULL);

	res = ratbag_profile_get_resolution(p, 0);
	ck_assert(res != NULL);

	b = ratbag_profile_get_button(p, 0);
	ck_assert(b != NULL);

	p = ratbag_profile_unref(p);
	/* a ref to p is still kept through res and b */
	ck_assert(p == NULL);

	/* a ref to p is still in res */
	b = ratbag_button_unref(b);
	ck_assert(b == NULL);

	/* FIXME: should probably call something for the resolution */
	res = ratbag_resolution_unref(res);
	ck_assert(res == NULL);

	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_and_profile_and_resolution_freed_before_button)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_resolution *res;
	struct ratbag_button *b;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	p = ratbag_device_get_profile(d, 0);
	ck_assert(p != NULL);

	d = ratbag_device_unref(d);
	/* a ref to d is still kept through p */
	ck_assert(d == NULL);

	res = ratbag_profile_get_resolution(p, 0);
	ck_assert(res != NULL);

	b = ratbag_profile_get_button(p, 0);
	ck_assert(b != NULL);

	p = ratbag_profile_unref(p);
	/* a ref to p is still kept through res and b */
	ck_assert(p == NULL);

	/* a ref to p is still in res */
	res = ratbag_resolution_unref(res);
	ck_assert(res == NULL);

	/* FIXME: should probably call something for the button */
	b = ratbag_button_unref(b);
	ck_assert(b == NULL);

	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_buttons)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_button *b;
	struct ratbag_button_macro *m;
	int nprofiles, nbuttons;
	int i, j;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;
	td.num_buttons = 10;

	td.profiles[0].buttons[8].action_type = RATBAG_BUTTON_ACTION_TYPE_MACRO;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	nbuttons = ratbag_device_get_num_buttons(d);
	ck_assert_int_eq(nbuttons, 10);

	for (i = 0; i < nprofiles; i++) {
		p = ratbag_device_get_profile(d, i);
		ck_assert(p != NULL);

		for (j = 0; j < nbuttons; j++) {
			b = ratbag_profile_get_button(p, j);
			ck_assert(b != NULL);

			if (ratbag_button_get_action_type(b) ==
			    RATBAG_BUTTON_ACTION_TYPE_MACRO) {
				m = ratbag_button_get_macro(b);
				ck_assert(m != NULL);

				ratbag_button_macro_unref(m);
			}

			ratbag_button_unref(b);
		}
		ratbag_profile_unref(p);
	}
	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_buttons_ref_unref)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_button *b;

	struct ratbag_test_device td = sane_device;
	td.num_buttons = 10;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	p = ratbag_device_get_profile(d, 1);
	b = ratbag_profile_get_button(p, 0);

	ratbag_unref(r);
	ratbag_device_unref(d);
	ratbag_profile_unref(p);

	ref_unref_test(ratbag_button, b);

	ratbag_button_unref(b);
}
END_TEST

START_TEST(device_buttons_set)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_button *b;

	struct ratbag_test_device td = sane_device;
	td.num_buttons = 10;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	p = ratbag_device_get_profile(d, 1);
	b = ratbag_profile_get_button(p, 0);

	ratbag_button_set_button(b, 3);

	ratbag_button_unref(b);
	ratbag_profile_unref(p);
	ratbag_device_unref(d);
	ratbag_unref(r);
}
END_TEST

static void
assert_led_equals(struct ratbag_led *l, struct ratbag_test_led e_l)
{
	enum ratbag_led_mode mode;
	struct ratbag_color color;
	int brightness, ms;

	ck_assert(l != NULL);
	mode = ratbag_led_get_mode(l);
	color = ratbag_led_get_color(l);
	ms = ratbag_led_get_effect_duration(l);
	brightness = ratbag_led_get_brightness(l);
	ck_assert_int_eq(mode, e_l.mode);
	ck_assert_int_eq(color.red, e_l.color.red);
	ck_assert_int_eq(color.green, e_l.color.green);
	ck_assert_int_eq(color.blue, e_l.color.blue);
	ck_assert_int_eq(ms, e_l.ms);
	ck_assert_int_eq(brightness, e_l.brightness);
}

START_TEST(device_leds)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_led *led_logo, *led_side;
	int nprofiles;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	p = ratbag_device_get_profile(d, 2);
	ck_assert(p != NULL);

	led_logo = ratbag_profile_get_led(p, 0);
	assert_led_equals(led_logo, td.profiles[2].leds[0]);
	led_side = ratbag_profile_get_led(p, 1);
	assert_led_equals(led_side, td.profiles[2].leds[1]);

	ratbag_led_unref(led_logo);
	ratbag_led_unref(led_side);
	ratbag_profile_unref(p);
	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

START_TEST(device_leds_set)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	struct ratbag_led *l;
	int nprofiles;
	int device_freed_count = 0;

	struct ratbag_test_device td = sane_device;

	struct ratbag_color c = {
		.red = 0,
		.green = 111,
		.blue = 222
	};

	td.destroyed_data = &device_freed_count;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	p = ratbag_device_get_profile(d, 0);
	ck_assert(p != NULL);

	l = ratbag_profile_get_led(p, 0);

	ratbag_led_set_mode(l, RATBAG_LED_BREATHING);
	ratbag_led_set_color(l, c);
	ratbag_led_set_effect_duration(l, 90);
	ratbag_led_set_brightness(l, 22);

	l = ratbag_profile_get_led(p, 0);
	struct ratbag_test_led e_l = {
		.mode = RATBAG_LED_BREATHING,
		.color = {
			.red = c.red,
			.green = c.green,
			.blue = c.blue
		},
		.ms = 90,
		.brightness = 22
	};
	assert_led_equals(l, e_l);

	ratbag_led_unref(l);
	ratbag_led_unref(l);
	ratbag_profile_unref(p);
	ratbag_device_unref(d);
	ratbag_unref(r);
	ck_assert_int_eq(device_freed_count, 1);
}
END_TEST

static Suite *
test_context_suite(void)
{
	TCase *tc;
	Suite *s;

	s = suite_create("device");
	tc = tcase_create("device");
	tcase_add_test(tc, device_init);
	tcase_add_test(tc, device_ref_unref);
	tcase_add_test(tc, device_free_context_before_device);
	suite_add_tcase(s, tc);

	tc = tcase_create("profiles");
	tcase_add_test(tc, device_profiles);
	tcase_add_test(tc, device_profiles_activate_disabled);
	tcase_add_test(tc, device_profiles_disable_active);
	tcase_add_test(tc, device_profiles_ref_unref);
	tcase_add_test(tc, device_profiles_num_0);
	tcase_add_test(tc, device_profiles_multiple_active);
	tcase_add_test(tc, device_profiles_get_invalid);
	tcase_add_test(tc, device_freed_before_profile);
	tcase_add_test(tc, device_and_profile_freed_before_button);
	tcase_add_test(tc, device_and_profile_freed_before_resolution);
	tcase_add_test(tc, device_and_profile_and_button_freed_before_resolution);
	tcase_add_test(tc, device_and_profile_and_resolution_freed_before_button);
	suite_add_tcase(s, tc);

	tc = tcase_create("resolutions");
	tcase_add_test(tc, device_resolutions);
	tcase_add_test(tc, device_resolutions_ref_unref);
	tcase_add_test(tc, device_resolutions_num_0);
	suite_add_tcase(s, tc);

	tc = tcase_create("buttons");
	tcase_add_test(tc, device_buttons);
	tcase_add_test(tc, device_buttons_ref_unref);
	tcase_add_test(tc, device_buttons_set);
	suite_add_tcase(s, tc);

	tc = tcase_create("led");
	tcase_add_test(tc, device_leds);
	tcase_add_test(tc, device_leds_set);
	suite_add_tcase(s, tc);

	return s;
}

int main(void)
{
	int nfailed;
	Suite *s;
	SRunner *sr;
	const struct rlimit corelimit = { 0, 0 };

	setenv("RATBAG_TEST", "1", 0);

	setrlimit(RLIMIT_CORE, &corelimit);

	s = test_context_suite();
        sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	nfailed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nfailed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
