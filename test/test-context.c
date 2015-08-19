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
#include <stdlib.h>
#include <unistd.h>

#include "libratbag.h"

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);

	if (fd < 0)
		fprintf(stderr, "Failed to open %s (%s)\n",
			path, strerror(errno));

	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

struct ratbag_interface simple_iface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

START_TEST(context_init_NULL)
{
	struct ratbag *lr;
	lr = ratbag_create_context(NULL, NULL);
	ck_assert(lr == NULL);
}
END_TEST

START_TEST(context_init_bad_iface)
{
	struct ratbag *lr;
	struct ratbag_interface iface = {
		.open_restricted = NULL,
		.close_restricted = NULL,
	};

	lr = ratbag_create_context(&iface, NULL);
	ck_assert(lr == NULL);

	iface.open_restricted = open_restricted;
	lr = ratbag_create_context(&iface, NULL);
	ck_assert(lr == NULL);

	iface.open_restricted = NULL;
	iface.close_restricted = close_restricted;
	lr = ratbag_create_context(&iface, NULL);
	ck_assert(lr == NULL);
}
END_TEST

START_TEST(context_init)
{
	struct ratbag *lr;

	lr = ratbag_create_context(&simple_iface, NULL);
	ck_assert(lr != NULL);
	ratbag_unref(lr);
}
END_TEST

START_TEST(context_ref)
{
	struct ratbag *lr;
	struct ratbag *lr2;

	lr = ratbag_create_context(&simple_iface, NULL);
	ck_assert(lr != NULL);

	lr2 = ratbag_ref(lr);
	ck_assert_ptr_eq(lr, lr2);

	lr2 = ratbag_unref(lr2);
	ck_assert_ptr_eq(lr2, lr);

	lr2 = ratbag_unref(lr2);
	ck_assert_ptr_eq(lr2, NULL);
}
END_TEST

static Suite *
test_context_suite(void)
{
	TCase *tc;
	Suite *s;

	s = suite_create("context");
	tc = tcase_create("init");
	tcase_add_test(tc, context_init_NULL);
	tcase_add_test(tc, context_init_bad_iface);
	tcase_add_test(tc, context_init);
	tcase_add_test(tc, context_ref);
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
