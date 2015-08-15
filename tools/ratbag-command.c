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

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <libratbag.h>

static void
list_ratbags_usage(void)
{
	printf("Usage: %s [options] /dev/input/eventX]\n"
	       "/path/to/device .... open the given device only\n"
	       "\n"
	       "Features:\n"
	       "\n"
	       "Other options:\n"
	       "--verbose ....... Print debugging output.\n"
	       "--help .......... Print this help.\n",
		program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	struct ratbag *rb;
	struct libratbag *libratbag;
	struct ratbag_profile *profile;
	char *path;
	int fd;
	int retval = 0;

	if (argc < 2) {
		list_ratbags_usage();
		return 1;
	}

	path = argv[1];
	fd = open(path, O_RDWR);
	if (fd <= 0) {
		fprintf(stderr, "Can't open '%s': %s\n", path, strerror(errno));
		return 1;
	}

	libratbag = libratbag_create_context();
	if (!libratbag) {
		fprintf(stderr, "Can't initilize libratbag\n");
		return 1;
	}

	rb = ratbag_new_from_fd(libratbag, fd);

	if (!rb) {
		fprintf(stderr, "Looks like '%s' is not supported\n", path);
		retval = 1;
		goto out;
	}

	fprintf(stderr, "Opened '%s' (%s).\n", ratbag_get_name(rb), path);

	profile = ratbag_get_active_profile(rb);

out:
	profile = ratbag_profile_unref(profile);
	rb = ratbag_unref(rb);
	libratbag_unref(libratbag);
	close(fd);
	return retval;
}
