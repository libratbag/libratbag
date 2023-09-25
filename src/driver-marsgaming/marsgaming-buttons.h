#pragma once
#include "libratbag-private.h"

enum marsgaming_button_action {
	MARSGAMING_MM4_ACTION_LEFT_CLICK = 0x01,
	MARSGAMING_MM4_ACTION_RIGHT_CLICK = 0x02,
	MARSGAMING_MM4_ACTION_MIDDLE_CLICK = 0x03,
	MARSGAMING_MM4_ACTION_BACKWARD = 0x04,
	MARSGAMING_MM4_ACTION_FORWARD = 0x05,
	MARSGAMING_MM4_ACTION_UNKNOWN_6 = 0x06,
	MARSGAMING_MM4_ACTION_UNKNOWN_7 = 0x07,
	MARSGAMING_MM4_ACTION_DPI_SWITCH = 0x08, // DPI_CYCLE_UP
	MARSGAMING_MM4_ACTION_DPI_MINUS = 0x09,
	MARSGAMING_MM4_ACTION_DPI_PLUS = 0x0a,
	MARSGAMING_MM4_ACTION_UNKNOWN_B = 0x0b,
	MARSGAMING_MM4_ACTION_UNKNOWN_C = 0x0c,
	MARSGAMING_MM4_ACTION_PROFILE_SWITCH = 0x0d,
	MARSGAMING_MM4_ACTION_DISABLE = 0x0e, // Same code as media, but with null additional data. Will handle them the same
	MARSGAMING_MM4_ACTION_MEDIA = 0x0e,
	MARSGAMING_MM4_ACTION_COMBO_KEY = 0x0f,
	MARSGAMING_MM4_ACTION_SINGLE_KEY = 0x10,
	MARSGAMING_MM4_ACTION_MACRO = 0x11, // TODO: Implement macros
	MARSGAMING_MM4_ACTION_FIRE = 0x12 // Execute left button key X times with specified delay
} __attribute__((packed));
_Static_assert(sizeof(enum marsgaming_button_action) == sizeof(uint8_t), "enum marsgaming_button_action should be 1 byte");

struct marsgaming_button_action_media_data {
	uint8_t zero_1;
	uint8_t media_key;
	uint8_t zero_3;
};

struct marsgaming_button_action_combo_key_data {
	uint8_t modifiers;
	uint8_t keys[2];
};

struct marsgaming_button_action_single_key_data {
	uint8_t zero_0;
	uint8_t key;
	uint8_t zero_2;
};

struct marsgaming_button_action_macro_data {
	uint8_t macro_id;
	uint8_t macro_length; // Maybe?
	uint8_t unknown_2;
};

struct marsgaming_button_action_fire_data {
	uint8_t times;
	uint8_t delay_ms;
	uint8_t unknown_2;
};

struct marsgaming_button_info {
	enum marsgaming_button_action action;
	union {
		struct marsgaming_button_action_media_data media;
		struct marsgaming_button_action_combo_key_data combo_key;
		struct marsgaming_button_action_single_key_data single_key;
		struct marsgaming_button_action_macro_data macro;
		struct marsgaming_button_action_fire_data fire;
	} action_info;
};

_Static_assert(sizeof(struct marsgaming_button_info) == 4, "Marsgaming button info is not 4 bytes long");

struct marsgaming_optional_button_info {
	unsigned is_present;
	union {
		uint32_t none;
		struct marsgaming_button_info button_info;
	};
};

struct ratbag_button_action
marsgaming_parse_button_to_action(struct ratbag_button *button,
				  const struct marsgaming_button_info *button_info);

struct marsgaming_optional_button_info
marsgaming_button_of_type(struct ratbag_button *button);
