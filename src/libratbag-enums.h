/*
 * Copyright © 2017 Red Hat, Inc.
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

#pragma once

/**
 * This file contains enum values that are used in the DBus API and thus
 * considered ABI.
 */

/**
 * @defgroup enums Enumerations used in the DBus API
 */

/**
 * @ingroup enums
 *
 * Error codes used by libratbag.
 */
enum ratbag_error_code {
	RATBAG_SUCCESS = 0,

	/**
	 * An error occured on the device. Either the device is not a
	 * libratbag device or communication with the device failed.
	 */
	RATBAG_ERROR_DEVICE = -1000,

	/**
	 * Insufficient capabilities. This error occurs when a requested change is
	 * beyond the device's capabilities.
	 */
	RATBAG_ERROR_CAPABILITY = -1001,

	/**
	 * Invalid value or value range. The provided value or value range
	 * is outside of the legal or supported range.
	 */
	RATBAG_ERROR_VALUE = -1002,

	/**
	 * A low-level system error has occured, e.g. a failure to access
	 * files that should be there. This error is usually unrecoverable
	 * and libratbag will print a log message with details about the
	 * error.
	 */
	RATBAG_ERROR_SYSTEM = -1003,

	/**
	 * Implementation bug, either in libratbag or in the caller. This
	 * error is usually unrecoverable and libratbag will print a log
	 * message with details about the
	 * error.
	 */
	RATBAG_ERROR_IMPLEMENTATION = -1004,
};

/**
 * @ingroup enums
 */
enum ratbag_device_capability {
	RATBAG_DEVICE_CAP_NONE = 0,

	/**
	 * The device has the capability to query the current hardware
	 * configuration. If this capability is missing, libratbag cannot
	 * query the device for its current configuration and the
	 * configured resolutions and button mappings are unknown.
	 * libratbag will still provide information about the structure of
	 * the device such as the number of buttons and resolutions.
	 * Clients that encounter a device without this resolution are
	 * encouraged to upload a configuration stored on-disk to the
	 * device to reset the device to a known state.
	 *
	 * Any changes uploaded to the device will be cached in libratbag,
	 * once a client has sent a full configuration to the device
	 * libratbag can be used to query the device as normal.
	 */
	RATBAG_DEVICE_CAP_QUERY_CONFIGURATION,

	/**
	 * The device provides read and/or write access to one or more
	 * resolutions.
	 */
	RATBAG_DEVICE_CAP_RESOLUTION = 100,

	/**
	 * The device can change resolution, either software-controlled or
	 * by a hardware button.
	 *
	 * FIXME: what about devices that only have hw buttons? can we
	 * query that, even if we can't switch it ourselves? Maybe better to
	 * have a separate cap for that then.
	 */
	RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION,

	/**
	 * The device provides read and/or write access to one or more
	 * profiles.
	 */
	RATBAG_DEVICE_CAP_PROFILE = 200,

	/**
	 * The device can switch between hardware profiles.
	 * A device with this capability can store multiple profiles in the
	 * hardware and provides the ability to switch between the profiles,
	 * possibly with a button.
	 * Devices without this capability will only have a single profile.
	 */
	RATBAG_DEVICE_CAP_SWITCHABLE_PROFILE,

	/**
	 * The device has the capability to disable and enable profiles.  While
	 * profiles are not immediately deleted after being disabled, it is not
	 * guaranteed that the device will remember any disabled profiles the
	 * next time ratbag runs. Furthermore, the order of profiles may get
	 * changed the next time ratbag runs if profiles are disabled.
	 */
	RATBAG_DEVICE_CAP_DISABLE_PROFILE,

	/**
	 * The device can have one profile assigned as a default profile.
	 * A default profile is the profile that is selected when the device
	 * is plugged in. Devices without this capability may select the
	 * last-used profile or a specific profile (usually the first).
	 */
	RATBAG_DEVICE_CAP_DEFAULT_PROFILE,

	/**
	 * The device provides read and/or write access to one or more
	 * buttons.
	 */
	RATBAG_DEVICE_CAP_BUTTON = 300,

	/**
	 * The device supports assigning button numbers, key events or key +
	 * modifier combinations.
	 */
	RATBAG_DEVICE_CAP_BUTTON_KEY,

	/**
	 * The device supports user-defined key or button sequences.
	 */
	RATBAG_DEVICE_CAP_BUTTON_MACROS,

	/**
	 * The device supports assigning LED colors and effects
	 */
	RATBAG_DEVICE_CAP_LED = 400,
};

/**
 * @ingroup enums
 */
enum ratbag_profile_capability {
	RATBAG_PROFILE_CAP_NONE = 0,

	/**
	 * The device has the capability to write a name to a profile
	 * and store it in its flash.
	 *
	 * Any changes uploaded to the device will be cached in libratbag
	 * and will be committed to it after a commit only.
	 */
	RATBAG_PROFILE_CAP_WRITABLE_NAME = 100,
};

/**
 * @ingroup enums
 */
enum ratbag_resolution_capability {
	/**
	 * The report rate can be set per resolution mode. If this property
	 * is not available, all resolutions within the same profile have
	 * the same report rate and changing one changes the others.
	 */
	RATBAG_RESOLUTION_CAP_INDIVIDUAL_REPORT_RATE = 1,

	/**
	 * The resolution can be set for x and y separately.
	 */
	RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION,
};

/**
 * @ingroup enums
 *
 * Button types describing the physical button.
 *
 * This enum is deprecated and should not be used.
 *
 * @deprecated
 */
enum ratbag_button_type {
	RATBAG_BUTTON_TYPE_UNKNOWN = 0,

	/* mouse buttons */
	RATBAG_BUTTON_TYPE_LEFT,
	RATBAG_BUTTON_TYPE_MIDDLE,
	RATBAG_BUTTON_TYPE_RIGHT,
	RATBAG_BUTTON_TYPE_THUMB,
	RATBAG_BUTTON_TYPE_THUMB2,
	RATBAG_BUTTON_TYPE_THUMB3,
	RATBAG_BUTTON_TYPE_THUMB4,
	RATBAG_BUTTON_TYPE_WHEEL_LEFT,
	RATBAG_BUTTON_TYPE_WHEEL_RIGHT,
	RATBAG_BUTTON_TYPE_WHEEL_CLICK,
	RATBAG_BUTTON_TYPE_WHEEL_UP,
	RATBAG_BUTTON_TYPE_WHEEL_DOWN,
	/**
	 * A button to toggle the wheel from free-spinning to click-based.
	 */
	RATBAG_BUTTON_TYPE_WHEEL_RATCHET_MODE_SHIFT,
	RATBAG_BUTTON_TYPE_EXTRA,
	RATBAG_BUTTON_TYPE_SIDE,
	RATBAG_BUTTON_TYPE_PINKIE,
	RATBAG_BUTTON_TYPE_PINKIE2,

	/* DPI switch */
	RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP,
	RATBAG_BUTTON_TYPE_RESOLUTION_UP,
	RATBAG_BUTTON_TYPE_RESOLUTION_DOWN,

	/* Profile */
	RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP,
	RATBAG_BUTTON_TYPE_PROFILE_UP,
	RATBAG_BUTTON_TYPE_PROFILE_DOWN,
};

/**
 * @ingroup enums
 *
 * The type assigned to a button.
 */
enum ratbag_button_action_type {
	/**
	 * Button is disabled
	 */
	RATBAG_BUTTON_ACTION_TYPE_NONE = 0,
	/**
	 * Button sends numeric button events
	 */
	RATBAG_BUTTON_ACTION_TYPE_BUTTON,
	/**
	 * Button triggers a mouse-specific special function. This includes
	 * resolution changes and profile changes.
	 */
	RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
	/**
	 * Button sends a key or key + modifier combination
	 */
	RATBAG_BUTTON_ACTION_TYPE_KEY,
	/**
	 * Button sends a user-defined key or button sequence
	 */
	RATBAG_BUTTON_ACTION_TYPE_MACRO,
	/**
	 * Button action is unknown
	 */
	RATBAG_BUTTON_ACTION_TYPE_UNKNOWN = 1000,
};

/**
 * @ingroup enums
 */
enum ratbag_button_action_special {
	/**
	 * This button is not set up for a special action
	 */
	RATBAG_BUTTON_ACTION_SPECIAL_INVALID = -1,
	RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN = (1 << 30),

	RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK,

	/* Wheel mappings */
	RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT,
	RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT,
	RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP,
	RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN,
	RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH,

	/* DPI switch */
	RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP,
	RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN,
	RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP,
	RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN,
	RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE,
	RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT,

	/* Profile */
	RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP,
	RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_DOWN,
	RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP,
	RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN,

	/* second mode for buttons */
	RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE,

	/* battery level */
	RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL,

};

/**
 * @ingroup enums
 *
 * Each LED mode has different properties, e.g. the brightness and rate are only
 * available in modes @ref RATBAG_LED_CYCLE and @ref RATBAG_LED_BREATHING modes
 */
enum ratbag_led_mode {
	/**
	 * led is now off
	 */
	RATBAG_LED_OFF = 0,

	/**
	 * led is on with static color
	 */
	RATBAG_LED_ON,

	/**
	 * led is cycling between all colors
	 */
	RATBAG_LED_CYCLE,

	/**
	 * led is pulsating with static color
	 */
	RATBAG_LED_BREATHING,
};
/**
 * @ingroup enums
 *
 * LED types, usually based on their physical location
 */
enum ratbag_led_type {
	RATBAG_LED_TYPE_LOGO = 1,
	RATBAG_LED_TYPE_SIDE,
	RATBAG_LED_TYPE_BATTERY,
	RATBAG_LED_TYPE_DPI,
	RATBAG_LED_TYPE_WHEEL,
};

/**
 * @ingroup enums
 */
enum ratbag_led_colordepth {
	/**
	 * The device only supports a single color.
	 * All color components should be set to 255.
	 */
	RATBAG_LED_COLORDEPTH_MONOCHROME = 400,
	/**
	 * The device supports RBG color with 8 bits per color.
	 */
	RATBAG_LED_COLORDEPTH_RGB_888,
	/**
	 * The device supports RBG colors with 1 bit per color.
	 */
	RATBAG_LED_COLORDEPTH_RGB_111,
};

/**
 * @ingroup enums
 *
 * Macro event types describing the event.
 */
enum ratbag_macro_event_type {
	RATBAG_MACRO_EVENT_INVALID = -1,
	RATBAG_MACRO_EVENT_NONE = 0,
	RATBAG_MACRO_EVENT_KEY_PRESSED,
	RATBAG_MACRO_EVENT_KEY_RELEASED,
	RATBAG_MACRO_EVENT_WAIT,
};
