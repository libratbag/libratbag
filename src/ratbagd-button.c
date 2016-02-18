/***
  This file is part of ratbagd.

  Copyright 2016 Red Hat, Inc.

  Permission is hereby granted, free of charge, to any person obtaining a
  copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the
  Software is furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice (including the next
  paragraph) shall be included in all copies or substantial portions of the
  Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
  THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
  DEALINGS IN THE SOFTWARE.
***/

#include <assert.h>
#include <errno.h>
#include <libratbag.h>
#include <libudev.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "ratbagd.h"
#include "shared-macro.h"

struct ratbagd_button {
	struct ratbag_button *lib_button;
	unsigned int index;
	char *path;
};

const sd_bus_vtable ratbagd_button_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_button, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_VTABLE_END,
};

int ratbagd_button_new(struct ratbagd_button **out,
		       struct ratbagd_device *device,
		       struct ratbagd_profile *profile,
		       struct ratbag_button *lib_button,
		       unsigned int index)
{
	_cleanup_(ratbagd_button_freep) struct ratbagd_button *button = NULL;
	char profile_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1],
	     button_buffer[DECIMAL_TOKEN_MAX(unsigned int) + 1];
	int r;

	assert(out);
	assert(lib_button);

	button = calloc(1, sizeof(*button));
	if (!button)
		return -ENOMEM;

	button->lib_button = lib_button;
	button->index = index;

	sprintf(profile_buffer, "p%u", ratbagd_profile_get_index(profile));
	sprintf(button_buffer, "b%u", index);
	r = sd_bus_path_encode_many(&button->path,
				    "/org/freedesktop/ratbag1/button/%/%/%",
				    ratbagd_device_get_name(device),
				    profile_buffer,
				    button_buffer);
	if (r < 0)
		return r;

	*out = button;
	button = NULL;
	return 0;
}

const char *ratbagd_button_get_path(struct ratbagd_button *button)
{
	assert(button);
	return button->path;
}

struct ratbagd_button *ratbagd_button_free(struct ratbagd_button *button)
{
	if (!button)
		return NULL;

	button->path = mfree(button->path);

	/* we cannot assume the button is gone, so set NULL explicitly */
	ratbag_button_unref(button->lib_button);
	button->lib_button = NULL;

	return mfree(button);
}

