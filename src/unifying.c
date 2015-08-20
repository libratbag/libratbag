/*
 * Logitech Unifying Receiver library.
 *
 * Copyright 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013 Red Hat, Inc
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

/*
 * Based on the HID++ 1.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#include <linux/types.h>
#include <linux/input.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

#include <unifying.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#define DEV_DIR "/dev"
#define HIDRAW_DEV_NAME "hidraw"

int unifying_open_receiver(const char *hidraw) {
	struct hidraw_devinfo info;
	int fd, res;
	/* Open the Device with non-blocking reads. */
	fd = open(hidraw, O_RDWR);

	if (fd < 0) {
		perror("Unable to open device");
		goto out_err;
	}

	/* Get Raw Info */
	res = ioctl(fd, HIDIOCGRAWINFO, &info);
	if (res < 0) {
		perror("error while getting info from device");
	} else {
		/* Check if the device is an Unifying receiver */
		if (info.bustype != BUS_USB ||
		    info.vendor != USB_VENDOR_ID_LOGITECH ||
		    (info.product != USB_DEVICE_ID_UNIFYING_RECEIVER &&
		     info.product != USB_DEVICE_ID_UNIFYING_RECEIVER_2))
			goto out_err;
	}

	return fd;

out_err:
	if (fd >= 0)
		close(fd);
	return fd >= 0 ? -1 : fd;
}

/**
 * Filter for the AutoDevProbe scandir on /dev.
 *
 * @param dir The current directory entry provided by scandir.
 *
 * @return Non-zero if the given directory entry starts with "hidraw", or zero
 * otherwise.
 */
static int is_hidraw_device(const struct dirent *dir) {
	return strncmp(HIDRAW_DEV_NAME, dir->d_name, 6) == 0;
}

/**
 * Scans all /dev/hidraw*, open the first unifying receiver.
 *
 * @return The hidraw device file descriptor of the first unifying receiver.
 */
int unifying_find_receiver(void) {
	struct dirent **namelist;
	int fd = -1;
	char fname[64];
	int i, ndev;

	ndev = scandir(DEV_DIR, &namelist, is_hidraw_device, alphasort);
	if (ndev <= 0)
		return fd;

	for (i = 0; i < ndev; i++)
	{
		if (fd < 0) {
			snprintf(fname, sizeof(fname),
				 "%s/%s", DEV_DIR, namelist[i]->d_name);
			fd = unifying_open_receiver(fname);
		}

		free(namelist[i]);
	}

	free(namelist);

	if (fd >= 0)
		printf("Using %s as the Unifying receiver.\n", fname);
	else
		printf("Unable to find an Unifying receiver.\n");

	return fd;
}
