/*
 * Copyright © 2017 Red Hat, Inc.
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

#include <config.h>
#include <errno.h>
#include <fcntl.h>

#include <hidpp20.h>
#include <libratbag-util.h>

static inline int
reset_sector(struct hidpp20_device *dev, uint16_t sector_size,  uint16_t sector)
{
	int rc = 0;
	_cleanup_free_ uint8_t *data = NULL;

	data = zalloc(sector_size);

	/* this returns error 4: hardware error and is expected */
	rc = hidpp20_onboard_profiles_write_sector(dev, sector, sector_size, data, false);
	if (rc == 4)
		rc = 0;

	return rc;
}

static int
reset_all_sectors(struct hidpp20_device *dev, uint16_t sector_size)
{
	uint8_t sector;
	int rc = 0;

	for (sector = 1; sector < 31; sector++) {
		rc = reset_sector(dev, sector_size, sector);
		if (rc != 0)
			break;
	}

	if (!rc)
		rc = reset_sector(dev, sector_size, 0);

	return rc;
}

static void
usage(void)
{
	printf("Usage: %s [sector] /dev/hidraw0\n", program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	_cleanup_close_ int fd = -1;
	const char *path;
	size_t sector = 0;
	struct hidpp20_device *dev = NULL;
	struct hidpp_device base;
	struct hidpp20_onboard_profiles_info info = { 0 };
	int rc;

	if (argc < 2 || argc > 3) {
		usage();
		return 1;
	}

	path = argv[argc - 1];
	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "Failed to open path '%s': %s", path, strerror(errno));
		exit(3);
	}

	hidpp_device_init(&base, fd);
	dev = hidpp20_device_new(&base, 0xff, NULL, 0);
	if (!dev) {
		fprintf(stderr, "Failed to open %s as a HID++ 2.0 device", path);
		exit(3);
	}

	hidpp20_onboard_profiles_get_profiles_desc(dev, &info);

	if (argc == 2)
		rc = reset_all_sectors(dev, info.sector_size);
	else {
		sector = atoi(argv[1]);
		rc = reset_sector(dev, info.sector_size, sector);
	}

	hidpp20_device_destroy(dev);

	return rc;
}
