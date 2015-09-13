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

#define ROCCAT_PROFILE_MAX			4
#define ROCCAT_BUTTON_MAX			23
#define ROCCAT_NUM_DPI				5

#define ROCCAT_MAX_RETRY_READY			10

#define ROCCAT_REPORT_ID_CONFIGURE_PROFILE	4
#define ROCCAT_REPORT_ID_PROFILE		5
#define ROCCAT_REPORT_ID_SETTINGS		6
#define ROCCAT_REPORT_ID_KEY_MAPPING		7
#define ROCCAT_REPORT_ID_MACRO			8

#define ROCCAT_REPORT_SIZE_PROFILE		77
#define ROCCAT_REPORT_SIZE_SETTINGS		43
#define ROCCAT_REPORT_SIZE_MACRO		2082

#define ROCCAT_CONFIG_SETTINGS		0x80
#define ROCCAT_CONFIG_KEY_MAPPING		0x90

#define ROCCAT_MAX_MACRO_LENGTH		500

struct roccat_settings_report {
	uint8_t reportID;
	uint8_t twoB;
	uint8_t profileID;
	uint8_t x_y_linked;
	uint8_t x_sensitivity; /* 0x06 means 0 */
	uint8_t y_sensitivity; /* 0x06 means 0 */
	uint8_t dpi_mask;
	uint8_t xres[5];
	uint8_t current_dpi;
	uint8_t yres[5];
	uint8_t padding1;
	uint8_t report_rate;
	uint8_t padding2[21];
	uint16_t checksum;
//	uint8_t padding2[4];
//	uint8_t light;
//	uint8_t light_heartbit;
//	uint8_t padding3[5];
} __attribute__((packed));

struct roccat_macro {
	uint8_t reportID;
	uint8_t twentytwo;
	uint8_t height;
	uint8_t profile;
	uint8_t button_index;
	uint8_t active;
	uint8_t padding[24];
	char group[24];
	char name[24];
	uint16_t length;
	struct {
		uint8_t keycode;
		uint8_t flag;
		uint16_t time;
	} keys[ROCCAT_MAX_MACRO_LENGTH];
	uint16_t checksum;
} __attribute__((packed));

struct roccat_data {
	uint8_t profiles[(ROCCAT_PROFILE_MAX + 1)][ROCCAT_REPORT_SIZE_PROFILE];
	struct roccat_settings_report settings[(ROCCAT_PROFILE_MAX + 1)];
	struct roccat_macro macros[(ROCCAT_PROFILE_MAX + 1)][(ROCCAT_BUTTON_MAX + 1)];
};

struct roccat_button_type_mapping {
	uint8_t raw;
	enum ratbag_button_type type;
};

static const struct roccat_button_type_mapping roccat_button_type_mapping[] = {
	{ 0, RATBAG_BUTTON_TYPE_LEFT },
	{ 1, RATBAG_BUTTON_TYPE_RIGHT },
	{ 2, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 3, RATBAG_BUTTON_TYPE_EXTRA },
	{ 4, RATBAG_BUTTON_TYPE_SIDE },
	{ 5, RATBAG_BUTTON_TYPE_WHEEL_LEFT },
	{ 6, RATBAG_BUTTON_TYPE_WHEEL_RIGHT },
	{ 7, RATBAG_BUTTON_TYPE_WHEEL_UP },
	{ 8, RATBAG_BUTTON_TYPE_WHEEL_DOWN },
	{ 9, RATBAG_BUTTON_TYPE_RESOLUTION_UP },
	{ 10, RATBAG_BUTTON_TYPE_RESOLUTION_DOWN },
//	{ 11, RATBAG_BUTTON_TYPE_ }, /* top button above the wheel */
	{ 12, RATBAG_BUTTON_TYPE_LEFT },
	{ 13, RATBAG_BUTTON_TYPE_RIGHT },
	{ 14, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 15, RATBAG_BUTTON_TYPE_EXTRA },
	{ 16, RATBAG_BUTTON_TYPE_SIDE },
	{ 17, RATBAG_BUTTON_TYPE_WHEEL_LEFT },
	{ 18, RATBAG_BUTTON_TYPE_WHEEL_RIGHT },
	{ 19, RATBAG_BUTTON_TYPE_WHEEL_UP },
	{ 20, RATBAG_BUTTON_TYPE_WHEEL_DOWN },
	{ 21, RATBAG_BUTTON_TYPE_RESOLUTION_UP },
	{ 22, RATBAG_BUTTON_TYPE_RESOLUTION_DOWN },
//	{ 23, RATBAG_BUTTON_TYPE_ }, /* top button above the wheel */
};

static enum ratbag_button_type
roccat_raw_to_button_type(uint8_t data)
{
	const struct roccat_button_type_mapping *mapping;

	ARRAY_FOR_EACH(roccat_button_type_mapping, mapping) {
		if (mapping->raw == data)
			return mapping->type;
	}

	return RATBAG_BUTTON_TYPE_UNKNOWN;
}

struct roccat_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static struct roccat_button_mapping roccat_button_mapping[] = {
/* FIXME:	{ 0, Disabled }, */
	{ 1, BUTTON_ACTION_BUTTON(1) },
	{ 2, BUTTON_ACTION_BUTTON(2) },
	{ 3, BUTTON_ACTION_BUTTON(3) },
	{ 4, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },
/* FIXME:	{ 5, Shortcut (modifier + key) }, */
	{ 6, BUTTON_ACTION_NONE },
	{ 7, BUTTON_ACTION_BUTTON(4) },
	{ 8, BUTTON_ACTION_BUTTON(5) },
	{ 9, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT) },
	{ 10, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT) },
	{ 13, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ 14, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
/* FIXME:	{ 15, quicklaunch },  -> hidraw report 03 00 60 07 01 00 00 00 */
	{ 16, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 17, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ 18, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
	{ 20, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 21, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 22, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 26, BUTTON_ACTION_KEY(KEY_LEFTMETA) },
/* FIXME:	{ 27, open driver },  -> hidraw report 02 83 01 00 00 00 00 00 */
	{ 32, BUTTON_ACTION_KEY(KEY_CONFIG) },
	{ 33, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ 34, BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ 35, BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ 36, BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ 37, BUTTON_ACTION_KEY(KEY_MUTE) },
	{ 38, BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ 39, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },
	{ 48, BUTTON_ACTION_MACRO },
	{ 65, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE) },
/* FIXME:	{ 66, Easywheel sensitivity }, */
/* FIXME:	{ 67, Easywheel profile }, */
/* FIXME:	{ 68, Easywheel CPI }, */
/* FIXME:	{ 81, Other Easyshift },	-> hidraw report 03 00 ff 05 01 00 00 00 */
/* FIXME:	{ 82, Other Easyshift Lock },	-> hidraw report 03 00 ff 05 01 00 00 00 */
/* FIXME:	{ 83, Both Easyshift },		-> hidraw report 03 00 ff 04 01 00 00 00 */
};

static const struct ratbag_button_action*
roccat_raw_to_button_action(uint8_t data)
{
	struct roccat_button_mapping *mapping;

	ARRAY_FOR_EACH(roccat_button_mapping, mapping) {
		if (mapping->raw == data)
			return &mapping->action;
	}

	return NULL;
}

static uint8_t
roccat_button_action_to_raw(const struct ratbag_button_action *action)
{
	struct roccat_button_mapping *mapping;

	ARRAY_FOR_EACH(roccat_button_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->raw;
	}

	return 0;
}

static inline uint16_t
roccat_get_unaligned_u16(uint8_t *buf)
{
	return (buf[1] << 8) | buf[0];
}

static inline uint16_t
roccat_compute_crc(uint8_t *buf, unsigned int len)
{
	unsigned i;
	uint16_t crc = 0;

	if (len < 3)
		return 0;

	for (i = 0; i < len - 2; i++) {
		crc += buf[i];
	}

	return crc;
}

static inline int
roccat_crc_is_valid(struct ratbag_device *device, uint8_t *buf, unsigned int len)
{
	uint16_t crc;
	uint16_t given_crc;

	if (len < 3)
		return 0;

	crc = roccat_compute_crc(buf, len);

	given_crc = roccat_get_unaligned_u16(&buf[len - 2]);

	log_raw(device->ratbag,
		"checksum computed: 0x%04x, checksum given: 0x%04x\n",
		crc,
		given_crc);

	return crc == given_crc;
}

static int
roccat_is_ready(struct ratbag_device *device)
{
	uint8_t buf[3] = { 0 };
	int rc;

	rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_CONFIGURE_PROFILE,
					buf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (rc < 0)
		return rc;
	if (rc != sizeof(buf))
		return -EIO;

	if (buf[1] == 0x03)
		msleep(100);

	return buf[1] == 0x01;
}

static int
roccat_wait_ready(struct ratbag_device *device)
{
	unsigned count = 0;
	int rc;

	msleep(10);
	while (count < ROCCAT_MAX_RETRY_READY) {
		rc = roccat_is_ready(device);
		if (rc < 0)
			return rc;

		if (rc == 1)
			return 0;

		msleep(10);
	}

	return -ETIMEDOUT;
}

static int
roccat_has_capability(const struct ratbag_device *device,
		      enum ratbag_device_capability cap)
{
	switch (cap) {
	case RATBAG_DEVICE_CAP_NONE:
	case RATBAG_DEVICE_CAP_DEFAULT_PROFILE:
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
roccat_current_profile(struct ratbag_device *device)
{
	uint8_t buf[3];
	int ret;

	ret = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_PROFILE, buf,
			sizeof(buf), HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0)
		return ret;

	if (ret != 3)
		return -EIO;

	return buf[2];
}

static int
roccat_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	uint8_t buf[] = {ROCCAT_REPORT_ID_PROFILE, 0x03, index};
	int ret;

	if (index > ROCCAT_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (ret < 0)
		return ret;

	if (ret != sizeof(buf))
		return -EIO;

	ret = roccat_wait_ready(device);
	if (ret)
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-ret), ret);

	return ret;
}

static int
roccat_set_config_profile(struct ratbag_device *device, uint8_t profile, uint8_t type)
{
	uint8_t buf[] = {ROCCAT_REPORT_ID_CONFIGURE_PROFILE, profile, type};
	int ret;

	if (profile > ROCCAT_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_raw_request(device, buf[0], buf, sizeof(buf),
				 HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (ret < 0)
		return ret;

	if (ret != sizeof(buf))
		return -EIO;

	ret = roccat_wait_ready(device);
	if (ret)
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-ret), ret);

	return ret;
}

static const struct ratbag_button_action *
roccat_button_to_action(struct ratbag_profile *profile,
			  unsigned int button_index)
{
	struct ratbag_device *device = profile->device;
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	uint8_t data;

	data = drv_data->profiles[profile->index][3 + button_index * 3];
	return roccat_raw_to_button_action(data);
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
roccat_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct roccat_data *drv_data;
	struct ratbag_resolution *resolution;
	struct roccat_settings_report *setting_report;
	uint8_t *buf;
	unsigned int report_rate;
	unsigned int i;
	int dpi_x, dpi_y, hz;
	int rc;

	assert(index <= ROCCAT_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(device);

	setting_report = &drv_data->settings[index];
	buf = (uint8_t*)setting_report;
	roccat_set_config_profile(device, index, ROCCAT_CONFIG_SETTINGS);
	rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_SETTINGS,
			buf, ROCCAT_REPORT_SIZE_SETTINGS,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	if (rc < ROCCAT_REPORT_SIZE_SETTINGS)
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

	profile->resolution.num_modes = ROCCAT_NUM_DPI;

	for (i = 0; i < ROCCAT_NUM_DPI; i++) {
		dpi_x = setting_report->xres[i] * 50;
		dpi_y = setting_report->yres[i] * 50;
		hz = report_rate;
		if (!(setting_report->dpi_mask & (1 << i))) {
			/* the profile is disabled, overwrite it */
			dpi_x = 0;
			dpi_y = 0;
			hz = 0;
		}
		resolution = ratbag_resolution_init(profile, i, dpi_x, dpi_y, hz);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		resolution->is_active = (i == setting_report->current_dpi);
	}

	buf = drv_data->profiles[index];
	roccat_set_config_profile(device, index, ROCCAT_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_KEY_MAPPING,
			buf, ROCCAT_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_GET_REPORT);

	msleep(10);

	if (rc < ROCCAT_REPORT_SIZE_PROFILE)
		return;

	if (!roccat_crc_is_valid(device, buf, ROCCAT_REPORT_SIZE_PROFILE))
		log_error(device->ratbag,
			  "Error while reading profile %d, continuing...\n",
			  profile->index);

	log_raw(device->ratbag, "profile: %d %s:%d\n",
		buf[2],
		__FILE__, __LINE__);
}

static int
roccat_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	unsigned int index = profile->index;
	struct roccat_data *drv_data;
	int rc;
	uint8_t *buf;
	uint16_t *crc;

	assert(index <= ROCCAT_PROFILE_MAX);

	drv_data = ratbag_get_drv_data(device);
	buf = drv_data->profiles[index];
	crc = (uint16_t *)&buf[ROCCAT_REPORT_SIZE_PROFILE - 2];
	*crc = roccat_compute_crc(buf, ROCCAT_REPORT_SIZE_PROFILE);

	roccat_set_config_profile(device, index, ROCCAT_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_KEY_MAPPING,
			buf, ROCCAT_REPORT_SIZE_PROFILE,
			HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (rc < ROCCAT_REPORT_SIZE_PROFILE)
		return -EIO;

	log_raw(device->ratbag, "profile: %d written %s:%d\n",
		buf[2],
		__FILE__, __LINE__);

	rc = roccat_wait_ready(device);
	if (rc)
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-rc), rc);

	return rc;
}

static void
roccat_read_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action;
	struct ratbag_device *device;
	struct roccat_macro *macro;
	struct roccat_data *drv_data;
	uint8_t *buf;
	unsigned j;
	int rc;

	device = button->profile->device;
	drv_data = ratbag_get_drv_data(device);
	action = roccat_button_to_action(button->profile, button->index);
	if (action)
		button->action = *action;
	button->type = roccat_raw_to_button_type(button->index);
//	if (action == NULL)
//		log_error(device->ratbag, "button: %d -> %d %s:%d\n",
//			button->index, drv_data->profiles[button->profile->index][3 + button->index * 3],
//			__FILE__, __LINE__);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	if (action && action->type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {

		roccat_set_config_profile(device,
					  button->profile->index,
					  button->index);
		macro = &drv_data->macros[button->profile->index][button->index];
		buf = (uint8_t*)macro;
		buf[0] = ROCCAT_REPORT_ID_MACRO;
		rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_MACRO,
				buf, ROCCAT_REPORT_SIZE_MACRO,
				HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
		if (rc != ROCCAT_REPORT_SIZE_MACRO) {
			log_error(device->ratbag,
				  "Unable to retrieve the macro for button %d of profile %d: %s (%d)\n",
				  button->index, button->profile->index,
				  rc < 0 ? strerror(-rc) : "not read enough", rc);
		} else {
			if (!roccat_crc_is_valid(device, buf, ROCCAT_REPORT_SIZE_MACRO)) {
				log_error(device->ratbag,
					  "wrong checksum while reading the macro of button %d of profile %d.\n",
					  button->index,
					  button->profile->index);
				goto out_macro;
			}

			ratbag_button_set_macro(button, macro->name);
			log_raw(device->ratbag,
				"macro on button %d of profile %d is named '%s', and contains %d events:\n",
				button->index, button->profile->index,
				macro->name, macro->length);
			for (j = 0; j < macro->length; j++) {
				ratbag_button_set_macro_event(button,
							      j * 2,
							      macro->keys[j].flag & 0x01 ? RATBAG_MACRO_EVENT_KEY_PRESSED : RATBAG_MACRO_EVENT_KEY_RELEASED,
							      macro_mapping[macro->keys[j].keycode]);
				if (macro->keys[j].time)
					ratbag_button_set_macro_event(button,
								      j * 2 + 1,
								      RATBAG_MACRO_EVENT_WAIT,
								      macro->keys[j].time);

				log_raw(device->ratbag,
					"    - %s %s\n",
					libevdev_event_code_get_name(EV_KEY, macro_mapping[macro->keys[j].keycode]),
					macro->keys[j].flag & 0x80 ? "released" : "pressed");
			}
		}
out_macro:
		msleep(10);
	}
}

static int
roccat_write_macro(struct ratbag_button *button,
		     const struct ratbag_button_action *action)
{
	struct ratbag_device *device;
	struct roccat_macro *macro;
	struct roccat_data *drv_data;
	uint8_t *buf;
	unsigned i, j, count = 0;
	int rc;

	if (action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return 0;

	device = button->profile->device;
	drv_data = ratbag_get_drv_data(device);
	macro = &drv_data->macros[button->profile->index][button->index];
	buf = (uint8_t*)macro;

	memset(buf, 0, ROCCAT_REPORT_SIZE_MACRO);

	for (i = 0; i < MAX_MACRO_EVENTS && count < ROCCAT_MAX_MACRO_LENGTH; i++) {
		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_INVALID)
			return -EINVAL; /* should not happen, ever */

		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_NONE)
			break;

		/* ignore the first wait */
		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_WAIT &&
		    !count)
			continue;

		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_KEY_PRESSED ||
		    action->macro->events[i].type == RATBAG_MACRO_EVENT_KEY_RELEASED) {
			for (j = 0; j < ARRAY_LENGTH(macro_mapping); j++) {
				if (macro_mapping[j] == action->macro->events[i].event.key)
					macro->keys[count].keycode = j;
			}
		}

		switch (action->macro->events[i].type) {
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			macro->keys[count].flag = 0x01;
			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			macro->keys[count].flag = 0x02;
			break;
		case RATBAG_MACRO_EVENT_WAIT:
			macro->keys[--count].time = action->macro->events[i].event.timeout;
			break;
		case RATBAG_MACRO_EVENT_INVALID:
		case RATBAG_MACRO_EVENT_NONE:
			/* should not happen */
			log_error(device->ratbag,
				  "something went wrong while writing a macro.\n");
		}
		count++;
	}

	macro->reportID = ROCCAT_REPORT_ID_MACRO;
	macro->twentytwo = 0x22;
	macro->height = 0x08;
	macro->profile = button->profile->index;
	macro->button_index = button->index;
	macro->active = 0x01;
	strcpy(macro->group, "g0");
	strncpy(macro->name, action->macro->name, 23);
	macro->length = count;
	macro->checksum = roccat_compute_crc(buf, ROCCAT_REPORT_SIZE_MACRO);

	rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_MACRO,
		buf, ROCCAT_REPORT_SIZE_MACRO,
		HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (rc < 0)
		return rc;

	if (rc != ROCCAT_REPORT_SIZE_MACRO)
		return -EIO;

	rc = roccat_wait_ready(device);
	if (rc)
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-rc), rc);

	return rc;
}

static int
roccat_write_button(struct ratbag_button *button,
		      const struct ratbag_button_action *action)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	uint8_t rc, *data;

	data = &drv_data->profiles[profile->index][3 + button->index * 3];

	rc = roccat_button_action_to_raw(action);
	if (!rc)
		return -EINVAL;

	*data = rc;

	rc = roccat_write_macro(button, action);
	if (rc)
		log_error(device->ratbag,
			  "unable to write the macro to the device: '%s' (%d)\n",
			  strerror(-rc), rc);

	return rc;
}

static int
roccat_write_resolution_dpi(struct ratbag_resolution *resolution,
			      int dpi_x, int dpi_y)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_device *device = profile->device;
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	struct roccat_settings_report *settings_report;
	unsigned int index;
	uint8_t *buf;
	int rc;

	if (dpi_x < 200 || dpi_x > 8200 || dpi_x % 50)
		return -EINVAL;
	if (dpi_y < 200 || dpi_y > 8200 || dpi_y % 50)
		return -EINVAL;

	settings_report = &drv_data->settings[profile->index];

	/* retrieve which resolution is asked to be changed */
	index = resolution - profile->resolution.modes;

	settings_report->xres[index] = dpi_x / 50;
	settings_report->yres[index] = dpi_y / 50;

	buf = (uint8_t*)settings_report;

	settings_report->checksum = roccat_compute_crc(buf, ROCCAT_REPORT_SIZE_SETTINGS);

	rc = ratbag_hidraw_raw_request(device, ROCCAT_REPORT_ID_SETTINGS,
				       buf, ROCCAT_REPORT_SIZE_SETTINGS,
				       HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (rc < 0)
		return rc;

	if (rc != ROCCAT_REPORT_SIZE_SETTINGS)
		return -EIO;

	rc = roccat_wait_ready(device);
	if (rc)
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-rc), rc);

	return rc;
}

static int
roccat_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct ratbag_profile *profile;
	struct roccat_data *drv_data;
	int active_idx;

	rc = ratbag_open_hidraw(device);
	if (rc) {
		log_error(device->ratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	if (!ratbag_hidraw_has_report(device, ROCCAT_REPORT_ID_KEY_MAPPING)) {
		ratbag_close_hidraw(device);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	/* profiles are 0-indexed */
	ratbag_device_init_profiles(device, ROCCAT_PROFILE_MAX + 1, ROCCAT_BUTTON_MAX + 1);

	active_idx = roccat_current_profile(device);
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
roccat_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

static const struct ratbag_id roccat_table[] = {
	{.id = { .bustype = BUS_USB,
		 .vendor = 0x1e7d,
		 .product = 0x2e22,
		 .version = VERSION_ANY },
	 .svg_filename = "roccat-kone-xtd.svg",
	},

	{ },
};

struct ratbag_driver roccat_driver = {
	.name = "Roccat Kone XTD",
	.table_ids = roccat_table,
	.probe = roccat_probe,
	.remove = roccat_remove,
	.read_profile = roccat_read_profile,
	.write_profile = roccat_write_profile,
	.set_active_profile = roccat_set_current_profile,
	.has_capability = roccat_has_capability,
	.read_button = roccat_read_button,
	.write_button = roccat_write_button,
	.write_resolution_dpi = roccat_write_resolution_dpi,
};
