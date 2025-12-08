#pragma once

#include "libratbag-data.h"

struct data_hyperx {
	int profile_count;
	int button_count;
	int led_count;
	size_t nrates;
	unsigned int *rates;
	int dpi_count;
	int is_wireless;
	struct dpi_range *dpi_range;
};

const struct data_hyperx *
ratbag_device_data_hyperx_get_data(const struct ratbag_device_data *data);
