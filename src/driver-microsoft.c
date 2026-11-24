/*
 * Copyright © 2026 The libratbag contributors.
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
 * Driver for the Microsoft Pro IntelliMouse (2019), USB VID:PID 045e:082a.
 *
 * The USB HID feature-report protocol used here was reverse-engineered by
 * the xlanor/intellimouse and namazso/ProIntelliColor projects and is
 * documented (and was cross-checked against a real device) by the nimouse
 * project's PROTOCOL.md:
 *
 *   https://github.com/xlanor/intellimouse
 *   https://github.com/namazso/ProIntelliColor
 *   nimouse (Nim CLI for this mouse) — PROTOCOL.md has the full byte layout
 *
 * Transport summary:
 *
 *  - Requests (both "set a property" and "ask to read a property") are
 *    sent as a HID Feature report with report ID 0x24, padded to 0x49 (73)
 *    bytes:  [0x24, PROP, DATA_LEN, DATA...]
 *
 *  - The response to a read request is *not* returned as the Feature
 *    report's reply; the device only answers a GET_REPORT of type Input
 *    for report ID 0x27 (41 bytes): [0x27, PROP, reserved, DATA_LEN,
 *    DATA...]. This is why this driver needs
 *    ratbag_hidraw_get_input_report() rather than the more commonly used
 *    ratbag_hidraw_get_feature_report().
 *
 *  - The device needs ~50ms to process each request before it can be
 *    queried or sent another one.
 *
 * The device has no on-board profile storage: every setting takes effect
 * immediately and there is nothing to "activate", so this driver exposes a
 * single ratbag profile.
 *
 * Only the back (thumb) button and the middle (wheel) button have a
 * documented remapping protocol; the left/right buttons are exposed as
 * fixed, non-remappable buttons. The lift-off distance and the "custom
 * per-surface LOD calibration" from the Windows software are not exposed:
 * libratbag has no matching capability for the former, and the wire
 * protocol for the latter has not been reverse-engineered.
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <linux/input.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define MS_PRO_IM_NUM_BUTTONS		4
#define MS_PRO_IM_NUM_LEDS		1
#define MS_PRO_IM_NUM_RESOLUTIONS	1

#define MS_PRO_IM_DPI_MIN		200
#define MS_PRO_IM_DPI_MAX		16000

#define MS_PRO_IM_WRITE_REPORT_ID	0x24
#define MS_PRO_IM_WRITE_REPORT_LEN	0x49	/* 73 bytes, always padded to this */
#define MS_PRO_IM_READ_REPORT_ID	0x27
#define MS_PRO_IM_READ_REPORT_LEN	0x29	/* 41 bytes */

#define MS_PRO_IM_PROP_DPI_WRITE	0x96
#define MS_PRO_IM_PROP_DPI_READ		0x97
#define MS_PRO_IM_PROP_COLOR_WRITE	0xB2
#define MS_PRO_IM_PROP_COLOR_READ	0xB3
#define MS_PRO_IM_PROP_POLLING_WRITE	0x83
#define MS_PRO_IM_PROP_POLLING_READ	0x84
#define MS_PRO_IM_PROP_BUTTON_WRITE	0x88
#define MS_PRO_IM_PROP_BUTTON_READ	0x89

/* Physical button IDs used by the 0x88/0x89 button-mapping property.
 * Only these two buttons have a documented remapping protocol. */
#define MS_PRO_IM_BUTTON_ID_MIDDLE	0x03
#define MS_PRO_IM_BUTTON_ID_BACK	0x04

/* Fixed, non-remappable buttons (no documented protocol for them). */
static const struct ratbag_button_action ms_pro_im_left_click = BUTTON_ACTION_BUTTON(1);
static const struct ratbag_button_action ms_pro_im_right_click = BUTTON_ACTION_BUTTON(2);

struct ms_pro_im_polling_mapping {
	uint8_t raw;
	unsigned int hz;
};

/* PROTOCOL.md § Polling Rate */
static struct ms_pro_im_polling_mapping ms_pro_im_polling_map[] = {
	{ 0x00, 1000 },
	{ 0x01,  500 },
	{ 0x02,  125 },
};

struct ms_pro_im_button_mapping {
	uint32_t raw;	/* 4-byte big-endian action code */
	struct ratbag_button_action action;
};

/* PROTOCOL.md § Button Mapping → Known Action Codes, reverse-engineered by
 * xlanor from USB captures of the Windows "Mouse and Keyboard Center"
 * software. actionKeyCombination (0x09000200) is the device's own "no key
 * combination" reset value and doubles as our fallback when a button
 * action can't be represented in this table. */
static struct ms_pro_im_button_mapping ms_pro_im_button_map[] = {
	{ 0x09000104, BUTTON_ACTION_BUTTON(8) },	/* back -> mouse back */
	{ 0x09000105, BUTTON_ACTION_BUTTON(9) },	/* browser forward -> mouse forward */
	{ 0x09000103, BUTTON_ACTION_BUTTON(3) },	/* middle click */
	{ 0x09000204, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE) },
	{ 0x09000201, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 0x09000202, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 0x09000203, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH) },
	{ 0x07000000, BUTTON_ACTION_KEY(KEY_LEFTALT) },
	{ 0x07100000, BUTTON_ACTION_KEY(KEY_LEFTCTRL) },
	{ 0x07200000, BUTTON_ACTION_KEY(KEY_LEFTSHIFT) },
	{ 0x07002800, BUTTON_ACTION_KEY(KEY_ENTER) },
	{ 0x09000200, BUTTON_ACTION_NONE },
};

static const struct ratbag_button_action *
ms_pro_im_raw_to_button_action(uint32_t code)
{
	struct ms_pro_im_button_mapping *m;

	ARRAY_FOR_EACH(ms_pro_im_button_map, m) {
		if (m->raw == code)
			return &m->action;
	}

	return NULL;
}

static uint32_t
ms_pro_im_button_action_to_raw(const struct ratbag_button_action *action)
{
	struct ms_pro_im_button_mapping *m;

	ARRAY_FOR_EACH(ms_pro_im_button_map, m) {
		if (ratbag_button_action_match(&m->action, action))
			return m->raw;
	}

	/* Not representable in the documented protocol - reset to "no key
	 * combination" instead of sending a code that might do something
	 * unexpected. */
	return 0x09000200;
}

/* ---- low-level protocol I/O ---------------------------------------- */

/*
 * Sends a request as a 0x24 Feature report: [0x24, prop, data_len, data...],
 * padded to MS_PRO_IM_WRITE_REPORT_LEN bytes. Used both to write a
 * property and to ask the device to prepare a property's value for a
 * following read.
 */
static int
ms_pro_im_send_request(struct ratbag_device *device, uint8_t prop,
		       const uint8_t *data, uint8_t data_len)
{
	uint8_t buf[MS_PRO_IM_WRITE_REPORT_LEN] = { 0 };
	int rc;

	if (data_len > sizeof(buf) - 3)
		return -EINVAL;

	buf[1] = prop;
	buf[2] = data_len > 0 ? data_len : 1;
	if (data)
		memcpy(&buf[3], data, data_len);

	rc = ratbag_hidraw_set_feature_report(device, MS_PRO_IM_WRITE_REPORT_ID,
					      buf, sizeof(buf));
	msleep(50);

	if (rc < 0)
		return rc;

	return rc == sizeof(buf) ? 0 : -EIO;
}

/*
 * Reads the response to a previously sent read request. The device only
 * answers with a GET_REPORT of type *Input* for report ID 0x27, not a
 * Feature report - see the file comment above.
 */
static int
ms_pro_im_read_response(struct ratbag_device *device, uint8_t *out, size_t out_len)
{
	uint8_t buf[MS_PRO_IM_READ_REPORT_LEN] = { 0 };
	int rc;
	size_t data_len;

	rc = ratbag_hidraw_get_input_report(device, MS_PRO_IM_READ_REPORT_ID,
					    buf, sizeof(buf));
	msleep(50);

	if (rc < 0)
		return rc;

	if (rc < 4)
		return -EIO;

	data_len = buf[3];
	if (data_len > sizeof(buf) - 4)
		data_len = sizeof(buf) - 4;
	if (data_len == 0)
		return -EIO;
	if (data_len > out_len)
		data_len = out_len;

	memcpy(out, &buf[4], data_len);

	return (int)data_len;
}

static int
ms_pro_im_read_property(struct ratbag_device *device, uint8_t prop,
			const uint8_t *req_data, uint8_t req_len,
			uint8_t *out, size_t out_len)
{
	int rc;

	rc = ms_pro_im_send_request(device, prop, req_data, req_len);
	if (rc < 0)
		return rc;

	return ms_pro_im_read_response(device, out, out_len);
}

/* ---- property accessors ---------------------------------------------- */

static int
ms_pro_im_get_dpi(struct ratbag_device *device, unsigned int *dpi_out)
{
	uint8_t raw[2];
	int rc;

	rc = ms_pro_im_read_property(device, MS_PRO_IM_PROP_DPI_READ, NULL, 0,
				     raw, sizeof(raw));
	if (rc < (int)sizeof(raw))
		return rc < 0 ? rc : -EIO;

	*dpi_out = raw[0] | (raw[1] << 8);

	return 0;
}

static int
ms_pro_im_set_dpi(struct ratbag_device *device, unsigned int dpi)
{
	uint8_t raw[2];

	if (dpi < MS_PRO_IM_DPI_MIN || dpi > MS_PRO_IM_DPI_MAX)
		return -EINVAL;

	raw[0] = dpi & 0xff;
	raw[1] = (dpi >> 8) & 0xff;

	return ms_pro_im_send_request(device, MS_PRO_IM_PROP_DPI_WRITE, raw, sizeof(raw));
}

static int
ms_pro_im_get_color(struct ratbag_device *device, struct ratbag_color *color_out)
{
	uint8_t raw[3];
	int rc;

	rc = ms_pro_im_read_property(device, MS_PRO_IM_PROP_COLOR_READ, NULL, 0,
				     raw, sizeof(raw));
	if (rc < (int)sizeof(raw))
		return rc < 0 ? rc : -EIO;

	color_out->red = raw[0];
	color_out->green = raw[1];
	color_out->blue = raw[2];

	return 0;
}

static int
ms_pro_im_set_color(struct ratbag_device *device, struct ratbag_color color)
{
	uint8_t raw[3];

	raw[0] = color.red & 0xff;
	raw[1] = color.green & 0xff;
	raw[2] = color.blue & 0xff;

	return ms_pro_im_send_request(device, MS_PRO_IM_PROP_COLOR_WRITE, raw, sizeof(raw));
}

static int
ms_pro_im_get_report_rate(struct ratbag_device *device, unsigned int *hz_out)
{
	struct ms_pro_im_polling_mapping *m;
	uint8_t raw;
	int rc;

	rc = ms_pro_im_read_property(device, MS_PRO_IM_PROP_POLLING_READ, NULL, 0,
				     &raw, sizeof(raw));
	if (rc < (int)sizeof(raw))
		return rc < 0 ? rc : -EIO;

	ARRAY_FOR_EACH(ms_pro_im_polling_map, m) {
		if (m->raw == raw) {
			*hz_out = m->hz;
			return 0;
		}
	}

	return -EIO;
}

static int
ms_pro_im_set_report_rate(struct ratbag_device *device, unsigned int hz)
{
	struct ms_pro_im_polling_mapping *m;

	ARRAY_FOR_EACH(ms_pro_im_polling_map, m) {
		if (m->hz == hz)
			return ms_pro_im_send_request(device,
						      MS_PRO_IM_PROP_POLLING_WRITE,
						      &m->raw, 1);
	}

	return -EINVAL;
}

static int
ms_pro_im_get_button_action(struct ratbag_device *device, uint8_t button_id,
			    const struct ratbag_button_action **action_out)
{
	uint8_t raw[5];
	uint32_t code;
	int rc;

	rc = ms_pro_im_read_property(device, MS_PRO_IM_PROP_BUTTON_READ,
				     &button_id, 1, raw, sizeof(raw));
	if (rc < (int)sizeof(raw))
		return rc < 0 ? rc : -EIO;

	code = ((uint32_t)raw[1] << 24) | ((uint32_t)raw[2] << 16) |
	       ((uint32_t)raw[3] << 8) | (uint32_t)raw[4];

	*action_out = ms_pro_im_raw_to_button_action(code);

	return 0;
}

static int
ms_pro_im_set_button_action(struct ratbag_device *device, uint8_t button_id,
			    const struct ratbag_button_action *action)
{
	uint32_t code = ms_pro_im_button_action_to_raw(action);
	uint8_t raw[5];

	raw[0] = button_id;
	raw[1] = (code >> 24) & 0xff;
	raw[2] = (code >> 16) & 0xff;
	raw[3] = (code >> 8) & 0xff;
	raw[4] = code & 0xff;

	return ms_pro_im_send_request(device, MS_PRO_IM_PROP_BUTTON_WRITE, raw, sizeof(raw));
}

/* ---- ratbag driver interface ------------------------------------------ */

static int
ms_pro_im_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, MS_PRO_IM_READ_REPORT_ID);
}

static int
ms_pro_im_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	unsigned int rates[] = { 125, 500, 1000 };
	int rc;

	rc = ratbag_find_hidraw(device, ms_pro_im_test_hidraw);
	if (rc)
		return rc;

	/* No on-board profile storage: everything takes effect immediately,
	 * so we expose a single profile. */
	ratbag_device_init_profiles(device, 1,
				    MS_PRO_IM_NUM_RESOLUTIONS,
				    MS_PRO_IM_NUM_BUTTONS,
				    MS_PRO_IM_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		unsigned int hz = 0;
		unsigned int dpi = 0;
		struct ratbag_color color = { 0, 0, 0 };

		profile->is_active = true;

		rc = ms_pro_im_get_report_rate(device, &hz);
		if (rc < 0) {
			log_error(device->ratbag,
				 "Could not read report rate: %s (%d)\n",
				 strerror(-rc), rc);
			return rc;
		}
		ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));
		profile->hz = hz;

		rc = ms_pro_im_get_color(device, &color);
		if (rc < 0) {
			log_error(device->ratbag,
				 "Could not read LED color: %s (%d)\n",
				 strerror(-rc), rc);
			return rc;
		}

		ratbag_profile_for_each_resolution(profile, resolution) {
			rc = ms_pro_im_get_dpi(device, &dpi);
			if (rc < 0) {
				log_error(device->ratbag,
					 "Could not read DPI: %s (%d)\n",
					 strerror(-rc), rc);
				return rc;
			}

			ratbag_resolution_set_dpi_list_from_range(resolution,
								  MS_PRO_IM_DPI_MIN,
								  MS_PRO_IM_DPI_MAX);
			ratbag_resolution_set_resolution(resolution, dpi, dpi);
			resolution->is_active = true;
			resolution->is_default = true;
		}

		ratbag_profile_for_each_led(profile, led) {
			led->mode = RATBAG_LED_ON;
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			led->color = color;
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		}

		ratbag_profile_for_each_button(profile, button) {
			const struct ratbag_button_action *action = NULL;
			uint8_t button_id;

			switch (button->index) {
			case 0:
				ratbag_button_enable_action_type(button,
								 RATBAG_BUTTON_ACTION_TYPE_BUTTON);
				ratbag_button_set_action(button, &ms_pro_im_left_click);
				continue;
			case 1:
				ratbag_button_enable_action_type(button,
								 RATBAG_BUTTON_ACTION_TYPE_BUTTON);
				ratbag_button_set_action(button, &ms_pro_im_right_click);
				continue;
			case 2:
				button_id = MS_PRO_IM_BUTTON_ID_MIDDLE;
				break;
			case 3:
				button_id = MS_PRO_IM_BUTTON_ID_BACK;
				break;
			default:
				continue;
			}

			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

			rc = ms_pro_im_get_button_action(device, button_id, &action);
			if (rc < 0) {
				log_error(device->ratbag,
					 "Could not read mapping for button %d: %s (%d)\n",
					 button->index, strerror(-rc), rc);
				continue;
			}

			if (action)
				ratbag_button_set_action(button, action);
			else
				log_debug(device->ratbag,
					 "Button %d has an unrecognised action code, leaving it unset\n",
					 button->index);
		}
	}

	return 0;
}

static int
ms_pro_im_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dirty)
			continue;

		rc = ms_pro_im_set_dpi(device, resolution->dpi_x);
		if (rc < 0) {
			log_error(device->ratbag,
				 "Could not write DPI: %s (%d)\n",
				 strerror(-rc), rc);
			return rc;
		}
	}

	if (profile->rate_dirty) {
		rc = ms_pro_im_set_report_rate(device, profile->hz);
		if (rc < 0) {
			log_error(device->ratbag,
				 "Could not write report rate: %s (%d)\n",
				 strerror(-rc), rc);
			return rc;
		}
	}

	ratbag_profile_for_each_led(profile, led) {
		if (!led->dirty)
			continue;

		rc = ms_pro_im_set_color(device, led->color);
		if (rc < 0) {
			log_error(device->ratbag,
				 "Could not write LED color: %s (%d)\n",
				 strerror(-rc), rc);
			return rc;
		}
	}

	ratbag_profile_for_each_button(profile, button) {
		uint8_t button_id;

		if (!button->dirty)
			continue;

		switch (button->index) {
		case 2:
			button_id = MS_PRO_IM_BUTTON_ID_MIDDLE;
			break;
		case 3:
			button_id = MS_PRO_IM_BUTTON_ID_BACK;
			break;
		default:
			log_error(device->ratbag,
				 "Button %d cannot be remapped on this device, ignoring\n",
				 button->index);
			continue;
		}

		rc = ms_pro_im_set_button_action(device, button_id, &button->action);
		if (rc < 0) {
			log_error(device->ratbag,
				 "Could not write mapping for button %d: %s (%d)\n",
				 button->index, strerror(-rc), rc);
			return rc;
		}
	}

	return 0;
}

static int
ms_pro_im_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		rc = ms_pro_im_write_profile(profile);
		if (rc)
			return rc;
	}

	return 0;
}

static void
ms_pro_im_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

struct ratbag_driver microsoft_driver = {
	.name = "Microsoft Pro IntelliMouse",
	.id = "microsoft_pro_intellimouse",
	.probe = ms_pro_im_probe,
	.remove = ms_pro_im_remove,
	.commit = ms_pro_im_commit,
};
