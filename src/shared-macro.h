#pragma once

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

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

/*
 * We require:
 *   sizeof(void*) == sizeof(long)
 *   sizeof(long) == 4 || sizeof(long) == 8
 *   sizeof(int) == 4
 * The linux kernel requires the same from the toolchain, so this should work
 * just fine.
 */
#if __SIZEOF_POINTER__ != __SIZEOF_LONG__
#  error "sizeof(void*) != sizeof(long)"
#elif __SIZEOF_LONG__ != 4 && __SIZEOF_LONG__ != 8
#  error "sizeof(long) != 4 && sizeof(long) != 8"
#elif __SIZEOF_INT__ != 4
#  error "sizeof(int) != 4"
#endif

/*
 * Shortcuts for gcc attributes. See GCC manual for details.
 */
#define _alignas_(_x) __attribute__((aligned(__alignof(_x))))
#define _alloc_(...) __attribute__((alloc_size(__VA_ARGS__)))
#define _cleanup_(_x) __attribute__((cleanup(_x)))
#define _const_ __attribute__((const))
#define _deprecated_ __attribute__((deprecated))
#define _hidden_ __attribute__((visibility("hidden")))
#define _likely_(_x) (__builtin_expect(!!(_x), 1))
#define _malloc_ __attribute__((malloc))
#define _packed_ __attribute__((packed))
#define _printf_(_a, _b) __attribute__((format (printf, _a, _b)))
#define _public_ __attribute__((visibility("default")))
#define _pure_ __attribute__((pure))
#define _sentinel_ __attribute__((sentinel))
#define _unlikely_(_x) (__builtin_expect(!!(_x), 0))
#define _unused_ __attribute__((unused))
#define _weak_ __attribute__((weak))
#define _weakref_(_x) __attribute__((weakref(#_x)))

/* container_of macro */
#ifdef __GNUC__
#define container_of(ptr, sample, member)				\
	(__typeof__(sample))((char *)(ptr)	-			\
		 ((char *)&(sample)->member - (char *)(sample)))
#else
#define container_of(ptr, sample, member)				\
	(void *)((char *)(ptr)	-				        \
		 ((char *)&(sample)->member - (char *)(sample)))
#endif

#define LONG_BITS (sizeof(long) * 8)
#define NLONGS(x) (((x) + LONG_BITS - 1) / LONG_BITS)
#define ARRAY_LENGTH(a) (sizeof (a) / sizeof (a)[0])
#define ARRAY_FOR_EACH(_arr, _elem) \
	for (size_t _i = 0; _i < ARRAY_LENGTH(_arr) && (_elem = &_arr[_i]); _i++)
#define AS_MASK(v) (1 << (v))

#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

/*
 * DECIMAL_TOKEN_MAX() - calculate maximum length of the decimal representation
 *                       of an integer
 * @_type: type of integer
 */
#define DECIMAL_TOKEN_MAX(_type)                        \
        (1 + (sizeof(_type) <= 1 ? 3 :                  \
              sizeof(_type) <= 2 ? 5 :                  \
              sizeof(_type) <= 4 ? 10 :                 \
              sizeof(_type) <= 8 ? 20 :                 \
              sizeof(int[-2 * (sizeof(_type) > 8)])))

/*
 * PROTECT_ERRNO: make sure a function protects errno
 */
static inline void reset_errno(int *saved_errno)
{
	errno = *saved_errno;
}
#define PROTECT_ERRNO _cleanup_(reset_errno) _unused_ int saved_errno = errno

#define _cleanup_free_ _cleanup_(freep)
#define _cleanup_close_ _cleanup_(safe_closep)
/*
 * DEFINE_TRIVIAL_CLEANUP_FUNC() - define helper suitable for _cleanup_()
 * @_type: type of object to cleanup
 * @_func: function to call on cleanup
 */
#define DEFINE_TRIVIAL_CLEANUP_FUNC(_type, _func)		\
	static inline void _func##p(_type *_p)			\
	{							\
		if (*_p)					\
			_func(*_p);				\
	}							\
	struct __useless_struct_to_allow_trailing_semicolon__

DEFINE_TRIVIAL_CLEANUP_FUNC(struct udev*, udev_unref);
DEFINE_TRIVIAL_CLEANUP_FUNC(struct udev_device*, udev_device_unref);
DEFINE_TRIVIAL_CLEANUP_FUNC(struct udev_enumerate*, udev_enumerate_unref);

/*
 * ELEMENTSOF() - number of array elements
 * @_array: C array
 *
 * This calculates the number of elements (compared to the byte size) of
 * the given C array. This returns (void) if the passed argument is not
 * actually a valid C array (decided at compile time).
 */
#define ELEMENTSOF(_array)						\
	__extension__ (__builtin_choose_expr(				\
		!__builtin_types_compatible_p(typeof(_array),		\
					      typeof(&*(_array))),	\
		sizeof(_array) / sizeof((_array)[0]),			\
		(void)0))

/*
 * negative_errno() - return negative errno
 *
 * This helper should be used to shut up gcc if you know 'errno' is
 * negative. Instead of "return -errno;", use "return negative_errno();"
 * It will suppress bogus gcc warnings in case it assumes 'errno' might
 * be 0 and thus the caller's error-handling might not be triggered.
 */
static inline int negative_errno(void)
{
	return (errno <= 0) ? -EINVAL : -errno;
}

/*
 * mfree() - free memory
 * @mem: memory to free
 *
 * This is basically the same as free(), but returns NULL. This makes free()
 * follow the same style as all our other destructors.
 */
static inline void *mfree(void *mem)
{
	free(mem);
	return NULL;
}

/*
 * safe_close() - safe variant of close(2)
 * @fd: fd to close
 *
 * This is the same as close(2), but allows passing negative FDs, which makes
 * it a no-op. Furthermore, this always returns -1.
 */
static inline int safe_close(int fd)
{
	if (fd >= 0) {
		PROTECT_ERRNO;
		close(fd);
	}
	return -1;
}

/*
 * Miscellaneous cleanup helpers
 * All these helpers are suitable for use with _cleanup_(). They call the helper
 * they're named after on the variable marked for cleanup.
 */
static inline void freep(void *p)
{
	free(*(void**)p);
}

static inline void safe_closep(int *fd)
{
	safe_close(*fd);
}

/*
 * streq() - test whether two strings are equal
 * @_a: string A
 * @_b: string B
 */
#define streq(_a, _b) (strcmp((_a), (_b)) == 0)
#define strneq(s1, s2, n) (strncmp((s1), (s2), (n)) == 0)

/*
 * streq_ptr() - test whether two strings are equal, considering NULL valid
 * @a: string A or NULL
 * @b: string B or NULL
 */
static inline bool streq_ptr(const char *a, const char *b)
{
	return (a && b) ? streq(a, b) : (!a && !b);
}

/*
 * startswith() - test prefix of a string
 * @s: string to test
 * @prefix: prefix to test for
 *
 * This returns a pointer to the first character in @s that follows @prefix. If
 * @s does not start with @prefix, NULL is returned.
 */
static inline const char *startswith(const char *s, const char *prefix)
{
	size_t l;

	l = strlen(prefix);
	if (strncmp(s, prefix, l) == 0)
		return s + l;

	return NULL;
}

/*
 * safe_atou() - safe variant of strtoul()
 * @s: string to parse
 * @ret_u: output storage for parsed integer
 *
 * This is a sane and safe variant of strtoul(), which rejects any invalid
 * input, or trailing garbage.
 *
 * Return: 0 on success, negative error code on failure.
 */
static inline int safe_atou(const char *s, unsigned int *ret_u)
{
	char *x = NULL;
	unsigned long l;

	assert(s);
	assert(ret_u);

	errno = 0;
	l = strtoul(s, &x, 0);
	if (!x || x == s || *x || errno)
		return errno > 0 ? -errno : -EINVAL;

	if ((unsigned long)(unsigned int)l != l)
		return -ERANGE;

	*ret_u = (unsigned int)l;
	return 0;
}

/*
 * now() - returns current time in nano-seconds
 * @clock: clock to use
 */
static inline uint64_t now(clockid_t clock)
{
	struct timespec spec = {};
	clock_gettime(clock, &spec);
	return spec.tv_sec * 1000ULL * 1000ULL * 1000ULL + spec.tv_nsec;
}
