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
#include <stdio.h>
#include <stdlib.h>

#include "libratbag-private.h"
#include "libratbag-util.h"

struct libratbag {
	int refcount;
	libratbag_log_handler log_handler;
	enum libratbag_log_priority log_priority;
};

static void
libratbag_default_log_func(struct libratbag *libratbag,
			  enum libratbag_log_priority priority,
			  const char *format, va_list args)
{
	const char *prefix;

	switch(priority) {
	case LIBRATBAG_LOG_PRIORITY_DEBUG: prefix = "debug"; break;
	case LIBRATBAG_LOG_PRIORITY_INFO: prefix = "info"; break;
	case LIBRATBAG_LOG_PRIORITY_ERROR: prefix = "error"; break;
	default: prefix="<invalid priority>"; break;
	}

	fprintf(stderr, "libratbag %s: ", prefix);
	vfprintf(stderr, format, args);
}

void
log_msg_va(struct libratbag *libratbag,
	   enum libratbag_log_priority priority,
	   const char *format,
	   va_list args)
{
	if (libratbag->log_handler &&
	    libratbag->log_priority <= priority)
		libratbag->log_handler(libratbag, priority, format, args);
}

void
log_msg(struct libratbag *libratbag,
	enum libratbag_log_priority priority,
	const char *format, ...)
{
	va_list args;

	va_start(args, format);
	log_msg_va(libratbag, priority, format, args);
	va_end(args);
}

LIBRATBAG_EXPORT struct libratbag *
libratbag_create_context(void)
{
	struct libratbag *libratbag;

	libratbag = zalloc(sizeof(*libratbag));
	if (!libratbag)
		return NULL;

	libratbag->refcount = 1;

	libratbag->log_handler = libratbag_default_log_func;
	libratbag->log_priority = LIBRATBAG_LOG_PRIORITY_DEBUG;

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
