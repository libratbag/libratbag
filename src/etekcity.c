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
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define ETEKCITY_PROFILE_MAX			4
#define ETEKCITY_BUTTON_MAX			10

#define ETEKCITY_REPORT_ID_CONFIGURE_PROFILE	4
#define ETEKCITY_REPORT_ID_PROFILE		5
#define ETEKCITY_REPORT_ID_KEY_MAPPING		7

#define ETEKCITY_REPORT_SIZE_PROFILE		50

#define ETEKCITY_CONFIG_KEY_MAPPING		0x20

struct etekcity_data {
	uint8_t profiles[(ETEKCITY_PROFILE_MAX + 1)][ETEKCITY_REPORT_SIZE_PROFILE];
};

static char *
print_key(uint8_t key)
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
etekcity_has_capability(const struct ratbag *ratbag, enum ratbag_capability cap)
{
	switch (cap) {
	case RATBAG_CAP_NONE:
		return 0;
	case RATBAG_CAP_SWITCHABLE_RESOLUTION:
	case RATBAG_CAP_SWITCHABLE_PROFILE:
	case RATBAG_CAP_BUTTON_PROFILES:
	case RATBAG_CAP_BUTTON_KEY:
	case RATBAG_CAP_BUTTON_MACROS:
		return 1;
	}
	return 0;
}

static int
etekcity_current_profile(struct ratbag *ratbag)
{
	uint8_t buf[3];
	int ret;

	ret = ratbag_hidraw_raw_request(ratbag, ETEKCITY_REPORT_ID_PROFILE, buf,
			sizeof(buf), HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	if (ret != 3)
		return -EIO;

	return buf[2];
}

static int
etekcity_set_current_profile(struct ratbag *ratbag, unsigned int index)
{
	uint8_t buf[] = {ETEKCITY_REPORT_ID_PROFILE, 0x03, index};
	int ret;

	if (index > ETEKCITY_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(ratbag, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	msleep(100);

	return ret == sizeof(buf) ? 0 : ret;
}

static int
etekcity_set_config_profile(struct ratbag *ratbag, uint8_t profile, uint8_t type)
{
	uint8_t buf[] = {ETEKCITY_REPORT_ID_CONFIGURE_PROFILE, profile, type};
	int ret;

	if (profile > ETEKCITY_PROFILE_MAX)
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
	struct etekcity_data *drv_data;
	int rc;
	uint8_t *buf;

	assert(index <= ETEKCITY_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(ratbag);
	buf = drv_data->profiles[index];

	etekcity_set_config_profile(ratbag, index, ETEKCITY_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(ratbag, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, ETEKCITY_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	msleep(100);

	if (rc < 50)
		return;

	log_debug(ratbag->libratbag, "profile: %d %s:%d\n",
		  buf[2],
		  __FILE__, __LINE__);
}

static int
etekcity_write_profile(struct ratbag_profile *profile)
{
	return 0;
}

static inline unsigned
etekcity_button_to_index(unsigned button)
{
	return button < 8 ? button : button + 5;
}

static void
etekcity_read_button(struct ratbag *ratbag, struct ratbag_profile *profile,
		     struct ratbag_button *button)
{
	struct etekcity_data *drv_data = ratbag_get_drv_data(ratbag);
	uint8_t data;
	unsigned index = etekcity_button_to_index(button->index);

	data = drv_data->profiles[profile->index][3 + index * 3];
	if (data)
		log_debug(ratbag->libratbag,
			  " - button%d: %s (%02x) %s:%d\n",
			  button->index, print_key(data), data, __FILE__, __LINE__);
}

static int
etekcity_write_button(struct ratbag *ratbag, struct ratbag_profile *profile,
		      struct ratbag_button *button)
{
	return 0;
}

static int
etekcity_probe(struct ratbag *ratbag, const struct ratbag_id id)
{
	int rc;
	struct ratbag_profile *profile;
	struct etekcity_data *drv_data;

	log_debug(ratbag->libratbag, "data: %d\n", id.data);

	rc = ratbag_open_hidraw(ratbag);
	if (rc) {
		log_error(ratbag->libratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	if (!drv_data)
		return -ENODEV;

	ratbag_set_drv_data(ratbag, drv_data);

	ratbag->num_profiles = ETEKCITY_PROFILE_MAX;
	ratbag->num_buttons = ETEKCITY_BUTTON_MAX;

	profile = ratbag_get_active_profile(ratbag);

	if (!profile) {
		log_error(ratbag->libratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		rc = -ENODEV;
		goto err;
	}

	log_info(ratbag->libratbag,
		 "'%s' is in profile %d\n",
		 ratbag_get_name(ratbag),
		 profile->index);

	profile = ratbag_profile_unref(profile);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(ratbag, NULL);
	return rc;
}

static void
etekcity_remove(struct ratbag *ratbag)
{
	free(ratbag_get_drv_data(ratbag));
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
	.remove = etekcity_remove,
	.read_profile = etekcity_read_profile,
	.write_profile = etekcity_write_profile,
	.get_active_profile = etekcity_current_profile,
	.set_active_profile = etekcity_set_current_profile,
	.has_capability = etekcity_has_capability,
	.read_button = etekcity_read_button,
	.write_button = etekcity_write_button,
};
