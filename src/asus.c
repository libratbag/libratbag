/*
 * Copyright (C) 2021 Kyoken, kyoken@kyoken.ninja
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

#include "asus.h"

#include <assert.h>

#include "libratbag-data.h"
#include "libratbag-private.h"

/* ASUS commands */
#define ASUS_CMD_GET_LED_DATA		0x0312  /* get all LEDs */
#define ASUS_CMD_GET_SETTINGS		0x0412  /* dpi, rate, button response, angle snapping */
#define ASUS_CMD_GET_BUTTON_DATA	0x0512  /* get all buttons */
#define ASUS_CMD_GET_PROFILE_DATA	0x0012  /* get current profile info */
#define ASUS_CMD_SET_LED		0x2851  /* set single led */
#define ASUS_CMD_SET_SETTING		0x3151  /* dpi / rate / button response / angle snapping */
#define ASUS_CMD_SET_BUTTON		0x2151  /* set single button */
#define ASUS_CMD_SET_PROFILE		0x0250  /* switch profile */
#define ASUS_CMD_SAVE			0x0350  /* save settings */

/* fields order in _asus_dpiX_data, used for setting with ASUS_CMD_SET_SETTING */
#define ASUS_FIELD_RATE	0
#define ASUS_FIELD_RESPONSE	1
#define ASUS_FIELD_SNAPPING	2

/* key mapping, the index is actual ASUS code */
static const unsigned char ASUS_KEY_MAPPING[] = {
/* 00 */	0,		0,		0,		0,
/* 04 */	KEY_A,		KEY_B,		KEY_C,		KEY_D,
/* 08 */	KEY_E,		KEY_F,		KEY_G,		KEY_H,
/* 0C */	KEY_I,		KEY_J,		KEY_K,		KEY_L,
/* 0E */	KEY_M,		KEY_N,		KEY_O,		KEY_P,
/* 14 */	KEY_Q,		KEY_R,		KEY_S,		KEY_T,
/* 18 */	KEY_U,		KEY_V,		KEY_W,		KEY_X,
/* 1C */	KEY_Y,		KEY_Z,		KEY_1,		KEY_2,
/* 1E */	KEY_3,		KEY_4,		KEY_5,		KEY_6,
/* 24 */	KEY_7,		KEY_8,		KEY_9,		KEY_0,
/* 28 */	KEY_ENTER,	KEY_ESC,	KEY_BACKSPACE,	KEY_TAB,
/* 2C */	KEY_SPACE,	KEY_MINUS,	KEY_KPPLUS,	0,
/* 2E */	0,		0,		0,		0,
/* 34 */	0,		KEY_GRAVE,	KEY_EQUAL,	0,
/* 38 */	KEY_SLASH,	0,		KEY_F1,		KEY_F2,
/* 3C */	KEY_F3,		KEY_F4,		KEY_F5,		KEY_F6,
/* 3E */	KEY_F7,		KEY_F8,		KEY_F9,		KEY_F10,
/* 44 */	KEY_F11,	KEY_F12,	0,		0,
/* 48 */	0,		0,		KEY_HOME,	KEY_PAGEUP,
/* 4C */	KEY_DELETE,	0,		KEY_PAGEDOWN,	KEY_RIGHT,
/* 4E */	KEY_LEFT,	KEY_DOWN,	KEY_UP,		0,
/* 54 */	0,		0,		0,		0,
/* 58 */	0,		KEY_KP1,	KEY_KP2,	KEY_KP3,
/* 5C */	KEY_KP4,	KEY_KP5,	KEY_KP6,	KEY_KP7,
/* 5E */	KEY_KP8,	KEY_KP9,	0,
};

static const uint8_t ASUS_JOYSTICK_CODES[] = { 0xd0, 0xd1, 0xd2, 0xd3, 0xd7, 0xd8, 0xda, 0xdb };
static const unsigned int ASUS_POLLING_RATES[] = { 125, 250, 500, 1000 };
static const unsigned int ASUS_DEBOUNCE_TIMES[] = { 4, 8, 12, 16, 20, 24, 28, 32 };

/* search for ASUS button by ratbag types */
const struct asus_button *
asus_find_button_by_action(struct ratbag_button_action action, bool is_joystick)
{
	const struct asus_button *asus_button;
	ARRAY_FOR_EACH(ASUS_BUTTON_MAPPING, asus_button) {
		if (is_joystick != asus_code_is_joystick(asus_button->asus_code))
			continue;
		if ((action.type == RATBAG_BUTTON_ACTION_TYPE_BUTTON && asus_button->button == action.action.button) ||
				(action.type == RATBAG_BUTTON_ACTION_TYPE_SPECIAL && asus_button->special == action.action.special))
			return asus_button;
	}
	return NULL;
}

/* search for ASUS button by ASUS button code */
const struct asus_button *
asus_find_button_by_code(uint8_t asus_code)
{
	for (unsigned int i = 0; i < ARRAY_LENGTH(ASUS_BUTTON_MAPPING); i++) {
		if (ASUS_BUTTON_MAPPING[i].asus_code == asus_code)
			return &ASUS_BUTTON_MAPPING[i];
	}
	return NULL;
}

/* search for ASUS key code by Linux key code */
int
asus_find_key_code(unsigned int linux_code)
{
	for (unsigned int i = 0; i < ARRAY_LENGTH(ASUS_KEY_MAPPING); i++) {
		if (ASUS_KEY_MAPPING[i] == linux_code)
			return i;
	}
	return -1;
}

bool
asus_code_is_joystick(uint8_t asus_code) {
	for (unsigned int i = 0; i < ARRAY_LENGTH(ASUS_JOYSTICK_CODES); i++) {
		if (ASUS_JOYSTICK_CODES[i] == asus_code)
			return true;
	}
	return false;
}

int
asus_get_linux_key_code(uint8_t asus_code) {
	if (asus_code > ARRAY_LENGTH(ASUS_KEY_MAPPING)) {
		return -1;
	}
	return ASUS_KEY_MAPPING[asus_code];
}

int
asus_query(struct ratbag_device *device,
		union asus_request *request, union asus_response *response)
{
	int rc;

	rc = ratbag_hidraw_output_report(device, request->raw, ASUS_PACKET_SIZE);
	if (rc < 0)
		return rc;

	memset(response, 0, sizeof(union asus_response));
	rc = ratbag_hidraw_read_input_report(device, response->raw, ASUS_PACKET_SIZE, NULL);
	if (rc < 0)
		return rc;

	/* invalid state, disconnected or sleeping */
	if (response->data.code == ASUS_STATUS_ERROR) {
		return ASUS_STATUS_ERROR;
	}

	return 0;
}

void
asus_setup_profile(struct ratbag_device *device, struct ratbag_profile *profile)
{
	ratbag_profile_set_report_rate_list(
		profile, ASUS_POLLING_RATES, ARRAY_LENGTH(ASUS_POLLING_RATES));
	ratbag_profile_set_debounce_list(
		 profile, ASUS_DEBOUNCE_TIMES, ARRAY_LENGTH(ASUS_DEBOUNCE_TIMES));
}

void
asus_setup_button(struct ratbag_device *device, struct ratbag_button *button)
{
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
}

void
asus_setup_resolution(struct ratbag_device *device, struct ratbag_resolution *resolution)
{
	const struct dpi_range *dpirange = ratbag_device_data_asus_get_dpi_range(device->data);
	if (!dpirange)
		return;

	ratbag_resolution_set_dpi_list_from_range(
		resolution, dpirange->min, dpirange->max);
}

void
asus_setup_led(struct ratbag_device *device, struct ratbag_led *led)
{
	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
	ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
}

int
asus_save_profile(struct ratbag_device *device)
{
	int rc;
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_SAVE,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

int
asus_get_profile_data(struct ratbag_device *device, struct asus_profile_data *data)
{
	int rc;
	uint32_t quirks = ratbag_device_data_asus_get_quirks(device->data);
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_GET_PROFILE_DATA,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	if (quirks & ASUS_QUIRK_STRIX_PROFILE)
		data->profile_id = response.data.results[7];
	else
		data->profile_id = response.data.results[8];

	if (response.data.results[9])
		data->dpi_preset = response.data.results[9] - 1;
	else
		data->dpi_preset = -1;

	data->version_primary_major = response.data.results[13];
	data->version_primary_minor = response.data.results[12];
	data->version_primary_build = response.data.results[11];

	data->version_secondary_major = response.data.results[4];
	data->version_secondary_minor = response.data.results[3];
	data->version_secondary_build = response.data.results[2];

	return 0;
}

int
asus_set_profile(struct ratbag_device *device, unsigned int index)
{
	int rc;
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_PROFILE,
		.data.params[0] = index,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

/* read button bindings */
int
asus_get_binding_data(struct ratbag_device *device, union asus_binding_data *data, unsigned int group)
{
	int rc;
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_GET_BUTTON_DATA,
		.data.params[0] = (uint8_t)group,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	memcpy(data->raw, response.raw, sizeof(data->raw));
	return 0;
}

/* set button binding using ASUS code of the button */
int
asus_set_button_action(struct ratbag_device *device, uint8_t asus_code_src,
		       uint8_t asus_code_dst, uint8_t asus_type)
{
	int rc;
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_BUTTON,
	};

	/* source (physical mouse button) */
	request.data.params[2] = asus_code_src;
	request.data.params[3] = ASUS_BUTTON_ACTION_TYPE_BUTTON;

	/* destination (mouse button or keyboard key action) */
	request.data.params[4] = asus_code_dst;
	request.data.params[5] = asus_type;

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

int
asus_get_resolution_data(struct ratbag_device *device, union asus_resolution_data *data, bool sep_xy_dpi)
{
	int rc;
	uint32_t quirks = ratbag_device_data_asus_get_quirks(device->data);
	union asus_response response;
	unsigned int dpi_count = ratbag_device_get_profile(device, 0)->num_resolutions;
	unsigned int i;
	union asus_request request = {
		.data.cmd = ASUS_CMD_GET_SETTINGS,
		.data.params[0] = sep_xy_dpi ? 2 : 0,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	memcpy(data->raw, response.raw, sizeof(data->raw));

	/* convert DPI rates */
	switch (dpi_count) {
	case 2:  /* 2 DPI presets */
		for (i = 0; i < dpi_count; i++) {
			data->data2.dpi[i] = data->data2.dpi[i] * 50 + 50;
			if (quirks & ASUS_QUIRK_DOUBLE_DPI)
				data->data2.dpi[i] *= 2;
		}
		data->data2.rate = ASUS_POLLING_RATES[data->data2.rate];
		data->data2.response = ASUS_DEBOUNCE_TIMES[data->data2.response];
		break;

	case 4:  /* 4 DPI presets */
		if (sep_xy_dpi) {  /* separate X & Y values */
			for (i = 0; i < dpi_count; i++) {
				data->data_xy.dpi[i].x = data->data_xy.dpi[i].x * 50 + 50;
				data->data_xy.dpi[i].y = data->data_xy.dpi[i].y * 50 + 50;
				if (quirks & ASUS_QUIRK_DOUBLE_DPI) {
					data->data_xy.dpi[i].x *= 2;
					data->data_xy.dpi[i].y *= 2;
				}
			}
		} else {
			for (i = 0; i < dpi_count; i++) {
				data->data4.dpi[i] = data->data4.dpi[i] * 50 + 50;
				if (quirks & ASUS_QUIRK_DOUBLE_DPI)
					data->data4.dpi[i] *= 2;
			}
			data->data4.rate = ASUS_POLLING_RATES[data->data4.rate];
			data->data4.response = ASUS_DEBOUNCE_TIMES[data->data4.response];
		}
		break;

	default:
		break;
	}

	return 0;
}

/* set DPI for the specified preset */
int
asus_set_dpi(struct ratbag_device *device, unsigned int index, unsigned int dpi)
{
	int rc;
	uint32_t quirks = ratbag_device_data_asus_get_quirks(device->data);
	union asus_response response;
	unsigned int idpi;

	idpi = dpi;
	if (quirks & ASUS_QUIRK_DOUBLE_DPI)
		idpi /= 2;

	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_SETTING,
		.data.params[0] = index,
		.data.params[2] = (idpi - 50) / 50,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

/* set polling rate in Hz */
int
asus_set_polling_rate(struct ratbag_device *device, unsigned int hz)
{
	int rc;
	union asus_response response;
	unsigned int dpi_count = ratbag_device_get_profile(device, 0)->num_resolutions;
	unsigned int i;

	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_SETTING,
		.data.params[0] = dpi_count + ASUS_FIELD_RATE,  /* field index to set */
	};

	for (i = 0; i < ARRAY_LENGTH(ASUS_POLLING_RATES); i++) {
		if (ASUS_POLLING_RATES[i] == hz) {
			request.data.params[2] = i;
			break;
		}
	}

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

/* set button response/debounce in ms (from 4 to 32 with step of 4) */
int
asus_set_button_response(struct ratbag_device *device, unsigned int ms)
{
	assert(ms >= 4);

	int rc;
	unsigned int dpi_count = ratbag_device_get_profile(device, 0)->num_resolutions;
	union asus_response response;
	unsigned int index = 0;
	for (unsigned int i = 0; i < ARRAY_LENGTH(ASUS_DEBOUNCE_TIMES); i++) {
		if (ASUS_DEBOUNCE_TIMES[i] == ms) {
			index = i;
			break;
		}
	}

	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_SETTING,
		.data.params[0] = dpi_count + ASUS_FIELD_RESPONSE,  /* field index to set */
		.data.params[2] = index,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

int
asus_set_angle_snapping(struct ratbag_device *device, bool is_enabled)
{
	int rc;
	union asus_response response;
	unsigned int dpi_count = ratbag_device_get_profile(device, 0)->num_resolutions;

	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_SETTING,
		.data.params[0] = dpi_count + ASUS_FIELD_SNAPPING,  /* field index to set */
		.data.params[2] = is_enabled ? 1 : 0,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}

int
asus_get_led_data(struct ratbag_device *device, union asus_led_data *data, unsigned int led)
{
	int rc;
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_GET_LED_DATA,
		.data.params[0] = led,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	memcpy(data->raw, response.raw, sizeof(data->raw));
	return 0;
}

/* set LED mode, brightness (0-4) and color */
int
asus_set_led(struct ratbag_device *device,
		uint8_t index, uint8_t mode, uint8_t brightness,
		struct ratbag_color color)
{
	int rc;
	union asus_response response;
	union asus_request request = {
		.data.cmd = ASUS_CMD_SET_LED,
		.data.params[0] = index,
		.data.params[2] = mode,
		.data.params[3] = brightness,
		.data.params[4] = color.red,
		.data.params[5] = color.green,
		.data.params[6] = color.blue,
	};

	rc = asus_query(device, &request, &response);
	if (rc)
		return rc;

	return 0;
}
