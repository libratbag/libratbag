/*
 * Copyright © 2020 Marian Beermann
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

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

enum sinowealth_report_id {
	SINOWEALTH_REPORT_ID_CONFIG = 0x4,
	SINOWEALTH_REPORT_ID_CMD = 0x5,
	SINOWEALTH_REPORT_ID_CONFIG_LONG = 0x6,
};

enum sinowealth_command_id {
	SINOWEALTH_CMD_FIRMWARE_VERSION = 0x1,
	SINOWEALTH_CMD_GET_CONFIG = 0x11,
};

#define SINOWEALTH_CONFIG_SIZE 520
#define SINOWEALTH_CONFIG_SIZE_USED_MIN 131
#define SINOWEALTH_CONFIG_SIZE_USED_MAX 167

#define SINOWEALTH_XY_INDEPENDENT 0b1000

/* The PC software only goes down to 400, but the PMW3360 doesn't care */
#define SINOWEALTH_DPI_MIN 100
#define SINOWEALTH_DPI_MAX 12000
#define SINOWEALTH_DPI_STEP 100

/* Different software expose different amount of DPI slots:
 * Glorious - 6;
 * G-Wolves - 7.
 * But in fact fact there are eight slots.
 */
#define SINOWEALTH_NUM_DPIS 8

#define SINOWEALTH_RGB_BRIGHTNESS_BITS 0xF0
#define SINOWEALTH_RGB_SPEED_BITS 0x0F

struct sinowealth_rgb8 {
	uint8_t r, g, b;
} __attribute__((packed));

_Static_assert(sizeof(struct sinowealth_rgb8) == 3, "Invalid size");

struct sinowealth_rbg8 {
	uint8_t r, b, g;
} __attribute__((packed));

_Static_assert(sizeof(struct sinowealth_rbg8) == 3, "Invalid size");

enum rgb_effect {
	RGB_OFF = 0,
	RGB_GLORIOUS = 0x1,   /* unicorn mode */
	RGB_SINGLE = 0x2,     /* single constant color */
	RGB_BREATHING7 = 0x3, /* breathing with seven user-defined colors */
	RGB_TAIL = 0x4,
	RGB_BREATHING = 0x5,  /* Full RGB breathing. */
	RGB_CONSTANT = 0x6,   /* Each LED gets its own static color. Not available in Glorious software. */
	RGB_RAVE = 0x7,
	RGB_RANDOM = 0x8,     /* Randomly change colors. Not available in Glorious software. */
	RGB_WAVE = 0x9,
	RGB_BREATHING1 = 0xa, /* Single color breathing. */

	/* The value mice with no LEDs have.
	* Unreliable as non-constant.
	* Do **not** overwrite it.
	 */
	RGB_NOT_SUPPORTED = 0xff,
};

struct sinowealth_config_report {
	uint8_t report_id; /* SINOWEALTH_REPORT_ID_CONFIG */
	uint8_t command_id;
	uint8_t unknown1;
	/* 0x0 - read.
	 * CONFIG_SIZE_USED-8 - write.
	 */
	uint8_t config_write;
	uint8_t unknown2[6];
	/* @ref sinowealth_report_rate_map. */
	uint8_t report_rate:4;
	/* 0b1000 - make DPI axes independent. */
	uint8_t config:4;
	uint8_t dpi_count:4;
	uint8_t active_dpi:4;
	/* bit set: disabled, unset: enabled */
	uint8_t disabled_dpi_slots;
	/* DPI/CPI is encoded in the way the PMW3360 sensor accepts it
	 * value = (DPI - 100) / 100
	 * If XY are identical, dpi[0-6] contain the sensitivities,
	 * while in XY independent mode each entry takes two chars for X and Y.
	 */
	uint8_t dpi[16];
	struct sinowealth_rgb8 dpi_color[8];
	uint8_t rgb_effect; /* see enum rgb_effect */
	/* 0x40 - brightness (constant)
	 * 0x1/2/3 - speed
	 */
	uint8_t glorious_mode;
	uint8_t glorious_direction;
	uint8_t single_mode;
	struct sinowealth_rbg8 single_color;
	/* 0x40 - brightness (constant)
	 * 0x1/2/3 - speed
	 */
	uint8_t breathing7_mode;
	uint8_t breathing7_colorcount;
	struct sinowealth_rbg8 breathing7_colors[7];
	/* 0x10/20/30/40 - brightness
	 * 0x1/2/3 - speed
	 */
	uint8_t tail_mode;
	uint8_t breathing_mode;
	uint8_t constant_color_mode;
	struct sinowealth_rbg8 constant_color_colors[6];
	uint8_t unknown3[13];
	/* 0x10/20/30/40 - brightness
	 * 0x1/2/3 - speed
	 */
	uint8_t rave_mode;
	struct sinowealth_rbg8 rave_colors[2];
	/* 0x10/20/30/40 - brightness
	 * 0x1/2/3 - speed
	 */
	uint8_t wave_mode;
	/* 0x1/2/3 - speed */
	uint8_t breathing1_mode;
	struct sinowealth_rbg8 breathing1_color;
	uint8_t unknown4;
	/* 0x1 - 2 mm
	 * 0x2 - 3 mm
	 * 0xff - indicates that lift off distance is changed with a dedicated command. Not constant, so do **NOT** overwrite it.
	 */
	uint8_t lift_off_distance;
	uint8_t unknown5[36];
	uint8_t padding[SINOWEALTH_CONFIG_SIZE - SINOWEALTH_CONFIG_SIZE_USED_MAX];
} __attribute__((packed));

_Static_assert(sizeof(struct sinowealth_config_report) == SINOWEALTH_CONFIG_SIZE, "Invalid size");

struct sinowealth_data {
	bool is_long;
	unsigned int config_size;
	unsigned int led_count;
	struct sinowealth_config_report config;
};

struct sinowealth_report_rate_mapping {
	uint8_t raw;
	unsigned int report_rate;
};

static const struct sinowealth_report_rate_mapping sinowealth_report_rate_map[] = {
	{ 0x1, 125 },
	{ 0x2, 250 },
	{ 0x3, 500 },
	{ 0x4, 1000 },
};

/* @return Internal report rate representation or 0 on error. */
static uint8_t
sinowealth_report_rate_to_raw(unsigned int report_rate)
{
	const struct sinowealth_report_rate_mapping *mapping = NULL;
	ARRAY_FOR_EACH(sinowealth_report_rate_map, mapping)
		if (mapping->report_rate == report_rate)
			return mapping->raw;
	return 0;
}

/* @return Report rate in hz or 0 on error. */
static unsigned int
sinowealth_raw_to_report_rate(uint8_t raw)
{
	const struct sinowealth_report_rate_mapping *mapping = NULL;
	ARRAY_FOR_EACH(sinowealth_report_rate_map, mapping)
		if (mapping->raw == raw)
			return mapping->report_rate;
	return 0;
}

static int
sinowealth_raw_to_dpi(int raw)
{
	return (raw + 1) * 100;
}

static int
sinowealth_dpi_to_raw(int dpi)
{
	assert(dpi >= SINOWEALTH_DPI_MIN && dpi <= SINOWEALTH_DPI_MAX);
	return dpi / 100 - 1;
}

static struct ratbag_color
sinowealth_raw_to_color(struct sinowealth_rgb8 raw)
{
	return (struct ratbag_color) {.red = raw.r, .green = raw.g, .blue = raw.b};
}

static struct sinowealth_rgb8
sinowealth_color_to_raw(struct ratbag_color color)
{
	return (struct sinowealth_rgb8) {.r = color.red, .g = color.green, .b = color.blue};
}

static struct ratbag_color
sinowealth_rbg_to_color(struct sinowealth_rbg8 raw)
{
	return (struct ratbag_color) {.red = raw.r, .green = raw.g, .blue = raw.b};
}

static struct sinowealth_rbg8
sinowealth_color_to_rbg(struct ratbag_color color)
{
	return (struct sinowealth_rbg8) {.r = color.red, .g = color.green, .b = color.blue};
}

static int
sinowealth_rgb_mode_to_brightness(uint8_t mode)
{
	/* brightness is 0-4 in the upper nibble of mode,
	 * while libratbag uses 0-255.
	 */
	return min((mode >> 4) * 64, 255);
}

static uint8_t
sinowealth_brightness_to_rgb_mode(int brightness)
{
	/* convert 0-255 to 0-4 in the upper nibble of mode */
	return ((brightness + 1) / 64) << 4;
}

static int
sinowealth_rgb_mode_to_duration(uint8_t mode)
{
	switch (mode & SINOWEALTH_RGB_SPEED_BITS) {
	case 0: return 10000; /* static: does not translate to duration */
	case 1: return 1500;
	case 2: return 1000;
	case 3: return 500;
	default: return 0;
	}
}

static uint8_t
sinowealth_duration_to_rgb_mode(int duration_ms)
{
	uint8_t mode = 0;
	if (duration_ms <= 500) {
		mode |= 3;
	} else if (duration_ms <= 1000) {
		mode |= 2;
	} else {
		mode |= 1;
	}
	return mode;
}

static void
sinowealth_set_led_from_rgb_mode(struct ratbag_led *led, uint8_t mode)
{
	led->brightness = sinowealth_rgb_mode_to_brightness(mode);
	led->ms = sinowealth_rgb_mode_to_duration(mode);
}

static uint8_t
sinowealth_led_to_rgb_mode(const struct ratbag_led *led)
{
	uint8_t mode;
	mode = sinowealth_brightness_to_rgb_mode(led->brightness);
	mode |= sinowealth_duration_to_rgb_mode(led->ms);
	return mode;
}

static int
sinowealth_print_fw_version(struct ratbag_device *device) {
	int rc = 0;

	uint8_t version[6] = {SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_FIRMWARE_VERSION};
	rc = ratbag_hidraw_set_feature_report(device, SINOWEALTH_REPORT_ID_CMD, version, sizeof(version));
	if (rc != sizeof(version)) {
		log_error(device->ratbag, "Error while sending read firmware version command: %d\n", rc);
		return -1;
	}
	rc = ratbag_hidraw_get_feature_report(device, SINOWEALTH_REPORT_ID_CMD, version, sizeof(version));
	if (rc != sizeof(version)) {
		log_error(device->ratbag, "Could not read firmware version: %d\n", rc);
		return -1;
	}
	log_info(device->ratbag, "firmware version: %.4s\n", version + 2);

	return 0;
}

static int
sinowealth_read_raw_config(struct ratbag_device* device) {
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->config;

	uint8_t cmd[6] = {SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_GET_CONFIG};
	rc = ratbag_hidraw_set_feature_report(device, SINOWEALTH_REPORT_ID_CMD, cmd, sizeof(cmd));
	if (rc != sizeof(cmd)) {
		log_error(device->ratbag, "Error while sending read config command: %d\n", rc);
		return -1;
	}

	const char config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	rc = ratbag_hidraw_get_feature_report(device, config_report_id,
					      (uint8_t*) config, SINOWEALTH_CONFIG_SIZE);
	/* The GET_FEATURE report length has to be 520, but the actual data returned is less */
	if (rc < SINOWEALTH_CONFIG_SIZE_USED_MIN || rc > SINOWEALTH_CONFIG_SIZE_USED_MAX) {
		log_error(device->ratbag, "Could not read device configuration: %d\n", rc);
		return -1;
	}
	drv_data->config_size = rc;

	return 0;
}

/* Update profile with values from raw configuration data. */
static int
sinowealth_update_profile_from_config(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->config;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

	/* Report rate */
	const unsigned int hz = sinowealth_raw_to_report_rate(config->report_rate);
	ratbag_profile_set_report_rate(profile, hz);

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (config->config & SINOWEALTH_XY_INDEPENDENT) {
			resolution->dpi_x = sinowealth_raw_to_dpi(config->dpi[resolution->index * 2]);
			resolution->dpi_y = sinowealth_raw_to_dpi(config->dpi[resolution->index * 2 + 1]);
		} else {
			resolution->dpi_x = sinowealth_raw_to_dpi(config->dpi[resolution->index]);
			resolution->dpi_y = resolution->dpi_x;
		}
		if (config->disabled_dpi_slots & (1 << resolution->index)) {
			/* DPI step is disabled, fake it by setting DPI to 0 */
			resolution->dpi_x = 0;
			resolution->dpi_y = 0;
		}
		resolution->is_active = resolution->index == config->active_dpi - 1u;
		resolution->is_default = resolution->is_active;
	}

	/* Body lighting */
	if (drv_data->led_count > 0) {
		led = ratbag_profile_get_led(profile, 0);
		switch (config->rgb_effect) {
		case RGB_OFF:
			led->mode = RATBAG_LED_OFF;
			break;
		case RGB_SINGLE:
			led->mode = RATBAG_LED_ON;
			led->color = sinowealth_rbg_to_color(config->single_color);
			led->brightness = sinowealth_rgb_mode_to_brightness(config->single_mode);
			break;
		case RGB_GLORIOUS:
		case RGB_BREATHING:
		case RGB_BREATHING7:
		case RGB_TAIL:
		case RGB_RAVE:
		case RGB_WAVE:
			led->mode = RATBAG_LED_CYCLE;
			sinowealth_set_led_from_rgb_mode(led, config->glorious_mode);
			break;
		case RGB_BREATHING1:
			led->mode = RATBAG_LED_BREATHING;
			led->color = sinowealth_rbg_to_color(config->breathing1_color);
			sinowealth_set_led_from_rgb_mode(led, config->breathing1_mode);
			break;
		}
		ratbag_led_unref(led);
	}

	profile->is_active = true;

	return 0;
}

static int
sinowealth_init_profile(struct ratbag_device *device)
{
	int rc;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->config;

	rc = sinowealth_print_fw_version(device);
	if (rc)
		return rc;

	rc = sinowealth_read_raw_config(device);
	if (rc)
		return rc;

	/* LED count. */
	drv_data->led_count = 0;
	if (config->rgb_effect != RGB_NOT_SUPPORTED) {
		drv_data->led_count += 1;
	}
	/* We may want to account for the DPI LEDs in the future.
	 * We don't support them yet, so it's not a priority now.
	 */

	/* number of DPIs = all DPIs from min to max (inclusive) and "0 DPI" as a special value
	 * to signal a disabled DPI step.
	 */
	int num_dpis = (SINOWEALTH_DPI_MAX - SINOWEALTH_DPI_MIN) / SINOWEALTH_DPI_STEP + 2;
	unsigned int dpis[num_dpis];

	/* TODO: Button remapping */
	ratbag_device_init_profiles(device, 1, SINOWEALTH_NUM_DPIS, 0, drv_data->led_count);

	profile = ratbag_device_get_profile(device, 0);

	/* Generate DPI list */
	dpis[0] = 0; /* 0 DPI = disabled */
	for (int i = 1; i < num_dpis; i++) {
		dpis[i] = SINOWEALTH_DPI_MIN + (i - 1) * SINOWEALTH_DPI_STEP;
	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		ratbag_resolution_set_dpi_list(resolution, dpis, num_dpis);
		ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
	}

	/* Set up available report rates. */
	unsigned int report_rates[] = { 125, 250, 500, 1000 };
	ratbag_profile_set_report_rate_list(profile, report_rates, ARRAY_LENGTH(report_rates));

	/* Set up LED capabilities */
	if (drv_data->led_count > 0) {
		led = ratbag_profile_get_led(profile, 0);
		led->type = RATBAG_LED_TYPE_SIDE;
		led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
		ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
		ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
		ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
		ratbag_led_unref(led);
	}

	ratbag_profile_unref(profile);

	return 0;
}

static int
sinowealth_test_hidraw(struct ratbag_device *device)
{
	int rc = 0;

	/* Only the keyboard interface has this report */
	rc = ratbag_hidraw_has_report(device, SINOWEALTH_REPORT_ID_CONFIG);
	if (rc)
		return rc;

	rc = ratbag_hidraw_has_report(device, SINOWEALTH_REPORT_ID_CONFIG_LONG);
	if (rc) {
		struct sinowealth_data *drv_data = device->drv_data;
		drv_data->is_long = true;

		return rc;
	}

	return 0;
}

static int
sinowealth_probe(struct ratbag_device *device)
{
	int rc;
	struct sinowealth_data *drv_data = 0;
	struct ratbag_profile *profile = 0;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	rc = ratbag_find_hidraw(device, sinowealth_test_hidraw);
	if (rc)
		goto err;

	rc = sinowealth_init_profile(device);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	profile = ratbag_device_get_profile(device, 0);
	rc = sinowealth_update_profile_from_config(profile);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	ratbag_profile_unref(profile);

	return 0;

err:
	ratbag_profile_unref(profile);

	free(drv_data);
	ratbag_set_drv_data(device, 0);
	return rc;
}

static int
sinowealth_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile = ratbag_device_get_profile(device, 0);
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->config;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	int rc;
	uint8_t dpi_enabled = 0;

	/* Update report rate. */
	uint8_t reported_rate = sinowealth_report_rate_to_raw(profile->hz);
	if (reported_rate == 0) {
		log_error(device->ratbag, "Incorrect report rate %u was requested\n", profile->hz);

		/* Fall back to 125hz. */
		reported_rate = sinowealth_report_rate_to_raw(125);
		profile->hz = 125;
	}
	config->report_rate = reported_rate;

	/* Check if any resolution requires independent XY DPIs */
	config->config &= ~SINOWEALTH_XY_INDEPENDENT;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->dpi_x != resolution->dpi_y && resolution->dpi_x && resolution->dpi_y) {
			config->config |= SINOWEALTH_XY_INDEPENDENT;
			break;
		}
	}

	config->dpi_count = 0;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dpi_x || !resolution->dpi_y)
			continue;
		if (resolution->is_active)
			config->active_dpi = resolution->index + 1U;
		if (config->config & SINOWEALTH_XY_INDEPENDENT) {
			config->dpi[resolution->index * 2] = sinowealth_dpi_to_raw(resolution->dpi_x);
			config->dpi[resolution->index * 2 + 1] = sinowealth_dpi_to_raw(resolution->dpi_y);
		} else {
			config->dpi[resolution->index] = sinowealth_dpi_to_raw(resolution->dpi_x);
		}
		dpi_enabled |= 1<<resolution->index;
		config->dpi_count++;
	}
	config->disabled_dpi_slots = ~dpi_enabled;

	/* Body lighting */
	if (drv_data->led_count > 0) {
		led = ratbag_profile_get_led(profile, 0);
		switch(led->mode) {
		case RATBAG_LED_OFF:
			config->rgb_effect = RGB_OFF;
			break;
		case RATBAG_LED_ON:
			config->rgb_effect = RGB_SINGLE;
			config->single_color = sinowealth_color_to_rbg(led->color);
			break;
		case RATBAG_LED_CYCLE:
			config->rgb_effect = RGB_GLORIOUS;
			config->glorious_mode = sinowealth_led_to_rgb_mode(led);
			break;
		case RATBAG_LED_BREATHING:
			config->rgb_effect = RGB_BREATHING1;
			config->breathing1_color = sinowealth_color_to_rbg(led->color);
			config->breathing1_mode = sinowealth_led_to_rgb_mode(led);
			break;
		}
		ratbag_led_unref(led);
	}

	config->config_write = drv_data->config_size - 8;

	const char config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	rc = ratbag_hidraw_set_feature_report(device, config_report_id,
					      (uint8_t*) config, SINOWEALTH_CONFIG_SIZE);
	if (rc != SINOWEALTH_CONFIG_SIZE) {
		log_error(device->ratbag, "Error while writing config: %d\n", rc);
		ratbag_profile_unref(profile);
		return -1;
	}

	ratbag_profile_unref(profile);
	return 0;
}

static void
sinowealth_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver sinowealth_driver = {
	.name = "Sinowealth",
	.id = "sinowealth",
	.probe = sinowealth_probe,
	.remove = sinowealth_remove,
	.commit = sinowealth_commit
};
