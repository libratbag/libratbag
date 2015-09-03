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
	uint8_t one;
	char name[24];
	uint8_t length;
	struct {
		uint8_t keycode;
		uint8_t flag;
	} keys[50];
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
etekcity_has_capability(const struct ratbag_device *device,
			enum ratbag_device_capability cap)
{
	switch (cap) {
	case RATBAG_DEVICE_CAP_NONE:
		return 0;
	case RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION:
	case RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE:
	case RATBAG_DEVICE_CAP_BUTTON_KEY:
	case RATBAG_DEVICE_CAP_BUTTON_MACROS:
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
etekcity_set_default_profile(struct ratbag_device *device, unsigned int index)
{
	return -ENOTSUP;
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

static const unsigned int macro_mapping[] = {
	[0x00 ... 0x03] = 0,
	[0x04] = KEY_A,
	[0x05] = KEY_B,
	[0x06] = KEY_C,
	[0x07] = KEY_D,
	[0x08] = KEY_E,
	[0x09] = KEY_F,
	[0x0a] = KEY_G,
	[0x0b] = KEY_H,
	[0x0c] = KEY_I,
	[0x0d] = KEY_J,
	[0x0e] = KEY_K,
	[0x0f] = KEY_L,
	[0x10] = KEY_M,
	[0x11] = KEY_N,
	[0x12] = KEY_O,
	[0x13] = KEY_P,
	[0x14] = KEY_Q,
	[0x15] = KEY_R,
	[0x16] = KEY_S,
	[0x17] = KEY_T,
	[0x18] = KEY_U,
	[0x19] = KEY_V,
	[0x1a] = KEY_W,
	[0x1b] = KEY_X,
	[0x1c] = KEY_Y,
	[0x1d] = KEY_Z,
	[0x1e] = KEY_1,
	[0x1f] = KEY_2,
	[0x20] = KEY_3,
	[0x21] = KEY_4,
	[0x22] = KEY_5,
	[0x23] = KEY_6,
	[0x24] = KEY_7,
	[0x25] = KEY_8,
	[0x26] = KEY_9,
	[0x27] = KEY_0,
	[0x28] = KEY_ENTER,
	[0x29] = KEY_ESC,
	[0x2a] = KEY_BACKSPACE,
	[0x2b] = KEY_TAB,
	[0x2c] = KEY_SPACE,
	[0x2d] = KEY_MINUS,
	[0x2e] = KEY_EQUAL,
	[0x2f] = KEY_LEFTBRACE,
	[0x30] = KEY_RIGHTBRACE,
	[0x31] = KEY_BACKSLASH,
	[0x32] = KEY_BACKSLASH,
	[0x33] = KEY_SEMICOLON,
	[0x34] = KEY_APOSTROPHE,
	[0x35] = KEY_GRAVE,
	[0x36] = KEY_COMMA,
	[0x37] = KEY_DOT,
	[0x38] = KEY_SLASH,
	[0x39] = KEY_CAPSLOCK,
	[0x3a] = KEY_F1,
	[0x3b] = KEY_F2,
	[0x3c] = KEY_F3,
	[0x3d] = KEY_F4,
	[0x3e] = KEY_F5,
	[0x3f] = KEY_F6,
	[0x40] = KEY_F7,
	[0x41] = KEY_F8,
	[0x42] = KEY_F9,
	[0x43] = KEY_F10,
	[0x44] = KEY_F11,
	[0x45] = KEY_F12,
	[0x46] = KEY_SYSRQ,
	[0x47] = KEY_SCROLLLOCK,
	[0x48] = KEY_PAUSE,
	[0x49] = KEY_INSERT,
	[0x4a] = KEY_HOME,
	[0x4b] = KEY_PAGEUP,
	[0x4c] = KEY_DELETE,
	[0x4d] = KEY_END,
	[0x4e] = KEY_PAGEDOWN,
	[0x4f] = KEY_RIGHT,
	[0x50] = KEY_LEFT,
	[0x51] = KEY_DOWN,
	[0x52] = KEY_UP,
	[0x53] = KEY_NUMLOCK,
	[0x54] = KEY_KPSLASH,
	[0x55] = KEY_KPASTERISK,
	[0x56] = KEY_KPMINUS,
	[0x57] = KEY_KPPLUS,
	[0x58] = KEY_KPENTER,
	[0x59] = KEY_KP1,
	[0x5a] = KEY_KP2,
	[0x5b] = KEY_KP3,
	[0x5c] = KEY_KP4,
	[0x5d] = KEY_KP5,
	[0x5e] = KEY_KP6,
	[0x5f] = KEY_KP7,
	[0x60] = KEY_KP8,
	[0x61] = KEY_KP9,
	[0x62] = KEY_KP0,
	[0x63] = KEY_KPDOT,
	[0x64] = KEY_102ND,
	[0x65] = KEY_COMPOSE,
	[0x66 ... 0xdf] = 0,
	[0xe0] = KEY_LEFTCTRL,
	[0xe1] = KEY_LEFTSHIFT,
	[0xe2] = KEY_LEFTALT,
	[0xe3] = KEY_LEFTMETA,
	[0xe4] = KEY_RIGHTCTRL,
	[0xe5] = KEY_RIGHTSHIFT,
	[0xe6] = KEY_RIGHTALT,
	[0xe7] = KEY_RIGHTMETA,
	[0xe8 ... 0xff] = 0,
};

static void
etekcity_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data;
	struct ratbag_resolution *resolution;
	struct etekcity_settings_report *setting_report;
	uint8_t *buf;
	unsigned int report_rate;
	unsigned int i, j;
	int dpi_x, dpi_y, hz;
	int rc;

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

	for (i = 0; i < ETEKCITY_NUM_DPI; i++) {
		dpi_x = setting_report->xres[i] * 50;
		dpi_y = setting_report->yres[i] * 50;
		hz = report_rate;
		if (!(setting_report->dpi_mask & (1 << i))) {
			/* the profile is disabled, overwrite it */
			resolution->dpi_x = 0;
			resolution->dpi_y = 0;
			resolution->hz = 0;
		}
		resolution = ratbag_resolution_init(profile, i, dpi_x, dpi_y, hz);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		resolution->is_active = (i == setting_report->current_dpi);
	}

	buf = drv_data->profiles[index];
	etekcity_set_config_profile(device, index, ETEKCITY_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_KEY_MAPPING,
			buf, ETEKCITY_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	msleep(10);

	for (i = 0; i < ETEKCITY_BUTTON_MAX; i++) {
		const struct ratbag_button_action *action;
		struct etekcity_macro *macro;

		action = etekcity_button_to_action(profile, i);
		if (!action || action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
			continue;

		etekcity_set_config_profile(device, index, i);
		macro = &drv_data->macros[index][i];
		buf = (uint8_t*)macro;
		rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_MACRO,
				buf, ETEKCITY_REPORT_SIZE_MACRO,
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
		log_info(device->ratbag,
			 "macro on button %d of profile %d is named '%s', and contains %d events:\n",
			 i, profile->index,
			 macro->name, macro->length);
		for (j = 0; j < macro->length; j++) {
			log_info(device->ratbag,
				 "    - %s %s\n",
				 libevdev_event_code_get_name(EV_KEY, macro_mapping[macro->keys[j].keycode]),
				 macro->keys[j].flag & 0x80 ? "released" : "pressed");
		}

	}

	msleep(10);

	if (rc < ETEKCITY_REPORT_SIZE_PROFILE)
		return;

	log_raw(device->ratbag, "profile: %d %s:%d\n",
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

	log_raw(device->ratbag, "profile: %d written %s:%d\n",
		buf[2],
		__FILE__, __LINE__);
	return 0;
}

static void
etekcity_read_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action;

	action = etekcity_button_to_action(button->profile, button->index);
	if (action)
		button->action = *action;
	button->type = etekcity_raw_to_button_type(button->index);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
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
etekcity_write_resolution_dpi(struct ratbag_resolution *resolution,
			      int dpi_x, int dpi_y)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_device *device = profile->device;
	struct etekcity_data *drv_data = ratbag_get_drv_data(device);
	struct etekcity_settings_report *settings_report;
	unsigned int index;
	uint8_t *buf;
	int rc;

	if (dpi_x < 50 || dpi_x > 8200 || dpi_x % 50)
		return -EINVAL;
	if (dpi_y < 50 || dpi_y > 8200 || dpi_y % 50)
		return -EINVAL;

	settings_report = &drv_data->settings[profile->index];

	/* retrieve which resolution is asked to be changed */
	index = resolution - profile->resolution.modes;

	settings_report->x_sensitivity = 0x0a;
	settings_report->y_sensitivity = 0x0a;
	settings_report->xres[index] = dpi_x / 50;
	settings_report->yres[index] = dpi_y / 50;

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
etekcity_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct ratbag_profile *profile;
	struct etekcity_data *drv_data;
	int active_idx;

	log_raw(device->ratbag, "data: %d\n", id.data);

	rc = ratbag_open_hidraw(device);
	if (rc) {
		log_error(device->ratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	/* retrieve the "on-to-go" speed setting */
	rc = ratbag_hidraw_raw_request(device, ETEKCITY_REPORT_ID_SPEED_SETTING,
			drv_data->speed_setting, ETEKCITY_REPORT_SIZE_SPEED_SETTING,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	log_debug(device->ratbag, "device is at %d ms of latency\n", drv_data->speed_setting[2]);

	/* profiles are 0-indexed */
	ratbag_device_init_profiles(device, ETEKCITY_PROFILE_MAX + 1, ETEKCITY_BUTTON_MAX + 1);

	active_idx = etekcity_current_profile(device);
	if (active_idx < 0) {
		log_error(device->ratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
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
	free(ratbag_get_drv_data(device));
}

#define USB_VENDOR_ID_ETEKCITY			0x1ea7

static const struct ratbag_id etekcity_table[] = {
	{.id = { .bustype = BUS_USB,
		 .vendor = USB_VENDOR_ID_ETEKCITY,
		 .product = 0x4011,
		 .version = VERSION_ANY },
	 .svg_filename = "etekcity.svg",
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
	.set_active_profile = etekcity_set_current_profile,
	.set_default_profile = etekcity_set_default_profile,
	.has_capability = etekcity_has_capability,
	.read_button = etekcity_read_button,
	.write_button = etekcity_write_button,
	.write_resolution_dpi = etekcity_write_resolution_dpi,
};
