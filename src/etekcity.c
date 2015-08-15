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
#include <assert.h>
#include <errno.h>
#include <linux/input.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define ETEKCITY_PROFILE_MAX			4

#define ETEKCITY_REPORT_ID_KEY_MAPPING		7

static char *
print_key(__u8 key)
{
	switch (key) {
	case 1: return "BTN_LEFT";
	case 2: return "BTN_RIGHT";
	case 3: return "BTN_MIDDLE";
	case 4: return "2 x BTN_LEFT";
	case 7: return "BTN_EXTRA";
	case 8: return "BTN_SIDE";
	case 9: return "REL_WHEEL 1";
	case 10: return "REL_WHEEL -1";
	case 11: return "REL_HWHEEL -1";
	case 12: return "REL_HWHEEL 1";

	/* DPI switch */
	case 13: return "DPI cycle";
	case 14: return "DPI++";
	case 15: return "DPI--";

	/* Profile */
	case 18: return "profile cycle";
	case 19: return "profile++";
	case 20: return "profile--";

	case 21: return "HOLD BTN_LEFT ON/OFF";

	/* multimedia */
	case 25: return "KEY_CONFIG";
	case 26: return "KEY_PREVIOUSSONG";
	case 27: return "KEY_NEXTSONG";
	case 28: return "KEY_PLAYPAUSE";
	case 29: return "KEY_STOPCD";
	case 30: return "KEY_MUTE";
	case 31: return "KEY_VOLUMEUP";
	case 32: return "KEY_VOLUMEDOWN";

	/* windows */
	case 33: return "KEY_CALC";
	case 34: return "KEY_MAIL";
	case 35: return "KEY_BOOKMARKS";
	case 36: return "KEY_FORWARD";
	case 37: return "KEY_BACK";
	case 38: return "KEY_STOP";
	case 39: return "KEY_FILE";
	case 40: return "KEY_REFRESH";
	case 41: return "KEY_HOMEPAGE";
	case 42: return "KEY_SEARCH";
	}

	return "UNKNOWN";
}

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
etekcity_set_config_profile(struct ratbag *ratbag, __u8 profile, __u8 type)
{
	__u8 buf[] = {0x04, profile, type};
	int ret;

	if (profile > 5)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(ratbag, buf[0], buf, sizeof(buf),
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	msleep(100);

	return ret == sizeof(buf) ? 0 : ret;
}

static void
etekcity_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag *ratbag = profile->ratbag;
	int i, rc;
	__u8 buf[50];
	__u8 data;

	assert(index <= ETEKCITY_PROFILE_MAX);

	etekcity_set_config_profile(ratbag, index, 0x20);
	rc = ratbag_hidraw_raw_request(ratbag, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, sizeof(buf), HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	if (rc < 50)
		return;

	log_debug(ratbag->libratbag, "profile: %d %s:%d\n",
		  buf[2],
		  __FILE__, __LINE__);

	for (i = 0; i < 16; i++) {
		data = buf[3 + i * 3];
		if (data)
			log_debug(ratbag->libratbag,
				  " - button%d: %s (%02x) %s:%d\n",
				  i,
				  print_key(data),
				  data,
				  __FILE__, __LINE__);
	}
}

static int
etekcity_probe(struct ratbag *ratbag, const struct ratbag_id id)
{
	int rc;
	struct ratbag_profile *profile;

	log_debug(ratbag->libratbag, "data: %d\n", id.data);

	rc = ratbag_open_hidraw(ratbag);
	if (rc) {
		log_error(ratbag->libratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	ratbag->num_profiles = ETEKCITY_PROFILE_MAX;

	profile = ratbag_get_active_profile(ratbag);

	if (!profile) {
		log_error(ratbag->libratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	log_info(ratbag->libratbag,
		 "'%s' is in profile %d\n",
		 ratbag_get_name(ratbag),
		 profile->index);

	profile = ratbag_profile_unref(profile);

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
	.read_profile = etekcity_read_profile,
	.get_active_profile = etekcity_current_profile,
};
