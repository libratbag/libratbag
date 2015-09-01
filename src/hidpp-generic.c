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

static const struct hidpp20_1b04_action_mapping hidpp20_1b04_logical_mapping[] =
{
	{ 0, "None"			, BUTTON_ACTION_NONE },
	{ 1, "Volume Up"		, BUTTON_ACTION_KEY(KEY_VOLUMEUP) },
	{ 2, "Volume Down"		, BUTTON_ACTION_KEY(KEY_VOLUMEDOWN) },
	{ 3, "Mute"			, BUTTON_ACTION_KEY(KEY_MUTE) },
	{ 4, "Play/Pause"		, BUTTON_ACTION_KEY(KEY_PLAYPAUSE) },
	{ 5, "Next"			, BUTTON_ACTION_KEY(KEY_NEXTSONG) },
	{ 6, "Previous"			, BUTTON_ACTION_KEY(KEY_PREVIOUSSONG) },
	{ 7, "Stop"			, BUTTON_ACTION_KEY(KEY_STOPCD) },
	{ 80, "Left"			, BUTTON_ACTION_BUTTON(1) },
	{ 81, "Right"			, BUTTON_ACTION_BUTTON(2) },
	{ 82, "Middle"			, BUTTON_ACTION_BUTTON(3) },
	{ 83, "Back"			, BUTTON_ACTION_BUTTON(4) },
	{ 86, "Forward"			, BUTTON_ACTION_BUTTON(5) },
	{ 195, "AppSwitchGesture"	, BUTTON_ACTION_NONE },
	{ 196, "SmartShift"		, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH) },
	{ 315, "LedToggle"		, BUTTON_ACTION_NONE },
};

struct hidpp20_1b04_physical_mapping {
	uint16_t value;
	const char *name;
	enum ratbag_button_type type;
};

static const struct hidpp20_1b04_physical_mapping hidpp20_1b04_physical_mapping[] =
{
	{ 0, "None"			, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 1, "Volume Up"		, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 2, "Volume Down"		, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 3, "Mute"			, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 4, "Play/Pause"		, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 5, "Next"			, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 6, "Previous"			, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 7, "Stop"			, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 56, "Left Click"		, RATBAG_BUTTON_TYPE_LEFT },
	{ 57, "Right Click"		, RATBAG_BUTTON_TYPE_RIGHT },
	{ 58, "Middle Click"		, RATBAG_BUTTON_TYPE_MIDDLE },
	{ 59, "Wheel Side Click Left"	, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 60, "Back Click"		, RATBAG_BUTTON_TYPE_SIDE },
	{ 61, "Wheel Side Click Right"	, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 62, "Forward Click"		, RATBAG_BUTTON_TYPE_EXTRA },
	{ 156, "Gesture Button"		, RATBAG_BUTTON_TYPE_UNKNOWN },
	{ 157, "SmartShift"		, RATBAG_BUTTON_TYPE_WHEEL_RATCHET_MODE_SHIFT },
	{ 221, "LedToggle"		, RATBAG_BUTTON_TYPE_UNKNOWN },
};

const struct ratbag_button_action *
hidpp20_1b04_get_logical_mapping(uint16_t value)
{
	const struct hidpp20_1b04_action_mapping *map;

	ARRAY_FOR_EACH(hidpp20_1b04_logical_mapping, map) {
		if (map->value == value)
			return &map->action;
	}

	return RATBAG_BUTTON_TYPE_UNKNOWN;
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

