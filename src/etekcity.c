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
#include <errno.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

static int
etekcity_current_profile(struct ratbag *ratbag)
{
	__u8 buf[3];
	int ret;

	ret = ratbag_hidraw_raw_request(ratbag, 5, buf, sizeof(buf),
				 HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	if (ret != 3)
		return -EIO;

	return buf[2];
}

static int
etekcity_probe(struct ratbag *ratbag, const struct ratbag_id id)
{
	int rc, current_profile;

	log_debug(ratbag->libratbag, "data: %d\n", id.data);

	rc = ratbag_open_hidraw(ratbag);
	if (rc) {
		log_error(ratbag->libratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	rc = etekcity_current_profile(ratbag);
	if (rc < 0) {
		log_error(ratbag->libratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	current_profile = rc;

	log_info(ratbag->libratbag,
		 "'%s' is in profile %d\n",
		 ratbag_get_name(ratbag),
		 current_profile);

	return 0;
}

static const struct ratbag_id etekcity_table[] = {
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_ETEKCITY,
		 .product = USB_DEVICE_ID_ETEKCITY_SCROLL_ALPHA,
		 .version = VERSION_ANY },
	 .data = 1,
	},

	{ },
};

struct ratbag_driver etekcity_driver = {
	.name = "EtekCity",
	.table_ids = etekcity_table,
	.probe = etekcity_probe,
};
