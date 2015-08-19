/*
 * Logitech Unifying Receiver library.
 *
 * Copyright 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
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
