// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Michał Lubas
 */

#include "libratbag-private.h"
#include "holtek8-shared.h"

#define HOLTEK8A_BUTTON_CONFIG_SIZE 64
#define HOLTEK8A_RESOLUTION_CONFIG_SIZE 8

#define HOLTEK8A_CMD_SET_ACTIVE_RATE 0x01
#define HOLTEK8A_CMD_SET_ACTIVE_RESOLUTION 0x0b
#define HOLTEK8A_CMD_WRITE_RESOLUTION_CONFIG 0x11
#define HOLTEK8A_CMD_WRITE_BUTTON_CONFIG 0x12

#define HOLTEK8A_CMD_GET_ACTIVE_RATE 0x81
#define HOLTEK8A_CMD_GET_ACTIVE_PROFILE 0x82
#define HOLTEK8A_CMD_GET_ACTIVE_RESOLUTION 0x8b
#define HOLTEK8A_CMD_READ_RESOLUTION_CONFIG 0x91
#define HOLTEK8A_CMD_READ_BUTTON_CONFIG 0x92

#define HOLTEK8A_PROFILE_COUNT 1
#define HOLTEK8A_RESOLUTION_COUNT 8
#define HOLTEK8A_DPI_VAL_COUNT 128

#define HOLTEK8A_CHUNK_SIZE 32

const unsigned int holtek8a_report_rates[] = {
	125, 250, 500, 1000
};

struct holtek8a_button_config {
	struct holtek8_button_data button[16];
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8a_button_config) == HOLTEK8A_BUTTON_CONFIG_SIZE, "Invalid size");

struct holtek8a_resolution_config {
	uint8_t dpi_val[8];
} __attribute__((packed));
_Static_assert(sizeof(struct holtek8a_resolution_config) == HOLTEK8A_RESOLUTION_CONFIG_SIZE, "Invalid size");

/*
 * Encrypts a feature report using device's obfuscation algorithm
 * if the password is set.
 */
static void
holtek8a_encrypt(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	const uint8_t MAGIC_ADD[8] = { 0x25, 0xf6, 0xe4, 0x76, 0x47, 0x54, 0xe6, 0x76 };
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t *password = drv_data->api_a.password;
	uint8_t *data = ((uint8_t*) report) + 1;
	uint8_t result[8];
	int i;

	if (!*password)
		return;

	swap_u8(&data[6], &data[3]);
	swap_u8(&data[7], &data[2]);
	swap_u8(&data[4], &data[1]);
	swap_u8(&data[5], &data[0]);

	for(i = 0; i<6; i++) {
		data[i+1] ^= password[i];
	}

	for(i = 0; i<8; i++) {
		result[i] = (data[i] << 3) | (data[(i+1)%8] >> 5);
	}

	for(i = 0; i<8; i++) {
		result[i] += MAGIC_ADD[i];
	}

	for(i = 0; i<8; i++) {
		data[i] = result[i];
	}
}

/*
 * Decrypts a feature report using device's obfuscation algorithm
 * if the password is set.
 */
static void
holtek8a_decrypt(struct ratbag_device *device, struct holtek8_feature_report *report) {
	const uint8_t MAGIC_ADD[8] = { 0x25, 0xf6, 0xe4, 0x76, 0x47, 0x54, 0xe6, 0x76 };
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t *password = drv_data->api_a.password;
	uint8_t *data = ((uint8_t*) report) + 1;
	uint8_t result[8];
	int i;

	if (!*password)
		return;

	for(i = 0; i<8; i++) {
		data[i] -= MAGIC_ADD[i];
	}

	for(i = 0; i<8; i++) {
		result[i] = (data[i] >> 3) | (data[(i-1)%8] << 5);
	}

	for(i = 0; i<6; i++) {
		result[i+1] ^= password[i];
	}

	for(i = 0; i<8; i++) {
		data[i] = result[i];
	}

	swap_u8(&data[6], &data[3]);
	swap_u8(&data[7], &data[2]);
	swap_u8(&data[4], &data[1]);
	swap_u8(&data[5], &data[0]);
}

int
holtek8a_get_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	int rc;
	rc = ratbag_hidraw_get_feature_report(device, 0, (uint8_t*) report, HOLTEK8_FEATURE_REPORT_SIZE);
	holtek8a_decrypt(device, report);
	return rc;
}

int
holtek8a_set_feature_report(struct ratbag_device *device, struct holtek8_feature_report *report)
{
	struct holtek8_feature_report report_send = *report;

	holtek8_calculate_checksum(&report_send);
	holtek8a_encrypt(device, &report_send);
	return ratbag_hidraw_set_feature_report(device, 0, (uint8_t*) &report_send, HOLTEK8_FEATURE_REPORT_SIZE);
}

static int
holtek8a_read_resolution_config(struct ratbag_device *device,
			     struct holtek8a_resolution_config *resolution_config)
{
	int rc;
	uint8_t *data = (uint8_t*) resolution_config;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_READ_RESOLUTION_CONFIG, {0}, 0};
	struct holtek8_feature_report result;

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8_read_padded(device, data, HOLTEK8A_RESOLUTION_CONFIG_SIZE, &result);
	if (rc < 0)
		return rc;

	return result.arg[0];
}

static int
holtek8a_write_resolution_config(struct ratbag_device *device,
			     const struct holtek8a_resolution_config *resolution_config, uint8_t resolution_count)
{
	int rc;
	const uint8_t *data = (const uint8_t*) resolution_config;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_WRITE_RESOLUTION_CONFIG, {resolution_count, HOLTEK8A_RESOLUTION_CONFIG_SIZE}, 0};

	assert(resolution_count >= 1 && resolution_count <= HOLTEK8A_RESOLUTION_COUNT);
	_Static_assert(HOLTEK8A_RESOLUTION_CONFIG_SIZE <= 8, "Config size safety check failed");

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_write_padded(device, data, HOLTEK8A_RESOLUTION_CONFIG_SIZE);
}

static int
holtek8a_read_button_config(struct ratbag_device *device,
			     struct holtek8a_button_config *button_config)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t *data = (uint8_t*) button_config;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_READ_BUTTON_CONFIG, {drv_data->api_a.active_profile}, 0};
	int rc;

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_read_chunked(device, data, HOLTEK8A_BUTTON_CONFIG_SIZE, NULL);
}

static int
holtek8a_write_button_config(struct ratbag_device *device,
			     struct holtek8a_button_config *button_config)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t *data = (uint8_t*) button_config;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_WRITE_BUTTON_CONFIG, {drv_data->api_a.active_profile, HOLTEK8A_BUTTON_CONFIG_SIZE}, 0};
	int rc;

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return holtek8_write_chunked(device, data, HOLTEK8A_BUTTON_CONFIG_SIZE);
}

static int
holtek8a_get_active_profile(struct ratbag_device *device)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_GET_ACTIVE_PROFILE, {0}, 0};

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8a_get_feature_report(device, &report);
	if (rc < 0)
		return rc;

	if (report.command != HOLTEK8A_CMD_GET_ACTIVE_PROFILE)
		return -EBADMSG;

	return report.arg[0];
}

static int
holtek8a_get_active_rate(struct ratbag_device *device)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_GET_ACTIVE_RATE, {0}, 0};

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8a_get_feature_report(device, &report);
	if (rc < 0)
		return rc;

	if (report.command != HOLTEK8A_CMD_GET_ACTIVE_RATE)
		return -EBADMSG;

	rc = (int)holtek8_raw_to_report_rate(report.arg[0]);
	if (rc == 0)
		return -EINVAL;

	return rc;
}

static int
holtek8a_set_active_rate(struct ratbag_device *device, unsigned int rate)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_SET_ACTIVE_RATE, {0}, 0};
	uint8_t raw = holtek8_report_rate_to_raw(rate);

	if (!raw)
		return -EINVAL;

	report.arg[0] = raw;
	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return 0;
}

static int
holtek8a_get_active_resolution(struct ratbag_device *device)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_GET_ACTIVE_RESOLUTION, {0}, 0};

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8a_get_feature_report(device, &report);
	if (rc < 0)
		return rc;

	if (report.command != HOLTEK8A_CMD_GET_ACTIVE_RESOLUTION)
		return -EBADMSG;

	return report.arg[0];
}

static int
holtek8a_set_active_resolution(struct ratbag_device *device, uint8_t resolution_idx)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8A_CMD_SET_ACTIVE_RESOLUTION, {resolution_idx}, 0};

	assert(resolution_idx >= 1 && resolution_idx <= HOLTEK8A_RESOLUTION_COUNT);

	rc = holtek8a_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	return 0;
}

static int
holtek8a_update_active_profile(struct ratbag_device *device)
{
	struct holtek8_data *drv_data = device->drv_data;
	int rc;

	rc = holtek8a_get_active_profile(device);
	if (rc < 0)
		return rc;

	drv_data->api_a.active_profile = rc;
	return 0;
}

static void
holtek8a_init_profiles(struct ratbag_device *device)
{
	struct holtek8_data *drv_data = device->drv_data;
	const struct holtek8_sensor_config *sensor_cfg = drv_data->sensor_cfg;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	unsigned int dpi_arr[HOLTEK8A_DPI_VAL_COUNT] = {0};
	unsigned int dpi_arr_len = 0;
	unsigned int dpi;
	int i;

	for (i = 0; i < HOLTEK8A_DPI_VAL_COUNT; i++) {
		dpi = sensor_cfg->dpi_min + sensor_cfg->dpi_step * i;
		if (dpi > sensor_cfg->dpi_max)
			break;
		dpi_arr[i] = dpi;
		dpi_arr_len++;
	}

	assert(drv_data->button_count > 0); // called after holtek8a_load_device_data
	ratbag_device_init_profiles(device, HOLTEK8A_PROFILE_COUNT, HOLTEK8A_RESOLUTION_COUNT, drv_data->button_count, 0);

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_set_report_rate_list(profile, holtek8a_report_rates, ARRAY_LENGTH(holtek8a_report_rates));

		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list(resolution, dpi_arr, dpi_arr_len);
			ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);
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
holtek8a_download_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct holtek8a_button_config button_config;
	struct ratbag_button *button;
	int rc;
	uint8_t cfg_index;

	rc = holtek8a_read_button_config(device, &button_config);
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
holtek8a_download_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_resolution *resolution;
	struct holtek8a_resolution_config resolution_config;
	unsigned int active_resolution, resolution_count;
	int rc, dpi;
	uint16_t raw;

	rc = holtek8a_read_resolution_config(device, &resolution_config);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to read resolutions: %s\n", strerror(-rc));
		return rc;
	}
	resolution_count = rc;

	rc = holtek8a_download_buttons(profile);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to download buttons: %s\n", strerror(-rc));
		return rc;
	}

	rc = holtek8a_get_active_rate(device);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to get active rate: %s\n", strerror(-rc));
		return rc;
	}
	profile->hz = rc;

	rc = holtek8a_get_active_resolution(device);
	if (rc < 0) {
		log_error(device->ratbag, "Failed to get active resolution: %s\n", strerror(-rc));
		return rc;
	}
	active_resolution = rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		resolution->is_active = (resolution->index + 1 == active_resolution);

		raw = resolution_config.dpi_val[resolution->index] & 0x7f;

		dpi = (int)holtek8_raw_to_dpi(device, raw);

		ratbag_resolution_set_resolution(resolution, dpi, dpi);

		resolution->is_disabled = (resolution->index >= resolution_count);
	}

	return 0;
}

static int
holtek8a_download_profiles(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc;

	rc = holtek8a_update_active_profile(device);
	if (rc < 0)
		return rc;

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		rc = holtek8a_download_profile(profile);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
holtek8a_upload_buttons(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct holtek8a_button_config button_config = {0};
	struct ratbag_button *button;
	int rc;
	uint8_t cfg_index;
	bool cfg_dirty = false;

	rc = holtek8a_read_button_config(device, &button_config);
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

	rc = holtek8a_write_button_config(device, &button_config);
	if (rc < 0)
		return rc;

	return 0;
}

static int
holtek8a_upload_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_resolution *resolution;
	struct holtek8a_resolution_config resolution_config = {0};
	unsigned int active_resolution = 0;
	unsigned int dpi;
	unsigned int resolution_count = 0;
	unsigned int i = 0;
	int rc;
	uint16_t raw;
	bool resolution_dirty = false;

	rc = holtek8a_upload_buttons(profile);
	if (rc) {
		log_error(device->ratbag, "Failed to upload buttons: %s\n", strerror(-rc));
		return rc;
	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->dirty)
			resolution_dirty = true;

		if (resolution->is_disabled)
			continue;
		resolution_count += 1;

		if (resolution->is_active)
			active_resolution = resolution_count;

		dpi = resolution->dpi_x;
		raw = holtek8_dpi_to_raw(device, dpi);

		resolution_config.dpi_val[resolution_count - 1] = raw;
	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->is_disabled)
			continue;

		dpi = resolution->dpi_x;
		raw = holtek8_dpi_to_raw(device, dpi);

		resolution_config.dpi_val[resolution_count + i++] = raw;
	}

	if (resolution_dirty) {
		rc = holtek8a_write_resolution_config(device, &resolution_config, resolution_count);
		if (rc < 0) {
			log_error(device->ratbag, "Failed to write resolution config: %s\n", strerror(-rc));
			return rc;
		}

		rc = holtek8a_set_active_resolution(device, active_resolution);
		if (rc < 0) {
			log_error(device->ratbag, "Failed to set active resolution: %s\n", strerror(-rc));
			return rc;
		}
	}

	if (profile->rate_dirty) {
		rc = holtek8a_set_active_rate(device, profile->hz);
		if (rc < 0) {
			log_error(device->ratbag, "Failed to set active rate: %s\n", strerror(-rc));
			return rc;
		}
	}

	return 0;
}

static int
holtek8a_commit(struct ratbag_device *device)
{
	struct holtek8_data *drv_data = device->drv_data;
	struct ratbag_profile *profile;
	int rc;

	drv_data->macro_index = 1;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		rc = holtek8a_upload_profile(profile);
		if (rc < 0)
			return rc;
	}

	return 0;
}

static int
holtek8a_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_get_usage_page(device, 0) == 0xff00 && ratbag_hidraw_get_usage(device, 0) == 0xff00;
}

static int
holtek8a_probe(struct ratbag_device *device)
{
	int rc;
	struct holtek8_data *drv_data = NULL;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	drv_data->api_version = HOLTEK8_API_A;
	drv_data->chunk_size = HOLTEK8A_CHUNK_SIZE;

	rc = ratbag_find_hidraw(device, holtek8a_test_hidraw);
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

	holtek8a_init_profiles(device);

	rc = holtek8a_download_profiles(device);
	if (rc)
		goto err;

	return 0;
err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return -ENODEV;
}

static void
holtek8a_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver holtek8a_driver = {
	.name = "Holtek8 ver.A",
	.id = "holtek8a",
	.probe = holtek8a_probe,
	.remove = holtek8a_remove,
	.commit = holtek8a_commit,
};
