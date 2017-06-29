/***
  This file is part of ratbagd.

  Copyright 2017 Jente Hidskes <hjdskes@gmail.com>.

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice (including the next
  paragraph) shall be included in all copies or substantial portions of the
  Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
***/

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <libratbag.h>
#include <systemd/sd-bus.h>
#include "ratbagd.h"
#include "shared-macro.h"

struct ratbagd_led {
	struct ratbag_led *lib_led;
	unsigned int index;
	char *path;
};

static int ratbagd_led_get_mode(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	enum ratbag_led_mode mode;

	mode = ratbag_led_get_mode(led->lib_led);
	return sd_bus_message_append(reply, "u", mode);
}

static int ratbagd_led_set_mode(sd_bus_message *m,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	enum ratbag_led_mode mode;
	int r;

	r = sd_bus_message_read(m, "u", &mode);
	if (r < 0)
		return r;

	r = ratbag_led_set_mode(led->lib_led, mode);
	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_led_get_type(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	const char *type = NULL;
	enum ratbag_led_type t;

	t = ratbag_led_get_type(led->lib_led);

	switch(t) {
		default:
			log_error("Unknown led type %d\n", t);
			/* fallthrough */
		case RATBAG_LED_TYPE_UNKNOWN:
			type = "unknown";
			break;
		case RATBAG_LED_TYPE_LOGO:
			type = "logo";
			break;
		case RATBAG_LED_TYPE_SIDE:
			type = "side";
			break;
	}

	return sd_bus_message_append(reply, "s", type);
}

static int ratbagd_led_get_color(sd_bus *bus,
				 const char *path,
				 const char *interface,
				 const char *property,
				 sd_bus_message *reply,
				 void *userdata,
				 sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	struct ratbag_color c;

	c = ratbag_led_get_color(led->lib_led);
	return sd_bus_message_append(reply, "(uuu)", c.red, c.green, c.blue);
}

static int ratbagd_led_set_color(sd_bus_message *m,
				 void *userdata,
				 sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	struct ratbag_color c;
	int r;

	r = sd_bus_message_read(m, "(uuu)", &c.red, &c.green, &c.blue);
	if (r < 0)
		return r;

	r = ratbag_led_set_color(led->lib_led, c);
	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_led_get_effect_rate(sd_bus *bus,
				       const char *path,
				       const char *interface,
				       const char *property,
				       sd_bus_message *reply,
				       void *userdata,
				       sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	int rate;

	rate = ratbag_led_get_effect_rate(led->lib_led);
	return sd_bus_message_append(reply, "i", rate);
}

static int ratbagd_led_set_effect_rate(sd_bus_message *m,
				       void *userdata,
				       sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	unsigned int rate;
	int r;

	r = sd_bus_message_read(m, "u", &rate);
	if (r < 0)
		return r;

	if (rate < 100 || rate > 20000)
		return 0;

	r = ratbag_led_set_effect_rate(led->lib_led, rate);
	return sd_bus_reply_method_return(m, "u", r);
}

static int ratbagd_led_get_brightness(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *reply,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	unsigned int brightness;

	brightness = ratbag_led_get_brightness(led->lib_led);
	return sd_bus_message_append(reply, "u", brightness);
}

static int ratbagd_led_set_brightness(sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	unsigned int brightness;
	int r;

	r = sd_bus_message_read(m, "u", &brightness);
	if (r < 0)
		return r;

	if (brightness > 255)
		return 0;

	r = ratbag_led_set_brightness(led->lib_led, brightness);
	return sd_bus_reply_method_return(m, "u", r);
}

const sd_bus_vtable ratbagd_led_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_led, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Mode", "u", ratbagd_led_get_mode, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Type", "s", ratbagd_led_get_type, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Color", "(uuu)", ratbagd_led_get_color, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("EffectRate", "i", ratbagd_led_get_effect_rate, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Brightness", "u", ratbagd_led_get_brightness, 0, SD_BUS_VTABLE_PROPERTY_CONST),

	SD_BUS_METHOD("SetMode", "u", "u", ratbagd_led_set_mode, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetColor", "(uuu)", "u", ratbagd_led_set_color, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetEffectRate", "u", "u", ratbagd_led_set_effect_rate, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_METHOD("SetBrightness", "u", "u", ratbagd_led_set_brightness, SD_BUS_VTABLE_UNPRIVILEGED),
	SD_BUS_VTABLE_END,
};

int ratbagd_led_new(struct ratbagd_led **out,
		    struct ratbagd_device *device,
		    struct ratbagd_profile *profile,
		    struct ratbag_led *lib_led,
		    unsigned int index)
{
	_cleanup_(ratbagd_led_freep) struct ratbagd_led *led = NULL;
	char profile_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1],
	     led_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	assert(out);
	assert(lib_led);

	led = calloc(1, sizeof(*led));
	if (!led)
		return -ENOMEM;

	led->lib_led = lib_led;
	led->index = index;

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(led_buffer, "b%u", index);
	r = sd_bus_path_encode_many(&led->path,
				    "/org/freedesktop/ratbag1/led/%/%/%",
				    ratbagd_device_get_name(device),
				    profile_buffer,
				    led_buffer);
	if (r < 0)
		return r;

	*out = led;
	led = NULL;
	return 0;
}

const char *ratbagd_led_get_path(struct ratbagd_led *led)
{
	assert(led);
	return led->path;
}

struct ratbagd_led *ratbagd_led_free(struct ratbagd_led *led)
{
	if (!led)
		return NULL;

	led->path = mfree(led->path);
	led->lib_led = ratbag_led_unref(led->lib_led);

	return mfree(led);
}

