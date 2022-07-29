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

#include <config.h>
#include <errno.h>
#include <fcntl.h>

#include <hidpp10.h>
#include <libratbag-util.h>

static inline int
dump_page(struct hidpp10_device *dev, size_t page, size_t offset)
{
	int rc = 0;
	uint8_t bytes[16];

	while (offset < 512) {
		hidpp_log_info(&dev->base, "page 0x%02zx off 0x%03zx: ", page, offset);
		rc = hidpp10_read_memory(dev, page, offset, bytes);
		if (rc != 0)
			break;

		hidpp_log_buffer(&dev->base, HIDPP_LOG_PRIORITY_INFO, " ", bytes, ARRAY_LENGTH(bytes));
		offset += 16;
	}

	return rc;
}

static inline int
dump_all_pages(struct hidpp10_device *dev)
{
	uint8_t page;
	int rc = 0;

	for (page = 0; page < 31; page++) {
		rc = dump_page(dev, page, 0);
		if (rc != 0)
			break;
	}

	/* We dumped at least one page successfully and get EAGAIN, so we're
	 * on the last page. Overwrite the last line with a blank one so it
	 * doesn't look like an error */
	if (page > 0 && rc == EAGAIN) {
		hidpp_log_info(&dev->base, "\r                                   \n");
		rc = 0;
	}

	return rc;
}

static void
usage(void)
{
	printf("Usage: %s [page] [offset] /dev/hidraw0\n", program_invocation_short_name);
}

int
main(int argc, char **argv)
{
	_cleanup_close_ int fd = 0;
	const char *path;
	size_t page = 0, offset = 0;
	struct hidpp10_device *dev = NULL;
	struct hidpp_device base;
	int rc;

	if (argc < 2 || argc > 4) {
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
	rc = hidpp10_device_new(&base, HIDPP_WIRED_DEVICE_IDX, HIDPP10_PROFILE_UNKNOWN, 5, &dev);
	if (rc)
		return rc;

	if (argc == 2)
		rc = dump_all_pages(dev);
	else {
		page = atoi(argv[1]);
		if (argc > 3)
			offset = atoi(argv[2]);
		rc = dump_page(dev, page, offset);
	}

	hidpp10_device_destroy(dev);

	return rc;
}
