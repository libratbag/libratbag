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
	struct ratbag_test_button *b = &p->buttons[button->index];
	struct ratbag_button_macro *m;
	struct ratbag_test_macro_event *e;
	int idx;

	switch (b->action_type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
		button->action.action.button = p->buttons[button->index].button;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		button->action.action.key = p->buttons[button->index].key;
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
		m = ratbag_button_macro_new("test macro");

		idx = 0;
		ARRAY_FOR_EACH(b->macro, e) {
			if (e->type == RATBAG_MACRO_EVENT_NONE)
				break;
			ratbag_button_macro_set_event(m, idx++, e->type, e->value);
		}
		ratbag_button_copy_macro(button, m);
		ratbag_button_macro_unref(m);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		button->action.action.special = p->buttons[button->index].special;
		break;
	default:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
	}

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
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

	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
	ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
	ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);

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
	led->ms = t_led.ms;
	led->brightness = t_led.brightness;
}

static int
test_fake_probe(struct ratbag_device *device)
{
	return -ENODEV;
}

static void
test_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_test_device *d = ratbag_get_drv_data(profile->device);
	struct ratbag_test_profile *p;
	struct ratbag_test_profile *p0;
	struct ratbag_test_resolution *r;
	struct ratbag_test_resolution *r0;
	struct ratbag_button *button;
	struct ratbag_led *led;
	unsigned int i;
	bool active_set = false;
	bool default_set = false;
	size_t nrates = 0;

	assert(profile->index < d->num_profiles);

	p = &d->profiles[profile->index];
	p0 = &d->profiles[0];
	r0 = &p0->resolutions[0];

	for (size_t report_rate_index = 0;
	     report_rate_index < ARRAY_LENGTH(p0->report_rates);
	     report_rate_index++) {
		if (p0->report_rates[report_rate_index] > 0)
			nrates++;
	}

	if (nrates > 0)
		ratbag_profile_set_report_rate_list(profile,
						    p0->report_rates,
						    nrates);
	profile->hz = p->hz;

	for (i = 0; i < d->num_resolutions; i++) {
		_cleanup_resolution_ struct ratbag_resolution *res = NULL;

		r = &p->resolutions[i];

		res = ratbag_profile_get_resolution(profile, i);
		assert(res);
		ratbag_resolution_set_resolution(res, r->xres, r->yres);
		if (r0->dpi_min != 0 && r0->dpi_max != 0)
			ratbag_resolution_set_dpi_list_from_range(res,
								  r0->dpi_min,
								  r0->dpi_max);


		res->is_active = r->active;
		if (r->active)
			active_set = true;
		res->is_default = r->dflt;
		if (r->dflt)
			default_set = true;
		res->is_disabled = r->disabled;

		for (size_t j = 0; j < ARRAY_LENGTH(r->caps) && r->caps[j]; j++) {
			ratbag_resolution_set_cap(res, r->caps[j]);
		}
	}

	/* special case triggered by the test suite when num_resolutions is 0 */
	if (d->num_resolutions) {
		_cleanup_resolution_ struct ratbag_resolution *res = NULL;

		res = ratbag_profile_get_resolution(profile, 0);
		assert(res);
		if (!active_set)
			res->is_active = true;
		if (!default_set)
			res->is_default = true;
	}

	ratbag_profile_for_each_button(profile, button)
		test_read_button(button);

	ratbag_profile_for_each_led(profile, led)
		test_read_led(led);

	profile->is_active = p->active;
	profile->is_enabled = !p->disabled;
	if (p->name) {
		free(profile->name);
		profile->name = strdup(p->name);
	}

	for (i = 0; i < ARRAY_LENGTH(p->caps) && p->caps[i]; i++) {
		ratbag_profile_set_cap(profile, p->caps[i]);
	}
}

static int
test_probe(struct ratbag_device *device, const void *data)
{
	struct ratbag_test_device *test_device;
	struct ratbag_profile *profile;

	test_device = zalloc(sizeof(*test_device));
	memcpy(test_device, data, sizeof(*test_device));

	ratbag_set_drv_data(device, test_device);
	ratbag_device_init_profiles(device,
				    test_device->num_profiles,
				    test_device->num_resolutions,
				    test_device->num_buttons,
				    test_device->num_leds);

	ratbag_device_for_each_profile(device, profile)
		test_read_profile(profile);

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

static int
test_commit(struct ratbag_device *device)
{
	/* check if the device is still valid */
	assert(ratbag_get_drv_data(device) != NULL);

	return 0;
}

struct ratbag_driver test_driver = {
	.name = "Test driver",
	.id = "test_driver",
	.probe = test_fake_probe,
	.test_probe = test_probe,
	.remove = test_remove,
	.commit = test_commit,
	.set_active_profile = test_set_active_profile,
};
