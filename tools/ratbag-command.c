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

static inline const char*
button_type_to_str(enum ratbag_button_type type)
{
	const char *str = "UNKNOWN";

	switch(type) {
	case RATBAG_BUTTON_TYPE_UNKNOWN:	str = "unknown"; break;
	case RATBAG_BUTTON_TYPE_LEFT:		str = "left"; break;
	case RATBAG_BUTTON_TYPE_MIDDLE:		str = "middle"; break;
	case RATBAG_BUTTON_TYPE_RIGHT:		str = "right"; break;
	case RATBAG_BUTTON_TYPE_THUMB:		str = "thumb"; break;
	case RATBAG_BUTTON_TYPE_THUMB2:		str = "thumb2"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_LEFT:	str = "wheel left"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_RIGHT:	str = "wheel right"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_CLICK:	str = "wheel click"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_UP:	str = "wheel up"; break;
	case RATBAG_BUTTON_TYPE_WHEEL_DOWN:	str = "wheel down"; break;
	case RATBAG_BUTTON_TYPE_EXTRA:		str = "extra (forward)"; break;
	case RATBAG_BUTTON_TYPE_SIDE:		str = "side (backward)"; break;

	/* DPI switch */
	case RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP:	str = "resolution cycle up"; break;
	case RATBAG_BUTTON_TYPE_RESOLUTION_UP:		str = "resolution up"; break;
	case RATBAG_BUTTON_TYPE_RESOLUTION_DOWN:	str = "resolution down"; break;

	/* Profile */
	case RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP:	str = "profile cycle up"; break;
	case RATBAG_BUTTON_TYPE_PROFILE_UP:		str = "profile up"; break;
	case RATBAG_BUTTON_TYPE_PROFILE_DOWN:		str = "profile down"; break;

	/* multimedia */
	case RATBAG_BUTTON_TYPE_KEY_CONFIG:		str = "key config"; break;
	case RATBAG_BUTTON_TYPE_KEY_PREVIOUSSONG:	str = "key previous song"; break;
	case RATBAG_BUTTON_TYPE_KEY_NEXTSONG:		str = "key next song"; break;
	case RATBAG_BUTTON_TYPE_KEY_PLAYPAUSE:		str = "key play/pause"; break;
	case RATBAG_BUTTON_TYPE_KEY_STOPCD:		str = "key stop"; break;
	case RATBAG_BUTTON_TYPE_KEY_MUTE:		str = "key mute"; break;
	case RATBAG_BUTTON_TYPE_KEY_VOLUMEUP:		str = "key volume up"; break;
	case RATBAG_BUTTON_TYPE_KEY_VOLUMEDOWN:		str = "key volume down"; break;

	/* desktop */
	case RATBAG_BUTTON_TYPE_KEY_CALC:	str = "key calc"; break;
	case RATBAG_BUTTON_TYPE_KEY_MAIL:	str = "key mail"; break;
	case RATBAG_BUTTON_TYPE_KEY_BOOKMARKS:	str = "key bookmarks"; break;
	case RATBAG_BUTTON_TYPE_KEY_FORWARD:	str = "key forward"; break;
	case RATBAG_BUTTON_TYPE_KEY_BACK:	str = "key back"; break;
	case RATBAG_BUTTON_TYPE_KEY_STOP:	str = "key stop"; break;
	case RATBAG_BUTTON_TYPE_KEY_FILE:	str = "key file"; break;
	case RATBAG_BUTTON_TYPE_KEY_REFRESH:	str = "key refresh"; break;
	case RATBAG_BUTTON_TYPE_KEY_HOMEPAGE:	str = "key homepage"; break;
	case RATBAG_BUTTON_TYPE_KEY_SEARCH:	str = "key search"; break;
	}

	return str;
}

static int
ratbag_cmd_info(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *device;
	struct ratbag_profile *profile, *active_profile;
	struct ratbag_button *button;
	int num_profiles, num_buttons;
	int i, b;
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

	device = ratbag_device_new_from_udev_device(ratbag, udev_device);
	if (!device) {
		error("Looks like '%s' is not supported\n", path);
		goto out;
	}

	printf("Device '%s' (%s)\n", ratbag_device_get_name(device), path);
	active_profile = ratbag_device_get_active_profile(device);

	printf("Capabilities:");
	if (ratbag_device_has_capability(device,
					 RATBAG_CAP_SWITCHABLE_RESOLUTION))
		printf(" res");
	if (ratbag_device_has_capability(device,
					 RATBAG_CAP_SWITCHABLE_PROFILE))
		printf(" profile");
	if (ratbag_device_has_capability(device,
					 RATBAG_CAP_BUTTON_PROFILES))
		printf(" btn-profile");
	if (ratbag_device_has_capability(device,
					 RATBAG_CAP_BUTTON_KEY))
		printf(" btn-key");
	if (ratbag_device_has_capability(device,
					 RATBAG_CAP_BUTTON_MACROS))
		printf(" btn-macros");
	printf("\n");

	num_buttons = ratbag_device_get_num_buttons(device);
	printf("Number of buttons: %d\n", num_buttons);

	num_profiles = ratbag_device_get_num_profiles(device);
	printf("Profiles supported: %d\n", num_profiles);

	for (i = 0; i < num_profiles; i++) {
		int dpi, rate;
		profile = ratbag_device_get_profile_by_index(device, i);

		dpi = ratbag_profile_get_resolution_dpi(profile);
		rate = ratbag_profile_get_report_rate_hz(profile);
		printf("  Profile %d%s\n", i,
		       profile == active_profile ? " (active)" : "");
		printf("    Resolution: %ddpi\n", dpi);
		printf("    Report rate: %dhz\n", rate);

		ratbag_profile_unref(profile);

		for (b = 0; b < num_buttons; b++) {
			enum ratbag_button_type type;

			button = ratbag_profile_get_button_by_index(profile, b);
			type = ratbag_button_get_type(button);
			printf("Button: %d type %s\n", b, button_type_to_str(type));
			button = ratbag_button_unref(button);
		}
	}
	profile = ratbag_profile_unref(profile);
	device = ratbag_device_unref(device);

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

	if (flags & FLAG_VERBOSE)
		ratbag_log_set_priority(ratbag, RATBAG_LOG_PRIORITY_DEBUG);

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
