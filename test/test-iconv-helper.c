/*
 * Copyright © 2016 Red Hat, Inc.
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
#include <string.h>
#include <errno.h>
#include <sys/resource.h>

#include "libratbag-util.h"

static const char sample_utf16le[] = { 'F', '\0', 'o', '\0', 'o', '\0' };
static const char sample_single_char_utf16le[] = { 'A', '\0' };

/* Sample UTF-8 string used: 🐺🖖🗺🗹💯👏 */
static const char sample_emoji_utf8[] = {
	0xf0, 0x9f, 0x90, 0xba, 0xf0, 0x9f, 0x96, 0x96, 0xf0, 0x9f, 0x97, 0xba,
	0xf0, 0x9f, 0x97, 0xb9, 0xf0, 0x9f, 0x92, 0xaf, 0xf0, 0x9f, 0x91, 0x8f,
	0x0a, 0x00
};

static const char sample_emoji_utf16le[] = {
	0x3d, 0xd8, 0x3a, 0xdc, 0x3d, 0xd8, 0x96, 0xdd, 0x3d, 0xd8, 0xfa, 0xdd,
	0x3d, 0xd8, 0xf9, 0xdd, 0x3d, 0xd8, 0xaf, 0xdc, 0x3d, 0xd8, 0x4f, 0xdc,
	0x0a, 0x00
};

START_TEST(iconv_convert_to_utf16le)
{
	char output[4096];
	ssize_t rc;

	rc = ratbag_utf8_to_enc(output, sizeof(sample_utf16le),
				"UTF-16LE", "%s", "Foo");

	ck_assert_int_eq(rc, sizeof(sample_utf16le));
	ck_assert(memcmp(output, sample_utf16le, sizeof(sample_utf16le)) == 0);

	rc = ratbag_utf8_to_enc(output, sizeof(sample_emoji_utf16le),
				"UTF-16LE", "%s", sample_emoji_utf8);

	ck_assert_int_eq(rc, sizeof(sample_emoji_utf16le));
	ck_assert(memcmp(output, sample_emoji_utf16le,
			 sizeof(sample_emoji_utf16le)) == 0);

	rc = ratbag_utf8_to_enc(output, sizeof(sample_single_char_utf16le),
				"UTF-16LE", "%s", "A");

	ck_assert_int_eq(rc, sizeof(sample_single_char_utf16le));
	ck_assert(memcmp(output, sample_single_char_utf16le,
			 sizeof(sample_single_char_utf16le)) == 0);
}
END_TEST

START_TEST(iconv_convert_from_utf16le)
{
	char input[4096];
	char *output;
	ssize_t rc;

	memcpy(input, sample_utf16le, sizeof(sample_utf16le));
	rc = ratbag_utf8_from_enc(input, sizeof(sample_utf16le),
				  "UTF-16LE", &output);

	ck_assert_int_eq(rc, sizeof("Foo"));
	ck_assert(memcmp(output, "Foo", sizeof("Foo")) == 0);
	free(output);

	memcpy(input, sample_emoji_utf16le, sizeof(sample_emoji_utf16le));
	rc = ratbag_utf8_from_enc(input, sizeof(sample_emoji_utf16le),
				  "UTF-16LE", &output);

	ck_assert_int_eq(rc, sizeof(sample_emoji_utf8));
	ck_assert(memcmp(output, sample_emoji_utf8,
			 sizeof(sample_emoji_utf8)) == 0);
	free(output);

	memcpy(input, sample_single_char_utf16le,
	       sizeof(sample_single_char_utf16le));
	rc = ratbag_utf8_from_enc(input, sizeof(sample_single_char_utf16le),
				  "UTF-16LE", &output);

	ck_assert_int_eq(rc, sizeof("A"));
	ck_assert(memcmp(output, "A", sizeof("A")) == 0);
	free(output);
}
END_TEST

START_TEST(iconv_invalid_encoding)
{
	char output[10] = { 0 };
	ssize_t rc;

	rc = ratbag_utf8_to_enc(output, sizeof(output),
				"This encoding is invalid", "%s", "Foo");

	ck_assert_int_eq(rc, -EINVAL);
}
END_TEST

START_TEST(iconv_bad_utf16le)
{
	char odd_numbered[] = { 'F', '\0', 'o' };
	char single_char[] = { 'F' };
	char single_null[] = { '\0' };
	char double_null[] = { 'F', '\0', '\0', 'o', '\0', 'o', '\0' };
	char *output;
	ssize_t rc;

	rc = ratbag_utf8_from_enc(odd_numbered, sizeof(odd_numbered),
				  "UTF-16LE", &output);
	ck_assert_int_eq(rc, -EINVAL);

	rc = ratbag_utf8_from_enc(single_char, sizeof(single_char), "UTF-16LE",
				  &output);
	ck_assert_int_eq(rc, -EINVAL);

	rc = ratbag_utf8_from_enc(single_null, sizeof(single_null), "UTF-16LE",
				  &output);
	ck_assert_int_eq(rc, -EINVAL);

	rc = ratbag_utf8_from_enc(double_null, sizeof(double_null), "UTF-16LE",
				  &output);
	ck_assert_int_eq(rc, -EINVAL);
}
END_TEST

static Suite *
test_context_suite(void)
{
	TCase *tc;
	Suite *s;

	s = suite_create("iconv-helper");
	tc = tcase_create("iconv-helper");
	tcase_add_test(tc, iconv_convert_to_utf16le);
	tcase_add_test(tc, iconv_convert_from_utf16le);
	tcase_add_test(tc, iconv_invalid_encoding);
	tcase_add_test(tc, iconv_bad_utf16le);
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
