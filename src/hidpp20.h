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

#ifndef HIDPP_20_H
#define HIDPP_20_H

#include <stdint.h>

#include "hidpp-generic.h"

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

int hidpp20_request_command(struct hidpp_device *dev, union hidpp20_message *msg);

const char *hidpp20_feature_get_name(uint16_t feature);

/* -------------------------------------------------------------------------- */
/* 0x0000: Root                                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ROOT					0x0000

int hidpp_root_get_feature(struct hidpp_device *devdevice,
			   uint16_t feature,
			   uint8_t *feature_index,
			   uint8_t *feature_type,
			   uint8_t *feature_version);
int hidpp20_root_get_protocol_version(struct hidpp_device *dev,
				      unsigned *major,
				      unsigned *minor);
/* -------------------------------------------------------------------------- */
/* 0x0001: Feature Set                                                        */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_FEATURE_SET				0x0001

struct hidpp20_feature {
	uint16_t feature;
	uint8_t type;
};

/**
 * allocates a list of features that has to be freed by the caller.
 *
 * returns the elements in the list or a negative error
 */
int hidpp20_feature_set_get(struct hidpp_device *device,
			    struct hidpp20_feature **feature_list);

/* -------------------------------------------------------------------------- */
/* 0x0001: Device Info                                                        */
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
int hidpp20_batterylevel_get_battery_level(struct hidpp_device *device,
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

int hidpp20_kbd_reprogrammable_keys_get_controls(struct hidpp_device *device,
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
int hidpp20_special_key_mouse_get_controls(struct hidpp_device *device,
					   struct hidpp20_control_id **controls_list);

/**
 * commit a control previously allocated by
 * hidpp20_special_key_mouse_get_controls().
 *
 * returns 0 or a negative error
 */
int hidpp20_special_key_mouse_set_control(struct hidpp_device *device,
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

int hidpp20_mousepointer_get_mousepointer_info(struct hidpp_device *device,
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
int hidpp20_adjustable_dpi_get_sensors(struct hidpp_device *device,
				       struct hidpp20_sensor **sensors_list);

/**
 * set the current dpi of the provided sensor. sensor must have been
 * allocated by  hidpp20_adjustable_dpi_get_sensors()
 */
int hidpp20_adjustable_dpi_set_sensor_dpi(struct hidpp_device *device,
					  struct hidpp20_sensor *sensor, uint16_t dpi);

/* -------------------------------------------------------------------------- */
/* 0x8060 - Adjustable Report Rate                                            */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ADJUSTABLE_REPORT_RATE		0x8060

/* -------------------------------------------------------------------------- */
/* 0x8070v4 - Color LED effects                                               */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_COLOR_LED_EFFECTS			0x8070

/* -------------------------------------------------------------------------- */
/* 0x8100 - Onboard Profiles                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_ONBOARD_PROFILES			0x8100

#define HIDPP20_BUTTON_HID_MOUSE		0x81
#define HIDPP20_BUTTON_HID_KEYBOARD		0x82
#define HIDPP20_BUTTON_HID_CONSUMER_CONTROL	0x83
#define HIDPP20_BUTTON_SPECIAL			0x90
#define HIDPP20_BUTTON_DISABLED			0xFF

#define HIDPP20_MODIFIER_KEY_CTRL	0x01
#define HIDPP20_MODIFIER_KEY_SHIFT	0x02

struct hidpp20_profile {
	uint8_t index;
	uint8_t enabled;
	unsigned report_rate;
	unsigned default_dpi;
	unsigned switched_dpi;
	uint16_t dpi[5];
	struct {
		uint8_t type;
		uint8_t modifiers;
		uint8_t code;
	} buttons[32];
};

struct hidpp20_profiles {
	uint8_t feature_index;
	uint8_t num_profiles;
	uint8_t num_buttons;
	uint8_t num_modes;
	struct hidpp20_profile profiles[5];
};

/**
 * allocates a list of profiles that has to be freed by the caller.
 *
 * returns the number of profiles in the list or a negative error
 */
int hidpp20_onboard_profiles_allocate(struct hidpp_device *device,
					struct hidpp20_profiles **profiles_list);

/**
 * return the current profile index or a negative error.
 */
int hidpp20_onboard_profiles_get_current_profile(struct hidpp_device *device,
					struct hidpp20_profiles *profiles_list);

/**
 * parse a given profile from the mouse and fill in the right profile in
 * profiles_list.
 *
 * return 0 or a negative error.
 */
int hidpp20_onboard_profiles_read(struct hidpp_device *device,
				  unsigned int index,
				  struct hidpp20_profiles *profiles_list);


enum ratbag_button_action_special
hidpp20_onboard_profiles_get_special(struct hidpp_device *device,
				     uint8_t code);

/* -------------------------------------------------------------------------- */
/* 0x8110 - Mouse Button Spy                                                  */
/* -------------------------------------------------------------------------- */

#define HIDPP_PAGE_MOUSE_BUTTON_SPY			0x8110



#endif /* HIDPP_20_H */
