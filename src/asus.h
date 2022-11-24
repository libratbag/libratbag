#pragma once

#include <linux/input.h>
#include <stdint.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define ASUS_QUIRK_DOUBLE_DPI 1 << 0
#define ASUS_QUIRK_STRIX_PROFILE 1 << 1
#define ASUS_QUIRK_BATTERY_V2 1 << 2

#define ASUS_PACKET_SIZE 64
#define ASUS_BUTTON_ACTION_TYPE_KEY 0  /* keyboard key */
#define ASUS_BUTTON_ACTION_TYPE_BUTTON 1  /* mouse button */
#define ASUS_BUTTON_CODE_DISABLED 0xff  /* disabled mouse button */
#define ASUS_STATUS_ERROR 0xffaa  /* invalid state/request, disconnected or sleeping */

/* maximum number of buttons across all ASUS devices */
#define ASUS_MAX_NUM_BUTTON 17

/* maximum number of DPI presets across all ASUS devices */
/* for 4 DPI devices: 0 - red, 1 - purple, 2 - blue (default), 3 - green */
/* for 2 DPI devices: 0 - main (default), 1 - alternative */
#define ASUS_MAX_NUM_DPI 4

/* maximum number of LEDs across all ASUS devices */
#define ASUS_MAX_NUM_LED 3

/* base request */

struct _asus_request {
	uint16_t cmd;
	uint8_t params[ASUS_PACKET_SIZE - 2];
} __attribute__((packed));
_Static_assert(sizeof(struct _asus_request) == ASUS_PACKET_SIZE, "The size of `_asus_request` is wrong.");

union asus_request {
	struct _asus_request data;
	uint8_t raw[ASUS_PACKET_SIZE];
};

/* base response */

struct _asus_response {
	uint16_t code;
	uint8_t results[ASUS_PACKET_SIZE - 2];
} __attribute__((packed));
_Static_assert(sizeof(struct _asus_response) == ASUS_PACKET_SIZE, "The size of `_asus_response` is wrong.");

union asus_response {
	struct _asus_response data;
	uint8_t raw[ASUS_PACKET_SIZE];
};

/* current profile ID and firmware info */

struct asus_profile_data {
	unsigned int profile_id;
	uint8_t version_primary_major;
	uint8_t version_primary_minor;
	uint8_t version_primary_build;
	uint8_t version_secondary_major;
	uint8_t version_secondary_minor;
	uint8_t version_secondary_build;
} __attribute__((packed));

/* button bindings */

struct _asus_binding {
	uint8_t action;  /* ASUS code (for both keyboard keys and mouse buttons) */
	uint8_t type;   /* ASUS action type */
} __attribute__((packed));

struct _asus_binding_data {
	uint32_t pad;
	struct _asus_binding binding[ASUS_MAX_NUM_BUTTON];
} __attribute__((packed));

union asus_binding_data {
	struct _asus_binding_data data;
	uint8_t raw[ASUS_PACKET_SIZE];
};

/* DPI data */

struct _asus_dpi2_data {
	uint32_t pad;
	uint16_t dpi[2];  /* DPI presets */
	uint16_t rate;  /* polling rate */
	uint16_t response;  /* button response */
	uint16_t snapping;  /* angle snapping (on/off) */
} __attribute__((packed));  /* struct for storing 2 DPI presets and extra settings */

struct _asus_dpi4_data {
	uint32_t pad;
	uint16_t dpi[4];
	uint16_t rate;
	uint16_t response;
	uint16_t snapping;
} __attribute__((packed));  /* struct for storing 4 DPI presets and extra settings */

union asus_resolution_data {
	struct _asus_dpi2_data data2;  /* data for 2 DPI presets */
	struct _asus_dpi4_data data4;  /* data for 4 DPI presets */
	uint8_t raw[sizeof(struct _asus_dpi4_data)];
};

/* LED data */

struct _asus_led {
	uint8_t mode;
	uint8_t brightness;  /* 0-4 */
	uint8_t r;
	uint8_t g;
	uint8_t b;
} __attribute__((packed));

struct _asus_led_data {
	uint32_t pad;
	struct _asus_led led[ASUS_MAX_NUM_LED];  /* LEDs */
} __attribute__((packed));

union asus_led_data {
	struct _asus_led_data data;
	uint8_t raw[sizeof(struct _asus_led_data)];
};

/* button define */
struct asus_button {
	uint8_t asus_code;  /* used for button action */
	enum ratbag_button_action_type type;
	uint8_t button;  /* mouse button number, optional */
	enum ratbag_button_action_special special;  /* special action, optional */
};

/* ASUS code, button type, button number, special button action */
static struct asus_button ASUS_BUTTON_MAPPING[] = {
	{ 0xf0, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 1, 0 },  /* left */
	{ 0xf1, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 2, 0 },  /* right (button 3 in xev) */
	{ 0xf2, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 3, 0 },  /* middle (button 2 in xev) */
	{ 0xe8, RATBAG_BUTTON_ACTION_TYPE_SPECIAL, 0, RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP },
	{ 0xe9, RATBAG_BUTTON_ACTION_TYPE_SPECIAL, 0, RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN },
	{ 0xe6, RATBAG_BUTTON_ACTION_TYPE_SPECIAL, 0, RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP },
	{ 0xe4, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 4, 0 },  /* backward, left side */
	{ 0xe5, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 5, 0 },  /* forward, left side */
	{ 0xe1, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 4, 0 },  /* backward, right side */
	{ 0xe2, RATBAG_BUTTON_ACTION_TYPE_BUTTON, 5, 0 },  /* forward, right side */
	{ 0xe7, RATBAG_BUTTON_ACTION_TYPE_SPECIAL, 0, RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE },  /* DPI target */
	{ 0xea, RATBAG_BUTTON_ACTION_TYPE_NONE, 0, 0 },  /* side button A */
	{ 0xeb, RATBAG_BUTTON_ACTION_TYPE_NONE, 0, 0 },  /* side button B */
	{ 0xec, RATBAG_BUTTON_ACTION_TYPE_NONE, 0, 0 },  /* side button C */
	{ 0xed, RATBAG_BUTTON_ACTION_TYPE_NONE, 0, 0 },  /* side button D */
	{ 0xee, RATBAG_BUTTON_ACTION_TYPE_NONE, 0, 0 },  /* side button E */
	{ 0xef, RATBAG_BUTTON_ACTION_TYPE_NONE, 0, 0 },  /* side button F */
};

struct asus_button *
asus_find_button_by_action(struct ratbag_button_action action);

struct asus_button *
asus_find_button_by_code(uint8_t asus_code);

int
asus_find_key_code(unsigned int linux_code);

int
asus_get_linux_key_code(uint8_t asus_code);

/* initializers of ratbag data */

void
asus_setup_profile(struct ratbag_device *device, struct ratbag_profile *profile);

void
asus_setup_button(struct ratbag_device *device, struct ratbag_button *button);

void
asus_setup_resolution(struct ratbag_device *device, struct ratbag_resolution *resolution);

void
asus_setup_led(struct ratbag_device *device, struct ratbag_led *led);

/* generic i/o */

int
asus_query(struct ratbag_device *device, union asus_request *request, union asus_response *response);

/* commit */

int
asus_save_profile(struct ratbag_device *device);

/* profiles */

int
asus_get_profile_data(struct ratbag_device *device, struct asus_profile_data *data);

int
asus_set_profile(struct ratbag_device *device, unsigned int index);

/* button bindings */

int
asus_get_binding_data(struct ratbag_device *device, union asus_binding_data *data);

int
asus_set_button_action(struct ratbag_device *device, uint8_t button_asus_code,
		uint8_t asus_code, uint8_t asus_type);

/* resolution settings */

int
asus_get_resolution_data(struct ratbag_device *device, union asus_resolution_data *data);

int
asus_set_dpi(struct ratbag_device *device, unsigned int index, unsigned int dpi);

int
asus_set_polling_rate(struct ratbag_device *device, unsigned int hz);

int
asus_set_button_response(struct ratbag_device *device, unsigned int ms);

int
asus_set_angle_snapping(struct ratbag_device *device, bool is_enabled);

/* LED settings */

int
asus_get_led_data(struct ratbag_device *device, union asus_led_data *data);

int
asus_set_led(struct ratbag_device *device,
		uint8_t index, uint8_t mode, uint8_t brightness,
		struct ratbag_color color);
