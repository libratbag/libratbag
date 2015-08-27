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
#define ETEKCITY_NUM_DPI			6

#define ETEKCITY_REPORT_ID_CONFIGURE_PROFILE	4
#define ETEKCITY_REPORT_ID_PROFILE		5
#define ETEKCITY_REPORT_ID_SETTINGS		6
#define ETEKCITY_REPORT_ID_KEY_MAPPING		7

#define ETEKCITY_REPORT_SIZE_PROFILE		50
#define ETEKCITY_REPORT_SIZE_SETTINGS		40

#define ETEKCITY_CONFIG_SETTINGS		0x10
#define ETEKCITY_CONFIG_KEY_MAPPING		0x20

struct etekcity_settings_report {
	uint8_t reportID;
	uint8_t twentyHeight;
	uint8_t profileID;
	uint8_t x_sensitivity; /* 0x0a means 0 */
	uint8_t y_sensitivity; /* 0x0a means 0 */
	uint8_t dpi_mask;
	uint8_t xres[6];
	uint8_t yres[6];
	uint8_t current_dpi;
	uint8_t padding1[7];
	uint8_t report_rate;
	uint8_t padding2[4];
	uint8_t light;
	uint8_t light_heartbit;
	uint8_t padding3[5];
} __attribute__((packed));

struct etekcity_data {
	uint8_t profiles[(ETEKCITY_PROFILE_MAX + 1)][ETEKCITY_REPORT_SIZE_PROFILE];
	struct etekcity_settings_report settings[(ETEKCITY_PROFILE_MAX + 1)];
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
	case 6: return "NONE";
	case 8: return "BTN_SIDE";
	case 9: return "REL_WHEEL 1";
	case 10: return "REL_WHEEL -1";
	case 11: return "REL_HWHEEL -1";
	case 12: return "REL_HWHEEL 1";

	/* DPI switch */
	case 13: return "DPI cycle";
	case 14: return "DPI++";
	case 15: return "DPI--";

	case 16: return "Macro";

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

struct etekcity_button_type_mapping {
	uint8_t raw;
	enum ratbag_button_type type;
};

static const struct etekcity_button_type_mapping etekcity_button_type_mapping[] = {
	{ 0, RATBAG_BUTTON_TYPE_LEFT },
	{ 1, RATBAG_BUTTON_TYPE_RIGHT },
	{ 2, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 3, RATBAG_BUTTON_TYPE_EXTRA },
	{ 4, RATBAG_BUTTON_TYPE_SIDE },
	{ 5, RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP },
	{ 6, RATBAG_BUTTON_TYPE_PINKIE },
	{ 7, RATBAG_BUTTON_TYPE_PINKIE2 },
	{ 8, RATBAG_BUTTON_TYPE_WHEEL_UP },
	{ 9, RATBAG_BUTTON_TYPE_WHEEL_DOWN },
};

static enum ratbag_button_type
etekcity_raw_to_button_type(uint8_t data)
{
	const struct etekcity_button_type_mapping *mapping;

	ARRAY_FOR_EACH(etekcity_button_type_mapping, mapping) {
		if (mapping->raw == data)
			return mapping->type;
	}

	return RATBAG_BUTTON_TYPE_UNKNOWN;
}

struct etekcity_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static struct etekcity_button_mapping etekcity_button_mapping[] = {
	{ 1, BUTTON_ACTION_BUTTON(1) },
	{ 2, BUTTON_ACTION_BUTTON(2) },
	{ 3, BUTTON_ACTION_BUTTON(3) },
	{ 4, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },
	{ 6, BUTTON_ACTION_NONE },
	{ 7, BUTTON_ACTION_BUTTON(4) },
	{ 8, BUTTON_ACTION_BUTTON(5) },
	{ 9, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ 10, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
	{ 11, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT) },
	{ 12, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT) },
	{ 13, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 14, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 15, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 16, BUTTON_ACTION_MACRO },
	{ 17, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 18, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ 19, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
	{ 25, BUTTON_ACTION_KEY(KEY_CONFIG) },
	{ 26, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ 27, BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ 28, BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ 29, BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ 30, BUTTON_ACTION_KEY(KEY_MUTE) },
	{ 31, BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ 32, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },
	{ 33, BUTTON_ACTION_KEY(KEY_CALC) },
	{ 34, BUTTON_ACTION_KEY(KEY_MAIL) },
	{ 35, BUTTON_ACTION_KEY(KEY_BOOKMARKS) },
	{ 36, BUTTON_ACTION_KEY(KEY_FORWARD) },
	{ 37, BUTTON_ACTION_KEY(KEY_BACK) },
	{ 38, BUTTON_ACTION_KEY(KEY_STOP) },
	{ 39, BUTTON_ACTION_KEY(KEY_FILE) },
	{ 40, BUTTON_ACTION_KEY(KEY_REFRESH) },
	{ 41, BUTTON_ACTION_KEY(KEY_HOMEPAGE) },
	{ 42, BUTTON_ACTION_KEY(KEY_SEARCH) },
};

static const struct ratbag_button_action*
etekcity_raw_to_button_action(uint8_t data)
{
	struct etekcity_button_mapping *mapping;

	ARRAY_FOR_EACH(etekcity_button_mapping, mapping) {
		if (mapping->raw == data)
			return &mapping->action;
	}

	return NULL;
}

static uint8_t
etekcity_button_action_to_raw(const struct ratbag_button_action *action)
{
	struct etekcity_button_mapping *mapping;

	ARRAY_FOR_EACH(etekcity_button_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
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
	struct ratbag_resolution *resolution;
	struct etekcity_settings_report *setting_report;
	int rc;
	uint8_t *buf, *dpi_list;
	unsigned int report_rate;
	unsigned int i;

	assert(index <= ETEKCITY_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(device);

	setting_report = &drv_data->settings[index];
	buf = (uint8_t*)setting_report;
	etekcity_set_config_profile(device, index, ETEKCITY_CONFIG_SETTINGS);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_SETTINGS,
			buf, ETEKCITY_REPORT_SIZE_SETTINGS,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	if (rc < ETEKCITY_REPORT_SIZE_SETTINGS)
		return;

	/* first retrieve the report rate, it is set per profile */
	switch (setting_report->report_rate) {
	case 0x00: report_rate = 125; break;
	case 0x01: report_rate = 250; break;
	case 0x02: report_rate = 500; break;
	case 0x03: report_rate = 1000; break;
	default:
		log_error(device->ratbag,
			  "error while reading the report rate of the mouse (0x%02x)\n",
			  buf[26]);
		report_rate = 0;
	}

	profile->resolution.num_modes = ETEKCITY_NUM_DPI;

	/* this will be optimized out by the compiler, but:
	 * - if one of the 2 X or Y sensitivity is set to 0 (0x0a), we read the
	 *   dpi settings from here
	 * - if not, then we take the ones from X and pray they have a meaning
	 */
	/* FIXME: yeah, xres and yres would be even better */
	if (setting_report->x_sensitivity == 0x0a)
		dpi_list = setting_report->xres;
	else if (setting_report->x_sensitivity == 0x0a)
		dpi_list = setting_report->yres;
	else
		dpi_list = setting_report->xres;

	for (i = 0; i < ETEKCITY_NUM_DPI; i++) {
		resolution = &profile->resolution.modes[i];
		resolution->dpi = dpi_list[i] * 50;
		resolution->hz = report_rate;
		if (!(setting_report->dpi_mask & (1 << i))) {
			/* the profile is disabled, overwrite it */
			resolution->dpi = 0;
			resolution->hz = 0;
		}
		resolution->is_active = (i == setting_report->current_dpi);
	}

	buf = drv_data->profiles[index];
	etekcity_set_config_profile(device, index, ETEKCITY_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, ETEKCITY_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	msleep(10);

	if (rc < ETEKCITY_REPORT_SIZE_PROFILE)
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
etekcity_read_button(struct ratbag_button *button)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	uint8_t data;
	unsigned raw_index = etekcity_button_to_index(button->index);
	const struct ratbag_button_action *action;

	data = drv_data->profiles[profile->index][3 + raw_index * 3];
	log_debug(device->ratbag,
		  " - button%d: %s (%02x) %s:%d\n",
		  button->index, print_key(data), data, __FILE__, __LINE__);
	action = etekcity_raw_to_button_action(data);
	if (action)
		button->action = *action;
	button->type = etekcity_raw_to_button_type(button->index);
}

static int
etekcity_write_button(struct ratbag_button *button,
		      const struct ratbag_button_action *action)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	uint8_t rc, *data;
	unsigned index = etekcity_button_to_index(button->index);

	data = &drv_data->profiles[profile->index][3 + index * 3];

	rc = etekcity_button_action_to_raw(action);
	if (!rc)
		return -EINVAL;

	*data = rc;

	return 0;
}

static int
etekcity_write_resolution_dpi(struct ratbag_profile *profile, int dpi)
{
	return -ENOTSUP;
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
	ratbag_device_init_profiles(device, ETEKCITY_PROFILE_MAX + 1, ETEKCITY_BUTTON_MAX + 1);

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

#define USB_VENDOR_ID_ETEKCITY			0x1ea7

static const struct ratbag_id etekcity_table[] = {
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_ETEKCITY,
		 .product = 0x4011,
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
	.write_resolution_dpi = etekcity_write_resolution_dpi,
};
