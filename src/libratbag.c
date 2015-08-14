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

#include "config.h"
#include <assert.h>
#include <stdlib.h>

#include "libratbag-private.h"
#include "libratbag-util.h"

struct libratbag {
	int refcount;
};

LIBRATBAG_EXPORT struct libratbag *
libratbag_create_context(void)
{
	struct libratbag *libratbag;

	libratbag = zalloc(sizeof(*libratbag));
	if (!libratbag)
		return NULL;

	libratbag->refcount = 1;

	return libratbag;
}

LIBRATBAG_EXPORT struct libratbag *
libratbag_ref(struct libratbag *libratbag)
{
	libratbag->refcount++;
	return libratbag;
}

LIBRATBAG_EXPORT struct libratbag *
libratbag_unref(struct libratbag *libratbag)
{
	if (libratbag == NULL)
		return NULL;

	assert(libratbag->refcount > 0);
	libratbag->refcount--;
	if (libratbag->refcount > 0)
		return libratbag;

	free(libratbag);

	return NULL;
}
