/*
 * Copyright © 2021 Filipe Laíns
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

#include "libratbag-private.h"


/* reports */
#define OI_REPORT_SHORT		0x20
#define OI_REPORT_LONG		0x21
#define OI_REPORT_SHORT_SIZE	8
#define OI_REPORT_LONG_SIZE	32

#define OI_REPORT_MAX_SIZE		OI_REPORT_LONG_SIZE
#define OI_REPORT_DATA_INDEX		3
#define OI_REPORT_DATA_MAX_SIZE		OI_REPORT_LONG_SIZE - OI_REPORT_DATA_INDEX

/* protocol function pages */
#define OI_PAGE_INFO			0x00
#define OI_PAGE_GIMMICKS		0xFD
#define OI_PAGE_DEBUG			0xFE
#define OI_PAGE_ERROR			0xFF

/* info page (0x00) functions */
#define OI_FUNCTION_VERSION			0x00
#define OI_FUNCTION_FW_INFO			0x01
#define OI_FUNCTION_SUPPORTED_FUNCTION_PAGES	0x02
#define OI_FUNCTION_SUPPORTED_FUNCTIONS		0x03

/* error page (0xFF) */
#define OI_ERROR_INVALID_VALUE		0x01
#define OI_ERROR_UNSUPPORTED_FUNCTION	0x02
#define OI_ERROR_CUSTOM			0xFE


static unsigned int report_rates[] = { 125, 250, 500, 750, 1000 };

struct openinput_drv_data {
	unsigned int num_profiles;
	unsigned int num_resolutions;
	unsigned int num_buttons;
	unsigned int num_leds;
	unsigned int fw_major;
	unsigned int fw_minor;
	unsigned int fw_patch;
	uint64_t supported;
};

struct oi_report_t {
	uint8_t id;
	uint8_t function_page;
	uint8_t function;
	uint8_t data[29];
} __attribute__((__packed__));


#define CASE_RETURN_STRING(a) case a: return #a; break
static const char*
openinput_function_page_get_name(uint8_t page)
{
	static char numeric[16];
	char *str;

	switch(page) {
	CASE_RETURN_STRING(OI_PAGE_INFO);
	CASE_RETURN_STRING(OI_PAGE_GIMMICKS);
	CASE_RETURN_STRING(OI_PAGE_DEBUG);
	CASE_RETURN_STRING(OI_PAGE_ERROR);
	default:
		sprintf_safe(numeric, "0x%02x", page);
		str = numeric;
		break;
	}

	return str;
}

static const char*
openinput_function_get_name(uint8_t page, uint8_t function)
{
	static char numeric[32];
	char *str = NULL;

	switch(page) {
	case OI_PAGE_INFO:
		switch (function) {
		CASE_RETURN_STRING(OI_FUNCTION_VERSION);
		CASE_RETURN_STRING(OI_FUNCTION_FW_INFO);
		CASE_RETURN_STRING(OI_FUNCTION_SUPPORTED_FUNCTION_PAGES);
		CASE_RETURN_STRING(OI_FUNCTION_SUPPORTED_FUNCTIONS);
		}
	}

	if (!str) {
		sprintf_safe(numeric, "0x%02x 0x%02x", page, function);
		str = numeric;
	}

	return str;
}
#undef CASE_RETURN_STRING

static const char*
openinput_get_error_string(struct oi_report_t *report)
{
	char help_str[OI_REPORT_LONG_SIZE - OI_REPORT_DATA_INDEX + 1] = {0};
	char *str;

	switch (report->function) {
	case OI_ERROR_INVALID_VALUE:
		xasprintf(&str, "Invalid value (in position %u)", report->data[2]);
		break;
	case OI_ERROR_CUSTOM:
		memcpy(help_str, report->data, sizeof(help_str)-1); /* copy error string */
		xasprintf(&str, "Custom error (%s)", help_str);
		break;
	case OI_ERROR_UNSUPPORTED_FUNCTION:
		xasprintf(&str, "Unsupported function (0x%02x, 0x%02x)", report->data[0], report->data[1]);
		break;
	default:
		xasprintf(&str, "Unknown error (%u)", report->function);
		break;
	}

	return str;
}

static size_t
openinput_get_report_size(unsigned int report)
{
	switch (report) {
	case OI_REPORT_SHORT:
		return OI_REPORT_SHORT_SIZE;
	case OI_REPORT_LONG:
		return OI_REPORT_LONG_SIZE;
	default:
		return 0;
	}
}

static bool
openinput_report_filter(uint8_t *buf, size_t len)
{
	if (len < 1)
		return false;

	switch(buf[0]) {
	case OI_REPORT_SHORT:
		return len == OI_REPORT_SHORT_SIZE;
	case OI_REPORT_LONG:
		return len == OI_REPORT_LONG_SIZE;
	default:
		return false;
	}
}

static int
openinput_send_report(struct ratbag_device *device, struct oi_report_t *report)
{
	int ret;
	uint8_t buffer[OI_REPORT_MAX_SIZE];
	size_t size = openinput_get_report_size(report->id);

	memcpy(buffer, report, size);

	ret = ratbag_hidraw_output_report(device, buffer, size);
	if (ret < 0) {
		log_error(device->ratbag, "openinput: failed to send data to device (%s)\n",
			  strerror(-ret));
		return ret;
	}

	ret = ratbag_hidraw_read_input_report(device, buffer, OI_REPORT_MAX_SIZE, openinput_report_filter);
	if (ret < 0) {
		log_error(device->ratbag, "openinput: failed to read data from device (%s)\n",
			  strerror(-ret));
		return ret;
	}

	memcpy(report, buffer, openinput_get_report_size(buffer[0]));

	/* check for error */
	if (report->function_page == OI_PAGE_ERROR) {
		log_error(device->ratbag, "openinput: %s\n", openinput_get_error_string(report));
		return report->function;
	}

	return 0;
}

static int
openinput_info_version(struct ratbag_device *device)
{
	int ret;
	struct openinput_drv_data *drv_data = ratbag_get_drv_data(device);
	struct oi_report_t report = {
		.id = OI_REPORT_SHORT,
		.function_page = OI_PAGE_INFO,
		.function = OI_FUNCTION_VERSION
	};

	ret = openinput_send_report(device, &report);
	if (ret)
		return ret;

	drv_data->fw_major = report.data[0];
	drv_data->fw_minor = report.data[1];
	drv_data->fw_patch = report.data[2];

	log_info(device->ratbag, "openinput: protocol version %u.%u.%u\n",
		 drv_data->fw_major, drv_data->fw_minor, drv_data->fw_patch);

	return 0;
}

#define OI_FUNCTION_FW_INFO_VENDOR		0x00
#define OI_FUNCTION_FW_INFO_VERSION		0x01
#define OI_FUNCTION_FW_INFO_DEVICE_NAME		0x02

static int
openinput_info_fw_info(struct ratbag_device *device,
				  uint8_t field_id,
				  unsigned char *description,
				  size_t description_size)
{
	int ret;
	struct oi_report_t report = {
		.id = OI_REPORT_SHORT,
		.function_page = OI_PAGE_INFO,
		.function = OI_FUNCTION_FW_INFO,
		.data = {field_id}
	};

	ret = openinput_send_report(device, &report);
	if (ret)
		return ret;

	memcpy(description, report.data, min(sizeof(report.data), description_size));

	return 0;
}


static int
openinput_info_supported_function_pages(struct ratbag_device *device,
						   uint8_t start_index,
						   uint8_t *pages_count,
						   uint8_t *pages_left,
						   uint8_t *pages,
						   size_t pages_size)
{
	int ret;
	struct oi_report_t report = {
		.id = OI_REPORT_SHORT,
		.function_page = OI_PAGE_INFO,
		.function = OI_FUNCTION_SUPPORTED_FUNCTION_PAGES,
		.data = {start_index}
	};

	ret = openinput_send_report(device, &report);
	if (ret)
		return ret;

	*pages_count = report.data[0];
	*pages_left = report.data[1];
	memcpy(pages, report.data + 2, min(sizeof(report.data), pages_size));

	return 0;
}

static int
openinput_info_supported_functions(struct ratbag_device *device,
					      uint8_t function_page,
					      uint8_t start_index,
					      uint8_t *functions_count,
					      uint8_t *functions_left,
					      uint8_t *functions,
					      size_t functions_size)
{
	int ret;
	struct oi_report_t report = {
		.id = OI_REPORT_SHORT,
		.function_page = OI_PAGE_INFO,
		.function = OI_FUNCTION_SUPPORTED_FUNCTIONS,
		.data = {function_page, start_index}
	};

	ret = openinput_send_report(device, &report);
	if (ret)
		return ret;

	*functions_count = report.data[0];
	*functions_left = report.data[1];
	memcpy(functions, report.data + 2, min(sizeof(report.data), functions_size));

	return 0;
}

static int
openinput_read_supported_functions(struct ratbag_device *device, uint8_t page)
{
	struct ratbag *ratbag = device->ratbag;
	int ret;
	uint8_t i, total, read = 0, count = 0, left = 0;
	uint8_t buffer[OI_REPORT_DATA_MAX_SIZE];

	ret = openinput_info_supported_functions(device,
						 page,
						 read,
						 &count, &left,
						 buffer, sizeof(buffer));
	if (ret)
		return ret;

	total = count + left;
	uint8_t functions[total];

	memcpy(functions, buffer, count);

	/* there are still functions left to read! */
	while (left) {
		ret = openinput_info_supported_functions(device,
							 page,
							 read,
							 &count, &left,
							 buffer, sizeof(buffer));
		if (ret)
			return ret;

		/* make sure the new size values make sense, to avoid deadlocks */
		if (total != (read + count + left)) {
			log_error(ratbag, "openinput: invalid number of functions left to read (%u)\n", left);
			return -EINVAL;
		}
		log_debug(ratbag, "openinput: read %u functions, %u left\n", count, left);

		memcpy(functions + read, buffer, count);
		read += count;
	}

	/* iterate over read functions */
	for (i = 0; i < total; i++) {
		log_debug(ratbag, "openinput: found function %s\n", openinput_function_get_name(page, functions[i]));
		/* TODO: set bits in drv_data->supported when we implement support for certain capabilities */
	}

	return 0;
}

static int
openinput_read_supported_function_pages(struct ratbag_device *device)
{
	struct ratbag *ratbag = device->ratbag;
	int ret;
	uint8_t i, total, read = 0, count = 0, left = 0;
	uint8_t buffer[OI_REPORT_DATA_MAX_SIZE];

	log_debug(ratbag, "openinput: starting reading device functions...\n");

	ret = openinput_info_supported_function_pages(device,
						      read,
						      &count, &left,
						      buffer, sizeof(buffer));
	if (ret)
		return ret;

	total = count + left;
	if (total == 0) {
		log_debug(ratbag,
			  "openinput: not proceeding to read device functions as there are 0 pages\n");
		return 0;
	}
	uint8_t pages[total];

	memcpy(pages, buffer, count);

	/* there are still function pages left to read! */
	while (left) {
		ret = openinput_info_supported_function_pages(device,
							      read,
							      &count, &left,
							      buffer, sizeof(buffer));
		if (ret)
			return ret;

		/* make sure the new size values make sense, to avoid deadlocks */
		if (total != (read + count + left)) {
			log_error(ratbag, "openinput: invalid number of function pages left to read (%u)\n", left);
			return -EINVAL;
		}
		log_debug(ratbag, "openinput: read %u pages, %u left\n", count, left);

		memcpy(pages + read, buffer, count);
		read += count;
	}

	/* iterate over read function pages, and read their functions */
	for (i = 0; i < total; i++) {
		log_debug(ratbag, "openinput: found function page %s\n", openinput_function_page_get_name(pages[i]));
		openinput_read_supported_functions(device, pages[i]);
	}

	return 0;
}

static void
openinput_read_profile(struct ratbag_profile *profile)
{
	ratbag_profile_set_report_rate_list(profile, report_rates, sizeof(*report_rates));
	profile->is_active = true;
}

static int
openinput_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, OI_REPORT_SHORT);
}

static int
openinput_probe(struct ratbag_device *device)
{
	int ret;
	struct openinput_drv_data *drv_data;
	struct ratbag_profile *profile;
	unsigned char str[OI_REPORT_DATA_MAX_SIZE];

	ret = ratbag_find_hidraw(device, openinput_test_hidraw);
	if (ret)
		return ret;

	drv_data = zalloc(sizeof(*drv_data));

	drv_data->num_profiles = 1;

	ratbag_set_drv_data(device, drv_data);

	openinput_info_version(device);

	ret = openinput_info_fw_info(device, OI_FUNCTION_FW_INFO_VENDOR, str, sizeof(str));
	if (ret)
		return ret;
	log_info(device->ratbag, "openinput: firmware vendor: %s\n", str);

	ret = openinput_info_fw_info(device, OI_FUNCTION_FW_INFO_VERSION, str, sizeof(str));
	if (ret)
		return ret;
	log_info(device->ratbag, "openinput: firmware version: %s\n", str);

	ret = openinput_info_fw_info(device, OI_FUNCTION_FW_INFO_DEVICE_NAME, str, sizeof(str));
	if (ret)
		return ret;
	log_info(device->ratbag, "openinput: device: %s\n", str);

	ret = openinput_read_supported_function_pages(device);
	if (ret)
		return ret;

	ratbag_device_init_profiles(device,
				    drv_data->num_profiles,
				    drv_data->num_resolutions,
				    drv_data->num_buttons,
				    drv_data->num_leds);

	ratbag_device_for_each_profile(device, profile)
		openinput_read_profile(profile);

	return 0;
}

static void
openinput_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver openinput_driver = {
	.name = "openinput",
	.id = "openinput",
	.probe = openinput_probe,
	.remove = openinput_remove,
};
