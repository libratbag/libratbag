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
#define ROCCAT_BUTTON_MAX           12 * 2 // (Easy Shift)
#define ROCCAT_NUM_DPI              5
#define ROCCAT_LED_MAX              11

#define ROCCAT_MAX_RETRY_READY      10

#define ROCCAT_REPORT_ID_CONFIGURE_PROFILE  4
#define ROCCAT_REPORT_ID_PROFILE            5
#define ROCCAT_REPORT_ID_SETTINGS           6
#define ROCCAT_REPORT_ID_KEY_MAPPING        7
#define ROCCAT_REPORT_ID_MACRO              8

#define ROCCAT_SETTINGS_DATA_LENGTH         126
#define ROCCAT_KEY_MAPPING_DATA_LENGTH      75
#define ROCCAT_REPORT_SIZE_BUTTONS          75
#define ROCCAT_REPORT_SIZE_SETTINGS	    126

#define ROCCAT_BANK_ID_1    1
#define ROCCAT_BANK_ID_2    2
#define ROCCAT_REPORT_SIZE_MACRO_BANK1       1026
#define ROCCAT_REPORT_SIZE_MACRO_BANK2       977
#define ROCCAT_MACRO_BANK1_KEYS_LENGTH      237
#define ROCCAT_MACRO_BANK2_KEYS_LENGTH      243
#define ROCCAT_MACRO_BANK2_TERMINATOR       0x4A

#define ROCCAT_MACRO_GROUP_NAME_LENGTH  40
#define ROCCAT_MACRO_NAME_LENGTH        32

#define ROCCAT_CONFIG_SETTINGS      0x80 // (LED and mouse configuration)
#define ROCCAT_CONFIG_KEY_MAPPING   0x90 // (Buttons configuration)

#define ROCCAT_MAX_MACRO_LENGTH     480

#define ROCCAT_MIN_DPI  100
#define ROCCAT_MAX_DPI  16000

#define ROCCAT_USER_DEFINED_COLOR   0x1e // The mouse knows some predefined colors. User can also set RGB values
#define ROCCAT_LED_FIXED            0x01
#define ROCCAT_LED_SNAKE            0x06
#define ROCCAT_LED_WAVE             0x0a
#define ROCCAT_LED_BREATHING        0x07
#define ROCCAT_LED_HEARTBEAT        0x08
#define ROCCAT_LED_AIMO             0x09

static unsigned int report_rates[] = { 125, 250, 500, 1000 };

struct color {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} __attribute__((packed));

struct roccat_color {
	uint8_t intensity;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t padding;
} __attribute__((packed));

struct roccat_settings_report {
	uint8_t report_id;                          // 0x06
	uint8_t report_data_length;                 // 126 for settings
	uint8_t profile;                            // 5 Profiles, 0-4
	uint8_t sensitivity;                        // 0x06 means 0 because its -5 to 5 in the UI, so 1-11
	uint8_t x_y_linked;                         // Set X and Y dpi separately, but its not a feature in ROCCAT Swarm, default `1F`
	uint8_t current_dpi;                        // 0-4 for the 5 resolutions
	uint16_t xres[ROCCAT_NUM_DPI];              // 5 resolutions saved to switch between. value * 50 = DPI
	uint16_t yres[ROCCAT_NUM_DPI];              // 5 resolutions saved to switch between. value * 50 = DPI
	uint8_t report_rate;                        // 0 = 125 hz, 1 = 250hz, 2 = 500hz, 3 = 1000hz
	uint8_t angle_snapping;                     // 0 = off, 1 = on
	struct roccat_color unk_color1;             // 08 FF 07 00 not sure, looks like some kind of 0 intensity blue. Nothing in the official software changes it.
	uint8_t lighting_effect;                    // From 0x01 to 0x04 : fixed, blinking, breathing, beating
	uint8_t lighting_effect_speed;              // From 0x01 to 0x03
	struct roccat_color brightness;             // intensity is global brightness, color is unused but `1D 13 FF` default.
	struct roccat_color unk_color2;             // FF 59 FF 00, looks like some kind of default gradient, might be AIMO gradient?
	struct roccat_color unk_color3;             // FF FD FD 00
	struct roccat_color unk_color4;             // FF F4 64 00
	struct roccat_color unk_color5;             // FF F4 00 00
	uint8_t unknown1;                           // FF default, doesn't change by any UI settings.
	struct roccat_color led_scrollwheel_color;  // only solid
	struct roccat_color led_leftstrip_color_1;  // 4 color gradient. Solid is just all same color
	struct roccat_color led_leftstrip_color_2;
	struct roccat_color led_leftstrip_color_3;
	struct roccat_color led_leftstrip_color_4;
	struct roccat_color led_rightstrip_color_1; // 4 color gradient. Solid is just all same color
	struct roccat_color led_rightstrip_color_2;
	struct roccat_color led_rightstrip_color_3;
	struct roccat_color led_rightstrip_color_4;
	struct roccat_color led_leftblob_color;    // only solid
	struct roccat_color led_rightblob_color;   // only solid
	uint8_t custom_or_theme;                   // 00-09 for custom based on theme, theme is 80-89
	uint8_t unknown2;                          // 01 default, doesn't change by any UI settings.
	uint8_t padding[6];
	uint16_t checksum;
} __attribute__((packed));
_Static_assert(sizeof(struct roccat_settings_report) == ROCCAT_REPORT_SIZE_SETTINGS, "Size of roccat_buttons is wrong");

struct roccat_macro_keys {
	uint8_t keycode;
	uint8_t flag;  // 0x01 = press, 0x02 = release
	uint16_t time; // Fixed Delay in milliseconds
} __attribute__((packed));

struct last_macro_key {
	uint8_t keycode;
	uint8_t flag; // 0x01 = press, 0x02 = release
	uint8_t first_half_time; // For the last key, the time is split between the pages
} __attribute__((packed));

struct _roccat_macro_bank1 {
	uint8_t report_id;
	uint8_t bank;
	uint8_t profile;
	uint8_t button_index;
	uint8_t repeat; // number of times to repeat the macro sequence
	char group[40]; // Max 40 characters for the group/folder name
	char name[32];  // Max 32 characters for the macro name
	uint16_t length; // OR'd with On Press = 0x0000, While Press = 0x0010, Macro toggle = 0x0020
	struct roccat_macro_keys keys[ROCCAT_MACRO_BANK1_KEYS_LENGTH-1];
	struct last_macro_key last_key;
} __attribute__((packed));

union roccat_macro_bank1 {
	struct _roccat_macro_bank1 msg;
	uint8_t data[ROCCAT_REPORT_SIZE_MACRO_BANK1];
};
_Static_assert(sizeof(struct _roccat_macro_bank1) == ROCCAT_REPORT_SIZE_MACRO_BANK1, "Size of roccat_macro_bank1 is wrong");

struct _roccat_macro_bank2 {
	uint8_t report_id;
	uint8_t bank;
	uint8_t second_half_time; // For the last key, the time is split between the pages
	struct roccat_macro_keys keys[ROCCAT_MACRO_BANK2_KEYS_LENGTH];
	uint16_t checksum;  // This is the checksum of both pages of keys.
} __attribute__((packed));

union roccat_macro_bank2 {
	struct _roccat_macro_bank2 msg;
	uint8_t data[ROCCAT_REPORT_SIZE_MACRO_BANK2];
};

_Static_assert(sizeof(struct _roccat_macro_bank2) == ROCCAT_REPORT_SIZE_MACRO_BANK2, "Size of roccat_macro_bank2 is wrong");

struct _roccat_macro_combined {
	union roccat_macro_bank1 bank1;
	union roccat_macro_bank2 bank2;
} __attribute__((packed));

union roccat_macro_combined {
	struct _roccat_macro_combined msg;
	uint8_t data[ROCCAT_REPORT_SIZE_MACRO_BANK1 + ROCCAT_REPORT_SIZE_MACRO_BANK2];
} __attribute__((packed));

_Static_assert(sizeof(struct _roccat_macro_combined) == ROCCAT_REPORT_SIZE_MACRO_BANK1+ROCCAT_REPORT_SIZE_MACRO_BANK2, "Size of roccat_macro_combined is wrong");

struct button {
	uint8_t keycode;
	uint16_t modifiers;
} __attribute__((packed));

struct roccat_buttons {
	uint8_t report_id;               // 0x07
	uint8_t report_data_length;    // 0x4b 75
	uint8_t profile;
	struct button keys[ROCCAT_BUTTON_MAX];
} __attribute__((packed));
_Static_assert(sizeof(struct roccat_buttons) == ROCCAT_REPORT_SIZE_BUTTONS, "Size of roccat_buttons is wrong");

struct roccat_data {
	struct roccat_buttons buttons[(ROCCAT_PROFILE_MAX)];
	struct roccat_settings_report settings[(ROCCAT_PROFILE_MAX)];
	union roccat_macro_combined macros[(ROCCAT_PROFILE_MAX)][(ROCCAT_BUTTON_MAX + 1)];
};

struct roccat_button_type_mapping {
	uint8_t raw;
	enum ratbag_button_type type;
};

static const struct roccat_button_type_mapping roccat_button_type_mapping[] = {
	{ 0, RATBAG_BUTTON_TYPE_LEFT },
	{ 1, RATBAG_BUTTON_TYPE_RIGHT },
	{ 2, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 3, RATBAG_BUTTON_TYPE_WHEEL_LEFT },
	{ 4, RATBAG_BUTTON_TYPE_WHEEL_RIGHT },
	{ 5, RATBAG_BUTTON_TYPE_WHEEL_UP },
	{ 6, RATBAG_BUTTON_TYPE_WHEEL_DOWN },
	{ 7, RATBAG_BUTTON_TYPE_EXTRA },
	{ 8, RATBAG_BUTTON_TYPE_SIDE },
	{ 9, RATBAG_BUTTON_TYPE_THUMB },
	{ 10, RATBAG_BUTTON_TYPE_RESOLUTION_UP },
	{ 11, RATBAG_BUTTON_TYPE_RESOLUTION_DOWN },

	// Easy Shift+, these buttons are not physical
	{ 12, RATBAG_BUTTON_TYPE_LEFT },
	{ 13, RATBAG_BUTTON_TYPE_RIGHT },
	{ 14, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 15, RATBAG_BUTTON_TYPE_WHEEL_LEFT },
	{ 16, RATBAG_BUTTON_TYPE_WHEEL_RIGHT },
	{ 17, RATBAG_BUTTON_TYPE_WHEEL_UP },
	{ 18, RATBAG_BUTTON_TYPE_WHEEL_DOWN },
	{ 19, RATBAG_BUTTON_TYPE_EXTRA },
	{ 20, RATBAG_BUTTON_TYPE_SIDE },
	{ 21, RATBAG_BUTTON_TYPE_THUMB },
	{ 22, RATBAG_BUTTON_TYPE_RESOLUTION_UP },
 	{ 23, RATBAG_BUTTON_TYPE_RESOLUTION_DOWN },
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
/* FIXME:   { 23, Toogle sensibility }, */
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
 * identifier when quering a macro. In this case, the first bank can be queried by adding 0x10 to the profile index,
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

static struct roccat_color
rtbg_to_rct_color(struct ratbag_color color)
{
	struct roccat_color rct_clr = {};
	rct_clr.red = color.red;
	rct_clr.green = color.green;
	rct_clr.blue = color.blue;
	rct_clr.intensity = 255;

	return rct_clr;
}

static struct ratbag_color
rct_to_rtbg_color(struct roccat_color color)
{
	struct ratbag_color rtbg_clr = {};
	rtbg_clr.red = color.red;
	rtbg_clr.green = color.green;
	rtbg_clr.blue = color.blue;

	return rtbg_clr;
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
	union roccat_macro_combined* macro;
	int rc = 0;
	int i = 0, count = 0;


	assert(index <= ROCCAT_PROFILE_MAX);

	report = &drv_data->settings[profile->index];
	report->report_id = ROCCAT_REPORT_ID_SETTINGS;
	report->report_data_length = ROCCAT_SETTINGS_DATA_LENGTH;
	report->report_rate = roccat_report_rate_to_index(profile->hz);

	ratbag_profile_for_each_resolution(profile, resolution) {
		report->xres[resolution->index] = (resolution->dpi_x - 100) / 100;
		report->yres[resolution->index] = (resolution->dpi_y - 100) / 100;

		if(resolution->is_active) {
			report->current_dpi = resolution->index;
		}
	}

	ratbag_profile_for_each_led(profile, led) {
		switch (led->index) {
		case 0:
			report->led_scrollwheel_color = rtbg_to_rct_color(led->color);
			break;
		case 1:
			report->led_leftstrip_color_1 = rtbg_to_rct_color(led->color);
			break;
		case 2:
			report->led_leftstrip_color_2 = rtbg_to_rct_color(led->color);
			break;
		case 3:
			report->led_leftstrip_color_3 = rtbg_to_rct_color(led->color);
			break;
		case 4:
			report->led_leftstrip_color_4 = rtbg_to_rct_color(led->color);
			break;
		case 5:
			report->led_rightstrip_color_1 = rtbg_to_rct_color(led->color);
			break;
		case 6:
			report->led_rightstrip_color_2 = rtbg_to_rct_color(led->color);
			break;
		case 7:
			report->led_rightstrip_color_3 = rtbg_to_rct_color(led->color);
			break;
		case 8:
			report->led_rightstrip_color_4 = rtbg_to_rct_color(led->color);
			break;
		case 9:
			report->led_leftblob_color = rtbg_to_rct_color(led->color);
			break;
		case 10:
			report->led_rightblob_color = rtbg_to_rct_color(led->color);
			break;
		}

		// Last LED sets the profile values
		switch(led->mode) {
			case RATBAG_LED_OFF:
				report->brightness.intensity = 0x00;
				break;
			case RATBAG_LED_ON:
				report->brightness.intensity = 0xff;
				report->lighting_effect = ROCCAT_LED_FIXED;
				break;
			case RATBAG_LED_CYCLE:
				report->brightness.intensity = 0xff;
				report->lighting_effect = ROCCAT_LED_WAVE;
				report->lighting_effect_speed = led->ms / 1000;
				break;
			case RATBAG_LED_BREATHING:
				report->brightness.intensity = 0xff;
				report->lighting_effect = ROCCAT_LED_BREATHING;
				report->lighting_effect_speed = led->ms / 1000;
		}
	}
	report->checksum = roccat_compute_crc((uint8_t*)report, ROCCAT_REPORT_SIZE_SETTINGS);


	buttons = &drv_data->buttons[profile->index];
	buttons->report_id = ROCCAT_REPORT_ID_KEY_MAPPING;
	buttons->report_data_length = ROCCAT_KEY_MAPPING_DATA_LENGTH;
	ratbag_profile_for_each_button(profile, button) {
		buttons->keys[button->index].keycode = roccat_button_action_to_raw(&button->action);
		if(button->action.type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
			macro = &drv_data->macros[profile->index][button->index];
			memset(macro, 0, sizeof(union roccat_macro_combined));

			struct _roccat_macro_bank1* bank1 = &macro->msg.bank1.msg;
			struct _roccat_macro_bank2* bank2 = &macro->msg.bank2.msg;

			bank1->report_id = ROCCAT_REPORT_ID_MACRO;
			bank1->bank = ROCCAT_BANK_ID_1;
			bank1->profile = profile->index;
			bank1->button_index = button->index;
			bank1->repeat = 0; // No repeats in libratbag

			if(button->action.macro->group) {
				// Seems no use of group in libratbag
				strncpy(bank1->group, button->action.macro->group, ROCCAT_MACRO_GROUP_NAME_LENGTH);
			} else {
				strncpy(bank1->group, "libratbag macros", ROCCAT_MACRO_GROUP_NAME_LENGTH);
			}
			strncpy(bank1->name, button->action.macro->name, ROCCAT_MACRO_NAME_LENGTH);

			for (i = 0; i < MAX_MACRO_EVENTS && count < ROCCAT_MACRO_BANK1_KEYS_LENGTH; i++) {
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
					bank1->keys[count].keycode = ratbag_hidraw_get_keyboard_usage_from_keycode(device, button->action.macro->events[i].event.key);
				}

				switch (button->action.macro->events[i].type) {
				case RATBAG_MACRO_EVENT_KEY_PRESSED:
					bank1->keys[count].flag = 0x01;
					break;
				case RATBAG_MACRO_EVENT_KEY_RELEASED:
					bank1->keys[count].flag = 0x02;
					break;
				case RATBAG_MACRO_EVENT_WAIT:
					bank1->keys[--count].time = button->action.macro->events[i].event.timeout;
					break;
				case RATBAG_MACRO_EVENT_INVALID:
				case RATBAG_MACRO_EVENT_NONE:
					/* should not happen */
					log_error(device->ratbag,
						"something went wrong while writing a macro.\n");
				}
				count++;
			}
			for (i = 0; i < MAX_MACRO_EVENTS-count && count < ROCCAT_MAX_MACRO_LENGTH; i++) {
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
					bank2->keys[count].keycode = ratbag_hidraw_get_keyboard_usage_from_keycode(device, button->action.macro->events[i].event.key);
				}

				switch (button->action.macro->events[i].type) {
				case RATBAG_MACRO_EVENT_KEY_PRESSED:
					bank2->keys[count].flag = 0x01;
					break;
				case RATBAG_MACRO_EVENT_KEY_RELEASED:
					bank2->keys[count].flag = 0x02;
					break;
				case RATBAG_MACRO_EVENT_WAIT:
					bank2->keys[--count].time = button->action.macro->events[i].event.timeout;
					break;
				case RATBAG_MACRO_EVENT_INVALID:
				case RATBAG_MACRO_EVENT_NONE:
					/* should not happen */
					log_error(device->ratbag,
						"something went wrong while writing a macro.\n");
				}
				count++;
			}
			bank1->length = count;


			// Macro has to be send in two packets
			rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_MACRO,
						  macro->msg.bank1.data, ROCCAT_REPORT_SIZE_MACRO_BANK1);
			if (rc < 0)
				return rc;

			if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK1)
				return -EIO;

			rc = roccat_wait_ready(device);
			if (rc)
				log_error(device->ratbag,
					"Error while waiting for the device to be ready: %s (%d)\n",
					strerror(-rc), rc);

			bank2->report_id = ROCCAT_REPORT_ID_MACRO;
			bank2->bank = ROCCAT_BANK_ID_2;
			bank2->checksum = roccat_compute_crc(macro->data, ROCCAT_REPORT_SIZE_MACRO_BANK1 + ROCCAT_REPORT_SIZE_MACRO_BANK2);

			uint8_t *data = (uint8_t*)zalloc(ROCCAT_REPORT_SIZE_MACRO_BANK2+1);
			memcpy(data, macro->msg.bank2.data, ROCCAT_REPORT_SIZE_MACRO_BANK2);
			data[ROCCAT_REPORT_SIZE_MACRO_BANK2] = ROCCAT_MACRO_BANK2_TERMINATOR;

			rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_MACRO,
				data, ROCCAT_REPORT_SIZE_MACRO_BANK2);
			if (rc < 0)
				return rc;

			if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK2)
				return -EIO;

			rc = roccat_wait_ready(device);
			if (rc)
				log_error(device->ratbag,
					"Error while waiting for the device to be ready: %s (%d)\n",
					strerror(-rc), rc);
		}
	}
	// No checksum for buttons
	//buttons->checksum = roccat_compute_crc((uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS);


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

static void roccat_read_macro(union roccat_macro_combined* macro, struct ratbag_button* button) {
	struct ratbag_button_macro *m = NULL;
	unsigned j, time;

	struct _roccat_macro_bank1* bank1 = &macro->msg.bank1.msg;
	struct _roccat_macro_bank2* bank2 = &macro->msg.bank2.msg;

	char name[ROCCAT_MACRO_NAME_LENGTH+1] = { '\0' };
	strncpy(name, bank1->name, ROCCAT_MACRO_NAME_LENGTH);

	m = ratbag_button_macro_new(name);
	// libratbag does offer API for macro groups
	m->macro.group = (char*)zalloc(ROCCAT_MACRO_GROUP_NAME_LENGTH+1);
	strncpy(m->macro.group, bank1->group, ROCCAT_MACRO_GROUP_NAME_LENGTH);

	log_debug(button->profile->device->ratbag,
		"macro on button %d of profile %d is named '%s' (from folder '%s'), and contains %d events:\n",
		button->index, button->profile->index,
		name, m->macro.group, bank1->length);
	// libratbag can't keep track of the whole macro (MAX_MACRO_EVENTS)
	// In libratbag, each event is implemented as two separate (KEY_PRESS/KEY_RELEASE and WAIT)
	for (j = 0; j < bank1->length && j < MAX_MACRO_EVENTS/2; j++) {
		unsigned int keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(button->profile->device,
						bank1->keys[j].keycode);
		ratbag_button_macro_set_event(m,
							j * 2,
							bank1->keys[j].flag & 0x01 ? RATBAG_MACRO_EVENT_KEY_PRESSED : RATBAG_MACRO_EVENT_KEY_RELEASED,
							keycode);
		if (bank1->keys[j].time)
			time = bank1->keys[j].time;
		else
			time = bank1->keys[j].flag & 0x01 ? 10 : 50;
		ratbag_button_macro_set_event(m,
							j * 2 + 1,
							RATBAG_MACRO_EVENT_WAIT,
							time);

		log_debug(button->profile->device->ratbag,
			"    - %s %s\n",
			libevdev_event_code_get_name(EV_KEY, keycode),
			bank1->keys[j].flag == 0x02 ? "released" : "pressed");
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
	union roccat_macro_combined *macro;
	int rc;

	action = roccat_button_to_action(button->profile, button->index);
	if (action)
		ratbag_button_set_action(button, action);
	button->type = roccat_raw_to_button_type(button->index);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	log_debug(device->ratbag, "reading button %d key %d on action button %d, with special %d\n", button->index, button->action.action.key.key, button->action.action.button, button->action.action.special);
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
							  (uint8_t*)macro->msg.bank2.data, ROCCAT_REPORT_SIZE_MACRO_BANK1);
		if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK1) {
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
							  (uint8_t*)macro->msg.bank1.data, ROCCAT_REPORT_SIZE_MACRO_BANK1);
		if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK1) {
			log_error(device->ratbag,
				  "Unable to retrieve the first bank for macro for button %d of profile %d: %s (%d)\n",
				  button->index, button->profile->index,
				  rc < 0 ? strerror(-rc) : "not read enough", rc);
			goto out_macro;
		}

		if (macro->msg.bank1.msg.report_id != ROCCAT_REPORT_ID_MACRO) {
			log_error(device->ratbag,
					"Error while reading the macro of button %d of profile %d.\n",
					button->index,
					button->profile->index);
			goto out_macro;
		}

		roccat_crc_is_valid(device, (uint8_t*)macro, ROCCAT_REPORT_SIZE_MACRO_BANK1 + ROCCAT_REPORT_SIZE_MACRO_BANK2);

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
	ratbag_profile_set_report_rate(profile, report_rate);

	ratbag_profile_for_each_resolution(profile, resolution) {
		dpi_x = settings->xres[resolution->index] * 50;
		dpi_y = settings->yres[resolution->index] * 50;
		resolution->is_active = (resolution->index == settings->current_dpi);

		ratbag_resolution_set_resolution(resolution, dpi_x, dpi_y);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

		ratbag_resolution_set_dpi_list_from_range(resolution, ROCCAT_MIN_DPI, ROCCAT_MAX_DPI);
	}
}

static void
roccat_read_led(struct roccat_settings_report* settings, struct ratbag_led *led)
{
	led->type = RATBAG_LED_TYPE_SIDE;
	if(settings->brightness.intensity == 0) {
		led->mode = RATBAG_LED_OFF;
	} else {
		led->mode = RATBAG_LED_ON;
	}
	if(settings->lighting_effect == ROCCAT_LED_WAVE) {
		led->mode = RATBAG_LED_CYCLE;
		led->ms = settings->lighting_effect_speed * 1000;
	}
	if(settings->lighting_effect == ROCCAT_LED_BREATHING) {
		led->mode = RATBAG_LED_BREATHING;
		led->ms = settings->lighting_effect_speed * 1000;
	}

	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;

	switch (led->index) {
	case 0:
		led->color = rct_to_rtbg_color(settings->led_scrollwheel_color);
		break;
	case 1:
		led->color = rct_to_rtbg_color(settings->led_leftstrip_color_1);
		break;
	case 2:
		led->color = rct_to_rtbg_color(settings->led_leftstrip_color_2);
		break;
	case 3:
		led->color = rct_to_rtbg_color(settings->led_leftstrip_color_3);
		break;
	case 4:
		led->color = rct_to_rtbg_color(settings->led_leftstrip_color_4);
		break;
	case 5:
		led->color = rct_to_rtbg_color(settings->led_rightstrip_color_1);
		break;
	case 6:
		led->color = rct_to_rtbg_color(settings->led_rightstrip_color_2);
		break;
	case 7:
		led->color = rct_to_rtbg_color(settings->led_rightstrip_color_3);
		break;
	case 8:
		led->color = rct_to_rtbg_color(settings->led_rightstrip_color_4);
		break;
	case 9:
		led->color = rct_to_rtbg_color(settings->led_leftblob_color);
		break;
	case 10:
		led->color = rct_to_rtbg_color(settings->led_rightblob_color);
		break;
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

	// No Checksum for buttons
	// if (!roccat_crc_is_valid(device, (uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS)) {
	// 	log_error(device->ratbag,
	// 		  "Error while reading buttons from profile %d, checksum invalid, continuing...\n",
	// 		  profile->index);
	// }

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

struct ratbag_driver roccat_kone_aimo_driver = {
	.name = "Roccat Kone AIMO",
	.id = "roccat-kone-aimo",
	.probe = roccat_probe,
	.remove = roccat_remove,
	.commit = roccat_commit,
	.set_active_profile = roccat_set_current_profile,
};
