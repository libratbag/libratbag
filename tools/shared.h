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

#include "config.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <linux/input.h>

#include <libratbag.h>
#include <libratbag-util.h>
#include <libevdev/libevdev.h>

#define MAX_MACRO_EVENTS 256

LIBRATBAG_ATTRIBUTE_PRINTF(1, 2)
static inline void
error(const char *format, ...)
{
	va_list args;

	fprintf(stderr, "Error: ");

	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
}

struct udev_device*
udev_device_from_path(struct udev *udev, const char *path);

const char *
led_mode_to_str(enum ratbag_led_mode mode);

const char *
button_action_special_to_str(struct ratbag_button *button);

char *
button_action_button_to_str(struct ratbag_button *button);

char *
button_action_key_to_str(struct ratbag_button *button);

char *
button_action_to_str(struct ratbag_button *button);

char *
button_action_macro_to_str(struct ratbag_button *button);

enum ratbag_button_action_special
str_to_special_action(const char *str);

struct ratbag_device *
ratbag_cmd_open_device(struct ratbag *ratbag, const char *path);

extern const struct ratbag_interface interface;
