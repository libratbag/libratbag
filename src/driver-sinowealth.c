/*
 * Copyright © 2020 Marian Beermann
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

#include "driver-sinowealth.h"

#include "libratbag-data.h"
#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "shared-macro.h"

enum sinowealth_report_id {
	SINOWEALTH_REPORT_ID_CONFIG = 0x4,
	SINOWEALTH_REPORT_ID_CMD = 0x5,
	SINOWEALTH_REPORT_ID_CONFIG_LONG = 0x6,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_report_id) == sizeof(uint8_t), "Invalid size");

enum sinowealth_command_id {
	SINOWEALTH_CMD_FIRMWARE_VERSION = 0x1,
	SINOWEALTH_CMD_PROFILE = 0x2,
	SINOWEALTH_CMD_GET_CONFIG = 0x11,
	SINOWEALTH_CMD_GET_BUTTONS = 0x12,
	/* Doesn't work on devices with shorter configuration data (123 instead of 137). */
	SINOWEALTH_CMD_DEBOUNCE = 0x1a,
	/* Only works on devices that use CONFIG_LONG report ID. */
	SINOWEALTH_CMD_LONG_ANGLESNAPPING_AND_LOD = 0x1b,
	/* Same as GET_CONFIG but for the second profile. */
	SINOWEALTH_CMD_GET_CONFIG2 = 0x21,
	/* Same as GET_CONFIG but for the second profile. */
	SINOWEALTH_CMD_GET_BUTTONS2 = 0x22,
	SINOWEALTH_CMD_MACRO = 0x30,
	/* Puts the device into DFU mode.
	 * To reset re-plug the mouse or do a clean reboot.
	 */
	SINOWEALTH_CMD_DFU = 0x75,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_command_id) == sizeof(uint8_t), "Invalid size");

#define SINOWEALTH_BUTTON_SIZE 88

#define SINOWEALTH_CMD_SIZE 6

/* Report length commands that get configuration data should use. */
#define SINOWEALTH_CONFIG_REPORT_SIZE 520
#define SINOWEALTH_CONFIG_SIZE_MAX 167
#define SINOWEALTH_CONFIG_SIZE_MIN 123

#define SINOWEALTH_MACRO_SIZE 515

/* The PC software only goes down to 400, but the PMW3360 doesn't care */
#define SINOWEALTH_DPI_MIN 100
#define SINOWEALTH_DPI_STEP 100
/* Arbitrary, but I think every sensor supports this DPI, and this is
 * about as high as most people would ever like to go anyway.
 */
#define SINOWEALTH_DPI_FALLBACK 2000

/* Different software expose different amount of DPI slots:
 * Glorious - 6;
 * G-Wolves - 7.
 * But in fact fact there are eight slots.
 */
#define SINOWEALTH_NUM_DPIS 8

/* Technically SinoWealth mice support second profile, but there isn't
 * a single configuration software that exposes it.
 */
#define SINOWEALTH_NUM_PROFILES 1

/* Maximum amount of real events in a macro. */
#define SINOWEALTH_MACRO_LENGTH_MAX 168

#define SINOWEALTH_FW_VERSION_LEN 4

/* Bit mask for @ref sinowealth_config_report.config.
 *
 * This naming may be incorrect as it's not actually known what the other bits do.
 */
enum sinowealth_config_data_mask {
	SINOWEALTH_XY_INDEPENDENT = 0b1000,
};

/* Color data the way mouse stores it.
 *
 * @ref sinowealth_raw_to_color.
 *
 * @ref sinowealth_color_to_raw.
 *
 * @ref sinowealth_led_format.
 */
struct sinowealth_color {
	/* May be in either RGB or RBG format depending on the device.
	 * See the comment above this struct.
	 */
	uint8_t data[3];
} __attribute__((packed));
_Static_assert(sizeof(struct sinowealth_color) == 3, "Invalid size");

/* Sensor IDs used in SinoWealth firmware and software. */
enum sinowealth_sensor {
	SINOWEALTH_SENSOR_NONE,
	SINOWEALTH_SENSOR_PMW3360 = 0x06,
	SINOWEALTH_SENSOR_PMW3327 = 0x0e,
	SINOWEALTH_SENSOR_PMW3389 = 0x0f,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_sensor) == sizeof(uint8_t), "Invalid sensor enum size");

enum sinowealth_rgb_effect {
	RGB_OFF = 0,
	RGB_GLORIOUS = 0x1,   /* unicorn mode */
	RGB_SINGLE = 0x2,     /* single constant color */
	RGB_BREATHING7 = 0x3, /* breathing with seven user-defined colors */
	RGB_TAIL = 0x4,
	RGB_BREATHING = 0x5,  /* Full RGB breathing. */
	RGB_CONSTANT = 0x6,   /* Each LED gets its own static color. Not available in Glorious software. */
	RGB_RAVE = 0x7,
	RGB_RANDOM = 0x8,     /* Randomly change colors. Not available in Glorious software. */
	RGB_WAVE = 0x9,
	/* Single color breathing.
	 * Not available on some mice, for example Genesis Xenon 770 and
	 * DreamMachines DM5 (both are 0027 mice). On them RGB_BREATHING7
	 * with one color should be used instead.
	 */
	RGB_BREATHING1 = 0xa,

	/* The value mice with no LEDs have.
	* Unreliable as non-constant.
	* Do **not** overwrite it.
	 */
	RGB_NOT_SUPPORTED = 0xff,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_rgb_effect) == sizeof(uint8_t), "Invalid size");

struct sinowealth_rgb_mode {
	/* 0x1/2/3.
	 * @ref sinowealth_duration_to_rgb_mode.
	 * @ref sinowealth_rgb_mode_to_duration.
	 */
	uint8_t speed:4;
	/* 0x1/2/3/4.
	 * @ref sinowealth_brightness_to_rgb_mode.
	 * @ref sinowealth_rgb_mode_to_brightness.
	 */
	uint8_t brightness:4;
};
_Static_assert(sizeof(struct sinowealth_rgb_mode) == sizeof(uint8_t), "Invalid size");

struct sinowealth_xy_independent_dpi {
	uint8_t x;
	uint8_t y;
};
_Static_assert(sizeof(struct sinowealth_xy_independent_dpi) == sizeof(uint8_t[2]), "Invalid size");

/* DPI/CPI is encoded in the way the PMW3360 and PWM3327 sensors
 * accept it:
 * value = (DPI - 100) / 100;
 * or the way the PMW3389 sensor accepts it:
 * value = DPI / 100;
 *
 * @ref sinowealth_raw_to_dpi
 * @ref sinowealth_dpi_to_raw
 */
union sinowealth_dpis {
	/* You MUST use this field if no resolutions have separate X and Y.
	 */
	uint8_t dpis[8];
	/* You MUST use this field if at least one resolution has separate
	 * X and Y.
	 */
	struct sinowealth_xy_independent_dpi independent[8];
};
_Static_assert(sizeof(union sinowealth_dpis) == sizeof(uint8_t[16]), "Invalid size");

/* Configuration data the way it's stored in mouse memory.
 * When we want to change a setting, we basically copy the entire mouse
 * configuration, modify it and send it back.
 */
struct sinowealth_config_report {
	enum sinowealth_report_id report_id;
	enum sinowealth_command_id command_id;
	uint8_t unknown1;
	/* 0x0 - read.
	 * CONFIG_SIZE-8 - write.
	 */
	uint8_t config_write;
	uint8_t unknown2[2];
	enum sinowealth_sensor sensor_type;
	uint8_t unknown6[3];
	/* @ref sinowealth_report_rate_map. */
	uint8_t report_rate:4;
	/* 0b1000 - make DPI axes independent. */
	uint8_t config_flags:4;
	uint8_t dpi_count:4;
	/* Starting from 1 counting only active slots. */
	uint8_t active_dpi:4;
	/* bit set: disabled, unset: enabled */
	uint8_t disabled_dpi_slots;
	union sinowealth_dpis dpis;
	struct sinowealth_color dpi_color[8];
	enum sinowealth_rgb_effect rgb_effect;
	struct sinowealth_rgb_mode glorious_mode;
	uint8_t glorious_direction;
	struct sinowealth_rgb_mode single_mode;
	struct sinowealth_color single_color;
	struct sinowealth_rgb_mode breathing7_mode;
	uint8_t breathing7_colorcount;
	struct sinowealth_color breathing7_colors[7];
	struct sinowealth_rgb_mode tail_mode;
	struct sinowealth_rgb_mode breathing_mode;
	struct sinowealth_rgb_mode constant_color_mode;
	struct sinowealth_color constant_color_colors[6];
	uint8_t unknown3[12];
	struct sinowealth_rgb_mode rave_mode;
	struct sinowealth_color rave_colors[2];

	/* From here onward goes the data not available in short mice.
	 * .. judging by the size of this struct. The data in them may
	 * actually be different, we didn't test this yet.
	 */

	struct sinowealth_rgb_mode random_mode;
	struct sinowealth_rgb_mode wave_mode;
	struct sinowealth_rgb_mode breathing1_mode;
	struct sinowealth_color breathing1_color;
	/* 0x1 - 2 mm.
	 * 0x2 - 3 mm.
	 * 0xff - indicates that lift off distance is changed with a dedicated command. Not constant, so do **NOT** overwrite it.
	 */
	uint8_t lift_off_distance;
	uint8_t unknown4;

	/* From here onward goes the data only available in long mice. */

	uint8_t unknown5[36];

	uint8_t padding[SINOWEALTH_CONFIG_REPORT_SIZE - SINOWEALTH_CONFIG_SIZE_MAX];
} __attribute__((packed));
_Static_assert(sizeof(struct sinowealth_config_report) == SINOWEALTH_CONFIG_REPORT_SIZE, "Invalid size");

enum sinowealth_button_type {
	SINOWEALTH_BUTTON_TYPE_NONE = 0, /* This value might appear on broken configurations. */
	SINOWEALTH_BUTTON_TYPE_BUTTON = 0x11,
	SINOWEALTH_BUTTON_TYPE_WHEEL = 0x12,
	SINOWEALTH_BUTTON_TYPE_KEY = 0x21,
	SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY = 0x22,
	SINOWEALTH_BUTTON_TYPE_REPEATED = 0x31,
	SINOWEALTH_BUTTON_TYPE_SWITCH_DPI = 0x41,
	SINOWEALTH_BUTTON_TYPE_DPI_LOCK = 0x42,
	SINOWEALTH_BUTTON_TYPE_SPECIAL = 0x50,
	SINOWEALTH_BUTTON_TYPE_MACRO = 0x70,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_button_type) == sizeof(uint8_t), "Invalid size");

/* Bit masks. */
enum sinowealth_button_key_modifiers {
	SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTCTRL = 0x01,
	SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTSHIFT = 0x02,
	SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTALT = 0x04,
	SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTMETA = 0x08,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_button_key_modifiers) == sizeof(uint8_t), "Invalid size");

enum sinowealth_button_macro_mode {
	/* Repeat <option> times. */
	SINOWEALTH_BUTTON_MACRO_MODE_REPEAT = 0x1,
	/* Repeat until any button is pressed. */
	SINOWEALTH_BUTTON_MACRO_MODE_REPEAT_UNTIL_PRESSED = 0x2,
	/* Repeat until released. */
	SINOWEALTH_BUTTON_MACRO_MODE_REPEAT_UNTIL_RELEASED = 0x4,
	/* Anything above freezes up the mouse. */
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_button_macro_mode) == sizeof(uint8_t), "Invalid size");

struct sinowealth_button_data {
	enum sinowealth_button_type type;
	union {
		/* In some button types, byte are bit masks of enabled buttons.
		 * If several bits are enabled at the same time, their corresponding buttons
		 * will be activated at the same time.
		 */
		uint8_t data[3];
		struct {
			/* DPI divided by 100. */
			uint8_t dpi;
			uint8_t padding[2];
		} dpi_lock;
		struct {
			enum sinowealth_button_key_modifiers modifiers;
			uint8_t key;
			uint8_t padding;
		} key;
		struct {
			/* Macro index starting with 1.
			 *
			 * For consistency we do it like this: `button_index + (profile_index * button_count)`.
			 * This may clash with macros set by official software.
			 */
			uint8_t index;
			enum sinowealth_button_macro_mode mode;
			/* Mode-specific option.
			 *
			 * For 0x1:
			 * Value is repeat count.
			 *
			 * Other modes don't have any options.
			 */
			uint8_t option;
		} macro;
		struct {
			/* Button index. */
			uint8_t index;
			uint8_t delay;
			/* If zero, then repeat for as long as the button is held. */
			uint8_t count;
		} repeated_button;
	};
} __attribute__((packed));
_Static_assert(sizeof(struct sinowealth_button_data) == sizeof(uint32_t), "Invalid size");

struct sinowealth_button_report {
	uint8_t report_id;
	uint8_t command_id;
	uint8_t unknown1;
	/* 0x0 --- read.
	 * <size of configuration> - 8 --- write.
	 */
	uint8_t config_write;
	uint8_t unknown2[4];
	struct sinowealth_button_data buttons[20];
	uint8_t padding[SINOWEALTH_CONFIG_REPORT_SIZE - SINOWEALTH_BUTTON_SIZE];
} __attribute__((packed));
_Static_assert(sizeof(struct sinowealth_button_report) == SINOWEALTH_CONFIG_REPORT_SIZE, "Invalid size");

enum sinowealth_macro_command {
	SINOWEALTH_MACRO_COMMAND_BUTTON_PRESS = 0x10,
	SINOWEALTH_MACRO_COMMAND_BUTTON_RELEASE = 0x90,

	SINOWEALTH_MACRO_COMMAND_KEY_PRESS = 0x50,
	SINOWEALTH_MACRO_COMMAND_KEY_RELEASE = 0xd0,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_macro_command) == sizeof(uint8_t), "Invalid size");

struct sinowealth_macro_event {
	enum sinowealth_macro_command command;
	/* Use `1` for no delay.
	 * In case this is set to `0`, the event will ignored.
	 */
	uint8_t delay;
	union {
		/* HID button usage.
		 *
		 * 0x1 - button 1;
		 * 0x2 - button 2;
		 * 0x4 - button 3;
		 * and so forth.
		 */
		uint8_t button;

		/* HID keyboard usage.
		 *
		 * @ref ratbag_hidraw_get_keyboard_usage_from_keycode.
		 */
		uint8_t key;
	};
};
_Static_assert(sizeof(struct sinowealth_macro_event) == sizeof(uint8_t[3]), "Invalid size");

struct sinowealth_macro_report {
	uint8_t report_id;
	uint8_t command_id;
	uint8_t unknown1; // 0x2 when writing
	uint8_t _empty1[5];
	/* The index of this macro.
	 *
	 * In original software it may differ from index of the button where it's used.
	 * It's hard to keep track of, so we just set it like this:
	 * `button->index + (profile->index * BUTTON_COUNT)`.
	 * Because we do it like this, we may overwrite some already existing macro,
	 * which is not really good, but this can't be improved until we find a way
	 * to read macros from the mouse.
	 */
	uint8_t index;
	uint8_t _empty2;
	/* The amount of events going next that will get processed by mouse.
	 * If there are more events than this number, they will just get ignored.
	 */
	uint8_t event_count;
	struct sinowealth_macro_event events[SINOWEALTH_MACRO_LENGTH_MAX];
	uint8_t padding[SINOWEALTH_CONFIG_REPORT_SIZE - SINOWEALTH_MACRO_SIZE];
};
_Static_assert(sizeof(struct sinowealth_macro_report) == SINOWEALTH_CONFIG_REPORT_SIZE, "Invalid size");

/* Data related to mouse we store for ourselves. */
struct sinowealth_data {
	/* Whether the device uses REPORT_ID_CONFIG or REPORT_ID_CONFIG_LONG. */
	bool is_long;
	enum sinowealth_led_format led_type;
	unsigned int button_count;
	unsigned int config_size;
	/* Cached profile index. This might be incorrect if profile index was changed by another program while we are running. */
	unsigned int current_profile_index;
	unsigned int led_count;
	struct sinowealth_button_report buttons[SINOWEALTH_NUM_PROFILES];
	struct sinowealth_config_report configs[SINOWEALTH_NUM_PROFILES];
};

struct sinowealth_button_mapping {
	struct sinowealth_button_data data;
	struct ratbag_button_action action;
};

static const struct sinowealth_button_mapping sinowealth_button_map[] = {
	{ { SINOWEALTH_BUTTON_TYPE_BUTTON, { { 0x01 } } }, BUTTON_ACTION_BUTTON(1) },
	{ { SINOWEALTH_BUTTON_TYPE_BUTTON, { { 0x02 } } }, BUTTON_ACTION_BUTTON(2) },
	{ { SINOWEALTH_BUTTON_TYPE_BUTTON, { { 0x04 } } }, BUTTON_ACTION_BUTTON(3) },
	{ { SINOWEALTH_BUTTON_TYPE_BUTTON, { { 0x08 } } }, BUTTON_ACTION_BUTTON(5) },
	{ { SINOWEALTH_BUTTON_TYPE_BUTTON, { { 0x10 } } }, BUTTON_ACTION_BUTTON(4) },

	/* None of the other bits do anything. */

	/* First data byte is a 0-255 range. */
	{ { SINOWEALTH_BUTTON_TYPE_WHEEL, { { 0x1 } } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ { SINOWEALTH_BUTTON_TYPE_WHEEL, { { 0xff } } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
	/* None of the other bits do anything. */

	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x01 } } }, BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x02 } } }, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x04 } } }, BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x08 } } }, BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x10 } } }, BUTTON_ACTION_KEY(KEY_MUTE) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x20 } } }, BUTTON_ACTION_KEY(KEY_UNKNOWN) }, /* Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x40 } } }, BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x80 } } }, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },

	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x01 } } }, BUTTON_ACTION_KEY(KEY_CONFIG) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x02 } } }, BUTTON_ACTION_KEY(KEY_FILE) },
	/* 0x04 makes mouse send something, but it's not processed by Linux. Hidden. */
	/* 0x08 makes mouse send something, but it's not processed by Linux. Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x10 } } }, BUTTON_ACTION_KEY(KEY_MAIL) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x20 } } }, BUTTON_ACTION_KEY(KEY_CALC) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x40 } } }, BUTTON_ACTION_KEY(KEY_UNKNOWN) }, /* Hidden. */
	/* 0x80 makes mouse send something, but it's not processed by Linux. Hidden. */

	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x2 } } }, BUTTON_ACTION_KEY(KEY_HOMEPAGE) },
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x4 } } }, BUTTON_ACTION_KEY(KEY_BACK) },	 /* Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x8 } } }, BUTTON_ACTION_KEY(KEY_FORWARD) },	 /* Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x10 } } }, BUTTON_ACTION_KEY(KEY_STOP) },	 /* Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x20 } } }, BUTTON_ACTION_KEY(KEY_REFRESH) },	 /* Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x40 } } }, BUTTON_ACTION_KEY(KEY_BOOKMARKS) }, /* Hidden. */
	{ { SINOWEALTH_BUTTON_TYPE_MULTIMEDIA_KEY, { { 0x0, 0x0, 0x80 } } }, BUTTON_ACTION_KEY(KEY_UNKNOWN) },	 /* Hidden. */

	{ { SINOWEALTH_BUTTON_TYPE_SWITCH_DPI, { { 0x0 } } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ { SINOWEALTH_BUTTON_TYPE_SWITCH_DPI, { { 0x1 } } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ { SINOWEALTH_BUTTON_TYPE_SWITCH_DPI, { { 0x2 } } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	/* None of the other bits do anything. */

	{ { SINOWEALTH_BUTTON_TYPE_SPECIAL, { { 0x1 } } }, BUTTON_ACTION_NONE },
	/* Disabled as we don't support second profile officially.
	 * Not only that, by default default second profile has no button
	 * mapping and by using this the user is just going to be left with
	 * a practically non-working mouse. To fix this we would have to
	 * populate the empty profile with some default mappings.
	 */
	/* Hidden. */
	/* { { SINOWEALTH_BUTTON_TYPE_SPECIAL, { { 0x6 } } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) }, */

	/* This must defined after `SPECIAL` type so that correct raw data
	 * for action type `NONE` is used. */
	{ { SINOWEALTH_BUTTON_TYPE_NONE, {} }, BUTTON_ACTION_NONE },
};

/* Check if two given button data structs are equal.
 *
 * @return 1 if structs are equal or 0 otherwise.
 */
static int
sinowealth_button_data_is_equal(const struct sinowealth_button_data *lhs, const struct sinowealth_button_data *rhs)
{
	if (lhs->type != rhs->type)
		return 0;

	for (unsigned int i = 0; i < sizeof(lhs->data); ++i) {
		if (lhs->data[i] != rhs->data[i]) {
			return 0;
		}
	}

	return 1;
}

/* Convert a button action to raw data using the `sinowealth_button_map`.
 * NOTE: It does not contain all of the button types, as some of them are
 * better made programmatically. See @ref sinowealth_update_buttons_from_profile.
 *
 * @param data Struct to write to.
 *
 * @return 0 on success or 1 if such action is not in the map.
 */
static int
sinowealth_button_action_to_raw(const struct ratbag_button_action *action, struct sinowealth_button_data *data)
{
	const struct sinowealth_button_mapping *mapping = NULL;

	ARRAY_FOR_EACH(sinowealth_button_map, mapping) {
		if (ratbag_button_action_match(&mapping->action, action)) {
			memcpy(data, &mapping->data, sizeof(struct sinowealth_button_data));
			return 0;
		}
	}

	return 1;
}

/* Convert raw button data to a button action using the `sinowealth_button_map`.
 * NOTE: It does not contain all of the button types, as some of them are
 * better made programmatically. See @ref sinowealth_update_profile_from_buttons.
 *
 * @return Button action or NULL if such action is not in the map. */
static const struct ratbag_button_action *
sinowealth_raw_to_button_action(const struct sinowealth_button_data *data)
{
	const struct sinowealth_button_mapping *mapping = NULL;

	ARRAY_FOR_EACH(sinowealth_button_map, mapping) {
		if (sinowealth_button_data_is_equal(data, &mapping->data) == 0)
			continue;

		return &mapping->action;
	}

	return NULL;
}

struct sinowealth_report_rate_mapping {
	uint8_t raw;
	unsigned int report_rate;
};

static const struct sinowealth_report_rate_mapping sinowealth_report_rate_map[] = {
	{ 0x1, 125 },
	{ 0x2, 250 },
	{ 0x3, 500 },
	{ 0x4, 1000 },
};

/* @return Internal report rate representation or 0 on error. */
static uint8_t
sinowealth_report_rate_to_raw(unsigned int report_rate)
{
	const struct sinowealth_report_rate_mapping *mapping = NULL;
	ARRAY_FOR_EACH(sinowealth_report_rate_map, mapping)
		if (mapping->report_rate == report_rate)
			return mapping->raw;
	return 0;
}

/* @return Report rate in hz or 0 on error. */
static unsigned int
sinowealth_raw_to_report_rate(uint8_t raw)
{
	const struct sinowealth_report_rate_mapping *mapping = NULL;
	ARRAY_FOR_EACH(sinowealth_report_rate_map, mapping)
		if (mapping->raw == raw)
			return mapping->report_rate;
	return 0;
}

/* @return Maximum DPI for sensor `sensor` or 0 on error. */
static unsigned int
sinowealth_get_max_dpi_for_sensor(enum sinowealth_sensor sensor)
{
	switch (sensor) {
	case SINOWEALTH_SENSOR_PMW3327: return 10200;
	case SINOWEALTH_SENSOR_PMW3360: return 12000;
	case SINOWEALTH_SENSOR_PMW3389: return 16000;
	default: return SINOWEALTH_DPI_FALLBACK;
	}
}

/* Convert sensor-encoded resolution `raw` to DPI.
 *
 * @ref sinowealth_dpis.
 */
static unsigned int
sinowealth_raw_to_dpi(struct ratbag_device *device, unsigned int raw)
{
	struct sinowealth_data *drv_data = device->drv_data;
	enum sinowealth_sensor sensor = drv_data->configs[0].sensor_type;

	if (sensor == SINOWEALTH_SENSOR_PMW3327 || sensor == SINOWEALTH_SENSOR_PMW3360)
		raw += 1;

	unsigned int dpi = raw * 100;

	return dpi;
}

/* Convert DPI `dpi` to sensor-encoded resolution.
 *
 * @ref sinowealth_dpis.
 */
static uint8_t
sinowealth_dpi_to_raw(struct ratbag_device *device, unsigned int dpi)
{
	struct sinowealth_data *drv_data = device->drv_data;
	enum sinowealth_sensor sensor = drv_data->configs[0].sensor_type;

	assert(dpi >= SINOWEALTH_DPI_MIN && dpi <= sinowealth_get_max_dpi_for_sensor(sensor));

	uint8_t raw = dpi / 100;

	if (sensor == SINOWEALTH_SENSOR_PMW3327 || sensor == SINOWEALTH_SENSOR_PMW3360)
		raw -= 1;

	return raw;
}

/* Convert internal mouse color `raw` to color.
 * If LED type defined in the device data is incorrect, RBG color order is used.
 */
static struct ratbag_color
sinowealth_raw_to_color(struct ratbag_device *device, struct sinowealth_color raw_color)
{
	struct sinowealth_data *drv_data = device->drv_data;

	struct ratbag_color color;

	switch (drv_data->led_type) {
	/* Fall back to RBG if the LED type is incorrect. */
	default:
	case SINOWEALTH_LED_TYPE_RBG:
		color.red = raw_color.data[0];
		color.green = raw_color.data[2];
		color.blue = raw_color.data[1];
		break;
	case SINOWEALTH_LED_TYPE_RGB:
		color.red = raw_color.data[0];
		color.green = raw_color.data[1];
		color.blue = raw_color.data[2];
		break;
	}

	return color;
}

/* Convert color `color` to internal representation of color of the mouse.
 * If LED type defined in the device data is incorrect, RBG color order is used.
 */
static struct sinowealth_color
sinowealth_color_to_raw(struct ratbag_device *device, struct ratbag_color color)
{
	struct sinowealth_data *drv_data = device->drv_data;

	struct sinowealth_color raw_color;

	switch (drv_data->led_type) {
	/* Fall back to RBG if the LED type is incorrect. */
	default:
	case SINOWEALTH_LED_TYPE_RBG:
		raw_color.data[0] = color.red;
		raw_color.data[1] = color.blue;
		raw_color.data[2] = color.green;
		break;
	case SINOWEALTH_LED_TYPE_RGB:
		raw_color.data[0] = color.red;
		raw_color.data[1] = color.green;
		raw_color.data[2] = color.blue;
		break;
	}

	return raw_color;
}

/* Get brightness to use with ratbag's API from RGB mode `mode`. */
static int
sinowealth_rgb_mode_to_brightness(struct sinowealth_rgb_mode mode)
{
	/* Convert 0-4 to 0-255. */
	return min(mode.brightness * 64, 255);
}

/* Convert 8 bit brightness value to internal representation of brightness of the mouse. */
static uint8_t
sinowealth_brightness_to_rgb_mode(uint8_t brightness)
{
	/* Convert 0-255 to 0-4. */
	return (brightness + 1) / 64;
}

/* @return Effect duration or `0` on error. */
static int
sinowealth_rgb_mode_to_duration(struct sinowealth_rgb_mode mode)
{
	switch (mode.speed) {
	case 0: return 10000; /* static: does not translate to duration */
	case 1: return 1500;
	case 2: return 1000;
	case 3: return 500;
	default: return 0;
	}
}

/* Convert duration value `duration` to representation of brightness of the mouse.
 *
 * @param duration Duration in milliseconds.
 */
static uint8_t
sinowealth_duration_to_rgb_mode(unsigned int duration)
{
	uint8_t mode = 0;
	if (duration <= 500) {
		mode |= 3;
	} else if (duration <= 1000) {
		mode |= 2;
	} else {
		mode |= 1;
	}
	return mode;
}

/* Fill LED `led` with values from mode `mode`. */
static void
sinowealth_set_led_from_rgb_mode(struct ratbag_led *led, struct sinowealth_rgb_mode mode)
{
	led->brightness = sinowealth_rgb_mode_to_brightness(mode);
	led->ms = sinowealth_rgb_mode_to_duration(mode);
}

/* Convert data in LED `led` to RGB mode. */
static struct sinowealth_rgb_mode
sinowealth_led_to_rgb_mode(const struct ratbag_led *led)
{
	struct sinowealth_rgb_mode mode;
	mode.brightness = sinowealth_brightness_to_rgb_mode(led->brightness);
	mode.speed = sinowealth_duration_to_rgb_mode(led->ms);
	return mode;
}

/* Do a read query.
 *
 * After an error assume `buffer` now has garbage data.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_query_read(struct ratbag_device *device, uint8_t buffer[], unsigned int buffer_length)
{
	/*
	 * TODO: make this work with sinowealth_read_raw_config. Currently it
	 * doesn't because that function has some custom behavior.
	 */

	int rc = 0;

	/* Buffer's first byte is always the report ID. */
	const uint8_t report_id = buffer[0];
	/* Buffer's second byte in case of SinoWealth is always the command ID. */
	const uint8_t query_command = buffer[1];

	/* The way we retrieve data from SinoWealth is as follows:
	 *
	 * - Set a feature report with first two bytes corresponding to the
	 * wanted command.
	 *
	 * - Get a feature report with the same report ID and buffer length.
	 * The buffer can be reused from the previous step for more efficiency.
	 * We also do this to reduce the amount of arguments in the function.
	 */

	rc = ratbag_hidraw_set_feature_report(device, report_id, buffer, buffer_length);
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Could not set feature report in a read query: %d\n", rc);
		return -1;
	}
	rc = ratbag_hidraw_get_feature_report(device, report_id, buffer, buffer_length);
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Could not get feature report in a read query: %d\n", rc);
		return -1;
	}

	/* Check if the response we got is for the correct command. */
	if (buffer[1] != query_command) {
		log_error(device->ratbag, "Could not read command %#x, got command %#x instead\n", query_command, buffer[1]);
		return -1;
	}

	return 0;
}

/* Do a write query.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_query_write(struct ratbag_device *device, uint8_t buffer[], unsigned int buffer_length)
{
	int rc = 0;

	/* Buffer's first byte is always the report ID. */
	const uint8_t report_id = buffer[0];

	rc = ratbag_hidraw_set_feature_report(device, report_id, buffer, buffer_length);
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Could not set feature report in a write query: %d\n", rc);
		return -1;
	}

	return 0;
}

/* @return Active profile index or a negative error code. */
static int
sinowealth_get_active_profile(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;

	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_PROFILE };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Could not get device's active profile\n");
		return -1;
	}

	unsigned int index = buf[2] - 1;

	drv_data->current_profile_index = index;

	return (int)index;
}

/* Make the profile at index `index` the active one.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	if (index >= SINOWEALTH_NUM_PROFILES) {
		log_error(device->ratbag, "Profile index %u is out of range", index);
		return -EINVAL;
	}

	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;

	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_PROFILE, index + 1 };

	rc = sinowealth_query_write(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Error while selecting profile: %d\n", rc);
		return -1;
	}

	drv_data->current_profile_index = index;

	return 0;
}

/* Fill buffer `out` with firmware version.
 *
 * @param out The buffer output will be written to.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_get_fw_version(struct ratbag_device *device, char out[4])
{
	int rc = 0;

	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_FIRMWARE_VERSION };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Couldn't read firmware version: %d\n", rc);
		return -1;
	}

	memcpy(out, buf + 2, 4);

	return 0;
}

/* @return Time in milliseconds or a negative error code. */
static int
sinowealth_get_debounce_time(struct ratbag_device *device)
{
	int rc = 0;

	/* TODO: implement debounce time changing once we have an API for that.
	 * To implement it here just set the third index to the desired debounce time / 2.
	 */
	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_DEBOUNCE };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Could not read debounce time: %d\n", rc);
		return -1;
	}

	return buf[2] * 2;
}

/* Print angle snapping (Cal line) and lift-off distance (LOD) modes.
 *
 * This is only confirmed to work on G-Wolves Hati where the way with
 * config report doesn't work. This does not work on Glorious Model O.
 */
static int
sinowealth_print_long_lod_and_anglesnapping(struct ratbag_device *device)
{
	int rc = 0;

	/* TODO: implement angle snapping and lift-off distance changing once we have an API for that.
	 * To implement LOD changing here: set the third index to <whether you want LOD high or low> + 1.
	 * To implement angle snapping toggling here: set the fourth index to 1 or 0 to enable or disable accordingly.
	 */
	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_LONG_ANGLESNAPPING_AND_LOD };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Could not read LOD and angle snapping values: %d\n", rc);
		return -1;
	}

	log_info(device->ratbag, "LOD is high: %u\n", buf[2] - 1);
	log_info(device->ratbag, "Angle snapping enabled: %u\n", buf[3]);

	return 0;
}

/* Read button configuration data from the mouse and save it in drv_data.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_read_raw_buttons(struct ratbag_device *device)
{
	/* TODO: analogous to @ref sinowealth_read_raw_config. */

	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_button_report *buttons1 = &drv_data->buttons[0];

	const unsigned char config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	uint8_t cmd1[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_GET_BUTTONS };
	rc = ratbag_hidraw_set_feature_report(device, SINOWEALTH_REPORT_ID_CMD, cmd1, sizeof(cmd1));
	if (rc != sizeof(cmd1)) {
		log_error(device->ratbag, "Error while sending read config command: %d\n", rc);
		return -1;
	}

	rc = ratbag_hidraw_get_feature_report(device, config_report_id, (uint8_t*)buttons1, SINOWEALTH_CONFIG_REPORT_SIZE);
	if (rc != SINOWEALTH_BUTTON_SIZE) {
		log_error(device->ratbag, "Could not read device button configuration: %d\n", rc);
		return -1;
	}

	return 0;
}

/* Read configuration data from the mouse and save it in drv_data.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_read_raw_config(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config1 = &drv_data->configs[0];

	const uint8_t config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	uint8_t cmd[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_GET_CONFIG };

	/* TODO: adapt @ref sinowealth_query_read to work here and use it. */

	rc = ratbag_hidraw_set_feature_report(device, SINOWEALTH_REPORT_ID_CMD, cmd, sizeof(cmd));
	if (rc != sizeof(cmd)) {
		log_error(device->ratbag, "Error while sending read config command: %d\n", rc);
		return -1;
	}

	rc = ratbag_hidraw_get_feature_report(device, config_report_id,
					      (uint8_t*) config1, SINOWEALTH_CONFIG_REPORT_SIZE);
	/* The GET_FEATURE report length has to be 520, but the actual data returned is less */
	if (rc < SINOWEALTH_CONFIG_SIZE_MIN || rc > SINOWEALTH_CONFIG_SIZE_MAX) {
		log_error(device->ratbag, "Could not read device configuration: %d\n", rc);
		return -1;
	}
	drv_data->config_size = rc;

	log_debug(device->ratbag, "Configuration size is %d bytes\n", drv_data->config_size);

	return 0;
}

/* Update profile with values from raw configuration data. */
static void
sinowealth_update_profile_from_config(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->configs[profile->index];
	struct ratbag_led *led = NULL;
	struct ratbag_resolution *resolution = NULL;

	/* Report rate */
	const unsigned int hz = sinowealth_raw_to_report_rate(config->report_rate);
	ratbag_profile_set_report_rate(profile, hz);

	unsigned int enabled_dpi_count = 0;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (config->config_flags & SINOWEALTH_XY_INDEPENDENT) {
			resolution->dpi_x = sinowealth_raw_to_dpi(device, config->dpis.independent[resolution->index].x);
			resolution->dpi_y = sinowealth_raw_to_dpi(device, config->dpis.independent[resolution->index].y);
		} else {
			resolution->dpi_x = sinowealth_raw_to_dpi(device, config->dpis.dpis[resolution->index]);
			resolution->dpi_y = resolution->dpi_x;
		}
		if (config->disabled_dpi_slots & (1 << resolution->index)) {
			/* DPI step is disabled, fake it by setting DPI to 0 */
			resolution->dpi_x = 0;
			resolution->dpi_y = 0;
		} else {
			/* NOTE: we mark this `1` unsigned explicitly as otherwise
			 * the right-hand side will become a signed integer and the
			 * comparison will be between expressions of different
			 * signedness.
			 */
			resolution->is_active = enabled_dpi_count == config->active_dpi - 1U;
			resolution->is_default = resolution->is_active;

			++enabled_dpi_count;
		}
	}

	/* Body lighting */
	if (drv_data->led_count > 0) {
		led = ratbag_profile_get_led(profile, 0);
		switch (config->rgb_effect) {
		case RGB_OFF:
			led->mode = RATBAG_LED_OFF;
			break;
		case RGB_SINGLE:
			led->mode = RATBAG_LED_ON;
			led->color = sinowealth_raw_to_color(device, config->single_color);
			led->brightness = sinowealth_rgb_mode_to_brightness(config->single_mode);
			break;
		case RGB_BREATHING7:
			/* NOTE: I don't know how mice would react to this, but this
			 * can happen if configuration data gets broken.
			 */
			if (config->breathing7_colorcount < 1) {
				log_error(device->ratbag, "LED mode is multi-colored breathing, but there are no colors configured\n");
				led->mode = RATBAG_LED_OFF;
				break;
			}
			if (config->breathing7_colorcount > 1) {
				log_debug(device->ratbag, "LED mode is multi-colored breathing, but we can only use one color. Using the first one...\n");
			}
			led->mode = RATBAG_LED_BREATHING;
			led->color = sinowealth_raw_to_color(device, config->breathing7_colors[0]);
			sinowealth_set_led_from_rgb_mode(led, config->breathing7_mode);
			break;
		case RGB_GLORIOUS:
		case RGB_BREATHING:
		case RGB_CONSTANT:
		case RGB_RANDOM:
		case RGB_TAIL:
		case RGB_RAVE:
		case RGB_WAVE:
			led->mode = RATBAG_LED_CYCLE;
			sinowealth_set_led_from_rgb_mode(led, config->glorious_mode);
			break;
		case RGB_BREATHING1:
			led->mode = RATBAG_LED_BREATHING;
			led->color = sinowealth_raw_to_color(device, config->breathing1_color);
			sinowealth_set_led_from_rgb_mode(led, config->breathing1_mode);
			break;
		case RGB_NOT_SUPPORTED:
		default:
			log_error(device->ratbag, "Got unknown RGB effect: %d\n", config->rgb_effect);
			break;
		}
		ratbag_led_unref(led);
	}

	profile->is_active = profile->index == drv_data->current_profile_index;
}

static void
sinowealth_update_profile_from_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_button_report *buf = &drv_data->buttons[profile->index];
	struct ratbag_button *button = NULL;
	struct sinowealth_button_data button_data;
	int rc = 0;

	ratbag_profile_for_each_button(profile, button) {
		button_data = buf->buttons[button->index];

		const struct ratbag_button_action *action = sinowealth_raw_to_button_action(&button_data);
		/* Match was found in the map, continue. */
		if (action != NULL) {
			button->action.action = action->action;
			button->action.type = action->type;
			continue;
		}

		/* Explicitly fall back to type `UNKNOWN` as `NONE` is the default. */
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;

		switch (button_data.type) {
		case SINOWEALTH_BUTTON_TYPE_KEY: {
			unsigned int modifiers = 0;
			if (button_data.key.modifiers & SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTCTRL)
				modifiers |= MODIFIER_LEFTCTRL;
			if (button_data.key.modifiers & SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTSHIFT)
				modifiers |= MODIFIER_LEFTSHIFT;
			if (button_data.key.modifiers & SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTALT)
				modifiers |= MODIFIER_LEFTALT;
			if (button_data.key.modifiers & SINOWEALTH_BUTTON_KEY_MODIFIER_LEFTMETA)
				modifiers |= MODIFIER_LEFTMETA;

			const unsigned int key = ratbag_hidraw_get_keycode_from_keyboard_usage(device, button_data.key.key);

			rc = ratbag_button_macro_new_from_keycode(button, key, modifiers);
			if (rc < 0) {
				log_error(device->ratbag, "Error while reading button %d\n", button->index);
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
			}
			break;
		}
		case SINOWEALTH_BUTTON_TYPE_REPEATED: {
			/* NOTE: We do not support such button actions yet. */

			const uint8_t button_index = button_data.repeated_button.index;
			const uint8_t repeat_delay = button_data.repeated_button.delay;
			const uint8_t repeat_count = button_data.repeated_button.count;

			log_debug(device->ratbag, "Read repeating button %u: %#x %#x %#x\n", button->index, button_index, repeat_delay, repeat_count);
			break;
		}
		case SINOWEALTH_BUTTON_TYPE_DPI_LOCK: {
			/* NOTE: We do not support such button actions yet. */

			const unsigned int dpi = button_data.dpi_lock.dpi * 100;

			log_debug(device->ratbag, "Read button %u locks DPI on %u\n", button->index, dpi);
			break;
		}
		case SINOWEALTH_BUTTON_TYPE_MACRO: {
			const uint8_t macro_index = button_data.macro.index;
			const uint8_t mode = button_data.macro.mode;
			const uint8_t option = button_data.macro.option;

			log_debug(device->ratbag, "Read button %u activates macro %u: %#x %#x\n", button->index, macro_index, mode, option);

			/* There is no known way to read a macro blob, so create a dummy
			 * macro event, so that the button action displays as a macro.
			 */

			const int key = 0; /* Dummy. */
			const int modifiers = 0; /* Dummy. */
			rc = ratbag_button_macro_new_from_keycode(button, key, modifiers);
			if (rc < 0) {
				log_error(device->ratbag, "Could not make a dummy macro: %d\n", rc);
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
			}

			break;
		}
		default:
			log_debug(device->ratbag, "Read button %u can't be determined: %#x %#x %#x %#x\n", button->index, button_data.type, button_data.data[0], button_data.data[1], button_data.data[2]);
			break;
		}
	}
}

/* @return 0 on success or an error code. */
static int
sinowealth_button_set_key_action(struct ratbag_device *device, const struct ratbag_button *button, struct sinowealth_button_data *button_data)
{
	assert(button->action.type == RATBAG_BUTTON_ACTION_TYPE_KEY);

	const unsigned int key = button->action.action.key.key;
	// libratbag doesn't support modifiers in `key` actions.
	const unsigned int modifiers = 0;

	const uint8_t raw_key = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
	if (raw_key == 0) {
		log_debug(device->ratbag, "Could not set unsupported key %#x to button %u\n", key, button->index);
		return -1;
	}

	button_data->type = SINOWEALTH_BUTTON_TYPE_KEY;
	button_data->key.modifiers = modifiers;
	button_data->key.key = raw_key;

	return 0;
}

static int
sinowealth_update_buttons_from_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_button_report *buttons = &drv_data->buttons[profile->index];
	struct ratbag_button *button = NULL;
	int rc = 0;

	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;

		struct ratbag_button_action *action = &button->action;
		struct sinowealth_button_data *button_data = &buttons->buttons[button->index];

		rc = sinowealth_button_action_to_raw(action, button_data);
		/* Match was found in the map, continue. */
		if (rc == 0) {
			continue;
		}

		switch (action->type) {
		case RATBAG_BUTTON_ACTION_TYPE_KEY:
			sinowealth_button_set_key_action(device, button, button_data);
			break;
		case RATBAG_BUTTON_ACTION_TYPE_MACRO: {
			/* Make the button activate a macro.
			 * The macro itself will be written later by @ref sinowealth_write_macros.
			 */

			button_data->type = SINOWEALTH_BUTTON_TYPE_MACRO;
			button_data->macro.index = button->index + (profile->index * drv_data->button_count);
			button_data->macro.mode = SINOWEALTH_BUTTON_MACRO_MODE_REPEAT;
			button_data->macro.option = 1;

			break;
		}
		default:
			log_debug(device->ratbag, "Can't set unsupported action type %#x to button %u\n", action->action.special, button->index);
			break;
		}
	}

	return 0;
}

/* Update macro report `macro` with macro button action in button `button`. */
static void
sinowealth_update_macro_from_action(struct ratbag_profile *profile, struct ratbag_button *button, struct sinowealth_macro_report *macro)
{
	struct ratbag_button_action *action = &button->action;
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;

	if (action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return;

	macro->index = button->index + (profile->index * drv_data->button_count);

	/* Reset the `events` field. Even if we don't do this, the mouse will ignore unneeded data. */
	memset(&macro->events, 0, sizeof(macro->events));

	unsigned int raw_event_count = 0;
	for (unsigned int i = 0; i < MAX_MACRO_EVENTS && raw_event_count < SINOWEALTH_MACRO_LENGTH_MAX; ++i) {
		struct ratbag_macro_event *event = &action->macro->events[i];

		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_INVALID)
			break;
		if (action->macro->events[i].type == RATBAG_MACRO_EVENT_NONE)
			break;

		/* Fall back to delay of 1 ms as it's required. */
		macro->events[raw_event_count].delay = 1;

		switch (event->type) {
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
		case RATBAG_MACRO_EVENT_KEY_RELEASED: {
			int raw_key = ratbag_hidraw_get_keyboard_usage_from_keycode(device, event->event.key);
			if (raw_key == 0) {
				log_error(device->ratbag, "Could not set unsupported key %#x in macro for button %u", event->event.key, button->index);
				/* Ignore the error to not mess up event order. */
			}

			/* NOTE: Ugly, but better than duplicating the code. */
			if (event->type == RATBAG_MACRO_EVENT_KEY_PRESSED)
				macro->events[raw_event_count].command = SINOWEALTH_MACRO_COMMAND_KEY_PRESS;
			else
				macro->events[raw_event_count].command = SINOWEALTH_MACRO_COMMAND_KEY_RELEASE;

			macro->events[raw_event_count].key = raw_key;

			++raw_event_count;
			break;
		}
		case RATBAG_MACRO_EVENT_WAIT:
			/* Delay is a part of every macro event in SinoWealth mice,
			 * in other words it does not occupy a separate event slot.
			 */

			/* Ignore if it's the first event.
			 * This is impossible to do on SinoWealth mice.
			 */
			if (raw_event_count == 0)
				break;

			/* Limit timeout to 255 ms.
			 * Obviously we can't put more in 8 bytes.
			 * Glorious software allows you to set up to 4096,
			 * but it's actually a bug and the sent number overflows.
			 */
			if (event->event.timeout > 0xff)
				event->event.timeout = 0xff;

			/* Set delay of the previous event. */
			macro->events[raw_event_count - 1].delay = event->event.timeout;
			break;
		/* Should not be reachable but let's ignore it just in case. */
		default:
		case RATBAG_MACRO_EVENT_INVALID:
		case RATBAG_MACRO_EVENT_NONE:
			break;
		}
	}

	/* Update the event counter in the macro. */
	macro->event_count = raw_event_count;
}


/*
 * @return Supported device data for the device or `NULL`.
 */
static const struct sinowealth_device_data *
sinowealth_find_device_data(struct ratbag_device *device, const char *fw_version)
{
	const struct ratbag_device_data *data = device->data;

	const struct list *supported_devices = ratbag_device_data_sinowealth_get_supported_devices(data);

	struct sinowealth_device_data *device_data = NULL;
	list_for_each(device_data, supported_devices, link) {
		if (device_data->fw_version == NULL || device_data->device_name == NULL) {
			log_error(device->ratbag, "Skipping invalid device data\n");
			continue;
		}

		if (!strneq(fw_version, device_data->fw_version, SINOWEALTH_FW_VERSION_LEN))
			continue;

		return device_data;
	}

	return NULL;
}

/* Initialize profiles for device `device`.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_init_profile(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_button *button = NULL;
	struct ratbag_led *led = NULL;
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution = NULL;

	struct sinowealth_data *drv_data = device->drv_data;
	/* We only use this to detect whether RGB effects are available and
	 * what sensor is used so it doesn't matter which one we use.
	 * Technically they might have different values in the checked slot.
	 */
	struct sinowealth_config_report *config = &drv_data->configs[0];

	char fw_version[SINOWEALTH_FW_VERSION_LEN + 1] = { 0 };
	rc = sinowealth_get_fw_version(device, fw_version);
	if (rc)
		return rc;
	log_info(device->ratbag, "firmware version: %s\n", fw_version);

	rc = sinowealth_read_raw_config(device);
	if (rc)
		return rc;

	rc = sinowealth_read_raw_buttons(device);
	if (rc)
		return rc;

	const struct sinowealth_device_data *device_data = sinowealth_find_device_data(device, fw_version);

	if (device_data == NULL) {
		log_error(device->ratbag, "Device with firmware version %s is not supported\n", fw_version);
		return -1;
	}

	drv_data->button_count = device_data->button_count;
	drv_data->led_type = device_data->led_type;

	log_info(device->ratbag, "Found device: %s\n", device_data->device_name);

	/* LED count. */
	drv_data->led_count = 0;
	if (config->rgb_effect != RGB_NOT_SUPPORTED && drv_data->led_type != SINOWEALTH_LED_TYPE_NONE) {
		drv_data->led_count += 1;
	}
	/* We may want to account for the DPI LEDs in the future.
	 * We don't support them yet, so it's not a priority now.
	 */

	/* Number of DPIs = all DPIs from min to max (inclusive) and "0 DPI" as a special value
	 * to signal a disabled DPI step.
	 */
	unsigned int num_dpis = (sinowealth_get_max_dpi_for_sensor(config->sensor_type) - SINOWEALTH_DPI_MIN) / SINOWEALTH_DPI_STEP + 2;

	ratbag_device_init_profiles(device, SINOWEALTH_NUM_PROFILES, SINOWEALTH_NUM_DPIS, drv_data->button_count, drv_data->led_count);

	/* Generate DPI list */
	unsigned int dpis[num_dpis];
	dpis[0] = 0; /* 0 DPI = disabled */
	for (unsigned int i = 1; i < num_dpis; i++) {
		dpis[i] = SINOWEALTH_DPI_MIN + (i - 1) * SINOWEALTH_DPI_STEP;
	}

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
		}

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list(resolution, dpis, num_dpis);
			ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		}

		/* Set up available report rates. */
		unsigned int report_rates[] = { 125, 250, 500, 1000 };
		ratbag_profile_set_report_rate_list(profile, report_rates, ARRAY_LENGTH(report_rates));

		/* Set up LED capabilities */
		if (drv_data->led_count > 0) {
			led = ratbag_profile_get_led(profile, 0);
			led->type = RATBAG_LED_TYPE_SIDE;
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			ratbag_led_unref(led);
		}
	}

	return 0;
}

static int
sinowealth_test_hidraw(struct ratbag_device *device)
{
	int rc = 0;

	/* Only the keyboard interface has this report */
	rc = ratbag_hidraw_has_report(device, SINOWEALTH_REPORT_ID_CONFIG);
	if (rc)
		return rc;

	rc = ratbag_hidraw_has_report(device, SINOWEALTH_REPORT_ID_CONFIG_LONG);
	if (rc) {
		struct sinowealth_data *drv_data = device->drv_data;
		drv_data->is_long = true;

		return rc;
	}

	return 0;
}

/* Write raw button configuration data in drv_data to the mouse.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_write_buttons(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;

	const char config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	struct sinowealth_button_report *buttons1 = &drv_data->buttons[0];

	buttons1->report_id = config_report_id;
	buttons1->command_id = SINOWEALTH_CMD_GET_BUTTONS;
	buttons1->config_write = SINOWEALTH_BUTTON_SIZE - 8;

	rc = sinowealth_query_write(device, (uint8_t*)buttons1, sizeof(*buttons1));
	if (rc != 0) {
		log_error(device->ratbag, "Error while writing buttons: %d\n", rc);
		return -1;
	}

	return 0;
}

/* Write raw configuration data in drv_data to the mouse.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_write_config(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config1 = &drv_data->configs[0];

	const uint8_t config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	config1->report_id = config_report_id;
	config1->command_id = SINOWEALTH_CMD_GET_CONFIG;
	config1->config_write = drv_data->config_size - 8;

	rc = sinowealth_query_write(device, (uint8_t*)config1, sizeof(*config1));
	if (rc != 0) {
		log_error(device->ratbag, "Error while writing config: %d\n", rc);
		return -1;
	}

	return 0;
}

static int
sinowealth_write_macros(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_button *button = NULL;
	struct ratbag_profile *profile = NULL;

	struct sinowealth_data *drv_data = device->drv_data;

	const char config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	/* NOTE: We will reuse the same buffer for all commands and reset it's `events` field for every button. */
	struct sinowealth_macro_report macro = {};
	macro.report_id = config_report_id;
	macro.command_id = SINOWEALTH_CMD_MACRO;
	macro.unknown1 = 0x2;

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_for_each_button(profile, button) {
			if (!button->dirty)
				continue;

			struct ratbag_button_action *action = &button->action;

			/* Ignore non macro actions.
			 * They were already handled by @ref sinowealth_update_profile_from_buttons.
			 */
			if (action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
				continue;

			sinowealth_update_macro_from_action(profile, button, &macro);

			rc = sinowealth_query_write(device, (uint8_t*)&macro, sizeof(macro));
			if (rc != 0) {
				log_error(device->ratbag, "Error while writing macro %u: %d\n", macro.index, rc);
				return -1;
			}
		}
	}

	return 0;
}

static int
sinowealth_probe(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_profile *profile = NULL;
	struct sinowealth_data *drv_data = NULL;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	rc = ratbag_find_hidraw(device, sinowealth_test_hidraw);
	if (rc)
		goto err;

	rc = sinowealth_set_active_profile(device, 0);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	rc = sinowealth_init_profile(device);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	ratbag_device_for_each_profile(device, profile) {
		sinowealth_update_profile_from_config(profile);
		sinowealth_update_profile_from_buttons(profile);
	}

	rc = sinowealth_get_active_profile(device);
	if (rc >= 0)
		log_debug(device->ratbag, "Current profile index: %d\n", rc);

	rc = sinowealth_get_debounce_time(device);
	if (rc >= 0)
		log_info(device->ratbag, "Debounce time: %d ms\n", rc);

	if (drv_data->is_long)
		sinowealth_print_long_lod_and_anglesnapping(device);

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

/* Update saved raw configuration data of the mouse with values from profile `profile`. */
static void
sinowealth_update_config_from_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->configs[profile->index];
	struct ratbag_led *led = NULL;
	struct ratbag_resolution *resolution = NULL;
	uint8_t dpi_enabled = 0;

	/* Update report rate. */
	uint8_t reported_rate = sinowealth_report_rate_to_raw(profile->hz);
	if (reported_rate == 0) {
		log_error(device->ratbag, "Incorrect report rate %u was requested\n", profile->hz);

		/* Fall back to 125hz. */
		reported_rate = sinowealth_report_rate_to_raw(125);
		profile->hz = 125;
	}
	config->report_rate = reported_rate;

	/* Check if any resolution requires independent XY DPIs */
	config->config_flags &= ~SINOWEALTH_XY_INDEPENDENT;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->dpi_x != resolution->dpi_y && resolution->dpi_x && resolution->dpi_y) {
			config->config_flags |= SINOWEALTH_XY_INDEPENDENT;
			break;
		}
	}

	config->dpi_count = 0;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dpi_x || !resolution->dpi_y)
			continue;

		/* Limit the resolution if it somehow got higher than allowed. */
		{
			const unsigned int max_dpi = sinowealth_get_max_dpi_for_sensor(config->sensor_type);
			resolution->dpi_x = min(resolution->dpi_x, max_dpi);
			resolution->dpi_y = min(resolution->dpi_y, max_dpi);
		}

		if (config->config_flags & SINOWEALTH_XY_INDEPENDENT) {
			config->dpis.independent[resolution->index].x = sinowealth_dpi_to_raw(device, resolution->dpi_x);
			config->dpis.independent[resolution->index].y = sinowealth_dpi_to_raw(device, resolution->dpi_y);
		} else {
			config->dpis.dpis[resolution->index] = sinowealth_dpi_to_raw(device, resolution->dpi_x);
		}
		dpi_enabled |= 1 << resolution->index;
		config->dpi_count++;
		if (resolution->is_active)
			config->active_dpi = config->dpi_count;
	}
	config->disabled_dpi_slots = ~dpi_enabled;

	/* Body lighting */
	if (drv_data->led_count > 0) {
		led = ratbag_profile_get_led(profile, 0);
		switch (led->mode) {
		case RATBAG_LED_OFF:
			config->rgb_effect = RGB_OFF;
			break;
		case RATBAG_LED_ON:
			config->rgb_effect = RGB_SINGLE;
			config->single_color = sinowealth_color_to_raw(device, led->color);
			break;
		case RATBAG_LED_CYCLE:
			config->rgb_effect = RGB_GLORIOUS;
			config->glorious_mode = sinowealth_led_to_rgb_mode(led);
			break;
		case RATBAG_LED_BREATHING:
			config->rgb_effect = RGB_BREATHING7;
			config->breathing7_mode = sinowealth_led_to_rgb_mode(led);
			config->breathing7_colorcount = 1;
			config->breathing7_colors[0] = sinowealth_color_to_raw(device, led->color);
			break;
		}
		ratbag_led_unref(led);
	} else {
		/* Reset the value in case we accidentally managed to set it when we were not supposed to. */
		config->rgb_effect = RGB_NOT_SUPPORTED;
	}
}

static int
sinowealth_commit(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_profile *profile = NULL;

	ratbag_device_for_each_profile(device, profile) {
		sinowealth_update_config_from_profile(profile);
		sinowealth_update_buttons_from_profile(profile);
	}

	rc = sinowealth_write_config(device);
	if (rc)
		return rc;

	rc = sinowealth_write_buttons(device);
	if (rc)
		return rc;

	rc = sinowealth_write_macros(device);
	if (rc)
		return rc;

	return 0;
}

static void
sinowealth_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver sinowealth_driver = {
	.name = "Sinowealth",
	.id = "sinowealth",
	.probe = sinowealth_probe,
	.remove = sinowealth_remove,
	.commit = sinowealth_commit,
	.set_active_profile = sinowealth_set_active_profile,
};
