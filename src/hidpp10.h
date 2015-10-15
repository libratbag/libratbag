/*
 * HID++ 1.0 library - headers file.
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
 * Based on the HID++ 1.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#ifndef HIDPP_10_H
#define HIDPP_10_H

#include <stdint.h>
#include <stdbool.h>

#include "hidpp-generic.h"

/* FIXME: that's what my G500s supports, but only pages 3-5 are valid.
 * 0 is zeroed, 1 and 2 are garbage, all above 6 is garbage */
#define HIDPP10_NUM_PROFILES 3

struct hidpp10_device  {
	struct hidpp_device base;
	unsigned index;
	uint16_t wpid;
};

struct hidpp10_device*
hidpp10_device_new_from_wpid(const struct hidpp_device *base,
			     uint16_t wpid);
struct hidpp10_device*
hidpp10_device_new_from_idx(const struct hidpp_device *base,
			    int idx);

void
hidpp10_device_destroy(struct hidpp10_device *dev);

/* -------------------------------------------------------------------------- */
/* 0x00: Enable HID++ Notifications                                           */
/* -------------------------------------------------------------------------- */

/**
 * All notifications are disabled by default on powerup.
 */
enum hidpp10_hidpp_notifications {
	/**
	 * enabled: Multimedia and MS vendor specific keys are reported as
	 * HID++ notification 0x03
	 * disabled: reported as normal HID reports
	 */
	HIDPP10_NOTIFICATIONS_CONSUMER_VENDOR_SPECIFIC_CONTROL      = (1 << 0),
	/**
	 * enabled: power keys are reported as HID++ notification 0x04
	 * disabled: reported as normal HID reports
	 */
	HIDPP10_NOTIFICATIONS_POWER_KEYS                            = (1 << 1),
	/**
	 * enabled: Vertical scroll wheel/iNav are reported as HID++
	 * notification 0x05
	 * disabled: reported as normal HID reports
	 */
	HIDPP10_NOTIFICATIONS_ROLLER_V                              = (1 << 2),
	/**
	 * enabled: buttons not available in standard HID are reported as
	 * HID++ notification 0x06
	 * disabled: buttons not available in standard HID are not reported
	 */
	HIDPP10_NOTIFICATIONS_MOUSE_EXTRA_BUTTONS                   = (1 << 3),
	/**
	 * enabled: battery status/milage are reported as HID++ notification
	 * 0x07 or 0x0D (device-dependent)
	 * disabled: battery status/milage are not reported
	 */
	HIDPP10_NOTIFICATIONS_BATTERY_STATUS                        = (1 << 4),
	/**
	 * enabled: Horizontal scroll wheel/iNav are reported as HID++
	 * notification 0x05
	 * disabled: reported as normal HID reports
	 */
	HIDPP10_NOTIFICATIONS_ROLLER_H                              = (1 << 5),
	/**
	 * enabled: F-Lock status is reported as HID++ notification 0x09
	 * disabled: F-Lock status is not reported
	 */
	HIDPP10_NOTIFICATIONS_F_LOCK_STATUS                         = (1 << 6),
	/**
	 * enabled: Numpad keys are reported as buttons in HID++
	 * notification 0x03
	 * disabled: reported as normal keys
	 */
	HIDPP10_NOTIFICATIONS_NUMPAD_NUMERIC_KEYS                   = (1 << 7),
	/**
	 * enabled: Device arrival/removal/... are reported as HID++
	 * notifications 0x40, 0x41, 0x46 or 0x78
	 * disabled: these events are not reported
	 */
	HIDPP10_NOTIFICATIONS_WIRELESS_NOTIFICATIONS                = (1 << 8),
	/**
	 * enabled: User interface events are reported as HID++ notification
	 * 0x08
	 * disabled: these events are not reported
	 */
	HIDPP10_NOTIFICATIONS_UI_NOTIFICATIONS                      = (1 << 9),
	/**
	 * enabled: Quad link quality info events are reported as HID++ notification
	 * 0x49
	 * disabled: these events are not reported
	 */
	HIDPP10_NOTIFICATIONS_QUAD_LINK_QUALITY_INFO                = (1 << 10),
	HIDPP10_NOTIFICATIONS_SOFTWARE_PRESENT                      = (1 << 11),
	HIDPP10_NOTIFICATIONS_TOUCHPAD_MULTITOUCH_NOTIFICATIONS     = (1 << 12),
	/* 1 << 13 is reserved */
	/* 1 << 14 is reserved */
	/* 1 << 15 is reserved */

	/**
	 * enabled: 3D gestures are reported as HID++ notification 0x65
	 * disabled: these events are not reported
	 */
	HIDPP10_NOTIFICATIONS_3D_GESTURE                            = (1 << 16),
	HIDPP10_NOTIFICATIONS_VOIP_TELEPHONY                        = (1 << 17),
	HIDPP10_NOTIFICATIONS_CONFIGURATION_COMPLETE                = (1 << 18),
	/* 1 << 19 is reserved */
	/* 1 << 20 is reserved */
	/* 1 << 21 is reserved */
	/* 1 << 22 is reserved */
	/* 1 << 23 is reserved */
};

int
hidpp10_get_hidpp_notifications(struct hidpp10_device *dev,
				uint32_t *reporting_flags);

int
hidpp10_set_hidpp_notifications(struct hidpp10_device *dev,
				uint32_t reporting_flags);

/* -------------------------------------------------------------------------- */
/* 0x01: Enable Individual Features                                           */
/* -------------------------------------------------------------------------- */

enum hidpp10_individual_features {
	HIDPP10_FEATURE_BIT_MOUSE_SENSOR_RESOLUTION = (1 << 0),
	/**
	 * disabled: buttons send button codes
	 * enabled: buttons have special functions (default)
	 * @note Do not use, use 0x63 instead
	 */
	HIDPP10_FEATURE_BIT_SPECIAL_BUTTON_FUNCTION = (1 << 1),
	/**
	 * disabled: normal key usage (default)
	 * enabled: enhanced key usage
	 */
	HIDPP10_FEATURE_BIT_ENHANCED_KEY_USAGE      = (1 << 2),
	/**
	 * disabled: (default)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_FAST_FORWARD_REWIND     = (1 << 3),
	/**
	 * disabled: (default)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_SEND_CALCULATOR_RESULT  = (1 << 4),
	/**
	 * disabled:
	 * enabled: (default)
	 */
	HIDPP10_FEATURE_BIT_MOTION_WAKEUP           = (1 << 5),
	/**
	 * disabled: (default)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_FAST_SCROLLING          = (1 << 6),
	/**
	 * disabled: work as buttons
	 * enabled: control the resolution (default)
	 */
	HIDPP10_FEATURE_BIT_BUTTONS_CONTROL_RESOLUTION = (1 << 7),

	/* 1 << 8 is reserved */

	/**
	 * disabled: (default)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_RECEIVER_MULTIPLE_RF_LOCK = (1 << 9),

	/**
	 * disabled: (default)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_RECEIVER_DISABLE_RFSCAN_IN_SUSPEND = (1 << 10),

	/**
	 * disabled: (default)
	 * enabled:
	 *
	 * When enabled,removes all compatibility checks.
	 */
	HIDPP10_FEATURE_BIT_RECEIVER_ACCEPT_ALL_DEVICES_IN_PAIRING = (1 << 11),

	/* 1 << 12 is reserved */
	/* 1 << 13 is reserved */
	/* 1 << 14 is reserved */
	/* 1 << 15 is reserved */

	/**
	 * disabled: (default)
	 * enabled: no sound
	 */
	HIDPP10_FEATURE_BIT_INHIBIT_LOCK_KEY_SOUND  = (1 << 16),

	/**
	 * disabled: (default)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_INHIBIT_TOUCHPAD        = (1 << 17),

	/**
	 * disabled:
	 * enabled: (default)
	 */
	HIDPP10_FEATURE_BIT_3D_ENGINE               = (1 << 18),

	/**
	 * disabled: (disabled)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_SW_CONTROLS_LEDS        = (1 << 19),

	/**
	 * disabled: (disabled)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_NO_NUMLOCK_TOGGLE       = (1 << 20),

	/**
	 * disabled: (disabled)
	 * enabled:
	 */
	HIDPP10_FEATURE_BIT_INHIBIT_PRESENCE_DETECTION = (1 << 21),
};

int
hidpp10_get_individual_features(struct hidpp10_device *dev,
				uint32_t *feature_mask);

int
hidpp10_set_individual_features(struct hidpp10_device *dev,
				uint32_t feature_mask);

/* -------------------------------------------------------------------------- */
/* 0x07: Battery Status                                                       */
/* -------------------------------------------------------------------------- */

enum hidpp10_battery_level {
	HIDPP10_BATTERY_LEVEL_UNKNOWN = 0x00,
	HIDPP10_BATTERY_LEVEL_CRITICAL = 0x01,
	HIDPP10_BATTERY_LEVEL_CRITICAL_LEGACY = 0x02,
	HIDPP10_BATTERY_LEVEL_LOW = 0x03,
	HIDPP10_BATTERY_LEVEL_LOW_LEGACY = 0x04,
	HIDPP10_BATTERY_LEVEL_GOOD = 0x05,
	HIDPP10_BATTERY_LEVEL_GOOD_LEGACY = 0x06,
	HIDPP10_BATTERY_LEVEL_FULL_LEGACY = 0x07,
	/* 0x08..0xFF ... reserved */
};

enum hidpp10_battery_charge_state {
	HIDPP10_BATTERY_CHARGE_STATE_NOT_CHARGING = 0x00,
	/* 0x01 ... 0x1F ... reserved (not charging) */
	HIDPP10_BATTERY_CHARGE_STATE_UNKNOWN = 0x20,
	HIDPP10_BATTERY_CHARGE_STATE_CHARGING = 0x21,
	HIDPP10_BATTERY_CHARGE_STATE_CHARGING_COMPLETE = 0x22,
	HIDPP10_BATTERY_CHARGE_STATE_CHARGING_ERROR = 0x23,
	HIDPP10_BATTERY_CHARGE_STATE_CHARGING_FAST = 0x24,
	HIDPP10_BATTERY_CHARGE_STATE_CHARGING_SLOW = 0x25,
	HIDPP10_BATTERY_CHARGE_STATE_TOPPING_CHARGE = 0x26,
	/* 0x27 .. 0xff ... reserved */
};

int
hidpp10_get_battery_status(struct hidpp10_device *dev,
			   enum hidpp10_battery_level *level,
			   enum hidpp10_battery_charge_state *charge_state,
			   uint8_t *low_threshold_in_percent);
/* -------------------------------------------------------------------------- */
/* 0x0D: Battery Mileage                                                      */
/* -------------------------------------------------------------------------- */

int
hidpp10_get_battery_mileage(struct hidpp10_device *dev,
			    uint8_t *level_in_percent,
			    uint32_t *max_seconds,
			    enum hidpp10_battery_charge_state *state);

/* -------------------------------------------------------------------------- */
/* 0x0F: Profile queries                                                      */
/* -------------------------------------------------------------------------- */
#define PROFILE_NUM_BUTTONS				13
#define PROFILE_NUM_DPI_MODES				5
#define PROFILE_BUTTON_TYPE_BUTTON			0x81
#define PROFILE_BUTTON_TYPE_KEYS			0x82
#define PROFILE_BUTTON_TYPE_SPECIAL			0x83
#define PROFILE_BUTTON_TYPE_CONSUMER_CONTROL		0x84
#define PROFILE_BUTTON_TYPE_DISABLED			0x8F

#define PROFILE_BUTTON_SPECIAL_PAN_LEFT			0x1
#define PROFILE_BUTTON_SPECIAL_PAN_RIGHT		0x2
#define PROFILE_BUTTON_SPECIAL_DPI_NEXT			0x4
#define PROFILE_BUTTON_SPECIAL_DPI_PREV			0x8

struct hidpp10_profile {
	struct {
		uint16_t xres;
		uint16_t yres;
		bool led[4];
	} dpi_modes[5];
	size_t num_dpi_modes;

	uint8_t red;
	uint8_t green;
	uint8_t blue;
	bool angle_correction;
	uint8_t default_dpi_mode;
	uint16_t refresh_rate;
	union hidpp10_button {
		struct { uint8_t type; } any;
		struct {
			uint8_t type;
			uint16_t button;
		} button;
		struct {
			uint8_t type;
			uint8_t modifier_flags;
			uint8_t key;
		} keys;
		struct {
			uint8_t type;
			uint16_t special;
		} special;
		struct {
			uint8_t type;
			uint16_t consumer_control;
		} consumer_control;
		struct {
			uint8_t type;
		} disabled;
	} buttons[PROFILE_NUM_BUTTONS];
	size_t num_buttons;
};

int
hidpp10_get_current_profile(struct hidpp10_device *dev, int8_t *current_profile);

int
hidpp10_get_profile(struct hidpp10_device *dev, int8_t number,
		    struct hidpp10_profile *profile);
/* -------------------------------------------------------------------------- */
/* 0x51: LED Status                                                           */
/* -------------------------------------------------------------------------- */

enum hidpp10_led_status {
	HIDPP10_LED_STATUS_NO_CHANGE = 0x0, /**< LED does not exist, or
					      should not change */
	HIDPP10_LED_STATUS_OFF = 0x1,
	HIDPP10_LED_STATUS_ON = 0x2,
	HIDPP10_LED_STATUS_BLINK = 0x3,
	HIDPP10_LED_STATUS_HEARTBEAT = 0x4,
	HIDPP10_LED_STATUS_SLOW_ON = 0x5,
	HIDPP10_LED_STATUS_SLOW_OFF = 0x6,
};

int
hidpp10_get_led_status(struct hidpp10_device *dev,
		       enum hidpp10_led_status led[6]);
int
hidpp10_set_led_status(struct hidpp10_device *dev,
		       const enum hidpp10_led_status led[6]);

/* -------------------------------------------------------------------------- */
/* 0x54: LED Intensity                                                        */
/* -------------------------------------------------------------------------- */

int
hidpp10_get_led_intensity(struct hidpp10_device *dev,
			  uint8_t led_intensity_in_percent[6]);

/* Granularity for the led intensity is 10% increments. A value of 0 leaves
 * the intensity unchanged */
int
hidpp10_set_led_intensity(struct hidpp10_device *dev,
			  const uint8_t led_intensity_in_percent[6]);

/* -------------------------------------------------------------------------- */
/* 0x57: LED Color                                                           */
/* -------------------------------------------------------------------------- */

/* Note: this changes the color of the LED only, use 0x51 to turn the LED
 * on/off */
int
hidpp10_get_led_color(struct hidpp10_device *dev,
		      uint8_t *red,
		      uint8_t *green,
		      uint8_t *blue);
int
hidpp10_set_led_color(struct hidpp10_device *dev,
		      uint8_t red,
		      uint8_t green,
		      uint8_t blue);

/* -------------------------------------------------------------------------- */
/* 0x61: Optical Sensor Settings                                              */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_optical_sensor_settings(struct hidpp10_device *dev,
				    uint8_t *surface_reflectivity);
/* -------------------------------------------------------------------------- */
/* 0x63: Current Resolution                                                   */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_current_resolution(struct hidpp10_device *dev,
			       uint16_t *xres, uint16_t *yres);
int
hidpp10_set_current_resolution(struct hidpp10_device *dev,
			       uint16_t xres, uint16_t yres);

/* -------------------------------------------------------------------------- */
/* 0x64: USB Refresh Rate                                                     */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_usb_refresh_rate(struct hidpp10_device *dev,
			     uint16_t *rate);

int
hidpp10_set_usb_refresh_rate(struct hidpp10_device *dev,
			     uint16_t rate);

/* -------------------------------------------------------------------------- */
/* 0xA2: Read Sector                                                          */
/* -------------------------------------------------------------------------- */

int
hidpp10_read_memory(struct hidpp10_device *dev,
		    uint8_t page,
		    uint8_t offset,
		    uint8_t bytes[16]);

/* -------------------------------------------------------------------------- */
/* 0xB2: Device Connection and Disconnection (Pairing)                        */
/* -------------------------------------------------------------------------- */

int
hidpp10_open_lock(struct hidpp10_device *device);
int
hidpp10_disconnect(struct hidpp10_device *device, int idx);

/* -------------------------------------------------------------------------- */
/* 0xB5: Pairing Information                                                  */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_pairing_information(struct hidpp10_device *dev,
				uint8_t *report_interval,
				uint16_t *wpid,
				uint8_t *device_type);
int
hidpp10_get_pairing_information_device_name(struct hidpp10_device *dev,
					    char *name,
					    size_t *name_sz);

/* -------------------------------------------------------------------------- */
/* 0xF1: Device Firmware Information                                          */
/* -------------------------------------------------------------------------- */
int
hidpp10_get_firmare_information(struct hidpp10_device *dev,
				uint8_t *major,
				uint8_t *minor,
				uint8_t *build_number);

#endif /* HIDPP_10_H */
