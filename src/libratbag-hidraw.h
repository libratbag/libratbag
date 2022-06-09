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

#pragma once

#include <linux/hid.h>
#include <stdint.h>

#include "libratbag.h"

/* defined in the internal hid API in the kernel */
#define HID_INPUT_REPORT	0
#define HID_OUTPUT_REPORT	1
#define HID_FEATURE_REPORT	2
#define MAX_HIDRAW 2

struct ratbag_hid_report {
	unsigned int report_id;
	unsigned int usage_page;
	unsigned int usage;
};

struct ratbag_hidraw {
	int fd;
	struct ratbag_hid_report *reports;
	unsigned num_reports;
	char *sysname;
};

typedef bool (*ratbagd_hidraw_filter_t)(uint8_t *buf, size_t len);

/**
 * Open the hidraw device associated with the device.
 *
 * @param device the ratbag device
 *
 * @return 0 on success or a negative errno on error
 */
int ratbag_open_hidraw(struct ratbag_device *device);

/**
 * Open a hidraw device associated with the device by its enumeration order to
 * a specific internal hidraw index.
 *
 * @param device the ratbag device
 * @param enumeration index of the endpoint
 * @param internal index of hidraw array
 *
 * @return 0 on success or a negative errno on error
 */
int ratbag_open_hidraw_index(struct ratbag_device *device, int endpoint_index, int hidraw_index);

/**
 * Find and open the hidraw device associated with the device by using the
 * given matching function.
 *
 * @param device the ratbag device
 * @param match the matching test (return 1 if matched, 0 if not)
 *
 * @return 0 on success or a negative errno on error
 */
int
ratbag_find_hidraw(struct ratbag_device *device,
		   int (*match)(struct ratbag_device *device));

/**
 * Close the hidraw device associated with the device.
 *
 * @param device the ratbag device
 */
void ratbag_close_hidraw(struct ratbag_device *device);

/**
 * Close a hidraw device associated with the device by the internal index.
 *
 * @param device the ratbag device
 * @param internal index of hidraw array
 */
void ratbag_close_hidraw_index(struct ratbag_device *device, int idx);

/**
 * Send report request to device
 *
 * @param device the ratbag device
 * @param reportnum report ID
 * @param buf[in/out] data to transfer
 * @param len length of buf
 * @param rtype HID report type
 * @param reqtype HID_REQ_GET_REPORT or HID_REQ_SET_REPORT
 *
 * @return count of data transferred, or a negative errno on error
 *
 * Same behavior as hid_hw_request, but with raw buffers instead.
 */
int ratbag_hidraw_raw_request(struct ratbag_device *device, unsigned char reportnum,
			      uint8_t *buf, size_t len, unsigned char rtype,
			      int reqtype);

/**
 * Get feature report from device.
 *
 * This call sends a HID_REQ_GET_REPORT to the device for the given feature
 * and returns the data in the caller-allocated buffer buf of size len.
 *
 * @param device the ratbag device
 * @param reportnum report ID
 * @param buf[out] data returned from device
 * @param len length of buf
 *
 * @return count of data transferred, or a negative errno on error
 *
 * Convenience wrapper around ratbag_hidraw_raw_request()
 */
int ratbag_hidraw_get_feature_report(struct ratbag_device *device, unsigned char reportnum,
				     uint8_t *buf, size_t len);

/**
 * Set feature report on device.
 *
 * This call sends a HID_REQ_SET_REPORT to the device for the given feature
 * and the buffer buf of size len.
 *
 * @param device the ratbag device
 * @param reportnum report ID
 * @param buf[in] data returned from device
 * @param len length of buf
 *
 * @return count of data transferred, or a negative errno on error
 *
 * Convenience wrapper around ratbag_hidraw_raw_request()
 */
int ratbag_hidraw_set_feature_report(struct ratbag_device *device, unsigned char reportnum,
				     uint8_t *buf, size_t len);
/**
 * Send output report to device
 *
 * @param device the ratbag device
 * @param buf raw data to transfer
 * @param len length of buf
 *
 * @return count of data transferred, or a negative errno on error
 */
int ratbag_hidraw_output_report(struct ratbag_device *device, uint8_t *buf, size_t len);

/**
 * Read an input report from the device
 * Optional filter function can be provided, when the filter returns false the packet is ignored
 *
 * @param device the ratbag device
 * @param[out] buf resulting raw data
 * @param len length of buf
 * @param filter filter function
 *
 * @return count of data transferred, or a negative errno on error
 */
int ratbag_hidraw_read_input_report(const struct ratbag_device *device, uint8_t *buf, size_t len,
				 ratbagd_hidraw_filter_t filter);

/**
 * Read an input report from the device from a specific hidraw index
 * Optional filter function can be provided, when the filter returns false the packet is ignored
 *
 * @param device the ratbag device
 * @param[out] buf resulting raw data
 * @param len length of buf
 * @param hidrawno index of hidraw array
 * @param filter filter function
 *
 * @return count of data transferred, or a negative errno on error
 */
int ratbag_hidraw_read_input_report_index(const struct ratbag_device *device, uint8_t *buf, size_t len, int hidrawno,
				 ratbagd_hidraw_filter_t filter);

/**
 * Tells if a given device has the specified report ID.
 *
 * @param device the ratbag device which hidraw node is opened
 * @param report_id the report ID we inquire about
 *
 * @return 1 if the device has the given report id, 0 otherwise
 */
int
ratbag_hidraw_has_report(const struct ratbag_device *device, unsigned int report_id);

/**
 * Gives the usage page of a report with the specified report ID.
 *
 * @param device the ratbag device which hidraw node is opened
 * @param report_id the report ID we inquire about
 *
 * @return the usage page of the report if the device has the given report id,
 * 0 otherwise
 */
unsigned int
ratbag_hidraw_get_usage_page(const struct ratbag_device *device, unsigned int report_id);

/**
 * Gives the usage of a report with the specified report ID.
 *
 * @param device the ratbag device which hidraw node is opened
 * @param report_id the report ID we inquire about
 *
 * @return the usage of the report if the device has the given report id,
 * 0 otherwise
 */
unsigned int
ratbag_hidraw_get_usage(const struct ratbag_device *device, unsigned int report_id);

/**
 * Tells if a given device has a vendor usage page
 *
 * @param device the ratbag device which hidraw node is opened
 *
 * @return 1 if the device has a vendor usage page, 0 otherwise
 */
unsigned int
ratbag_hidraw_has_vendor_page(const struct ratbag_device *device);

/**
 * Gives the input key code associated to the keyboard HID usage.
 *
 * @return the key code of the HID usage or 0.
 */
unsigned int
ratbag_hidraw_get_keycode_from_keyboard_usage(const struct ratbag_device *device,
					      uint8_t hid_code);

/**
 * Gives the HID keyboard usage associated to the input keycode.
 *
 * @return the HID keyboard usage or 0.
 */
uint8_t
ratbag_hidraw_get_keyboard_usage_from_keycode(const struct ratbag_device *device,
					      unsigned keycode);

/**
 * Gives the input key code associated to the Consumer Control HID usage.
 *
 * @return the key code of the HID usage or 0.
 */
unsigned int
ratbag_hidraw_get_keycode_from_consumer_usage(const struct ratbag_device *device,
					      uint16_t hid_code);

/**
 * Gives the HID Consumer Control usage associated to the input keycode.
 *
 * @return the HID Consumer Control usage or 0.
 */
uint16_t
ratbag_hidraw_get_consumer_usage_from_keycode(const struct ratbag_device *device,
					      unsigned keycode);
