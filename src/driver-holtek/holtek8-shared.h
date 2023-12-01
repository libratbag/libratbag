// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Michał Lubas
 */

#pragma once

#include "driver-holtek.h"
#include "libratbag-private.h"

#define HOLTEK8_FEATURE_REPORT_SIZE 9
#define HOLTEK8_BUTTON_SIZE 4
#define HOLTEK8_MACRO_EVENT_SIZE 2
#define HOLTEK8_MACRO_DATA_SIZE 128

#define HOLTEK8_MAX_MACRO_EVENTS 256

enum holtek8_api_version {
	HOTLEK8_API_A = 1,
	HOTLEK8_API_B = 2,
};

struct hotlek8_sensor_config {
	enum holtek8_sensor sensor;
	const char *name;
	unsigned int dpi_min;
	unsigned int dpi_max;
	unsigned int dpi_step;
	bool zero_indexed;
	bool independent_xy;
};

struct holtek8_data {
	enum holtek8_api_version api_version;
	const struct hotlek8_sensor_config *sensor_cfg;
	uint8_t chunk_size;
	int button_count;
	uint8_t macro_index;

	union {
		struct {
			uint8_t password[6];
			uint8_t active_profile;
		} api_a;
	};
};

struct holtek8_feature_report {
	uint8_t report_id;
	uint8_t command;
	uint8_t arg[6];
	uint8_t checksum;
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8_feature_report) == HOLTEK8_FEATURE_REPORT_SIZE, "Invalid size");

enum holtek8_button_type {
	HOLTEK8_BUTTON_TYPE_KEYBOARD = 0x00,
	HOLTEK8_BUTTON_TYPE_MOUSE = 0x01,
	HOLTEK8_BUTTON_TYPE_ACPI = 0x02,
	HOLTEK8_BUTTON_TYPE_MEDIA = 0x03,
	HOLTEK8_BUTTON_TYPE_SCROLL = 0x04,
	HOLTEK8_BUTTON_TYPE_RATE = 0x05,
	HOLTEK8_BUTTON_TYPE_REPORT = 0x06,
	HOLTEK8_BUTTON_TYPE_DPI = 0x07,
	HOLTEK8_BUTTON_TYPE_PROFILE = 0x08,
	HOLTEK8_BUTTON_TYPE_MACRO = 0x09,
	HOLTEK8_BUTTON_TYPE_MULTICLICK = 0x0a,
	HOLTEK8_BUTTON_TYPE_SPECIAL = 0x0b,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_button_type) == sizeof(uint8_t), "Invalid size");

enum holtek8_button_mouse {
	HOLTEK8_BUTTON_MOUSE_LEFT = 0xf0,
	HOLTEK8_BUTTON_MOUSE_RIGHT = 0xf1,
	HOLTEK8_BUTTON_MOUSE_MIDDLE = 0xf2,
	HOLTEK8_BUTTON_MOUSE_MB4 = 0xf3,
	HOLTEK8_BUTTON_MOUSE_MB5 = 0xf4,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_button_mouse) == sizeof(uint8_t), "Invalid size");

enum holtek8_button_scroll {
	HOLTEK8_BUTTON_SCROLL_UP = 0x01,
	HOLTEK8_BUTTON_SCROLL_DOWN = 0x02,
	HOLTEK8_BUTTON_SCROLL_RIGHT = 0x03,
	HOLTEK8_BUTTON_SCROLL_LEFT = 0x04,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_button_scroll) == sizeof(uint8_t), "Invalid size");

enum holtek8_button_dpi {
	HOLTEK8_BUTTON_DPI_UP = 0x01,
	HOLTEK8_BUTTON_DPI_DOWN = 0x02,
	HOLTEK8_BUTTON_DPI_CYCLE = 0x03,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_button_dpi) == sizeof(uint8_t), "Invalid size");

enum holtek8_button_profile {
	HOLTEK8_BUTTON_PROFILE_PREVIOUS = 0x00,
	HOLTEK8_BUTTON_PROFILE_UP = 0x01,
	HOLTEK8_BUTTON_PROFILE_DOWN = 0x02,
	HOLTEK8_BUTTON_PROFILE_CYCLE = 0x03,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_button_profile) == sizeof(uint8_t), "Invalid size");

enum holtek8_button_macro {
	HOLTEK8_BUTTON_MACRO_REPEAT_COUNT = 0x00,
	HOLTEK8_BUTTON_MACRO_UNTIL_KEYPRESS = 0x01,
	HOLTEK8_BUTTON_MACRO_UNTIL_RELEASE = 0x02,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_button_macro) == sizeof(uint8_t), "Invalid size");

enum holtek8_modifiers {
	HOLTEK8_MODIFIER_LEFTCTRL = 0x01,
	HOLTEK8_MODIFIER_LEFTSHIFT = 0x02,
	HOLTEK8_MODIFIER_LEFTALT = 0x04,
	HOLTEK8_MODIFIER_LEFTMETA = 0x08,
	HOLTEK8_MODIFIER_RIGHTCTRL = 0x10,
	HOLTEK8_MODIFIER_RIGHTSHIFT = 0x20,
	HOLTEK8_MODIFIER_RIGHTALT = 0x40,
	HOLTEK8_MODIFIER_RIGHTMETA = 0x80,
} __attribute__((packed));
_Static_assert(sizeof(enum holtek8_modifiers) == sizeof(uint8_t), "Invalid size");

struct holtek8_button_data {
	enum holtek8_button_type type;
	union {
		uint8_t data[3];
		struct {
			enum holtek8_modifiers modifiers;
			uint8_t hid_key;
			uint8_t hid_key2;
		} keyboard;
		struct {
			uint8_t _padding1;
			enum holtek8_button_mouse button;
			uint8_t _padding2;
		} mouse;
		struct {
			uint8_t _padding1;
			enum holtek8_button_scroll event;
			uint8_t _padding2;
		} scroll;
		struct {
			uint8_t _padding;
			uint8_t hid_key[2]; //little-endian
		} media;
		struct {
			uint8_t _padding1;
			enum holtek8_button_dpi event;
			uint8_t _padding2;
		} dpi;
		struct {
			uint8_t _padding1;
			enum holtek8_button_profile event;
			uint8_t _padding2;
		} profile;
		struct {
			enum holtek8_button_macro mode;
			uint8_t index;
			uint8_t _padding;
		} macro;
		struct {
			uint8_t hid_key; // or button
			uint8_t delay; // 4ms in a, 2ms in b
			uint8_t count;
		} multiclick;
	};
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8_button_data) == HOLTEK8_BUTTON_SIZE, "Invalid size");

struct holtek8_macro_event {
	uint8_t delay : 7; // at least 1
	uint8_t release : 1;
	uint8_t key;
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8_macro_event) == HOLTEK8_MACRO_EVENT_SIZE, "Invalid size");

struct holtek8_rgb {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8_rgb) == 3, "Invalid size");

struct holtek8_macro_data {
	uint8_t repeat_count[2]; //big-endian
	struct holtek8_macro_event event[62];
	uint8_t _padding[2];
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8_macro_data) == HOLTEK8_MACRO_DATA_SIZE, "Invalid size");

uint8_t
holtek8_report_rate_to_raw(unsigned int report_rate);

unsigned int
holtek8_raw_to_report_rate(uint8_t raw);

uint16_t
holtek8_dpi_to_raw(struct ratbag_device *device, unsigned int dpi);

unsigned int
holtek8_raw_to_dpi(struct ratbag_device *device, uint16_t raw);

int
holtek8_button_from_data(struct ratbag_button *button, const struct holtek8_button_data *data);

int
holtek8_button_to_data(const struct ratbag_button *button, struct holtek8_button_data *data);

const struct hotlek8_sensor_config*
holtek8_get_sensor_config(enum holtek8_sensor sensor);

int
holtek8_read_chunked(struct ratbag_device *device, uint8_t *buf, uint8_t len, struct holtek8_feature_report *response);

int
holtek8_write_chunked(struct ratbag_device *device, const uint8_t *buf, uint8_t len);

int
holtek8_read_padded(struct ratbag_device *device, uint8_t *buf, uint8_t len, struct holtek8_feature_report *response);

int
holtek8_write_padded(struct ratbag_device *device, const uint8_t *buf, uint8_t len);

void
holtek8_calculate_checksum(struct holtek8_feature_report *report);

bool
holtek8_test_echo(struct ratbag_device *device);

int
holtek8_load_device_data(struct ratbag_device *device);

int
holtek8_test_report_descriptor(struct ratbag_device *device);

/* api specific */

int
holtek8a_get_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report);

int
holtek8b_get_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report);

int
holtek8a_set_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report);

int
holtek8b_set_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report);

static inline int
holtek8_get_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	struct holtek8_data *drv_data = device->drv_data;
	enum holtek8_api_version api_version = drv_data->api_version;

	switch (api_version) {
		case HOTLEK8_API_A:
			return holtek8a_get_feature_report(device, report);
		case HOTLEK8_API_B:
			return holtek8b_get_feature_report(device, report);
		default:
			assert(0);
	}
}

static inline int
holtek8_set_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	struct holtek8_data *drv_data = device->drv_data;
	enum holtek8_api_version api_version = drv_data->api_version;

	switch (api_version) {
		case HOTLEK8_API_A:
			return holtek8a_set_feature_report(device, report);
		case HOTLEK8_API_B:
			return holtek8b_set_feature_report(device, report);
		default:
			assert(0);
	}
}
