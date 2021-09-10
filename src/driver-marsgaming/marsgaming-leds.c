#include "marsgaming-leds.h"

struct ratbag_color
marsgaming_led_color_to_ratbag(struct marsgaming_led_color color)
{
	return (struct ratbag_color){
		.red = 0xff - color.red,
		.green = 0xff - color.green,
		.blue = 0xff - color.blue
	};
}
