/*
 * HID++ generic definitions
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
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#include "config.h"

#include "hidpp-generic.h"

#include <stddef.h>

#include "libratbag-private.h"

const char *hidpp_errors[0xFF] = {
	[0x00] = "ERR_SUCCESS",
	[0x01] = "ERR_INVALID_SUBID",
	[0x02] = "ERR_INVALID_ADDRESS",
	[0x03] = "ERR_INVALID_VALUE",
	[0x04] = "ERR_CONNECT_FAIL",
	[0x05] = "ERR_TOO_MANY_DEVICES",
	[0x06] = "ERR_ALREADY_EXISTS",
	[0x07] = "ERR_BUSY",
	[0x08] = "ERR_UNKNOWN_DEVICE",
	[0x09] = "ERR_RESOURCE_ERROR",
	[0x0A] = "ERR_REQUEST_UNAVAILABLE",
	[0x0B] = "ERR_INVALID_PARAM_VALUE",
	[0x0C] = "ERR_WRONG_PIN_CODE",
	[0x0D ... 0xFE] = NULL,
};

struct hidpp20_1b04_action_mapping {
	uint16_t value;
	const char *name;
	struct ratbag_button_action action;
};

#define BUTTON_ACTION_MAPPING(v_, n_, act_) \
 { v_, n_, act_ }

static const struct hidpp20_1b04_action_mapping hidpp20_1b04_logical_mapping[] =
{
	BUTTON_ACTION_MAPPING(0, "None"			, BUTTON_ACTION_NONE),
	BUTTON_ACTION_MAPPING(1, "Volume Up"		, BUTTON_ACTION_KEY(KEY_VOLUMEUP)),
	BUTTON_ACTION_MAPPING(2, "Volume Down"		, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN)),
	BUTTON_ACTION_MAPPING(3, "Mute"			, BUTTON_ACTION_KEY(KEY_MUTE)),
	BUTTON_ACTION_MAPPING(4, "Play/Pause"		, BUTTON_ACTION_KEY(KEY_PLAYPAUSE)),
	BUTTON_ACTION_MAPPING(5, "Next"			, BUTTON_ACTION_KEY(KEY_NEXTSONG)),
	BUTTON_ACTION_MAPPING(6, "Previous"		, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG)),
	BUTTON_ACTION_MAPPING(7, "Stop"			, BUTTON_ACTION_KEY(KEY_STOPCD)),
	BUTTON_ACTION_MAPPING(80, "Left"		, BUTTON_ACTION_BUTTON(1)),
	BUTTON_ACTION_MAPPING(81, "Right"		, BUTTON_ACTION_BUTTON(2)),
	BUTTON_ACTION_MAPPING(82, "Middle"		, BUTTON_ACTION_BUTTON(3)),
	BUTTON_ACTION_MAPPING(83, "Back"		, BUTTON_ACTION_BUTTON(4)),
	BUTTON_ACTION_MAPPING(86, "Forward"		, BUTTON_ACTION_BUTTON(5)),
	BUTTON_ACTION_MAPPING(89, "Button 6"		, BUTTON_ACTION_BUTTON(6)),
	BUTTON_ACTION_MAPPING(90, "Button 7"		, BUTTON_ACTION_BUTTON(7)),
	BUTTON_ACTION_MAPPING(91, "Left Scroll"		, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT)),
	BUTTON_ACTION_MAPPING(92, "Button 8"		, BUTTON_ACTION_BUTTON(8)),
	BUTTON_ACTION_MAPPING(93, "Right Scroll"	, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT)),
	BUTTON_ACTION_MAPPING(94, "Button 9"		, BUTTON_ACTION_BUTTON(9)),
	BUTTON_ACTION_MAPPING(95, "Button 10"		, BUTTON_ACTION_BUTTON(10)),
	BUTTON_ACTION_MAPPING(96, "Button 11"		, BUTTON_ACTION_BUTTON(11)),
	BUTTON_ACTION_MAPPING(97, "Button 12"		, BUTTON_ACTION_BUTTON(12)),
	BUTTON_ACTION_MAPPING(98, "Button 13"		, BUTTON_ACTION_BUTTON(13)),
	BUTTON_ACTION_MAPPING(99, "Button 14"		, BUTTON_ACTION_BUTTON(14)),
	BUTTON_ACTION_MAPPING(100, "Button 15"		, BUTTON_ACTION_BUTTON(15)),
	BUTTON_ACTION_MAPPING(101, "Button 16"		, BUTTON_ACTION_BUTTON(16)),
	BUTTON_ACTION_MAPPING(102, "Button 17"		, BUTTON_ACTION_BUTTON(17)),
	BUTTON_ACTION_MAPPING(103, "Button 18"		, BUTTON_ACTION_BUTTON(18)),
	BUTTON_ACTION_MAPPING(104, "Button 19"		, BUTTON_ACTION_BUTTON(19)),
	BUTTON_ACTION_MAPPING(105, "Button 20"		, BUTTON_ACTION_BUTTON(20)),
	BUTTON_ACTION_MAPPING(106, "Button 21"		, BUTTON_ACTION_BUTTON(21)),
	BUTTON_ACTION_MAPPING(107, "Button 22"		, BUTTON_ACTION_BUTTON(22)),
	BUTTON_ACTION_MAPPING(108, "Button 23"		, BUTTON_ACTION_BUTTON(23)),
	BUTTON_ACTION_MAPPING(109, "Button 24"		, BUTTON_ACTION_BUTTON(24)),
	BUTTON_ACTION_MAPPING(184, "Second Left"	, BUTTON_ACTION_BUTTON(1)),
	BUTTON_ACTION_MAPPING(195, "AppSwitchGesture"	, BUTTON_ACTION_NONE),
	BUTTON_ACTION_MAPPING(196, "SmartShift"		, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH)),
	BUTTON_ACTION_MAPPING(315, "LedToggle"		, BUTTON_ACTION_NONE),
};

struct hidpp20_1b04_physical_mapping {
	uint16_t value;
	const char *name;
	enum ratbag_button_type type;
};

#define BUTTON_PHYS_MAPPING(v_, n_, t_) \
 { v_, n_, t_ }

static const struct hidpp20_1b04_physical_mapping hidpp20_1b04_physical_mapping[] =
{
	BUTTON_PHYS_MAPPING(0, "None"			, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(1, "Volume Up"		, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(2, "Volume Down"		, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(3, "Mute"			, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(4, "Play/Pause"		, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(5, "Next"			, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(6, "Previous"		, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(7, "Stop"			, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(56, "Left Click"		, RATBAG_BUTTON_TYPE_LEFT),
	BUTTON_PHYS_MAPPING(57, "Right Click"		, RATBAG_BUTTON_TYPE_RIGHT),
	BUTTON_PHYS_MAPPING(58, "Middle Click"		, RATBAG_BUTTON_TYPE_MIDDLE),
	BUTTON_PHYS_MAPPING(59, "Wheel Side Click Left"	, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(60, "Back Click"		, RATBAG_BUTTON_TYPE_SIDE),
	BUTTON_PHYS_MAPPING(61, "Wheel Side Click Right", RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(62, "Forward Click"		, RATBAG_BUTTON_TYPE_EXTRA),
	BUTTON_PHYS_MAPPING(63, "Left Scroll"		, RATBAG_BUTTON_TYPE_WHEEL_LEFT),
	BUTTON_PHYS_MAPPING(64, "Right Scroll"		, RATBAG_BUTTON_TYPE_WHEEL_RIGHT),
	BUTTON_PHYS_MAPPING(98, "Do Nothing"		, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(156, "Gesture Button"	, RATBAG_BUTTON_TYPE_UNKNOWN),
	BUTTON_PHYS_MAPPING(157, "SmartShift"		, RATBAG_BUTTON_TYPE_WHEEL_RATCHET_MODE_SHIFT),
	BUTTON_PHYS_MAPPING(221, "LedToggle"		, RATBAG_BUTTON_TYPE_UNKNOWN),
};

const struct ratbag_button_action *
hidpp20_1b04_get_logical_mapping(uint16_t value)
{
	const struct hidpp20_1b04_action_mapping *map;

	ARRAY_FOR_EACH(hidpp20_1b04_logical_mapping, map) {
		if (map->value == value)
			return &map->action;
	}

	return NULL;
}

uint16_t
hidpp20_1b04_get_logical_control_id(const struct ratbag_button_action *action)
{
	const struct hidpp20_1b04_action_mapping *mapping;

	ARRAY_FOR_EACH(hidpp20_1b04_logical_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->value;
	}

	return 0;
}

const char *
hidpp20_1b04_get_logical_mapping_name(uint16_t value)
{
	const struct hidpp20_1b04_action_mapping *mapping;

	ARRAY_FOR_EACH(hidpp20_1b04_logical_mapping, mapping) {
		if (mapping->value == value)
			return mapping->name;
	}

	return "UNKNOWN";
}

enum ratbag_button_type
hidpp20_1b04_get_physical_mapping(uint16_t value)
{
	const struct hidpp20_1b04_physical_mapping *map;

	ARRAY_FOR_EACH(hidpp20_1b04_physical_mapping, map) {
		if (map->value == value)
			return map->type;
	}

	return RATBAG_BUTTON_TYPE_UNKNOWN;
}

const char *
hidpp20_1b04_get_physical_mapping_name(uint16_t value)
{
	const struct hidpp20_1b04_physical_mapping *map;

	ARRAY_FOR_EACH(hidpp20_1b04_physical_mapping, map) {
		if (map->value == value)
			return map->name;
	}

	return "UNKNOWN";
}

