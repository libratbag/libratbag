/***
  This file is part of ratbagd.

  Copyright 2015 David Herrmann <dh.herrmann@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice (including the next
  paragraph) shall be included in all copies or substantial portions of the
  Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
***/

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include "shared-macro.h"

struct ratbagctl {
	sd_bus *bus;
};

static int verb_help(struct ratbagctl *ctl, int argc, char **argv);

static struct ratbagctl *ratbagctl_free(struct ratbagctl *ctl)
{
	if (!ctl)
		return NULL;

	ctl->bus = sd_bus_unref(ctl->bus);

	return mfree(ctl);
}

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbagctl *, ratbagctl_free);

static int ratbagctl_new(struct ratbagctl **out)
{
	_cleanup_(ratbagctl_freep) struct ratbagctl *ctl = NULL;
	int r;

	ctl = calloc(1, sizeof(*ctl));
	if (!ctl)
		return -ENOMEM;

	r = sd_bus_open_system(&ctl->bus);
	if (r < 0)
		return r;

	*out = ctl;
	ctl = NULL;
	return 0;
}

static int get_device_path(struct ratbagctl *ctl,
			   const char *device_name,
			   char **out_path,
			   sd_bus_error *error)
{
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	const char *device_path = NULL;
	char *path;
	int r;

	if (device_name) {
		r = sd_bus_call_method(ctl->bus,
				       "org.freedesktop.ratbag1",
				       "/org/freedesktop/ratbag1",
				       "org.freedesktop.ratbag1.Manager",
				       "GetDeviceByName",
				       error,
				       &reply,
				       "s",
				       device_name);
		if (r < 0)
			return r;

		r = sd_bus_message_read(reply, "o", &device_path);
		if (r < 0)
			return r;
	} else {
		r = sd_bus_call_method(ctl->bus,
				       "org.freedesktop.ratbag1",
				       "/org/freedesktop/ratbag1",
				       "org.freedesktop.DBus.Properties",
				       "Get",
				       error,
				       &reply,
				       "ss",
				       "org.freedesktop.ratbag1.Manager",
				       "Devices");
		if (r < 0)
			return r;

		r = sd_bus_message_enter_container(reply, 'v', "ao");
		if (r < 0)
			return r;

		r = sd_bus_message_enter_container(reply, 'a', "o");
		if (r < 0)
			return r;

		while ((r = sd_bus_message_read_basic(reply, 'o', &path)) > 0) {
			if (!device_path)
				device_path = path;
		}
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			return r;

		if (!device_path)
			return -ENXIO;
	}

	assert(device_path);

	path = strdup(device_path);
	if (!path)
		return -ENOMEM;

	*out_path = path;
	return 0;
}

static int get_profile_path(struct ratbagctl *ctl,
			    const char *device_name,
			    const char *profile_name,
			    char **out_path,
			    sd_bus_error *error)
{
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply2 = NULL;
	_cleanup_(freep) char *device_path = NULL;
	const char *profile_path = NULL;
	unsigned int profile_index;
	char *path;
	int r;

	r = get_device_path(ctl, device_name, &device_path, error);
	if (r < 0)
		return r;

	if (profile_name) {
		r = safe_atou(profile_name, &profile_index);
		if (r < 0)
			return -ENXIO;

		r = sd_bus_call_method(ctl->bus,
				       "org.freedesktop.ratbag1",
				       device_path,
				       "org.freedesktop.ratbag1.Device",
				       "GetProfileByIndex",
				       error,
				       &reply2,
				       "u",
				       profile_index);
		if (r < 0)
			return r;

		r = sd_bus_message_read(reply2, "o", &profile_path);
		if (r < 0)
			return r;
	} else {
		r = sd_bus_call_method(ctl->bus,
				       "org.freedesktop.ratbag1",
				       device_path,
				       "org.freedesktop.DBus.Properties",
				       "Get",
				       error,
				       &reply2,
				       "ss",
				       "org.freedesktop.ratbag1.Device",
				       "ActiveProfile");
		if (r < 0)
			return r;

		r = sd_bus_message_read(reply2, "v", "o", &profile_path);
		if (r < 0)
			return r;

		if (streq(profile_path, "/")) {
			reply2 = sd_bus_message_unref(reply2);

			r = sd_bus_call_method(ctl->bus,
					       "org.freedesktop.ratbag1",
					       device_path,
					       "org.freedesktop.DBus.Properties",
					       "Get",
					       error,
					       &reply2,
					       "ss",
					       "org.freedesktop.ratbag1.Device",
					       "DefaultProfile");
			if (r < 0)
				return r;

			r = sd_bus_message_read(reply2, "v", "o", &profile_path);
			if (r < 0)
				return r;

			if (streq(profile_path, "/"))
				return -ENXIO;
		}
	}

	assert(profile_path);

	path = strdup(profile_path);
	if (!path)
		return -ENOMEM;

	*out_path = path;
	return 0;
}

static int list_devices_show(struct ratbagctl *ctl, const char *path)
{
	_cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	const char *prop_id = NULL, *prop_description = NULL;
	int r;

	r = sd_bus_call_method(ctl->bus,
			       "org.freedesktop.ratbag1",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &error,
			       &reply,
			       "s",
			       "org.freedesktop.ratbag1.Device");
	if (r < 0)
		return r;

	r = sd_bus_message_enter_container(reply, 'a', "{sv}");
	if (r < 0)
		return r;

	while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
		const char *property;

		r = sd_bus_message_read_basic(reply, 's', &property);
		if (r < 0)
			return r;

		if (streq(property, "Id")) {
			r = sd_bus_message_read(reply, "v", "s",
						&prop_id);
		} else if (streq(property, "Description")) {
			r = sd_bus_message_read(reply, "v", "s",
						&prop_description);
		} else {
			r = sd_bus_message_skip(reply, "v");
		}
		if (r < 0)
			return r;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			return r;
	}
	if (r < 0)
		return r;

	r = sd_bus_message_exit_container(reply);
	if (r < 0)
		return r;

	printf("%10s %-32s\n", prop_id, prop_description);
	return 0;
}

static int list_devices_all(struct ratbagctl *ctl)
{
	_cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	unsigned int k = 0;
	const char *path;
	int r;

	r = sd_bus_call_method(ctl->bus,
			       "org.freedesktop.ratbag1",
			       "/org/freedesktop/ratbag1",
			       "org.freedesktop.DBus.Properties",
			       "Get",
			       &error,
			       &reply,
			       "ss",
			       "org.freedesktop.ratbag1.Manager",
			       "Devices");
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 'v', "ao");
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 'a', "o");
	if (r < 0)
		goto exit;

	printf("%10s %-32s\n", "DEVICE", "DESCRIPTION");

	while ((r = sd_bus_message_read_basic(reply, 'o', &path)) > 0) {
		r = list_devices_show(ctl, path);
		if (r < 0)
			goto exit;

		++k;
	}
	if (r < 0)
		goto exit;

	printf("\n%u device%s listed.\n", k, k > 1 ? "s" : "");

	r = sd_bus_message_exit_container(reply);
	if (r < 0)
		goto exit;

	r = sd_bus_message_exit_container(reply);
exit:
	if (r < 0)
		fprintf(stderr, "Cannot list devices: %s\n",
			error.message ? : "Parser error");
	return r;
}

static int verb_list_devices(struct ratbagctl *ctl, int argc, char **argv)
{
	static const struct option options[] = {
		{},
	};
	int c;

	while ((c = getopt_long(argc, argv, "+", options, NULL)) >= 0) {
		switch (c) {
		default:
			return -EINVAL;
		}
	}

	if (argv[optind]) {
		fprintf(stderr, "Command does not take arguments\n");
		return -EINVAL;
	}

	return list_devices_all(ctl);
}

static int show_device_get_profile_index(struct ratbagctl *ctl,
					 const char *path,
					 unsigned int *out_index)
{
	_cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	int r;

	if (streq(path, "/")) {
		*out_index = -1;
		return 0;
	}

	r = sd_bus_call_method(ctl->bus,
			       "org.freedesktop.ratbag1",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "Get",
			       &error,
			       &reply,
			       "ss",
			       "org.freedesktop.ratbag1.Profile",
			       "Index");
	if (r < 0)
		return r;

	r = sd_bus_message_enter_container(reply, 'v', "u");
	if (r < 0)
		return r;

	r = sd_bus_message_read_basic(reply, 'u', out_index);
	if (r < 0)
		return r;

	r = sd_bus_message_exit_container(reply);
	if (r < 0)
		return r;

	return 0;
}

static int show_device_print(struct ratbagctl *ctl, const char *device)
{
	_cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	unsigned int prop_min_index = 0, prop_max_index = 0;
	unsigned int prop_active_profile = -1;
	const char *prop_id = NULL, *prop_description = NULL, *prop_svg = NULL;
	_cleanup_(freep) char *path = NULL;
	int r;

	r = get_device_path(ctl, device, &path, &error);
	if (r < 0)
		goto exit;

	r = sd_bus_call_method(ctl->bus,
			       "org.freedesktop.ratbag1",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &error,
			       &reply,
			       "s",
			       "org.freedesktop.ratbag1.Device");
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 'a', "{sv}");
	if (r < 0)
		goto exit;

	while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
		const char *property, *profile;

		r = sd_bus_message_read_basic(reply, 's', &property);
		if (r < 0)
			goto exit;

		if (streq(property, "Id")) {
			r = sd_bus_message_read(reply, "v", "s",
						&prop_id);
		} else if (streq(property, "Description")) {
			r = sd_bus_message_read(reply, "v", "s",
						&prop_description);
		} else if (streq(property, "Svg")) {
			r = sd_bus_message_read(reply, "v", "s", &prop_svg);
		} else if (streq(property, "Profiles")) {
			r = sd_bus_message_enter_container(reply, 'v', "ao");
			if (r < 0)
				goto exit;

			r = sd_bus_message_enter_container(reply, 'a', "o");
			if (r < 0)
				goto exit;

			while ((r = sd_bus_message_read_basic(reply,
							      'o',
							      &profile)) > 0) {
				unsigned int index;

				r = show_device_get_profile_index(ctl,
								  profile,
								  &index);
				if (r < 0)
					goto exit;

				if (index < prop_min_index)
					prop_min_index = index;
				if (index >= prop_max_index)
					prop_max_index = index + 1;
			}
			if (r < 0)
				goto exit;

			r = sd_bus_message_exit_container(reply);
			if (r < 0)
				goto exit;

			r = sd_bus_message_exit_container(reply);
		} else if (streq(property, "ActiveProfile")) {
			r = sd_bus_message_read(reply, "v", "u", &prop_active_profile);
			if (r < 0)
				goto exit;
		} else {
			r = sd_bus_message_skip(reply, "v");
		}
		if (r < 0)
			goto exit;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			goto exit;
	}
	if (r < 0)
		goto exit;

	r = sd_bus_message_exit_container(reply);
	if (r < 0)
		goto exit;

	printf("%s - %s\n", prop_id, prop_description);
		printf("\t            Svg: %s\n", strlen(prop_svg) > 0 ? prop_svg : "<missing>");

	if (prop_min_index == prop_max_index)
		printf("\t       Profiles:\n");
	else if (prop_min_index + 1 == prop_max_index)
		printf("\t       Profiles: %u\n", prop_min_index);
	else
		printf("\t       Profiles: %u - %u\n",
		       prop_min_index, prop_max_index - 1);

	if (prop_active_profile == (unsigned int)-1)
		printf("\t Active Profile: (unknown)\n");
	else
		printf("\t Active Profile: %u\n", prop_active_profile);

exit:
	if (r < 0)
		fprintf(stderr, "Cannot show device: %s\n",
			error.message ? : "Parser error");
	return r;
}

static int verb_show_device(struct ratbagctl *ctl, int argc, char **argv)
{
	static const struct option options[] = {
		{},
	};
	const char *device;
	int c;

	while ((c = getopt_long(argc, argv, "+", options, NULL)) >= 0) {
		switch (c) {
		default:
			return -EINVAL;
		}
	}

	device = argv[optind];

	return show_device_print(ctl, device);
}

static int show_profile_print_resolution(struct ratbagctl *ctl, const char *path)
{
	_cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	unsigned int prop_index = -1,
		     prop_report_rate = -1,
		     prop_xres = -1,
		     prop_yres = -1;
	int r;

	r = sd_bus_call_method(ctl->bus,
			       "org.freedesktop.ratbag1",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &error,
			       &reply,
			       "s",
			       "org.freedesktop.ratbag1.Resolution");
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 'a', "{sv}");
	if (r < 0)
		goto exit;

	while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
		const char *property;

		r = sd_bus_message_read_basic(reply, 's', &property);
		if (r < 0)
			goto exit;

		if (streq(property, "Index")) {
			r = sd_bus_message_read(reply, "v", "u",
						&prop_index);
		} else if (streq(property, "ReportRate")) {
			r = sd_bus_message_read(reply, "v", "u",
						&prop_report_rate);
		} else if (streq(property, "XResolution")) {
			r = sd_bus_message_read(reply, "v", "u", &prop_xres);
		} else if (streq(property, "YResolution")) {
			r = sd_bus_message_read(reply, "v", "u", &prop_yres);
		} else {
			r = sd_bus_message_skip(reply, "v");
		}
		if (r < 0)
			goto exit;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			goto exit;
	}
	if (r < 0)
		goto exit;

	r = sd_bus_message_exit_container(reply);
	if (r < 0)
		goto exit;

	printf("resolution-%u\n", prop_index);

	printf("\t           Index: %u\n", prop_index);
	printf("\t     Report Rate: %uHz\n", prop_report_rate);
	printf("\t      Resolution: %ux%udpi\n", prop_xres, prop_yres);

	printf("\n");

exit:
	if (r < 0)
		fprintf(stderr, "Cannot show resolution: %s\n",
			error.message ? : "Parser error");
	return r;
}

static int show_profile_print_resolutions(struct ratbagctl *ctl,
					  sd_bus_message *m,
					  unsigned int active_resolution,
					  unsigned int default_resolution)
{
	const char *resolution;
	int r;

	r = sd_bus_message_enter_container(m, 'v', "ao");
	if (r < 0)
		return r;

	r = sd_bus_message_enter_container(m, 'a', "o");
	if (r < 0)
		return r;

	while ((r = sd_bus_message_read_basic(m, 'o', &resolution)) > 0)
		show_profile_print_resolution(ctl, resolution);

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return r;

	r = sd_bus_message_exit_container(m);
	if (r < 0)
		return r;

	return 0;
}

static int show_profile_print(struct ratbagctl *ctl,
			      const char *device,
			      const char *profile)
{
	_cleanup_(sd_bus_error_free) sd_bus_error error = SD_BUS_ERROR_NULL;
	_cleanup_(sd_bus_message_unrefp) sd_bus_message *reply = NULL;
	_cleanup_(freep) char *path = NULL;
	unsigned int prop_index = -1, prop_active_resolution = -1;
	unsigned int prop_default_resolution = -1;
	int r;

	r = get_profile_path(ctl, device, profile, &path, &error);
	if (r < 0)
		goto exit;

	r = sd_bus_call_method(ctl->bus,
			       "org.freedesktop.ratbag1",
			       path,
			       "org.freedesktop.DBus.Properties",
			       "GetAll",
			       &error,
			       &reply,
			       "s",
			       "org.freedesktop.ratbag1.Profile");
	if (r < 0)
		goto exit;

	r = sd_bus_message_enter_container(reply, 'a', "{sv}");
	if (r < 0)
		goto exit;

	while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
		const char *property;

		r = sd_bus_message_read_basic(reply, 's', &property);
		if (r < 0)
			goto exit;

		if (streq(property, "Index")) {
			r = sd_bus_message_read(reply, "v", "u",
						&prop_index);
		} else if (streq(property, "ActiveResolution")) {
			r = sd_bus_message_read(reply, "v", "u",
						&prop_active_resolution);
		} else if (streq(property, "DefaultResolution")) {
			r = sd_bus_message_read(reply, "v", "u",
						&prop_default_resolution);
		} else {
			r = sd_bus_message_skip(reply, "v");
		}
		if (r < 0)
			goto exit;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			goto exit;
	}
	if (r < 0)
		goto exit;

	r = sd_bus_message_rewind(reply, 0);
	if (r < 0)
		goto exit;

	printf("profile-%u\n", prop_index);

	printf("\t           Index: %u\n", prop_index);

	while ((r = sd_bus_message_enter_container(reply, 'e', "sv")) > 0) {
		const char *property;

		r = sd_bus_message_read_basic(reply, 's', &property);
		if (r < 0)
			goto exit;

		if (streq(property, "Resolutions")) {
			r = show_profile_print_resolutions(ctl,
							   reply,
							   prop_active_resolution,
							   prop_default_resolution);
		} else {
			r = sd_bus_message_skip(reply, "v");
		}
		if (r < 0)
			goto exit;

		r = sd_bus_message_exit_container(reply);
		if (r < 0)
			goto exit;
	}
	if (r < 0)
		goto exit;

	r = sd_bus_message_exit_container(reply);
exit:
	if (r < 0)
		fprintf(stderr, "Cannot show device: %s\n",
			error.message ? : "Parser error");
	return r;
}

static int verb_show_profile(struct ratbagctl *ctl, int argc, char **argv)
{
	static const struct option options[] = {
		{ "device", required_argument, NULL, 'd' },
		{},
	};
	const char *device = NULL, *profile = NULL;
	int c;

	while ((c = getopt_long(argc, argv, "+d:", options, NULL)) >= 0) {
		switch (c) {
		case 'd':
			device = optarg;
			break;
		default:
			return -EINVAL;
		}
	}

	profile = argv[optind];

	return show_profile_print(ctl, device, profile);
}

static const struct {
	const char *verb;
	const char *help;
	int (*dispatch) (struct ratbagctl *ctl, int argc, char **argv);
	void (*long_help) (void);
} verbs[] = {
	{ "list-devices", "List available configurable mice", verb_list_devices, NULL },
	{ "show-device", "Show device information", verb_show_device, NULL },
	{ "show-profile", "Show profile information", verb_show_profile, NULL },
	{ "help", "Show help for a command", verb_help, NULL },
	{},
};

static void help(void)
{
	unsigned int i;

	printf("%s [OPTIONS..] COMMAND [OPTIONS..]\n\n"
	       "Query or modify configurable mice.\n\n"
	       "Commands:\n",
	       program_invocation_short_name);

	for (i = 0; i < ELEMENTSOF(verbs); ++i) {
		if (verbs[i].help != NULL)
			printf("  %-12s  %s\n", verbs[i].verb, verbs[i].help);
	}
}

static int verb_help(struct ratbagctl *ctl, int argc, char **argv)
{
	static const struct option options[] = {
		{},
	};
	const char *verb;
	unsigned int i;
	int c;

	while ((c = getopt_long(argc, argv, "+", options, NULL)) >= 0) {
		switch (c) {
		default:
			return -EINVAL;
		}
	}

	verb = argv[optind];
	if (!verb) {
		help();
		return 1;
	} else {
		for (i = 0; i < ELEMENTSOF(verbs); ++i) {
			if (!streq_ptr(verb, verbs[i].verb))
				continue;

			if (verbs[i].long_help)
				verbs[i].long_help();
			else
				printf("%s: No help available for '%s'\n",
				       program_invocation_short_name, verb);

			return 1;
		}
	}

	fprintf(stderr, "%s: Unknown verb '%s'\n", program_invocation_short_name, verb);
	return 1;
}

static void version(void)
{
	printf("%s\n", VERSION);
}

static int ratbagctl_dispatch(struct ratbagctl *ctl, int argc, char **argv)
{
	static const struct option options[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "version", no_argument, NULL, 'V' },
		{},
	};
	char *fake[] = { (char*)verbs[0].verb, NULL };
	const char *verb;
	unsigned int i;
	int c;

	/* '+' makes getopt_long() stop at the first non-argument */
	while ((c = getopt_long(argc, argv, "+hV", options, NULL)) >= 0) {
		switch (c) {
		case 'h':
			help();
			return 1;
		case 'V':
			version();
			return 1;
		default:
			return -EINVAL;
		}
	}

	verb = argv[optind];
	if (!verb) {
		assert(argc - optind == 0);

		argc = ELEMENTSOF(fake) - 1;
		argv = fake;
		verb = fake[0];
		optind = 0;

		assert(verb);
	}

	for (i = 0; i < ELEMENTSOF(verbs); ++i) {
		if (!streq_ptr(verb, verbs[i].verb))
			continue;

		argc -= optind;
		argv += optind;
		optind = 0;

		return verbs[i].dispatch(ctl, argc, argv);
	}

	fprintf(stderr, "%s: Missing or unknown command\n",
		program_invocation_short_name);

	return -EINVAL;
}

int main(int argc, char **argv)
{
	struct ratbagctl *ctl = NULL;
	int r;

	r = ratbagctl_new(&ctl);
	if (r < 0)
		goto exit;

	r = ratbagctl_dispatch(ctl, argc, argv);

exit:
	ratbagctl_free(ctl);

	if (r < 0) {
		errno = -r;
		fprintf(stderr, "%s: Failed: %m\n",
			program_invocation_short_name);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
