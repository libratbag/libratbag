/*
 * HID++ generic definitions
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

/*
 * Based on the HID++ documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include "hidpp-constants.h"

#ifndef HIDPP_GENERIC_H
#define HIDPP_GENERIC_H

/* Keep this in sync with ratbag_log_priority */
enum hidpp_log_priority {
	/**
	 * Raw protocol messages. Using this log level results in *a lot* of
	 * output.
	 */
	HIDPP_LOG_PRIORITY_RAW = 10,
	HIDPP_LOG_PRIORITY_DEBUG = 20,
	HIDPP_LOG_PRIORITY_INFO = 30,
	HIDPP_LOG_PRIORITY_ERROR = 40,
};

typedef void (*hidpp_log_handler)(void *userdata,
				  enum hidpp_log_priority priority,
				  const char *format, va_list args)
	__attribute__ ((format (printf, 3, 0)));

struct hidpp_device {
	int hidraw_fd;
	void *userdata;
	hidpp_log_handler log_handler;
	enum hidpp_log_priority log_priority;
};

void
hidpp_device_init(struct hidpp_device *dev, int fd);
void
hidpp_device_set_log_handler(struct hidpp_device *dev,
			     hidpp_log_handler log_handler,
			     enum hidpp_log_priority priority,
			     void *userdata);

extern const char *hidpp_errors[0xFF];

const char *
hidpp20_1b04_get_physical_mapping_name(uint16_t value);

enum ratbag_button_type
hidpp20_1b04_get_physical_mapping(uint16_t value);

const char *
hidpp20_1b04_get_logical_mapping_name(uint16_t value);

const struct ratbag_button_action *
hidpp20_1b04_get_logical_mapping(uint16_t value);

uint16_t
hidpp20_1b04_get_logical_control_id(const struct ratbag_button_action *action);

int
hidpp_write_command(struct hidpp_device *dev, uint8_t *cmd, int size);

int
hidpp_read_response(struct hidpp_device *dev, uint8_t *buf, size_t size);

void
hidpp_log(struct hidpp_device *dev,
	  enum hidpp_log_priority priority,
	  const char *format,
	  ...)
	__attribute__ ((format (printf, 3, 4)));

void
hidpp_log_buffer(struct hidpp_device *dev,
		 enum hidpp_log_priority priority,
		 const char *header,
		 uint8_t *buf, size_t len);

#define hidpp_log_raw(li_, ...) hidpp_log((li_), HIDPP_LOG_PRIORITY_RAW, __VA_ARGS__)
#define hidpp_log_debug(li_, ...) hidpp_log((li_), HIDPP_LOG_PRIORITY_DEBUG, __VA_ARGS__)
#define hidpp_log_info(li_, ...) hidpp_log((li_), HIDPP_LOG_PRIORITY_INFO, __VA_ARGS__)
#define hidpp_log_error(li_, ...) hidpp_log((li_), HIDPP_LOG_PRIORITY_ERROR, __VA_ARGS__)
#define hidpp_log_bug_kernel(li_, ...) hidpp((li_), HIDPP_LOG_PRIORITY_ERROR, "kernel bug: " __VA_ARGS__)
#define hidpp_log_buf_raw(li_, h_, buf_, len_) hidpp_log_buffer(li_, HIDPP_LOG_PRIORITY_RAW, h_, buf_, len_)
#define hidpp_log_buf_debug(li_, h_, buf_, len_) hidpp_log_buffer(li_, HIDPP_LOG_PRIORITY_DEBUG, h_, buf_, len_)
#define hidpp_log_buf_info(li_, h_, buf_, len_) hidpp_log_buffer(li_, HIDPP_LOG_PRIORITY_INFO, h_, buf_, len_)
#define hidpp_log_buf_error(li_, h_, buf_, len_) hidpp_log_buffer(li_, HIDPP_LOG_PRIORITY_ERROR, h_, buf_, len_)

uint16_t hidpp_crc_ccitt(uint8_t *data, unsigned int length);

static inline uint16_t
hidpp_get_unaligned_be_u16(uint8_t *buf)
{
	return (buf[0] << 8) | buf[1];
}

static inline uint16_t
hidpp_be_u16_to_cpu(uint16_t data)
{
	return hidpp_get_unaligned_be_u16((uint8_t *)&data);
}

static inline void
hidpp_set_unaligned_be_u16(uint8_t *buf, uint16_t value)
{
	buf[0] = value >> 8;
	buf[1] = value & 0xFF;
}

static inline uint16_t
hidpp_cpu_to_be_u16(uint16_t data)
{
	uint16_t result;
	hidpp_set_unaligned_be_u16((uint8_t *)&result, data);
	return result;
}

static inline uint16_t
hidpp_get_unaligned_le_u16(uint8_t *buf)
{
	return (buf[1] << 8) | buf[0];
}

static inline uint16_t
hidpp_le_u16_to_cpu(uint16_t data)
{
	return hidpp_get_unaligned_le_u16((uint8_t *)&data);
}

static inline void
hidpp_set_unaligned_le_u16(uint8_t *buf, uint16_t value)
{
	buf[0] = value & 0xFF;
	buf[1] = value >> 8;
}

static inline uint16_t
hidpp_cpu_to_le_u16(uint16_t data)
{
	uint16_t result;
	hidpp_set_unaligned_le_u16((uint8_t *)&result, data);
	return result;
}

static inline uint32_t
hidpp_get_unaligned_be_u32(uint8_t *buf)
{
	return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
}

#endif /* HIDPP_GENERIC_H */
