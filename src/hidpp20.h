/*
 * HID++ 2.0 library - headers file.
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
 * Based on the HID++ 2.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#pragma once

#include <stdint.h>

#include "hidpp-generic.h"
#include "libratbag-util.h"

struct _hidpp20_message {
	uint8_t report_id;
	uint8_t device_idx;
	uint8_t sub_id;
	uint8_t address;
	uint8_t parameters[LONG_MESSAGE_LENGTH - 4U];
} __attribute__((packed));

union hidpp20_message {
	struct _hidpp20_message msg;
	uint8_t data[LONG_MESSAGE_LENGTH];
};

struct hidpp20_feature {
	uint16_t feature;
	uint8_t type;
};

struct hidpp20_device {
	struct hidpp_device base;
	unsigned int index;
	unsigned proto_major;
	unsigned proto_minor;
	unsigned feature_count;
	struct hidpp20_feature *feature_list;
};

int hidpp20_request_command(struct hidpp20_device *dev, union hidpp20_message *msg);

const char *hidpp20_feature_get_name(uint16_t feature);

/* -------------------------------------------------------------------------- */
/* generic hidpp20 device operations                                          */
/* -------------------------------------------------------------------------- */

struct hidpp20_device *
hidpp20_device_new(const struct hidpp_device *base, unsigned int idx);

void
hidpp20_device_destroy(struct hidpp20_device *device);

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT					0x0000

int hidpp_root_get_feature(struct hidpp20_device *device,
			   uint16_t feature,
			   uint8_t *feature_index,
			   uint8_t *feature_type,
			   uint8_t *feature_version);
int hidpp20_root_get_protocol_version(struct hidpp20_device *dev,
				      unsigned *major,
				      unsigned *minor);
/* -------------------------------------------------------------------------- */
/* 0x0001: Feature Set                                                        */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_FEATURE_SET				0x0001

/* -------------------------------------------------------------------------- */
/* 0x0003: Device Info                                                        */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_DEVICE_INFO				0x0003

/* -------------------------------------------------------------------------- */
/* 0x1000: Battery level status                                               */
/* -------------------------------------------------------------------------- */
#define HIDPP_PAGE_BATTERY_LEVEL_STATUS			0x1000

enum hidpp20_battery_status {
	BATTERY_STATUS_DISCHARGING = 0,
	BATTERY_STATUS_RECHARGING,
	BATTERY_STATUS_CHARGING_IN_FINAL_STATE,
	BATTERY_STATUS_CHARGE_COMPLETE,
	BATTERY_STATUS_RECHARGING_BELOW_OPTIMAL_SPEED,
	BATTERY_STATUS_INVALID_BATTERY_TYPE,
	BATTERY_STATUS_THERMAL_ERROR,
	BATTERY_STATUS_OTHER_CHARGING_ERROR,
	BATTERY_STATUS_INVALID,
};

/**
 * Retrieves the battery level status.
 *
 * @return the battery status or a negative errno on error
 */
int hidpp20_batterylevel_get_battery_level(struct hidpp20_device *device,
					   uint16_t *level,
					   uint16_t *next_level);

/* -------------------------------------------------------------------------- */
/* 0x1b00: KBD reprogrammable keys and mouse buttons                          */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS		0x1b00

enum hidpp2_controL_id_flags {
	HIDPP20_CONTROL_ID_FLAG_NONE = 0,
	HIDPP20_CONTROL_ID_FLAG_MOUSE_BUTTON = (1 << 0), /**< Is a mouse button */
	HIDPP20_CONTROL_ID_FLAG_FN_KEY = (1 << 1), /**< Is a fn button */
	HIDPP20_CONTROL_ID_FLAG_HOTKEY = (1 << 2), /**< Is a Hot key, not a standard kbd key */
	HIDPP20_CONTROL_ID_FLAG_FN_TOGGLE_AFFECTED = (1 << 3), /**< Fn toggle affects this key */
	HIDPP20_CONTROL_ID_FLAG_REPROGRAMMABLE = (1 << 4), /**< Key can be reprogrammed */
};

struct hidpp20_control_id {
	uint8_t index;
	uint16_t control_id;
	uint16_t task_id;
	uint8_t flags;
	/* fields below are only set for 0x1b04, not for 0x1b00 */
	uint8_t position;
	uint8_t group;
	uint8_t group_mask;
	uint8_t raw_XY;
	struct {
		uint8_t raw_XY;
		uint8_t persist;
		uint8_t divert;
		uint16_t remapped;
		int updated;
	} reporting;
};

int hidpp20_kbd_reprogrammable_keys_get_controls(struct hidpp20_device *device,
						 struct hidpp20_control_id **controls_list);

/* -------------------------------------------------------------------------- */
/* 0x1b04: Special keys and mouse buttons                                     */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_SPECIAL_KEYS_BUTTONS			0x1b04

/**
 * allocates a list of controls that has to be freed by the caller.
 *
 * returns the elements in the list or a negative error
 */
int hidpp20_special_key_mouse_get_controls(struct hidpp20_device *device,
					   struct hidpp20_control_id **controls_list);

/**
 * commit a control previously allocated by
 * hidpp20_special_key_mouse_get_controls().
 *
 * returns 0 or a negative error
 */
int hidpp20_special_key_mouse_set_control(struct hidpp20_device *device,
					  struct hidpp20_control_id *control);

const struct ratbag_button_action *hidpp20_1b04_get_logical_mapping(uint16_t value);
uint16_t hidpp20_1b04_get_logical_control_id(const struct ratbag_button_action *action);
const char *hidpp20_1b04_get_logical_mapping_name(uint16_t value);
enum ratbag_button_type hidpp20_1b04_get_physical_mapping(uint16_t value);
const char *hidpp20_1b04_get_physical_mapping_name(uint16_t value);

/* -------------------------------------------------------------------------- */
/* 0x2200: Mouse Pointer Basic Optical Sensors                                */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_MOUSE_POINTER_BASIC			0x2200

#define HIDDP20_MOUSE_POINTER_FLAGS_VERTICAL_TUNING	(1 << 4)
#define HIDDP20_MOUSE_POINTER_FLAGS_OS_BALLISTICS	(1 << 3)

#define HIDDP20_MOUSE_POINTER_FLAGS_ACCELERATION_MASK	0x03
#define HIDDP20_MOUSE_POINTER_ACCELERATION_NONE		0x00
#define HIDDP20_MOUSE_POINTER_ACCELERATION_LOW		0x01
#define HIDDP20_MOUSE_POINTER_ACCELERATION_MEDIUM	0x02
#define HIDDP20_MOUSE_POINTER_ACCELERATION_HIGH		0x03

int hidpp20_mousepointer_get_mousepointer_info(struct hidpp20_device *device,
					       uint16_t *resolution,
					       uint8_t *flags);

/* -------------------------------------------------------------------------- */
/* 0x2201: Adjustable DPI                                                     */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ADJUSTABLE_DPI			0x2201

/**
 * either dpi_steps is not null or the values are stored in the null terminated
 * array dpi_list.
 */
struct hidpp20_sensor {
	uint8_t index;
	uint16_t dpi;
	uint16_t dpi_min;
	uint16_t dpi_max;
	uint16_t dpi_steps;
	uint16_t default_dpi;
	uint16_t dpi_list[LONG_MESSAGE_LENGTH / 2 + 1];
};

/**
 * allocates a list of sensors that has to be freed by the caller.
 *
 * returns the elements in the list or a negative error
 */
int hidpp20_adjustable_dpi_get_sensors(struct hidpp20_device *device,
				       struct hidpp20_sensor **sensors_list);

/**
 * set the current dpi of the provided sensor. sensor must have been
 * allocated by  hidpp20_adjustable_dpi_get_sensors()
 */
int hidpp20_adjustable_dpi_set_sensor_dpi(struct hidpp20_device *device,
					  struct hidpp20_sensor *sensor, uint16_t dpi);

/* -------------------------------------------------------------------------- */
/* 0x8060 - Adjustable Report Rate                                            */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ADJUSTABLE_REPORT_RATE		0x8060

/* -------------------------------------------------------------------------- */
/* 0x8070v4 - Color LED effects                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_COLOR_LED_EFFECTS			0x8070


struct hidpp20_color_led_info;

struct hidpp20_color_led_zone_info;

int
hidpp20_color_led_effects_get_zone_info(struct hidpp20_device *device,
					uint8_t reg, struct hidpp20_color_led_zone_info *info);

int
hidpp20_color_led_effects_get_zone_infos(struct hidpp20_device *device,
					 struct hidpp20_color_led_zone_info **infos_list);

enum hidpp20_color_led_location {
	HIDPP20_COLOR_LED_LOCATION_UNDEFINED = 0,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY,
	HIDPP20_COLOR_LED_LOCATION_LOGO,
	HIDPP20_COLOR_LED_LOCATION_LEFT,
	HIDPP20_COLOR_LED_LOCATION_RIGHT,
	HIDPP20_COLOR_LED_LOCATION_COMBINED,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY_1,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY_2,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY_3,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY_4,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY_5,
	HIDPP20_COLOR_LED_LOCATION_PRIMARY_6,
};

enum hidpp20_color_led_persistency {
	HIDPP20_COLOR_LED_PERSISTENCY_UNSUPPORTED,
	HIDPP20_COLOR_LED_PERSISTENCY_ON,
	HIDPP20_COLOR_LED_PERSISTENCY_OFF,
	HIDPP20_COLOR_LED_PERSISTENCY_ON_OFF,
};

/* -------------------------------------------------------------------------- */
/* 0x8100 - Onboard Profiles                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ONBOARD_PROFILES			0x8100

#define HIDPP20_BUTTON_HID_TYPE				0x80
#define HIDPP20_BUTTON_HID_TYPE_MOUSE			0x01
#define HIDPP20_BUTTON_HID_TYPE_KEYBOARD		0x02
#define HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL	0x03
#define HIDPP20_BUTTON_SPECIAL				0x90
#define HIDPP20_BUTTON_MACRO				0x00
#define HIDPP20_BUTTON_DISABLED				0xFF

#define HIDPP20_MODIFIER_KEY_CTRL			0x01
#define HIDPP20_MODIFIER_KEY_SHIFT			0x02

#define HIDPP20_DPI_COUNT				5
#define HIDPP20_LED_COUNT				2

union hidpp20_button_binding {
	struct {
		uint8_t type;
	} any;
	struct {
		uint8_t type;
		uint8_t subtype;
	} subany;
	struct {
		uint8_t type;		/* HIDPP20_BUTTON_HID_TYPE */
		uint8_t subtype;	/* HIDPP20_BUTTON_HID_TYPE_MOUSE */
		uint16_t buttons;	/* flags when internal */
	} __attribute__((packed)) button;
	struct {
		uint8_t type;		/* HIDPP20_BUTTON_HID_TYPE */
		uint8_t subtype;	/* HIDPP20_BUTTON_HID_TYPE_KEYBOARD */
		uint8_t modifier_flags;
		uint8_t key;
	} __attribute__((packed)) keyboard_keys;
	struct {
		uint8_t type;		/* HIDPP20_BUTTON_HID_TYPE */
		uint8_t subtype;	/* HIDPP20_BUTTON_HID_TYPE_CONSUMER_CONTROL */
		uint16_t consumer_control;
	} __attribute__((packed)) consumer_control;
	struct {
		uint8_t type; /* HIDPP20_BUTTON_SPECIAL */
		uint8_t special;
	} __attribute__((packed)) special;
	struct {
		uint8_t type; /* HIDPP20_BUTTON_MACRO */
		uint8_t page;
		uint8_t zero;
		uint8_t offset;
	} __attribute__((packed)) macro;
	struct {
		uint8_t type; /* PROFILE_BUTTON_TYPE_DISABLED */
	} disabled;
} __attribute__((packed));
_Static_assert(sizeof(union hidpp20_button_binding) == 4, "Invalid size");

struct hidpp20_color_led_info {
	uint8_t zone_count;
	/* we don't care about NV capabilities for libratbag, they just
	 * indicate sale demo effects */
	uint16_t nv_caps;
	uint16_t ext_caps;
} __attribute__((packed));
_Static_assert(sizeof(struct hidpp20_color_led_info) == 5, "Invalid size");

struct hidpp20_color_led_zone_info {
	uint8_t index;
	uint16_t location;
	uint8_t num_effects;
	uint8_t persistency_caps;
} __attribute__((packed));
_Static_assert(sizeof(struct hidpp20_color_led_zone_info) == 5, "Invalid size");

struct hidpp20_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} __attribute__((packed));
_Static_assert(sizeof(struct hidpp20_color) == 3, "Invalid size");

enum hidpp20_led_type {
	HIDPP20_LED_UNKNOWN = -1,
	HIDPP20_LED_LOGO = 0,
	HIDPP20_LED_SIDE,
};

enum hidpp20_led_mode {
	HIDPP20_LED_OFF = 0x00,
	HIDPP20_LED_ON = 0x01,
	HIDPP20_LED_CYCLE = 0x03,
	HIDPP20_LED_BREATHING = 0x0a,
};

enum hidpp20_led_waveform {
	HIDPP20_LED_WF_DEFAULT = 0x00,
	HIDPP20_LED_WF_SINE = 0x01,
	HIDPP20_LED_WF_SQUARE = 0x02,
	HIDPP20_LED_WF_TRIANGLE = 0x03,
	HIDPP20_LED_WF_SAWTOOTH = 0x04,
	HIDPP20_LED_WF_SHARKFIN = 0x05,
	HIDPP20_LED_WF_EXPONENTIAL = 0x06,
};

struct hidpp20_internal_led {
	uint8_t mode; /* enum hidpp20_led_mode */
	union {
		struct hidpp20_led_fixed {
			struct hidpp20_color color;
			uint8_t effect;
		} __attribute__((packed)) fixed;
		struct hidpp20_led_cycle {
			uint8_t unused[5];
			uint16_t period_or_speed; /* period in ms, speed is device dependent */
			uint8_t intensity; /* 1 - 100 percent, 0 means 100 */
		} __attribute__((packed)) cycle;
		struct hidpp20_led_breath {
			struct hidpp20_color color;
			uint16_t period_or_speed; /* period in ms, speed is device dependent */
			uint8_t waveform; /* enum hidpp20_led_waveform */
			uint8_t intensity; /* 1 - 100 percent, 0 means 100 */
		} __attribute__((packed)) breath;
		uint8_t padding[10];
	} __attribute__((packed)) effect;
};
_Static_assert(sizeof(struct hidpp20_led_fixed) == 4, "Invalid size");
_Static_assert(sizeof(struct hidpp20_led_cycle) == 8, "Invalid size");
_Static_assert(sizeof(struct hidpp20_led_breath) == 7, "Invalid size");
_Static_assert(sizeof(struct hidpp20_internal_led) == 11, "Invalid size");

typedef uint8_t percent_t;

struct hidpp20_led {
	enum hidpp20_led_mode mode;
	struct hidpp20_color color;
	uint16_t period;
	percent_t brightness;
};

#define HIDPP20_MACRO_NOOP			0x01
#define HIDPP20_MACRO_DELAY			0x40
#define HIDPP20_MACRO_KEY_PRESS			0x43
#define HIDPP20_MACRO_KEY_RELEASE		0x44
#define HIDPP20_MACRO_JUMP			0x60
#define HIDPP20_MACRO_END			0xff

union hidpp20_macro_data {
	struct {
		uint8_t type;
	} __attribute__((packed)) any;
	struct {
		uint8_t type; /* HIDPP20_MACRO_DELAY */
		uint16_t time;
	} __attribute__((packed)) delay;
	struct {
		uint8_t type; /* HIDPP20_MACRO_KEY_PRESS or HIDPP20_MACRO_KEY_RELEASE */
		uint8_t modifier;
		uint8_t key;
	} __attribute__((packed)) key;
	struct {
		uint8_t type; /* HIDPP20_MACRO_JUMP */
		uint8_t offset;
		uint8_t page;
	} __attribute__((packed)) jump;
	struct {
		uint8_t type; /* HIDPP20_MACRO_END */
	} __attribute__((packed)) end;
} __attribute__((packed));
_Static_assert(sizeof(union hidpp20_macro_data) == 3, "Invalid size");

struct hidpp20_profile {
	uint8_t index;
	uint8_t enabled;
	char name[16 * 3];
	unsigned report_rate;
	unsigned default_dpi;
	unsigned switched_dpi;
	unsigned current_dpi;
	uint16_t dpi[HIDPP20_DPI_COUNT];
	union hidpp20_button_binding buttons[32];
	union hidpp20_macro_data *macros[32];
	struct hidpp20_led leds[HIDPP20_LED_COUNT];
};

struct hidpp20_onboard_profiles_info {
	uint8_t memory_model_id;
	uint8_t profile_format_id;
	uint8_t macro_format_id;
	uint8_t profile_count;
	uint8_t profile_count_oob;
	uint8_t button_count;
	uint8_t sector_count;
	uint16_t sector_size;
	uint8_t mechanical_layout;
	uint8_t various_info;
	uint8_t reserved[5];
} __attribute__((packed));
_Static_assert(sizeof(struct hidpp20_onboard_profiles_info) == 16, "Invalid size");

struct hidpp20_profiles {
	uint8_t num_profiles;
	uint8_t num_rom_profiles;
	uint8_t num_buttons;
	uint8_t num_modes;
	uint8_t num_leds;
	uint8_t has_g_shift;
	uint8_t has_dpi_shift;
	uint8_t corded;
	uint8_t wireless;
	uint8_t sector_count;
	uint16_t sector_size;
	struct hidpp20_profile *profiles;
};

/**
 * fetches the profiles description as reported by the mouse.
 *
 * returns 0 or a negative error.
 */
int
hidpp20_onboard_profiles_get_profiles_desc(struct hidpp20_device *device,
					   struct hidpp20_onboard_profiles_info *info);

/**
 * allocates a list of profiles that has to be destroyed by the caller.
 * The caller must use hidpp20_onboard_profiles_destroy() to free the memory.
 *
 * returns the number of profiles in the list or a negative error
 */
int hidpp20_onboard_profiles_allocate(struct hidpp20_device *device,
					struct hidpp20_profiles **profiles_list);

/**
 * free a list of profiles allocated by hidpp20_onboard_profiles_allocate()
 */
void
hidpp20_onboard_profiles_destroy(struct hidpp20_profiles *profiles_list);

/**
 * initialize a struct hidpp20_profiles previous allocated with
 * hidpp20_onboard_profiles_allocate().
 */
int
hidpp20_onboard_profiles_initialize(struct hidpp20_device *device,
				    struct hidpp20_profiles *profiles);

/**
 * return the current profile index or a negative error.
 */
int hidpp20_onboard_profiles_get_current_profile(struct hidpp20_device *device);

/**
 * Sets the current profile index.
 * Indexes are 1-indexed.
 *
 * return 0 or a negative error.
 */
int
hidpp20_onboard_profiles_set_current_profile(struct hidpp20_device *device,
					     uint8_t index);

/**
 * return the current dpi index of the current active profile
 * or a negative error.
 */
int hidpp20_onboard_profiles_get_current_dpi_index(struct hidpp20_device *device);

/**
 * Sets the current dpi index on the current active profile.
 * Indexes are 0-indexed.
 *
 * return 0 or a negative error.
 */
int
hidpp20_onboard_profiles_set_current_dpi_index(struct hidpp20_device *device,
					       uint8_t index);

/**
 * Write the internal state of the device onto the FLASH.
 */
int
hidpp20_onboard_profiles_commit(struct hidpp20_device *device,
				struct hidpp20_profiles *profiles_list);

enum ratbag_button_action_special
hidpp20_onboard_profiles_get_special(uint8_t code);

uint8_t
hidpp20_onboard_profiles_get_code_from_special(enum ratbag_button_action_special special);

int
hidpp20_onboard_profiles_read_sector(struct hidpp20_device *device,
				     uint16_t sector,
				     uint16_t sector_size,
				     uint8_t *data);

static inline uint8_t *
hidpp20_onboard_profiles_allocate_sector(struct hidpp20_profiles *profiles)
{
	return zalloc(profiles->sector_size);
}

/* -------------------------------------------------------------------------- */
/* 0x8110 - Mouse Button Spy                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_MOUSE_BUTTON_SPY			0x8110


