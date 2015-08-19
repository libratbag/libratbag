/*
 * Paring tool.
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
	int fd;

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

	hidpp10_open_lock(fd);
	printf("The receiver is ready to pair a new device.\n"
		"Switch your device on to pair it.\n");

	close(fd);
	return 0;
}
