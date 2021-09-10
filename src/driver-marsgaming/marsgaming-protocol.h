#pragma once
#include "marsgaming-buttons.h"
#include "marsgaming-leds.h"

enum marsgaming_report_type {
	MARSGAMING_MM4_REPORT_TYPE_UNKNOWN_1 = 0x01,
	MARSGAMING_MM4_REPORT_TYPE_WRITE = 0x02,
	MARSGAMING_MM4_REPORT_TYPE_READ = 0x03,
	MARSGAMING_MM4_REPORT_TYPE_UNKNOWN_4 = 0x04,
	MARSGAMING_MM4_REPORT_TYPE_UNKNOWN_6 = 0x06
} __attribute__((packed));
_Static_assert(sizeof(enum marsgaming_report_type) == sizeof(uint8_t), "enum marsgaming_report_type should be 1 byte");

struct marsgaming_report_resolution_info {
	bool enabled;
	uint16_t x_res;
	uint16_t y_res;
	// 4 lowest bits, each one corresponds to one led
	// Resolution 0 -> b0000
	// Resolution 1 -> b0001
	// Resolution 2 -> b0011
	// etc
	uint8_t led_bitset;
	uint8_t zeros_3[2];
} __attribute__((packed));

struct marsgaming_report_resolutions {
	uint8_t usb_report_id;
	enum marsgaming_report_type report_type;
	uint8_t unknown_2; // 0x4f
	uint8_t profile_id;
	uint8_t unknown_4; // 0x2a
	uint8_t unknown_5; // 0x00
	uint8_t unknown_6; // 0x00 from device | 0xfa from host
	uint8_t unknown_7; // 0x00 from device | 0xfa from host
	uint8_t count_resolutions;
	uint8_t current_resolution;
	struct marsgaming_report_resolution_info resolutions[6];
	uint8_t padding[6];
} __attribute__((packed));

_Static_assert(sizeof(struct marsgaming_report_resolutions) == 64, "Marsgaming resolution report is not 64 bytes long");

struct marsgaming_report_buttons {
	uint8_t usb_report_id;
	enum marsgaming_report_type report_type;
	uint8_t unknown_2; // 0x90
	uint8_t profile_id;
	uint8_t unknown_4; // 0x4d
	uint8_t unknown_5; // 0x00
	uint8_t unknown_6; // 0x00 from device | 0xfa from host
	uint8_t unknown_7; // 0x00 from device | 0xfa from host
	uint8_t button_count;
	struct marsgaming_button_info buttons[253];
	uint8_t padding[3];
} __attribute__((packed));

_Static_assert(sizeof(struct marsgaming_report_buttons) == 1024, "Marsgaming button report is not 1024 bytes long");

struct marsgaming_report_led {
	uint8_t usb_report_id;
	enum marsgaming_report_type report_type;
	uint8_t unknown2; // 0xf1
	uint8_t profile_id;
	uint8_t unknown4; // 0x06
	uint8_t unknown5; // 0x00
	uint8_t unknown6; // 0xfa
	uint8_t unknown7; // 0xfa
	struct marsgaming_led led;
	uint8_t unknown13; // 0x00
	uint8_t unknown14; // 0x00
	uint8_t unknown15; // 0x00
};

_Static_assert(sizeof(struct marsgaming_report_led) == 16, "Marsgaming led report is not 16 bytes long");

struct marsgaming_profile_drv_data {
	struct marsgaming_report_buttons buttons_report;
	struct marsgaming_report_resolutions resolutions_report;
	struct marsgaming_report_led led_report;
};

inline static struct marsgaming_profile_drv_data *
marsgaming_profile_get_drv_data(struct ratbag_profile *profile)
{
	return (struct marsgaming_profile_drv_data *)profile->drv_data;
}
