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

#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

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
 * Miscellaneous cleanup helpers
 * All these helpers are suitable for use with _cleanup_(). They call the helper
 * they're named after on the variable marked for cleanup.
 */
static inline void freep(void *p)
{
	free(*(void**)p);
}

/*
 * streq() - test whether two strings are equal
 * @_a: string A
 * @_b: string B
 */
#define streq(_a, _b) (!strcmp((_a), (_b)))

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
 * now() - returns current time in nano-seconds
 * @clock: clock to use
 */
static inline uint64_t now(clockid_t clock)
{
	struct timespec spec = {};
	clock_gettime(clock, &spec);
	return spec.tv_sec * 1000ULL * 1000ULL * 1000ULL + spec.tv_nsec;
}
