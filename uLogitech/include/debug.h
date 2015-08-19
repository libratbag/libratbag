/*
 * debug definitions.
 *
 * Copyright 2013 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
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
