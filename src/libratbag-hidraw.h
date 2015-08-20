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

#ifndef LIBRATBAG_HIDRAW_H
#define LIBRATBAG_HIDRAW_H

#include <linux/hid.h>
#include <stdint.h>

#include "libratbag.h"

/* defined in the internal hid API in the kernel */
#define HID_INPUT_REPORT	0
#define HID_OUTPUT_REPORT	1
#define HID_FEATURE_REPORT	2

/**
 * Open the hidraw device associated with the device.
 *
 * @param ratbag the ratbag device
 *
 * @return 0 on success or a negative errno on error
 */
int ratbag_open_hidraw(struct ratbag_device *ratbag);

/**
 * Send report request to device
 *
 * @param ratbag ratbag device
 * @param reportnum report ID
 * @param buf in/out data to transfer
 * @param len length of buf
 * @param rtype HID report type
 * @param reqtype HID_REQ_GET_REPORT or HID_REQ_SET_REPORT
 *
 * @return count of data transfered, or a negative errno on error
 *
 * Same behavior as hid_hw_request, but with raw buffers instead.
 */
int ratbag_hidraw_raw_request(struct ratbag_device *ratbag, unsigned char reportnum,
			      uint8_t *buf, size_t len, unsigned char rtype,
			      int reqtype);

/**
 * Send output report to device
 *
 * @param ratbag the ratbag device
 * @param buf raw data to transfer
 * @param len length of buf
 *
 * @return count of data transfered, or a negative errno on error
 */
int ratbag_hidraw_output_report(struct ratbag_device *ratbag, uint8_t *buf, size_t len);

/**
 * Read an input report from the device
 *
 * @param ratbag the ratbag device
 * @param[out] buf resulting raw data
 * @param len length of buf
 *
 * @return count of data transfered, or a negative errno on error
 */
int ratbag_hidraw_read_input_report(struct ratbag_device *ratbag, uint8_t *buf, size_t len);

#endif /* LIBRATBAG_HIDRAW_H */
