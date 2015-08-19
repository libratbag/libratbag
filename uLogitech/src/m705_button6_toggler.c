/*
 * M705 button 6 toggler.
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
#include <stdio.h>
#include <unistd.h>

#include <unifying.h>
#include <hidpp10.h>

int main(int argc, char **argv)
{
	struct unifying_device dev;
	int fd;
	int res;

	/* Open the Unifying Receiver. */
	if (argc == 1)
		fd = unifying_find_receiver();
	else {
		fd = unifying_open_receiver(argv[1]);
		if (fd < 0)
			perror("Unable to open device");
	}

	if (fd < 0)
		return 1;

	res = hidpp10_get_device_from_wpid(fd, 0x101b, &dev);

	if (!res) {
		printf("M705 found at index %d: fw RR %02x.%02x, build %04x\n", dev.index, dev.fw_major, dev.fw_minor, dev.build);

		/* M705 with FW RR 17.01 - build 0017 */
		if (dev.fw_major == 0x17 &&
		    dev.fw_minor == 0x01 &&
		    dev.build == 0x0015)
			hidpp10_toggle_individual_feature(fd, &dev, FEATURE_BIT_R0_SPECIAL_BUTTON_FUNCTION, -1);
	}

	close(fd);
	return 0;
}
