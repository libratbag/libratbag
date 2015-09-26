/***
  This file is part of ratbagd.

  Copyright 2015 David Herrmann <dh.herrmann@gmail.com>

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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <systemd/sd-bus.h>
#include <systemd/sd-event.h>
#include "ratbagd.h"
#include "shared-macro.h"

struct ratbagd_profile {
	struct ratbag_profile *lib_profile;
	unsigned int index;
};

const sd_bus_vtable ratbagd_profile_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Index", "u", NULL, offsetof(struct ratbagd_profile, index), SD_BUS_VTABLE_PROPERTY_CONST),
	SD_BUS_VTABLE_END,
};

int ratbagd_profile_new(struct ratbagd_profile **out,
			struct ratbag_profile *lib_profile,
			unsigned int index)
{
	_cleanup_(ratbagd_profile_freep) struct ratbagd_profile *profile = NULL;

	assert(out);
	assert(lib_profile);

	profile = calloc(1, sizeof(*profile));
	if (!profile)
		return -ENOMEM;

	profile->lib_profile = lib_profile;
	profile->index = index;

	*out = profile;
	profile = NULL;
	return 0;
}

struct ratbagd_profile *ratbagd_profile_free(struct ratbagd_profile *profile)
{
	if (!profile)
		return NULL;

	/* we cannot assume the profile is gone, so set NULL explicitly */
	ratbag_profile_unref(profile->lib_profile);
	profile->lib_profile = NULL;

	return mfree(profile);
}
