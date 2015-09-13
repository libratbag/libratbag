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
#include <linux/input.h>

#include "shared.h"

enum options {
	OPT_VERBOSE,
	OPT_HELP,
};

enum cmd_flags {
	FLAG_VERBOSE = 1 << 0,
	FLAG_VERBOSE_RAW = 1 << 1,
};

struct ratbag_cmd {
	const char *name;
	int (*cmd)(struct ratbag *ratbag, uint32_t flags, int argc, char **argv);
	const char *args;
	const char *help;
};

static const struct ratbag_cmd *ratbag_commands[];

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
	       "    --verbose[=raw] ....... Print debugging output, with protocol output if requested.\n"
	       "    --help .......... Print this help.\n");
}

static int
ratbag_cmd_info(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path;
	struct ratbag_device *device;
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	char *action;
	int num_profiles, num_buttons;
	int i, j, b;
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

	printf("Capabilities:");
	if (ratbag_device_has_capability(device,
					 RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION))
		printf(" res");
	if (ratbag_device_has_capability(device,
					 RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE))
		printf(" profile");
	if (ratbag_device_has_capability(device,
					 RATBAG_DEVICE_CAP_BUTTON_KEY))
		printf(" btn-key");
	if (ratbag_device_has_capability(device,
					 RATBAG_DEVICE_CAP_BUTTON_MACROS))
		printf(" btn-macros");
	printf("\n");

	num_buttons = ratbag_device_get_num_buttons(device);
	printf("Number of buttons: %d\n", num_buttons);

	num_profiles = ratbag_device_get_num_profiles(device);
	printf("Profiles supported: %d\n", num_profiles);

	for (i = 0; i < num_profiles; i++) {
		int dpi, rate;
		profile = ratbag_device_get_profile_by_index(device, i);
		if (!profile)
			continue;

		printf("  Profile %d%s%s\n", i,
		       ratbag_profile_is_active(profile) ? " (active)" : "",
		       ratbag_profile_is_default(profile) ? " (default)" : "");
		printf("    Resolutions:\n");
		for (j = 0; j < ratbag_profile_get_num_resolutions(profile); j++) {
			struct ratbag_resolution *res;

			res = ratbag_profile_get_resolution(profile, j);
			dpi = ratbag_resolution_get_dpi(res);
			rate = ratbag_resolution_get_report_rate(res);
			if (dpi == 0)
				printf("      %d: <disabled>\n", j);
			else if (ratbag_resolution_has_capability(res,
								  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION))
				printf("      %d: %dx%ddpi @ %dHz%s%s\n", j,
				       ratbag_resolution_get_dpi_x(res),
				       ratbag_resolution_get_dpi_y(res),
				       rate,
				       ratbag_resolution_is_active(res) ? " (active)" : "",
				       ratbag_resolution_is_default(res) ? " (default)" : "");
			else
				printf("      %d: %ddpi @ %dHz%s%s\n", j, dpi, rate,
				       ratbag_resolution_is_active(res) ? " (active)" : "",
				       ratbag_resolution_is_default(res) ? " (default)" : "");

			ratbag_resolution_unref(res);
		}

		for (b = 0; b < num_buttons; b++) {
			enum ratbag_button_type type;

			button = ratbag_profile_get_button_by_index(profile, b);
			type = ratbag_button_get_type(button);
			action = button_action_to_str(button);
			printf("    Button: %d type %s is mapped to '%s'\n",
			       b, button_type_to_str(type), action);
			free(action);
			button = ratbag_button_unref(button);
		}

		profile = ratbag_profile_unref(profile);
	}

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
	struct ratbag_profile *profile = NULL, *active_profile = NULL;
	int num_profiles, index;
	int rc = 1;
	int i;

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
					  RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE)) {
		error("Looks like '%s' has no switchable profiles\n", path);
		goto out;
	}

	num_profiles = ratbag_device_get_num_profiles(device);
	if (index > num_profiles) {
		error("'%s' is not a valid profile\n", argv[0]);
		goto out;
	}

	profile = ratbag_device_get_profile_by_index(device, index);
	if (ratbag_profile_is_active(profile)) {
		printf("'%s' is already in profile '%d'\n",
		       ratbag_device_get_name(device), index);
		goto out;
	}

	for (i = 0; i < num_profiles; i++) {
		active_profile = ratbag_device_get_profile_by_index(device, i);
		if (ratbag_profile_is_active(active_profile))
			break;
		ratbag_profile_unref(active_profile);
		active_profile = NULL;
	}

	if (!active_profile) {
		error("Huh hoh, something bad happened, unable to retrieve the profile '%d' \n",
		      index);
		goto out;
	}

	rc = ratbag_profile_set_active(profile);
	if (!rc) {
		printf("Switched '%s' to profile '%d'\n",
		       ratbag_device_get_name(device), index);
	}

out:
	profile = ratbag_profile_unref(profile);
	active_profile = ratbag_profile_unref(active_profile);

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
	unsigned int modifiers[10];
	size_t modifiers_sz = 10;
	int i;

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
					  RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE)) {
		error("Looks like '%s' has no switchable profiles\n", path);
		goto out;
	}

	for (i = 0; i < ratbag_device_get_num_profiles(device); i++) {
		profile = ratbag_device_get_profile_by_index(device, i);
		if (ratbag_profile_is_active(profile))
			break;

		ratbag_profile_unref(profile);
		profile = NULL;
	}

	if (!profile) {
		error("Huh hoh, something bad happened, unable to retrieve the active profile\n");
		goto out;
	}

	button_6 = ratbag_profile_get_button_by_index(profile, 6);
	button_7 = ratbag_profile_get_button_by_index(profile, 7);

	if (ratbag_button_get_key(button_6, modifiers, &modifiers_sz) == KEY_VOLUMEUP &&
	    ratbag_button_get_key(button_7, modifiers, &modifiers_sz) == KEY_VOLUMEDOWN) {
		ratbag_button_disable(button_6);
		ratbag_button_disable(button_7);
		commit = 1;
	} else if (ratbag_button_get_action_type(button_6) == RATBAG_BUTTON_ACTION_TYPE_NONE &&
		   ratbag_button_get_action_type(button_7) == RATBAG_BUTTON_ACTION_TYPE_NONE) {
		ratbag_button_set_key(button_6, KEY_VOLUMEUP, modifiers, 0);
		ratbag_button_set_key(button_7, KEY_VOLUMEDOWN, modifiers, 0);
		commit = 2;
	}

	button_6 = ratbag_button_unref(button_6);
	button_7 = ratbag_button_unref(button_7);

	if (!commit)
		goto out;

	rc = ratbag_profile_set_active(profile);
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

struct macro {
	const char *name;
	struct {
		enum ratbag_macro_event_type type;
		unsigned data;
	} events[64];
};

static int
str_to_macro(const char *action_arg, struct macro *m)
{
	if (!action_arg)
		return -EINVAL;

	if (action_arg[0] == 'f') {
		m->name = "foo";
		m->events[0].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		m->events[0].data = KEY_F;
		m->events[1].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[1].data = 50;
		m->events[2].type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		m->events[2].data = KEY_F;
		m->events[3].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[3].data = 50;
		m->events[4].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		m->events[4].data = KEY_O;
		m->events[5].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[5].data = 50;
		m->events[6].type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		m->events[6].data = KEY_O;
		m->events[7].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[7].data = 50;
		m->events[8].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		m->events[8].data = KEY_O;
		m->events[9].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[9].data = 50;
		m->events[10].type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		m->events[10].data = KEY_O;
	} else if (action_arg[0] == 'b') {
		m->name = "bar";
		m->events[0].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		m->events[0].data = KEY_B;
		m->events[1].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[1].data = 50;
		m->events[2].type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		m->events[2].data = KEY_B;
		m->events[3].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[3].data = 50;
		m->events[4].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		m->events[4].data = KEY_A;
		m->events[5].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[5].data = 50;
		m->events[6].type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		m->events[6].data = KEY_A;
		m->events[7].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[7].data = 50;
		m->events[8].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		m->events[8].data = KEY_R;
		m->events[9].type = RATBAG_MACRO_EVENT_WAIT;
		m->events[9].data = 50;
		m->events[10].type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		m->events[10].data = KEY_R;
	}

	return 0;
}

static int
ratbag_cmd_change_button(struct ratbag *ratbag, uint32_t flags, int argc, char **argv)
{
	const char *path, *action_str, *action_arg;
	struct ratbag_device *device;
	struct ratbag_button *button = NULL;
	struct ratbag_profile *profile = NULL;
	int button_index;
	enum ratbag_button_action_type action_type;
	int rc = 1;
	unsigned int btnkey;
	enum ratbag_button_action_special special;
	struct macro macro = {0};
	int i;

	if (argc != 4) {
		usage();
		return 1;
	}

	button_index = atoi(argv[0]);
	action_str = argv[1];
	action_arg = argv[2];
	path = argv[3];
	if (streq(action_str, "button")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
		btnkey = atoi(action_arg);
	} else if (streq(action_str, "key")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		btnkey = libevdev_event_code_from_name(EV_KEY, action_arg);
		if (!btnkey) {
			error("Failed to resolve key %s\n", action_arg);
			return 1;
		}
	} else if (streq(action_str, "special")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		special = str_to_special_action(action_arg);
		if (special == RATBAG_BUTTON_ACTION_SPECIAL_INVALID) {
			error("Invalid special command '%s'\n", action_arg);
			return 1;
		}
	} else if (streq(action_str, "macro")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
		if (str_to_macro(action_arg, &macro)) {
			error("Invalid special command '%s'\n", action_arg);
			return 1;
		}
	} else {
		usage();
		return 1;
	}


	device = ratbag_cmd_open_device(ratbag, path);
	if (!device) {
		error("Looks like '%s' is not supported\n", path);
		return 1;
	}

	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_BUTTON_KEY)) {
		error("Looks like '%s' has no programmable buttons\n", path);
		goto out;
	}

	for (i = 0; i < ratbag_device_get_num_profiles(device); i++) {
		profile = ratbag_device_get_profile_by_index(device, i);
		if (ratbag_profile_is_active(profile))
			break;
		ratbag_profile_unref(profile);
		profile = NULL;
	}

	if (!profile) {
		error("Huh hoh, something bad happened, unable to retrieve the active profile\n");
		goto out;
	}

	button = ratbag_profile_get_button_by_index(profile, button_index);
	if (!button) {
		error("Invalid button number %d\n", button_index);
		goto out;
	}

	switch (action_type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		rc = ratbag_button_set_button(button, btnkey);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		rc = ratbag_button_set_key(button, btnkey, NULL, 0);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		rc = ratbag_button_set_special(button, special);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		rc = ratbag_button_set_macro(button, macro.name);
		for (i = 0; i < ARRAY_LENGTH(macro.events); i++) {
			if (macro.events[i].type == RATBAG_MACRO_EVENT_NONE)
				break;

			ratbag_button_set_macro_event(button,
						      i,
						      macro.events[i].type,
						      macro.events[i].data);
		}
		rc = ratbag_button_write_macro(button);
		break;
	default:
		error("well, that shouldn't have happened\n");
		abort();
		break;
	}
	if (rc) {
		error("Unable to perform button %d mapping %s %s\n",
		      button_index,
		      action_str,
		      action_arg);
		goto out;
	}

	rc = ratbag_profile_set_active(profile);
	if (rc) {
		error("Unable to apply the current profile: %s (%d)\n",
		      strerror(-rc),
		      rc);
		goto out;
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
	.args = "X <button|key|special|macro> <number|KEY_FOO|special|macro name:KEY_FOO,KEY_BAR,...>",
	.help = "Remap button X to the given action in the active profile",
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
	struct ratbag_profile *profile = NULL;
	int rc = 1;
	int dpi;
	int i;

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
					  RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION)) {
		error("Looks like '%s' has no switchable resolution\n", path);
		goto out;
	}

	for (i = 0; i < ratbag_device_get_num_profiles(device); i++) {
		profile = ratbag_device_get_profile_by_index(device, i);
		if (ratbag_profile_is_active(profile))
			break;

		ratbag_profile_unref(profile);
		profile = NULL;
	}

	if (!profile) {
		error("Huh hoh, something bad happened, unable to retrieve the active profile\n");
		goto out;
	}

	for (i = 0; i < ratbag_profile_get_num_resolutions(profile); i++) {
		struct ratbag_resolution *res;

		res = ratbag_profile_get_resolution(profile, i);
		if (ratbag_resolution_is_active(res)) {
			rc = ratbag_resolution_set_dpi(res, dpi);
			if (!rc)
				printf("Switched the current resolution profile of '%s' to %d dpi\n",
				       ratbag_device_get_name(device),
				       dpi);
			else
				error("can't seem to be able to change the dpi: %s (%d)\n",
				      strerror(-rc),
				      rc);
		}
		ratbag_resolution_unref(res);
	}

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
			{ "verbose", 2, 0, OPT_VERBOSE },
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
			if (optarg && streq(optarg, "raw"))
				flags |= FLAG_VERBOSE_RAW;
			else
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

	if (flags & FLAG_VERBOSE_RAW)
		ratbag_log_set_priority(ratbag, RATBAG_LOG_PRIORITY_RAW);
	else if (flags & FLAG_VERBOSE)
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
