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

#ifndef LIBRATBAG_H
#define LIBRATBAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>

#define LIBRATBAG_ATTRIBUTE_PRINTF(_format, _args) \
	__attribute__ ((format (printf, _format, _args)))
#define LIBRATBAG_ATTRIBUTE_DEPRECATED __attribute__ ((deprecated))

/**
 * @defgroup base Initialization and manipulation of libratbag contexts
 */

/**
 * @ingroup base
 *
 * A base handle for accessing devices. This struct is
 * refcounted, use ratbag_ref() and ratbag_unref().
 */
struct ratbag;

/**
 * @ingroup base
 *
 * Add a reference to the context. A context is destroyed whenever the
 * reference count reaches 0. See @ref ratbag_unref.
 *
 * @param ratbag A previously initialized valid ratbag context
 * @return The passed ratbag context
 */
struct ratbag *
ratbag_ref(struct ratbag *ratbag);

/**
 * @ingroup base
 *
 * Dereference the ratbag context. After this, the context may have been
 * destroyed, if the last reference was dereferenced. If so, the context is
 * invalid and may not be interacted with.
 *
 * @param ratbag A previously initialized ratbag context
 * @return NULL if context was destroyed otherwise the passed context
 */
struct ratbag *
ratbag_unref(struct ratbag *ratbag);

/**
 * @ingroup base
 *
 * Log priority for internal logging messages.
 */
enum ratbag_log_priority {
	RATBAG_LOG_PRIORITY_DEBUG = 10,
	RATBAG_LOG_PRIORITY_INFO = 20,
	RATBAG_LOG_PRIORITY_ERROR = 30,
};

/**
 * @ingroup base
 *
 * Set the log priority for the ratbag context. Messages with priorities
 * equal to or higher than the argument will be printed to the context's
 * log handler.
 *
 * The default log priority is @ref RATBAG_LOG_PRIORITY_ERROR.
 *
 * @param ratbag A previously initialized ratbag context
 * @param priority The minimum priority of log messages to print.
 *
 * @see ratbag_log_set_handler
 * @see ratbag_log_get_priority
 */
void
ratbag_log_set_priority(struct ratbag *ratbag,
			enum ratbag_log_priority priority);

/**
 * @ingroup base
 *
 * Get the context's log priority. Messages with priorities equal to or
 * higher than the argument will be printed to the current log handler.
 *
 * The default log priority is @ref RATBAG_LOG_PRIORITY_ERROR.
 *
 * @param ratbag A previously initialized ratbag context
 * @return The minimum priority of log messages to print.
 *
 * @see ratbag_log_set_handler
 * @see ratbag_log_set_priority
 */
enum ratbag_log_priority
ratbag_log_get_priority(const struct ratbag *ratbag);

/**
 * @ingroup base
 *
 * Log handler type for custom logging.
 *
 * @param ratbag The ratbag context
 * @param priority The priority of the current message
 * @param format Message format in printf-style
 * @param args Message arguments
 *
 * @see ratbag_log_set_priority
 * @see ratbag_log_get_priority
 * @see ratbag_log_set_handler
 */
typedef void (*ratbag_log_handler)(struct ratbag *ratbag,
				   enum ratbag_log_priority priority,
				   const char *format, va_list args)
		LIBRATBAG_ATTRIBUTE_PRINTF(3, 0);

/**
 * @ingroup base
 *
 * Set the context's log handler. Messages with priorities equal to or
 * higher than the context's log priority will be passed to the given
 * log handler.
 *
 * The default log handler prints to stderr.
 *
 * @param ratbag A previously initialized ratbag context
 * @param log_handler The log handler for library messages.
 *
 * @see ratbag_log_set_priority
 * @see ratbag_log_get_priority
 */
void
ratbag_log_set_handler(struct ratbag *ratbag,
		       ratbag_log_handler log_handler);

#ifdef __cplusplus
}
#endif
#endif /* LIBRATBAG_H */
