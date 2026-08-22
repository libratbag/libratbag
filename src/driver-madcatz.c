/*
 * Copyright © 2026 Contributors
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

/*
 * Mad Catz MMO 7+ wireless gaming mouse driver.
 *
 * Protocol reverse-engineered from the Windows H.U.D. software's
 * hiddriver_dongle.dll (native) and mcz.allinone.devices.dll (.NET 8).
 *
 * Communication uses HID feature reports via a USB wireless dongle.
 * Reads use a command/poll/read protocol through report 0xA0.
 * Writes go directly to the target report with retries for wireless relay.
 *
 * Note: The hid-generic kernel driver on interface 2 interferes with
 * feature reports. A udev rule to unbind hid-generic from the config
 * interface is required for this driver to function.
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

/* Device limits */
#define MADCATZ_PROFILE_MAX		5
#define MADCATZ_BUTTON_MAX		21
#define MADCATZ_NUM_DPI			8
#define MADCATZ_NUM_LED			1
#define MADCATZ_NUM_BUTTONS_ALL		30
#define MADCATZ_MAX_MACRO_LENGTH	50

/* HID report IDs */
#define MADCATZ_REPORT_CMD		0xA0
#define MADCATZ_REPORT_FW		0x0B
#define MADCATZ_REPORT_PROFILE		0x0C
#define MADCATZ_REPORT_SENSOR		0x04
#define MADCATZ_REPORT_LIGHT		0x05
#define MADCATZ_REPORT_RATE		0x06
#define MADCATZ_REPORT_KEYS		0x08
#define MADCATZ_REPORT_KEYS_FN		0x18
#define MADCATZ_REPORT_MACRO		0x09

/* HID descriptor sizes (for reads via GetFeature) */
#define MADCATZ_SIZE_CMD		8
#define MADCATZ_SIZE_FW			8
#define MADCATZ_SIZE_PROFILE		6
#define MADCATZ_SIZE_SENSOR		52
#define MADCATZ_SIZE_LIGHT		13
#define MADCATZ_SIZE_RATE		9
#define MADCATZ_SIZE_KEYS		64
#define MADCATZ_SIZE_MACRO		64

/* Windows DLL send sizes (for writes via SetFeature) */
#define MADCATZ_SEND_LIGHT		15
#define MADCATZ_SEND_RATE		9
#define MADCATZ_SEND_SENSOR		56

/* PixArt 3395 sensor */
#define MADCATZ_DPI_MULTIPLIER		200
#define MADCATZ_DPI_MIN			200
#define MADCATZ_DPI_MAX			26000

/* Wireless retry parameters */
#define MADCATZ_SEND_RETRIES		10
#define MADCATZ_SEND_DELAY_MS		500

/* Button function codes from the device */
#define MADCATZ_FUNC_DISABLED		0x00
#define MADCATZ_FUNC_OFF		0x01
#define MADCATZ_FUNC_CLICK		0x02
#define MADCATZ_FUNC_MENU		0x03
#define MADCATZ_FUNC_MIDDLE		0x04
#define MADCATZ_FUNC_BROWSER_BACK	0x05
#define MADCATZ_FUNC_BROWSER_FWD	0x06
#define MADCATZ_FUNC_DOUBLECLICK	0x07
#define MADCATZ_FUNC_RAPID_FIRE		0x08
#define MADCATZ_FUNC_SCROLL_UP		0x09
#define MADCATZ_FUNC_SCROLL_DOWN	0x0A
#define MADCATZ_FUNC_TILT_LEFT		0x0B
#define MADCATZ_FUNC_TILT_RIGHT		0x0C
#define MADCATZ_FUNC_DPI_CYCLE		0x0D
#define MADCATZ_FUNC_DPI_UP		0x0E
#define MADCATZ_FUNC_DPI_DOWN		0x0F
#define MADCATZ_FUNC_EASY_AIM		0x10
#define MADCATZ_FUNC_KEY		0x11
#define MADCATZ_FUNC_MACRO		0x12
#define MADCATZ_FUNC_WIN_KEY		0x14
#define MADCATZ_FUNC_MEDIA_PLAYER	0x15
#define MADCATZ_FUNC_PREV_TRACK		0x16
#define MADCATZ_FUNC_NEXT_TRACK		0x17
#define MADCATZ_FUNC_PLAY_PAUSE		0x18
#define MADCATZ_FUNC_STOP		0x19
#define MADCATZ_FUNC_MUTE		0x1A
#define MADCATZ_FUNC_VOL_UP		0x1B
#define MADCATZ_FUNC_VOL_DOWN		0x1C
#define MADCATZ_FUNC_FN			0x27
#define MADCATZ_FUNC_RATE_CYCLE		0x28
#define MADCATZ_FUNC_PROFILE_CYCLE	0x34
#define MADCATZ_FUNC_PROFILE_UP		0x35
#define MADCATZ_FUNC_PROFILE_DOWN	0x36
#define MADCATZ_FUNC_LEFT_ACTIVE	0x3D
#define MADCATZ_FUNC_RIGHT_ACTIVE	0x3E

/* Macro action flags */
#define MADCATZ_MACRO_MAKE		0x00
#define MADCATZ_MACRO_BREAK		0x80

/* Modifier key bitmask */
#define MADCATZ_MOD_CTRL		0x01
#define MADCATZ_MOD_SHIFT		0x02
#define MADCATZ_MOD_ALT			0x04
#define MADCATZ_MOD_WIN			0x08

/* LED modes */
#define MADCATZ_LED_OFF			0x00
#define MADCATZ_LED_STATIC		0x01
#define MADCATZ_LED_BREATHE		0x02
#define MADCATZ_LED_NEON		0x03
#define MADCATZ_LED_COLOR_LOOP		0x04

/* Rate wire encoding (only safe values) */
#define MADCATZ_RATE_1000HZ		1
#define MADCATZ_RATE_250HZ		4

struct madcatz_button_info {
	uint8_t function;
	uint8_t modifier;
	uint8_t keycode;
} __attribute__((packed));

struct madcatz_data {
	uint8_t active_profile;
	uint8_t profile_count;

	/* Cached raw report data per profile (1-indexed internally) */
	uint8_t sensor[MADCATZ_PROFILE_MAX][MADCATZ_SIZE_SENSOR];
	uint8_t light[MADCATZ_PROFILE_MAX][MADCATZ_SIZE_LIGHT];
	uint8_t rate[MADCATZ_PROFILE_MAX][MADCATZ_SIZE_RATE];
	uint8_t keys[MADCATZ_PROFILE_MAX][MADCATZ_SIZE_KEYS];
	uint8_t keys_fn[MADCATZ_PROFILE_MAX][MADCATZ_SIZE_KEYS];
};

/* --- Button mapping table --- */

struct madcatz_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static struct madcatz_button_mapping madcatz_button_mapping[] = {
	{ MADCATZ_FUNC_DISABLED,	BUTTON_ACTION_NONE },
	{ MADCATZ_FUNC_OFF,		BUTTON_ACTION_NONE },
	{ MADCATZ_FUNC_CLICK,		BUTTON_ACTION_BUTTON(1) },
	{ MADCATZ_FUNC_MENU,		BUTTON_ACTION_BUTTON(2) },
	{ MADCATZ_FUNC_MIDDLE,		BUTTON_ACTION_BUTTON(3) },
	{ MADCATZ_FUNC_BROWSER_BACK,	BUTTON_ACTION_BUTTON(4) },
	{ MADCATZ_FUNC_BROWSER_FWD,	BUTTON_ACTION_BUTTON(5) },
	{ MADCATZ_FUNC_LEFT_ACTIVE,	BUTTON_ACTION_BUTTON(6) },
	{ MADCATZ_FUNC_RIGHT_ACTIVE,	BUTTON_ACTION_BUTTON(7) },
	{ MADCATZ_FUNC_DOUBLECLICK,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },
	{ MADCATZ_FUNC_SCROLL_UP,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ MADCATZ_FUNC_SCROLL_DOWN,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
	{ MADCATZ_FUNC_TILT_LEFT,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT) },
	{ MADCATZ_FUNC_TILT_RIGHT,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT) },
	{ MADCATZ_FUNC_DPI_CYCLE,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ MADCATZ_FUNC_DPI_UP,		BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ MADCATZ_FUNC_DPI_DOWN,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ MADCATZ_FUNC_EASY_AIM,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE) },
	{ MADCATZ_FUNC_MACRO,		BUTTON_ACTION_MACRO },
	{ MADCATZ_FUNC_WIN_KEY,		BUTTON_ACTION_KEY(KEY_LEFTMETA) },
	{ MADCATZ_FUNC_MEDIA_PLAYER,	BUTTON_ACTION_KEY(KEY_MEDIA) },
	{ MADCATZ_FUNC_PREV_TRACK,	BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ MADCATZ_FUNC_NEXT_TRACK,	BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ MADCATZ_FUNC_PLAY_PAUSE,	BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ MADCATZ_FUNC_STOP,		BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ MADCATZ_FUNC_MUTE,		BUTTON_ACTION_KEY(KEY_MUTE) },
	{ MADCATZ_FUNC_VOL_UP,		BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ MADCATZ_FUNC_VOL_DOWN,	BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },
	{ MADCATZ_FUNC_FN,		BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE) },
	{ MADCATZ_FUNC_PROFILE_CYCLE,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ MADCATZ_FUNC_PROFILE_UP,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ MADCATZ_FUNC_PROFILE_DOWN,	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
};

static const struct ratbag_button_action *
madcatz_raw_to_button_action(uint8_t raw)
{
	struct madcatz_button_mapping *mapping;

	ARRAY_FOR_EACH(madcatz_button_mapping, mapping) {
		if (mapping->raw == raw)
			return &mapping->action;
	}

	return NULL;
}

static uint8_t
madcatz_button_action_to_raw(const struct ratbag_button_action *action)
{
	struct madcatz_button_mapping *mapping;

	ARRAY_FOR_EACH(madcatz_button_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->raw;
	}

	return MADCATZ_FUNC_DISABLED;
}

/* --- Core protocol functions --- */

/*
 * Query a feature report from the device.
 * Uses the 0xA0 command/poll/read protocol:
 *   1. SetFeature 0xA0 with target report ID and profile
 *   2. Poll GetFeature 0xA0 until byte[1] == 0x01 (ready)
 *   3. GetFeature target report to read data
 */
static int
madcatz_query(struct ratbag_device *device, uint8_t report_id,
	      uint8_t *buf, size_t size, uint8_t profile_id)
{
	uint8_t cmd[MADCATZ_SIZE_CMD] = {
		MADCATZ_REPORT_CMD,
		report_id,
		(uint8_t)size,
		0x00,
		0x00,
		profile_id,
		0x00,
		0x00
	};
	uint8_t status[MADCATZ_SIZE_CMD];
	int rc;

	rc = ratbag_hidraw_raw_request(device, MADCATZ_REPORT_CMD,
				       cmd, sizeof(cmd),
				       HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (rc < 0)
		return rc;

	msleep(250);

	for (int i = 0; i < 10; i++) {
		memset(status, 0, sizeof(status));
		status[0] = MADCATZ_REPORT_CMD;
		rc = ratbag_hidraw_raw_request(device, MADCATZ_REPORT_CMD,
					       status, sizeof(status),
					       HID_FEATURE_REPORT,
					       HID_REQ_GET_REPORT);
		if (rc < 0)
			return rc;

		if (status[1] == 0x01)
			goto ready;

		msleep(150);
	}

	log_error(device->ratbag,
		  "madcatz: device not ready for report 0x%02x\n", report_id);
	return -ETIMEDOUT;

ready:
	buf[0] = report_id;
	rc = ratbag_hidraw_raw_request(device, report_id,
				       buf, size,
				       HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (rc < 0)
		return rc;

	return 0;
}

/*
 * Send a feature report to the device with retries.
 * Wireless dongle relay requires multiple attempts with delays.
 */
static int
madcatz_send(struct ratbag_device *device, uint8_t report_id,
	     uint8_t *buf, size_t size)
{
	int rc = -EIO;

	for (int i = 0; i < MADCATZ_SEND_RETRIES; i++) {
		rc = ratbag_hidraw_raw_request(device, report_id,
					       buf, size,
					       HID_FEATURE_REPORT,
					       HID_REQ_SET_REPORT);
		if (rc >= 0)
			msleep(MADCATZ_SEND_DELAY_MS);
		else
			msleep(MADCATZ_SEND_DELAY_MS);
	}

	return rc < 0 ? rc : 0;
}

/* --- Read functions --- */

static int
madcatz_read_profile_id(struct ratbag_device *device, struct madcatz_data *drv_data)
{
	uint8_t buf[MADCATZ_SIZE_PROFILE];
	int rc;

	rc = madcatz_query(device, MADCATZ_REPORT_PROFILE,
			   buf, sizeof(buf), 0);
	if (rc < 0)
		return rc;

	/* Response: [0x0C, ?, profileId, ~profileId, profileCount, ~profileCount] */
	drv_data->active_profile = buf[2];
	drv_data->profile_count = buf[4];

	if (drv_data->profile_count == 0 || drv_data->profile_count > MADCATZ_PROFILE_MAX)
		drv_data->profile_count = MADCATZ_PROFILE_MAX;

	log_debug(device->ratbag,
		  "madcatz: active profile %d, count %d\n",
		  drv_data->active_profile, drv_data->profile_count);

	return 0;
}

static void
madcatz_read_sensor(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	unsigned int idx = profile->index;
	uint8_t device_profile = idx + 1;
	uint8_t *buf = drv_data->sensor[idx];
	struct ratbag_resolution *resolution;
	int rc;

	rc = madcatz_query(device, MADCATZ_REPORT_SENSOR,
			   buf, MADCATZ_SIZE_SENSOR, device_profile);
	if (rc < 0) {
		log_error(device->ratbag,
			  "madcatz: failed to read sensor for profile %d\n", idx);
		return;
	}

	/* Sensor data starts at byte 1 (after report ID):
	 * [reportId, profileId, lift, angleSnap, dpiFlag, reserve,
	 *  centerOffset, dpiX[8], dpiY[8], currentLevel, dpiIndication[24],
	 *  dpiIndicationType] */
	uint8_t *d = &buf[1];
	uint8_t dpi_flag = d[3];
	uint8_t *dpi_x = &d[6];
	uint8_t *dpi_y = &d[14];
	uint8_t current_level = d[22];

	ratbag_profile_for_each_resolution(profile, resolution) {
		unsigned int i = resolution->index;

		if (i >= MADCATZ_NUM_DPI)
			break;

		ratbag_resolution_set_dpi_list_from_range(resolution,
							  MADCATZ_DPI_MIN,
							  MADCATZ_DPI_MAX);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

		if (dpi_flag & (1 << i)) {
			resolution->dpi_x = dpi_x[i] * MADCATZ_DPI_MULTIPLIER;
			resolution->dpi_y = dpi_y[i] * MADCATZ_DPI_MULTIPLIER;
			resolution->is_active = (i + 1 == current_level);
			resolution->is_disabled = false;
		} else {
			resolution->dpi_x = 0;
			resolution->dpi_y = 0;
			resolution->is_disabled = true;
		}
	}
}

static void
madcatz_read_rate(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	unsigned int idx = profile->index;
	uint8_t device_profile = idx + 1;
	uint8_t *buf = drv_data->rate[idx];
	int rc;
	unsigned int report_rates[] = { 250, 1000 };

	rc = madcatz_query(device, MADCATZ_REPORT_RATE,
			   buf, MADCATZ_SIZE_RATE, device_profile);
	if (rc < 0) {
		log_error(device->ratbag,
			  "madcatz: failed to read rate for profile %d\n", idx);
		return;
	}

	ratbag_profile_set_report_rate_list(profile, report_rates,
					    ARRAY_LENGTH(report_rates));

	/* Rate response: [reportId, ?, wireValue, ~wireValue, ...] */
	uint8_t wire = buf[2];
	switch (wire) {
	case MADCATZ_RATE_1000HZ:
		profile->hz = 1000;
		break;
	case MADCATZ_RATE_250HZ:
		profile->hz = 250;
		break;
	default:
		log_debug(device->ratbag,
			  "madcatz: unknown rate wire value %d\n", wire);
		profile->hz = 1000;
		break;
	}
}

static void
madcatz_read_light(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	unsigned int idx = profile->index;
	uint8_t device_profile = idx + 1;
	uint8_t *buf = drv_data->light[idx];
	struct ratbag_led *led;
	int rc;

	rc = madcatz_query(device, MADCATZ_REPORT_LIGHT,
			   buf, MADCATZ_SIZE_LIGHT, device_profile);
	if (rc < 0) {
		log_error(device->ratbag,
			  "madcatz: failed to read light for profile %d\n", idx);
		return;
	}

	/* Light response: [reportId, sizeMarker, profileId, mode, speed,
	 *                  bright, R, G, B, sleep, reserve, chk_hi, chk_lo] */
	led = ratbag_profile_get_led(profile, 0);
	if (!led)
		return;

	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
	ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
	ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);

	uint8_t mode = buf[3];
	switch (mode & 0x0F) {
	case MADCATZ_LED_OFF:
		led->mode = RATBAG_LED_OFF;
		break;
	case MADCATZ_LED_STATIC:
		led->mode = RATBAG_LED_ON;
		break;
	case MADCATZ_LED_BREATHE:
		led->mode = RATBAG_LED_BREATHING;
		break;
	case MADCATZ_LED_NEON:
	case MADCATZ_LED_COLOR_LOOP:
		led->mode = RATBAG_LED_CYCLE;
		break;
	default:
		led->mode = RATBAG_LED_BREATHING;
		break;
	}

	led->color.red = buf[6];
	led->color.green = buf[7];
	led->color.blue = buf[8];
	led->brightness = buf[5];
}

static void
madcatz_read_buttons(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	unsigned int idx = profile->index;
	uint8_t device_profile = idx + 1;
	uint8_t *buf = drv_data->keys[idx];
	struct ratbag_button *button;
	int rc;

	rc = madcatz_query(device, MADCATZ_REPORT_KEYS,
			   buf, MADCATZ_SIZE_KEYS, device_profile);
	if (rc < 0) {
		log_error(device->ratbag,
			  "madcatz: failed to read buttons for profile %d\n", idx);
		return;
	}

	/* Keys response: [reportId, ?, {func, mod, key} * 21, ...] */
	uint8_t *d = &buf[1];

	ratbag_profile_for_each_button(profile, button) {
		unsigned int i = button->index;
		unsigned int off = 1 + i * 3;

		if (off + 2 >= MADCATZ_SIZE_KEYS - 1)
			break;

		uint8_t func = d[off];
		uint8_t mod = d[off + 1];
		uint8_t key = d[off + 2];

		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
		ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

		if (func == MADCATZ_FUNC_KEY) {
			/* Keyboard shortcut: convert HID usage to keycode */
			unsigned int keycode;

			keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(
					device, key);
			if (keycode == 0)
				keycode = KEY_UNKNOWN;

			struct ratbag_button_action action =
				BUTTON_ACTION_KEY(keycode);
			ratbag_button_set_action(button, &action);
		} else if (func == MADCATZ_FUNC_MACRO) {
			/* Read macro data from device using the macro index */
			uint8_t macro_buf[MADCATZ_SIZE_MACRO];
			int mrc;

			mrc = madcatz_query(device, MADCATZ_REPORT_MACRO,
					    macro_buf, MADCATZ_SIZE_MACRO, key);
			if (mrc == 0) {
				/*
				 * MACRO struct (from byte 2, after reportId+sizeMarker):
				 * [Index, MacroCodeType, Reserve[3], MacroExecBehavior,
				 *  MacroName[20], KeyMacroNumber, KeyInfo[50]]
				 *
				 * KeyInfo: 50 x MACRO_KEY_INFO {AttributeCode, KeyCode}
				 *   AttributeCode: 0x00=MAKE(press), 0x80=BREAK(release)
				 */
				uint8_t *md = &macro_buf[2];
				uint8_t num_keys = md[26];
				char macro_name[21] = {0};
				memcpy(macro_name, &md[6], 20);

				struct ratbag_button_macro *m;
				m = ratbag_button_macro_new(macro_name);

				for (unsigned int j = 0; j < num_keys && j < MADCATZ_MAX_MACRO_LENGTH; j++) {
					uint8_t attr = md[27 + j * 2];
					uint8_t kc = md[27 + j * 2 + 1];
					unsigned int keycode;

					keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(
							device, kc);
					if (keycode == 0)
						keycode = KEY_UNKNOWN;

					ratbag_button_macro_set_event(m,
						j * 2,
						(attr & MADCATZ_MACRO_BREAK) ?
							RATBAG_MACRO_EVENT_KEY_RELEASED :
							RATBAG_MACRO_EVENT_KEY_PRESSED,
						keycode);
					ratbag_button_macro_set_event(m,
						j * 2 + 1,
						RATBAG_MACRO_EVENT_WAIT,
						10);
				}

				ratbag_button_copy_macro(button, m);
				ratbag_button_macro_unref(m);
			} else {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
			}
		} else {
			const struct ratbag_button_action *action;

			action = madcatz_raw_to_button_action(func);
			if (action)
				ratbag_button_set_action(button, action);
			else
				button->action.type =
					RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		}
	}
}

/* --- Write functions --- */

static int
madcatz_write_light(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_led *led;
	uint8_t device_profile = drv_data->active_profile;
	uint8_t buf[MADCATZ_SEND_LIGHT];
	uint8_t mode;
	uint16_t checksum;

	led = ratbag_profile_get_led(profile, 0);
	if (!led || !led->dirty)
		return 0;

	switch (led->mode) {
	case RATBAG_LED_OFF:
		mode = MADCATZ_LED_OFF;
		break;
	case RATBAG_LED_ON:
		mode = MADCATZ_LED_STATIC;
		break;
	case RATBAG_LED_BREATHING:
		mode = MADCATZ_LED_BREATHE;
		break;
	case RATBAG_LED_CYCLE:
		mode = MADCATZ_LED_NEON;
		break;
	default:
		mode = MADCATZ_LED_BREATHE;
		break;
	}

	/* Build send buffer:
	 * [reportId, sendSize, profileId, mode, speed, bright,
	 *  R, G, B, sleep, reserve, chk_hi, chk_lo, pad, pad] */
	memset(buf, 0, sizeof(buf));
	buf[0] = MADCATZ_REPORT_LIGHT;
	buf[1] = MADCATZ_SEND_LIGHT;
	buf[2] = device_profile;
	buf[3] = mode;
	buf[4] = 3; /* default breathing speed */
	buf[5] = led->brightness;
	buf[6] = led->color.red;
	buf[7] = led->color.green;
	buf[8] = led->color.blue;
	buf[9] = 2; /* default sleep time */
	buf[10] = 4; /* default reserve */

	/* Checksum: sum of mode through reserve (bytes 3-10) */
	checksum = 0;
	for (int i = 3; i <= 10; i++)
		checksum += buf[i];

	buf[11] = (checksum >> 8) & 0xFF;
	buf[12] = checksum & 0xFF;

	log_debug(device->ratbag,
		  "madcatz: writing LED RGB(%d,%d,%d) mode=%d profile=%d\n",
		  led->color.red, led->color.green, led->color.blue,
		  mode, device_profile);

	return madcatz_send(device, MADCATZ_REPORT_LIGHT, buf, sizeof(buf));
}

static int
madcatz_write_sensor(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	uint8_t device_profile = drv_data->active_profile;
	uint8_t *src = drv_data->sensor[profile->index];
	uint8_t buf[MADCATZ_SIZE_SENSOR];
	struct ratbag_resolution *resolution;
	uint16_t checksum;

	/*
	 * Build send buffer from the read data, then apply changes.
	 * Wire format: [reportId, sendSize(0x38), SENSOR struct (48 bytes),
	 *               checksum_hi, checksum_lo]
	 *
	 * SENSOR struct: [ProfileId, Lift, AngleSnap, DpiFlag, Reserve,
	 *                 CenterOffset, DpiX[8], DpiY[8], CurrentLevel,
	 *                 DpiIndication[24], DpiIndicationType]
	 *
	 * Read response: [reportId, sizeMarker, data...]
	 * Data starts at src[2], which is the SENSOR struct.
	 */
	memset(buf, 0, sizeof(buf));
	buf[0] = MADCATZ_REPORT_SENSOR;
	buf[1] = MADCATZ_SEND_SENSOR;
	buf[2] = device_profile;

	/* Copy sensor data from read buffer.
	 * Read format: [reportId, sizeMarker, Lift, AngleSnap, DpiFlag, ...]
	 * Write format: [reportId, sendSize, ProfileId, Lift, AngleSnap, DpiFlag, ...]
	 * src[2] = Lift (first struct field after ProfileId in the response) */
	memcpy(&buf[3], &src[2], 47); /* 47 bytes: Lift through DpiIndicationType */

	/* Apply DPI changes from ratbag */
	uint8_t *dpi_x = &buf[8];  /* offset 2 + 6 in struct */
	uint8_t *dpi_y = &buf[16]; /* offset 2 + 14 in struct */
	uint8_t dpi_flag = 0;

	ratbag_profile_for_each_resolution(profile, resolution) {
		unsigned int i = resolution->index;

		if (i >= MADCATZ_NUM_DPI)
			break;

		if (!resolution->is_disabled && resolution->dpi_x > 0) {
			dpi_flag |= (1 << i);
			dpi_x[i] = resolution->dpi_x / MADCATZ_DPI_MULTIPLIER;
			dpi_y[i] = resolution->dpi_y / MADCATZ_DPI_MULTIPLIER;
		} else {
			dpi_x[i] = 0;
			dpi_y[i] = 0;
		}

		if (resolution->is_active)
			buf[24] = i + 1; /* CurrentDpiLevel, 1-indexed */
	}

	buf[5] = dpi_flag; /* DpiFlag at SENSOR struct offset 3 → buf offset 5 */

	/* Checksum: sum of bytes 3 to end of sensor data (before checksum) */
	checksum = 0;
	for (int i = 3; i < MADCATZ_SIZE_SENSOR - 2; i++)
		checksum += buf[i];

	buf[MADCATZ_SIZE_SENSOR - 2] = (checksum >> 8) & 0xFF;
	buf[MADCATZ_SIZE_SENSOR - 1] = checksum & 0xFF;

	log_debug(device->ratbag,
		  "madcatz: writing sensor for profile %d\n", profile->index);

	return madcatz_send(device, MADCATZ_REPORT_SENSOR, buf, MADCATZ_SIZE_SENSOR);
}

static int
madcatz_write_rate(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	uint8_t device_profile = drv_data->active_profile;
	uint8_t buf[MADCATZ_SEND_RATE];
	uint8_t wire;

	switch (profile->hz) {
	case 1000:
		wire = MADCATZ_RATE_1000HZ;
		break;
	case 250:
		wire = MADCATZ_RATE_250HZ;
		break;
	default:
		log_error(device->ratbag,
			  "madcatz: unsupported rate %d Hz\n", profile->hz);
		return -EINVAL;
	}

	/* Build send buffer:
	 * [reportId, sendSize, profileId, wireRate, ~wireRate, pad...] */
	memset(buf, 0, sizeof(buf));
	buf[0] = MADCATZ_REPORT_RATE;
	buf[1] = MADCATZ_SEND_RATE;
	buf[2] = device_profile;
	buf[3] = wire;
	buf[4] = ~wire;

	/* Send 0xA0 write command first, then the data */
	uint8_t cmd[MADCATZ_SIZE_CMD] = {
		MADCATZ_REPORT_CMD,
		MADCATZ_REPORT_RATE,
		MADCATZ_SEND_RATE,
		0x01,
		0x00,
		device_profile,
		0x00,
		0x00
	};
	ratbag_hidraw_raw_request(device, MADCATZ_REPORT_CMD,
				  cmd, sizeof(cmd),
				  HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	msleep(300);

	log_debug(device->ratbag,
		  "madcatz: writing rate %d Hz (wire=%d) profile=%d\n",
		  profile->hz, wire, device_profile);

	return madcatz_send(device, MADCATZ_REPORT_RATE, buf, sizeof(buf));
}

static int
madcatz_write_macro(struct ratbag_device *device, uint8_t macro_index,
		    struct ratbag_button *button)
{
	uint8_t macro_struct[127]; /* MACRO struct */
	uint8_t data[128]; /* struct bytes 1-126 + checksum */
	uint8_t chunk[MADCATZ_SIZE_MACRO]; /* 64 bytes per chunk */
	uint16_t checksum;
	struct ratbag_macro *macro;
	int rc;
	unsigned int num_keys = 0;

	if (!button->action.macro)
		return -EINVAL;

	macro = button->action.macro;

	/* Build MACRO struct */
	memset(macro_struct, 0, sizeof(macro_struct));
	macro_struct[0] = macro_index;     /* Index */
	macro_struct[1] = 0x00;            /* MacroCodeType: LOOP_COUNT */
	/* [2-4] Reserve = 0 */
	macro_struct[5] = 0x01;            /* MacroExecBehavior: repeat 1 time */

	/* MacroName at offset 6-25 */
	if (macro->name)
		strncpy((char *)&macro_struct[6], macro->name, 20);

	/* Convert ratbag macro events to MACRO_KEY_INFO at offset 27 */
	for (int i = 0; i < MAX_MACRO_EVENTS && num_keys < MADCATZ_MAX_MACRO_LENGTH; i++) {
		struct ratbag_macro_event *ev = &macro->events[i];

		if (ev->type == RATBAG_MACRO_EVENT_NONE)
			break;

		if (ev->type == RATBAG_MACRO_EVENT_KEY_PRESSED ||
		    ev->type == RATBAG_MACRO_EVENT_KEY_RELEASED) {
			uint8_t hid_usage;

			hid_usage = ratbag_hidraw_get_keyboard_usage_from_keycode(
					device, ev->event.key);

			macro_struct[27 + num_keys * 2] =
				(ev->type == RATBAG_MACRO_EVENT_KEY_RELEASED) ?
				MADCATZ_MACRO_BREAK : MADCATZ_MACRO_MAKE;
			macro_struct[27 + num_keys * 2 + 1] = hid_usage;
			num_keys++;
		}
		/* Skip WAIT events — device uses fixed timing */
	}

	macro_struct[26] = num_keys; /* KeyMacroNumber */

	log_debug(device->ratbag,
		  "madcatz: writing macro %d (%d keys)\n",
		  macro_index, num_keys);

	/* Build data: macro struct bytes 1-126 (skip Index) + checksum */
	memcpy(data, &macro_struct[1], 126);

	/* Checksum: sum of all 126 data bytes */
	checksum = 0;
	for (int i = 0; i < 126; i++)
		checksum += data[i];
	data[126] = (checksum >> 8) & 0xFF;
	data[127] = checksum & 0xFF;

	/*
	 * Chunked write protocol (from DLL decompilation):
	 * Three SET_REPORT calls per write, each 64 bytes.
	 *
	 * Chunk 0: [0x09, 0x40, macroIndex, 0x00, data[0..59]]
	 * Chunk 1: [0x09, 0x40, macroIndex, 0x01, data[60..119]]
	 * Chunk 2: [0x09, 0x0C, macroIndex, 0x02, data[120..127]]
	 */
	for (int retry = 0; retry < MADCATZ_SEND_RETRIES; retry++) {
		for (int ci = 0; ci < 3; ci++) {
			memset(chunk, 0, MADCATZ_SIZE_MACRO);
			chunk[0] = MADCATZ_REPORT_MACRO;
			chunk[1] = (ci == 2) ? 0x0C : 0x40;
			chunk[2] = macro_index;
			chunk[3] = ci;

			unsigned int data_len = (ci == 2) ? 8 : 60;
			memcpy(&chunk[4], &data[ci * 60], data_len);

			rc = ratbag_hidraw_raw_request(device,
						       MADCATZ_REPORT_MACRO,
						       chunk, MADCATZ_SIZE_MACRO,
						       HID_FEATURE_REPORT,
						       HID_REQ_SET_REPORT);
			if (rc >= 0)
				msleep(MADCATZ_SEND_DELAY_MS);
		}
	}

	return rc < 0 ? rc : 0;
}

static int
madcatz_write_buttons(struct ratbag_device *device, struct ratbag_profile *profile)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	unsigned int idx = profile->index;
	uint8_t device_profile = drv_data->active_profile;
	uint8_t *src = drv_data->keys[idx];
	uint8_t buttons[MADCATZ_NUM_BUTTONS_ALL * 3]; /* 30 buttons × 3 bytes */
	uint8_t buf[MADCATZ_SIZE_KEYS];
	struct ratbag_button *button;
	bool any_dirty = false;
	uint16_t checksum;
	int rc;

	/*
	 * Extract button data from read buffer.
	 * Read format: [reportId, profileId, button_data...]
	 * We have 62 bytes of button data (64 - 2 header bytes).
	 */
	memset(buttons, 0, sizeof(buttons));
	memcpy(buttons, &src[2], 62);

	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;

		any_dirty = true;
		unsigned int i = button->index;
		unsigned int off = i * 3;

		if (off + 2 >= (int)sizeof(buttons))
			continue;

		switch (button->action.type) {
		case RATBAG_BUTTON_ACTION_TYPE_KEY: {
			uint8_t hid_usage;
			hid_usage = ratbag_hidraw_get_keyboard_usage_from_keycode(
					device, button->action.action.key);
			buttons[off] = MADCATZ_FUNC_KEY;
			buttons[off + 1] = 0; /* modifier */
			buttons[off + 2] = hid_usage;
			break;
		}
		case RATBAG_BUTTON_ACTION_TYPE_MACRO: {
			/* Use button index as macro slot */
			uint8_t macro_slot = button->index;
			buttons[off] = MADCATZ_FUNC_MACRO;
			buttons[off + 1] = 0;
			buttons[off + 2] = macro_slot;

			/* Write the macro data to the device */
			if (button->action.macro)
				madcatz_write_macro(device, macro_slot, button);
			break;
		}
		case RATBAG_BUTTON_ACTION_TYPE_NONE:
			buttons[off] = MADCATZ_FUNC_DISABLED;
			buttons[off + 1] = 0;
			buttons[off + 2] = 0;
			break;
		default: {
			uint8_t raw = madcatz_button_action_to_raw(
					&button->action);
			buttons[off] = raw;
			buttons[off + 1] = 0;
			buttons[off + 2] = 0;
			break;
		}
		}
	}

	if (!any_dirty)
		return 0;

	/* Checksum: sum of all 90 button bytes */
	checksum = 0;
	for (int i = 0; i < MADCATZ_NUM_BUTTONS_ALL * 3; i++)
		checksum += buttons[i];

	log_debug(device->ratbag,
		  "madcatz: writing buttons for profile %d (checksum=0x%04x)\n",
		  idx, checksum);

	/*
	 * SendKeys uses a chunked write protocol (from DLL decompilation):
	 * Two SET_REPORT calls per write, each 64 bytes, sent as a pair.
	 * The pair is retried 10 times (for wireless dongle relay).
	 *
	 * Chunk 0: [reportId, 0x40, profileId, 0x00, buttons 0-19 (60 bytes)]
	 * Chunk 1: [reportId, 0x24, profileId, 0x01, buttons 20-29 (30 bytes),
	 *           checksum_hi, checksum_lo, padding...]
	 */
	uint8_t chunk0[MADCATZ_SIZE_KEYS];
	uint8_t chunk1[MADCATZ_SIZE_KEYS];

	/* Build chunk 0: buttons 0-19 */
	memset(chunk0, 0, MADCATZ_SIZE_KEYS);
	chunk0[0] = MADCATZ_REPORT_KEYS;
	chunk0[1] = 0x40;
	chunk0[2] = device_profile;
	chunk0[3] = 0x00;
	memcpy(&chunk0[4], &buttons[0], 60);

	/* Build chunk 1: buttons 20-29 + checksum */
	memset(chunk1, 0, MADCATZ_SIZE_KEYS);
	chunk1[0] = MADCATZ_REPORT_KEYS;
	chunk1[1] = 0x24;
	chunk1[2] = device_profile;
	chunk1[3] = 0x01;
	memcpy(&chunk1[4], &buttons[60], 30);
	chunk1[34] = (checksum >> 8) & 0xFF;
	chunk1[35] = checksum & 0xFF;

	/* Send both chunks as a pair, retried 10 times (matching DLL behavior) */
	for (int i = 0; i < MADCATZ_SEND_RETRIES; i++) {
		rc = ratbag_hidraw_raw_request(device, MADCATZ_REPORT_KEYS,
					       chunk0, MADCATZ_SIZE_KEYS,
					       HID_FEATURE_REPORT,
					       HID_REQ_SET_REPORT);
		if (rc >= 0)
			msleep(MADCATZ_SEND_DELAY_MS);

		rc = ratbag_hidraw_raw_request(device, MADCATZ_REPORT_KEYS,
					       chunk1, MADCATZ_SIZE_KEYS,
					       HID_FEATURE_REPORT,
					       HID_REQ_SET_REPORT);
		if (rc >= 0)
			msleep(MADCATZ_SEND_DELAY_MS);
	}

	return rc < 0 ? rc : 0;
}

/* --- Driver callbacks --- */

static int
madcatz_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, MADCATZ_REPORT_CMD);
}

static int
madcatz_probe(struct ratbag_device *device)
{
	struct madcatz_data *drv_data;
	struct ratbag_profile *profile;
	int rc;

	rc = ratbag_find_hidraw(device, madcatz_test_hidraw);
	if (rc)
		return rc;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	/* Read firmware version */
	uint8_t fw_buf[MADCATZ_SIZE_FW];
	rc = madcatz_query(device, MADCATZ_REPORT_FW,
			   fw_buf, sizeof(fw_buf), 0);
	if (rc < 0) {
		log_error(device->ratbag,
			  "madcatz: failed to read firmware version\n");
		goto err;
	}

	char fw_version[32];
	snprintf(fw_version, sizeof(fw_version), "%d.%d.%d.%d",
		 fw_buf[1], fw_buf[2], fw_buf[3], fw_buf[4]);
	ratbag_device_set_firmware_version(device, fw_version);

	/* Read active profile */
	rc = madcatz_read_profile_id(device, drv_data);
	if (rc < 0)
		goto err;

	/* Initialize profiles */
	ratbag_device_init_profiles(device,
				    drv_data->profile_count,
				    MADCATZ_NUM_DPI,
				    MADCATZ_BUTTON_MAX,
				    MADCATZ_NUM_LED);

	/* Read all profile data */
	ratbag_device_for_each_profile(device, profile) {
		uint8_t device_profile = profile->index + 1;

		profile->is_active =
			(device_profile == drv_data->active_profile);

		madcatz_read_sensor(device, profile);
		madcatz_read_rate(device, profile);
		madcatz_read_light(device, profile);
		madcatz_read_buttons(device, profile);
	}

	log_info(device->ratbag,
		 "madcatz: Mad Catz MMO 7+ detected, FW %s, %d profiles\n",
		 fw_version, drv_data->profile_count);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static void
madcatz_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

static int
madcatz_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_led *led;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		/* Write LED */
		led = ratbag_profile_get_led(profile, 0);
		if (led && led->dirty) {
			rc = madcatz_write_light(device, profile);
			if (rc)
				return rc;
		}

		/* Write sensor/DPI */
		{
			struct ratbag_resolution *res;
			bool any_res_dirty = false;
			ratbag_profile_for_each_resolution(profile, res) {
				if (res->dirty) {
					any_res_dirty = true;
					break;
				}
			}
			if (any_res_dirty) {
				rc = madcatz_write_sensor(device, profile);
				if (rc)
					return rc;
			}
		}

		/* Write rate */
		if (profile->rate_dirty) {
			rc = madcatz_write_rate(device, profile);
			if (rc && rc != -EINVAL)
				return rc;
		}

		/* Write buttons */
		rc = madcatz_write_buttons(device, profile);
		if (rc)
			return rc;
	}

	return 0;
}

static int
madcatz_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	struct madcatz_data *drv_data = ratbag_get_drv_data(device);
	uint8_t device_profile = index + 1;
	uint8_t buf[MADCATZ_SIZE_PROFILE];

	/* Build profile switch buffer with complement bytes */
	memset(buf, 0, sizeof(buf));
	buf[0] = MADCATZ_REPORT_PROFILE;
	buf[1] = MADCATZ_SIZE_PROFILE;
	buf[2] = device_profile;
	buf[3] = ~device_profile;
	buf[4] = drv_data->profile_count;
	buf[5] = ~drv_data->profile_count;

	int rc = madcatz_send(device, MADCATZ_REPORT_PROFILE,
			      buf, sizeof(buf));
	if (rc == 0)
		drv_data->active_profile = device_profile;

	return rc;
}

struct ratbag_driver madcatz_driver = {
	.name = "Mad Catz",
	.id = "madcatz",
	.probe = madcatz_probe,
	.remove = madcatz_remove,
	.commit = madcatz_commit,
	.set_active_profile = madcatz_set_active_profile,
};
