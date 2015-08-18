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
#include <getopt.h>

#include <libratbag.h>

enum options {
	OPT_VERBOSE,
	OPT_HELP,
};

static void
list_ratbags_usage(void)
{
	printf("Usage: %s [options] /sys/class/input/eventX\n"
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
parse_args(int argc, char **argv, const char **path, int *verbose)
{
	while (1) {
		int c;
		int option_index = 0;
		static struct option opts[] = {
			{ "verbose", 0, 0, OPT_VERBOSE },
			{ "help", 0, 0, OPT_HELP },
		};

		c = getopt_long(argc, argv, "+h", opts, &option_index);
		if (c == -1)
			break;
		switch(c) {
		case 'h':
		case OPT_HELP:
			list_ratbags_usage();
			exit(0);
			break;
		case OPT_VERBOSE:
			*verbose = 1;
			break;
		default:
			list_ratbags_usage();
			return -1;
		}
	}

	if (optind >= argc) {
		list_ratbags_usage();
		return -1;
	}

	*path = argv[optind++];

	return 0;
}

int
main(int argc, char **argv)
{
	struct ratbag *rb;
	struct libratbag *libratbag;
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	const char *path;
	int i;
	int retval = 0;
	struct udev *udev;
	struct udev_device *udev_device;
	int verbose = 0;

	if (parse_args(argc, argv, &path, &verbose) == -1)
		return 1;

	udev = udev_new();
	udev_device = udev_device_new_from_syspath(udev, path);
	if (!udev_device) {
		fprintf(stderr, "Can't open '%s': %s\n", path, strerror(errno));
		return 1;
	}

	libratbag = libratbag_create_context();
	if (!libratbag) {
		fprintf(stderr, "Can't initilize libratbag\n");
		return 1;
	}

	rb = ratbag_new_from_udev_device(libratbag, udev_device);

	if (!rb) {
		fprintf(stderr, "Looks like '%s' is not supported\n", path);
		retval = 1;
		goto out;
	}

	fprintf(stderr, "Opened '%s' (%s).\n", ratbag_get_name(rb), path);

	profile = ratbag_get_profile_by_index(rb, 0);
	ratbag_set_active_profile(rb, profile);
	profile = ratbag_profile_unref(profile);

	profile = ratbag_get_active_profile(rb);
	for (i = 0; i < ratbag_get_num_buttons(rb); i++) {
		button = ratbag_profile_get_button_by_index(profile, i);
		button = ratbag_button_unref(button);
	}
	profile = ratbag_profile_unref(profile);

out:
	rb = ratbag_unref(rb);
	libratbag_unref(libratbag);
	udev_unref(udev);
	return retval;
}
