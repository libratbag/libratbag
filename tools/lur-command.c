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

#include "config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#include <liblur.h>
#include <libratbag-util.h>

enum options {
	OPT_HELP,
};

static struct lur_receiver *
open_receiver(const char *path)
{
	struct lur_receiver *receiver = NULL;
	int fd = -1;
	int rc;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Failed to open %s (%s)\n", path, strerror(errno));
		return NULL;
	}

	rc = lur_receiver_new_from_hidraw(fd, NULL, &receiver);
	if (rc != 0)
		close(fd);

	return receiver;
}

static void
list_connected_devices(struct lur_receiver *receiver)
{
	_cleanup_free_ struct lur_device **devices = NULL;
	int ndevices;
	int i;

	ndevices = lur_receiver_enumerate(receiver, &devices);
	if (ndevices < 0) {
		fprintf(stderr, "Failed to enumerate devices\n");
		return;
	} else if (ndevices == 0) {
		fprintf(stderr, "No devices connected to this receiver\n");
		return;
	}

	for (i = 0; i < ndevices; i++) {
		struct lur_device *dev = devices[i];
		const char *name, *strtype;
		enum lur_device_type type;
		uint32_t serial;

		name = lur_device_get_name(dev);
		type = lur_device_get_type(dev);
		serial = lur_device_get_serial(dev);

		switch(type) {
		case LUR_DEVICE_TYPE_UNKNOWN:	strtype = "unknown";	break;
		case LUR_DEVICE_TYPE_KEYBOARD:	strtype = "keyboard";	break;
		case LUR_DEVICE_TYPE_MOUSE:	strtype = "mouse";	break;
		case LUR_DEVICE_TYPE_NUMPAD:	strtype = "numpad";	break;
		case LUR_DEVICE_TYPE_PRESENTER: strtype = "presenter";	break;
		case LUR_DEVICE_TYPE_TRACKBALL:	strtype = "trackball";	break;
		case LUR_DEVICE_TYPE_TOUCHPAD:	strtype = "touchpad";	break;
		default:
			strtype = "<invalid>";
			break;
		}

		printf("%d: %s (%s) serial %#x\n", i, name, strtype, serial);

		lur_device_unref(dev);
	}
}

static void
disconnect_device(struct lur_receiver *receiver, int index)
{
	_cleanup_free_ struct lur_device **devices = NULL;
	int ndevices;
	int i;

	ndevices = lur_receiver_enumerate(receiver, &devices);
	if (ndevices < 0) {
		fprintf(stderr, "Failed to enumerate devices\n");
		return;
	} else if (ndevices == 0) {
		fprintf(stderr, "No devices connected to this receiver\n");
		return;
	}

	if (index < 0 || index >= ndevices) {
		fprintf(stderr, "Invalid index %d, only %d devices connected\n",
			index, ndevices);
	} else {
		lur_device_disconnect(devices[index]);
	}

	for (i = 0; i < ndevices; i++)
		lur_device_unref(devices[i]);
}

static int
filter_hidraw(const struct dirent *entry)
{
	return strneq(entry->d_name, "hidraw", 6);
}

static void
find_receiver(void)
{
	_cleanup_free_ struct dirent **hidraw_list = NULL;
	int n, i;
	char path[PATH_MAX] = {0};
	bool found = false;

	n = scandir("/dev/", &hidraw_list, filter_hidraw, alphasort);
	if (n < 0)
		return;

	for (i = 0; i < n; i++) {
		struct lur_receiver *receiver;
		sprintf_safe(path, "/dev/%s", hidraw_list[i]->d_name);
		receiver = open_receiver(path);
		if (receiver) {
			found = true;
			printf("%s\n", path);
			lur_receiver_unref(receiver);
		}
		free(hidraw_list[i]);
	}

	if (!found)
		fprintf(stderr, "No receivers found.\n");
}

static void
usage(void)
{
	printf("Usage: %s COMMAND /dev/hidrawX\n"
	       "\n"
	       "Commands:\n"
	       "  list ............. list devices connected to receiver\n"
	       "  open ............. open receiver for pairing (timeout 30s)\n"
	       "  close ............ close receiver if currently open\n"
	       "  disconnect N ..... disconnect device N\n"
	       "  find ............. find a receiver amongst the /dev/hidraw devices \n",
	       program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	struct lur_receiver *receiver;
	const char *path;
	const char *command;

	while (1) {
		int c;
		int option_index = 0;
		static struct option opts[] = {
			{ "help", 0, 0, OPT_HELP },
		};

		c = getopt_long(argc, argv, "+h", opts, &option_index);
		if (c == -1)
			break;
		switch(c) {
		case 'h':
		case OPT_HELP:
			usage();
			return EXIT_SUCCESS;
		default:
			usage();
			return EXIT_FAILURE;
		}
	}

	if (argc < 2) {
		usage();
		return EXIT_FAILURE;
	}

	command = argv[1];
	if (streq(command, "find")) {
		find_receiver();
		return EXIT_SUCCESS;
	}

	if (argc < 3) {
		usage();
		return EXIT_FAILURE;
	}

	path = argv[argc - 1];
	receiver = open_receiver(path);
	if (!receiver) {
		fprintf(stderr, "Failed to open receiver at %s\n", path);
		return EXIT_FAILURE;
	}

	if (streq(command, "list")) {
		list_connected_devices(receiver);
	} else if (streq(command, "open")) {
		lur_receiver_open(receiver, 0);
	} else if (streq(command, "close")) {
		lur_receiver_close(receiver);
	} else if (streq(command, "disconnect")) {
		int index;

		if (argc < 4) {
			usage();
			return EXIT_FAILURE;
		}

		index = atoi(argv[2]);
		disconnect_device(receiver, index);
	} else {
		usage();
		return EXIT_FAILURE;
	}

	lur_receiver_unref(receiver);

	return EXIT_SUCCESS;
}
