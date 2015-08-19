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

static enum ratbag_button_action_type
etekcity_raw_to_action(uint8_t data, enum ratbag_button_type button_type)
{
	if (button_type == RATBAG_BUTTON_TYPE_UNKNOWN)
		return RATBAG_BUTTON_ACTION_TYPE_NONE;

	return RATBAG_BUTTON_ACTION_TYPE_BUTTON;
}

struct etekcity_button_mapping {
	uint8_t raw;
	enum ratbag_button_type type;
};

static struct etekcity_button_mapping etekcity_button_mapping[] = {
	{ 0, RATBAG_BUTTON_TYPE_NONE },
	{ 1, RATBAG_BUTTON_TYPE_LEFT },
	{ 2, RATBAG_BUTTON_TYPE_RIGHT },
	{ 3, RATBAG_BUTTON_TYPE_MIDDLE },
//	{ 4, "2 x BTN_LEFT" },
	{ 7, RATBAG_BUTTON_TYPE_EXTRA },
	{ 8, RATBAG_BUTTON_TYPE_SIDE },
	{ 9, RATBAG_BUTTON_TYPE_WHEEL_UP },
	{ 10, RATBAG_BUTTON_TYPE_WHEEL_DOWN },
	{ 11, RATBAG_BUTTON_TYPE_WHEEL_LEFT },
	{ 12, RATBAG_BUTTON_TYPE_WHEEL_RIGHT },

	/* DPI switch */
	{ 13, RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP },
	{ 14, RATBAG_BUTTON_TYPE_RESOLUTION_UP },
	{ 15, RATBAG_BUTTON_TYPE_RESOLUTION_DOWN },

	/* Profile */
	{ 18, RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP },
	{ 19, RATBAG_BUTTON_TYPE_PROFILE_UP },
	{ 20, RATBAG_BUTTON_TYPE_PROFILE_DOWN },

//	{ 21, "HOLD BTN_LEFT ON/OFF" },

	/* multimedia */
	{ 25, RATBAG_BUTTON_TYPE_KEY_CONFIG },
	{ 26, RATBAG_BUTTON_TYPE_KEY_PREVIOUSSONG },
	{ 27, RATBAG_BUTTON_TYPE_KEY_NEXTSONG },
	{ 28, RATBAG_BUTTON_TYPE_KEY_PLAYPAUSE },
	{ 29, RATBAG_BUTTON_TYPE_KEY_STOPCD },
	{ 30, RATBAG_BUTTON_TYPE_KEY_MUTE },
	{ 31, RATBAG_BUTTON_TYPE_KEY_VOLUMEUP },
	{ 32, RATBAG_BUTTON_TYPE_KEY_VOLUMEDOWN },

	/* windows */
	{ 33, RATBAG_BUTTON_TYPE_KEY_CALC },
	{ 34, RATBAG_BUTTON_TYPE_KEY_MAIL },
	{ 35, RATBAG_BUTTON_TYPE_KEY_BOOKMARKS },
	{ 36, RATBAG_BUTTON_TYPE_KEY_FORWARD },
	{ 37, RATBAG_BUTTON_TYPE_KEY_BACK },
	{ 38, RATBAG_BUTTON_TYPE_KEY_STOP },
	{ 39, RATBAG_BUTTON_TYPE_KEY_FILE },
	{ 40, RATBAG_BUTTON_TYPE_KEY_REFRESH },
	{ 41, RATBAG_BUTTON_TYPE_KEY_HOMEPAGE },
	{ 42, RATBAG_BUTTON_TYPE_KEY_SEARCH },
};

static enum ratbag_button_type
etekcity_raw_to_button_type(uint8_t data)
{
	struct etekcity_button_mapping *mapping;

	ARRAY_FOR_EACH(etekcity_button_mapping, mapping) {
		if (mapping->raw == data)
			return mapping->type;
	}

	return RATBAG_BUTTON_TYPE_UNKNOWN;
}

static uint8_t
etekcity_button_type_to_raw(enum ratbag_button_type type)
{
	struct etekcity_button_mapping *mapping;

	ARRAY_FOR_EACH(etekcity_button_mapping, mapping) {
		if (mapping->type == type)
			return mapping->raw;
	}

	return 0;
}

static int
etekcity_has_capability(const struct ratbag_device *device, enum ratbag_capability cap)
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
etekcity_current_profile(struct ratbag_device *device)
{
	uint8_t buf[3];
	int ret;

	ret = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_PROFILE, buf,
			sizeof(buf), HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	if (ret != 3)
		return -EIO;

	return buf[2];
}

static int
etekcity_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	uint8_t buf[] = {ETEKCITY_REPORT_ID_PROFILE, 0x03, index};
	int ret;

	if (index > ETEKCITY_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	msleep(100);

	return ret == sizeof(buf) ? 0 : ret;
}

static int
etekcity_set_config_profile(struct ratbag_device *device, uint8_t profile, uint8_t type)
{
	uint8_t buf[] = {ETEKCITY_REPORT_ID_CONFIGURE_PROFILE, profile, type};
	int ret;

	if (profile > ETEKCITY_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	msleep(100);

	return ret == sizeof(buf) ? 0 : ret;
}

static void
etekcity_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data;
	int rc;
	uint8_t *buf;

	assert(index <= ETEKCITY_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(device);
	buf = drv_data->profiles[index];

	etekcity_set_config_profile(device, index, ETEKCITY_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, ETEKCITY_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	msleep(100);

	if (rc < 50)
		return;

	log_debug(device->ratbag, "profile: %d %s:%d\n",
		  buf[2],
		  __FILE__, __LINE__);
}

static int
etekcity_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	unsigned int index = profile->index;
	struct etekcity_data *drv_data;
	int rc;
	uint8_t *buf;

	assert(index <= ETEKCITY_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(device);
	buf = drv_data->profiles[index];

	etekcity_set_config_profile(device, index, ETEKCITY_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, ETEKCITY_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	msleep(100);

	if (rc < 50)
		return -EIO;

	log_debug(device->ratbag, "profile: %d written %s:%d\n",
		  buf[2],
		  __FILE__, __LINE__);
	return 0;
}

static inline unsigned
etekcity_button_to_index(unsigned button)
{
	return button < 8 ? button : button + 5;
}

static void
etekcity_read_button(struct ratbag_device *device, struct ratbag_profile *profile,
		     struct ratbag_button *button)
{
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	uint8_t data;
	unsigned index = etekcity_button_to_index(button->index);

	data = drv_data->profiles[profile->index][3 + index * 3];
	log_debug(device->ratbag,
		  " - button%d: %s (%02x) %s:%d\n",
		  button->index, print_key(data), data, __FILE__, __LINE__);
	button->type = etekcity_raw_to_button_type(data);
	button->action_type = etekcity_raw_to_action(data, button->type);
}

static int
etekcity_write_button(struct ratbag_device *device, struct ratbag_profile *profile,
		      struct ratbag_button *button)
{
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	uint8_t *data;
	unsigned index = etekcity_button_to_index(button->index);

	data = &drv_data->profiles[profile->index][3 + index * 3];

	if (button->action_type == RATBAG_BUTTON_ACTION_TYPE_BUTTON) {
		*data = etekcity_button_type_to_raw(button->type);
	}

	return 0;
}

static int
etekcity_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct ratbag_profile *profile;
	struct etekcity_data *drv_data;

	log_debug(device->ratbag, "data: %d\n", id.data);

	rc = ratbag_open_hidraw(device);
	if (rc) {
		log_error(device->ratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	if (!drv_data)
		return -ENODEV;

	ratbag_set_drv_data(device, drv_data);

	/* profiles are 0-indexed */
	device->num_profiles = ETEKCITY_PROFILE_MAX + 1;
	device->num_buttons = ETEKCITY_BUTTON_MAX;

	profile = ratbag_device_get_active_profile(device);

	if (!profile) {
		log_error(device->ratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		rc = -ENODEV;
		goto err;
	}

	log_debug(device->ratbag,
		  "'%s' is in profile %d\n",
		  ratbag_device_get_name(device),
		  profile->index);

	profile = ratbag_profile_unref(profile);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static void
etekcity_remove(struct ratbag_device *device)
{
	free(ratbag_get_drv_data(device));
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
