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
#include "shared-macro.h"

struct ratbagctl {
	int unused;
};

static int verb_help(struct ratbagctl *ctl, int argc, char **argv);

static struct ratbagctl *ratbagctl_free(struct ratbagctl *ctl)
{
	if (!ctl)
		return NULL;

	free(ctl);

	return NULL;
}

static int ratbagctl_new(struct ratbagctl **out)
{
	struct ratbagctl *ctl;

	ctl = calloc(1, sizeof(*ctl));
	if (!ctl)
		return -ENOMEM;

	*out = ctl;
	return 0;
}

static int verb_list(struct ratbagctl *ctl, int argc, char **argv)
{
	return 0;
}

static const struct {
	const char *verb;
	const char *help;
	int (*dispatch) (struct ratbagctl *ctl, int argc, char **argv);
	void (*long_help) (void);
} verbs[] = {
	{ "list", "List available configurable mice", verb_list, NULL },
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
