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
#include <dirent.h>
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

struct ratbag_cmd {
	const char *name;
	int (*cmd)(struct ratbag *ratbag, uint32_t flags, int argc, char **argv);
	const char *args;
	const char *help;
};

static const struct ratbag_cmd *ratbag_commands[];

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
	unsigned i = 0;
	int count;
	const struct ratbag_cmd *cmd = ratbag_commands[0];

	printf("Usage: %s [options] [command] /sys/class/input/eventX\n"
	       "/path/to/device ..... Open the given device only\n"
	       "\n"
	       "Commands:\n",
		program_invocation_short_name);

	while (cmd) {
		count = 20 - strlen(cmd->name);
		if (cmd->args)
			count -= 1 + strlen(cmd->args);
		if (count < 4)
			count = 4;
		printf("    %s%s%s %.*s %s\n",
		       cmd->name,
		       cmd->args ? " " : "",
		       cmd->args ? cmd->args : "",
		       count, "....................", cmd->help);
		cmd = ratbag_commands[++i];
	}

	printf("\n"
	       "Options:\n"
	       "    --verbose ....... Print debugging output.\n"
	       "    --help .......... Print this help.\n");
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

	/* Macro */
	case RATBAG_BUTTON_TYPE_MACRO:			str = "macro"; break;

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

	/* disabled button */
	case RATBAG_BUTTON_TYPE_NONE:		str = "none"; break;
	}

	return str;
}

static struct ratbag_device *
ratbag_cmd_open_device(struct ratbag *ratbag, const char *path)
{
	struct ratbag_device *device;
	struct udev *udev;
	struct udev_device *udev_device;

	udev = udev_new();
	udev_device = udev_device_from_path(udev, path);
	if (!udev_device) {
		udev_unref(udev);
		return NULL;
	}

	device = ratbag_device_new_from_udev_device(ratbag, udev_device);

	udev_device_unref(udev_device);
	udev_unref(udev);

	return device;
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

	if (argc != 1) {
		usage();
		return 1;
	}

	path = argv[0];

	device = ratbag_cmd_open_device(ratbag, path);
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

		for (b = 0; b < num_buttons; b++) {
			enum ratbag_button_type type;

			button = ratbag_profile_get_button_by_index(profile, b);
			type = ratbag_button_get_type(button);
			printf("Button: %d type %s\n", b, button_type_to_str(type));
			button = ratbag_button_unref(button);
		}

		profile = ratbag_profile_unref(profile);
	}

	active_profile = ratbag_profile_unref(active_profile);
	device = ratbag_device_unref(device);

	rc = 0;
out:
	return rc;
}

static const struct ratbag_cmd cmd_info = {
	.name = "info",
	.cmd = ratbag_cmd_info,
	.args = NULL,
	.help = "Show information about the device's capabilities",
};

static int
ratbag_cmd_switch_profile(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *device;
	struct ratbag_profile *profile, *active_profile;
	int num_profiles, index;
	int rc = 1;

	if (argc != 2) {
		usage();
		return 1;
	}

	path = argv[1];
	index = atoi(argv[0]);

	device = ratbag_cmd_open_device(ratbag, path);
	if (!device) {
		error("Looks like '%s' is not supported\n", path);
		return 1;
	}

	if (!ratbag_device_has_capability(device,
					  RATBAG_CAP_SWITCHABLE_PROFILE)) {
		error("Looks like '%s' has no switchable profiles\n", path);
		goto out;
	}

	num_profiles = ratbag_device_get_num_profiles(device);
	if (index > num_profiles) {
		error("'%s' is not a valid profile\n", argv[0]);
		goto out;
	}

	active_profile = ratbag_device_get_active_profile(device);
	profile = ratbag_device_get_profile_by_index(device, index);
	if (!profile || !active_profile) {
		error("Huh hoh, something bad happened, unable to retrieve the profile '%d' \n",
		      index);
		goto out;
	}

	if (active_profile != profile) {
		rc = ratbag_device_set_active_profile(profile);
		if (!rc)
			printf("Switched '%s' to profile '%d'\n",
			       ratbag_device_get_name(device), index);
	} else {
		printf("'%s' is already in profile '%d'\n",
		       ratbag_device_get_name(device), index);
	}

	profile = ratbag_profile_unref(profile);
	active_profile = ratbag_profile_unref(active_profile);

out:
	device = ratbag_device_unref(device);
	return rc;
}

static const struct ratbag_cmd cmd_switch_profile = {
	.name = "switch-profile",
	.cmd = ratbag_cmd_switch_profile,
	.args = "N",
	.help = "Set the current active profile to N",
};

static int
ratbag_cmd_switch_etekcity(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *device;
	struct ratbag_button *button_6, *button_7;
	struct ratbag_profile *profile = NULL;
	int rc = 1, commit = 0;

	if (argc != 1) {
		usage();
		return 1;
	}

	path = argv[0];

	device = ratbag_cmd_open_device(ratbag, path);
	if (!device) {
		error("Looks like '%s' is not supported\n", path);
		return 1;
	}

	if (!ratbag_device_has_capability(device,
					  RATBAG_CAP_SWITCHABLE_PROFILE)) {
		error("Looks like '%s' has no switchable profiles\n", path);
		goto out;
	}

	profile = ratbag_device_get_active_profile(device);
	if (!profile) {
		error("Huh hoh, something bad happened, unable to retrieve the active profile\n");
		goto out;
	}

	button_6 = ratbag_profile_get_button_by_index(profile, 6);
	button_7 = ratbag_profile_get_button_by_index(profile, 7);

	if (ratbag_button_get_type(button_6) == RATBAG_BUTTON_TYPE_KEY_VOLUMEUP &&
	    ratbag_button_get_type(button_7) == RATBAG_BUTTON_TYPE_KEY_VOLUMEDOWN) {
		ratbag_button_set_type(button_6, RATBAG_BUTTON_TYPE_NONE);
		ratbag_button_set_type(button_7, RATBAG_BUTTON_TYPE_NONE);
		commit = 1;
	} else if (ratbag_button_get_type(button_6) == RATBAG_BUTTON_TYPE_NONE &&
		   ratbag_button_get_type(button_7) == RATBAG_BUTTON_TYPE_NONE) {
		ratbag_button_set_type(button_6, RATBAG_BUTTON_TYPE_KEY_VOLUMEUP);
		ratbag_button_set_type(button_7, RATBAG_BUTTON_TYPE_KEY_VOLUMEDOWN);
		commit = 2;
	}

	button_6 = ratbag_button_unref(button_6);
	button_7 = ratbag_button_unref(button_7);

	if (!commit)
		goto out;

	rc = ratbag_device_set_active_profile(profile);
	if (!rc)
		printf("Switched the current profile of '%s' to %sreport the volume keys\n",
		       ratbag_device_get_name(device),
		       commit == 1 ? "not " : "");

out:
	profile = ratbag_profile_unref(profile);

	device = ratbag_device_unref(device);
	return rc;
}

static const struct ratbag_cmd cmd_switch_etekcity = {
	.name = "switch-etekcity",
	.cmd = ratbag_cmd_switch_etekcity,
	.args = NULL,
	.help = "Switch the Etekcity mouse active profile",
};

static int
ratbag_cmd_change_button(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *device;
	struct ratbag_button *button = NULL;
	struct ratbag_profile *profile = NULL;
	int button_index, type_index;
	int rc = 1, commit = 0;

	if (argc != 3) {
		usage();
		return 1;
	}

	button_index = atoi(argv[0]);
	type_index = atoi(argv[1]);
	path = argv[2];

	device = ratbag_cmd_open_device(ratbag, path);
	if (!device) {
		error("Looks like '%s' is not supported\n", path);
		return 1;
	}

	if (!ratbag_device_has_capability(device,
					  RATBAG_CAP_BUTTON_KEY)) {
		error("Looks like '%s' has no programmable buttons\n", path);
		goto out;
	}

	profile = ratbag_device_get_active_profile(device);
	if (!profile) {
		error("Huh hoh, something bad happened, unable to retrieve the active profile\n");
		goto out;
	}

	button = ratbag_profile_get_button_by_index(profile, button_index);

	if (ratbag_button_get_type(button) != type_index) {
		rc = ratbag_button_set_type(button, type_index);
		if (rc) {
			error("Unable to map button %d to '%s' (%d): %s (%d)\n",
			      button_index,
			      button_type_to_str(type_index),
			      button_index,
			      strerror(-rc),
			      rc);
			goto out;
		}

		rc = ratbag_device_set_active_profile(profile);
		if (rc) {
			error("Unable to apply the current profile: %s (%d)\n",
			      strerror(-rc),
			      rc);
			goto out;
		}
		printf("Switched the current profile of '%s' to report '%s' when button %d is pressed\n",
		       ratbag_device_get_name(device),
		       button_type_to_str(type_index),
		       button_index);
	} else {
		printf("The current profile of '%s' alread reports '%s' when button %d is pressed\n",
		       ratbag_device_get_name(device),
		       button_type_to_str(type_index),
		       button_index);
	}

out:
	button = ratbag_button_unref(button);
	profile = ratbag_profile_unref(profile);

	device = ratbag_device_unref(device);
	return rc;
}

static const struct ratbag_cmd cmd_change_button = {
	.name = "change-button",
	.cmd = ratbag_cmd_change_button,
	.args = "X Y",
	.help = "Remap button X to Y in the active profile",
};

static int
filter_event_node(const struct dirent *input_entry)
{
	return strneq(input_entry->d_name, "event", 5);
}

static int
ratbag_cmd_list_supported_devices(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	struct dirent **input_list;
	struct ratbag_device *device;
	char path[256];
	int n, i;
	int supported = 0;

	if (argc != 0) {
		usage();
		return 1;
	}

	n = scandir("/dev/input", &input_list, filter_event_node, alphasort);
	if (n < 0)
		return 0;

	i = -1;
	while (++i < n) {
		sprintf(path, "/dev/input/%s", input_list[i]->d_name);
		device = ratbag_cmd_open_device(ratbag, path);
		if (device) {
			printf("%s:\t%s\n", path, ratbag_device_get_name(device));
			device = ratbag_device_unref(device);
			supported++;
		}
		free(input_list[i]);
	}
	free(input_list);

	if (!supported)
		printf("No supported devices found\n");

	return 0;
}

static const struct ratbag_cmd cmd_list = {
	.name = "list",
	.cmd = ratbag_cmd_list_supported_devices,
	.args = NULL,
	.help = "List the available devices",
};

static int
ratbag_cmd_switch_dpi(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *device;
	struct ratbag_button *button_6, *button_7;
	struct ratbag_profile *profile = NULL;
	int rc = 1, commit = 0;
	int dpi;

	if (argc != 2) {
		usage();
		return 1;
	}

	dpi = atoi(argv[0]);
	path = argv[1];

	device = ratbag_cmd_open_device(ratbag, path);
	if (!device) {
		error("Looks like '%s' is not supported\n", path);
		return 1;
	}

	if (!ratbag_device_has_capability(device,
					  RATBAG_CAP_SWITCHABLE_RESOLUTION)) {
		error("Looks like '%s' has no switchable resolution\n", path);
		goto out;
	}

	profile = ratbag_device_get_active_profile(device);
	if (!profile) {
		error("Huh hoh, something bad happened, unable to retrieve the active profile\n");
		goto out;
	}

	rc = ratbag_profile_set_resolution_dpi(profile, dpi);
	if (!rc)
		printf("Switched the current resolution profile of '%s' to %d dpi\n",
		       ratbag_device_get_name(device),
		       dpi);
	else
		error("can't seem to be able to change the dpi: %s (%d)\n",
		      strerror(-rc),
		      rc);

out:
	profile = ratbag_profile_unref(profile);

	device = ratbag_device_unref(device);
	return rc;
}

static const struct ratbag_cmd cmd_switch_dpi = {
	.name = "switch-dpi",
	.cmd = ratbag_cmd_switch_dpi,
	.args = "N",
	.help = "Switch the resolution of the mouse in the active profile",
};

static const struct ratbag_cmd *ratbag_commands[] = {
	&cmd_info,
	&cmd_list,
	&cmd_change_button,
	&cmd_switch_etekcity,
	&cmd_switch_dpi,
	&cmd_switch_profile,
	NULL,
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
		ratbag_unref(ratbag);
		return 1;
	}

	if (flags & FLAG_VERBOSE)
		ratbag_log_set_priority(ratbag, RATBAG_LOG_PRIORITY_DEBUG);

	command = argv[optind++];
	ARRAY_FOR_EACH(ratbag_commands, cmd) {
		if (!*cmd || !streq((*cmd)->name, command))
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
