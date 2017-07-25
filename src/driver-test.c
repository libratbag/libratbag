/*
 * Copyright © 2015 Red Hat, Inc.
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

#include "config.h"

#include <errno.h>
#include <libevdev/libevdev.h>

#include "libratbag-private.h"
#include "libratbag-test.h"

static void
test_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(profile->device);
	struct ratbag_test_profile *p;
	struct ratbag_test_resolution *r;
	struct ratbag_resolution *res;
	unsigned int i;
	bool active_set = false;
	bool default_set = false;

	assert(index < d->num_profiles);

	p = &d->profiles[index];
	for (i = 0; i < d->num_resolutions; i++) {
		r = &p->resolutions[i];
		res = ratbag_resolution_init(profile, i, r->xres, r->yres, r->hz);
		res->is_active = r->active;
		if (r->active)
			active_set = true;
		res->is_default = r->dflt;
		if (r->dflt)
			default_set = true;
		ratbag_resolution_set_cap(res, r->caps);
		res->hz = r->hz;
		assert(res);
	}

	/* special case triggered by the test suite when num_resolutions is 0 */
	if (d->num_resolutions) {
		res = ratbag_profile_get_resolution(profile, 0);
		assert(res);
		if (!active_set)
			res->is_active = true;
		if (!default_set)
			res->is_default = true;
		ratbag_resolution_unref(res);
	}

	profile->is_active = p->active;
	profile->is_enabled = !p->disabled;
}

static int
test_write_profile(struct ratbag_profile *profile)
{
	/* check if the device is still valid */
	assert(ratbag_get_drv_data(profile->device) != NULL);
	return 0;
}

static int
test_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(device);

	/* check if the device is still valid */
	assert(d != NULL);
	assert(index < d->num_profiles);
	return 0;
}

static void
test_read_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct ratbag_test_device *d = ratbag_get_drv_data(device);
	struct ratbag_test_profile *p = &d->profiles[button->profile->index];
	struct ratbag_button_macro *m;
	const char data[] = "TEST";
	const char *c;
	int idx;

	switch (p->buttons[button->index].type) {
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
		button->action.action.button = p->buttons[button->index].button;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		button->action.action.key.key = p->buttons[button->index].key;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
		m = ratbag_button_macro_new("test macro");

		idx = 0;
		ARRAY_FOR_EACH(data, c) {
			char str[6];
			if (*c == '\0')
				break;
			snprintf_safe(str, 6, "KEY_%c", *c);
			ratbag_button_macro_set_event(m,
						      idx++,
						      RATBAG_MACRO_EVENT_KEY_PRESSED,
						      libevdev_event_code_from_name(EV_KEY, str));
			ratbag_button_macro_set_event(m,
						      idx++,
						      RATBAG_MACRO_EVENT_KEY_RELEASED,
						      libevdev_event_code_from_name(EV_KEY, str));
		}
		ratbag_button_copy_macro(button, m);
		ratbag_button_macro_unref(m);
		break;
	default:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
}

static void
test_read_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	struct ratbag_test_device *d = ratbag_get_drv_data(device);
	struct ratbag_test_profile *p = &d->profiles[led->profile->index];
	struct ratbag_test_led t_led = p->leds[led->index];

	switch (t_led.mode) {
	case RATBAG_LED_ON:
		led->mode = RATBAG_LED_ON;
		break;
	case RATBAG_LED_CYCLE:
		led->mode = RATBAG_LED_CYCLE;
		break;
	case RATBAG_LED_BREATHING:
		led->mode = RATBAG_LED_BREATHING;
		break;
	default:
		led->mode = RATBAG_LED_OFF;
	}
	led->color.red = t_led.color.red;
	led->color.green = t_led.color.green;
	led->color.blue = t_led.color.blue;
	led->hz = t_led.hz;
	led->brightness = t_led.brightness;
}

static int
test_write_button(struct ratbag_button *button,
		  const struct ratbag_button_action *action)
{
	/* check if the device is still valid */
	assert(ratbag_get_drv_data(button->profile->device) != NULL);
	return 0;
}

static int
test_write_led(struct ratbag_led *led,
	       enum ratbag_led_mode mode,
	       struct ratbag_color color, unsigned int hz,
	       unsigned int brightness)
{
	/* check if the device is still valid */
	assert(ratbag_get_drv_data(led->profile->device) != NULL);
	return 0;
}

static const char* test_get_svg_name(const struct ratbag_device *device)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(device);

	return d->svg;
}

static int
test_fake_probe(struct ratbag_device *device)
{
	return -ENODEV;
}

static int
test_probe(struct ratbag_device *device, const void *data)
{
	struct ratbag_test_device *test_device;

	test_device = zalloc(sizeof(*test_device));
	memcpy(test_device, data, sizeof(*test_device));

	ratbag_set_drv_data(device, test_device);
	ratbag_device_init_profiles(device,
				    test_device->num_profiles,
				    test_device->num_resolutions,
				    test_device->num_buttons,
				    test_device->num_leds);
	if (test_device->num_profiles > 1) {
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_BUTTON_MACROS);
		ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_DISABLE_PROFILE);
	}

	return 0;
}

static void
test_remove(struct ratbag_device *device)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(device);

	/* remove must be called only once */
	assert(d != NULL);
	if (d->destroyed)
		d->destroyed(device, d->destroyed_data);
	ratbag_set_drv_data(device, NULL);
	free(d);
}

struct ratbag_driver test_driver = {
	.name = "Test driver",
	.id = "test_driver",
	.probe = test_fake_probe,
	.test_probe = test_probe,
	.remove = test_remove,
	.get_svg_name = test_get_svg_name,
	.read_profile = test_read_profile,
	.write_profile = test_write_profile,
	.set_active_profile = test_set_active_profile,
	.read_button = test_read_button,
	.read_led = test_read_led,
	.write_button = test_write_button,
	.write_resolution_dpi = NULL,
	.write_led = test_write_led,
};
