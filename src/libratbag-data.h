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

#pragma once

struct ratbag_device_data;

struct ratbag_device_data *
ratbag_device_data_new_for_id(struct ratbag *ratbag, const struct input_id *id);


struct ratbag_device_data *
ratbag_device_data_unref(struct ratbag_device_data *data);

struct ratbag_device_data *
ratbag_device_data_ref(struct ratbag_device_data *data);


const char *
ratbag_device_data_get_driver(const struct ratbag_device_data *data);
const char *
ratbag_device_data_get_name(const struct ratbag_device_data *data);
enum ratbag_led_type
ratbag_device_data_get_led_type(const struct ratbag_device_data *data,
				unsigned int index);

/* HID++ 1.0 */
/**
 * @return The device index or -1 if not set
 */
int
ratbag_device_data_hidpp10_get_index(const struct ratbag_device_data *data);

const char *
ratbag_device_data_hidpp10_get_profile_type(const struct ratbag_device_data *data);

/**
 * @return The profile count index or -1 if not set
 */
int
ratbag_device_data_hidpp10_get_profile_count(const struct ratbag_device_data *data);

struct dpi_list *
ratbag_device_data_hidpp10_get_dpi_list(const struct ratbag_device_data *data);

struct dpi_range *
ratbag_device_data_hidpp10_get_dpi_range(const struct ratbag_device_data *data);

/**
 * @return The led count index or -1 if not set
 */
int
ratbag_device_data_hidpp10_get_led_count(const struct ratbag_device_data *data);

/* HID++ 2.0 */

/**
 * @return The device index or -1 if not set
 */
int
ratbag_device_data_hidpp20_get_index(const struct ratbag_device_data *data);

int
ratbag_device_data_hidpp20_get_led_count(const struct ratbag_device_data *data);

enum hidpp20_quirk
ratbag_device_data_hidpp20_get_quirk(const struct ratbag_device_data *data);

/* SteelSeries */

/**
 * @return The device version or -1 if not set
 */
int
ratbag_device_data_steelseries_get_device_version(const struct ratbag_device_data *data);

/**
 * @return The button count or -1 if not set
 */
int
ratbag_device_data_steelseries_get_button_count(const struct ratbag_device_data *data);

/**
 * @return The led count or -1 if not set
 */
int
ratbag_device_data_steelseries_get_led_count(const struct ratbag_device_data *data);

struct dpi_list *
ratbag_device_data_steelseries_get_dpi_list(const struct ratbag_device_data *data);

struct dpi_range *
ratbag_device_data_steelseries_get_dpi_range(const struct ratbag_device_data *data);

int
ratbag_device_data_steelseries_get_macro_length(const struct ratbag_device_data *data);

int
ratbag_device_data_steelseries_get_mono_led(const struct ratbag_device_data *data);

int
ratbag_device_data_steelseries_get_short_button(const struct ratbag_device_data *data);
