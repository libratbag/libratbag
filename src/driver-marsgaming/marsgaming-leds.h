#pragma once
#include "stdint.h"
#include "libratbag.h"

#define MARSGAMING_LED_BREATHING_OFF 0

/*
 *	Colors in Mars Gaming are encoded as the inverse of the value.
 *	That is, a value of 0x00 means fully bright, while 0xFF
 *	means completely dark.
 */
struct marsgaming_led_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
};

struct marsgaming_led {
	struct marsgaming_led_color color;
	uint8_t brightness;
	uint8_t breathing_speed;
} __attribute__((packed));

_Static_assert(sizeof(struct marsgaming_led) == 5, "Marsgaming led is not 5 bytes");

struct ratbag_color
marsgaming_led_color_to_ratbag(struct marsgaming_led_color color);
