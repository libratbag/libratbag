/*
 * M705 button 6 toggler.
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
