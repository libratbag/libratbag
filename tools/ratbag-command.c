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
#include <stdbool.h>
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

enum errors {
	SUCCESS = 0,
	ERR_UNSUPPORTED = 1,	/* device doesn't support function, or
				   an index exceeds the device */
	ERR_USAGE = 2,		/* invalid commandline */
	ERR_DEVICE = 3,		/* invalid/missing device or command failed */
};

enum cmd_flags {
	FLAG_VERBOSE = 1 << 0,
	FLAG_VERBOSE_RAW = 1 << 1,

	/* flags used in ratbag_cmd */
	FLAG_NEED_DEVICE = 1 << 10,
	FLAG_NEED_PROFILE = 1 << 11,
	FLAG_NEED_RESOLUTION = 1 << 12,
	FLAG_NEED_BUTTON = 1 << 13,
};

struct ratbag_cmd_options {
	enum cmd_flags flags;
	struct ratbag_device *device;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
};

struct ratbag_cmd {
	const char *name;
	int (*cmd)(const struct ratbag_cmd *cmd,
		   struct ratbag *ratbag,
		   struct ratbag_cmd_options *options,
		   int argc, char **argv);
	uint32_t flags;
	const struct ratbag_cmd *subcommands[];
};

static const struct ratbag_cmd *ratbag_commands;

static void
usage(void)
{
	printf("%s [OPTIONS] {COMMAND} ... /path/to/device\n"
	       "\n"
	       "Query or change a device's settings:\n"
	       "\n"
	       "Options:\n"
	       "    --verbose	 		Print debugging output\n"
	       "    --verbose=raw 		Print debugging output with protocol output.\n"
	       "    --help 			Print this help.\n"
	       "\n"
	       "General Commands:\n"
	       "  list 				List supported devices (does not take a device argument)\n"
	       "\n"
	       "Device Commands:\n"
	       "  info				Print information about a device \n"
	       "\n"
	       "Profile Commands:\n"
	       "  profile active get		Print the currently active profile\n"
	       "  profile active set N		Set profile N as to the  active profile\n"
	       "  profile N {COMMAND}		Use profile N for COMMAND\n"
	       "\n"
	       "Resolution Commands\n"
	       "  Resolution commands work on the given profile, or on the\n"
	       "  active profile if none is given.\n"
	       "\n"
	       "  resolution active get		Print the currently active resolution\n"
	       "  resolution active set N	Set resolution N as the active resolution\n"
	       "  resolution N {COMMAND}	Use resolution N for COMMAND\n"
	       "\n"
	       "DPI Commands:\n"
	       "  DPI commands work on the given profile and resolution, or on the\n"
	       "  active resolution of the active profile if none are given.\n"
	       "\n"
	       "  dpi get			Print the dpi value\n"
	       "  dpi set N			Set the dpi value to N\n"
	       "  rate get			Print the report rate in Hz\n"
	       "  rate set N			Set the report rate in N Hz\n"
	       "\n"
	       "Button Commands:\n"
	       "  Button commands work on the given profile, or on the\n"
	       "  active profile if none is given.\n"
	       "\n"
	       "  button count			Print the number of buttons\n"
	       "  button N action get		Print the button action\n"
	       "  button N action set button B	Set the button action to button B\n"
	       "  button N action set special S	Set the button action to special action S\n"
	       "  button N action set macro ...	Set the button action to the given macro \n"
	       "\n"
	       "  Macro syntax:\n"
	       " 	A macro is a series of key events or waiting periods.\n"
	       "	Keys must be specified in linux/input.h key names.\n"
	       "	KEY_A			Press and release 'a'\n"
	       "	+KEY_A			Press 'a'\n"
	       "	-KEY_A			Release 'a'\n"
	       "	t300			Wait 300ms\n"
	       "\n"
	       "Special Commands:\n"
	       "These commands are for testing purposes and may be removed without notice\n"
	       "\n"
	       "  switch-etekcity		Switch the Etekcity mouse active profile\n"
	       "\n"
	       "Examples:\n"
	       "  %s profile active get\n"
	       "  %s profile 0 resolution active set 4\n"
	       "  %s profile 0 resolution 1 dpi get\n"
	       "  %s resolution 4 rate get\n"
	       "  %s dpi set 800\n"
	       "\n"
	       "Exit codes:\n"
	       "  0	Success\n"
	       "  1	Unsupported feature or index out of available range\n"
	       "  2	Commandline arguments are invalid\n"
	       "  3	Invalid device or a command failed on the device\n"
	       "\n",
		program_invocation_short_name,
		program_invocation_short_name,
		program_invocation_short_name,
		program_invocation_short_name,
		program_invocation_short_name,
		program_invocation_short_name);
}

static inline int
ratbag_cmd_device_from_arg(struct ratbag *ratbag,
			   int *argc, char **argv,
			   struct ratbag_device **device_out)
{
	struct ratbag_device *device;
	const char *path;

	if (*argc == 0) {
		error("Missing device path.\n");
		return ERR_USAGE;
	}

	path = argv[*argc - 1];
	device = ratbag_cmd_open_device(ratbag, path);
	if (!device) {
		error("Device '%s' is not supported\n", path);
		return ERR_DEVICE;
	}

	(*argc)--;
	*device_out = device;

	return SUCCESS;
}

static inline struct ratbag_profile *
ratbag_cmd_get_active_profile(struct ratbag_device *device)
{
	struct ratbag_profile *profile = NULL;
	int i;

	for (i = 0; i < ratbag_device_get_num_profiles(device); i++) {
		profile = ratbag_device_get_profile(device, i);
		if (ratbag_profile_is_active(profile))
			return profile;

		ratbag_profile_unref(profile);
		profile = NULL;
	}

	if (!profile)
		error("Failed to retrieve the active profile\n");

	return NULL;
}

static inline struct ratbag_resolution *
ratbag_cmd_get_active_resolution(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution = NULL;
	int i;

	for (i = 0; i < ratbag_profile_get_num_resolutions(profile); i++) {
		resolution = ratbag_profile_get_resolution(profile, i);
		if (ratbag_resolution_is_active(resolution))
			return resolution;

		ratbag_resolution_unref(resolution);
		resolution = NULL;
	}

	if (!resolution)
		error("Failed to retrieve the active resolution\n");

	return NULL;
}

static inline int
fill_options(struct ratbag *ratbag,
	     struct ratbag_cmd_options *options,
	     uint32_t flags,
	     int *argc, char **argv)
{
	struct ratbag_device *device = options->device;
	struct ratbag_profile *profile = options->profile;
	struct ratbag_resolution *resolution = options->resolution;
	struct ratbag_button *button = options->button;
	int rc;

	if ((flags & (FLAG_NEED_DEVICE|FLAG_NEED_PROFILE|FLAG_NEED_RESOLUTION)) &&
	    device == NULL) {
		rc = ratbag_cmd_device_from_arg(ratbag, argc, argv,
						&device);
		if (rc != SUCCESS)
			return rc;
		options->device = device;
	}

	if ((flags & (FLAG_NEED_PROFILE|FLAG_NEED_RESOLUTION)) &&
	     profile == NULL) {
		profile = ratbag_cmd_get_active_profile(device);
		if (!profile)
			return ERR_DEVICE;
		options->profile = profile;
	}

	if (flags & FLAG_NEED_RESOLUTION && resolution == NULL) {
		resolution = ratbag_cmd_get_active_resolution(profile);
		if (!resolution)
			return ERR_DEVICE;
		options->resolution = resolution;
	}

	if (flags & FLAG_NEED_BUTTON && button == NULL) {
		error("Missing button identifier\n");
		return ERR_USAGE;
	}

	return SUCCESS;
}

static int
run_subcommand(const char *command,
	       const struct ratbag_cmd *cmd,
	       struct ratbag *ratbag,
	       struct ratbag_cmd_options *options,
	       int argc, char **argv)
{
	const struct ratbag_cmd *sub = cmd->subcommands[0];
	int i = 0;
	int rc;

	while (sub) {
		if (streq(command, sub->name)) {
			rc = fill_options(ratbag, options,
					  sub->flags,
					  &argc,
					  argv);
			if (rc != SUCCESS)
				return rc;

			argc--;
			argv++;
			return sub->cmd(sub, ratbag, options, argc, argv);
		}
		sub = cmd->subcommands[i++];
	}

	error("Invalid subcommand '%s'\n", command);
	return ERR_USAGE;
}

static int
ratbag_cmd_info(const struct ratbag_cmd *cmd,
		struct ratbag *ratbag,
		struct ratbag_cmd_options *options,
		int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	char *action;
	int num_profiles, num_buttons;
	int i, j, b;

	device = options->device;

	printf("Device '%s'\n", ratbag_device_get_name(device));

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
		profile = ratbag_device_get_profile(device, i);
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

			button = ratbag_profile_get_button(profile, b);
			type = ratbag_button_get_type(button);
			action = button_action_to_str(button);
			printf("    Button: %d type %s is mapped to '%s'\n",
			       b, button_type_to_str(type), action);
			free(action);
			button = ratbag_button_unref(button);
		}

		profile = ratbag_profile_unref(profile);
	}

	return SUCCESS;
}

static const struct ratbag_cmd cmd_info = {
	.name = "info",
	.cmd = ratbag_cmd_info,
	.flags = FLAG_NEED_DEVICE,
	.subcommands = { NULL },
};

static int
ratbag_cmd_switch_etekcity(const struct ratbag_cmd *cmd,
			   struct ratbag *ratbag,
			   struct ratbag_cmd_options *options,
			   int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_button *button_6, *button_7;
	struct ratbag_profile *profile = NULL;
	int commit = 0;
	unsigned int modifiers[10];
	size_t modifiers_sz = 10;

	device = options->device;
	profile = options->profile;

	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE)) {
		error("Device '%s' has no switchable profiles\n",
		      ratbag_device_get_name(device));
		return ERR_UNSUPPORTED;
	}

	button_6 = ratbag_profile_get_button(profile, 6);
	button_7 = ratbag_profile_get_button(profile, 7);

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

	printf("Switched the current profile of '%s' to %sreport the volume keys\n",
	       ratbag_device_get_name(device),
	       commit == 1 ? "not " : "");

	return SUCCESS;
}

static const struct ratbag_cmd cmd_switch_etekcity = {
	.name = "switch-etekcity",
	.cmd = ratbag_cmd_switch_etekcity,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = { NULL },
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
	char *str, *s;
	enum ratbag_macro_event_type type;
	int code;
	int idx = 0;
	int rc = ERR_USAGE;

	/* FIXME: handle per-device maximum lengths of macros */

	if (!action_arg)
		return -EINVAL;

	str = strdup(action_arg);
	s = str;

	m->name = "<cmdline>";

	while (idx < ARRAY_LENGTH(m->events)) {
		char *token;

		token = strsep(&s, " ");
		if (!token)
			break;
		if (strlen(token) == 0)
			continue;

		switch(token[0]) {
		case '+':
			type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			token++;
			break;
		case '-':
			type = RATBAG_MACRO_EVENT_KEY_RELEASED;
			token++;
			break;
		case 't':
			type = RATBAG_MACRO_EVENT_WAIT;
			token++;
			break;
		default:
			type = RATBAG_MACRO_EVENT_NONE;
			break;
		}

		if (type == RATBAG_MACRO_EVENT_WAIT) {
			char *endptr;
			code = strtol(token, &endptr, 10);
			if (*endptr != '\0') {
				error("Invalid token name: %s\n", token);
				goto out;
			}
		} else {
			code = libevdev_event_code_from_name(EV_KEY, token);
			if (code == -1) {
				error("Invalid token name: %s\n", token);
				goto out;
			}
		}

		if (type == RATBAG_MACRO_EVENT_NONE) {
			m->events[idx].type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			m->events[idx].data = code;
			type = RATBAG_MACRO_EVENT_KEY_RELEASED;
			idx++;
		}
		m->events[idx].data = code;
		m->events[idx].type = type;
		idx++;
	}

	rc = SUCCESS;

out:
	free(str);

	return rc;
}

static int
ratbag_cmd_change_button(const struct ratbag_cmd *cmd,
			 struct ratbag *ratbag,
			 struct ratbag_cmd_options *options,
			 int argc, char **argv)
{
	const char *action_str, *action_arg;
	struct ratbag_device *device;
	struct ratbag_button *button = NULL;
	struct ratbag_profile *profile = NULL;
	int button_index;
	enum ratbag_button_action_type action_type;
	int rc = ERR_DEVICE;
	unsigned int btnkey;
	enum ratbag_button_action_special special;
	struct macro macro = {0};
	int i;

	if (argc != 3)
		return ERR_USAGE;

	button_index = atoi(argv[0]);
	action_str = argv[1];
	action_arg = argv[2];

	argc -= 3;
	argv += 3;

	if (streq(action_str, "button")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
		btnkey = atoi(action_arg);
	} else if (streq(action_str, "key")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		btnkey = libevdev_event_code_from_name(EV_KEY, action_arg);
		if (!btnkey) {
			error("Failed to resolve key %s\n", action_arg);
			return ERR_USAGE;
		}
	} else if (streq(action_str, "special")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		special = str_to_special_action(action_arg);
		if (special == RATBAG_BUTTON_ACTION_SPECIAL_INVALID) {
			error("Invalid special command '%s'\n", action_arg);
			return ERR_USAGE;
		}
	} else if (streq(action_str, "macro")) {
		action_type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
		if (str_to_macro(action_arg, &macro)) {
			error("Invalid special command '%s'\n", action_arg);
			return ERR_USAGE;
		}
	} else {
		return ERR_USAGE;
	}

	device = options->device;
	profile = options->profile;

	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_BUTTON_KEY)) {
		error("Device '%s' has no programmable buttons\n",
		      ratbag_device_get_name(device));
		rc = ERR_UNSUPPORTED;
		goto out;
	}

	button = ratbag_profile_get_button(profile, button_index);
	if (!button) {
		error("Invalid button number %d\n", button_index);
		rc = ERR_UNSUPPORTED;
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
		rc = ERR_UNSUPPORTED;
		goto out;
	}

	rc = ratbag_profile_set_active(profile);
	if (rc) {
		error("Unable to apply the current profile: %s (%d)\n",
		      strerror(-rc),
		      rc);
		rc = ERR_DEVICE;
		goto out;
	}

out:
	button = ratbag_button_unref(button);

	return rc;
}

static const struct ratbag_cmd cmd_change_button = {
	.name = "change-button",
	.cmd = ratbag_cmd_change_button,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = { NULL },
};

static int
filter_event_node(const struct dirent *input_entry)
{
	return strneq(input_entry->d_name, "event", 5);
}

static int
ratbag_cmd_list_supported_devices(const struct ratbag_cmd *cmd,
				  struct ratbag *ratbag,
				  struct ratbag_cmd_options *options,
				  int argc, char **argv)
{
	struct dirent **input_list;
	struct ratbag_device *device;
	char path[256];
	int n, i;
	int supported = 0;

	if (argc != 0)
		return ERR_USAGE;

	n = scandir("/dev/input", &input_list, filter_event_node, alphasort);
	if (n < 0)
		return SUCCESS;

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

	return SUCCESS;
}

static const struct ratbag_cmd cmd_list = {
	.name = "list",
	.cmd = ratbag_cmd_list_supported_devices,
	.flags = 0,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_active_set(const struct ratbag_cmd *cmd,
				 struct ratbag *ratbag,
				 struct ratbag_cmd_options *options,
				 int argc, char **argv)
{
	struct ratbag_resolution *resolution;
	int rc;

	resolution = options->resolution;
	rc = ratbag_resolution_set_active(resolution);

	if (rc != 0) {
		error("Failed to to set resolution as active\n");
		return ERR_DEVICE;
	}

	return SUCCESS;
}

static const struct ratbag_cmd cmd_resolution_active_set = {
	.name = "set",
	.cmd = ratbag_cmd_resolution_active_set,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_active_get(const struct ratbag_cmd *cmd,
				 struct ratbag *ratbag,
				 struct ratbag_cmd_options *options,
				 int argc, char **argv)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution = NULL;
	int num_resolutions;
	int active_resolution = -1;
	int i;
	int rc = SUCCESS;

	profile = options->profile;

	num_resolutions = ratbag_profile_get_num_resolutions(profile);

	for (i = 0; i < num_resolutions && active_resolution < 0; i++) {
		resolution = ratbag_profile_get_resolution(profile, i);
		if (ratbag_resolution_is_active(resolution))
			active_resolution = i;

		ratbag_resolution_unref(resolution);
	}

	if (active_resolution < 0) {
		error("BUG: Unable to find active resolution\n");
		rc = ERR_DEVICE;
	}

	if (rc == SUCCESS)
		printf("%d\n", active_resolution);

	return rc;
}

static const struct ratbag_cmd cmd_resolution_active_get = {
	.name = "get",
	.cmd = ratbag_cmd_resolution_active_get,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_active(const struct ratbag_cmd *cmd,
			  struct ratbag *ratbag,
			  struct ratbag_cmd_options *options,
			  int argc, char **argv)
{
	if (argc < 1)
		return ERR_USAGE;

	return run_subcommand(argv[0],
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_resolution_active = {
	.name = "active",
	.cmd = ratbag_cmd_resolution_active,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = {
		&cmd_resolution_active_get,
		&cmd_resolution_active_set,
		NULL,
	},
};

static int
ratbag_cmd_resolution_dpi_get(const struct ratbag_cmd *cmd,
			      struct ratbag *ratbag,
			      struct ratbag_cmd_options *options,
			      int argc, char **argv)
{
	struct ratbag_resolution *resolution;
	int dpi;

	resolution = options->resolution;
	dpi = ratbag_resolution_get_dpi(resolution);
	printf("%d\n", dpi);

	return SUCCESS;
}

static const struct ratbag_cmd cmd_resolution_dpi_get = {
	.name = "get",
	.cmd = ratbag_cmd_resolution_dpi_get,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_RESOLUTION,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_dpi_set(const struct ratbag_cmd *cmd,
			      struct ratbag *ratbag,
			      struct ratbag_cmd_options *options,
			      int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_resolution *resolution;
	int rc = SUCCESS;
	int dpi;

	if (argc != 1)
		return ERR_USAGE;

	dpi = atoi(argv[0]);

	argc--;
	argv++;

	device = options->device;
	resolution = options->resolution;

	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION)) {
		error("Device '%s' has no switchable resolution\n",
		      ratbag_device_get_name(device));
		rc = ERR_UNSUPPORTED;
		goto out;
	}

	rc = ratbag_resolution_set_dpi(resolution, dpi);
	if (rc) {
		error("Failed to change the dpi: %s (%d)\n",
		      strerror(-rc),
		      rc);
		rc = ERR_DEVICE;
	}
out:
	return rc;
}

static const struct ratbag_cmd cmd_resolution_dpi_set = {
	.name = "set",
	.cmd = ratbag_cmd_resolution_dpi_set,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE| FLAG_NEED_RESOLUTION,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_dpi(const struct ratbag_cmd *cmd,
			  struct ratbag *ratbag,
			  struct ratbag_cmd_options *options,
			  int argc, char **argv)
{
	if (argc < 1)
		return ERR_USAGE;

	return run_subcommand(argv[0],
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_resolution_dpi = {
	.name = "dpi",
	.cmd = ratbag_cmd_resolution_dpi,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE| FLAG_NEED_RESOLUTION,
	.subcommands = {
		&cmd_resolution_dpi_get,
		&cmd_resolution_dpi_set,
		NULL,
	},
};

static int
ratbag_cmd_resolution_rate_get(const struct ratbag_cmd *cmd,
			       struct ratbag *ratbag,
			       struct ratbag_cmd_options *options,
			       int argc, char **argv)
{
	struct ratbag_resolution *resolution;
	int rate;

	resolution = options->resolution;
	rate = ratbag_resolution_get_report_rate(resolution);
	printf("%d\n", rate);

	return SUCCESS;
}

static const struct ratbag_cmd cmd_resolution_rate_get = {
	.name = "get",
	.cmd = ratbag_cmd_resolution_rate_get,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_RESOLUTION,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_rate_set(const struct ratbag_cmd *cmd,
			       struct ratbag *ratbag,
			       struct ratbag_cmd_options *options,
			       int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_resolution *resolution;
	int rc = SUCCESS;
	int rate;

	if (argc != 1)
		return ERR_USAGE;

	rate = atoi(argv[0]);

	argc--;
	argv++;

	device = options->device;
	resolution = options->resolution;

	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION)) {
		error("Device '%s' has no switchable resolution\n",
		      ratbag_device_get_name(device));
		rc = ERR_UNSUPPORTED;
		goto out;
	}

	rc = ratbag_resolution_set_report_rate(resolution, rate);
	if (rc) {
		error("Failed to change the rate: %s (%d)\n",
		      strerror(-rc),
		      rc);
		rc = ERR_DEVICE;
	}
out:
	return rc;
}

static const struct ratbag_cmd cmd_resolution_rate_set = {
	.name = "set",
	.cmd = ratbag_cmd_resolution_rate_set,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE| FLAG_NEED_RESOLUTION,
	.subcommands = { NULL },
};

static int
ratbag_cmd_resolution_rate(const struct ratbag_cmd *cmd,
			   struct ratbag *ratbag,
			   struct ratbag_cmd_options *options,
			   int argc, char **argv)
{
	if (argc < 1)
		return ERR_USAGE;

	return run_subcommand(argv[0],
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_resolution_rate = {
	.name = "rate",
	.cmd = ratbag_cmd_resolution_rate,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE| FLAG_NEED_RESOLUTION,
	.subcommands = {
		&cmd_resolution_rate_get,
		&cmd_resolution_rate_set,
		NULL,
	},
};

static int
ratbag_cmd_resolution(const struct ratbag_cmd *cmd,
		      struct ratbag *ratbag,
		      struct ratbag_cmd_options *options,
		      int argc, char **argv)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	const char *command;
	int resolution_idx = 0;
	char *endp;

	if (argc < 1)
		return ERR_USAGE;

	command = argv[0];

	profile = options->profile;

	resolution_idx = strtol(command, &endp, 10);
	if (command != endp && *endp == '\0') {
		resolution = ratbag_profile_get_resolution(profile,
							   resolution_idx);

		if (!resolution) {
			error("Unable to retrieve resolution %d\n",
			      resolution_idx);
			return ERR_UNSUPPORTED;
		}
		argc--;
		argv++;
		command = argv[0];
	} else {
		resolution = ratbag_cmd_get_active_resolution(profile);
		if (!resolution)
			return ERR_DEVICE;
	}

	options->resolution = resolution;

	return run_subcommand(command,
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_resolution = {
	.name = "resolution",
	.cmd = ratbag_cmd_resolution,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = {
		&cmd_resolution_active,
		&cmd_resolution_dpi,
		&cmd_resolution_rate,
		NULL,
	},
};

static int
ratbag_cmd_button_count(const struct ratbag_cmd *cmd,
			struct ratbag *ratbag,
			struct ratbag_cmd_options *options,
			int argc, char **argv)
{
	struct ratbag_device *device;
	int num_buttons;

	device = options->device;
	num_buttons = ratbag_device_get_num_buttons(device);
	printf("%d\n", num_buttons);

	return SUCCESS;
}

static const struct ratbag_cmd cmd_button_count = {
	.name = "count",
	.cmd = ratbag_cmd_button_count,
	.flags = FLAG_NEED_DEVICE,
	.subcommands = {
		NULL,
	},
};

static int
ratbag_cmd_button_get(const struct ratbag_cmd *cmd,
		      struct ratbag *ratbag,
		      struct ratbag_cmd_options *options,
		      int argc, char **argv)
{
	struct ratbag_button *button;
	enum ratbag_button_type type;
	const char *action;

	button = options->button;

	type = ratbag_button_get_type(button);
	action = button_action_to_str(button);
	printf("type %s to %s\n",
	       button_type_to_str(type), action);


	return SUCCESS;
}

static const struct ratbag_cmd cmd_button_get = {
	.name = "get",
	.cmd = ratbag_cmd_button_get,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		NULL,
	},
};

static int
ratbag_cmd_button_set_button(const struct ratbag_cmd *cmd,
			     struct ratbag *ratbag,
			     struct ratbag_cmd_options *options,
			     int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_button *button;
	char *str, *endptr;
	int b;
	int rc;

	if (argc < 1)
		return ERR_USAGE;

	str = argv[0];
	b = strtol(str, &endptr, 10);
	if (*endptr != '\0')
		return ERR_USAGE;

	device = options->device;
	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_BUTTON_KEY))
		return ERR_UNSUPPORTED;


	button = options->button;
	rc = ratbag_button_set_button(button, b);
	if (rc != 0)
		return ERR_DEVICE;

	return SUCCESS;
}

static const struct ratbag_cmd cmd_button_set_button = {
	.name = "button",
	.cmd = ratbag_cmd_button_set_button,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		NULL,
	},
};

static int
ratbag_cmd_button_set_key(const struct ratbag_cmd *cmd,
			  struct ratbag *ratbag,
			  struct ratbag_cmd_options *options,
			  int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_button *button;
	int keycode;
	char *str;
	int rc;

	if (argc < 1)
		return ERR_USAGE;

	str = argv[0];
	keycode = libevdev_event_code_from_name(EV_KEY, str);
	if (keycode == -1) {
		error("Failed to resolve keycode '%s'\n", str);
		return ERR_USAGE;
	}

	device = options->device;
	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_BUTTON_KEY))
		return ERR_UNSUPPORTED;

	button = options->button;
	rc = ratbag_button_set_key(button, keycode, NULL, 0);
	if (rc != 0)
		return ERR_DEVICE;

	return SUCCESS;
}

static const struct ratbag_cmd cmd_button_set_key = {
	.name = "key",
	.cmd = ratbag_cmd_button_set_key,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		NULL,
	},
};

static int
ratbag_cmd_button_set_special(const struct ratbag_cmd *cmd,
			      struct ratbag *ratbag,
			      struct ratbag_cmd_options *options,
			      int argc, char **argv)
{
	struct ratbag_button *button;
	enum ratbag_button_action_special special;
	char *str;
	int rc;

	if (argc < 1)
		return ERR_USAGE;

	str = argv[0];
	special = str_to_special_action(str);
	if (special == RATBAG_BUTTON_ACTION_SPECIAL_INVALID) {
		error("Invalid special identifier '%s'\n", str);
		return ERR_USAGE;
	}

	button = options->button;
	rc = ratbag_button_set_special(button, special);
	if (rc != 0)
		return ERR_DEVICE;

	return SUCCESS;
}

static const struct ratbag_cmd cmd_button_set_special = {
	.name = "special",
	.cmd = ratbag_cmd_button_set_special,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		NULL,
	},
};

static int
ratbag_cmd_button_set_macro(const struct ratbag_cmd *cmd,
			    struct ratbag *ratbag,
			    struct ratbag_cmd_options *options,
			    int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_button *button;
	struct macro macro = {0};
	int rc;
	int i;
	char macro_str[PATH_MAX] = {0};

	if (argc < 1)
		return ERR_USAGE;

	for (i = 0; i < argc; i++) {
		strncat(macro_str, argv[i], sizeof(macro_str) - strlen(macro_str) - 1);
		strcat(macro_str, " ");
	}

	if (str_to_macro(macro_str, &macro) != 0) {
		error("Invalid macro string '%s'\n", macro_str);
		return ERR_USAGE;
	}

	device = options->device;
	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_BUTTON_MACROS))
		return ERR_UNSUPPORTED;

	button = options->button;
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
	if (rc != 0)
		return ERR_DEVICE;

	return SUCCESS;
}

static const struct ratbag_cmd cmd_button_set_macro = {
	.name = "macro",
	.cmd = ratbag_cmd_button_set_macro,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		NULL,
	},
};

static int
ratbag_cmd_button_set(const struct ratbag_cmd *cmd,
		      struct ratbag *ratbag,
		      struct ratbag_cmd_options *options,
		      int argc, char **argv)
{
	const char *command;

	if (argc < 1)
		return ERR_USAGE;

	command = argv[0];

	return run_subcommand(command,
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_button_set = {
	.name = "set",
	.cmd = ratbag_cmd_button_set,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		&cmd_button_set_button,
		&cmd_button_set_key,
		&cmd_button_set_special,
		&cmd_button_set_macro,
		NULL,
	},
};

static int
ratbag_cmd_button_action(const struct ratbag_cmd *cmd,
			 struct ratbag *ratbag,
			 struct ratbag_cmd_options *options,
			 int argc, char **argv)
{
	const char *command;

	if (argc < 1)
		return ERR_USAGE;

	command = argv[0];

	return run_subcommand(command,
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_button_action = {
	.name = "action",
	.cmd = ratbag_cmd_button_action,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE | FLAG_NEED_BUTTON,
	.subcommands = {
		&cmd_button_get,
		&cmd_button_set,
		NULL,
	},
};

static int
ratbag_cmd_button(const struct ratbag_cmd *cmd,
		   struct ratbag *ratbag,
		   struct ratbag_cmd_options *options,
		   int argc, char **argv)
{
	struct ratbag_profile *profile;
	struct ratbag_button *button;
	const char *command;
	int button_idx = 0;
	char *endp;

	if (argc < 1)
		return ERR_USAGE;

	profile = options->profile;

	command = argv[0];

	button_idx = strtol(command, &endp, 10);
	if (command != endp && *endp == '\0') {
		button = ratbag_profile_get_button(profile,
							    button_idx);
		if (!button) {
			error("Invalid button %d\n", button_idx);
			return ERR_UNSUPPORTED;
		}
		options->button = button;
		argc--;
		argv++;
		command = argv[0];
	}

	return run_subcommand(command,
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_button = {
	.name = "button",
	.cmd = ratbag_cmd_button,
	.flags = FLAG_NEED_DEVICE | FLAG_NEED_PROFILE,
	.subcommands = {
		&cmd_button_count,
		&cmd_button_action,
		NULL,
	},
};

static int
ratbag_cmd_profile_active_set(const struct ratbag_cmd *cmd,
			      struct ratbag *ratbag,
			      struct ratbag_cmd_options *options,
			      int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_profile *profile = NULL;
	int num_profiles, index;
	int rc = ERR_UNSUPPORTED;

	if (argc != 1)
		return ERR_USAGE;

	index = atoi(argv[0]);

	argc--;
	argv++;

	device = options->device;

	if (!ratbag_device_has_capability(device,
					  RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE)) {
		error("Device '%s' has no switchable profiles\n",
		      ratbag_device_get_name(device));
		goto out;
	}

	num_profiles = ratbag_device_get_num_profiles(device);
	if (index > num_profiles) {
		error("'%d' is not a valid profile\n", index);
		goto out;
	}

	profile = ratbag_device_get_profile(device, index);
	if (ratbag_profile_is_active(profile)) {
		rc = SUCCESS;
		goto out;
	}

	rc = ratbag_profile_set_active(profile);
	if (rc == 0) {
		printf("Switched '%s' to profile '%d'\n",
		       ratbag_device_get_name(device), index);
		rc = SUCCESS;
	} else {
		rc = ERR_DEVICE;
	}

out:
	profile = ratbag_profile_unref(profile);

	return rc;
}

static const struct ratbag_cmd cmd_profile_active_set = {
	.name = "set",
	.cmd = ratbag_cmd_profile_active_set,
	.flags = FLAG_NEED_DEVICE,
	.subcommands = { NULL },
};

static int
ratbag_cmd_profile_active_get(const struct ratbag_cmd *cmd,
			      struct ratbag *ratbag,
			      struct ratbag_cmd_options *options,
			      int argc, char **argv)
{
	struct ratbag_device *device;
	struct ratbag_profile *profile = NULL;
	int i;
	int rc = SUCCESS;
	int active_profile = -1;
	int num_profiles = 0;

	device = options->device;

	num_profiles = ratbag_device_get_num_profiles(device);

	for (i = 0; i < num_profiles && active_profile < 0; i++) {
		profile = ratbag_device_get_profile(device, i);
		if (ratbag_profile_is_active(profile))
			active_profile = i;

		ratbag_profile_unref(profile);
	}

	if (active_profile < 0) {
		error("BUG: Unable to find active profile.\n");
		rc = ERR_DEVICE;
	}

	if (rc == SUCCESS)
		printf("%d\n", active_profile);

	return rc;
}

static const struct ratbag_cmd cmd_profile_active_get = {
	.name = "get",
	.cmd = ratbag_cmd_profile_active_get,
	.flags = FLAG_NEED_DEVICE,
	.subcommands = { NULL },
};

static int
ratbag_cmd_profile_active(const struct ratbag_cmd *cmd,
			  struct ratbag *ratbag,
			  struct ratbag_cmd_options *options,
			  int argc, char **argv)
{
	if (argc < 1)
		return ERR_USAGE;

	return run_subcommand(argv[0],
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_profile_active = {
	.name = "active",
	.cmd = ratbag_cmd_profile_active,
	.flags = FLAG_NEED_DEVICE,
	.subcommands = {
		&cmd_profile_active_get,
		&cmd_profile_active_set,
		NULL,
	},
};

static int
ratbag_cmd_profile(const struct ratbag_cmd *cmd,
		   struct ratbag *ratbag,
		   struct ratbag_cmd_options *options,
		   int argc, char **argv)
{
	struct ratbag_profile *profile;
	struct ratbag_device *device;
	const char *command;
	int profile_idx = 0;
	char *endp;

	device = options->device;

	if (argc < 1)
		return ERR_USAGE;

	command = argv[0];

	profile_idx = strtol(command, &endp, 10);
	if (command != endp && *endp == '\0') {
		profile = ratbag_device_get_profile(device,
							     profile_idx);
		if (!profile) {
			error("Unable to find profile %d\n", profile_idx);
			return ERR_UNSUPPORTED;
		}

		argc--;
		argv++;
		command = argv[0];
	} else {
		profile = ratbag_cmd_get_active_profile(device);
		if (!profile)
			return ERR_DEVICE;
	}

	options->profile = profile;

	return run_subcommand(command,
			      cmd,
			      ratbag, options,
			      argc, argv);
}

static const struct ratbag_cmd cmd_profile = {
	.name = "profile",
	.cmd = ratbag_cmd_profile,
	.flags = FLAG_NEED_DEVICE,
	.subcommands = {
		&cmd_profile_active,
		&cmd_resolution,
		&cmd_button,
		NULL,
	},
};

static const struct ratbag_cmd top_level_commands = {
	.name = "ratbag-command",
	.cmd = NULL,
	.subcommands = {
		&cmd_info,
		&cmd_list,
		&cmd_change_button,
		&cmd_switch_etekcity,
		&cmd_button,
		&cmd_resolution,
		&cmd_profile,
		&cmd_resolution_dpi,
		&cmd_resolution_rate,
		NULL,
	},
};

static const struct ratbag_cmd *ratbag_commands = &top_level_commands;

int
main(int argc, char **argv)
{
	struct ratbag *ratbag;
	const char *command;
	int rc = SUCCESS;
	struct ratbag_cmd_options options = {0};

	ratbag = ratbag_create_context(&interface, NULL);
	if (!ratbag) {
		rc = ERR_DEVICE;
		error("Failed to initialize ratbag\n");
		goto out;
	}

	options.flags = 0;

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
				options.flags |= FLAG_VERBOSE_RAW;
			else
				options.flags |= FLAG_VERBOSE;
			break;
		default:
			goto out;
		}
	}

	if (optind >= argc) {
		rc = ERR_USAGE;
		goto out;
	}

	if (options.flags & FLAG_VERBOSE_RAW)
		ratbag_log_set_priority(ratbag, RATBAG_LOG_PRIORITY_RAW);
	else if (options.flags & FLAG_VERBOSE)
		ratbag_log_set_priority(ratbag, RATBAG_LOG_PRIORITY_DEBUG);

	argc -= optind;
	argv += optind;

	command = argv[0];
	rc = run_subcommand(command,
			    ratbag_commands,
			    ratbag,
			    &options,
			    argc, argv);
out:
	ratbag_resolution_unref(options.resolution);
	ratbag_button_unref(options.button);
	ratbag_profile_unref(options.profile);
	ratbag_device_unref(options.device);
	ratbag_unref(ratbag);

	if (rc == ERR_USAGE)
		usage();

	return rc;
}
