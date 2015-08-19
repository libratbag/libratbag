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
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>

#include <libratbag.h>
#include <libratbag-util.h>

enum options {
	OPT_VERBOSE,
	OPT_HELP,
};

enum cmd_flags {
	FLAG_VERBOSE = 1 << 0,
};

LIBRATBAG_ATTRIBUTE_PRINTF(1, 2)
static inline void
error(const char *format, ...)
{
	va_list args;

	fprintf(stderr, "Error: ");

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

static void
usage(void)
{
	printf("Usage: %s [options] [command] /sys/class/input/eventX\n"
	       "/path/to/device .... open the given device only\n"
	       "\n"
	       "Commands:\n"
	       "    info .... show information about the device's capabilities\n"
	       "\n"
	       "Options:\n"
	       "    --verbose ....... Print debugging output.\n"
	       "    --help .......... Print this help.\n",
		program_invocation_short_name);
}

static inline struct udev_device*
udev_device_from_path(struct udev *udev, const char *path)
{
	struct udev_device *udev_device;
	const char *event_node_prefix = "/dev/input/event";

	if (strncmp(path, event_node_prefix, strlen(event_node_prefix)) == 0) {
		struct stat st;
		if (stat(path, &st) == -1) {
			error("Failed to stat '%s': %s\n", path, strerror(errno));
			return NULL;
		}
		udev_device = udev_device_new_from_devnum(udev, 'c', st.st_rdev);

	} else {
		udev_device = udev_device_new_from_syspath(udev, path);
	}
	if (!udev_device) {
		error("Can't open '%s': %s\n", path, strerror(errno));
		return NULL;
	}

	return udev_device;
}

static int
ratbag_cmd_info(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *rb;
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	int i;
	int rc = 1;
	struct udev *udev;
	struct udev_device *udev_device;

	if (argc != 1) {
		usage();
		return 1;
	}

	path = argv[0];

	udev = udev_new();
	udev_device = udev_device_from_path(udev, path);
	if (!udev_device)
		return 1;

	rb = ratbag_device_new_from_udev_device(ratbag, udev_device);
	if (!rb) {
		error("Looks like '%s' is not supported\n", path);
		goto out;
	}

	error("Opened '%s' (%s).\n", ratbag_device_get_name(rb), path);

	profile = ratbag_device_get_profile_by_index(rb, 0);
	ratbag_device_set_active_profile(rb, profile);
	profile = ratbag_profile_unref(profile);

	profile = ratbag_device_get_active_profile(rb);
	for (i = 0; i < ratbag_device_get_num_buttons(rb); i++) {
		button = ratbag_profile_get_button_by_index(profile, i);
		button = ratbag_button_unref(button);
	}
	profile = ratbag_profile_unref(profile);
	rb = ratbag_device_unref(rb);

	rc = 0;
out:
	udev_device_unref(udev_device);
	udev_unref(udev);
	return rc;
}

struct ratbag_cmd {
	const char *name;
	int (*cmd)(struct ratbag *ratbag, uint32_t flags, int argc, char **argv);
};

const struct ratbag_cmd cmd_info = {
	.name = "info",
	.cmd = ratbag_cmd_info,
};

static const struct ratbag_cmd *ratbag_commands[] = {
	&cmd_info,
};

static int
open_restricted(const char *path, int flags, void *user_data)
{
	int fd = open(path, flags);

	if (fd < 0)
		error("Failed to open %s (%s)\n",
			path, strerror(errno));

	return fd < 0 ? -errno : fd;
}

static void
close_restricted(int fd, void *user_data)
{
	close(fd);
}

const struct ratbag_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

int
main(int argc, char **argv)
{
	struct ratbag *ratbag;
	const char *command;
	int rc = 0;
	const struct ratbag_cmd **cmd;
	uint32_t flags = 0;

	ratbag = ratbag_create_context(&interface, NULL);
	if (!ratbag) {
		error("Can't initialize ratbag\n");
		goto out;
	}

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
			usage();
			goto out;
		case OPT_VERBOSE:
			flags |= FLAG_VERBOSE;
			break;
		default:
			usage();
			return 1;
		}
	}

	if (optind >= argc) {
		usage();
		return 1;
	}

	command = argv[optind++];
	ARRAY_FOR_EACH(ratbag_commands, cmd) {
		if (!streq((*cmd)->name, command))
			continue;

		argc -= optind;
		argv += optind;

		/* reset optind to reset the internal state, see NOTES in
		 * getopt(3) */
		optind = 0;
		rc = (*cmd)->cmd(ratbag, flags, argc, argv);
		goto out;
	}

	error("Invalid command '%s'\n", command);
	usage();
	rc = 1;

out:
	ratbag_unref(ratbag);

	return rc;
}
