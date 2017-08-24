/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2013-2015 Red Hat, Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#pragma once

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libudev.h>

#include "shared-macro.h"

/*
 * This list data structure is a verbatim copy from wayland-util.h from the
 * Wayland project; except that wl_ prefix has been removed.
 */

struct list {
	struct list *prev;
	struct list *next;
};

void list_init(struct list *list);
void list_insert(struct list *list, struct list *elm);
void list_remove(struct list *elm);
int list_empty(const struct list *list);

#define list_for_each(pos, head, member)				\
	for (pos = 0, pos = container_of((head)->next, pos, member);	\
	     &pos->member != (head);					\
	     pos = container_of(pos->member.next, pos, member))

#define list_for_each_safe(pos, tmp, head, member)			\
	for (pos = 0, tmp = 0,						\
	     pos = container_of((head)->next, pos, member),		\
	     tmp = container_of((pos)->member.next, tmp, member);	\
	     &pos->member != (head);					\
	     pos = tmp,							\
	     tmp = container_of(pos->member.next, tmp, member))

static inline char*
strncpy_safe(char *dest, const char *src, size_t n)
{
	strncpy(dest, src, n);
	dest[n - 1] = '\0';
	return dest;
}

#define LIBRATBAG_EXPORT __attribute__ ((visibility("default")))

static inline void *
zalloc(size_t size)
{
	void *p = calloc(1, size);

	if (!p)
		abort();
	return p;
}

/**
 * returns NULL if str is NULL, otherwise guarantees a successful strdup.
 */
static inline char *
strdup_safe(const char *str)
{
	char *s;

	if (!str)
		return NULL;

	s = strdup(str);
	if (!s)
		abort();

	return s;
}

static inline int
snprintf_safe(char *buf, size_t n, const char *fmt, ...)
{
	va_list args;
	int rc;

	va_start(args, fmt);
	rc = vsnprintf(buf, n, fmt, args);
	va_end(args);

	if (rc < 0 || n < (size_t)rc)
		abort();

	return rc;
}

#define sprintf_safe(buf, fmt, ...) \
	snprintf_safe(buf, ARRAY_LENGTH(buf), fmt, __VA_ARGS__)

static inline void
msleep(unsigned int ms)
{
	usleep(ms * 1000);
}

static inline int
long_bit_is_set(const unsigned long *array, int bit)
{
    return !!(array[bit / LONG_BITS] & (1LL << (bit % LONG_BITS)));
}

static inline void
long_set_bit(unsigned long *array, int bit)
{
    array[bit / LONG_BITS] |= (1LL << (bit % LONG_BITS));
}

static inline void
long_clear_bit(unsigned long *array, int bit)
{
    array[bit / LONG_BITS] &= ~(1LL << (bit % LONG_BITS));
}

static inline void
long_set_bit_state(unsigned long *array, int bit, int state)
{
	if (state)
		long_set_bit(array, bit);
	else
		long_clear_bit(array, bit);
}

const char *
udev_prop_value(struct udev_device *device,
		const char *property_name);

/**
 * Converts a string from UTF-8 to the encoding specified. Returns the number
 * of bytes written to buf on success, or negative errno value on failure.
 */
ssize_t
ratbag_utf8_to_enc(char *buf, size_t buf_len, const char *to_enc,
		   const char *format, ...) __attribute__((format(printf, 4, 5)));
/**
 * Converts a string from the given encoding into UTF-8. The memory for the
 * result is allocated and a pointer to the result is placed in *out. Returns
 * the number of bytes in the UTF-8 version of the string on success, negative
 * errno value on failure.
 */
ssize_t
ratbag_utf8_from_enc(char *in_buf, size_t in_len, const char *from_enc,
		     char **out);
