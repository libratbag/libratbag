// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Michał Lubas
 */

#include "libratbag-private.h"
#include "holtek8-shared.h"

#define BIT_SET(var, pos) (var |= (1 << (pos)))
#define BIT_CLEAR(var, pos) (var &= ~(1 << (pos)))
#define BIT_CHECK(var, pos) ((var) & (1 << (pos)))
#define BITMASK(pos) ((1 << (pos)) - 1)

#define HOLTEK8B_PROFILE_DATA_SIZE 128
#define HOLTEK8B_BUTTON_CONFIG_SIZE 128

#define HOLTEK8B_CMD_SET_ACTIVE_PROFILE 0x02
#define HOLTEK8B_CMD_SET_ACTIVE_RATE 0x03
#define HOLTEK8B_CMD_SET_ACTIVE_RESOLUTION 0x04
#define HOLTEK8B_CMD_WRITE_PROFILE_DATA 0x0c
#define HOLTEK8B_CMD_WRITE_BUTTON_CONFIG 0x0d

#define HOLTEK8B_CMD_GET_ACTIVE_PROFILE 0x82
#define HOLTEK8B_CMD_GET_ACTIVE_RATE 0x83
#define HOLTEK8B_CMD_GET_ACTIVE_RESOLUTION 0x84
#define HOLTEK8B_CMD_READ_PROFILE_DATA 0x8c
#define HOLTEK8B_CMD_READ_BUTTON_CONFIG 0x8d

#define HOLTEK8B_PROFILE_COUNT 6
#define HOLTEK8B_RESOLUTION_COUNT 8
#define HOLTEK8B_DPI_VAL_COUNT 512

#define HOLTEK8B_CHUNK_SIZE 64

const unsigned int holtek8b_report_rates[] = {
	125, 250, 500, 1000
};

struct holtek8b_profile_data {
	//global, profile 0 only, unused in other
	uint8_t unk0_1[2];
	uint8_t enabled_profiles; //bitmask
	uint8_t unk3_4[2];
	uint8_t sensor_srom_id;
	uint8_t sensor_firmware_size[2]; //little-endian
	uint8_t password[8];
	uint8_t dpi_indicator_enable_bitmask[8];
	struct holtek8_rgb illumination_color[8];

	//profile content
	uint8_t sensor_reg_config[8][2];
	uint8_t enabled_rates; //bitmask, low bit - highest rate
	uint8_t unk65_69[5];
	uint8_t resolution_count;
	uint8_t illumination_mode;
	uint8_t illumination_intensity;
	uint8_t illumination_speed;
	uint8_t dpi_scale_x;
	uint8_t dpi_scale_y;
	uint8_t unk76_81[6];
	uint8_t dpi_val_x_high_bit; //bitmask
	uint8_t dpi_val_y_high_bit; //bitmask
	uint8_t dpi_val_x[8];
	uint8_t dpi_val_y[8];
	uint8_t enabled_resolutions; //bitmask
	uint8_t unk101_102[2];
	uint8_t button_debounce_ms;
	struct holtek8_rgb dpi_color[8];
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8b_profile_data) == HOLTEK8B_PROFILE_DATA_SIZE, "Invalid size");

struct holtek8b_button_config {
	struct holtek8_button_data button[16];
	uint8_t _padding[64];
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8b_button_config) == HOLTEK8B_BUTTON_CONFIG_SIZE, "Invalid size");

int
holtek8b_get_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	return ratbag_hidraw_get_feature_report(device, 0, (uint8_t*) report, HOLTEK8_FEATURE_REPORT_SIZE);
}

int
holtek8b_set_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	// known dangerous combinations below
	assert(report->command != 0xe);
	assert(!(report->command == 0xf && report->arg[0] > 50));

	holtek8_calculate_checksum(report);
	return ratbag_hidraw_set_feature_report(device, 0, (uint8_t*) report, HOLTEK8_FEATURE_REPORT_SIZE);
}

static int
holtek8b_read_profile_data(struct ratbag_device *device,
			    struct holtek8b_profile_data *profile_data, uint8_t profile_idx)
{
	int rc;
	uint8_t *data = (uint8_t*) profile_data;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_READ_PROFILE_DATA, {profile_idx}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_read_chunked(device, data, HOLTEK8B_PROFILE_DATA_SIZE, NULL);
}

static int
holtek8b_write_profile_data(struct ratbag_device *device,
			    struct holtek8b_profile_data *profile_data, uint8_t profile_idx)
{
	int rc;
	uint8_t *data = (uint8_t*) profile_data;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_WRITE_PROFILE_DATA, {profile_idx, HOLTEK8B_PROFILE_DATA_SIZE}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_write_chunked(device, data, HOLTEK8B_PROFILE_DATA_SIZE);
}

static int
holtek8b_read_button_config(struct ratbag_device *device,
			     struct holtek8b_button_config *button_config, uint8_t profile_idx)
{
	int rc;
	uint8_t *data = (uint8_t*) button_config;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_READ_BUTTON_CONFIG, {profile_idx}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_read_chunked(device, data, HOLTEK8B_BUTTON_CONFIG_SIZE, NULL);
}

static int
holtek8b_write_button_config(struct ratbag_device *device,
			     struct holtek8b_button_config *button_config, uint8_t profile_idx)
{
	int rc;
	uint8_t *data = (uint8_t*) button_config;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_WRITE_BUTTON_CONFIG, {profile_idx, HOLTEK8B_BUTTON_CONFIG_SIZE}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_write_chunked(device, data, HOLTEK8B_BUTTON_CONFIG_SIZE);
}

static int
holtek8b_get_active_profile(struct ratbag_device *device)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_GET_ACTIVE_PROFILE, {0}, 0};

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8b_get_feature_report(device, &report);
	if (rc < 0)
		return rc;

	if (report.command != HOLTEK8B_CMD_GET_ACTIVE_PROFILE)
		return -EBADMSG;

	return report.arg[0];
}

static int
holtek8b_set_active_profile(struct ratbag_device *device, unsigned int profile_idx)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_SET_ACTIVE_PROFILE, {profile_idx}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return 0;
}

static int
holtek8b_get_active_rate(struct ratbag_device *device, uint8_t profile_idx)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_GET_ACTIVE_RATE, {profile_idx}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8b_get_feature_report(device, &report);
	if (rc < 0)
		return rc;

	if (report.command != HOLTEK8B_CMD_GET_ACTIVE_RATE)
		return -EBADMSG;

	rc = (int)holtek8_raw_to_report_rate(report.arg[1]);
	if (rc == 0)
		return -EINVAL;

	return rc;
}

static int
holtek8b_set_active_rate(struct ratbag_device *device, uint8_t profile_idx, unsigned int rate)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_SET_ACTIVE_RATE, {profile_idx}, 0};
	uint8_t raw = holtek8_report_rate_to_raw(rate);

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	if (!raw)
		return -EINVAL;

	report.arg[1] = raw;

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return 0;
}

static int
holtek8b_get_active_resolution(struct ratbag_device *device, uint8_t profile_idx)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_GET_ACTIVE_RESOLUTION, {profile_idx}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8b_get_feature_report(device, &report);
	if (rc < 0)
		return rc;

	if (report.command != HOLTEK8B_CMD_GET_ACTIVE_RESOLUTION)
		return -EBADMSG;

	return report.arg[1];
}

static int
holtek8b_set_active_resolution(struct ratbag_device *device, uint8_t profile_idx, uint8_t resolution_idx)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8B_CMD_SET_ACTIVE_RESOLUTION, {profile_idx, resolution_idx}, 0};

	assert(profile_idx < HOLTEK8B_PROFILE_COUNT);
	assert(resolution_idx <= HOLTEK8B_RESOLUTION_COUNT);

	rc = holtek8b_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return 0;
}

static inline bool
holtek8b_dpi_needs_high_bit(struct ratbag_device *device) {
	struct holtek8_data *drv_data = device->drv_data;
	return holtek8_dpi_to_raw(device, drv_data->sensor_cfg->dpi_max) > 0xff;
}

static void
holtek8b_init_profiles(struct ratbag_device *device)
{
	struct holtek8_data *drv_data = device->drv_data;
	const struct hotlek8_sensor_config *sensor_cfg = drv_data->sensor_cfg;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	unsigned int dpi_arr[HOLTEK8B_DPI_VAL_COUNT] = {0};
	unsigned int dpi_arr_len = 0;
	unsigned int dpi;
	int i;

	for (i = 0; i < HOLTEK8B_DPI_VAL_COUNT; i++) {
		dpi = sensor_cfg->dpi_min + sensor_cfg->dpi_step * i;
		if (dpi > sensor_cfg->dpi_max)
			break;
		dpi_arr[i] = dpi;
		dpi_arr_len++;
	}

	assert(drv_data->button_count > 0); // called after holtek8b_load_device_data
	ratbag_device_init_profiles(device, HOLTEK8B_PROFILE_COUNT, HOLTEK8B_RESOLUTION_COUNT, drv_data->button_count, 0);

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_set_report_rate_list(profile, holtek8b_report_rates, ARRAY_LENGTH(holtek8b_report_rates));

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list(resolution, dpi_arr, dpi_arr_len);

			ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);
			if (sensor_cfg->independent_xy)
				ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		}

		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
		}
	}
}

static int
holtek8b_download_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct holtek8b_button_config button_config;
	struct ratbag_button *button;
	int rc;
	uint8_t cfg_index;

	rc = holtek8b_read_button_config(device, &button_config, profile->index);
	if (rc < 0)
		return rc;

	ratbag_profile_for_each_button(profile, button) {
		cfg_index = button->index;

		rc = holtek8_button_from_data(button, &button_config.button[cfg_index]);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
holtek8b_download_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct holtek8_data *drv_data = device->drv_data;
	const struct hotlek8_sensor_config *sensor_cfg = drv_data->sensor_cfg;
	struct holtek8b_profile_data profile_data;
	struct ratbag_resolution *resolution;
	unsigned int active_resolution;
	int rc, dpi_x, dpi_y;
	uint16_t raw_x, raw_y;

	rc = holtek8b_read_profile_data(device, &profile_data, profile->index);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to read profile data: %s\n", strerror(-rc));
		return rc;
	}

	/*
	 * For now. More testing is needed until we support disabling profiles
	 * and no official software does it anyway so they should all be enabled.
	 */
	if (profile->index == 0 && (profile_data.enabled_profiles |
		BITMASK(HOLTEK8B_PROFILE_COUNT)) != BITMASK(HOLTEK8B_PROFILE_COUNT)) {
		log_error(device->ratbag, "Unexpected enabled profiles value: %#x\n", profile_data.enabled_profiles);
		return -EINVAL;
	}

	rc = holtek8b_download_buttons(profile);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to download buttons: %s\n", strerror(-rc));
		return rc;
	}

	rc = holtek8b_get_active_rate(device, profile->index);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to get active rate: %s\n", strerror(-rc));
		return rc;
	}
	profile->hz = rc;

	rc = holtek8b_get_active_resolution(device, profile->index);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to get active resolution: %s\n", strerror(-rc));
		return rc;
	}
	active_resolution = rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		resolution->is_active = (resolution->index + 1 == active_resolution);

		raw_x = profile_data.dpi_val_x[resolution->index];
		raw_y = profile_data.dpi_val_y[resolution->index];

		if (holtek8b_dpi_needs_high_bit(device)) {
			if (BIT_CHECK(profile_data.dpi_val_x_high_bit, resolution->index)) raw_x += 0x100;
			if (BIT_CHECK(profile_data.dpi_val_y_high_bit, resolution->index)) raw_y += 0x100;
		}

		dpi_x = (int)holtek8_raw_to_dpi(device, raw_x);
		dpi_y = (int)holtek8_raw_to_dpi(device, raw_y);

		if (sensor_cfg->independent_xy)
			ratbag_resolution_set_resolution(resolution, dpi_x, dpi_y);
		else
			ratbag_resolution_set_resolution(resolution, dpi_x, dpi_x);

		resolution->is_disabled = (resolution->index >= profile_data.resolution_count) ||
			!BIT_CHECK(profile_data.enabled_resolutions, resolution->index);
	}

	return 0;
}

static int
holtek8b_download_profiles(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	unsigned int active_profile;
	int rc;

	rc = holtek8b_get_active_profile(device);
	if (rc < 0)
		return rc;
	active_profile = rc;

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = (profile->index == active_profile);

		rc = holtek8b_download_profile(profile);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
holtek8b_upload_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct holtek8b_button_config button_config;
	struct ratbag_button *button;
	int rc;
	uint8_t cfg_index;
	bool cfg_dirty = false;

	rc = holtek8b_read_button_config(device, &button_config, profile->index);
	if (rc < 0)
		return rc;

	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;

		cfg_dirty = true;
		cfg_index = button->index;

		rc = holtek8_button_to_data(button, &button_config.button[cfg_index]);
		if (rc < 0)
			return rc;
	}

	if (!cfg_dirty)
		return 0;

	rc = holtek8b_write_button_config(device, &button_config, profile->index);
	if (rc < 0)
		return rc;

	return 0;
}

static int
holtek8b_upload_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct holtek8_data *drv_data = device->drv_data;
	const struct hotlek8_sensor_config *sensor_cfg = drv_data->sensor_cfg;
	struct holtek8b_profile_data profile_data;
	struct ratbag_resolution *resolution;
	int rc;
	unsigned int dpi_x, dpi_y;
	uint8_t active_resolution = 0;
	uint16_t raw_x, raw_y;
	bool cfg_dirty = false;
	bool resolution_dirty = false;

	rc = holtek8b_read_profile_data(device, &profile_data, profile->index);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to read profile data: %s\n", strerror(-rc));
		return rc;
	}

	rc = holtek8b_upload_buttons(profile);
	if (rc) {
		log_error(device->ratbag, "Failed to upload buttons: %s\n", strerror(-rc));
		return rc;
	}

	profile_data.resolution_count = HOLTEK8B_RESOLUTION_COUNT;
	profile_data.enabled_resolutions = BITMASK(HOLTEK8B_RESOLUTION_COUNT);

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->is_active)
			active_resolution = resolution->index + 1;

		if (resolution->is_disabled)
			BIT_CLEAR(profile_data.enabled_resolutions, resolution->index);

		if (!resolution->dirty)
			continue;
		resolution_dirty = true;

		dpi_x = resolution->dpi_x;
		dpi_y = sensor_cfg->independent_xy ? resolution->dpi_y : dpi_x;

		raw_x = holtek8_dpi_to_raw(device, dpi_x);
		raw_y = holtek8_dpi_to_raw(device, dpi_y);

		if (holtek8b_dpi_needs_high_bit(device)) {
			if (raw_x & 0x100) BIT_SET(profile_data.dpi_val_x_high_bit, resolution->index);
			else BIT_CLEAR(profile_data.dpi_val_x_high_bit, resolution->index);

			if (raw_y & 0x100) BIT_SET(profile_data.dpi_val_y_high_bit, resolution->index);
			else BIT_CLEAR(profile_data.dpi_val_y_high_bit, resolution->index);
		}

		profile_data.dpi_val_x[resolution->index] = raw_x;
		profile_data.dpi_val_y[resolution->index] = raw_y;
	}

	cfg_dirty = resolution_dirty;
	if (cfg_dirty) {
		rc = holtek8b_write_profile_data(device, &profile_data, profile->index);
		if (rc < 0) {
			log_error(device->ratbag, "Failed to write profile data: %s\n", strerror(-rc));
			return rc;
		}
	}

	if (resolution_dirty) {
		rc = holtek8b_set_active_resolution(device, profile->index, active_resolution);
		if (rc < 0) {
			log_error(device->ratbag, "Failed to set active resolution: %s\n", strerror(-rc));
			return rc;
		}
	}

	if (profile->rate_dirty) {
		rc = holtek8b_set_active_rate(device, profile->index, profile->hz);
		if (rc < 0) {
			log_error(device->ratbag, "Failed to set active rate: %s\n", strerror(-rc));
			return rc;
		}
	}

	return 0;
}

static int
holtek8b_commit(struct ratbag_device *device)
{
	struct holtek8_data *drv_data = device->drv_data;
	struct ratbag_profile *profile;
	int rc;

	drv_data->macro_index = 1;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		rc = holtek8b_upload_profile(profile);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
holtek8b_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_get_usage_page(device, 0) == 0xff00 && ratbag_hidraw_get_usage(device, 0) == 0xff00;
}

static int
holtek8b_probe(struct ratbag_device *device)
{
	int rc;
	struct holtek8_data *drv_data = NULL;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	drv_data->chunk_size = HOLTEK8B_CHUNK_SIZE;
	drv_data->api_version = HOTLEK8_API_B;

	rc = ratbag_find_hidraw(device, holtek8b_test_hidraw);
	if (rc)
		goto err;

	rc = holtek8_test_report_descriptor(device);
	if (rc)
		goto err;

	rc = holtek8_load_device_data(device);
	if (rc)
		goto err;

	rc = holtek8_test_echo(device);
	if (!rc) {
		log_error(device->ratbag, "Invalid reply\n");
		goto err;
	}

	holtek8b_init_profiles(device);

	rc = holtek8b_download_profiles(device);
	if (rc)
		goto err;

	return 0;
err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return -ENODEV;
}

static void
holtek8b_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver holtek8b_driver = {
	.name = "Holtek8 ver.B",
	.id = "holtek8b",
	.probe = holtek8b_probe,
	.remove = holtek8b_remove,
	.commit = holtek8b_commit,
	.set_active_profile = holtek8b_set_active_profile,
};
