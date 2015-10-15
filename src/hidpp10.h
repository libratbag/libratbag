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
#include "libratbag-private.h"
#include "libratbag.h"

struct hidpp10_device;

struct _hidpp10_message {
	uint8_t report_id;
	uint8_t device_idx;
	uint8_t sub_id;
	uint8_t address;
	union {
		uint8_t parameters[3];
		uint8_t string[16];
	};
} __attribute__((packed));

union hidpp10_message {
	struct _hidpp10_message msg;
	uint8_t data[LONG_MESSAGE_LENGTH];
};

void hidpp10_device_destroy(struct hidpp10_device *dev);

int hidpp10_request_command(struct hidpp10_device *device, union hidpp10_message *msg);
int hidpp10_open_lock(struct hidpp10_device *device);
int hidpp10_disconnect(struct hidpp10_device *device, int idx);
void hidpp10_list_devices(struct ratbag_device *device);
struct hidpp10_device *hidpp10_device_new_from_wpid(struct ratbag_device *device, uint16_t wpid);
struct hidpp10_device *hidpp10_device_new_from_idx(struct ratbag_device *device, int idx);

/* -------------------------------------------------------------------------- */
/* 0x00: Enable HID++ Notifications                                           */
/* -------------------------------------------------------------------------- */
#define REPORTING_FLAGS_R0_CONSUMER_SPECIFIC_CONTROL	0
#define REPORTING_FLAGS_R0_POWER_KEYS			1
#define REPORTING_FLAGS_R0_VERTICAL_SCROLL		2
#define REPORTING_FLAGS_R0_MOUSE_EXTRA_BUTTONS		3
#define REPORTING_FLAGS_R0_BATTERY_STATUS		4
#define REPORTING_FLAGS_R0_HORIZONTAL_SCROLL		5
#define REPORTING_FLAGS_R0_F_LOCK_STATUS		6
#define REPORTING_FLAGS_R0_NUMPAD_NUMERIC_KEYS		7
#define REPORTING_FLAGS_R2_3D_GESTURES			0

int
hidpp10_get_hidpp_notifications(struct hidpp10_device *dev,
				uint8_t *reporting_flags_r0,
				uint8_t *reporting_flags_r2);

int
hidpp10_set_hidpp_notifications(struct hidpp10_device *dev,
				uint8_t reporting_flags_r0,
				uint8_t reporting_flags_r2);

/* -------------------------------------------------------------------------- */
/* 0x01: Enable Individual Features                                           */
/* -------------------------------------------------------------------------- */
#define FEATURE_BIT_R0_SPECIAL_BUTTON_FUNCTION		1
#define FEATURE_BIT_R0_ENHANCED_KEY_USAGE		2
#define FEATURE_BIT_R0_FAST_FORWARD_REWIND		3
#define FEATURE_BIT_R0_SCROLLING_ACCELERATION		6
#define FEATURE_BIT_R0_BUTTONS_CONTROL_THE_RESOLUTION	7
#define FEATURE_BIT_R2_INHIBIT_LOCK_KEY_SOUND		0
#define FEATURE_BIT_R2_3D_ENGINE			2
#define FEATURE_BIT_R2_HOST_SW_CONTROLS_LEDS		3

int
hidpp10_get_individual_features(struct hidpp10_device *dev,
				uint8_t *feature_bit_r0,
				uint8_t *feature_bit_r2);

int
hidpp10_set_individual_feature(struct hidpp10_device *dev,
			       uint8_t feature_bit_r0,
			       uint8_t feature_bit_r2);

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


/* FIXME: that's what my G500s supports, but only pages 3-5 are valid.
 * 0 is zeroed, 1 and 2 are garbage, all above 6 is garbage */
#define HIDPP10_NUM_PROFILES 3
struct hidpp10_device  {
	struct ratbag_device *ratbag_device;
	unsigned index;
	char name[15];
	uint16_t wpid;
	uint8_t report_interval;
	uint8_t device_type;
	uint8_t fw_major;
	uint8_t fw_minor;
	uint8_t build;
	uint16_t xres, yres;
	uint16_t refresh_rate;
	enum hidpp10_led_status led[6];
	int8_t current_profile;
	struct hidpp10_profile profiles[HIDPP10_NUM_PROFILES];
};
#endif /* HIDPP_10_H */
