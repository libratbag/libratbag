// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Michał Lubas
 */

#pragma once

#include "libratbag-util.h"

#define HOLTEK8_FW_VERSION_LEN 4

enum holtek8_sensor {
	HOLTEK8_SENSOR_UNKNOWN = 0,
	HOLTEK8_SENSOR_PAW3333,
	HOLTEK8_SENSOR_PMW3320,
};

struct holtek8_device_data {
	char fw_version[HOLTEK8_FW_VERSION_LEN + 1];
	char *device_name;
	enum holtek8_sensor sensor;
	int button_count;
	uint8_t password[6];

	struct list link;
};

enum holtek8_sensor
holtek8_get_sensor_from_name(const char *name);
