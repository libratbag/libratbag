/*
 * Copyright © 2008-2011 Kristian Høgsberg
 * Copyright © 2011 Intel Corporation
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

/*
 * This list data structure is verbatim copy from wayland-util.h from the
 * Wayland project; except that wl_ prefix has been removed.
 */

#include "config.h"

#include <ctype.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <iconv.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>

#include "libratbag-util.h"
#include "libratbag-private.h"

void
list_init(struct list *list)
{
	list->prev = list;
	list->next = list;
}

void
list_insert(struct list *list, struct list *elm)
{
	elm->prev = list;
	elm->next = list->next;
	list->next = elm;
	elm->next->prev = elm;
}

void
list_append(struct list *list, struct list *elm)
{
	elm->next = list;
	elm->prev = list->prev;
	list->prev = elm;
	elm->prev->next = elm;
}

void
list_remove(struct list *elm)
{
	elm->prev->next = elm->next;
	elm->next->prev = elm->prev;
	elm->next = NULL;
	elm->prev = NULL;
}

int
list_empty(const struct list *list)
{
	return list->next == list;
}

const char *
udev_prop_value(struct udev_device *device,
		const char *prop_name)
{
	struct udev_device *parent;
	const char *prop_value = NULL;

	parent = device;
	while (parent && !prop_value) {
		prop_value = udev_device_get_property_value(parent, prop_name);
		parent = udev_device_get_parent(parent);
	}

	return prop_value;
}

ssize_t
ratbag_utf8_to_enc(char *buf, size_t buf_len, const char *to_enc,
		   const char *format, ...)
{
	va_list args;
	iconv_t converter;
	char str[buf_len];
	char *in_buf = str, *out_buf = (char*)buf;
	size_t in_bytes_left, out_bytes_left = buf_len;
	int ret;

	memset(buf, 0, buf_len);

	va_start(args, format);
	ret = vsnprintf(str, buf_len, format, args);
	va_end(args);

	if (ret < 0)
		return ret;

	in_bytes_left = ret;

	converter = iconv_open(to_enc, "UTF-8");
	if (converter == (iconv_t)-1)
		return -errno;

	ret = iconv(converter, &in_buf, &in_bytes_left, &out_buf,
		    &out_bytes_left);
	if (ret)
		ret = -errno;
	else
		ret = buf_len - out_bytes_left;

	iconv_close(converter);

	return ret;
}

ssize_t
ratbag_utf8_from_enc(char *in_buf, size_t in_len, const char *from_enc,
		     char **out)
{
	iconv_t converter;
	size_t len = in_len * 6,
	       in_bytes_left = in_len,
	       out_bytes_left = len;
	char *pos;
	ssize_t ret;

	converter = iconv_open("UTF-8", from_enc);
	if (converter == (iconv_t)-1)
		return -errno;

	/*
	 * We *could* dynamically allocate the out buffer. However iconv's
	 * obnoxious semantics, mainly the fact that it modifies every pointer
	 * given to it, would make this code a lot larger and complex than it
	 * needs to be for a mouse with blinking lights.
	 *
	 * So, since there's no encoding that takes more then 6 bytes per
	 * character, allocate for that and just fail if that's not enough.
	 */
	*out = zalloc(len);
	pos = *out;
	if (!*out) {
		ret = -errno;
		goto err;
	}

	ret = iconv(converter, &in_buf, &in_bytes_left, &pos, &out_bytes_left);
	if (ret) {
		ret = -errno;
		goto err;
	}

	/* Now get rid of any space in the buffer we don't need */
	*out = realloc(*out, (len - out_bytes_left) + 1);
	if (!*out)
		ret = -errno;
	else
		ret = (len - out_bytes_left) + 1;

err:
	if (ret < 0 && *out) {
		free(*out);
		*out = NULL;
	}

	iconv_close(converter);
	return ret;
}

int mkdir_p(const char *dir, mode_t mode)
{
    struct stat sb;

    if (!dir)
        return EINVAL;

    if (!stat(dir, &sb))
        return 0;

    mkdir_p(dirname(strdupa(dir)), mode);

    return mkdir(dir, mode);
}
