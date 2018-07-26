/*
 * Copyright © 2018 Stephen Dawkins
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
#include "libratbag-data.h"

#define CMSTORM_NUM_BUTTONS		9
#define CMSTORM_NUM_LEDS		3
#define CMSTORM_NUM_PROFILES		4
#define CMSTORM_NUM_DPI			4

#define CMSTORM_REPORT_ID		0x03

#define CMSTORM_REPORT_SIZE		64
#define CMSTORM_BLOCK_SIZE		56
#define CMSTORM_PROFILE_SIZE		0x69

#define CMSTORM_CMD_SELECT_PROFILE	0xc1
#define CMSTORM_CMD_CURRENT_PROFILE	0xc2
#define CMSTORM_CMD_RELOAD_PROFILES	0xc5
#define CMSTORM_CMD_WRITE_BLOCK		0xd1
#define CMSTORM_CMD_READ_BLOCK		0xd2
#define CMSTORM_CMD_ENABLE_PROG		0xea
#define CMSTORM_CMD_DISABLE_PROG	0xed

#define CMSTORM_BUTTON_TYPE_MOUSE	0x00
#define CMSTORM_BUTTON_TYPE_KEYBOARD	0x02
#define CMSTORM_BUTTON_TYPE_MACRO	0x03
#define CMSTORM_BUTTON_TYPE_PROFILE	0x08
#define CMSTORM_BUTTON_TYPE_DPI		0x09
#define CMSTORM_BUTTON_TYPE_CONSUMER	0x20

#define CMSTORM_MACRO_TYPE_END		0x00
#define CMSTORM_MACRO_TYPE_MOUSE	0x01
#define CMSTORM_MACRO_TYPE_KEYBOARD	0x03


#pragma pack(push,1)
struct cmstorm_button
{
	uint8_t repetitions;
	uint8_t response_time;
	uint8_t type;
	uint16_t function;
};

struct cmstorm_dpi
{
	uint8_t enabled;
	uint8_t dpi;
	uint8_t lod;
	uint8_t usb_rate;
};

struct cmstorm_led
{
	uint8_t mode;
	uint8_t red, green, blue;
	uint8_t intensity;
};

struct cmstorm_macro
{
	uint8_t type;
	uint8_t unknown; //part of button?
	uint8_t button;
	uint8_t pressed_released;
	uint8_t unknown2;
	uint16_t delay;
};

struct cmstorm_profile
{
	uint8_t index;
	uint8_t name[24]; //UTF-16BE
	uint8_t length;
	uint16_t macro_length;
	struct cmstorm_button buttons[CMSTORM_NUM_BUTTONS];
	struct cmstorm_dpi dpi[CMSTORM_NUM_DPI];
	struct cmstorm_led leds[CMSTORM_NUM_LEDS];
	struct cmstorm_macro *macros;
};
#pragma pack(pop)

#define DPI_COUNT 4
static uint32_t DPIS[] = {800, 1600, 3200, 4000};

static
enum ratbag_button_type button_types[9] = {
	RATBAG_BUTTON_TYPE_LEFT,
	RATBAG_BUTTON_TYPE_RIGHT,
	RATBAG_BUTTON_TYPE_MIDDLE,
	RATBAG_BUTTON_TYPE_RESOLUTION_UP,
	RATBAG_BUTTON_TYPE_RESOLUTION_DOWN,
	RATBAG_BUTTON_TYPE_THUMB,
	RATBAG_BUTTON_TYPE_THUMB2,
	RATBAG_BUTTON_TYPE_PINKIE,
	RATBAG_BUTTON_TYPE_PINKIE2
};

static
enum ratbag_led_type led_types[3] = {
	RATBAG_LED_TYPE_WHEEL,
	RATBAG_LED_TYPE_DPI,
	RATBAG_LED_TYPE_LOGO
};

static uint32_t
dpiIndexToDpi(uint8_t dpiIndex)
{
	if (dpiIndex < DPI_COUNT)
	{
		return DPIS[dpiIndex];
	}
	return DPIS[0];
}

static uint8_t
dpiToDpiIndex(uint32_t dpi)
{
	for (uint8_t i = 0; i < DPI_COUNT; i++)
	{
		if (DPIS[i] == dpi)
		{
			return i;
		}
	}
	return 0;
}

static int
send_command(struct ratbag_device *device, int cmd_length, uint8_t *cmd, int out_length, uint8_t *out)
{
	int ret = ratbag_hidraw_raw_request(device, CMSTORM_REPORT_ID, cmd, cmd_length, HID_FEATURE_REPORT, HID_REQ_SET_REPORT);

	if (ret < 0)
		return ret;

	msleep(100);

	ret = ratbag_hidraw_raw_request(device, CMSTORM_REPORT_ID, out, out_length, HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (ret < 0) {
		return ret;
	}

	return 0;
}


static int
reload_profiles(struct ratbag_device *device)
{
	uint8_t cmd[7];
	uint8_t chunk[8];
	cmd[1] = CMSTORM_CMD_RELOAD_PROFILES;
	cmd[2] = 5;
	cmd[3] = 0;
	cmd[4] = 0;
	cmd[5] = 0;
	cmd[6] = 0;

	send_command(device, 7, cmd, 8, chunk);

	//not entirely sure what this is needed for
	//the 5d 0f is just a timestamp
	cmd[1] = 0xc6;
	cmd[2] = 0x5d;
	cmd[3] = 0x0f;
	return send_command(device, 7, cmd, 8, chunk);

}

static int
enable_profile_writing(struct ratbag_device *device)
{
	uint8_t cmd[7];
	uint8_t chunk[8];
	cmd[1] = CMSTORM_CMD_ENABLE_PROG;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;
	cmd[5] = 0;
	cmd[6] = 0;

	return send_command(device, 7, cmd, 8, chunk);
}

static int
disable_profile_writing(struct ratbag_device *device)
{
	uint8_t cmd[7];
	uint8_t chunk[8];
	cmd[1] = CMSTORM_CMD_DISABLE_PROG;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;
	cmd[5] = 0;
	cmd[6] = 0;

	return send_command(device, 7, cmd, 8, chunk);
}

static int
set_selected_profile(struct ratbag_device *device, int profile_idx)
{
	uint8_t cmd[7];
	uint8_t chunk[8];
	cmd[1] = CMSTORM_CMD_SELECT_PROFILE;
	cmd[2] = profile_idx;
	cmd[3] = 0;
	cmd[4] = 0;
	cmd[5] = 0;
	cmd[6] = 0;

	return send_command(device, 7, cmd, 8, chunk);
}

static int
get_selected_profile(struct ratbag_device *device, unsigned int *profile_idx)
{
	uint8_t cmd[7];
	uint8_t chunk[8];
	cmd[1] = CMSTORM_CMD_CURRENT_PROFILE;
	cmd[2] = 0;
	cmd[3] = 0;
	cmd[4] = 0;
	cmd[5] = 0;
	cmd[6] = 0;

	int rc = send_command(device, 7, cmd, 8, chunk);
	if (rc)
		return rc;

	*profile_idx = chunk[2];

	return 0;
}

static int
read_chunk(struct ratbag_device *device, int profile, int offset, int length, uint8_t *out)
{
	//request the chunk
	uint8_t cmd[7];
	uint8_t chunk[length+7];

	cmd[1] = CMSTORM_CMD_READ_BLOCK;
	cmd[2] = profile;
	cmd[3] = (offset >> 8) & 0xff;
	cmd[4] = offset & 0xff;
	cmd[5] = length;
	cmd[6] = 0;

	int ret = send_command(device, 7, cmd, length+7, chunk);
	if (ret < 0)
		return ret;

	memcpy(out, chunk+7, length);

	return 0;
}

static int
write_chunk(struct ratbag_device *device, int profile, int offset, int length, uint8_t *out)
{
	//request the chunk
	uint8_t cmd[7+length];
	uint8_t chunk[8];
	cmd[1] = CMSTORM_CMD_WRITE_BLOCK;
	cmd[2] = profile;
	cmd[3] = (offset >> 8) & 0xff;
	cmd[4] = offset & 0xff;
	cmd[5] = length;
	cmd[6] = 0;

	memcpy(cmd+7, out, length);

	int ret = send_command(device, 7+length, cmd, 8, chunk);
	if (ret < 0)
		return ret;

	return 0;
}

static int
read_chunks(struct ratbag_device *device, int profile, int offset, int length, uint8_t *out)
{
	int ret;

	do
	{
		ret = read_chunk(device, profile, offset, min(length, CMSTORM_BLOCK_SIZE), out);
		if (ret < 0) {
			return ret;
		}
		offset += CMSTORM_BLOCK_SIZE;
		length -= CMSTORM_BLOCK_SIZE;
		out += CMSTORM_BLOCK_SIZE;
	}
	while (ret == 0 && length > 0);

	return 0;
}

static int
write_chunks(struct ratbag_device *device, int profile, int offset, int length, uint8_t *buf)
{
	int ret;

	do
	{
		ret = write_chunk(device, profile, offset, min(length, CMSTORM_BLOCK_SIZE), buf);
		if (ret < 0)
		{
			return ret;
		}
		offset += CMSTORM_BLOCK_SIZE;
		length -= CMSTORM_BLOCK_SIZE;
		buf += CMSTORM_BLOCK_SIZE;
	}
	while (ret == 0 && length > 0);

	return 0;
}


static int
read_profile(struct ratbag_device *device, int profile, struct cmstorm_profile *out)
{
	int ret = read_chunks(device, profile, 0, CMSTORM_PROFILE_SIZE, (uint8_t*) out);
	if (ret < 0)
		return ret;

	//fixup the 16-bit integers
	out->macro_length = be16toh(out->macro_length);
	for (int i = 0; i < CMSTORM_NUM_BUTTONS; i++)
	{
		out->buttons[i].function = be16toh(out->buttons[i].function);
	}

	//load the macros if there are any
	if (out->macro_length > 0) {
		out->macros = (struct cmstorm_macro*) zalloc(out->macro_length);

		read_chunks(device, profile, 0x100, out->macro_length, (uint8_t*) out->macros);

		for (uint16_t i = 0; i < out->macro_length / sizeof(struct cmstorm_macro); i++) {
			out->macros[i].delay = be16toh(out->macros[i].delay);
		}
	}
	return 0;
}

static int
write_profile(struct ratbag_device *device, int profile, struct cmstorm_profile *out)
{
	//fixup the 16-bit integers
	int remaining_length = out->macro_length;
	out->macro_length = htobe16(out->macro_length);
	for (int i = 0; i < CMSTORM_NUM_BUTTONS; i++) {
		out->buttons[i].function = htobe16(out->buttons[i].function);
	}

	if (remaining_length > 0) {
		for (uint16_t i = 0; i < out->macro_length / sizeof(struct cmstorm_macro); i++) {
			out->macros[i].delay = htobe16(out->macros[i].delay);
		}
	}

	int ret = write_chunks(device, profile, 0, CMSTORM_PROFILE_SIZE, (uint8_t*) out);
	if (ret < 0)
		return ret;

	//load the macros if there are any
	if (remaining_length > 0) {
		write_chunks(device, profile, 0x100, remaining_length, (uint8_t*) out->macros);
	}
	return 0;
}

static int
cmstorm_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, CMSTORM_REPORT_ID);
}

static int
cmstorm_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	int rc;
	_cleanup_(dpi_list_freep) struct dpi_list *dpilist = NULL;
	_cleanup_(freep) struct dpi_range *dpirange = NULL;
	unsigned int selected_profile;
	struct cmstorm_profile cprofile;

	unsigned int report_rates[] = { 125, 250, 500, 1000 };

	rc = ratbag_find_hidraw(device, cmstorm_test_hidraw);
	if (rc)
		return rc;

	ratbag_device_init_profiles(device,
					CMSTORM_NUM_PROFILES,
					CMSTORM_NUM_DPI,
					CMSTORM_NUM_BUTTONS,
					CMSTORM_NUM_LEDS);

	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_KEY);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_MACROS);

	dpilist = ratbag_device_data_cmstorm_get_dpi_list(device->data);

	get_selected_profile(device, &selected_profile);

	ratbag_device_for_each_profile(device, profile) {
		int ret = read_profile(device, profile->index+1, &cprofile);
		if (ret < 0)
			return ret;

		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITABLE_NAME);

		ratbag_utf8_from_enc((char*) cprofile.name, 24, "UTF-16BE", &profile->name);
		profile->is_enabled = true;

		if (profile->index+1 == selected_profile)
			profile->is_active = true;

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_INDIVIDUAL_REPORT_RATE);

			resolution->is_active = true;
			if (resolution->index == 0) {
				resolution->is_active = true;
				resolution->is_default = true;
			}

			if (dpilist)
			{
				ratbag_resolution_set_dpi_list(resolution,
							       (unsigned int *)dpilist->entries,
							       dpilist->nentries);
			}

			int rate = cprofile.dpi[resolution->index].usb_rate;

			ratbag_resolution_set_report_rate_list(resolution, report_rates,
							       ARRAY_LENGTH(report_rates));

			ratbag_resolution_set_dpi(resolution, dpiIndexToDpi(cprofile.dpi[resolution->index].dpi));
			ratbag_resolution_set_report_rate(resolution, 1000 / (rate == 0 ? 1 : rate));

			//store the lod in the userdata, double cast to prevent compiler complaining
			resolution->userdata = (void*) (size_t) cprofile.dpi[resolution->index].lod;
		}

		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

			button->type = button_types[button->index];

			int type = cprofile.buttons[button->index].type;
			//Mouse Buttons
			if (type == CMSTORM_BUTTON_TYPE_MOUSE) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = __builtin_ctz(cprofile.buttons[button->index].function) + 1;
			}
			//Profile Buttons
			else if (type == CMSTORM_BUTTON_TYPE_PROFILE) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
				button->action.action.special = cprofile.buttons[button->index].function == 0x1 ? RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP : RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN;
			}
			//DPI Buttons
			else if (type == CMSTORM_BUTTON_TYPE_DPI) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
				button->action.action.special = cprofile.buttons[button->index].function == 0x1 ? RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP : RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN;
			}
			//Keyboard
			else if (type == CMSTORM_BUTTON_TYPE_KEYBOARD) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
				struct ratbag_button_macro *macro = ratbag_button_macro_new(NULL);
				unsigned int event_data = ratbag_hidraw_get_keycode_from_keyboard_usage(device, cprofile.buttons[button->index].function);

				ratbag_button_macro_set_event(macro, 0, RATBAG_MACRO_EVENT_KEY_PRESSED, event_data);
				ratbag_button_macro_set_event(macro, 1, RATBAG_MACRO_EVENT_KEY_RELEASED, event_data);

				ratbag_button_copy_macro(button, macro);
				ratbag_button_macro_unref(macro);
			}
			//Consumer keys
			else if (type == CMSTORM_BUTTON_TYPE_CONSUMER) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
				struct ratbag_button_macro *macro = ratbag_button_macro_new(NULL);
				unsigned int event_data = ratbag_hidraw_get_keycode_from_consumer_usage(device, cprofile.buttons[button->index].function);

				ratbag_button_macro_set_event(macro, 0, RATBAG_MACRO_EVENT_KEY_PRESSED, event_data);
				ratbag_button_macro_set_event(macro, 1, RATBAG_MACRO_EVENT_KEY_RELEASED, event_data);

				ratbag_button_copy_macro(button, macro);
				ratbag_button_macro_unref(macro);
			}
			//Macro
			else if (type == CMSTORM_BUTTON_TYPE_MACRO) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
				struct ratbag_button_macro *macro = ratbag_button_macro_new(NULL);

				if (cprofile.macros != NULL) {
					int i = (cprofile.buttons[button->index].function - 0x100) / 7;
					int idx = 0;

					while (cprofile.macros[i].type != CMSTORM_MACRO_TYPE_END) {
						if (cprofile.macros[i].type != CMSTORM_MACRO_TYPE_KEYBOARD) {
							log_error(device->ratbag, "Unsupported mouse entry in macro\n");
							i++;
							continue;
						}

						enum ratbag_macro_event_type event_type = cprofile.macros[i].pressed_released == 0 ? RATBAG_MACRO_EVENT_KEY_PRESSED : RATBAG_MACRO_EVENT_KEY_RELEASED;
						unsigned int event_data = ratbag_hidraw_get_keycode_from_keyboard_usage(device, cprofile.macros[i].button);

						ratbag_button_macro_set_event(macro, idx++, event_type, event_data);

						uint16_t delay = cprofile.macros[i].delay;
						if (delay > 0) {
							ratbag_button_macro_set_event(macro, idx++, RATBAG_MACRO_EVENT_WAIT, delay);
						}
						i++;
					}
				}

				ratbag_button_copy_macro(button, macro);
				ratbag_button_macro_unref(macro);
			}
			else
			{
				log_error(device->ratbag, "Unknown button type: %X\n", type);
			}
		}

		ratbag_profile_for_each_led(profile, led) {
			led->type = led_types[led->index];

			//TODO support Flash on profile change and rapid fire...?
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			led->color.red = cprofile.leds[led->index].red << 1;
			led->color.green = cprofile.leds[led->index].green << 1;
			led->color.blue = cprofile.leds[led->index].blue << 1;

			int is_zero = led->color.red == 0 &&
						  led->color.green == 0 &&
						  led->color.blue == 0;

			led->mode = is_zero ? RATBAG_LED_OFF : RATBAG_LED_ON;
			led->brightness = 255;

			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		}

		profile->dirty = 0;

		if (cprofile.macros != NULL) {
			free(cprofile.macros);
			cprofile.macros = NULL;
		}
	}

	return 0;
}

static int
cmstorm_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;

	struct cmstorm_profile cprofile = {0};
	struct cmstorm_macro macros[512] = {0};
	int macroIdx = -1;

	cprofile.length = CMSTORM_PROFILE_SIZE;
	cprofile.macro_length = 0;
	cprofile.macros = NULL;

	ratbag_profile_for_each_resolution(profile, resolution) {
		cprofile.dpi[resolution->index].enabled = 0x80;
		//double cast to prevent compiler from complaining
		cprofile.dpi[resolution->index].lod = (uint8_t) (size_t) ratbag_resolution_get_user_data(resolution);
		cprofile.dpi[resolution->index].dpi = dpiToDpiIndex(ratbag_resolution_get_dpi(resolution));
		cprofile.dpi[resolution->index].usb_rate = 1000 / ratbag_resolution_get_report_rate(resolution);
	}

	ratbag_profile_for_each_button(profile, button) {
		cprofile.buttons[button->index].function = 0;
		cprofile.buttons[button->index].repetitions = 1;
		cprofile.buttons[button->index].response_time = 5; //250µs

		if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_BUTTON) {
			cprofile.buttons[button->index].type = CMSTORM_BUTTON_TYPE_MOUSE;
			cprofile.buttons[button->index].function = 1 << (button->action.action.button-1);
		}
		else if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_SPECIAL) {
			//Profile Buttons
			if (button->action.action.special == RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP ||
				button->action.action.special == RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) {
				cprofile.buttons[button->index].type = CMSTORM_BUTTON_TYPE_PROFILE;
				cprofile.buttons[button->index].function = button->action.action.special == RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP ? 1 : 2;
			}
			//DPI Buttons
			else if (button->action.action.special == RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP ||
					 button->action.action.special == RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) {
				cprofile.buttons[button->index].type = CMSTORM_BUTTON_TYPE_DPI;
				cprofile.buttons[button->index].function = button->action.action.special == RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP ? 1 : 2;
			}
		}
		else if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
			unsigned int keycode, modifiers;
			int ret = ratbag_action_keycode_from_macro(&button->action, &keycode, &modifiers);

			//TODO check modifiers is empty

			if (ret > 0) {
				int keyboard_usage = ratbag_hidraw_get_keyboard_usage_from_keycode(profile->device, keycode);

				if (keyboard_usage != 0) {
					cprofile.buttons[button->index].type = CMSTORM_BUTTON_TYPE_KEYBOARD;
					cprofile.buttons[button->index].function = keyboard_usage;
				}
				else {
					cprofile.buttons[button->index].type = CMSTORM_BUTTON_TYPE_CONSUMER;
					cprofile.buttons[button->index].function = ratbag_hidraw_get_consumer_usage_from_keycode(profile->device, keycode);
				}
			}
			else {
				int events = ratbag_button_macro_get_num_events(ratbag_button_get_macro(button));
				if (events > 0) {
					cprofile.buttons[button->index].type = CMSTORM_BUTTON_TYPE_MACRO;
					cprofile.buttons[button->index].function = (macroIdx+1) * 7;
					for (int i = 0; i < events; i++) {
						struct ratbag_macro_event *event = &button->action.macro->events[i];

						//skip over initial waits, we don't support them
						if (macroIdx == 0 && event->type == RATBAG_MACRO_EVENT_WAIT) {
							continue;
						}

						switch (event->type) {
							case RATBAG_MACRO_EVENT_KEY_PRESSED:
							case RATBAG_MACRO_EVENT_KEY_RELEASED:
								macroIdx++;
								macros[macroIdx].type = CMSTORM_MACRO_TYPE_KEYBOARD;
								macros[macroIdx].unknown = 0;
								macros[macroIdx].button = ratbag_hidraw_get_keyboard_usage_from_keycode(profile->device, event->event.key);
								macros[macroIdx].pressed_released = (event->type == RATBAG_MACRO_EVENT_KEY_PRESSED ? 0 : 1);
								macros[macroIdx].unknown2 = 0;
								macros[macroIdx].delay = 0; //we set the delay as part of the wait event
								break;
							case RATBAG_MACRO_EVENT_WAIT:
								macros[macroIdx].delay += event->event.timeout;
								break;
							case RATBAG_MACRO_EVENT_INVALID:
							case RATBAG_MACRO_EVENT_NONE:
								macros[macroIdx].type = 0x04; //TODO does this actually do nothing?
								break;
						}
					}
					//flip the order of the bytes
					macroIdx++;
					//add the end event
					macros[macroIdx].type = CMSTORM_MACRO_TYPE_END;
					macros[macroIdx].unknown = 0;
					macros[macroIdx].button = 0;
					macros[macroIdx].pressed_released = 0;
					macros[macroIdx].unknown2 = 0;
					macros[macroIdx].delay = 0;
				}
			}
		}
	}

	ratbag_profile_for_each_led(profile, led) {
		int is_off = (led->mode == RATBAG_LED_OFF);
		cprofile.leds[led->index].mode  = is_off ? 0 : 1; //continuous mode only
		cprofile.leds[led->index].red   = is_off ? 0 : led->color.red >> 1;
		cprofile.leds[led->index].green = is_off ? 0 : led->color.green >> 1;
		cprofile.leds[led->index].blue  = is_off ? 0 : led->color.blue >> 1;
		cprofile.leds[led->index].intensity = is_off ? 0 : 64;
	}

	cprofile.macro_length = (macroIdx+1) * 7;
	if (cprofile.macro_length > 0) {
		cprofile.macros = macros;
	}

	return write_profile(profile->device, profile->index+1, &cprofile);
}

static int
cmstorm_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc = 0;
	int active_profile = 1; //default to the first profile

	enable_profile_writing(device);

	list_for_each(profile, &device->profiles, link) {
		if (profile->is_active)
			active_profile = profile->index+1;

		if (!profile->dirty)
			continue;

		log_debug(device->ratbag,
			  "Profile %d changed, rewriting\n", profile->index);

		rc = cmstorm_write_profile(profile);
		if (rc)
		{
			disable_profile_writing(device);
			return rc;
		}
	}
	set_selected_profile(device, active_profile);
	reload_profiles(device);
	disable_profile_writing(device);

	return 0;
}

static void
cmstorm_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

static int
cmstorm_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	set_selected_profile(device, index+1);
	return 0;
}

struct ratbag_driver cmstorm_driver = {
	.name = "CMStorm",
	.id = "cmstorm",
	.probe = cmstorm_probe,
	.remove = cmstorm_remove,
	.commit = cmstorm_commit,
	.set_active_profile = cmstorm_set_active_profile,	
};
