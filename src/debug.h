/*
 * debug definitions.
 *
 * Copyright 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013 Red Hat, Inc
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

#ifndef DEBUG_H
#define DEBUG_H

#define DEBUG_LVL	0

#if DEBUG_LVL > 0
static void pr_buffer(__u8 *buffer, int size) {
	char *sep = "";
	int i;

	if (size < 0)
		return;

	for (i = 0; i < size; ++i) {
		printf("%s%02x", sep, buffer[i] & 0xFF);
		sep = " ";
	}

	printf("\n");
}
#else
#define pr_buffer(buffer, size) do {} while (0)
#endif

#if DEBUG_LVL > 0
#define pr_dbg(fmt, arg...)		\
	printf(fmt, ##arg)
#else /* DEBUG_LVL > 0 */
#define pr_dbg(fmt, arg...)		\
	do {} while(0);
#endif /* DEBUG_LVL > 0 */

#endif /* DEBUG_H */
