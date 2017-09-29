/*
 * Copyright © 2017 Red Hat, Inc.
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
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>

#include "libratbag-util.h"

START_TEST(dpi_range_parser)
{
	struct testcase {
		const char *str;
		bool success;
		struct dpi_range result;
	} tests[] = {
		{ "", false, { 0, 0, 0 }},
		{ "1", false, { 0, 0, 0 }},
		{ "a", false, { 0, 0, 0 }},
		{ "1:1", false, { 0, 0, 0 }},
		{ "2:1", false, { 0, 0, 0 }},
		{ "2:1@0", false, { 0, 0, 0 }},
		{ "10:100@0", false, { 0, 0, 0 }},
		{ "100:10@50", false, { 0, 0, 0 }},
		{ "100:10@", false, { 0, 0, 0 }},
		{ ":10@50", false, { 0, 0, 0 }},

		{ "10:100@50", true, { 10, 100, 50 }},
		{ "100:12000@20", true, { 100, 12000, 20 }},
		{ "50:12000@250", true, { 50, 12000, 250 }},
	};
	struct testcase *t;

	ARRAY_FOR_EACH(tests, t) {
		struct dpi_range *range;

		range = dpi_range_from_string(t->str);
		if (!t->success) {
			ck_assert(range == NULL);
			continue;
		}

		ck_assert(range != NULL);
		ck_assert_int_eq(t->result.min, range->min);
		ck_assert_int_eq(t->result.max, range->max);
		ck_assert_int_eq(t->result.step, range->step);

		free(range);
	}
}
END_TEST

START_TEST(dpi_list_parser)
{
	struct testcase {
		const char *str;
		int nentries;
		int entries[64];
	} tests[] = {
		{ "", -1, {0}},
		{ "a", -1, {0}},
		{ "a;b", -1, {0}},
		{ "1;a;b", -1, {0}},
		{ "100;200;b", -1, {0}},
		{ "10.2;200", -1, {0}},
		{ "0xab;100", -1, {0}},

		{ "100", 1, { 100 }},
		{ "100;200", 2, { 100, 200 }},
		{ "100;200;", 2, { 100, 200 }},
		{ "100;300;;;;", 2, { 100, 300 }},
		{ "0;300;", 2, { 0, 300 }},
		{ "0;300;400;", 3, { 0, 300, 400 }},
		{ "0;300;400;500;100;23;", 6, { 0, 300, 400, 500, 100, 23 }},
	};
	struct testcase *t;

	ARRAY_FOR_EACH(tests, t) {
		struct dpi_list *list;

		list = dpi_list_from_string(t->str);
		if (t->nentries == -1) {
			ck_assert(list == NULL);
			continue;
		}

		ck_assert(list != NULL);
		ck_assert_int_eq(t->nentries, list->nentries);
		ck_assert_int_eq(memcmp(t->entries, list->entries,
					t->nentries * sizeof(int)), 0);

		dpi_list_free(list);
	}
}
END_TEST

static Suite *
test_context_suite(void)
{
	TCase *tc;
	Suite *s;

	s = suite_create("device");
	tc = tcase_create("util");
	tcase_add_test(tc, dpi_range_parser);
	tcase_add_test(tc, dpi_list_parser);

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
