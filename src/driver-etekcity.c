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
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define ETEKCITY_PROFILE_MAX			4
#define ETEKCITY_BUTTON_MAX			10
#define ETEKCITY_NUM_DPI			6
#define ETEKCITY_LED			0

#define ETEKCITY_REPORT_ID_CONFIGURE_PROFILE	4
#define ETEKCITY_REPORT_ID_PROFILE		5
#define ETEKCITY_REPORT_ID_SETTINGS		6
#define ETEKCITY_REPORT_ID_KEY_MAPPING		7
#define ETEKCITY_REPORT_ID_SPEED_SETTING	8
#define ETEKCITY_REPORT_ID_MACRO		9

#define ETEKCITY_REPORT_SIZE_PROFILE		50
#define ETEKCITY_REPORT_SIZE_SETTINGS		40
#define ETEKCITY_REPORT_SIZE_SPEED_SETTING	6
#define ETEKCITY_REPORT_SIZE_MACRO		130

#define ETEKCITY_CONFIG_SETTINGS		0x10
#define ETEKCITY_CONFIG_KEY_MAPPING		0x20

#define ETEKCITY_MAX_MACRO_LENGTH		50

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

struct etekcity_macro {
	uint8_t reportID;
	uint8_t heightytwo;
	uint8_t profile;
	uint8_t button_index;
	uint8_t active;
	char name[24];
	uint8_t length;
	struct {
		uint8_t keycode;
		uint8_t flag;
	} keys[ETEKCITY_MAX_MACRO_LENGTH];
} __attribute__((packed));

struct etekcity_data {
	uint8_t profiles[(ETEKCITY_PROFILE_MAX + 1)][ETEKCITY_REPORT_SIZE_PROFILE];
	struct etekcity_settings_report settings[(ETEKCITY_PROFILE_MAX + 1)];
	struct etekcity_macro macros[(ETEKCITY_PROFILE_MAX + 1)][(ETEKCITY_BUTTON_MAX + 1)];
	uint8_t speed_setting[ETEKCITY_REPORT_SIZE_SPEED_SETTING];
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
	{ 18, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 19, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ 20, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
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

static inline unsigned
etekcity_button_to_index(unsigned button)
{
	return button < 8 ? button : button + 5;
}

static const struct ratbag_button_action *
etekcity_button_to_action(struct ratbag_profile *profile,
			  unsigned int button_index)
{
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	uint8_t data;
	unsigned raw_index = etekcity_button_to_index(button_index);

	data = drv_data->profiles[profile->index][3 + raw_index * 3];
	log_raw(device->ratbag,
		  " - button%d: %s (%02x) %s:%d\n",
		  button_index, print_key(data), data, __FILE__, __LINE__);
	return etekcity_raw_to_button_action(data);
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

	if (rc < ETEKCITY_REPORT_SIZE_PROFILE)
		return -EIO;

	log_raw(device->ratbag, "profile: %d written %s:%d\n",
		buf[2],
		__FILE__, __LINE__);
	return 0;
}

static void
etekcity_read_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action;
	struct ratbag_device *device;
	struct ratbag_button_macro *m;
	struct etekcity_macro *macro;
	struct etekcity_data *drv_data;
	uint8_t *buf;
	unsigned j;
	int rc;

	action = etekcity_button_to_action(button->profile, button->index);
	if (action)
		ratbag_button_set_action(button, action);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	if (action && action->type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
		device = button->profile->device;
		drv_data = ratbag_get_drv_data(device);

		etekcity_set_config_profile(device,
					    button->profile->index,
					    button->index);
		macro = &drv_data->macros[button->profile->index][button->index];
		buf = (uint8_t*)macro;
		rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_MACRO,
				buf, ETEKCITY_REPORT_SIZE_MACRO,
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
		if (rc != ETEKCITY_REPORT_SIZE_MACRO) {
			log_error(device->ratbag,
				  "Unable to retrieve the macro for button %d of profile %d: %s (%d)\n",
				  button->index, button->profile->index,
				  rc < 0 ? strerror(-rc) : "not read enough", rc);
		} else {
			m = ratbag_button_macro_new(macro->name);
			log_raw(device->ratbag,
				"macro on button %d of profile %d is named '%s', and contains %d events:\n",
				button->index, button->profile->index,
				macro->name, macro->length);
			for (j = 0; j < macro->length; j++) {
				unsigned int keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(device,
								macro->keys[j].keycode);
				ratbag_button_macro_set_event(m,
							      j,
							      macro->keys[j].flag & 0x80 ? RATBAG_MACRO_EVENT_KEY_RELEASED : RATBAG_MACRO_EVENT_KEY_PRESSED,
							      keycode);
				log_raw(device->ratbag,
					"    - %s %s\n",
					libevdev_event_code_get_name(EV_KEY, keycode),
					macro->keys[j].flag & 0x80 ? "released" : "pressed");
			}
			ratbag_button_copy_macro(button, m);
			ratbag_button_macro_unref(m);
		}
		msleep(10);
	}
}

static void
etekcity_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct etekcity_settings_report *setting_report;
	uint8_t *buf;
	unsigned int report_rate;
	int dpi_x, dpi_y;
	int rc;
	unsigned int report_rates[] = { 125, 250, 500, 1000 };

	assert(profile->index <= ETEKCITY_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(device);

	setting_report = &drv_data->settings[profile->index];
	buf = (uint8_t*)setting_report;
	etekcity_set_config_profile(device, profile->index, ETEKCITY_CONFIG_SETTINGS);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_SETTINGS,
			buf, ETEKCITY_REPORT_SIZE_SETTINGS,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	if (rc < ETEKCITY_REPORT_SIZE_SETTINGS)
		return;

	/* first retrieve the report rate, it is set per profile */
	if (setting_report->report_rate < ARRAY_LENGTH(report_rates)) {
		report_rate = report_rates[setting_report->report_rate];
	} else {
		log_error(device->ratbag,
			  "error while reading the report rate of the mouse (0x%02x)\n",
			  buf[26]);
		report_rate = 0;
	}

	ratbag_profile_set_report_rate_list(profile, report_rates,
					    ARRAY_LENGTH(report_rates));
	profile->hz = report_rate;

	ratbag_profile_for_each_resolution(profile, resolution) {
		dpi_x = setting_report->xres[resolution->index] * 50;
		dpi_y = setting_report->yres[resolution->index] * 50;
		if (!(setting_report->dpi_mask & (1 << resolution->index))) {
			/* the profile is disabled, overwrite it */
			dpi_x = 0;
			dpi_y = 0;
		}

		ratbag_resolution_set_resolution(resolution, dpi_x, dpi_y);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		resolution->is_active = (resolution->index == setting_report->current_dpi);

	}

	ratbag_profile_for_each_button(profile, button)
		etekcity_read_button(button);

	buf = drv_data->profiles[profile->index];
	etekcity_set_config_profile(device, profile->index, ETEKCITY_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, ETEKCITY_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	msleep(10);

	if (rc < ETEKCITY_REPORT_SIZE_PROFILE)
		return;

	log_raw(device->ratbag, "profile: %d %s:%d\n",
		buf[2],
		__FILE__, __LINE__);
}

static int
etekcity_write_macro(struct ratbag_button *button)
{
	const struct ratbag_button_action *action = &button->action;
	struct ratbag_device *device;
	struct etekcity_macro *macro;
	struct etekcity_data *drv_data;
	uint8_t *buf;
	unsigned i, count = 0;
	int rc;

	if (action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return 0;

	device = button->profile->device;
	drv_data = ratbag_get_drv_data(device);
	macro = &drv_data->macros[button->profile->index][button->index];
	buf = (uint8_t*)macro;

	for (i = 0; i < MAX_MACRO_EVENTS && count < ETEKCITY_MAX_MACRO_LENGTH; i++) {
		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_INVALID)
			return -EINVAL; /* should not happen, ever */

		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_NONE)
			break;

		/* ignore timeout events */
		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_WAIT)
			continue;

		macro->keys[count].keycode = ratbag_hidraw_get_keyboard_usage_from_keycode(device,
											   action->macro->events[i].event.key);
		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_KEY_PRESSED)
			macro->keys[count].flag = 0x00;
		else
			macro->keys[count].flag = 0x80;
		count++;
	}

	macro->reportID = ETEKCITY_REPORT_ID_MACRO;
	macro->heightytwo = 0x82;
	macro->profile = button->profile->index;
	macro->button_index = button->index;
	macro->active = 0x01;
	strncpy(macro->name, action->macro->name, 23);
	macro->length = count;

	etekcity_set_config_profile(device,
				    button->profile->index,
				    button->index);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_MACRO,
		buf, ETEKCITY_REPORT_SIZE_MACRO,
		HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (rc < 0)
		return rc;

	return rc == ETEKCITY_REPORT_SIZE_MACRO ? 0 : -EIO;
}

static int
etekcity_write_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action = &button->action;
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

	rc = etekcity_write_macro(button);
	if (rc) {
		log_error(device->ratbag,
			  "unable to write the macro to the device: '%s' (%d)\n",
			  strerror(-rc), rc);
		return rc;
	}

	return rc;
}

static int
etekcity_write_resolution(struct ratbag_resolution *resolution)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	struct etekcity_settings_report *settings_report;
	uint8_t *buf;
	int rc;

	const unsigned int dpi_x = resolution->dpi_x;
	const unsigned int dpi_y = resolution->dpi_y;

	if (dpi_x < 50 || dpi_x > 8200 || dpi_x % 50)
		return -EINVAL;
	if (dpi_y < 50 || dpi_y > 8200 || dpi_y % 50)
		return -EINVAL;

	settings_report = &drv_data->settings[profile->index];

	settings_report->x_sensitivity = 0x0a;
	settings_report->y_sensitivity = 0x0a;
	settings_report->xres[resolution->index] = dpi_x / 50;
	settings_report->yres[resolution->index] = dpi_y / 50;

	buf = (uint8_t*)settings_report;
	etekcity_set_config_profile(device, profile->index, ETEKCITY_CONFIG_SETTINGS);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_SETTINGS,
				       buf, ETEKCITY_REPORT_SIZE_SETTINGS,
				       HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (rc < 0)
		return rc;

	if (rc != ETEKCITY_REPORT_SIZE_SETTINGS)
		return -EIO;

	return 0;
}

static int
etekcity_probe(struct ratbag_device *device)
{
	int rc;
	struct ratbag_profile *profile;
	struct etekcity_data *drv_data;
	int active_idx;

	rc = ratbag_open_hidraw(device);
	if (rc)
		return rc;

	if (!ratbag_hidraw_has_report(device, ETEKCITY_REPORT_ID_KEY_MAPPING)) {
		ratbag_close_hidraw(device);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	/* retrieve the "on-to-go" speed setting */
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_SPEED_SETTING,
			drv_data->speed_setting, ETEKCITY_REPORT_SIZE_SPEED_SETTING,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (rc)
		return rc;

	log_debug(device->ratbag, "device is at %d ms of latency\n", drv_data->speed_setting[2]);

	/* profiles are 0-indexed */
	ratbag_device_init_profiles(device,
				    ETEKCITY_PROFILE_MAX + 1,
				    ETEKCITY_NUM_DPI,
				    ETEKCITY_BUTTON_MAX + 1,
				    ETEKCITY_LED);

	ratbag_device_for_each_profile(device, profile)
		etekcity_read_profile(profile);

	active_idx = etekcity_current_profile(device);
	if (active_idx < 0) {
		log_error(device->ratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-active_idx),
			  active_idx);
		rc = -ENODEV;
		goto err;
	}

	list_for_each(profile, &device->profiles, link) {
		if (profile->index == (unsigned int)active_idx) {
			profile->is_active = true;
			break;
		}
	}

	log_raw(device->ratbag,
		"'%s' is in profile %d\n",
		ratbag_device_get_name(device),
		profile->index);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static void
etekcity_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

static int
etekcity_commit(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_button *button = NULL;
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution = NULL;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		ratbag_profile_for_each_resolution(profile, resolution) {
			if (!resolution->dirty)
				continue;

			rc = etekcity_write_resolution(resolution);
			if (rc)
				return rc;
		}

		ratbag_profile_for_each_button(profile, button) {
			if (!button->dirty)
				continue;

			rc = etekcity_write_button(button);
			if (rc)
				return rc;
		}

		rc = etekcity_write_profile(profile);
		if (rc)
			return rc;
	}

	return 0;
}

struct ratbag_driver etekcity_driver = {
	.name = "EtekCity",
	.id = "etekcity",
	.probe = etekcity_probe,
	.remove = etekcity_remove,
	.commit = etekcity_commit,
	.set_active_profile = etekcity_set_current_profile,
};
