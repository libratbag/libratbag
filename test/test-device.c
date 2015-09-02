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

#include "libratbag.h"
#include "libratbag-test.h"

/* A pre-setup sane device. Use for sanity testing by toggling the various
 * error conditions.
 */
const struct ratbag_test_device sane_device = {
	.num_profiles = 3,
	.num_buttons = 1,
	.profiles = {
		{
		.num_resolutions = 3,
		.resolutions = {
			{ .xres = 100, .yres = 200, .hz = 1000 },
			{ .xres = 200, .yres = 300, .hz = 1000 },
			{ .xres = 300, .yres = 400, .hz = 1000 },
		},
		.active = true,
		.dflt = false,
		},
		{
		.num_resolutions = 3,
		.resolutions = {
			{ .xres = 1100, .yres = 1200, .hz = 2000 },
			{ .xres = 1200, .yres = 1300, .hz = 2000 },
			{ .xres = 1300, .yres = 1400, .hz = 2000 },
		},
		.active = false,
		.dflt = true,
		},
		{
		.num_resolutions = 3,
		.resolutions = {
			{ .xres = 2100, .yres = 2200, .hz = 3000 },
			{ .xres = 2200, .yres = 2300, .hz = 3000 },
			{ .xres = 2300, .yres = 2400, .hz = 3000 },
		},
		.active = false,
		.dflt = false,
		},
	},
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
	int nprofiles, nbuttons;
	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);
	ck_assert(d != NULL);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);
	nbuttons = ratbag_device_get_num_buttons(d);
	ck_assert_int_eq(nbuttons, 1);

	ratbag_device_unref(d);
	ratbag_unref(r);
}
END_TEST

START_TEST(device_profiles)
{
	struct ratbag *r;
	struct ratbag_device *d;
	struct ratbag_profile *p;
	int nprofiles;
	int i;
	bool is_active, is_default;

	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	for (i = 0; i < nprofiles; i++) {
		p = ratbag_device_get_profile_by_index(d, i);
		ck_assert(p != NULL);

		is_active = ratbag_profile_is_active(p);
		ck_assert_int_eq(is_active, (i == 0));
		is_default = ratbag_profile_is_default(p);
		ck_assert_int_eq(is_default, (i == 1));
		ratbag_profile_unref(p);
	}
	ratbag_device_unref(d);
	ratbag_unref(r);
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

	struct ratbag_test_device td = sane_device;

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	ck_assert_int_eq(nprofiles, 3);

	p = ratbag_device_get_profile_by_index(d, nprofiles);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile_by_index(d, nprofiles + 1);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile_by_index(d, -1);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile_by_index(d, INT_MAX);
	ck_assert(p == NULL);
	p = ratbag_device_get_profile_by_index(d, UINT_MAX);
	ck_assert(p == NULL);

	ratbag_device_unref(d);
	ratbag_unref(r);
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

	struct ratbag_test_device td = {
		.num_profiles = 3,
		.num_buttons = 1,
		.profiles = {
			{
			.num_resolutions = 3,
			.resolutions = {
				{ .xres = 100, .yres = 200, .hz = 1000 },
				{ .xres = 200, .yres = 300, .hz = 1000 },
				{ .xres = 300, .yres = 400, .hz = 1000 },
			},
			.active = true,
			},
			{
			.num_resolutions = 3,
			.resolutions = {
				{ .xres = 1100, .yres = 1200, .hz = 2000 },
				{ .xres = 1200, .yres = 1300, .hz = 2000 },
				{ .xres = 1300, .yres = 1400, .hz = 2000 },
			},
			},
			{
			.num_resolutions = 3,
			.resolutions = {
				{ .xres = 2100, .yres = 2200, .hz = 3000 },
				{ .xres = 2200, .yres = 2300, .hz = 3000 },
				{ .xres = 2300, .yres = 2400, .hz = 3000 },
			},
			},
		},
	};

	r = ratbag_create_context(&abort_iface, NULL);
	d = ratbag_device_new_test_device(r, &td);

	nprofiles = ratbag_device_get_num_profiles(d);
	for (i = 0; i < nprofiles; i++) {
		p = ratbag_device_get_profile_by_index(d, i);
		nresolutions = ratbag_profile_get_num_resolutions(p);
		ck_assert_int_eq(nresolutions, 3);

		for (j = 0; j < nresolutions; j++) {
			res = ratbag_profile_get_resolution(p, j);

			xres = ratbag_resolution_get_dpi_x(res);
			yres = ratbag_resolution_get_dpi_y(res);
			rate = ratbag_resolution_get_report_rate(res);

			ck_assert_int_eq(xres, i * 1000 + (j + 1) * 100);
			ck_assert_int_eq(yres, i * 1000 + (j + 1) * 100 + 100);
			ck_assert_int_eq(xres, ratbag_resolution_get_dpi(res));

			ck_assert_int_eq(rate, (i + 1) * 1000);

			ratbag_resolution_unref(res);
		}

		ratbag_profile_unref(p);
	}
	ratbag_device_unref(d);
	ratbag_unref(r);
}
END_TEST

START_TEST(device_resolutions_num_0)
{
	struct ratbag *r;
	struct ratbag_device *d;

	struct ratbag_test_device td = {
		.num_profiles = 1,
		.num_buttons = 1,
		.profiles = {
			{
			.num_resolutions = 0, /* failure trigger */
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

static Suite *
test_context_suite(void)
{
	TCase *tc;
	Suite *s;

	s = suite_create("device");
	tc = tcase_create("device");
	tcase_add_test(tc, device_init);
	suite_add_tcase(s, tc);

	tc = tcase_create("profiles");
	tcase_add_test(tc, device_profiles);
	tcase_add_test(tc, device_profiles_num_0);
	tcase_add_test(tc, device_profiles_multiple_active);
	tcase_add_test(tc, device_profiles_get_invalid);
	suite_add_tcase(s, tc);

	tc = tcase_create("resolutions");
	tcase_add_test(tc, device_resolutions);
	tcase_add_test(tc, device_resolutions_num_0);
	suite_add_tcase(s, tc);

	return s;
}

int main(void)
{
	int nfailed;
	Suite *s;
	SRunner *sr;

	s = test_context_suite();
        sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	nfailed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (nfailed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
