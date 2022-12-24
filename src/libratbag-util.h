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

#include <linux/input-event-codes.h>

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
void list_append(struct list *list, struct list *elm);
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

int mkdir_p(const char *dir, mode_t mode);

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

/**
 * Returns a strdup'd string with all non-ascii characters replaced with a
 * space.
 */
static inline char *
strdup_ascii_only(const char *str_in)
{
	char *str, *ascii_only;

	ascii_only = strdup_safe(str_in);
	str = ascii_only;
	while (str && *str) {
		unsigned char c = *str;
		if (c > 127)
			*str = ' ';

		str++;
	}
	return ascii_only;
}

#define sprintf_safe(buf, fmt, ...) \
	snprintf_safe(buf, ARRAY_LENGTH(buf), fmt, __VA_ARGS__)

__attribute__((format(printf, 1, 2)))
static inline char *
asprintf_safe(const char *fmt, ...)
{
	va_list args;
	int rc;
	char *result;

	va_start(args, fmt);
	rc = vasprintf(&result, fmt, args);
	va_end(args);

	if (rc < 0)
		abort();

	return result;
}

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

__attribute__((format(printf, 2, 3)))
static inline int
xasprintf(char **strp, const char *fmt, ...)
{
	int rc = 0;
	va_list args;

	va_start(args, fmt);
	rc = vasprintf(strp, fmt, args);
	va_end(args);
	if ((rc == -1) && strp)
		*strp = NULL;

	return rc;
}

struct dpi_range {
	unsigned int min;
	unsigned int max;
	float step;
};

/* Parse a string in the form min:max@step to a dpi_range */
static inline struct dpi_range *
dpi_range_from_string(const char *str)
{
	float min, max, step;
	int rc;
	struct dpi_range *range;

	rc = sscanf(str, "%f:%f@%f", &min, &max, &step);
	if (rc != 3 || min < 0 || max <= min || step < 20)
		return NULL;

	range = zalloc(sizeof(*range));
	range->min = min;
	range->max = max;
	range->step = step;

	return range;
}

struct dpi_list {
	int *entries;
	size_t nentries;
};

static inline void
dpi_list_free(struct dpi_list *list)
{
	if (list == NULL)
		return;
	free(list->entries);
	free(list);
}

DEFINE_TRIVIAL_CLEANUP_FUNC(struct dpi_list *, dpi_list_free);

/* Parse a string in the form "100;200;400"to a dpi_list */
static inline struct dpi_list *
dpi_list_from_string(const char *str)
{
	struct dpi_list *list;
	unsigned int i, count, index;
	int nread, dpi = 0;
	int rc = 0;
	char c;
	void *tmp;

	if (!str || str[0] == '\0')
		return NULL;

	/* first, count how many elements do we have in the table */
	count = 1;
	i = 0;
	while (str[i] != 0) {
		c = str[i++];
		if (c == ';')
			count++;
	}

	list = zalloc(sizeof *list);
	list->nentries = count;
	list->entries = zalloc(count * sizeof(list->entries[0]));

	index = 0;

	while (*str != 0 && index < count) {
		if (*str == ';') {
			str++;
			continue;
		}

		nread = 0;
		rc = sscanf(str, "%d%n", &dpi, &nread);
		if (rc != 1 || !nread || dpi < 0)
			goto err;

		list->entries[index] = dpi;
		str += nread;
		index++;
	}

	if (index == 0)
		goto err;

	/* Drop empty entries for trailing semicolons */
	list->nentries = index;
	tmp = realloc(list->entries, index * sizeof(list->entries[0]));
	if (tmp)
		list->entries = tmp;

	return list;

err:
	dpi_list_free(list);
	return NULL;
}

static inline uint16_t
get_unaligned_be_u16(const uint8_t *buf)
{
	return (buf[0] << 8) | buf[1];
}

static inline void
set_unaligned_be_u16(uint8_t *buf, uint16_t value)
{
	buf[0] = value >> 8;
	buf[1] = value & 0xFF;
}

static inline uint16_t
get_unaligned_le_u16(const uint8_t *buf)
{
	return (buf[1] << 8) | buf[0];
}

static inline void
set_unaligned_le_u16(uint8_t *buf, uint16_t value)
{
	buf[0] = value & 0xFF;
	buf[1] = value >> 8;
}

static inline uint32_t
get_unaligned_be_u32(const uint8_t *buf)
{
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

static inline bool
ratbag_key_is_modifier(const unsigned int key)
{
	switch (key) {
	case KEY_LEFTALT:
	case KEY_LEFTCTRL:
	case KEY_LEFTMETA:
	case KEY_LEFTSHIFT:
	case KEY_RIGHTALT:
	case KEY_RIGHTCTRL:
	case KEY_RIGHTMETA:
	case KEY_RIGHTSHIFT:
		return true;
	default:
		return false;
	}
}
