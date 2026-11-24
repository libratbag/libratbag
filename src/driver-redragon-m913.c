/*
 * Copyright © 2026 Valentin Baron
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
 * Redragon M913 (Impact Elite), receiver 25a7:fa07 and cable 25a7:fa08.
 *
 * The protocol was captured with usbmon from Redragon's Windows software.
 * Every request is a 16-byte feature report 0x08; the mouse answers each one
 * with input report 0x09, echoing the request. Byte 15 of both is a checksum,
 * (0x55 - sum of bytes 0..14) & 0xff, and the same rule closes every record
 * stored in the mouse.
 *
 *   08 08 00 AH AL N            read N (<= 10) bytes at address AH:AL
 *   08 07 00 AH AL N <data>     write N (<= 10) bytes at address AH:AL
 *
 * AH and AL are the address's high and low byte.
 *
 * The settings are a memory map; the vendor software's "apply" is nothing
 * but writes, there is no commit command. The mouse echoes some writes it
 * then ignores (a key sequence longer than 6 events), so every write is read
 * back.
 *
 *   0x00          1000/Hz report rate, checksum
 *   0x02          active resolution 1-5, checksum
 *   0x04          active profile 0-1, checksum (the button under the mouse
 *                 toggles)
 *   0x0c          5 resolutions: code, code, 0, checksum (codes:
 *                 m913_dpi_codes)
 *   0x54          LED red, green, blue, checksum
 *   0x58          LED mode, checksum
 *   0x5a          LED brightness, checksum
 *   0x5c          LED speed 1-5, checksum
 *   0x60          16 button records: type, p1, p2, checksum
 *   0x100 + 0x20 * record
 *                 key sequence of a type 05 record: count, count x (kind,
 *                 code low byte, code high byte), checksum
 *   0x300 + 0x180 * n
 *                 macro n (0-14): name length, UTF-16LE name (30 bytes in
 *                 all), event count (BE16), count x (kind, code low byte,
 *                 code high byte, delay-after ms BE16), checksum over count
 *                 and events
 *
 * Event kinds: 0x80/0x40 modifier bits down/up (HID modifier byte),
 * 0x81/0x41 keyboard usage down/up, 0x82/0x42 consumer usage down/up,
 * 0x84/0x44 mouse button mask down/up.
 *
 * Not supported:
 *
 * - The second profile: only the active one can be read or written, and
 *   only the button under the mouse switches between them.
 * - Button actions libratbag has no equivalent for: rapid fire and triple
 *   click, the report rate switch, the LED toggle, and macros that click
 *   mouse buttons. They read as unknown and are left as they are unless
 *   the button is changed.
 * - Macro repeat modes (a count, while held, until another key): reading
 *   ignores them, and a macro written by the driver plays once.
 * - Several keys on one button without a macro: read as a macro, and
 *   written back into macro memory rather than onto the button.
 * - The DPI indicator colours (0x2c-0x53), battery level and firmware
 *   version.
 * - Macro names are kept to ASCII.
 */

#include "config.h"

#include <errno.h>
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "libratbag-hidraw.h"
#include "libratbag-private.h"

#define M913_REPORT_REQUEST 0x08
#define M913_REPORT_ANSWER 0x09
#define M913_REPORT_SIZE 17

#define M913_CMD_WRITE 0x07
#define M913_CMD_READ 0x08
#define M913_CHUNK 10
#define M913_RETRIES 3

#define M913_NUM_PROFILES 1
#define M913_NUM_RESOLUTIONS 5
#define M913_NUM_BUTTONS 16
#define M913_NUM_LEDS 1

#define M913_ADDR_REPORT_RATE 0x00
#define M913_ADDR_ACTIVE_DPI 0x02
#define M913_ADDR_DPI 0x0c
#define M913_ADDR_LED_COLOR 0x54
#define M913_ADDR_LED_MODE 0x58
#define M913_ADDR_LED_BRIGHTNESS 0x5a
#define M913_ADDR_LED_SPEED 0x5c
#define M913_ADDR_RECORDS 0x60
#define M913_SETTINGS_SIZE 0xa0

#define M913_ADDR_SLOTS 0x100
#define M913_SLOT_SIZE 0x20
/* The slot has room for 10 events, but the firmware ignores a write of 7 or
 * more. */
#define M913_SLOT_EVENTS 6

#define M913_ADDR_MACROS 0x300
#define M913_MACRO_SIZE 0x180
#define M913_MACRO_COUNT 15
#define M913_MACRO_NAME 0x1e
#define M913_MACRO_EVENTS ((M913_MACRO_SIZE - M913_MACRO_NAME - 3) / 5)
/* What the vendor software stores after an event without a wait. */
#define M913_MACRO_DEFAULT_DELAY 3

enum m913_record_type {
	M913_TYPE_DISABLED = 0x00,
	M913_TYPE_MOUSE = 0x01,
	M913_TYPE_DPI = 0x02,
	M913_TYPE_FIRE = 0x04,
	M913_TYPE_SEQUENCE = 0x05,
	M913_TYPE_MACRO = 0x06,
	M913_TYPE_REPORT_RATE_SWITCH = 0x07,
	M913_TYPE_LED_TOGGLE = 0x08,
};

enum m913_dpi_function {
	M913_DPI_CYCLE = 0x01,
	M913_DPI_UP = 0x02,
	M913_DPI_DOWN = 0x03,
};

enum m913_event_kind {
	M913_EVENT_MODIFIER = 0x80,
	M913_EVENT_KEY = 0x81,
	M913_EVENT_CONSUMER = 0x82,
	M913_EVENT_MOUSE = 0x84,
};

/* HID keyboard usage of F24, the last of the ordinary keys. */
#define M913_LAST_ORDINARY_KEY 0x73

#define M913_EVENT_DOWN 0x80
#define M913_EVENT_UP 0x40
#define M913_EVENT_KIND(k_) ((k_) & 0x0f)
#define M913_EVENT_RELEASE(k_) (M913_EVENT_KIND(k_) | M913_EVENT_UP)

/* ratbag button index -> record index; the firmware stores the buttons in
 * its own order. The thumb grid is numbered as in the vendor software. */
static const uint8_t m913_button_records[M913_NUM_BUTTONS] = {
	7,	/* left */
	6,	/* right */
	10,	/* middle */
	11,	/* "fire", the small button beside the left one */
	0,	/* thumb 1 */
	1,	/* thumb 2 */
	2,	/* thumb 3 */
	3,	/* thumb 4 */
	4,	/* thumb 5 */
	5,	/* thumb 6 */
	8,	/* thumb 7 */
	9,	/* thumb 8 */
	12,	/* thumb 9 */
	13,	/* thumb 10 */
	14,	/* thumb 11 */
	15,	/* thumb 12 */
};

/* Mouse button mask -> ratbag button number. */
static const uint8_t m913_mouse_masks[] = { 0x01, 0x02, 0x04, 0x08, 0x10 };

static const unsigned int m913_report_rates[] = { 125, 250, 500, 1000 };

/* Resolutions 100-16000 in steps of 100, as the vendor software encodes them.
 * Irregular, so a table rather than a formula. */
static const uint8_t m913_dpi_codes[] = {
	0x00, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x10, 0x11, 0x12,
	0x13, 0x14, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x23, 0x25, 0x26,
	0x27, 0x28, 0x29, 0x2a, 0x2c, 0x2d, 0x2e, 0x2f, 0x30, 0x32, 0x33, 0x34, 0x35, 0x36, 0x38, 0x39,
	0x3a, 0x3b, 0x3c, 0x3e, 0x3f, 0x40, 0x41, 0x42, 0x44, 0x45, 0x46, 0x47, 0x48, 0x4a, 0x4b, 0x4c,
	0x4d, 0x4e, 0x50, 0x51, 0x52, 0x53, 0x54, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x62, 0x63, 0x64, 0x65, 0x66, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6e, 0x6f, 0x70, 0x71, 0x72,
	0x74, 0x75, 0x76, 0x77, 0x78, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x80, 0x81, 0x82, 0x83, 0x84, 0x86,
	0x87, 0x88, 0x89, 0x8a, 0x8c, 0x8d, 0x8e, 0x8f, 0x90, 0x92, 0x93, 0x94, 0x95, 0x96, 0x98, 0x99,
	0x9a, 0x9b, 0x9c, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xaa, 0xab, 0xac,
	0xad, 0xae, 0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd,
};

#define M913_DPI_MIN 100
#define M913_DPI_STEP 100

enum m913_led_mode {
	M913_LED_OFF = 0x00,
	M913_LED_STEADY = 0x01,
	M913_LED_BREATHING = 0x02,
	M913_LED_NEON = 0x03,
};

#define M913_LED_SPEED_MIN 1
#define M913_LED_SPEED_MAX 5

/* Modifier keycodes in the order of the HID modifier byte's bits. */
static const unsigned int m913_modifier_keys[] = {
	KEY_LEFTCTRL, KEY_LEFTSHIFT, KEY_LEFTALT, KEY_LEFTMETA,
	KEY_RIGHTCTRL, KEY_RIGHTSHIFT, KEY_RIGHTALT, KEY_RIGHTMETA,
};

struct m913_data {
	uint8_t settings[M913_SETTINGS_SIZE];
};

/* A button's record as last read from (or written to) the mouse. */
static const uint8_t *
m913_cached_record(const struct m913_data *data, const struct ratbag_button *button)
{
	return &data->settings[M913_ADDR_RECORDS + 4 * m913_button_records[button->index]];
}

/* Limits value to the range low..high. */
static int
m913_clamp(int value, int low, int high)
{
	return value < low ? low : value > high ? high : value;
}

/* The checksum the M913 puts after every record: 0x55 minus the sum of the
 * bytes, truncated to a byte. */
static uint8_t
m913_checksum(const uint8_t *data, size_t len)
{
	uint8_t sum = 0x55;

	for (size_t i = 0; i < len; i++)
		sum -= data[i];

	return sum;
}

/* Input-report filter: lets only the mouse's answers (report 0x09) through. */
static bool
m913_is_answer(uint8_t *buf, size_t len)
{
	return len >= 6 && buf[0] == M913_REPORT_ANSWER;
}

/* One request and its echo; returns 0 or a negative errno. */
static int
m913_try_request(struct ratbag_device *device, uint8_t cmd, uint16_t addr,
		 const uint8_t *data, uint8_t len, uint8_t *out)
{
	uint8_t request[M913_REPORT_SIZE] = {
		M913_REPORT_REQUEST, cmd, 0x00, addr >> 8, addr & 0xff, len,
	};
	uint8_t answer[64];
	int rc;

	if (data)
		memcpy(&request[6], data, len);
	request[M913_REPORT_SIZE - 1] = m913_checksum(request, M913_REPORT_SIZE - 1);

	rc = ratbag_hidraw_set_feature_report(device, M913_REPORT_REQUEST,
					      request, sizeof(request));
	if (rc < 0)
		return rc;

	/* Keyboard and consumer reports share the interface; wait for the
	 * echo of this request. */
	for (;;) {
		rc = ratbag_hidraw_read_input_report(device, answer, sizeof(answer),
						     m913_is_answer);
		if (rc < 0)
			return rc;
		if (rc == 0)
			return -ETIMEDOUT;
		if (rc >= 6 + len && memcmp(answer, (uint8_t[]){
				M913_REPORT_ANSWER, cmd, 0x00, addr >> 8, addr & 0xff, len,
			}, 6) == 0)
			break;
	}

	if (out)
		memcpy(out, &answer[6], len);

	return 0;
}

/* The receiver occasionally drops an answer; requests are idempotent. */
static int
m913_request(struct ratbag_device *device, uint8_t cmd, uint16_t addr,
	     const uint8_t *data, uint8_t len, uint8_t *out)
{
	int rc = -EIO;

	for (int i = 0; i < M913_RETRIES; i++) {
		rc = m913_try_request(device, cmd, addr, data, len, out);
		if (rc == 0)
			return 0;
	}

	log_error(device->ratbag, "m913: no answer to %s of %u bytes at 0x%04x (%d); is the mouse asleep?\n",
		  cmd == M913_CMD_READ ? "read" : "write", len, addr, rc);
	return rc;
}

/* Reads len bytes from the mouse's memory, in chunks the protocol allows. */
static int
m913_read(struct ratbag_device *device, uint16_t addr, uint8_t *buf, size_t len)
{
	for (size_t done = 0; done < len; done += M913_CHUNK) {
		uint8_t chunk = (uint8_t)min(M913_CHUNK, len - done);
		int rc = m913_request(device, M913_CMD_READ, addr + done, NULL, chunk, buf + done);
		if (rc)
			return rc;
	}

	return 0;
}

/* Writes len bytes to the mouse's memory, then reads them back: the mouse
 * echoes some writes it then ignores, so the echo alone proves nothing. */
static int
m913_write(struct ratbag_device *device, uint16_t addr, const uint8_t *data, size_t len)
{
	uint8_t stored[M913_MACRO_SIZE];
	int rc;

	assert(len <= sizeof(stored));

	for (size_t done = 0; done < len; done += M913_CHUNK) {
		uint8_t chunk = (uint8_t)min(M913_CHUNK, len - done);
		rc = m913_request(device, M913_CMD_WRITE, addr + done, data + done, chunk, NULL);
		if (rc)
			return rc;
	}

	rc = m913_read(device, addr, stored, len);
	if (rc)
		return rc;
	if (memcmp(stored, data, len) != 0) {
		log_error(device->ratbag, "m913: the mouse did not store the write at 0x%04x\n", addr);
		return -EIO;
	}

	return 0;
}

/* A value followed by its checksum. */
static int
m913_write_value(struct ratbag_device *device, uint16_t addr, uint8_t value)
{
	uint8_t data[2] = { value, m913_checksum(&value, 1) };

	return m913_write(device, addr, data, sizeof(data));
}

static int
m913_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, M913_REPORT_REQUEST) &&
	       ratbag_hidraw_has_report(device, M913_REPORT_ANSWER);
}

/* ---- keys ---------------------------------------------------------------- */

/* An M913 event as an evdev keycode, or 0 if libratbag can't express it. */
static unsigned int
m913_event_to_keycode(struct ratbag_device *device, uint8_t kind, uint16_t code)
{
	switch (M913_EVENT_KIND(kind) | M913_EVENT_DOWN) {
	case M913_EVENT_MODIFIER:
		for (size_t i = 0; i < ARRAY_LENGTH(m913_modifier_keys); i++)
			if (code == (1U << i))
				return m913_modifier_keys[i];
		return 0;
	case M913_EVENT_KEY:
		return ratbag_hidraw_get_keycode_from_keyboard_usage(device, (uint8_t)code);
	case M913_EVENT_CONSUMER:
		return ratbag_hidraw_get_keycode_from_consumer_usage(device, code);
	default:
		return 0;
	}
}

/* An evdev keycode as an M913 event kind and code; returns false if the
 * mouse has no way to send it. */
static bool
m913_keycode_to_event(struct ratbag_device *device, unsigned int keycode,
		      uint8_t *kind, uint16_t *code)
{
	uint8_t usage;
	uint16_t consumer;

	for (size_t i = 0; i < ARRAY_LENGTH(m913_modifier_keys); i++) {
		if (keycode == m913_modifier_keys[i]) {
			*kind = M913_EVENT_MODIFIER;
			*code = 1U << i;
			return true;
		}
	}

	/* Ordinary keys (letters to F24) as keyboard usages; the rest, volume
	 * and the like, as consumer usages where there is one, which more
	 * systems understand than the keyboard page's media keys. */
	usage = ratbag_hidraw_get_keyboard_usage_from_keycode(device, keycode);
	if (usage && usage <= M913_LAST_ORDINARY_KEY) {
		*kind = M913_EVENT_KEY;
		*code = usage;
		return true;
	}

	consumer = ratbag_hidraw_get_consumer_usage_from_keycode(device, keycode);
	if (consumer) {
		*kind = M913_EVENT_CONSUMER;
		*code = consumer;
		return true;
	}

	if (usage) {
		*kind = M913_EVENT_KEY;
		*code = usage;
		return true;
	}

	return false;
}

/* ---- reading ------------------------------------------------------------- */

/* Turns a button's key sequence into a ratbag action: a single key (with
 * modifiers) becomes a key action, several keys a macro. */
static void
m913_read_sequence(struct ratbag_button *button, const uint8_t *slot)
{
	struct ratbag_device *device = button->profile->device;
	struct ratbag_button_macro *macro;
	unsigned int count = min(slot[0], (M913_SLOT_SIZE - 2) / 3);
	unsigned int modifiers = 0, key = 0, keys = 0;

	/* A single key, possibly with modifiers, is a plain key action. */
	for (unsigned int i = 0; i < count; i++) {
		const uint8_t *event = &slot[1 + 3 * i];
		uint16_t code = event[1] | event[2] << 8;

		if (!(event[0] & M913_EVENT_DOWN))
			continue;
		if (M913_EVENT_KIND(event[0]) == M913_EVENT_KIND(M913_EVENT_MODIFIER)) {
			modifiers |= code;
		} else {
			key = m913_event_to_keycode(device, event[0], code);
			keys++;
		}
	}

	if (keys == 1 && key) {
		if (modifiers == 0) {
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
			button->action.action.key = key;
		} else if (ratbag_button_macro_new_from_keycode(button, key, modifiers) < 0) {
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		}
		return;
	}

	/* Several keys: the mouse plays them as a sequence. */
	macro = ratbag_button_macro_new("sequence");
	for (unsigned int i = 0; i < count; i++) {
		const uint8_t *event = &slot[1 + 3 * i];
		unsigned int keycode = m913_event_to_keycode(device, event[0], event[1] | event[2] << 8);

		if (!keycode) {
			ratbag_button_macro_unref(macro);
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
			return;
		}
		ratbag_button_macro_set_event(macro, i,
					      (event[0] & M913_EVENT_DOWN) ?
						RATBAG_MACRO_EVENT_KEY_PRESSED :
						RATBAG_MACRO_EVENT_KEY_RELEASED,
					      keycode);
	}
	ratbag_button_copy_macro(button, macro);
	ratbag_button_macro_unref(macro);
}

/* Reads macro number index from the mouse into the button's action. */
static int
m913_read_macro(struct ratbag_button *button, unsigned int index)
{
	struct ratbag_device *device = button->profile->device;
	uint16_t addr = M913_ADDR_MACROS + M913_MACRO_SIZE * index;
	uint8_t head[M913_MACRO_NAME + 2];
	uint8_t events[M913_MACRO_EVENTS * 5];
	char name[M913_MACRO_NAME];
	struct ratbag_button_macro *macro;
	unsigned int count, step = 0;
	size_t name_len;
	int rc;

	rc = m913_read(device, addr, head, sizeof(head));
	if (rc)
		return rc;

	/* The name is UTF-16LE; keep the ASCII part. */
	name_len = min(head[0], M913_MACRO_NAME - 1) / 2;
	for (size_t i = 0; i < name_len; i++)
		name[i] = (head[2 + 2 * i] == 0 && head[1 + 2 * i] < 0x80) ? (char)head[1 + 2 * i] : '?';
	name[name_len] = '\0';

	count = (unsigned int)(head[M913_MACRO_NAME] << 8 | head[M913_MACRO_NAME + 1]);
	if (count == 0 || count > M913_MACRO_EVENTS)
		return -EINVAL;

	rc = m913_read(device, addr + sizeof(head), events, 5 * count);
	if (rc)
		return rc;

	macro = ratbag_button_macro_new(name_len ? name : "macro");
	for (unsigned int i = 0; i < count; i++) {
		const uint8_t *event = &events[5 * i];
		unsigned int keycode = m913_event_to_keycode(device, event[0], event[1] | event[2] << 8);
		unsigned int delay = (unsigned int)(event[3] << 8 | event[4]);

		/* Mouse buttons in macros have no libratbag equivalent. */
		if (!keycode || step + 2 > MAX_MACRO_EVENTS) {
			ratbag_button_macro_unref(macro);
			return -ENOTSUP;
		}
		ratbag_button_macro_set_event(macro, step++,
					      (event[0] & M913_EVENT_DOWN) ?
						RATBAG_MACRO_EVENT_KEY_PRESSED :
						RATBAG_MACRO_EVENT_KEY_RELEASED,
					      keycode);
		if (delay != M913_MACRO_DEFAULT_DELAY)
			ratbag_button_macro_set_event(macro, step++, RATBAG_MACRO_EVENT_WAIT, delay);
	}
	ratbag_button_copy_macro(button, macro);
	ratbag_button_macro_unref(macro);

	return 0;
}

/* Fills in a button's action from its record; anything libratbag can't
 * express stays unknown. */
static void
m913_read_button(struct ratbag_button *button, const uint8_t *record)
{
	struct ratbag_device *device = button->profile->device;
	uint8_t slot[M913_SLOT_SIZE];
	unsigned int record_index = m913_button_records[button->index];

	button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;

	switch (record[0]) {
	case M913_TYPE_DISABLED:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		break;
	case M913_TYPE_MOUSE:
		for (size_t i = 0; i < ARRAY_LENGTH(m913_mouse_masks); i++) {
			if (record[1] == m913_mouse_masks[i]) {
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = i + 1;
			}
		}
		break;
	case M913_TYPE_DPI:
		button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		switch (record[1]) {
		case M913_DPI_CYCLE:
			button->action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
			break;
		case M913_DPI_UP:
			button->action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP;
			break;
		case M913_DPI_DOWN:
			button->action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN;
			break;
		default:
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		}
		break;
	case M913_TYPE_SEQUENCE:
		if (m913_read(device, M913_ADDR_SLOTS + M913_SLOT_SIZE * record_index, slot, sizeof(slot)) == 0)
			m913_read_sequence(button, slot);
		break;
	case M913_TYPE_MACRO:
		/* The repeat mode (record[2]) has no libratbag equivalent;
		 * a rewritten macro plays once. */
		if (record[1] >= M913_MACRO_COUNT || m913_read_macro(button, record[1]) < 0)
			button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		break;
	default:
		/* Rapid fire, report rate switch, LED toggle: kept as they are
		 * unless the button is changed. */
		log_debug(device->ratbag, "m913: button %u has action %02x %02x %02x\n",
			  button->index, record[0], record[1], record[2]);
		break;
	}
}

/* Reads the active profile's settings into libratbag's profile. */
static int
m913_read_settings(struct ratbag_device *device)
{
	struct m913_data *data = device->drv_data;
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	const uint8_t *settings = data->settings;
	int rc;

	rc = m913_read(device, 0, data->settings, sizeof(data->settings));
	if (rc)
		return rc;

	profile = ratbag_device_get_profile(device, 0);
	profile->is_active = true;

	for (size_t i = 0; i < ARRAY_LENGTH(m913_report_rates); i++)
		if (settings[M913_ADDR_REPORT_RATE] == 1000 / m913_report_rates[i])
			profile->hz = m913_report_rates[i];

	ratbag_profile_for_each_resolution(profile, resolution) {
		uint8_t code = settings[M913_ADDR_DPI + 4 * resolution->index];

		for (size_t i = 0; i < ARRAY_LENGTH(m913_dpi_codes); i++)
			if (m913_dpi_codes[i] == code)
				resolution->dpi_x = resolution->dpi_y = M913_DPI_MIN + M913_DPI_STEP * i;
		resolution->is_active = resolution->index + 1 == settings[M913_ADDR_ACTIVE_DPI];
		resolution->is_default = resolution->is_active;
	}

	ratbag_profile_for_each_button(profile, button) {
		m913_read_button(button, m913_cached_record(data, button));
	}

	ratbag_profile_for_each_led(profile, led) {
		switch (settings[M913_ADDR_LED_MODE]) {
		case M913_LED_STEADY:
			led->mode = RATBAG_LED_ON;
			break;
		case M913_LED_BREATHING:
			led->mode = RATBAG_LED_BREATHING;
			break;
		case M913_LED_NEON:
			led->mode = RATBAG_LED_CYCLE;
			break;
		default:
			led->mode = RATBAG_LED_OFF;
		}
		led->color.red = settings[M913_ADDR_LED_COLOR];
		led->color.green = settings[M913_ADDR_LED_COLOR + 1];
		led->color.blue = settings[M913_ADDR_LED_COLOR + 2];
		led->brightness = settings[M913_ADDR_LED_BRIGHTNESS];
		/* Speed 1 (slowest) to 5 (fastest) as a duration. */
		led->ms = (unsigned int)(M913_LED_SPEED_MAX + 1 -
					 m913_clamp(settings[M913_ADDR_LED_SPEED],
						    M913_LED_SPEED_MIN, M913_LED_SPEED_MAX)) * 1000;
	}

	return 0;
}

/* ---- writing ------------------------------------------------------------- */

/* Writes a button record (type and two parameters) with its checksum. */
static int
m913_write_record(struct ratbag_device *device, unsigned int record_index,
		  uint8_t type, uint8_t p1, uint8_t p2)
{
	uint8_t record[4] = { type, p1, p2, 0 };

	record[3] = m913_checksum(record, 3);
	return m913_write(device, M913_ADDR_RECORDS + 4 * record_index, record, sizeof(record));
}

/* Writes events (kind, code) as the key sequence of record r. */
static int
m913_write_sequence(struct ratbag_device *device, unsigned int record_index,
		    const uint8_t *kinds, const uint16_t *codes, unsigned int count)
{
	uint8_t slot[M913_SLOT_SIZE] = { 0 };
	size_t len = 1 + 3 * count;
	int rc;

	if (count == 0 || count > M913_SLOT_EVENTS) {
		log_error(device->ratbag, "m913: a button takes at most %d key presses and releases, not %u\n",
			  M913_SLOT_EVENTS, count);
		return -EINVAL;
	}

	slot[0] = (uint8_t)count;
	for (unsigned int i = 0; i < count; i++) {
		slot[1 + 3 * i] = kinds[i];
		slot[2 + 3 * i] = codes[i] & 0xff;
		slot[3 + 3 * i] = codes[i] >> 8;
	}
	slot[len] = m913_checksum(slot, len);

	rc = m913_write(device, M913_ADDR_SLOTS + M913_SLOT_SIZE * record_index, slot, len + 1);
	if (rc)
		return rc;

	return m913_write_record(device, record_index, M913_TYPE_SEQUENCE, 0, 0);
}

/* Stores a key with modifiers on the button itself, as a key sequence. */
static int
m913_write_key(struct ratbag_device *device, unsigned int record_index,
	       unsigned int keycode, unsigned int modifiers)
{
	uint8_t kinds[M913_SLOT_EVENTS];
	uint16_t codes[M913_SLOT_EVENTS];
	unsigned int count = 0, modifier_bits[8], modifier_count = 0;
	uint8_t kind;
	uint16_t code;

	if (!m913_keycode_to_event(device, keycode, &kind, &code)) {
		log_error(device->ratbag, "m913: key %u can't be sent by the mouse\n", keycode);
		return -EINVAL;
	}

	for (unsigned int i = 0; i < 8; i++)
		if (modifiers & (1U << i))
			modifier_bits[modifier_count++] = 1U << i;

	if (2 * (modifier_count + 1) > M913_SLOT_EVENTS) {
		log_error(device->ratbag, "m913: a key takes at most 2 modifiers\n");
		return -EINVAL;
	}

	for (unsigned int i = 0; i < modifier_count; i++) {
		kinds[count] = M913_EVENT_MODIFIER;
		codes[count++] = modifier_bits[i];
	}
	kinds[count] = kind;
	codes[count++] = code;
	for (unsigned int i = 0; i < modifier_count; i++) {
		kinds[count] = M913_EVENT_RELEASE(M913_EVENT_MODIFIER);
		codes[count++] = modifier_bits[i];
	}
	kinds[count] = M913_EVENT_RELEASE(kind);
	codes[count++] = code;

	return m913_write_sequence(device, record_index, kinds, codes, count);
}

/* Writes a libratbag macro into macro slot index and points the button at
 * it. */
static int
m913_write_macro(struct ratbag_device *device, unsigned int record_index, unsigned int index,
		 const struct ratbag_macro *macro)
{
	uint8_t buf[M913_MACRO_SIZE] = { 0 };
	uint8_t *events = &buf[M913_MACRO_NAME + 2];
	size_t name_len = 0;
	unsigned int count = 0;
	int rc;

	for (int i = 0; i < MAX_MACRO_EVENTS; i++) {
		const struct ratbag_macro_event *macro_event = &macro->events[i];
		uint8_t kind;
		uint16_t code;

		if (macro_event->type == RATBAG_MACRO_EVENT_NONE ||
		    macro_event->type == RATBAG_MACRO_EVENT_INVALID)
			break;

		if (macro_event->type == RATBAG_MACRO_EVENT_WAIT) {
			unsigned int delay;

			if (count == 0)
				continue;
			uint8_t *delay_bytes = &events[5 * (count - 1) + 3];
			delay = (unsigned int)(delay_bytes[0] << 8 | delay_bytes[1]);
			if (delay == M913_MACRO_DEFAULT_DELAY)
				delay = 0;
			delay = min(delay + macro_event->event.timeout, 0xffffU);
			delay_bytes[0] = delay >> 8;
			delay_bytes[1] = delay & 0xff;
			continue;
		}

		if (count == M913_MACRO_EVENTS) {
			log_error(device->ratbag, "m913: a macro takes at most %delay_bytes key presses and releases\n",
				  M913_MACRO_EVENTS);
			return -EINVAL;
		}
		if (!m913_keycode_to_event(device, macro_event->event.key, &kind, &code)) {
			log_error(device->ratbag, "m913: key %u can't be sent by the mouse\n", macro_event->event.key);
			return -EINVAL;
		}

		uint8_t *event = &events[5 * count++];
		event[0] = macro_event->type == RATBAG_MACRO_EVENT_KEY_PRESSED ? kind : M913_EVENT_RELEASE(kind);
		event[1] = code & 0xff;
		event[2] = code >> 8;
		event[3] = 0;
		event[4] = M913_MACRO_DEFAULT_DELAY;
	}

	if (count == 0)
		return m913_write_record(device, record_index, M913_TYPE_DISABLED, 0, 0);

	/* Name: length byte, then UTF-16LE (ASCII only here). */
	if (macro->name)
		for (; macro->name[name_len] && name_len < (M913_MACRO_NAME - 1) / 2; name_len++)
			buf[1 + 2 * name_len] = (uint8_t)macro->name[name_len];
	buf[0] = (uint8_t)(2 * name_len);

	buf[M913_MACRO_NAME] = count >> 8;
	buf[M913_MACRO_NAME + 1] = count & 0xff;
	buf[M913_MACRO_NAME + 2 + 5 * count] = m913_checksum(&buf[M913_MACRO_NAME], 2 + 5 * count);

	rc = m913_write(device, M913_ADDR_MACROS + M913_MACRO_SIZE * index, buf,
			M913_MACRO_NAME + 3 + 5 * count);
	if (rc)
		return rc;

	return m913_write_record(device, record_index, M913_TYPE_MACRO, (uint8_t)index, 1);
}

/* A macro slot no other button uses, preferring the one this button has. */
static int
m913_macro_slot(struct ratbag_profile *profile, struct ratbag_button *button)
{
	struct m913_data *data = profile->device->drv_data;
	bool used[M913_MACRO_COUNT] = { false };
	struct ratbag_button *other;
	const uint8_t *own_record = m913_cached_record(data, button);

	ratbag_profile_for_each_button(profile, other) {
		const uint8_t *other_record = m913_cached_record(data, other);

		if (other != button && other_record[0] == M913_TYPE_MACRO && other_record[1] < M913_MACRO_COUNT)
			used[other_record[1]] = true;
	}

	if (own_record[0] == M913_TYPE_MACRO && own_record[1] < M913_MACRO_COUNT && !used[own_record[1]])
		return own_record[1];
	for (int i = 0; i < M913_MACRO_COUNT; i++)
		if (!used[i])
			return i;

	return -ENOSPC;
}

/* Writes a changed button's action to the mouse. */
static int
m913_write_button(struct ratbag_profile *profile, struct ratbag_button *button)
{
	struct ratbag_device *device = profile->device;
	struct m913_data *data = device->drv_data;
	const struct ratbag_button_action *action = &button->action;
	unsigned int record_index = m913_button_records[button->index];
	unsigned int key, modifiers;
	int rc, slot;

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		rc = m913_write_record(device, record_index, M913_TYPE_DISABLED, 0, 0);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		if (action->action.button < 1 || action->action.button > ARRAY_LENGTH(m913_mouse_masks))
			return -EINVAL;
		rc = m913_write_record(device, record_index, M913_TYPE_MOUSE,
				       m913_mouse_masks[action->action.button - 1], 0);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		switch (action->action.special) {
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
			rc = m913_write_record(device, record_index, M913_TYPE_DPI, M913_DPI_CYCLE, 0);
			break;
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:
			rc = m913_write_record(device, record_index, M913_TYPE_DPI, M913_DPI_UP, 0);
			break;
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:
			rc = m913_write_record(device, record_index, M913_TYPE_DPI, M913_DPI_DOWN, 0);
			break;
		default:
			return -EINVAL;
		}
		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		rc = m913_write_key(device, record_index, action->action.key, 0);
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		/* A single key with modifiers fits the button itself. */
		if (ratbag_action_keycode_from_macro(action, &key, &modifiers) > 0) {
			rc = m913_write_key(device, record_index, key, modifiers);
			break;
		}
		slot = m913_macro_slot(profile, button);
		if (slot < 0)
			return slot;
		rc = m913_write_macro(device, record_index, (unsigned int)slot, action->macro);
		break;
	default:
		return -EINVAL;
	}

	if (rc)
		return rc;

	/* Keep the cached records current for the macro slot allocation. */
	return m913_read(device, M913_ADDR_RECORDS + 4 * record_index,
			 &data->settings[M913_ADDR_RECORDS + 4 * record_index], 4);
}

/* Writes everything that changed in the profile. */
static int
m913_write_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	int rc;

	if (profile->rate_dirty) {
		rc = m913_write_value(device, M913_ADDR_REPORT_RATE, (uint8_t)(1000 / profile->hz));
		if (rc)
			return rc;
	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		uint8_t level[4];
		unsigned int i;

		if (!resolution->dirty)
			continue;

		i = (resolution->dpi_x - M913_DPI_MIN) / M913_DPI_STEP;
		if (i >= ARRAY_LENGTH(m913_dpi_codes))
			return -EINVAL;
		level[0] = level[1] = m913_dpi_codes[i];
		level[2] = 0;
		level[3] = m913_checksum(level, 3);
		rc = m913_write(device, M913_ADDR_DPI + 4 * resolution->index, level, sizeof(level));
		if (rc)
			return rc;
	}

	ratbag_profile_for_each_resolution(profile, resolution) {
		if (resolution->is_active) {
			rc = m913_write_value(device, M913_ADDR_ACTIVE_DPI, (uint8_t)(resolution->index + 1));
			if (rc)
				return rc;
		}
	}

	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;
		rc = m913_write_button(profile, button);
		if (rc) {
			log_error(device->ratbag, "m913: writing button %u failed: %d\n", button->index, rc);
			return rc;
		}
	}

	ratbag_profile_for_each_led(profile, led) {
		uint8_t color[4] = { led->color.red, led->color.green, led->color.blue, 0 };
		uint8_t mode;

		if (!led->dirty)
			continue;

		switch (led->mode) {
		case RATBAG_LED_ON:
			mode = M913_LED_STEADY;
			break;
		case RATBAG_LED_BREATHING:
			mode = M913_LED_BREATHING;
			break;
		case RATBAG_LED_CYCLE:
			mode = M913_LED_NEON;
			break;
		default:
			mode = M913_LED_OFF;
		}

		color[3] = m913_checksum(color, 3);
		rc = m913_write(device, M913_ADDR_LED_COLOR, color, sizeof(color));
		if (rc)
			return rc;
		rc = m913_write_value(device, M913_ADDR_LED_BRIGHTNESS,
				      (uint8_t)m913_clamp((int)led->brightness, 1, 255));
		if (rc)
			return rc;
		rc = m913_write_value(device, M913_ADDR_LED_SPEED,
				      (uint8_t)m913_clamp(M913_LED_SPEED_MAX + 1 - (int)(led->ms / 1000),
							  M913_LED_SPEED_MIN, M913_LED_SPEED_MAX));
		if (rc)
			return rc;
		rc = m913_write_value(device, M913_ADDR_LED_MODE, mode);
		if (rc)
			return rc;
	}

	return 0;
}

/* ---- driver -------------------------------------------------------------- */

static int
m913_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	unsigned int dpis[ARRAY_LENGTH(m913_dpi_codes)];
	int rc;

	rc = ratbag_find_hidraw(device, m913_test_hidraw);
	if (rc)
		return rc;

	device->drv_data = zalloc(sizeof(struct m913_data));

	ratbag_device_init_profiles(device, M913_NUM_PROFILES, M913_NUM_RESOLUTIONS,
				    M913_NUM_BUTTONS, M913_NUM_LEDS);

	for (size_t i = 0; i < ARRAY_LENGTH(dpis); i++)
		dpis[i] = M913_DPI_MIN + M913_DPI_STEP * i;

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_set_report_rate_list(profile, m913_report_rates,
						    ARRAY_LENGTH(m913_report_rates));

		ratbag_profile_for_each_resolution(profile, resolution)
			ratbag_resolution_set_dpi_list(resolution, dpis, ARRAY_LENGTH(dpis));

		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
			ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
		}

		ratbag_profile_for_each_led(profile, led) {
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
		}
	}

	rc = m913_read_settings(device);
	if (rc) {
		log_error(device->ratbag, "m913: reading the settings failed: %d\n", rc);
		goto err;
	}

	return 0;

err:
	free(device->drv_data);
	device->drv_data = NULL;
	return rc;
}

static int
m913_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		rc = m913_write_profile(profile);
		if (rc)
			return rc;
	}

	return 0;
}

static void
m913_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(device->drv_data);
}

struct ratbag_driver redragon_m913_driver = {
	.name = "Redragon M913",
	.id = "redragon_m913",
	.probe = m913_probe,
	.remove = m913_remove,
	.commit = m913_commit,
};
