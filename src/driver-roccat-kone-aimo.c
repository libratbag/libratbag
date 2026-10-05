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

#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "shared-macro.h"

#define ROCCAT_PROFILE_MAX          5
#define ROCCAT_BUTTON_MAX           (12 * 2) /* 24 buttons total, 12 + 12 Easy Shift */
#define ROCCAT_NUM_DPI              5
#define ROCCAT_LED_MAX              11

#define ROCCAT_MAX_RETRY_READY      10

#define ROCCAT_REPORT_ID_CONFIGURE_PROFILE  4
#define ROCCAT_REPORT_ID_PROFILE            5
#define ROCCAT_REPORT_ID_SETTINGS           6
#define ROCCAT_REPORT_ID_KEY_MAPPING        7
#define ROCCAT_REPORT_ID_MACRO              8
#define ROCCAT_REPORT_ID_ACTIVATE           0x0e

#define ROCCAT_REPORT_SIZE_BUTTONS          75
#define ROCCAT_REPORT_SIZE_SETTINGS         126

#define ROCCAT_BANK_ID_1    1
#define ROCCAT_BANK_ID_2    2

/* When querying macro data, the profile byte in roccat_set_config_profile is
 * offset by these values to select the macro bank: profile + offset, button. */
#define ROCCAT_MACRO_BANK1_PROFILE_OFFSET  0x10
#define ROCCAT_MACRO_BANK2_PROFILE_OFFSET  0x20
#define ROCCAT_REPORT_SIZE_MACRO_BANK1       1026
#define ROCCAT_REPORT_SIZE_MACRO_BANK2       977
#define ROCCAT_MACRO_BANK1_KEYS_LENGTH      237
#define ROCCAT_MACRO_BANK2_KEYS_LENGTH      243
#define ROCCAT_MACRO_BANK2_TERMINATOR       0x4A

#define ROCCAT_MACRO_GROUP_NAME_LENGTH  40
#define ROCCAT_MACRO_NAME_LENGTH        32

#define ROCCAT_CONFIG_SETTINGS      0x80 /* LED and mouse configuration */
#define ROCCAT_CONFIG_KEY_MAPPING   0x90 /* Buttons configuration */

#define ROCCAT_MIN_DPI  100
#define ROCCAT_MAX_DPI  16000

/* Default values observed from ROCCAT Swarm software */
#define ROCCAT_DEFAULT_XY_LINKED        0x0e  /* X/Y DPI linked */
#define ROCCAT_DEFAULT_EFFECT_SPEED     0x02  /* Medium speed */
#define ROCCAT_DEFAULT_UNKNOWN2         0x01  /* Required by firmware */
#define ROCCAT_DEFAULT_CUSTOM_MODE      0x05  /* Custom colour mode */
#define ROCCAT_DEFAULT_DPI_RAW          0x10  /* 800 DPI (0x10 * 50 = 800) */

#define CLAMP(val, lo, hi) ((val) < (lo) ? (lo) : ((val) > (hi) ? (hi) : (val)))

#define ROCCAT_BUTTON_SHORTCUT      0x05 /* modifier + key combination */

#define ROCCAT_LED_OFF              0x00
#define ROCCAT_LED_FIXED            0x01 /* solid color, confirmed */
#define ROCCAT_LED_BLINKING         0x02 /* on/off strobe */
#define ROCCAT_LED_BREATHING        0x03 /* fade in/out, confirmed */
#define ROCCAT_LED_BEATING          0x04 /* heartbeat pulse */
#define ROCCAT_LED_WAVE             0x07 /* rainbow color cycle, confirmed */

static const unsigned int report_rates[] = { 125, 250, 500, 1000 };

struct roccat_color {
	uint8_t intensity;
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t padding;
} __attribute__((packed));

struct roccat_settings_report {
	uint8_t report_id;                          /* 0x06 */
	uint8_t report_data_length;                 /* 126 for settings */
	uint8_t profile;                            /* 5 Profiles, 0-4 */
	uint8_t sensitivity;                        /* 0x06 means 0 because its -5 to 5 in the UI, so 1-11 */
	uint8_t x_y_linked;                         /* Set X and Y dpi separately, but its not a feature in ROCCAT Swarm, default `0E` */
	uint8_t current_dpi;                        /* 0-4 for the 5 resolutions */
	uint16_t xres[ROCCAT_NUM_DPI];              /* 5 resolutions saved to switch between. value * 50 = DPI */
	uint16_t yres[ROCCAT_NUM_DPI];              /* 5 resolutions saved to switch between. value * 50 = DPI */
	uint8_t report_rate;                        /* 0 = 125 hz, 1 = 250hz, 2 = 500hz, 3 = 1000hz */
	uint8_t angle_snapping;                     /* 0 = off, 1 = on */
	struct roccat_color unk_color1;             /* 08 FF 07 00 not sure, looks like some kind of 0 intensity blue. Nothing in the official software changes it. */
	uint8_t lighting_effect;                    /* From 0x01 to 0x04 : fixed, blinking, breathing, beating */
	uint8_t lighting_effect_speed;              /* From 0x01 to 0x03 */
	struct roccat_color brightness;             /* intensity is global brightness, color is unused but `1D 13 FF` default. */
	struct roccat_color unk_color2;             /* FF 59 FF 00, looks like some kind of default gradient, might be AIMO gradient? */
	struct roccat_color unk_color3;             /* FF FD FD 00 */
	struct roccat_color unk_color4;             /* FF F4 64 00 */
	struct roccat_color unk_color5;             /* FF F4 00 00 */
	uint8_t unknown1;                           /* FF default, doesn't change by any UI settings. */
	struct roccat_color led_scrollwheel_color;  /* only solid */
	struct roccat_color led_leftstrip_color_1;  /* 4 color gradient. Solid is just all same color */
	struct roccat_color led_leftstrip_color_2;
	struct roccat_color led_leftstrip_color_3;
	struct roccat_color led_leftstrip_color_4;
	struct roccat_color led_rightstrip_color_1; /* 4 color gradient. Solid is just all same color */
	struct roccat_color led_rightstrip_color_2;
	struct roccat_color led_rightstrip_color_3;
	struct roccat_color led_rightstrip_color_4;
	struct roccat_color led_leftblob_color;    /* only solid */
	struct roccat_color led_rightblob_color;   /* only solid */
	uint8_t custom_or_theme;                   /* 00-09 for custom based on theme, theme is 80-89 */
	uint8_t unknown2;                          /* 01 default, doesn't change by any UI settings. */
	uint8_t padding[6];
	uint16_t checksum;
} __attribute__((packed));
_Static_assert(sizeof(struct roccat_settings_report) == ROCCAT_REPORT_SIZE_SETTINGS, "Size of roccat_settings_report is wrong");

struct roccat_macro_keys {
	uint8_t keycode;
	uint8_t flag;  /* 0x01 = press, 0x02 = release */
	uint16_t time; /* Fixed Delay in milliseconds */
} __attribute__((packed));

struct last_macro_key {
	uint8_t keycode;
	uint8_t flag; /* 0x01 = press, 0x02 = release */
	uint8_t first_half_time; /* reserved; the bank-1/bank-2 time split is not implemented */
} __attribute__((packed));

struct _roccat_macro_bank1 {
	uint8_t report_id;
	uint8_t bank;
	uint8_t profile;
	uint8_t button_index;
	uint8_t repeat; /* number of times to repeat the macro sequence */
	char group[40]; /* Max 40 characters for the group/folder name */
	char name[32];  /* Max 32 characters for the macro name */
	uint16_t length; /* OR'd with On Press = 0x0000, While Press = 0x0010, Macro toggle = 0x0020 */
	struct roccat_macro_keys keys[ROCCAT_MACRO_BANK1_KEYS_LENGTH-1];
	struct last_macro_key last_key; /* reserved; see first_half_time note above */
} __attribute__((packed));

union roccat_macro_bank1 {
	struct _roccat_macro_bank1 msg;
	uint8_t data[ROCCAT_REPORT_SIZE_MACRO_BANK1];
};
_Static_assert(sizeof(struct _roccat_macro_bank1) == ROCCAT_REPORT_SIZE_MACRO_BANK1, "Size of roccat_macro_bank1 is wrong");

struct _roccat_macro_bank2 {
	uint8_t report_id;
	uint8_t bank;
	uint8_t second_half_time; /* reserved; the bank-1/bank-2 time split is not implemented */
	struct roccat_macro_keys keys[ROCCAT_MACRO_BANK2_KEYS_LENGTH];
	uint16_t checksum;  /* This is the checksum of both pages of keys. */
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
	uint8_t report_id;             /* 0x07 */
	uint8_t report_data_length;    /* 0x4b 75 */
	uint8_t profile;
	struct button keys[ROCCAT_BUTTON_MAX];
} __attribute__((packed));
_Static_assert(sizeof(struct roccat_buttons) == ROCCAT_REPORT_SIZE_BUTTONS, "Size of roccat_buttons is wrong");

struct roccat_data {
	struct roccat_buttons buttons[ROCCAT_PROFILE_MAX];
	struct roccat_settings_report settings[ROCCAT_PROFILE_MAX];
	union roccat_macro_combined macros[ROCCAT_PROFILE_MAX][ROCCAT_BUTTON_MAX + 1];
};

struct roccat_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

/*
 * Raw value 0x05 (ROCCAT_BUTTON_SHORTCUT) is intentionally absent: it encodes a
 * modifier+key shortcut and is handled separately in the read/write paths.
 *
 * The firmware supports many more raw codes (EasyAim, EasyWheel, sensitivity
 * steps, browser/media/system shortcuts, profile selection, ...). They are not
 * mapped here because libratbag has no matching actions; see the ROCCAT Swarm
 * USB captures for the full list.
 */
static const struct roccat_button_mapping roccat_button_mapping[] = {
	{ 0, BUTTON_ACTION_NONE },
	{ 1, BUTTON_ACTION_BUTTON(1) },
	{ 2, BUTTON_ACTION_BUTTON(2) },
	{ 3, BUTTON_ACTION_BUTTON(3) },
	{ 4, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },
	{ 7, BUTTON_ACTION_BUTTON(4) }, /* Next page in browser */
	{ 8, BUTTON_ACTION_BUTTON(5) }, /* Previous page in browser */
	{ 9, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT) },
	{ 10, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT) },
	{ 13, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ 14, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
	{ 16, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ 17, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ 18, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
	{ 20, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 21, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 22, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 33, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ 34, BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ 35, BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ 36, BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ 37, BUTTON_ACTION_KEY(KEY_MUTE) },
	{ 38, BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ 39, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },
	{ 48, BUTTON_ACTION_MACRO },
	{ 65, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE) },
};

static const struct ratbag_button_action*
roccat_raw_to_button_action(uint8_t data)
{
	const struct roccat_button_mapping *mapping;

	ARRAY_FOR_EACH(roccat_button_mapping, mapping) {
		if (mapping->raw == data)
			return &mapping->action;
	}

	return NULL;
}

static uint8_t
roccat_button_action_to_raw(const struct ratbag_button_action *action)
{
	const struct roccat_button_mapping *mapping;

	ARRAY_FOR_EACH(roccat_button_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->raw;
	}

	return 0;
}

/*
 * Mapping between HID modifier bits and libratbag modifier masks.
 * HID modifier bits: 0=LCtrl 1=LShift 2=LAlt 3=LMeta 4=RCtrl 5=RShift 6=RAlt 7=RMeta
 */
static const unsigned int hid_to_ratbag_modifier[] = {
	MODIFIER_LEFTCTRL, MODIFIER_LEFTSHIFT, MODIFIER_LEFTALT, MODIFIER_LEFTMETA,
	MODIFIER_RIGHTCTRL, MODIFIER_RIGHTSHIFT, MODIFIER_RIGHTALT, MODIFIER_RIGHTMETA,
};

static uint8_t
roccat_hid_modifiers_from_ratbag(unsigned int modifiers)
{
	uint8_t hid = 0;

	for (unsigned int i = 0; i < ARRAY_LENGTH(hid_to_ratbag_modifier); i++) {
		if (modifiers & hid_to_ratbag_modifier[i])
			hid |= 1 << i;
	}

	return hid;
}

static unsigned int
roccat_hid_modifiers_to_ratbag(uint8_t hid)
{
	unsigned int modifiers = 0;

	for (unsigned int i = 0; i < ARRAY_LENGTH(hid_to_ratbag_modifier); i++) {
		if (hid & (1 << i))
			modifiers |= hid_to_ratbag_modifier[i];
	}

	return modifiers;
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

		/* Status 0x02 appears to be normal/expected on Kone Aimo.
		 * Treat it as success rather than propagating it as an error. */
		if (rc == 2)
			return 0;

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
	uint8_t buf[] = {ROCCAT_REPORT_ID_PROFILE, 0x03, index};
	int ret;

	if (index >= ROCCAT_PROFILE_MAX)
		return -EINVAL;

	log_debug(device->ratbag,
		"'%s' Setting profile %d as active\n",
		ratbag_device_get_name(device),
		index);

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
 * Sets the profile and which information we want to get from the mouse.
 *
 * @param profile the index of the profile (0–4) for normal settings/key-mapping
 *   queries. When reading macro data, pass profile_index +
 *   ROCCAT_MACRO_BANK1_PROFILE_OFFSET for bank 1 or +
 *   ROCCAT_MACRO_BANK2_PROFILE_OFFSET for bank 2.
 * @param type ROCCAT_CONFIG_SETTINGS or ROCCAT_CONFIG_KEY_MAPPING for normal
 *   queries; the button index when querying macro data.
 */
static int
roccat_set_config_profile(struct ratbag_device *device, uint8_t profile, uint8_t type)
{
	uint8_t buf[] = {ROCCAT_REPORT_ID_CONFIGURE_PROFILE, profile, type};
	int ret;

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

/*
 * Default button layout as written by ROCCAT Swarm (observed from profile 0).
 * Slots 3-6 are fixed hardware actions (scroll wheel left/right/up/down) that
 * the firmware requires to be non-zero for the scroll wheel to function.
 * A value of 0 means "disabled/none" and will break scrolling on profiles
 * that were never initialised by Swarm.
 */
static const uint8_t roccat_default_button_keycodes[] = {
	0x01, /* [0]  left click */
	0x02, /* [1]  right click */
	0x03, /* [2]  middle click */
	0x09, /* [3]  wheel left */
	0x0a, /* [4]  wheel right */
	0x0d, /* [5]  wheel up */
	0x0e, /* [6]  wheel down */
	0x07, /* [7]  browser forward */
	0x08, /* [8]  browser back */
	0x41, /* [9]  easy shift */
	0x10, /* [10] profile cycle up */
};

/*
 * Apply the observed Swarm default keycodes to any zero slot before handing
 * the button data to the ratbag layer.  Profiles that were never written by
 * Swarm have all-zero button reports; without this, ratbag stores
 * BUTTON_ACTION_NONE for slots like wheel-up/down and writes zeros back,
 * breaking scrolling.
 */
static void
roccat_apply_button_defaults(struct roccat_buttons *buttons)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(roccat_default_button_keycodes); i++) {
		if (buttons->keys[i].keycode == 0x00)
			buttons->keys[i].keycode = roccat_default_button_keycodes[i];
	}
}

static unsigned int
roccat_report_rate_to_index(unsigned int rate)
{
	for (unsigned int i = 0; i < ARRAY_LENGTH(report_rates); i++) {
		if (report_rates[i] == rate)
			return i;
	}
	return 0;
}

static struct roccat_color
ratbag_to_roccat_color(struct ratbag_color color)
{
	struct roccat_color rct_clr = {};
	rct_clr.red = color.red;
	rct_clr.green = color.green;
	rct_clr.blue = color.blue;
	rct_clr.intensity = 255;

	return rct_clr;
}

static struct ratbag_color
roccat_to_ratbag_color(struct roccat_color color)
{
	struct ratbag_color rtbg_clr = {};
	rtbg_clr.red = color.red;
	rtbg_clr.green = color.green;
	rtbg_clr.blue = color.blue;

	return rtbg_clr;
}

/*
 * Return the colour slot in the settings report for a given LED zone index
 * (0-10), or NULL for an unknown index.
 */
static struct roccat_color *
roccat_led_color_slot(struct roccat_settings_report *report, unsigned int index)
{
	switch (index) {
	case 0:  return &report->led_scrollwheel_color;
	case 1:  return &report->led_leftstrip_color_1;
	case 2:  return &report->led_leftstrip_color_2;
	case 3:  return &report->led_leftstrip_color_3;
	case 4:  return &report->led_leftstrip_color_4;
	case 5:  return &report->led_rightstrip_color_1;
	case 6:  return &report->led_rightstrip_color_2;
	case 7:  return &report->led_rightstrip_color_3;
	case 8:  return &report->led_rightstrip_color_4;
	case 9:  return &report->led_leftblob_color;
	case 10: return &report->led_rightblob_color;
	default: return NULL;
	}
}

/**
 * Send the activate/commit command (report 0x0e) to make the device latch a
 * report that was just written.
 *
 * Captured from Roccat Swarm: after a SET_REPORT the host sends
 * {0x0e, <report_id>, 0x01, 0x00, 0x00, 0xff}, where <report_id> is the report
 * that was written (e.g. 0x06 for settings, 0x07 for the key mapping). Without
 * this the device keeps using the previously latched configuration.
 */
static int
roccat_activate(struct ratbag_device *device, uint8_t report_id)
{
	uint8_t buf[] = {ROCCAT_REPORT_ID_ACTIVATE, report_id, 0x01, 0x00, 0x00, 0xff};
	int ret;

	ret = ratbag_hidraw_set_feature_report(device, buf[0], buf, sizeof(buf));
	if (ret < 0)
		return ret;

	if (ret != (int)sizeof(buf))
		return -EIO;

	return roccat_wait_ready(device);
}

/**
 * Write a report to the device, wait for ready, and activate it.
 * Combines the common pattern: set_feature -> wait_ready -> activate.
 */
static int
roccat_write_report(struct ratbag_device *device, uint8_t report_id,
		    uint8_t *data, size_t size)
{
	int rc;

	rc = ratbag_hidraw_set_feature_report(device, report_id, data, size);
	if (rc < 0)
		return rc;

	if (rc != (int)size)
		return -EIO;

	rc = roccat_wait_ready(device);
	if (rc) {
		log_error(device->ratbag,
			  "Error while waiting for the device to be ready: %s (%d)\n",
			  strerror(-rc), rc);
	}

	rc = roccat_activate(device, report_id);
	if (rc) {
		log_error(device->ratbag,
			  "Error sending activate command: %s (%d)\n",
			  strerror(-rc), rc);
	}

	return rc;
}

/*
 * Fill the colour slots and the single global lighting effect of the settings
 * report from the profile's LEDs and the chosen master mode.
 */
static void
roccat_settings_set_leds(struct roccat_settings_report *report,
			 struct ratbag_profile *profile,
			 enum ratbag_led_mode master_mode)
{
	struct ratbag_led *led;
	struct ratbag_led *led0 = ratbag_profile_get_led(profile, 0);
	unsigned int i;

	/* Per-zone colours. In the OFF case they are zeroed again below. */
	ratbag_profile_for_each_led(profile, led) {
		struct roccat_color *slot = roccat_led_color_slot(report, led->index);
		if (slot)
			*slot = ratbag_to_roccat_color(led->color);
	}

	/* Use LED0's brightness if available, otherwise default to full brightness.
	 * The device has a single global brightness control for all LED zones. */
	uint8_t brightness = (led0 && led0->brightness > 0) ? led0->brightness : 0xff;

	switch (master_mode) {
	case RATBAG_LED_OFF:
		report->brightness.intensity = 0x00;
		report->lighting_effect = ROCCAT_LED_OFF;
		/* The device ignores lighting_effect=OFF and keeps displaying
		 * whatever is stored in the colour slots, so zero them all. */
		for (i = 0; i < ROCCAT_LED_MAX; i++) {
			struct roccat_color off = { 0 };
			*roccat_led_color_slot(report, i) = off;
		}
		break;
	case RATBAG_LED_ON:
		report->brightness.intensity = brightness;
		report->lighting_effect = ROCCAT_LED_FIXED;
		break;
	case RATBAG_LED_CYCLE:
		report->brightness.intensity = brightness;
		report->lighting_effect = ROCCAT_LED_WAVE;
		report->lighting_effect_speed = led0 ? led0->ms / 1000 : 2;
		break;
	case RATBAG_LED_BREATHING:
		report->brightness.intensity = brightness;
		report->lighting_effect = ROCCAT_LED_BREATHING;
		report->lighting_effect_speed = led0 ? led0->ms / 1000 : 2;
		break;
	}
}

/*
 * Encode a multi-key macro into the two-bank layout and write both banks to
 * the device. Single-key shortcuts are handled by the caller, not here.
 */
static int
roccat_write_macro(struct ratbag_device *device, struct ratbag_button *button)
{
	struct roccat_data *drv_data = ratbag_get_drv_data(device);
	union roccat_macro_combined *macro = &drv_data->macros[button->profile->index][button->index];
	struct ratbag_macro *src = button->action.macro;
	struct ratbag_macro_event *events = src->events;
	struct _roccat_macro_bank1 *bank1 = &macro->msg.bank1.msg;
	struct _roccat_macro_bank2 *bank2 = &macro->msg.bank2.msg;
	int total = 0;     /* key-events stored in bank 1 (also the bank1 index) */
	int bank2_idx = 0; /* key-events stored in bank 2 */
	int ev = 0;        /* index into the source events[] array */
	uint8_t *data;
	int rc;

	memset(macro, 0, sizeof(union roccat_macro_combined));

	bank1->report_id = ROCCAT_REPORT_ID_MACRO;
	bank1->bank = ROCCAT_BANK_ID_1;
	bank1->profile = button->profile->index;
	bank1->button_index = button->index;
	bank1->repeat = 0; /* libratbag does not support repeats */

	/* libratbag has no concept of macro groups/folders.
	 * Use sizeof()-1 to ensure null-termination since strncpy doesn't
	 * guarantee it when the source string is >= the limit. */
	memset(bank1->group, 0, sizeof(bank1->group));
	memset(bank1->name, 0, sizeof(bank1->name));
	if (src->group)
		strncpy(bank1->group, src->group, sizeof(bank1->group) - 1);
	else
		strncpy(bank1->group, "libratbag macros", sizeof(bank1->group) - 1);
	if (src->name)
		strncpy(bank1->name, src->name, sizeof(bank1->name) - 1);

	/* Fill bank 1: events 0 .. ROCCAT_MACRO_BANK1_KEYS_LENGTH-2 */
	for (; ev < MAX_MACRO_EVENTS && total < ROCCAT_MACRO_BANK1_KEYS_LENGTH - 1; ev++) {
		if (events[ev].type == RATBAG_MACRO_EVENT_INVALID)
			return -EINVAL; /* should not happen, ever */

		if (events[ev].type == RATBAG_MACRO_EVENT_NONE)
			goto done;

		/* Ignore leading WAIT events (no previous key to attach delay to) */
		if (events[ev].type == RATBAG_MACRO_EVENT_WAIT && total == 0)
			continue;

		if (events[ev].type == RATBAG_MACRO_EVENT_KEY_PRESSED ||
		    events[ev].type == RATBAG_MACRO_EVENT_KEY_RELEASED)
			bank1->keys[total].keycode = ratbag_hidraw_get_keyboard_usage_from_keycode(device, events[ev].event.key);

		switch (events[ev].type) {
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			bank1->keys[total].flag = 0x01;
			total++;
			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			bank1->keys[total].flag = 0x02;
			total++;
			break;
		case RATBAG_MACRO_EVENT_WAIT:
			/* Attach delay to the previous key event (total-1) */
			bank1->keys[total - 1].time = events[ev].event.timeout;
			break;
		case RATBAG_MACRO_EVENT_INVALID:
		case RATBAG_MACRO_EVENT_NONE:
			/* should not happen */
			log_error(device->ratbag,
				"something went wrong while writing a macro.\n");
			break;
		}
	}

	/* Fill bank 2: continue from where bank 1 left off. */
	for (; ev < MAX_MACRO_EVENTS && bank2_idx < ROCCAT_MACRO_BANK2_KEYS_LENGTH; ev++) {
		if (events[ev].type == RATBAG_MACRO_EVENT_INVALID)
			return -EINVAL; /* should not happen, ever */

		if (events[ev].type == RATBAG_MACRO_EVENT_NONE)
			break;

		/* Ignore leading WAIT events at bank 2 start (no previous key to attach delay to).
		 * Note: A WAIT at the boundary should ideally attach to the last bank1 key,
		 * but that would require cross-bank time handling which is not implemented. */
		if (events[ev].type == RATBAG_MACRO_EVENT_WAIT && bank2_idx == 0)
			continue;

		if (events[ev].type == RATBAG_MACRO_EVENT_KEY_PRESSED ||
		    events[ev].type == RATBAG_MACRO_EVENT_KEY_RELEASED)
			bank2->keys[bank2_idx].keycode = ratbag_hidraw_get_keyboard_usage_from_keycode(device, events[ev].event.key);

		switch (events[ev].type) {
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			bank2->keys[bank2_idx].flag = 0x01;
			bank2_idx++;
			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			bank2->keys[bank2_idx].flag = 0x02;
			bank2_idx++;
			break;
		case RATBAG_MACRO_EVENT_WAIT:
			/* Attach delay to the previous key event (bank2_idx-1) */
			bank2->keys[bank2_idx - 1].time = events[ev].event.timeout;
			break;
		case RATBAG_MACRO_EVENT_INVALID:
		case RATBAG_MACRO_EVENT_NONE:
			/* should not happen */
			log_error(device->ratbag,
				"something went wrong while writing a macro.\n");
			break;
		}
	}

done:
	bank1->length = total + bank2_idx;

	/* Macro is sent in two packets: bank 1, then bank 2. */
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

	/* Bank 2 requires a terminator byte appended after the struct
	 * payload, as captured from ROCCAT Swarm. */
	data = zalloc(ROCCAT_REPORT_SIZE_MACRO_BANK2 + 1);
	memcpy(data, macro->msg.bank2.data, ROCCAT_REPORT_SIZE_MACRO_BANK2);
	data[ROCCAT_REPORT_SIZE_MACRO_BANK2] = ROCCAT_MACRO_BANK2_TERMINATOR;

	rc = ratbag_hidraw_set_feature_report(device, ROCCAT_REPORT_ID_MACRO,
		data, ROCCAT_REPORT_SIZE_MACRO_BANK2 + 1);
	free(data);
	if (rc < 0)
		return rc;

	if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK2 + 1)
		return -EIO;

	rc = roccat_wait_ready(device);
	if (rc)
		log_error(device->ratbag,
			"Error while waiting for the device to be ready: %s (%d)\n",
			strerror(-rc), rc);

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
	int rc = 0;

	assert(index < ROCCAT_PROFILE_MAX);

	report = &drv_data->settings[profile->index];
	report->report_id = ROCCAT_REPORT_ID_SETTINGS;
	report->report_data_length = ROCCAT_REPORT_SIZE_SETTINGS;
	report->report_rate = roccat_report_rate_to_index(profile->hz);

	ratbag_profile_for_each_resolution(profile, resolution) {
		/* Clamp DPI to valid range; firmware misbehaves with zero. */
		unsigned int dpi_x = CLAMP(resolution->dpi_x, ROCCAT_MIN_DPI, ROCCAT_MAX_DPI);
		unsigned int dpi_y = CLAMP(resolution->dpi_y, ROCCAT_MIN_DPI, ROCCAT_MAX_DPI);

		report->xres[resolution->index] = dpi_x / 50;
		report->yres[resolution->index] = dpi_y / 50;

		if (resolution->is_active) {
			report->current_dpi = resolution->index;
		}
	}

	/* The hardware has a single global lighting effect shared by all LED
	 * zones. Use the mode of the first dirty LED, or LED 0's mode if no
	 * LED is dirty. */
	enum ratbag_led_mode master_mode = RATBAG_LED_ON;
	ratbag_profile_for_each_led(profile, led) {
		if (led->dirty || led->index == 0)
			master_mode = led->mode;
		if (led->dirty)
			break; /* use the first dirty LED's mode */
	}

	log_debug(device->ratbag, "profile %d: master_mode=%d\n",
		  profile->index, master_mode);

	roccat_settings_set_leds(report, profile, master_mode);

	report->checksum = roccat_compute_crc((uint8_t*)report, ROCCAT_REPORT_SIZE_SETTINGS);

	buttons = &drv_data->buttons[profile->index];
	buttons->report_id = ROCCAT_REPORT_ID_KEY_MAPPING;
	buttons->report_data_length = ROCCAT_REPORT_SIZE_BUTTONS;

	/* Always restore the fixed hardware slots from the observed default
	 * layout. These slots are not freely reassignable: the firmware
	 * requires specific raw values for scroll wheel and basic clicks to
	 * function.  A blank/unused profile on the device has all zeros here,
	 * which disables scrolling entirely. */
	for (unsigned int di = 0; di < ARRAY_LENGTH(roccat_default_button_keycodes); di++)
		buttons->keys[di].keycode = roccat_default_button_keycodes[di];

	ratbag_profile_for_each_button(profile, button) {
		struct button *slot = &buttons->keys[button->index];

		slot->keycode = roccat_button_action_to_raw(&button->action);
		slot->modifiers = 0;

		if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_KEY) {
			/* A plain key action (piper "send keystroke") is encoded as
			 * ROCCAT_BUTTON_SHORTCUT with no modifier flags. */
			uint8_t hid_usage = ratbag_hidraw_get_keyboard_usage_from_keycode(device, button->action.action.key);
			if (hid_usage) {
				slot->keycode   = ROCCAT_BUTTON_SHORTCUT;
				slot->modifiers = (uint16_t)hid_usage << 8;
			}
			continue;
		}

		if (button->action.type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
			unsigned int key, modifiers;

			/* A single-key (shortcut) action is stored directly in the
			 * button slot as keycode=0x05 with the HID modifier flags in
			 * the low byte and the HID key usage in the high byte of the
			 * modifiers field.  Multi-key sequences use the macro path. */
			if (ratbag_action_keycode_from_macro(&button->action, &key, &modifiers) == 0) {
				uint8_t hid_usage = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
				if (hid_usage) {
					slot->keycode = ROCCAT_BUTTON_SHORTCUT;
					slot->modifiers = roccat_hid_modifiers_from_ratbag(modifiers) | ((uint16_t)hid_usage << 8);
					continue;
				}
			}

			rc = roccat_write_macro(device, button);
			if (rc < 0)
				return rc;
		}
	}

/* Note: there is no checksum for the buttons/key-mapping report. */
	rc = roccat_set_config_profile(device, index, ROCCAT_CONFIG_SETTINGS);
	if (rc < 0)
		return rc;

	rc = roccat_write_report(device, ROCCAT_REPORT_ID_SETTINGS,
				 (uint8_t *)report, ROCCAT_REPORT_SIZE_SETTINGS);
	if (rc)
		return rc;

	rc = roccat_set_config_profile(device, index, ROCCAT_CONFIG_KEY_MAPPING);
	if (rc < 0)
		return rc;

	rc = roccat_write_report(device, ROCCAT_REPORT_ID_KEY_MAPPING,
				 (uint8_t *)buttons, ROCCAT_REPORT_SIZE_BUTTONS);

	return rc;
}

static void roccat_read_macro(union roccat_macro_combined* macro, struct ratbag_button* button) {
	struct ratbag_button_macro *m = NULL;
	unsigned j, time;

	struct _roccat_macro_bank1* bank1 = &macro->msg.bank1.msg;

	char name[ROCCAT_MACRO_NAME_LENGTH + 1] = { '\0' };
	strncpy(name, bank1->name, ROCCAT_MACRO_NAME_LENGTH);

	m = ratbag_button_macro_new(name);
	if (!m) {
		log_error(button->profile->device->ratbag,
			  "Failed to allocate macro for button %d\n",
			  button->index);
		return;
	}

	/* libratbag does offer API for macro groups */
	m->macro.group = (char*)zalloc(ROCCAT_MACRO_GROUP_NAME_LENGTH + 1);
	if (!m->macro.group) {
		log_error(button->profile->device->ratbag,
			  "Failed to allocate macro group for button %d\n",
			  button->index);
		ratbag_button_macro_unref(m);
		return;
	}
	strncpy(m->macro.group, bank1->group, ROCCAT_MACRO_GROUP_NAME_LENGTH);

	log_debug(button->profile->device->ratbag,
		"macro on button %d of profile %d is named '%s' (from folder '%s'), and contains %d events:\n",
		button->index, button->profile->index,
		name, m->macro.group, bank1->length);

	/* Only bank-1 keys are decoded here. Bank-2 keys (events beyond
	 * ROCCAT_MACRO_BANK1_KEYS_LENGTH-1) are not loaded because libratbag's
	 * MAX_MACRO_EVENTS is the binding limit anyway. Each device event maps
	 * to two libratbag events (KEY_PRESS/KEY_RELEASE + WAIT), so the cap is
	 * MAX_MACRO_EVENTS/2. The extra bound on ROCCAT_MACRO_BANK1_KEYS_LENGTH-1
	 * guards against bank1->length reporting a total count that exceeds the
	 * bank-1 array size. */
	for (j = 0; j < bank1->length && j < MAX_MACRO_EVENTS/2 && j < ROCCAT_MACRO_BANK1_KEYS_LENGTH - 1; j++) {
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

	log_debug(device->ratbag, "reading button %d\n", button->index);

	action = roccat_button_to_action(button->profile, button->index);
	if (action) {
		ratbag_button_set_action(button, action);
	} else {
		/* Check for shortcut (key + modifier) action */
		struct button *slot = &drv_data->buttons[button->profile->index].keys[button->index];
		if (slot->keycode == ROCCAT_BUTTON_SHORTCUT) {
			uint8_t hid_modifier = slot->modifiers & 0xff;
			uint8_t hid_usage    = (slot->modifiers >> 8) & 0xff;
			unsigned int keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(device, hid_usage);
			if (keycode) {
				unsigned int modifiers = roccat_hid_modifiers_to_ratbag(hid_modifier);
				ratbag_button_macro_new_from_keycode(button, keycode, modifiers);
			}
		}
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	if (action && action->type == RATBAG_BUTTON_ACTION_TYPE_MACRO) {
		macro = &drv_data->macros[button->profile->index][button->index];

		/* Macros are read in two packets. Bank 2 is read first so that
		 * the bank-1 read (which follows) overwrites the report_id byte
		 * at the start of the combined buffer with the correct value. */
		roccat_set_config_profile(device,
				  button->profile->index,
				  0);
		roccat_set_config_profile(device,
				  button->profile->index + ROCCAT_MACRO_BANK2_PROFILE_OFFSET,
				  button->index);

		rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_MACRO,
						      (uint8_t*)macro->msg.bank2.data, ROCCAT_REPORT_SIZE_MACRO_BANK2);
		if (rc != ROCCAT_REPORT_SIZE_MACRO_BANK2) {
			log_error(device->ratbag,
				  "Unable to retrieve the second bank for macro for button %d of profile %d: %s (%d)\n",
				  button->index, button->profile->index,
				  rc < 0 ? strerror(-rc) : "not read enough", rc);
			goto out_macro;
		}

		roccat_set_config_profile(device,
				  button->profile->index + ROCCAT_MACRO_BANK1_PROFILE_OFFSET,
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

		/* Note: checksum mismatch is expected for macros written by Windows ROCCAT
		 * Swarm, which uses a different checksum formula. This does not affect
		 * read correctness; data is decoded regardless. Our write path uses our
		 * own consistent formula which the device firmware accepts.
		 */
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
	struct roccat_color *slot;

	switch (settings->lighting_effect) {
	case ROCCAT_LED_OFF:
		led->mode = RATBAG_LED_OFF;
		led->brightness = 0;
		break;
	case ROCCAT_LED_BREATHING:
		led->mode = RATBAG_LED_BREATHING;
		led->ms = settings->lighting_effect_speed * 1000;
		led->brightness = settings->brightness.intensity;
		break;
	case ROCCAT_LED_WAVE:
		led->mode = RATBAG_LED_CYCLE;
		led->ms = settings->lighting_effect_speed * 1000;
		led->brightness = settings->brightness.intensity;
		break;
	default:
		/* ROCCAT_LED_FIXED, ROCCAT_LED_BLINKING, ROCCAT_LED_BEATING, etc. */
		led->mode = RATBAG_LED_ON;
		led->brightness = settings->brightness.intensity;
		break;
	}

	led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;

	slot = roccat_led_color_slot(settings, led->index);
	if (slot)
		led->color = roccat_to_ratbag_color(*slot);

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

	assert(profile->index < ROCCAT_PROFILE_MAX);

	/* Read data from the mouse */
	settings = &drv_data->settings[profile->index];
	roccat_set_config_profile(device, profile->index, ROCCAT_CONFIG_SETTINGS);
	rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_SETTINGS,
					      (uint8_t*)settings, ROCCAT_REPORT_SIZE_SETTINGS);

	if (rc < (int)ROCCAT_REPORT_SIZE_SETTINGS) {
		log_error(device->ratbag,
			  "Failed to read settings for profile %d: %s (%d)\n",
			  profile->index,
			  rc < 0 ? strerror(-rc) : "short read",
			  rc);
		return;
	}

	if (!roccat_crc_is_valid(device, (uint8_t*)settings, ROCCAT_REPORT_SIZE_SETTINGS)) {
		log_error(device->ratbag,
			  "Profile %d has invalid checksum (blank/uninitialised profile), applying defaults\n",
			  profile->index);
	}

	/* Sanitize fields that the firmware requires to be non-zero.
	 * These are opaque/reserved bytes that Swarm always writes with
	 * specific values. Writing zeros causes the firmware to hang on
	 * profile activation. Applied unconditionally so partially-written
	 * profiles (valid CRC but zero fields) are also fixed. */
	if (settings->x_y_linked == 0x00)
		settings->x_y_linked = ROCCAT_DEFAULT_XY_LINKED;
	if (settings->unk_color1.red == 0x00 && settings->unk_color1.green == 0x00 &&
	    settings->unk_color1.blue == 0x00) {
		/* Default unk_color1 observed from Swarm */
		settings->unk_color1.intensity = 0x00;
		settings->unk_color1.red       = 0x08;
		settings->unk_color1.green     = 0xff;
		settings->unk_color1.blue      = 0x07;
	}
	if (settings->lighting_effect_speed == 0x00)
		settings->lighting_effect_speed = ROCCAT_DEFAULT_EFFECT_SPEED;
	if (settings->unknown2 == 0x00)
		settings->unknown2 = ROCCAT_DEFAULT_UNKNOWN2;

	/* A profile never configured by Swarm has custom_or_theme == 0x00.
	 * Swarm always writes 0x05 (custom) or 0x80+ (theme). Treat
	 * custom_or_theme == 0 as "uninitialised": force LEDs off and set
	 * white as the colour default so the user starts from a clean state. */
	if (settings->custom_or_theme == 0x00) {
		settings->lighting_effect       = ROCCAT_LED_OFF;
		settings->custom_or_theme       = ROCCAT_DEFAULT_CUSTOM_MODE;
		settings->brightness.intensity  = 0xff;
		struct roccat_color white = { .intensity = 0xff, .red = 0xff, .green = 0xff, .blue = 0xff };
		for (unsigned int i = 0; i < ROCCAT_LED_MAX; i++)
			*roccat_led_color_slot(settings, i) = white;
	}

	/* Clamp zero DPI slots to a safe minimum. The firmware may reject or
	 * malfunction if any of the 5 DPI slots is zero. Use 800 DPI as the
	 * fallback, matching the ROCCAT default DPI. */
	for (int dpi_i = 0; dpi_i < ROCCAT_NUM_DPI; dpi_i++) {
		if (settings->xres[dpi_i] == 0)
			settings->xres[dpi_i] = ROCCAT_DEFAULT_DPI_RAW;
		if (settings->yres[dpi_i] == 0)
			settings->yres[dpi_i] = ROCCAT_DEFAULT_DPI_RAW;
	}

	buttons = &drv_data->buttons[profile->index];
	roccat_set_config_profile(device, profile->index, ROCCAT_CONFIG_KEY_MAPPING);
	rc = ratbag_hidraw_get_feature_report(device, ROCCAT_REPORT_ID_KEY_MAPPING,
					      (uint8_t*)buttons, ROCCAT_REPORT_SIZE_BUTTONS);

	if (rc < (int)ROCCAT_REPORT_SIZE_BUTTONS) {
		log_error(device->ratbag,
			  "Failed to read button mapping for profile %d: %s (%d)\n",
			  profile->index,
			  rc < 0 ? strerror(-rc) : "short read",
			  rc);
		return;
	}

	roccat_apply_button_defaults(buttons);

	/* Feed libratbag with the data */
	roccat_read_dpi(settings, profile);
	ratbag_profile_for_each_led(profile, led) {
		roccat_read_led(settings, led);
	}

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
		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_DISABLE);
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
		active_idx);

	return 0;

err:
	ratbag_close_hidraw(device);
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static int
roccat_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc = 0;

	log_debug(device->ratbag, "roccat_commit called\n");

	list_for_each(profile, &device->profiles, link) {
		log_debug(device->ratbag,
			  "Profile %d dirty=%d\n", profile->index, profile->dirty);

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
