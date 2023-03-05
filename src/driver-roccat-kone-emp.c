/*
 * Copyright © 2021 Alexandre Laurent
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

/**
 * There is no elevation support
 * The LED effects is applied to the four LED of the mouse, but libratbag can set a different effect for each LED
 * The LED effects BLINKING and PULSING are not supported in libratbag
 * The maximum macro size is 480 in the mouse software. One event keeps the event data and the timing/delay
	- libratbag does not keep track of that number of events. It limits the mouse to 128 events
 * The mouse can repeat macro. Not supported in libratbag
 * In official soft, we can set a LED color to offset the cycle effect (only with predefined_led_colors). 
 *   Since predefined colors are not handled, we can't reproduce this effect.
 */

#include "config.h"
#include <assert.h>
#include <errno.h>
#include <libevdev/libevdev.h>
#include <linux/input.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "libratbag-enums.h"
#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "libratbag.h"
#include "shared-macro.h"

#define ROCCAT_PROFILE_MAX          5
#define ROCCAT_BUTTON_MAX           11 * 2 // (Easy Shift)
#define ROCCAT_NUM_DPI              5
#define ROCCAT_LED_MAX              4

#define ROCCAT_MAX_RETRY_READY      10

#define ROCCAT_REPORT_ID_CONFIGURE_PROFILE  4
#define ROCCAT_REPORT_ID_PROFILE            5
#define ROCCAT_REPORT_ID_SETTINGS           6
#define ROCCAT_REPORT_ID_KEY_MAPPING        7
#define ROCCAT_REPORT_ID_MACRO              8

#define ROCCAT_MAGIC_NUMBER_SETTINGS        0x29
#define ROCCAT_MAGIC_NUMBER_KEY_MAPPING     0x47

#define ROCCAT_BANK_ID_1    1
#define ROCCAT_BANK_ID_2    2
#define ROCCAT_REPORT_SIZE_MACRO_BANK       1026

#define ROCCAT_MACRO_GROUP_NAME_LENGTH  40
#define ROCCAT_MACRO_NAME_LENGTH        32

#define ROCCAT_CONFIG_SETTINGS      0x80 // (LED and mouse configuration)
#define ROCCAT_CONFIG_KEY_MAPPING   0x90 // (Buttons configuration)

#define ROCCAT_MAX_MACRO_LENGTH     480

#define ROCCAT_MIN_DPI  100
#define ROCCAT_MAX_DPI  12000

#define ROCCAT_USER_DEFINED_COLOR   0x1e // The mouse knows some predefined colors. User can also set RGB values
#define ROCCAT_LED_BLINKING         0x02
#define ROCCAT_LED_BREATHING        0x03
#define ROCCAT_LED_PULSING          0x04

unsigned int report_rates[] = { 125, 250, 500, 1000 };

struct color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} __attribute__((packed));

struct color predefined_led_colors[] = { 
	{ 179, 0, 0 }, { 255, 0, 0 }, { 255, 71, 0}, { 255, 106, 0 },
	{ 255, 157, 71 }, { 248, 232, 0 }, { 246, 255, 78 }, { 201, 255, 78 },
	{ 185, 255, 78 }, { 132, 255, 78 }, { 0, 255, 0 }, { 0, 207, 55 },
	{ 0, 166, 44 }, { 0, 207, 124 }, { 0,207, 158 }, { 0, 203, 207 },
	{ 41, 197, 255 }, { 37, 162, 233 }, { 99, 158, 239 }, { 37, 132, 233 },
	{ 0, 72, 255 }, { 15, 15, 255 }, { 15, 15, 188 }, { 89, 7, 255 },
	{ 121, 12, 255 }, { 161, 12, 255 }, { 170, 108, 232 }, { 181, 10, 216 },
	{ 205, 10, 217 }, { 217, 10, 125 } 
};

struct led_data {
	uint8_t predefined;             // Index of the predefined color. 0x1e for user defined color.
	struct color color;
} __attribute__((packed));

struct roccat_settings_report {
	uint8_t reportID;               // 0x06
	uint8_t magic_num;              // 0x29
	uint8_t profile;
	uint8_t x_y_linked;             // Always 0. Not on EMP ?
	uint8_t x_sensitivity;          // From -5 (0x01) to 5 (0x0b)
	uint8_t y_sensitivity;          // From -5 (0x01) to 5 (0x0b)
	uint8_t dpi_mask;               // Bitfield to know which DPI setting is enabled
	uint8_t xres[ROCCAT_NUM_DPI];   // DPI on X axis (from 0x00 to 0x77)
	uint8_t yres[ROCCAT_NUM_DPI];   // DPI on Y axis (always same values than xres)
	uint8_t current_dpi;            // One index, since X and Y DPIs are the same
	uint8_t report_rate;            // From 0x00 to 0x03
	uint8_t led_status;             // Two bitfields of 4 bits. First four bits tells if the LED colors is predefined. Latest four bits tells if the LED is on of off
	uint8_t lighting_flow;          // 0x01 for color cycle effect. 0x00 to disable it
	uint8_t lighting_effect;        // From 0x01 to 0x04 : fixed, blinking, breathing, beating
	uint8_t effect_speed;           // From 0x01 to 0x03
	struct led_data leds[ROCCAT_LED_MAX];
	uint16_t checksum;
} __attribute__((packed));
#define ROCCAT_REPORT_SIZE_SETTINGS sizeof(struct roccat_settings_report)

struct roccat_macro {
	uint8_t reportID;                           // 0x08
	uint8_t bank;                               // 0x01 or 0x02
	uint8_t profile;
	uint8_t button_index;
	uint8_t repeats;                            // Number of repetition for this macro
	char group[ROCCAT_MACRO_GROUP_NAME_LENGTH]; // Folder name
	char name[ROCCAT_MACRO_NAME_LENGTH];
	uint16_t length;
	struct {
		uint8_t keycode;
		uint8_t flag;                           // Pressed (0x01) or released (0x02)
		uint16_t time;
	} keys[ROCCAT_MAX_MACRO_LENGTH];
} __attribute__((packed));


struct button {
	uint8_t keycode;
	uint8_t undetermined1;
	uint8_t undetermined2;
} __attribute__((packed));

struct roccat_buttons {
	uint8_t reportID;               // 0x07
	uint8_t magic_num;              // 0x47
	uint8_t profile;
	struct button keys[ROCCAT_BUTTON_MAX];
	uint16_t checksum;
} __attribute__((packed));
#define ROCCAT_REPORT_SIZE_BUTTONS sizeof(struct roccat_buttons)

struct roccat_data {
	struct roccat_buttons buttons[(ROCCAT_PROFILE_MAX)];
	struct roccat_settings_report settings[(ROCCAT_PROFILE_MAX)];
	struct roccat_macro macros[(ROCCAT_PROFILE_MAX)][(ROCCAT_BUTTON_MAX + 1)];
};

struct roccat_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static struct roccat_button_mapping roccat_button_mapping[] = {
	{ 0, BUTTON_ACTION_NONE },
	{ 1, BUTTON_ACTION_BUTTON(1) },
	{ 2, BUTTON_ACTION_BUTTON(2) },
	{ 3, BUTTON_ACTION_BUTTON(3) },
	{ 4, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },
/* FIXME:   { 5, Shortcut (modifier + key) }, */
	{ 7, BUTTON_ACTION_BUTTON(4) }, /* Next page in browser */
	{ 8, BUTTON_ACTION_BUTTON(5) }, /* Previous page in browser */
	{ 9, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT) },
	{ 10, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT) },
	{ 13, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ 14, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
/* FIXME:   { 15, quicklaunch },  -> hidraw report 03 00 60 07 01 00 00 00 -> Open any configurated app */
	{ 16, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 17, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ 18, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
	{ 20, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 21, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 22, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
/* FIXME:   { 23, Toggle sensibility }, */
/* FIXME:   { 24, Sensibility UP }, */
/* FIXME:   { 25, Sensibility Down }, */
/* FIXME:   { 27, open driver/swarm },  -> hidraw report 02 83 01 00 00 00 00 00 */
	//{ 32, BUTTON_ACTION_KEY(KEY_CONFIG) },
	{ 33, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ 34, BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ 35, BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ 36, BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ 37, BUTTON_ACTION_KEY(KEY_MUTE) },
	{ 38, BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ 39, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },
	{ 48, BUTTON_ACTION_MACRO },
/* FIXME:   { 49, Start timer }, */
/* FIXME:   { 50, Stop timer}, */
/* FIXME:   { 51, EasyAim DPI 400 }, */
/* FIXME:   { 52, EasyAim DPI 400 }, */
/* FIXME:   { 53, EasyAim DPI 800 }, */
/* FIXME:   { 54, EasyAim DPI 1200 }, */
/* FIXME:   { 55, EasyAim DPI 1600 }, */
/* FIXME:   { 56, EasyAim DPI 3200 }, */
	{ 65, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE) },
/* FIXME:   { 66, Easywheel sensitivity }, */
/* FIXME:   { 67, Easywheel profile }, */
/* FIXME:   { 68, Easywheel DPI }, */
/* FIXME:   { 69, EasywheelVolume }, */
/* FIXME:   { 70, Easywheel Alt-Tab }, */
/* FIXME:   { 98, Home }, */
/* FIXME:   { 99, End }, */
/* FIXME:   { 100, Previous page }, */
/* FIXME:   { 100, Next page }, */
/* FIXME:   { 101, Maj left }, */
/* FIXME:   { 102, Maj right }, */
/* FIXME:   { 113, Sensibility -5 }, */
/* FIXME:   { 114, Sensibility -4 }, */
/* FIXME:   { 115, Sensibility -3 }, */
/* FIXME:   { 116, Sensibility -2 }, */
/* FIXME:   { 117, Sensibility -1 }, */
/* FIXME:   { 118, Sensibility 0 }, */
/* FIXME:   { 119, Sensibility 1 }, */
/* FIXME:   { 120, Sensibility 2 }, */
/* FIXME:   { 121, Sensibility 3 }, */
/* FIXME:   { 122, Sensibility 4 }, */
/* FIXME:   { 123, Sensibility 5 }, */
/* FIXME:   { 124, EasyAim DPI Userset }, - second byte of button_data is the DPI */
/* FIXME:   { 128, Browser search }, */
/* FIXME:   { 129, Browser home }, */
/* FIXME:   { 130, Browser stop }, */
/* FIXME:   { 131, Browser refresh }, */
/* FIXME:   { 132, Browser new tab (ctrl+T) }, */
/* FIXME:   { 133, Browser new window }, */
/* FIXME:   { 134, Open "Computer" }, */
/* FIXME:   { 135, Open calculator }, */
/* FIXME:   { 136, Open email }, */
/* FIXME:   { 137, Open file }, */
/* FIXME:   { 138, Open folder }, */
/* FIXME:   { 139, Open URL }, */
/* FIXME:   { 140, Mute mic }, */
/* FIXME:   { 141, Open Desktop }, */
/* FIXME:   { 142, Open Favorites }, */
/* FIXME:   { 143, Open Fonts }, */
/* FIXME:   { 144, Open My Documents }, */
/* FIXME:   { 145, Open Downloads }, */
/* FIXME:   { 146, Open Music }, */
/* FIXME:   { 147, Open Pictures }, */
/* FIXME:   { 148, Open Network }, */
/* FIXME:   { 149, Printers }, */
/* FIXME:   { 150, Network }, */

/* FIXME:   { 167, System hibernation }, */
/* FIXME:   { 168, System reboot }, */
/* FIXME:   { 169, System lock }, */
/* FIXME:   { 179, Logout }, */
/* FIXME:   { 171, Control panel }, */
/* FIXME:   { 172, System settings }, */
/* FIXME:   { 173, Task Manager }, */
/* FIXME:   { 174, Screen settings }, */
/* FIXME:   { 175, Screensaver settings }, */
/* FIXME:   { 176, Themes }, */
/* FIXME:   { 177, Date and Time }, */
/* FIXME:   { 178, Network settings }, */
/* FIXME:   { 179, Admin settings }, */
/* FIXME:   { 180, Firewall }, */
/* FIXME:   { 181, Regedit }, */
/* FIXME:   { 182, Event monitor }, */
/* FIXME:   { 183, Performance monitor }, */
/* FIXME:   { 184, Audio settings }, */
/* FIXME:   { 185, Internet settings }, */
/* FIXME:   { 186, Directx diagnostics }, */
/* FIXME:   { 187, Command line }, */
/* FIXME:   { 188, System poweroff }, */
/* FIXME:   { 189, System sleep }, */
/* FIXME:   { 190, System wakeup }, */

/* FIXME:   { 191, Set profile 1 }, */
/* FIXME:   { 192, Set profile 2 }, */
/* FIXME:   { 193, Set profile 3 }, */
/* FIXME:   { 194, Set profile 4 }, */
/* FIXME:   { 195, Set profile 5 }, */
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

/**
 * Compute the CRC from buf
 * len should be the length of buf, including the two bytes used for CRC
 */
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

/**
 * Returns if the CRC in buf is valid.
 * The CRC is expected to be the last two bytes of buf
 * len should be the length of buf, including the CRC
 */
static inline int
roccat_crc_is_valid(struct ratbag_device *device, uint8_t *buf, unsigned int len)
{
	uint16_t crc;
	uint16_t given_crc;

	if (len < 3)
		return 0;

	crc = roccat_compute_crc(buf, len);

	given_crc = roccat_get_unaligned_u16(&buf[len - 2]);

	log_debug(device->ratbag,
		"checksum computed: 0x%04x, checksum given: 0x%04x - %s\n",
		crc,
		given_crc,
		crc == given_crc ? "OK" : "FAIL");


	return crc == given_crc;
}

static int
roccat_is_ready(struct ratbag_device *device)
{
	uint8_t buf[3] = { 0 };
	int rc;

	rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_CONFIGURE_PROFILE,
						  buf, sizeof(buf));
	if (rc < 0)
		return rc;
	if (rc != sizeof(buf))
		return -EIO;

	if (buf[1] == 0x03)
		msleep(100);

	if (buf[1] == 0x02)
		return 2;

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

		if (rc == 2)
			return 2;

		msleep(10);
		count++;
	}

	return -ETIMEDOUT;
}

static int
roccat_current_profile(struct ratbag_device *device)
{
	uint8_t buf[3];
	int ret;

	ret = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_PROFILE, buf,
						   sizeof(buf));
	if (ret < 0)
		return ret;

	if (ret != 3)
		return -EIO;

	return buf[2];
}

static int
roccat_set_current_profile(struct ratbag_device *device, unsigned int index)
{

	log_debug(device->ratbag,
		"'%s' Setting profile %d as active\n",
		ratbag_device_get_name(device),
		index);

	uint8_t buf[] = {ROCCAT_REPORT_ID_PROFILE, 0x03, index};
	int ret;

	if (index > ROCCAT_PROFILE_MAX)
		return -EINVAL;

	ret = ratbag_hidraw_set_feature_report(device, buf[0], buf,
						   sizeof(buf));

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

/**
 * Sets the profile and which information we want to get from the mouse
 *
 * @param profile is the index of the profile from which you want the info. But, it is also used as a memory bank
 * identifier when querying a macro. In this case, the first bank can be queried by adding 0x10 to the profile index,
 * and the second bank, by adding 0x20.
 * @param type can be either which information you need (ROCCAT_CONFIG_SETTINGS or ROCCAT_CONFIG_KEY_MAPPING) or
 * it can be used to specify the button from which you want to get the macro.
 */
static int
roccat_set_config_profile(struct ratbag_device *device, uint8_t profile, uint8_t type)
{
	uint8_t buf[] = {ROCCAT_REPORT_ID_CONFIGURE_PROFILE, profile, type};
	int ret;
/*
	if (profile > ROCCAT_PROFILE_MAX)
		return -EINVAL;
*/
	ret = ratbag_hidraw_set_feature_report(device, buf[0], buf,
						   sizeof(buf));
	if (ret < 0)
		return ret;

	if (ret != sizeof(buf))
		return -EIO;

	ret = roccat_wait_ready(device);
	if (ret < 0)
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

	data = drv_data->buttons[profile->index].keys[button_index].keycode;
	return roccat_raw_to_button_action(data);
}

static unsigned int roccat_report_rate_to_index(unsigned int rate) {
	for(unsigned int i = 0 ; i < ARRAY_LENGTH(report_rates) ; i++) {
		if(report_rates[i] == rate) {
			return i;
		}
	}
	return 0;
}

static int
roccat_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	unsigned int index = profile->index;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	struct ratbag_button *button;
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	struct roccat_settings_report* report;
	struct roccat_buttons* buttons;
	struct roccat_macro* macro;
	uint8_t bank_buf[ROCCAT_REPORT_SIZE_MACRO_BANK] = { 0 };
	int rc = 0;
	int i = 0, count = 0;


	assert(index <= ROCCAT_PROFILE_MAX);

	report = &drv_data->settings[profile->index];
	report->reportID = ROCCAT_REPORT_ID_SETTINGS;
	report->magic_num = ROCCAT_MAGIC_NUMBER_SETTINGS;
	report->report_rate = roccat_report_rate_to_index(profile->hz);

	report->dpi_mask = 0;
	ratbag_profile_for_each_resolution(profile, resolution) {
		report->xres[resolution->index] = (resolution->dpi_x - 100) / 100;
		report->yres[resolution->index] = (resolution->dpi_y - 100) / 100;

		if(resolution->is_active) {
			report->current_dpi = resolution->index;
		}

		if(resolution->dpi_x != 0 && resolution->dpi_y != 0) {
			report->dpi_mask += (1 << resolution->index);
		}
	}
	
	ratbag_profile_for_each_led(profile, led) {
		report->leds[led->index].predefined = ROCCAT_USER_DEFINED_COLOR; // Always user defined with libratbag (easier)
		report->leds[led->index].color.r = led->color.red;
		report->leds[led->index].color.g = led->color.green;
		report->leds[led->index].color.b = led->color.blue;
	
		// Last LED sets the profile values
		switch(led->mode) {
			case RATBAG_LED_OFF:
				report->led_status = 0xf0;
				break;
			case RATBAG_LED_ON:
				report->led_status = 0xff;
				break;
			case RATBAG_LED_CYCLE:
				report->led_status = 0xff;
				report->lighting_flow = 1;
				report->effect_speed = led->ms / 1000;
				break;
			case RATBAG_LED_BREATHING:
				report->led_status = 0xff;
				report->lighting_effect = ROCCAT_LED_BREATHING;
				report->effect_speed = led->ms / 1000;
		}
	}
	report->checksum = roccat_compute_crc((uint8_t*)report, ROCCAT_REPORT_SIZE_SETTINGS);


	buttons = &drv_data->buttons[profile->index];
	buttons->reportID = ROCCAT_REPORT_ID_KEY_MAPPING;
	buttons->magic_num = ROCCAT_MAGIC_NUMBER_KEY_MAPPING;
	ratbag_profile_for_each_button(profile, button) {
		buttons->keys[button->index].keycode = roccat_button_action_to_raw(&button->action);
		if(button->action.type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
			macro = &drv_data->macros[profile->index][button->index];
			memset(macro, 0, sizeof(struct roccat_macro));

			macro->reportID = ROCCAT_REPORT_ID_MACRO;
			macro->bank = ROCCAT_BANK_ID_1;
			macro->profile = profile->index;
			macro->button_index = button->index;
			macro->repeats = 0; // No repeats in libratbag

			if(button->action.macro->group) {
				// Seems no use of group in libratbag
				strncpy(macro->group, button->action.macro->group, ROCCAT_MACRO_GROUP_NAME_LENGTH); 
			} else {
				strncpy(macro->group, "libratbag macros", ROCCAT_MACRO_GROUP_NAME_LENGTH); 
			}
			strncpy(macro->name, button->action.macro->name, ROCCAT_MACRO_NAME_LENGTH); 

			for (i = 0; i < MAX_MACRO_EVENTS && count < ROCCAT_MAX_MACRO_LENGTH; i++) {
				if (button->action.macro->events[i].type == RATBAG_MACRO_EVENT_INVALID)
					return -EINVAL; /* should not happen, ever */

				if (button->action.macro->events[i].type == RATBAG_MACRO_EVENT_NONE)
					break;

				/* ignore the first wait */
				if (button->action.macro->events[i].type == RATBAG_MACRO_EVENT_WAIT &&
					!count)
					continue;

				if (button->action.macro->events[i].type == RATBAG_MACRO_EVENT_KEY_PRESSED ||
					button->action.macro->events[i].type == RATBAG_MACRO_EVENT_KEY_RELEASED) {
					macro->keys[count].keycode = ratbag_hidraw_get_keyboard_usage_from_keycode(device, button->action.macro->events[i].event.key);
				}

				switch (button->action.macro->events[i].type) {
				case RATBAG_MACRO_EVENT_KEY_PRESSED:
					macro->keys[count].flag = 0x01;
					break;
				case RATBAG_MACRO_EVENT_KEY_RELEASED:
					macro->keys[count].flag = 0x02;
					break;
				case RATBAG_MACRO_EVENT_WAIT:
					macro->keys[--count].time = button->action.macro->events[i].event.timeout;
					break;
				case RATBAG_MACRO_EVENT_INVALID:
				case RATBAG_MACRO_EVENT_NONE:
					/* should not happen */
					log_error(device->ratbag,
						"something went wrong while writing a macro.\n");
				}
				count++;
			}
			macro->length = count;
			

			// Macro has to be send in two packets
			memcpy(bank_buf, macro, ROCCAT_REPORT_SIZE_MACRO_BANK);

			rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_MACRO,
						  bank_buf, ROCCAT_REPORT_SIZE_MACRO_BANK);    
			if (rc < 0)
				return rc;

			if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK)
				return -EIO;

			rc = roccat_wait_ready(device);
			if (rc)
				log_error(device->ratbag,
					"Error while waiting for the device to be ready: %s (%d)\n",
					strerror(-rc), rc);

			bank_buf[0] = ROCCAT_REPORT_ID_MACRO;
			bank_buf[1] = ROCCAT_BANK_ID_2;
			// The remaining macro structure is not big enough to fill the second bank
			// Write the remaining, fill the end with 0 
			unsigned int remaining_to_write = sizeof(struct roccat_macro)-ROCCAT_REPORT_SIZE_MACRO_BANK;
			memcpy(bank_buf+2, &((uint8_t*)macro)[ROCCAT_REPORT_SIZE_MACRO_BANK], remaining_to_write);
			memset(bank_buf+2+remaining_to_write, 0, ROCCAT_REPORT_SIZE_MACRO_BANK-(2+remaining_to_write));

			rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_MACRO,
				bank_buf, ROCCAT_REPORT_SIZE_MACRO_BANK);
			if (rc < 0)
				return rc;

			if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK)
				return -EIO;

			rc = roccat_wait_ready(device);
			if (rc)
				log_error(device->ratbag,
					"Error while waiting for the device to be ready: %s (%d)\n",
					strerror(-rc), rc);
		}
	}
	buttons->checksum = roccat_compute_crc((uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS);


	rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_SETTINGS,
						  (uint8_t*)report, ROCCAT_REPORT_SIZE_SETTINGS);

	if (rc < 0)
		return rc;

	if (rc != ROCCAT_REPORT_SIZE_SETTINGS)
		return -EIO;

	rc = roccat_wait_ready(device);
	if (rc) {
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-rc), rc);
	}

	rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_KEY_MAPPING,
						  (uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS);

	if (rc < 0)
		return rc;

	if (rc != ROCCAT_REPORT_SIZE_BUTTONS)
		return -EIO;

	rc = roccat_wait_ready(device);
	if (rc) {
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-rc), rc);
	}

	log_debug(device->ratbag, "profile: %d written %s:%d\n",
		profile->index,
		__FILE__, __LINE__);

	return rc;
}

static void roccat_read_macro(struct roccat_macro* macro, struct ratbag_button* button) {
	struct ratbag_button_macro *m = NULL;
	unsigned j, time;

	char name[ROCCAT_MACRO_NAME_LENGTH+1] = { '\0' };
	strncpy(name, macro->name, ROCCAT_MACRO_NAME_LENGTH);

	m = ratbag_button_macro_new(name);
	// libratbag does offer API for macro groups
	m->macro.group = (char*)zalloc(ROCCAT_MACRO_GROUP_NAME_LENGTH+1);
	strncpy(m->macro.group, macro->group, ROCCAT_MACRO_GROUP_NAME_LENGTH);

	log_debug(button->profile->device->ratbag,
		"macro on button %d of profile %d is named '%s' (from folder '%s'), and contains %d events:\n",
		button->index, button->profile->index,
		name, m->macro.group, macro->length);
	// libratbag can't keep track of the whole macro (MAX_MACRO_EVENTS)
	// In libratbag, each event is implemented as two separate (KEY_PRESS/KEY_RELEASE and WAIT)
	for (j = 0; j < macro->length && j < MAX_MACRO_EVENTS/2; j++) {
		unsigned int keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(button->profile->device,
						macro->keys[j].keycode);
		ratbag_button_macro_set_event(m,
							j * 2,
							macro->keys[j].flag & 0x01 ? RATBAG_MACRO_EVENT_KEY_PRESSED : RATBAG_MACRO_EVENT_KEY_RELEASED,
							keycode);
		if (macro->keys[j].time)
			time = macro->keys[j].time;
		else
			time = macro->keys[j].flag & 0x01 ? 10 : 50;
		ratbag_button_macro_set_event(m,
							j * 2 + 1,
							RATBAG_MACRO_EVENT_WAIT,
							time);

		log_debug(button->profile->device->ratbag,
			"    - %s %s\n",
			libevdev_event_code_get_name(EV_KEY, keycode),
			macro->keys[j].flag == 0x02 ? "released" : "pressed");
	}
	ratbag_button_copy_macro(button, m);
	ratbag_button_macro_unref(m);
}

static void
roccat_read_button(struct ratbag_button *button)
{
	const struct ratbag_button_action *action;
	struct ratbag_device *device = button->profile->device;
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	struct roccat_macro *macro;
	int rc;

	action = roccat_button_to_action(button->profile, button->index);
	if (action)
		ratbag_button_set_action(button, action);
//  if (action == NULL)
//      log_error(device->ratbag, "button: %d -> %d %s:%d\n",
//          button->index, drv_data->profiles[button->profile->index][3 + button->index * 3],
//          __FILE__, __LINE__);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	if (action && action->type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
		macro = &drv_data->macros[button->profile->index][button->index];

		// Macros are available through two packets
		// We read the second one first, to overwrite some useless data (report id) in the final structure
		roccat_set_config_profile(device,
					  button->profile->index,
					  0);
		roccat_set_config_profile(device,
					  button->profile->index + 0x20, // When setting for a specific button, the Profile ID is 0x10 (or 0x20 for second bank) * Profile ID + 1
					  button->index);

		// I know that the second bank will not fit in internal structure, so reducing the data read
		rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_MACRO,
							  (uint8_t*)macro + ROCCAT_REPORT_SIZE_MACRO_BANK - 2, sizeof(struct roccat_macro) - (ROCCAT_REPORT_SIZE_MACRO_BANK - 2));
		if (rc != sizeof(struct roccat_macro) - (ROCCAT_REPORT_SIZE_MACRO_BANK - 2)) {
			log_error(device->ratbag,
				  "Unable to retrieve the second bank for macro for button %d of profile %d: %s (%d)\n",
				  button->index, button->profile->index,
				  rc < 0 ? strerror(-rc) : "not read enough", rc);
			goto out_macro;
		}

		roccat_set_config_profile(device,
					  button->profile->index + 0x10, // When setting for a specific button, the Profile ID is 0x10 * Profile ID + 1
					  button->index);
		rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_MACRO,
							  (uint8_t*)macro, ROCCAT_REPORT_SIZE_MACRO_BANK);
		if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK) {
			log_error(device->ratbag,
				  "Unable to retrieve the first bank for macro for button %d of profile %d: %s (%d)\n",
				  button->index, button->profile->index,
				  rc < 0 ? strerror(-rc) : "not read enough", rc);
			goto out_macro;
		} 
		
		if (macro->reportID != ROCCAT_REPORT_ID_MACRO) {
			log_error(device->ratbag,
					"Error while reading the macro of button %d of profile %d.\n",
					button->index,
					button->profile->index);
			goto out_macro;
		}
		// No checksum for macros

		roccat_read_macro(macro, button);       

out_macro:
		msleep(10);
	}
}

static void
roccat_read_dpi(struct roccat_settings_report* settings, struct ratbag_profile* profile)
{
	struct ratbag_resolution *resolution;
	int dpi_x = 0, dpi_y = 0;
	unsigned int report_rate = 0;

	/* first retrieve the report rate, it is set per profile */
	if (settings->report_rate < ARRAY_LENGTH(report_rates)) {
		report_rate = report_rates[settings->report_rate];
	} else {
		log_error(profile->device->ratbag,
			  "error while reading the report rate of the mouse (0x%02x)\n",
			  settings->report_rate);
		report_rate = 0;
	}

	ratbag_profile_set_report_rate_list(profile, report_rates,
						ARRAY_LENGTH(report_rates));
	profile->hz = report_rate;

	ratbag_profile_for_each_resolution(profile, resolution) {
		dpi_x = settings->xres[resolution->index] * 100 + 100;
		dpi_y = settings->yres[resolution->index] * 100 + 100;
		resolution->is_active = (resolution->index == settings->current_dpi);
		if (!(settings->dpi_mask & (1 << resolution->index))) {
			/* this resolution is disabled, overwrite it */
			dpi_x = 0;
			dpi_y = 0;
		}

		ratbag_resolution_set_resolution(resolution, dpi_x, dpi_y);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

		ratbag_resolution_set_dpi_list_from_range(resolution, ROCCAT_MIN_DPI, ROCCAT_MAX_DPI);
	}
}

static void
roccat_read_led(struct roccat_settings_report* settings, struct ratbag_led *led)
{
	if(settings->led_status == 0) {
		led->mode = RATBAG_LED_OFF;
	} else {
		led->mode = RATBAG_LED_ON;
	}
	if(settings->lighting_flow) {
		led->mode = RATBAG_LED_CYCLE;
		led->ms = settings->effect_speed * 1000;
	}
	if(settings->lighting_effect == ROCCAT_LED_BREATHING) {
		led->mode = RATBAG_LED_BREATHING;
		led->ms = settings->effect_speed * 1000;
	}

	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
	if(settings->leds[led->index].predefined < ROCCAT_USER_DEFINED_COLOR) {
		led->color.red = predefined_led_colors[settings->leds[led->index].predefined].r;
		led->color.green = predefined_led_colors[settings->leds[led->index].predefined].g;
		led->color.blue = predefined_led_colors[settings->leds[led->index].predefined].b;
	}
	else {
		led->color.red = settings->leds[led->index].color.r;
		led->color.green = settings->leds[led->index].color.g;
		led->color.blue = settings->leds[led->index].color.b;
	}
	ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
	ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
}

static void
roccat_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_button *button;
	struct ratbag_led *led;
	struct roccat_settings_report *settings;
	struct roccat_buttons* buttons;
	int rc; 

	assert(profile->index <= ROCCAT_PROFILE_MAX);

	// Read data from the mouse
	settings = &drv_data->settings[profile->index];
	roccat_set_config_profile(device, profile->index, ROCCAT_CONFIG_SETTINGS);
	rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_SETTINGS,
						  (uint8_t*)settings, ROCCAT_REPORT_SIZE_SETTINGS);

	if (rc < (int)ROCCAT_REPORT_SIZE_SETTINGS) {
		return;
	}

	if (!roccat_crc_is_valid(device, (uint8_t*)settings, ROCCAT_REPORT_SIZE_SETTINGS)) {
		log_error(device->ratbag,
			  "Error while reading settings from profile %d, checksum invalid, continuing...\n",
			  profile->index);
	}

	buttons = &drv_data->buttons[profile->index];
	roccat_set_config_profile(device, profile->index, ROCCAT_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_KEY_MAPPING,
						  (uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS);

	if (rc < (int)ROCCAT_REPORT_SIZE_BUTTONS) {
		return;
	}

	if (!roccat_crc_is_valid(device, (uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS)) {
		log_error(device->ratbag,
			  "Error while reading buttons from profile %d, checksum invalid, continuing...\n",
			  profile->index);
	}

	// Feed libratbag with the data
	roccat_read_dpi(settings, profile);
	ratbag_profile_for_each_led(profile, led) {
		roccat_read_led(settings, led);
	}

	// Buttons were read from the buffer that was yet un-initialized with the device data.
	ratbag_profile_for_each_button(profile, button)
		roccat_read_button(button);

	
	log_debug(device->ratbag, "profile: %d %s:%d\n",
		settings->profile,
		__FILE__, __LINE__);
}

static int
roccat_probe(struct ratbag_device *device)
{
	int rc;
	struct ratbag_profile *profile;
	struct roccat_data *drv_data;
	int active_idx;

	rc = ratbag_open_hidraw(device);
	if (rc)
		return rc;

	if (!ratbag_hidraw_has_report(device, ROCCAT_REPORT_ID_KEY_MAPPING)) {
		ratbag_close_hidraw(device);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	ratbag_device_init_profiles(device,
					ROCCAT_PROFILE_MAX,
					ROCCAT_NUM_DPI,
					ROCCAT_BUTTON_MAX,
					ROCCAT_LED_MAX);

	ratbag_device_for_each_profile(device, profile) {
		roccat_read_profile(profile);
	}

	active_idx = roccat_current_profile(device);
	if (active_idx < 0) {
		log_error(device->ratbag,
			  "Can't talk to the mouse: '%s' (%d)\n",
			  strerror(-active_idx),
			  active_idx);
		rc = -ENODEV;
		goto err;
	}

	ratbag_device_for_each_profile(device, profile) {
		if (profile->index == (unsigned int)active_idx) {
			profile->is_active = true;
			break;
		}
	}

	log_debug(device->ratbag,
		"'%s' is in profile %d\n",
		ratbag_device_get_name(device),
		profile->index);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static int
roccat_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc = 0;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		log_debug(device->ratbag,
			  "Profile %d changed, rewriting\n", profile->index);

		rc = roccat_write_profile(profile);
		if (rc)
			return rc;
	}

	return 0;
}

static void
roccat_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver roccat_emp_driver = {
	.name = "Roccat Kone EMP",
	.id = "roccat-kone-emp",
	.probe = roccat_probe,
	.remove = roccat_remove,
	.commit = roccat_commit,
	.set_active_profile = roccat_set_current_profile,
};
