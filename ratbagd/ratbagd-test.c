/*
 * Copyright © 2017 Red Hat, Inc.
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

#include "ratbagd-test.h"

#ifdef RATBAG_DEVELOPER_EDITION

#include <linux/input.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>

#include "libratbag-test.h"
#include "ratbagd-json.h"

/* A pre-setup sane device. Use for sanity testing by toggling the various
 * error conditions.
 */
static const struct ratbag_test_device ratbagd_test_device_descr = {
	.num_profiles = 4,
	.num_resolutions = 3,
	.num_buttons = 4,
	.num_leds = 3,
	.profiles = {
		{
			.name = NULL,
			.buttons = {
				{ .button_type = RATBAG_BUTTON_TYPE_LEFT,
				  .action_type = RATBAG_BUTTON_ACTION_TYPE_BUTTON,
				  .button = 0 },
				{ .button_type = RATBAG_BUTTON_TYPE_MIDDLE,
				  .action_type = RATBAG_BUTTON_ACTION_TYPE_KEY,
				  .key = KEY_3 },
				{ .button_type = RATBAG_BUTTON_TYPE_RIGHT,
				  .action_type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
				  .special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP },
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_MACRO,
				  .macro = {
					  { .type = RATBAG_MACRO_EVENT_KEY_PRESSED,
					    .value = KEY_B },
					  { .type = RATBAG_MACRO_EVENT_KEY_RELEASED,
					    .value = KEY_B },
					  { .type = RATBAG_MACRO_EVENT_WAIT,
					    .value = 300 },
				  },
				}
			},
			.resolutions = {
				{ .xres = 100, .yres = 200,
					.dpi_min = 50, .dpi_max = 5000},
				{ .xres = 200, .yres = 300, .active = true, .dflt = true },
				{ .xres = 300, .yres = 400 },
			},
			.active = true,
			.dflt = false,
			.hz = 1000,
			.report_rates = {500, 1000},

			.leds = {
				{
					.mode = RATBAG_LED_OFF,
					.color = { .red = 255, .green = 0, .blue = 0 },
					.ms = 1000,
					.brightness = 20,
					.type = RATBAG_LED_TYPE_LOGO,
				},
				{
					.mode = RATBAG_LED_ON,
					.color = { .red = 255, .green = 0, .blue = 0 },
					.ms = 1000,
					.brightness = 20,
					.type = RATBAG_LED_TYPE_SIDE,
				},
				{
					.mode = RATBAG_LED_CYCLE,
					.color = { .red = 255, .green = 255, .blue = 0 },
					.ms = 333,
					.brightness = 40,
					.type = RATBAG_LED_TYPE_SIDE,
				}
			},
		},
		{
			.buttons = {
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_KEY,
				  .key = 4 },
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_KEY,
				  .key = 5 },
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_KEY,
				  .key = 6 },
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_KEY,
				  .key = 7 },
			},
			.resolutions = {
				{ .xres = 1100, .yres = 1200,
				  .caps = {RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION} },
				{ .xres = 1200, .yres = 1300, .dflt = true,
				  .caps = {RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION} },
				{ .xres = 1300, .yres = 1400, .active = true,
				  .caps = {RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION} },
			},
			.hz = 2000,
			.active = false,
			.dflt = true,
			.name = "test profile 2",
		},
		{
			.buttons = {
				{ .button_type = RATBAG_BUTTON_TYPE_LEFT,
				  .action_type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
				  .special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP },
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_MACRO,
				  .macro = {
					  { .type = RATBAG_MACRO_EVENT_KEY_PRESSED,
					    .value = KEY_A },
					  { .type = RATBAG_MACRO_EVENT_KEY_RELEASED,
					    .value = KEY_A },
					  { .type = RATBAG_MACRO_EVENT_WAIT,
					    .value = 150 },
				  }
				},
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_BUTTON,
				  .button = 2 },
				{ .action_type = RATBAG_BUTTON_ACTION_TYPE_BUTTON,
				  .button = 3 },
			},
			.resolutions = {
				{ .xres = 2100, .yres = 2200, .active = true, .caps = {RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION} },
				{ .xres = 2200, .yres = 2300, .dflt = true, .caps = {RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION} },
				{ .xres = 2300, .yres = 2400, .caps = {RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION} },
			},
			.hz = 3000,
			.leds = {
				{
					.mode = RATBAG_LED_ON,
					.color = { .red = 255, .green = 0, .blue = 0 },
					.ms = 1000,
					.brightness = 20
				},
				{
					.mode = RATBAG_LED_CYCLE,
					.color = { .red = 255, .green = 255, .blue = 0 },
					.ms = 333,
					.brightness = 40
				}
			},
			.active = false,
			.dflt = false,
		},
		{
			.disabled = true,
		},
	},
	.destroyed = NULL,
	.destroyed_data = NULL,
};

static int load_test_device(sd_bus_message *m,
			    struct ratbagd *ctx,
			    const struct ratbag_test_device *source)
{
	static int count;
	static struct ratbagd_device *ratbagd_test_device = NULL;
	struct ratbag_device *device;
	int r;
	char devicename[64];

	if (ratbagd_test_device) {
		ratbagd_device_unlink(ratbagd_test_device);
		ratbagd_device_unref(ratbagd_test_device);

		(void) sd_bus_emit_properties_changed(ctx->bus,
						      RATBAGD_OBJ_ROOT,
						      RATBAGD_NAME_ROOT ".Manager",
						      "Devices",
						      NULL);
	}

	device = ratbag_device_new_test_device(ctx->lib_ctx, source);

	snprintf(devicename, sizeof(devicename), "testdevice%d", count++);
	r = ratbagd_device_new(&ratbagd_test_device, ctx, devicename, device);

	/* the ratbagd_device takes its own reference, drop ours */
	ratbag_device_unref(device);

	if (r < 0) {
		log_error("Cannot track test device\n");
		return r;
	}

	ratbagd_device_link(ratbagd_test_device);
	if (m) {
		sd_bus_reply_method_return(m, "u", r);
		(void) sd_bus_emit_properties_changed(ctx->bus,
						      RATBAGD_OBJ_ROOT,
						      RATBAGD_NAME_ROOT ".Manager",
						      "Devices",
						      NULL);
	}

	return 0;
}

int ratbagd_reset_test_device(sd_bus_message *m,
			      void *userdata,
			      sd_bus_error *error)
{
	struct ratbagd *ctx = userdata;

	return load_test_device(m, ctx, &ratbagd_test_device_descr);
}

static const struct ratbag_test_device default_device_descr = {
	.num_profiles = 1,
	.num_resolutions = 1,
	.num_buttons = 1,
	.num_leds = 0,
	.profiles = {
		{
			.name = NULL,
			.buttons = {
				{ .button_type = RATBAG_BUTTON_TYPE_LEFT,
				  .action_type = RATBAG_BUTTON_ACTION_TYPE_BUTTON,
				  .button = 0 },
			},
			.resolutions = {
				{ .xres = 1000, .yres = 1000,
				  .dpi_min = 1000, .dpi_max = 1000},
			},
			.disabled = false,
			.active = true,
			.dflt = true,
			.hz = 1000,
			.report_rates = {1000},
		},
	},
	.destroyed = NULL,
	.destroyed_data = NULL,
};


int ratbagd_load_test_device(sd_bus_message *m,
			     void *userdata,
			     sd_bus_error *error)
{
	struct ratbagd *ctx = userdata;
	struct ratbag_test_device td = default_device_descr;
	char *data;
	int r = 0;

	CHECK_CALL(sd_bus_message_read(m, "s", &data));

	r = ratbagd_parse_json(data, &td);
	if (r != 0) {
		log_error("Failed to parse JSON data\n");
	} else {
		r = load_test_device(m, ctx, &td);
	}
	return sd_bus_reply_method_return(m, "i", r);
}

#endif

void ratbagd_init_test_device(struct ratbagd *ctx)
{
#ifdef RATBAG_DEVELOPER_EDITION
	setenv("RATBAG_TEST", "1", 0);

	ratbagd_reset_test_device(NULL, ctx, NULL);
#endif
}

