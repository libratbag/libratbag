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
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_report_id) == sizeof(uint8_t), "Invalid size");

enum sinowealth_command_id {
	SINOWEALTH_CMD_FIRMWARE_VERSION = 0x1,
	SINOWEALTH_CMD_PROFILE = 0x2,
	SINOWEALTH_CMD_GET_CONFIG = 0x11,
	SINOWEALTH_CMD_DEBOUNCE = 0x1a,
	/* Only works on devices that use CONFIG_LONG report ID. */
	SINOWEALTH_CMD_LONG_ANGLESNAPPING_AND_LOD = 0x1b,
	/* Same as GET_CONFIG but for the second profile. */
	SINOWEALTH_CMD_GET_CONFIG2 = 0x21,
	/* Same as GET_CONFIG but for the second profile. */
	SINOWEALTH_CMD_GET_BUTTONS2 = 0x22,
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_command_id) == sizeof(uint8_t), "Invalid size");

#define SINOWEALTH_CMD_SIZE 6

/* Report length commands that get configuration data should use. */
#define SINOWEALTH_CONFIG_REPORT_SIZE 520
#define SINOWEALTH_CONFIG_SIZE_MAX 167
#define SINOWEALTH_CONFIG_SIZE_MIN 131

/* The PC software only goes down to 400, but the PMW3360 doesn't care */
#define SINOWEALTH_DPI_MIN 100
#define SINOWEALTH_DPI_STEP 100

/* Different software expose different amount of DPI slots:
 * Glorious - 6;
 * G-Wolves - 7.
 * But in fact fact there are eight slots.
 */
#define SINOWEALTH_NUM_DPIS 8

/* Technically SinoWealth mice suport second profile, but there isn't
 * a single configuration software that exposes it.
 */
#define SINOWEALTH_NUM_PROFILES 1

/* Bit mask for @ref sinowealth_config_report.config.
 *
 * This naming may be incorrect as it's not actually known waht the other bits do.
 */
enum sinowealth_config_data_mask {
	SINOWEALTH_XY_INDEPENDENT = 0b1000,
};

/* Color data the way mouse stores it.
 *
 * @ref sinowealth_raw_to_color.
 *
 * @ref sinowealth_color_to_raw.
 *
 * @ref sinowealth_led_format.
 */
struct sinowealth_color {
	/* May be in either RGB or RBG format depending on the device.
	 * See the comment above this struct.
	 */
	uint8_t data[3];
} __attribute__((packed));
_Static_assert(sizeof(struct sinowealth_color) == 3, "Invalid size");

enum sinowealth_sensor {
	PWM3327,
	PWM3360,
	PWM3389,
};

enum sinowealth_rgb_effect {
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
} __attribute__((packed));
_Static_assert(sizeof(enum sinowealth_rgb_effect) == sizeof(uint8_t), "Invalid size");

struct sinowealth_rgb_mode {
	/* 0x1/2/3.
	 * @ref sinowealth_duration_to_rgb_mode.
	 * @ref sinowealth_rgb_mode_to_duration.
	 */
	uint8_t speed:4;
	/* 0x1/2/3/4.
	 * @ref sinowealth_brightness_to_rgb_mode.
	 * @ref sinowealth_rgb_mode_to_brightness.
	 */
	uint8_t brightness:4;
};
_Static_assert(sizeof(struct sinowealth_rgb_mode) == sizeof(uint8_t), "Invalid size");

enum sinowealth_led_format {
	LED_NONE,
	LED_RBG,
	LED_RGB,
};

/* Configuration data the way it's stored in mouse memory.
 * When we want to change a setting, we basically copy the entire mouse
 * configuration, modify it and send it back.
 */
struct sinowealth_config_report {
	enum sinowealth_report_id report_id;
	enum sinowealth_command_id command_id;
	uint8_t unknown1;
	/* 0x0 - read.
	 * CONFIG_SIZE-8 - write.
	 */
	uint8_t config_write;
	uint8_t unknown2[6];
	/* @ref sinowealth_report_rate_map. */
	uint8_t report_rate:4;
	/* 0b1000 - make DPI axes independent. */
	uint8_t config_flags:4;
	uint8_t dpi_count:4;
	/* Starting from 1 counting only active slots. */
	uint8_t active_dpi:4;
	/* bit set: disabled, unset: enabled */
	uint8_t disabled_dpi_slots;
	/* DPI/CPI is encoded in the way the PMW3360 sensor accepts it:
	 * value = (DPI - 100) / 100;
	 * or the way the PMW3389 sensor accepts it:
	 * value = DPI / 100;
	 * TODO: what about PWM3327?
	 * If XY are identical, dpi[0-6] contain the sensitivities,
	 * while in XY independent mode each entry takes two chars for X and Y.
	 * @ref sinowealth_raw_to_dpi
	 * @ref sinowealth_dpi_to_raw
	 */
	uint8_t dpi[16];
	struct sinowealth_color dpi_color[8];
	enum sinowealth_rgb_effect rgb_effect;
	struct sinowealth_rgb_mode glorious_mode;
	uint8_t glorious_direction;
	struct sinowealth_rgb_mode single_mode;
	struct sinowealth_color single_color;
	struct sinowealth_rgb_mode breathing7_mode;
	uint8_t breathing7_colorcount;
	struct sinowealth_color breathing7_colors[7];
	struct sinowealth_rgb_mode tail_mode;
	struct sinowealth_rgb_mode breathing_mode;
	struct sinowealth_rgb_mode constant_color_mode;
	struct sinowealth_color constant_color_colors[6];
	uint8_t unknown3[13];
	struct sinowealth_rgb_mode rave_mode;
	struct sinowealth_color rave_colors[2];
	struct sinowealth_rgb_mode wave_mode;
	struct sinowealth_rgb_mode breathing1_mode;
	struct sinowealth_color breathing1_color;
	/* 0x1 - 2 mm.
	 * 0x2 - 3 mm.
	 * 0xff - indicates that lift off distance is changed with a dedicated command. Not constant, so do **NOT** overwrite it.
	 */
	uint8_t lift_off_distance;
	uint8_t unknown4;

	/* From here onward goes the data only available in long mice. */

	uint8_t unknown5[SINOWEALTH_CONFIG_SIZE_MAX - SINOWEALTH_CONFIG_SIZE_MIN];

	uint8_t padding[SINOWEALTH_CONFIG_REPORT_SIZE - SINOWEALTH_CONFIG_SIZE_MAX];
} __attribute__((packed));
_Static_assert(sizeof(struct sinowealth_config_report) == SINOWEALTH_CONFIG_REPORT_SIZE, "Invalid size");

/* Data related to mouse we store for ourselves. */
struct sinowealth_data {
	/* Whether the device uses REPORT_ID_CONFIG or REPORT_ID_CONFIG_LONG. */
	bool is_long;
	enum sinowealth_led_format led_type;
	enum sinowealth_sensor sensor;
	unsigned int config_size;
	/* Cached profile index. This might be incorrect if profile index was changed by another program while we are running. */
	unsigned int current_profile_index;
	unsigned int led_count;
	struct sinowealth_config_report configs[SINOWEALTH_NUM_PROFILES];
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

/* @return Maximum DPI for sensor `sensor` or 0 on error. */
static unsigned int
get_max_dpi_for_sensor(enum sinowealth_sensor sensor)
{
	switch (sensor) {
	case PWM3327: return 10200;
	case PWM3360: return 12000;
	case PWM3389: return 16000;
	default: return 0;
	}
}

/* Convert internal sensor resolution `raw` to DPI. */
static unsigned int
sinowealth_raw_to_dpi(struct ratbag_device *device, unsigned int raw)
{
	struct sinowealth_data *drv_data = device->drv_data;
	enum sinowealth_sensor sensor = drv_data->sensor;

	if (sensor == PWM3360)
		raw += 1;

	unsigned int dpi = raw * 100;

	return dpi;
}

/* Convert DPI `dpi` to internal sensor resolution. */
static unsigned int
sinowealth_dpi_to_raw(struct ratbag_device *device, unsigned int dpi)
{
	struct sinowealth_data *drv_data = device->drv_data;
	enum sinowealth_sensor sensor = drv_data->sensor;

	assert(dpi >= SINOWEALTH_DPI_MIN && dpi <= get_max_dpi_for_sensor(sensor));

	unsigned int raw = dpi / 100;

	if (sensor == PWM3360)
		raw -= 1;

	return raw;
}

/* Convert internal mouse color `raw` to color.
 * If LED type defined in the device data is incorrect, RBG color order is used.
 */
static struct ratbag_color
sinowealth_raw_to_color(struct ratbag_device *device, struct sinowealth_color raw_color)
{
	struct sinowealth_data *drv_data = device->drv_data;

	struct ratbag_color color;

	switch (drv_data->led_type) {
	/* Fall back to RBG if the LED type is incorrect. */
	default:
	case LED_RBG:
		color.red = raw_color.data[0];
		color.green = raw_color.data[2];
		color.blue = raw_color.data[1];
		break;
	case LED_RGB:
		color.red = raw_color.data[0];
		color.green = raw_color.data[1];
		color.blue = raw_color.data[2];
		break;
	}

	return color;
}

/* Convert color `color` to internal representation of color of the mouse.
 * If LED type defined in the device data is incorrect, RBG color order is used.
 */
static struct sinowealth_color
sinowealth_color_to_raw(struct ratbag_device *device, struct ratbag_color color)
{
	struct sinowealth_data *drv_data = device->drv_data;

	struct sinowealth_color raw_color;

	switch (drv_data->led_type) {
	/* Fall back to RBG if the LED type is incorrect. */
	default:
	case LED_RBG:
		raw_color.data[0] = color.red;
		raw_color.data[1] = color.blue;
		raw_color.data[2] = color.green;
		break;
	case LED_RGB:
		raw_color.data[0] = color.red;
		raw_color.data[1] = color.green;
		raw_color.data[2] = color.blue;
		break;
	}

	return raw_color;
}

/* Get brightness to use with ratbag's API from RGB mode `mode`. */
static int
sinowealth_rgb_mode_to_brightness(struct sinowealth_rgb_mode mode)
{
	/* Convert 0-4 to 0-255. */
	return min(mode.brightness * 64, 255);
}

/* Convert 8 bit brightness value to internal representation of brightness of the mouse. */
static uint8_t
sinowealth_brightness_to_rgb_mode(uint8_t brightness)
{
	/* Convert 0-255 to 0-4. */
	return (brightness + 1) / 64;
}

/* @return Effect duration or `0` on error. */
static int
sinowealth_rgb_mode_to_duration(struct sinowealth_rgb_mode mode)
{
	switch (mode.speed) {
	case 0: return 10000; /* static: does not translate to duration */
	case 1: return 1500;
	case 2: return 1000;
	case 3: return 500;
	default: return 0;
	}
}

/* Convert duration value `duration` to representation of brightness of the mouse.
 *
 * @param duration Duration in milliseconds.
 */
static uint8_t
sinowealth_duration_to_rgb_mode(unsigned int duration)
{
	uint8_t mode = 0;
	if (duration <= 500) {
		mode |= 3;
	} else if (duration <= 1000) {
		mode |= 2;
	} else {
		mode |= 1;
	}
	return mode;
}

/* Fill LED `led` with values from mode `mode`. */
static void
sinowealth_set_led_from_rgb_mode(struct ratbag_led *led, struct sinowealth_rgb_mode mode)
{
	led->brightness = sinowealth_rgb_mode_to_brightness(mode);
	led->ms = sinowealth_rgb_mode_to_duration(mode);
}

/* Convert data in LED `led` to RGB mode. */
static struct sinowealth_rgb_mode
sinowealth_led_to_rgb_mode(const struct ratbag_led *led)
{
	struct sinowealth_rgb_mode mode;
	mode.brightness = sinowealth_brightness_to_rgb_mode(led->brightness);
	mode.speed = sinowealth_duration_to_rgb_mode(led->ms);
	return mode;
}

/* Do a read query.
 *
 * After an error assume `buffer` now has garbage data.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_query_read(struct ratbag_device *device, uint8_t buffer[], unsigned int buffer_length)
{
	/*
	 * TODO: make this work with sinowealth_read_raw_config. Currently it
	 * doesn't because that function has some custom behavior.
	 */

	int rc = 0;

	/* Buffer's first byte is always the report ID. */
	const uint8_t report_id = buffer[0];
	/* Buffer's second byte in case of SinoWealth is always the command ID. */
	const uint8_t query_command = buffer[1];

	/* The way we retrieve data from SinoWealth is as follows:
	 *
	 * - Set a feature report with first two bytes corresponding to the
	 * wanted command.
	 *
	 * - Get a feature report with the same report ID and buffer length.
	 * The buffer can be reused from the previous step for more efficiency.
	 * We also do this to reduce the amount of arguments in the function.
	 */

	rc = ratbag_hidraw_set_feature_report(device, report_id, buffer, buffer_length);
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Could not set feature report in a read query: %d\n", rc);
		return -1;
	}
	rc = ratbag_hidraw_get_feature_report(device, report_id, buffer, buffer_length);
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Could not get feature report in a read query: %d\n", rc);
		return -1;
	}

	/* Check if the response we got is for the correct command. */
	if (buffer[1] != query_command) {
		log_error(device->ratbag, "Could not read command %#x, got command %#x instead\n", query_command, buffer[1]);
		return -1;
	}

	return 0;
}

/* Do a write query.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_query_write(struct ratbag_device *device, uint8_t buffer[], unsigned int buffer_length)
{
	int rc = 0;

	/* Buffer's first byte is always the report ID. */
	const uint8_t report_id = buffer[0];

	rc = ratbag_hidraw_set_feature_report(device, report_id, buffer, buffer_length);
	if (rc != (int)buffer_length) {
		log_error(device->ratbag, "Could not set feature report in a write query: %d\n", rc);
		return -1;
	}

	return 0;
}

/* @return Active profile index or a negative error code. */
static int
sinowealth_get_active_profile(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;

	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_PROFILE };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Could not get device's active profile\n");
		return -1;
	}

	unsigned int index = buf[2] - 1;

	drv_data->current_profile_index = index;

	return (int)index;
}

/* Make the profile at index `index` the active one.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	assert(index <= SINOWEALTH_NUM_PROFILES - 1);

	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;

	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_PROFILE, index + 1 };

	rc = sinowealth_query_write(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Error while selecting profile: %d\n", rc);
		return -1;
	}

	drv_data->current_profile_index = index;

	return 0;
}

/* Fill buffer `out` with firmware version.
 *
 * @param out The buffer output will be written to.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_get_fw_version(struct ratbag_device *device, char out[4])
{
	int rc = 0;

	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_FIRMWARE_VERSION };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Couldn't read firmware version: %d\n", rc);
		return -1;
	}

	memcpy(out, buf + 2, 4);

	return 0;
}

/* @return Time in milliseconds or a negative error code. */
static int
sinowealth_get_debounce_time(struct ratbag_device *device)
{
	int rc = 0;

	/* TODO: implement debounce time changing once we have an API for that.
	 * To implement it here just set the third index to the desired debounce time / 2.
	 */
	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_DEBOUNCE };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Could not read debounce time: %d\n", rc);
		return -1;
	}

	return buf[2] * 2;
}

/* Print angle snapping (Cal line) and lift-off distance (LOD) modes.
 *
 * This is only confirmed to work on G-Wolves Hati where the way with
 * config report doesn't work. This does not work on Glorious Model O.
 */
static int
sinowealth_print_long_lod_and_anglesnapping(struct ratbag_device *device)
{
	int rc = 0;

	/* TODO: implement angle snapping and lift-off distance changing once we have an API for that.
	 * To implement LOD changing here: set the third index to <whether you want LOD high or low> + 1.
	 * To implement angle snapping toggling here: set the fourth index to 1 or 0 to enable or disable accordingly.
	 */
	uint8_t buf[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_LONG_ANGLESNAPPING_AND_LOD };

	rc = sinowealth_query_read(device, buf, sizeof(buf));
	if (rc != 0) {
		log_error(device->ratbag, "Could not read LOD and angle snapping values: %d\n", rc);
		return -1;
	}

	log_info(device->ratbag, "LOD is high: %u\n", buf[2] - 1);
	log_info(device->ratbag, "Angle snapping enabled: %u\n", buf[3]);

	return 0;
}

/* Read configuration data from the mouse and save it in drv_data.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_read_raw_config(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config1 = &drv_data->configs[0];

	const uint8_t config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	uint8_t cmd[SINOWEALTH_CMD_SIZE] = { SINOWEALTH_REPORT_ID_CMD, SINOWEALTH_CMD_GET_CONFIG };

	/* TODO: adapt @ref sinowealth_query_read to work here and use it. */

	rc = ratbag_hidraw_set_feature_report(device, SINOWEALTH_REPORT_ID_CMD, cmd, sizeof(cmd));
	if (rc != sizeof(cmd)) {
		log_error(device->ratbag, "Error while sending read config command: %d\n", rc);
		return -1;
	}

	rc = ratbag_hidraw_get_feature_report(device, config_report_id,
					      (uint8_t*) config1, SINOWEALTH_CONFIG_REPORT_SIZE);
	/* The GET_FEATURE report length has to be 520, but the actual data returned is less */
	if (rc < SINOWEALTH_CONFIG_SIZE_MIN || rc > SINOWEALTH_CONFIG_SIZE_MAX) {
		log_error(device->ratbag, "Could not read device configuration: %d\n", rc);
		return -1;
	}
	drv_data->config_size = rc;

	log_debug(device->ratbag, "Configuration size is %d bytes\n", drv_data->config_size);

	return 0;
}

/* Update profile with values from raw configuration data. */
static void
sinowealth_update_profile_from_config(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->configs[profile->index];
	struct ratbag_led *led = NULL;
	struct ratbag_resolution *resolution = NULL;

	/* Report rate */
	const unsigned int hz = sinowealth_raw_to_report_rate(config->report_rate);
	ratbag_profile_set_report_rate(profile, hz);

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (config->config_flags & SINOWEALTH_XY_INDEPENDENT) {
			resolution->dpi_x = sinowealth_raw_to_dpi(device, config->dpi[resolution->index * 2]);
			resolution->dpi_y = sinowealth_raw_to_dpi(device, config->dpi[resolution->index * 2 + 1]);
		} else {
			resolution->dpi_x = sinowealth_raw_to_dpi(device, config->dpi[resolution->index]);
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
			led->color = sinowealth_raw_to_color(device, config->single_color);
			led->brightness = sinowealth_rgb_mode_to_brightness(config->single_mode);
			break;
		case RGB_GLORIOUS:
		case RGB_BREATHING:
		case RGB_BREATHING7:
		case RGB_CONSTANT:
		case RGB_RANDOM:
		case RGB_TAIL:
		case RGB_RAVE:
		case RGB_WAVE:
			led->mode = RATBAG_LED_CYCLE;
			sinowealth_set_led_from_rgb_mode(led, config->glorious_mode);
			break;
		case RGB_BREATHING1:
			led->mode = RATBAG_LED_BREATHING;
			led->color = sinowealth_raw_to_color(device, config->breathing1_color);
			sinowealth_set_led_from_rgb_mode(led, config->breathing1_mode);
			break;
		default:
			log_error(device->ratbag, "Got unknown RGB effect: %d\n", config->rgb_effect);
			break;
		}
		ratbag_led_unref(led);
	}

	profile->is_active = profile->index == drv_data->current_profile_index;
}

/* Initialize profiles for device `device`.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_init_profile(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_led *led = NULL;
	struct ratbag_profile *profile = NULL;
	struct ratbag_resolution *resolution = NULL;

	struct sinowealth_data *drv_data = device->drv_data;
	/* We only use this to detect whether RGB effects are available,
	 * so it doesn't matter which one we use. Technically they might
	 * have different values in the checked slot.
	 */
	struct sinowealth_config_report *config = &drv_data->configs[0];

	char fw_version[4];
	rc = sinowealth_get_fw_version(device, fw_version);
	if (rc)
		return rc;
	log_info(device->ratbag, "firmware version: %.4s\n", fw_version);

	rc = sinowealth_read_raw_config(device);
	if (rc)
		return rc;

	if (strncmp(fw_version, "V102", 4) == 0) {
		log_info(device->ratbag, "Found a Glorious Model O (old firmware) or a Glorious Model D\n");

		drv_data->led_type = LED_RBG;
		drv_data->sensor = PWM3360;
	} else if (strncmp(fw_version, "V103", 4) == 0) {
		log_info(device->ratbag, "Found a Glorious Model O/O- (updated firmware)\n");

		drv_data->led_type = LED_RBG;
		drv_data->sensor = PWM3360;
	} else if (strncmp(fw_version, "V161", 4) == 0) {
		/* This matches both Classic and Ace versions. */
		log_info(device->ratbag, "Found a G-Wolves Hati HT-M Wired\n");

		drv_data->led_type = LED_NONE;
		/* Can also be PWM3389. */
		drv_data->sensor = PWM3360;
	} else if (strncmp(fw_version, "3105", 4) == 0) {
		/* This matches both Classic and Ace versions. */
		/* This mouse has no device file yet. */
		log_info(device->ratbag, "Found a G-Wolves Hati HT-S Wired\n");

		drv_data->led_type = LED_NONE;
		/* Can also be PWM3389. */
		drv_data->sensor = PWM3360;
	} else if (strncmp(fw_version, "3106", 4) == 0) {
		log_info(device->ratbag, "Found a DreamMachines DM5 Blink\n");

		drv_data->led_type = LED_RGB;
		drv_data->sensor = PWM3389;
	} else if (strncmp(fw_version, "3110", 4) == 0) {
		log_info(device->ratbag, "Found a Genesis Xenon 770\n");

		drv_data->led_type = LED_RGB; /* TODO: test this */
		drv_data->sensor = PWM3327;
	} else {
		log_info(device->ratbag, "Found an unknown SinoWealth mouse\n");

		/* These seem to be the most widely used values. */
		drv_data->led_type = LED_RBG;
		drv_data->sensor = PWM3360;
	}

	/* LED count. */
	drv_data->led_count = 0;
	if (config->rgb_effect != RGB_NOT_SUPPORTED && drv_data->led_type != LED_NONE) {
		drv_data->led_count += 1;
	}
	/* We may want to account for the DPI LEDs in the future.
	 * We don't support them yet, so it's not a priority now.
	 */

	/* Number of DPIs = all DPIs from min to max (inclusive) and "0 DPI" as a special value
	 * to signal a disabled DPI step.
	 */
	unsigned int num_dpis = (get_max_dpi_for_sensor(drv_data->sensor) - SINOWEALTH_DPI_MIN) / SINOWEALTH_DPI_STEP + 2;

	/* TODO: Button remapping */
	ratbag_device_init_profiles(device, SINOWEALTH_NUM_PROFILES, SINOWEALTH_NUM_DPIS, 0, drv_data->led_count);

	/* Generate DPI list */
	unsigned int dpis[num_dpis];
	dpis[0] = 0; /* 0 DPI = disabled */
	for (unsigned int i = 1; i < num_dpis; i++) {
		dpis[i] = SINOWEALTH_DPI_MIN + (i - 1) * SINOWEALTH_DPI_STEP;
	}

	ratbag_device_for_each_profile(device, profile) {
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
	}

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

/* Write raw configuration data in drv_data to the mouse.
 *
 * @return 0 on success or an error code.
 */
static int
sinowealth_write_config(struct ratbag_device *device)
{
	int rc = 0;

	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config1 = &drv_data->configs[0];

	const uint8_t config_report_id = drv_data->is_long ? SINOWEALTH_REPORT_ID_CONFIG_LONG : SINOWEALTH_REPORT_ID_CONFIG;

	config1->report_id = config_report_id;
	config1->command_id = SINOWEALTH_CMD_GET_CONFIG;
	config1->config_write = drv_data->config_size - 8;

	rc = sinowealth_query_write(device, (uint8_t*)config1, sizeof(*config1));
	if (rc != 0) {
		log_error(device->ratbag, "Error while writing config: %d\n", rc);
		return -1;
	}

	return 0;
}

static int
sinowealth_probe(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_profile *profile = NULL;
	struct sinowealth_data *drv_data = NULL;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	rc = ratbag_find_hidraw(device, sinowealth_test_hidraw);
	if (rc)
		goto err;

	rc = sinowealth_set_active_profile(device, 0);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	rc = sinowealth_init_profile(device);
	if (rc) {
		rc = -ENODEV;
		goto err;
	}

	ratbag_device_for_each_profile(device, profile) {
		sinowealth_update_profile_from_config(profile);
	}

	rc = sinowealth_get_active_profile(device);
	if (rc >= 0)
		log_debug(device->ratbag, "Current profile index: %d\n", rc);

	rc = sinowealth_get_debounce_time(device);
	if (rc >= 0)
		log_info(device->ratbag, "Debounce time: %d ms\n", rc);

	if (drv_data->is_long)
		sinowealth_print_long_lod_and_anglesnapping(device);

	return 0;

err:
	ratbag_profile_unref(profile);

	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

/* Update saved raw configuration data of the mouse with values from profile `profile`. */
static void
sinowealth_update_config_from_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct sinowealth_data *drv_data = device->drv_data;
	struct sinowealth_config_report *config = &drv_data->configs[profile->index];
	struct ratbag_led *led = NULL;
	struct ratbag_resolution *resolution = NULL;
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
	config->config_flags &= ~SINOWEALTH_XY_INDEPENDENT;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->dpi_x != resolution->dpi_y && resolution->dpi_x && resolution->dpi_y) {
			config->config_flags |= SINOWEALTH_XY_INDEPENDENT;
			break;
		}
	}

	config->dpi_count = 0;
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dpi_x || !resolution->dpi_y)
			continue;
		if (resolution->is_active)
			config->active_dpi = resolution->index + 1U;
		if (config->config_flags & SINOWEALTH_XY_INDEPENDENT) {
			config->dpi[resolution->index * 2] = sinowealth_dpi_to_raw(device, resolution->dpi_x);
			config->dpi[resolution->index * 2 + 1] = sinowealth_dpi_to_raw(device, resolution->dpi_y);
		} else {
			config->dpi[resolution->index] = sinowealth_dpi_to_raw(device, resolution->dpi_x);
		}
		dpi_enabled |= 1 << resolution->index;
		config->dpi_count++;
	}
	config->disabled_dpi_slots = ~dpi_enabled;

	/* Body lighting */
	if (drv_data->led_count > 0) {
		led = ratbag_profile_get_led(profile, 0);
		switch (led->mode) {
		case RATBAG_LED_OFF:
			config->rgb_effect = RGB_OFF;
			break;
		case RATBAG_LED_ON:
			config->rgb_effect = RGB_SINGLE;
			config->single_color = sinowealth_color_to_raw(device, led->color);
			break;
		case RATBAG_LED_CYCLE:
			config->rgb_effect = RGB_GLORIOUS;
			config->glorious_mode = sinowealth_led_to_rgb_mode(led);
			break;
		case RATBAG_LED_BREATHING:
			config->rgb_effect = RGB_BREATHING1;
			config->breathing1_color = sinowealth_color_to_raw(device, led->color);
			config->breathing1_mode = sinowealth_led_to_rgb_mode(led);
			break;
		}
		ratbag_led_unref(led);
	} else {
		/* Reset the value in case we accidentally managed to set it when we were not supposed to. */
		config->rgb_effect = RGB_NOT_SUPPORTED;
	}
}

static int
sinowealth_commit(struct ratbag_device *device)
{
	int rc = 0;
	struct ratbag_profile *profile = NULL;

	ratbag_device_for_each_profile(device, profile) {
		sinowealth_update_config_from_profile(profile);
	}

	rc = sinowealth_write_config(device);
	if (rc)
		return rc;

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
	.commit = sinowealth_commit,
	.set_active_profile = sinowealth_set_active_profile,
};
