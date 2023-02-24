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
#include "libratbag-util.h"

struct ratbagd_led {
	struct ratbag_led *lib_led;
	unsigned int index;
	char *path;
	enum ratbag_led_colordepth colordepth;
};

static int ratbagd_led_get_modes(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *reply,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	enum ratbag_led_mode mode = 0;

	CHECK_CALL(sd_bus_message_open_container(reply, 'a', "u"));


	while (mode <= RATBAG_LED_BREATHING) {
		if (ratbag_led_has_mode(led->lib_led, mode))
			CHECK_CALL(sd_bus_message_append(reply, "u", mode));
		mode++;
	}

	CHECK_CALL(sd_bus_message_close_container(reply));

	return 0;
}

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

	verify_unsigned_int(mode);

	CHECK_CALL(sd_bus_message_append(reply, "u", mode));

	return 0;
}

static int ratbagd_led_set_mode(sd_bus *bus,
				const char *path,
				const char *interface,
				const char *property,
				sd_bus_message *m,
				void *userdata,
				sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	enum ratbag_led_mode mode;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "u", &mode));

	r = ratbag_led_set_mode(led->lib_led, mode);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       led->path,
					       RATBAGD_NAME_ROOT ".Led",
					       "Mode",
					       NULL);
	}

	return 0;
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
	CHECK_CALL(sd_bus_message_append(reply, "(uuu)", c.red, c.green, c.blue));

	return 0;
}

static int ratbagd_led_set_color(sd_bus *bus,
				 const char *path,
				 const char *interface,
				 const char *property,
				 sd_bus_message *m,
				 void *userdata,
				 sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	struct ratbag_color c;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "(uuu)", &c.red, &c.green, &c.blue));

	if (c.red > 255)
		c.red = 255;
	if (c.green > 255)
		c.green = 255;
	if (c.blue > 255)
		c.blue = 255;

	r = ratbag_led_set_color(led->lib_led, c);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       led->path,
					       RATBAGD_NAME_ROOT ".Led",
					       "Color",
					       NULL);
	}

	return 0;
}

static int ratbagd_led_get_effect_duration(sd_bus *bus,
					   const char *path,
					   const char *interface,
					   const char *property,
					   sd_bus_message *reply,
					   void *userdata,
					   sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	int rate;

	rate = ratbag_led_get_effect_duration(led->lib_led);

	verify_unsigned_int(rate);

	CHECK_CALL(sd_bus_message_append(reply, "u", rate));

	return 0;
}

static int ratbagd_led_set_effect_duration(sd_bus *bus,
					   const char *path,
					   const char *interface,
					   const char *property,
					   sd_bus_message *m,
					   void *userdata,
					   sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	unsigned int rate;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "u", &rate));

	if (rate > 10000)
		rate = 10000;

	r = ratbag_led_set_effect_duration(led->lib_led, rate);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       led->path,
					       RATBAGD_NAME_ROOT ".Led",
					       "EffectDuration",
					       NULL);
	}

	return 0;
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

	verify_unsigned_int(brightness);

	CHECK_CALL(sd_bus_message_append(reply, "u", brightness));

	return 0;
}

static int ratbagd_led_set_brightness(sd_bus *bus,
				      const char *path,
				      const char *interface,
				      const char *property,
				      sd_bus_message *m,
				      void *userdata,
				      sd_bus_error *error)
{
	struct ratbagd_led *led = userdata;
	unsigned int brightness;
	int r;

	CHECK_CALL(sd_bus_message_read(m, "u", &brightness));

	if (brightness > 255)
		brightness = 255;

	r = ratbag_led_set_brightness(led->lib_led, brightness);

	if (r == 0) {
		sd_bus_emit_properties_changed(bus,
					       led->path,
					       RATBAGD_NAME_ROOT ".Led",
					       "Brightness",
					       NULL);
	}

	return 0;
}

const sd_bus_vtable ratbagd_led_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_led, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_PROPERTY("Modes", "au", ratbagd_led_get_modes, 0, SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_WRITABLE_PROPERTY("Mode", "u",
				 ratbagd_led_get_mode,
				 ratbagd_led_set_mode, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Color", "(uuu)",
				 ratbagd_led_get_color, ratbagd_led_set_color, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_PROPERTY("ColorDepth", "u", NULL, offsetof(struct ratbagd_led, colordepth), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_WRITABLE_PROPERTY("EffectDuration", "u",
				 ratbagd_led_get_effect_duration, ratbagd_led_set_effect_duration, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
	SD_BUS_WRITABLE_PROPERTY("Brightness", "u",
				 ratbagd_led_get_brightness, ratbagd_led_set_brightness, 0,
				 SD_BUS_VTABLE_UNPRIVILEGED|SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
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

	led = zalloc(sizeof(*led));
	led->lib_led = lib_led;
	led->index = index;
	led->colordepth = ratbag_led_get_colordepth(lib_led);

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(led_buffer, "l%u", index);
	r = sd_bus_path_encode_many(&led->path,
				    RATBAGD_OBJ_ROOT "/led/%/%/%",
				    ratbagd_device_get_sysname(device),
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

int ratbagd_led_resync(sd_bus *bus,
		       struct ratbagd_led *led)
{
	return sd_bus_emit_properties_changed(bus,
					      led->path,
					      RATBAGD_NAME_ROOT ".Led",
					      "Mode",
					      "Color",
					      "EffectDuration",
					      "Brightness",
					      NULL);
}
