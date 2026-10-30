/*
 * Copyright © 2026 Martin Pluskal
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
 * Gaming mice on the Holtek 04d9:a09f controller, sold under many names. This
 * driver was tested on a Rapture VENOM V2 with firmware 3.01, and it refuses
 * other firmware: 3.06 numbers its effects differently and 2.xx has a
 * different USB layout.
 *
 * The configuration lives on interface 2. That interface has no interrupt IN
 * endpoint, so usbhid does not bind it and it has no hidraw node, and the
 * firmware stalls the same requests on interfaces 0 and 1. This driver
 * therefore talks to interface 2 through usbfs, and uses hidraw only to tie
 * one ratbag device to the mouse.
 *
 * Commands are 8-byte feature reports, [cmd, args..., checksum], and bit 7 of
 * cmd makes a read: the next GET_REPORT returns [cmd, values...]. On the wire
 * they are encrypted with a 6-character key that differs from mouse to mouse,
 * and the mouse ignores anything that fails its checksum after decrypting.
 * https://github.com/JConradoN/pcyes-argus-mouse-linux/blob/main/docs/PROTOCOLO_ARGUS.md
 * describes the cipher. The effect numbers, the speed tables and the behaviour
 * of the reply buffer were measured on the VENOM V2.
 *
 * The DPI table can be written but not read: after asking for it the mouse
 * ignores every command until a USB reset, presumably waiting to send it over
 * the IN endpoint it does not have. The driver therefore starts from a table
 * close to what the tested VENOM V2 shipped with. After a restart it shows that
 * table whatever the mouse holds, changing one stage then writes the others as
 * shown, and asking for a value already shown writes nothing. The active stage
 * can be read.
 */

#include "config.h"
#include <errno.h>
#include <fcntl.h>
#include <libudev.h>
#include <limits.h>
#include <linux/usbdevice_fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include "libratbag-hidraw.h"
#include "libratbag-private.h"
#include "libratbag-util.h"
#include "shared-macro.h"

#define HOLTEK_A09F_FIRMWARE 0x0301

#define HOLTEK_A09F_INTERFACE 2
#define HOLTEK_A09F_PACKET_SIZE 8
#define HOLTEK_A09F_KEY_SIZE 6
#define HOLTEK_A09F_EFFECT_SIZE 3
#define HOLTEK_A09F_BLOCK_SIZE 32
#define HOLTEK_A09F_ENDPOINT_OUT 0x03
#define HOLTEK_A09F_TIMEOUT_MS 1000
/* The vendor software leaves this much after a write. */
#define HOLTEK_A09F_COMMAND_DELAY_MS 30
#define HOLTEK_A09F_REPLY_DELAY_MS 5
#define HOLTEK_A09F_REPLY_RETRIES 10

#define HOLTEK_A09F_REQUEST_TYPE_OUT 0x21 /* class, interface, host to device */
#define HOLTEK_A09F_REQUEST_TYPE_IN 0xa1 /* class, interface, device to host */
#define HOLTEK_A09F_FEATURE_REPORT_0 0x0300

#define HOLTEK_A09F_READ 0x80
#define HOLTEK_A09F_CMD_FIRMWARE 0x00
#define HOLTEK_A09F_CMD_POLLING 0x01
#define HOLTEK_A09F_CMD_STAGE 0x0b
#define HOLTEK_A09F_CMD_COLOR 0x0c
#define HOLTEK_A09F_CMD_EFFECT 0x0d
#define HOLTEK_A09F_CMD_DPI_TABLE 0x11

#define HOLTEK_A09F_NUM_PROFILES 1
#define HOLTEK_A09F_NUM_STAGES 7
#define HOLTEK_A09F_NUM_BUTTONS 0
#define HOLTEK_A09F_NUM_LEDS 1

/* The report rate is stored as a divider of this. */
#define HOLTEK_A09F_BASE_RATE 1000
/* Reported as the duration when the mouse has none we can read. */
#define HOLTEK_A09F_DEFAULT_MS 3200
/* What the mouse wants as the second argument of the cycling effect. */
#define HOLTEK_A09F_CYCLE_ARG 0xff

/*
 * A DPI table entry counts steps of 100, and this bit doubles it. The doubling
 * and the order of the steps were measured. The DPI values are nominal: the
 * measured ratios between them were up to 15% lower. An entry of 63 did not
 * take in the one test of it, so the steps stop at 62.
 */
#define HOLTEK_A09F_DPI_STEP 100
#define HOLTEK_A09F_DPI_DOUBLE 0x40
#define HOLTEK_A09F_DPI_MIN 800
#define HOLTEK_A09F_DPI_NATIVE_MAX 6200
#define HOLTEK_A09F_DPI_MAX 12400
#define HOLTEK_A09F_NUM_DPIS \
	((HOLTEK_A09F_DPI_NATIVE_MAX - HOLTEK_A09F_DPI_MIN) / HOLTEK_A09F_DPI_STEP + 1 + \
	 (HOLTEK_A09F_DPI_MAX - HOLTEK_A09F_DPI_NATIVE_MAX) / (2 * HOLTEK_A09F_DPI_STEP))
#define HOLTEK_A09F_DPI_TABLE_SIZE 8

enum holtek_a09f_effect {
	HOLTEK_A09F_EFFECT_OFF = 0x00,
	HOLTEK_A09F_EFFECT_SOLID = 0x01,
	HOLTEK_A09F_EFFECT_BREATHING = 0x02,
	HOLTEK_A09F_EFFECT_CYCLE = 0x03,
};

static const unsigned int holtek_a09f_report_rates[] = { 125, 250, 500, 1000 };

/*
 * One cycle in ms for speed values 0 to 4, measured with a camera. Higher
 * values behave erratically. Speed 0 really takes 13 s and 18 s, more than
 * the 10 s the libratbag API allows.
 */
static const unsigned int holtek_a09f_breathing_ms[] = { 10000, 4800, 3200, 2200, 1800 };
static const unsigned int holtek_a09f_cycle_ms[] = { 10000, 3670, 2000, 1400, 1070 };

/* DPI was only tested on the mouse with this key. Others may have another sensor. */
static const uint8_t holtek_a09f_dpi_key[HOLTEK_A09F_KEY_SIZE] = { 'F', 'B', '1', '1', '7', '8' };

/* Stands in for the table the mouse cannot report: the stages it is sold with. */
static const unsigned int holtek_a09f_default_dpi[HOLTEK_A09F_NUM_STAGES] = {
	800, 1600, 2400, 3200, 4800, 7200, 12000,
};

struct holtek_a09f_data {
	uint8_t key[HOLTEK_A09F_KEY_SIZE];
	bool has_dpi;
	/* The table as far as we know: the mouse cannot report it. */
	unsigned int dpi[HOLTEK_A09F_NUM_STAGES];
	/* A table write did not finish, so the mouse may hold anything. */
	bool dpi_unknown;
	/* The stage libratbag last showed: the DPI button makes it stale. */
	unsigned int stage;
};

/* The replies to the reads of these settings: [cmd, values...]. */
struct holtek_a09f_settings {
	uint8_t color[HOLTEK_A09F_PACKET_SIZE];
	uint8_t effect[HOLTEK_A09F_PACKET_SIZE];
	uint8_t polling[HOLTEK_A09F_PACKET_SIZE];
	uint8_t stage[HOLTEK_A09F_PACKET_SIZE]; /* only with has_dpi */
};

/* Swaps pairs of bytes, so it is its own inverse. */
static const uint8_t holtek_a09f_permutation[HOLTEK_A09F_PACKET_SIZE] = {
	5, 4, 7, 6, 1, 0, 3, 2,
};

static const uint8_t holtek_a09f_offsets[HOLTEK_A09F_PACKET_SIZE] = {
	0x25, 0xf6, 0xe4, 0x76, 0x47, 0x54, 0xe6, 0x76,
};

static void
holtek_a09f_encrypt(const uint8_t *key, const uint8_t *plain, uint8_t *wire)
{
	uint64_t value = 0;

	for (unsigned int i = 0; i < HOLTEK_A09F_PACKET_SIZE; i++) {
		uint8_t byte = plain[holtek_a09f_permutation[i]];

		if (i >= 1 && i <= HOLTEK_A09F_KEY_SIZE)
			byte ^= key[i - 1];

		value = value << 8 | byte;
	}

	value = value << 3 | value >> 61;

	for (unsigned int i = 0; i < HOLTEK_A09F_PACKET_SIZE; i++) {
		uint8_t byte = value >> (56 - 8 * i);

		wire[i] = byte + holtek_a09f_offsets[i];
	}
}

/* Undo the offsets and the rotation: the permuted bytes, with the key on them. */
static void
holtek_a09f_unrotate(const uint8_t *wire, uint8_t *keyed)
{
	uint64_t value = 0;

	for (unsigned int i = 0; i < HOLTEK_A09F_PACKET_SIZE; i++) {
		uint8_t byte = wire[i] - holtek_a09f_offsets[i];

		value = value << 8 | byte;
	}

	value = value >> 3 | value << 61;

	for (unsigned int i = 0; i < HOLTEK_A09F_PACKET_SIZE; i++)
		keyed[i] = value >> (56 - 8 * i);
}

static void
holtek_a09f_decrypt(const uint8_t *key, const uint8_t *wire, uint8_t *plain)
{
	uint8_t keyed[HOLTEK_A09F_PACKET_SIZE];

	holtek_a09f_unrotate(wire, keyed);

	for (unsigned int i = 0; i < HOLTEK_A09F_PACKET_SIZE; i++) {
		uint8_t byte = keyed[i];

		if (i >= 1 && i <= HOLTEK_A09F_KEY_SIZE)
			byte ^= key[i - 1];

		plain[holtek_a09f_permutation[i]] = byte;
	}
}

static int
holtek_a09f_control(int fd, uint8_t request_type, uint8_t request, uint8_t *data)
{
	struct usbdevfs_ctrltransfer transfer = {
		.bRequestType = request_type,
		.bRequest = request,
		.wValue = HOLTEK_A09F_FEATURE_REPORT_0,
		.wIndex = HOLTEK_A09F_INTERFACE,
		.wLength = HOLTEK_A09F_PACKET_SIZE,
		.timeout = HOLTEK_A09F_TIMEOUT_MS,
		.data = data,
	};
	int rc;

	rc = ioctl(fd, USBDEVFS_CONTROL, &transfer);
	if (rc < 0)
		return -errno;

	return rc == HOLTEK_A09F_PACKET_SIZE ? 0 : -EIO;
}

static int
holtek_a09f_get_report(int fd, uint8_t *wire)
{
	return holtek_a09f_control(fd, HOLTEK_A09F_REQUEST_TYPE_IN,
				   HID_REQ_GET_REPORT, wire);
}

static int
holtek_a09f_send(int fd, const uint8_t *key, uint8_t cmd,
		 uint8_t arg1, uint8_t arg2, uint8_t arg3)
{
	uint8_t plain[HOLTEK_A09F_PACKET_SIZE] = { cmd, arg1, arg2, arg3 };
	uint8_t wire[HOLTEK_A09F_PACKET_SIZE];
	uint8_t sum = 0;

	for (unsigned int i = 0; i < HOLTEK_A09F_PACKET_SIZE - 1; i++)
		sum += plain[i];
	plain[HOLTEK_A09F_PACKET_SIZE - 1] = 0xff - sum;

	holtek_a09f_encrypt(key, plain, wire);

	return holtek_a09f_control(fd, HOLTEK_A09F_REQUEST_TYPE_OUT,
				   HID_REQ_SET_REPORT, wire);
}

/*
 * The mouse answers a command with [cmd, ...] in its reply buffer, and leaves
 * the buffer alone if it did not accept the command: -ENODATA.
 */
static int
holtek_a09f_get_reply(int fd, const uint8_t *key, uint8_t cmd, uint8_t *plain)
{
	uint8_t wire[HOLTEK_A09F_PACKET_SIZE];
	int rc;

	for (unsigned int i = 0; i < HOLTEK_A09F_REPLY_RETRIES; i++) {
		msleep(HOLTEK_A09F_REPLY_DELAY_MS);

		rc = holtek_a09f_get_report(fd, wire);
		if (rc)
			return rc;

		holtek_a09f_decrypt(key, wire, plain);
		if (plain[0] == cmd)
			return 0;
	}

	return -ENODATA;
}

/*
 * Read a settings group. Never call this for group 0x09: on a Zelotes C-13,
 * reading it stalls the interface until the mouse is replugged. Nor for groups
 * 0x11 and 0x12, which hold tables, or presumably 0x10: the mouse then ignores
 * every command until a USB reset.
 */
static int
holtek_a09f_read(int fd, const uint8_t *key, uint8_t group, uint8_t *plain)
{
	uint8_t cmd = group | HOLTEK_A09F_READ;
	int rc;

	rc = holtek_a09f_send(fd, key, cmd, 0, 0, 0);
	if (rc)
		return rc;

	return holtek_a09f_get_reply(fd, key, cmd, plain);
}

static int
holtek_a09f_write(int fd, const uint8_t *key, uint8_t cmd,
		  uint8_t arg1, uint8_t arg2, uint8_t arg3)
{
	uint8_t plain[HOLTEK_A09F_PACKET_SIZE];
	int rc;

	rc = holtek_a09f_send(fd, key, cmd, arg1, arg2, arg3);
	if (rc)
		return rc;

	msleep(HOLTEK_A09F_COMMAND_DELAY_MS);

	return holtek_a09f_get_reply(fd, key, cmd, plain);
}

/* USBDEVFS_BULK also does interrupt endpoints. */
static int
holtek_a09f_write_block(int fd, uint8_t *block)
{
	struct usbdevfs_bulktransfer transfer = {
		.ep = HOLTEK_A09F_ENDPOINT_OUT,
		.len = HOLTEK_A09F_BLOCK_SIZE,
		.timeout = HOLTEK_A09F_TIMEOUT_MS,
		.data = block,
	};
	int rc;

	rc = ioctl(fd, USBDEVFS_BULK, &transfer);
	if (rc < 0)
		return -errno;

	if (rc != HOLTEK_A09F_BLOCK_SIZE)
		return -EIO;

	msleep(HOLTEK_A09F_COMMAND_DELAY_MS);

	return 0;
}

static struct udev_device *
holtek_a09f_usb_device(struct ratbag_device *device)
{
	return udev_device_get_parent_with_subsystem_devtype(device->udev_device,
							     "usb", "usb_device");
}

/* The interface is only held while talking to it, so other software can use
 * the mouse the rest of the time. */
static int
holtek_a09f_open(struct ratbag_device *device)
{
	struct udev_device *usb_device = holtek_a09f_usb_device(device);
	const char *devnode;
	unsigned int interface = HOLTEK_A09F_INTERFACE;
	int fd, rc;

	if (!usb_device)
		return -ENODEV;

	devnode = udev_device_get_devnode(usb_device);
	if (!devnode)
		return -ENODEV;

	fd = ratbag_open_path(device, devnode, O_RDWR);
	if (fd < 0)
		return -errno;

	if (ioctl(fd, USBDEVFS_CLAIMINTERFACE, &interface) < 0) {
		rc = -errno;
		log_error(device->ratbag, "Cannot claim the configuration interface: %s\n",
			  strerror(-rc));
		ratbag_close_fd(device, fd);
		return rc;
	}

	return fd;
}

static void
holtek_a09f_close(struct ratbag_device *device, int fd)
{
	unsigned int interface = HOLTEK_A09F_INTERFACE;

	if (ioctl(fd, USBDEVFS_RELEASEINTERFACE, &interface) < 0)
		log_debug(device->ratbag, "Cannot release the configuration interface\n");

	ratbag_close_fd(device, fd);
}

static int
holtek_a09f_get_firmware(struct ratbag_device *device, unsigned int *firmware)
{
	struct udev_device *usb_device = holtek_a09f_usb_device(device);
	const char *bcd_device;

	if (!usb_device)
		return -ENODEV;

	bcd_device = udev_device_get_sysattr_value(usb_device, "bcdDevice");
	if (!bcd_device || sscanf(bcd_device, "%x", firmware) != 1)
		return -ENODEV;

	return 0;
}

/* The firmware version the mouse reports is the one it has on USB. */
static int
holtek_a09f_check_key(int fd, const uint8_t *key, unsigned int firmware)
{
	uint8_t plain[HOLTEK_A09F_PACKET_SIZE];
	int rc;

	rc = holtek_a09f_read(fd, key, HOLTEK_A09F_CMD_FIRMWARE, plain);
	if (rc)
		return rc;

	return plain[1] == firmware >> 8 && plain[2] == (firmware & 0xff) ? 0 : -ENODATA;
}

static bool
holtek_a09f_is_key_char(uint8_t c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F');
}

/*
 * The reply buffer gives the key away, because what it holds is encrypted like
 * everything else. A reply to a read can only be fetched once and leaves zeros
 * behind, so the first GET_REPORT clears any reply still waiting. A write
 * leaves [cmd, 0...] until the next read, and cmd sits under key[4], so that
 * byte is only known once cmd is: try them. A wrong guess fails the checksum
 * and the mouse ignores it. As key characters are ASCII, every guess keeps the
 * command's top bit, so no guess can decode to a write. The ack of a DPI table
 * write adds 01 and the table size, and later writes keep them, which a commit
 * that fails halfway leaves behind.
 */
static int
holtek_a09f_find_key(struct ratbag_device *device, int fd, unsigned int firmware,
		     uint8_t *key)
{
	uint8_t wire[HOLTEK_A09F_PACKET_SIZE];
	uint8_t keyed[HOLTEK_A09F_PACKET_SIZE];
	bool plausible;
	int rc;

	for (unsigned int i = 0; i < 2; i++) {
		rc = holtek_a09f_get_report(fd, wire);
		if (rc)
			return rc;
	}

	holtek_a09f_unrotate(wire, keyed);

	if (keyed[7] == 0x01) {
		keyed[7] = 0;
		keyed[1] ^= HOLTEK_A09F_DPI_TABLE_SIZE;
	}

	memcpy(key, &keyed[1], HOLTEK_A09F_KEY_SIZE);

	plausible = keyed[0] == 0 && keyed[7] == 0;
	for (unsigned int i = 0; i < HOLTEK_A09F_KEY_SIZE; i++) {
		if (i != 4 && !holtek_a09f_is_key_char(key[i]))
			plausible = false;
	}

	for (unsigned int cmd = 0; plausible && cmd < HOLTEK_A09F_READ; cmd++) {
		key[4] = keyed[5] ^ cmd;
		if (!holtek_a09f_is_key_char(key[4]))
			continue;

		rc = holtek_a09f_check_key(fd, key, firmware);
		if (rc != -ENODATA)
			return rc;
	}

	log_error(device->ratbag, "Cannot find this mouse's key\n");

	return -ENODEV;
}

static unsigned int
holtek_a09f_speed_from_ms(const unsigned int *table, size_t len, unsigned int ms)
{
	unsigned int best = 0, best_distance = UINT_MAX;

	for (unsigned int i = 0; i < len; i++) {
		unsigned int distance = table[i] > ms ? table[i] - ms : ms - table[i];

		if (distance < best_distance) {
			best = i;
			best_distance = distance;
		}
	}

	return best;
}

static int
holtek_a09f_read_settings(int fd, const struct holtek_a09f_data *data,
			  struct holtek_a09f_settings *settings)
{
	int rc;

	rc = holtek_a09f_read(fd, data->key, HOLTEK_A09F_CMD_COLOR, settings->color);
	if (rc)
		return rc;

	rc = holtek_a09f_read(fd, data->key, HOLTEK_A09F_CMD_EFFECT, settings->effect);
	if (rc)
		return rc;

	rc = holtek_a09f_read(fd, data->key, HOLTEK_A09F_CMD_POLLING, settings->polling);
	if (rc || !data->has_dpi)
		return rc;

	rc = holtek_a09f_read(fd, data->key, HOLTEK_A09F_CMD_STAGE, settings->stage);
	if (rc)
		return rc;

	if (settings->stage[1] < 1 || settings->stage[1] > HOLTEK_A09F_NUM_STAGES)
		settings->stage[1] = 1;

	return 0;
}

static void
holtek_a09f_led_from_settings(struct ratbag_led *led,
			      const struct holtek_a09f_settings *settings)
{
	unsigned int effect = settings->effect[1];
	unsigned int value = settings->effect[2];

	led->color.red = settings->color[1];
	led->color.green = settings->color[2];
	led->color.blue = settings->color[3];
	led->brightness = 255;
	led->ms = HOLTEK_A09F_DEFAULT_MS;

	switch (effect) {
	case HOLTEK_A09F_EFFECT_OFF:
		led->mode = RATBAG_LED_OFF;
		break;
	case HOLTEK_A09F_EFFECT_SOLID:
		led->mode = RATBAG_LED_ON;
		led->brightness = value;
		break;
	case HOLTEK_A09F_EFFECT_BREATHING:
		led->mode = RATBAG_LED_BREATHING;
		if (value < ARRAY_LENGTH(holtek_a09f_breathing_ms))
			led->ms = holtek_a09f_breathing_ms[value];
		break;
	case HOLTEK_A09F_EFFECT_CYCLE:
		led->mode = RATBAG_LED_CYCLE;
		if (value < ARRAY_LENGTH(holtek_a09f_cycle_ms))
			led->ms = holtek_a09f_cycle_ms[value];
		break;
	default:
		/* The other effects are animations libratbag has no name for. */
		led->mode = RATBAG_LED_CYCLE;
		break;
	}
}

static int
holtek_a09f_effect_from_led(const struct ratbag_led *led, uint8_t *effect)
{
	memset(effect, 0, HOLTEK_A09F_EFFECT_SIZE);

	switch (led->mode) {
	case RATBAG_LED_OFF:
		effect[0] = HOLTEK_A09F_EFFECT_OFF;
		break;
	case RATBAG_LED_ON:
		effect[0] = HOLTEK_A09F_EFFECT_SOLID;
		effect[1] = led->brightness;
		break;
	case RATBAG_LED_BREATHING:
		effect[0] = HOLTEK_A09F_EFFECT_BREATHING;
		effect[1] = holtek_a09f_speed_from_ms(holtek_a09f_breathing_ms,
						      ARRAY_LENGTH(holtek_a09f_breathing_ms),
						      led->ms);
		break;
	case RATBAG_LED_CYCLE:
		effect[0] = HOLTEK_A09F_EFFECT_CYCLE;
		effect[1] = holtek_a09f_speed_from_ms(holtek_a09f_cycle_ms,
						      ARRAY_LENGTH(holtek_a09f_cycle_ms),
						      led->ms);
		effect[2] = HOLTEK_A09F_CYCLE_ARG;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static unsigned int
holtek_a09f_rate_from_divider(struct ratbag_device *device, unsigned int divider)
{
	for (unsigned int i = 0; i < ARRAY_LENGTH(holtek_a09f_report_rates); i++) {
		if (HOLTEK_A09F_BASE_RATE / holtek_a09f_report_rates[i] == divider)
			return holtek_a09f_report_rates[i];
	}

	log_info(device->ratbag, "Unknown report rate divider %u\n", divider);

	return HOLTEK_A09F_BASE_RATE;
}

static void
holtek_a09f_init_resolutions(struct ratbag_profile *profile,
			     const struct holtek_a09f_data *data)
{
	struct ratbag_resolution *resolution;
	unsigned int dpis[HOLTEK_A09F_NUM_DPIS];
	size_t ndpis = 0;

	for (unsigned int dpi = HOLTEK_A09F_DPI_MIN; dpi <= HOLTEK_A09F_DPI_NATIVE_MAX;
	     dpi += HOLTEK_A09F_DPI_STEP)
		dpis[ndpis++] = dpi;

	for (unsigned int dpi = HOLTEK_A09F_DPI_NATIVE_MAX + 2 * HOLTEK_A09F_DPI_STEP;
	     dpi <= HOLTEK_A09F_DPI_MAX; dpi += 2 * HOLTEK_A09F_DPI_STEP)
		dpis[ndpis++] = dpi;

	ratbag_profile_for_each_resolution(profile, resolution) {
		unsigned int dpi = data->dpi[resolution->index];

		ratbag_resolution_set_dpi_list(resolution, dpis, ndpis);
		ratbag_resolution_set_resolution(resolution, dpi, dpi);
		resolution->is_active = resolution->index + 1 == data->stage;
		resolution->is_default = resolution->is_active;
	}
}

static int
holtek_a09f_test_hidraw(struct ratbag_device *device)
{
	/* Interface 0 has no report IDs, interface 1 has several. */
	return ratbag_hidraw_has_report(device, 0);
}

static int
holtek_a09f_probe(struct ratbag_device *device)
{
	struct holtek_a09f_data *data;
	struct ratbag_profile *profile;
	struct ratbag_led *led;
	struct holtek_a09f_settings settings;
	unsigned int firmware;
	char version[16];
	int fd, rc;

	rc = ratbag_find_hidraw(device, holtek_a09f_test_hidraw);
	if (rc)
		return rc;

	rc = holtek_a09f_get_firmware(device, &firmware);
	if (rc)
		goto err_hidraw;

	if (firmware != HOLTEK_A09F_FIRMWARE) {
		log_info(device->ratbag,
			 "Firmware %x.%02x is not supported: it may need other effect numbers, please file an issue\n",
			 firmware >> 8, firmware & 0xff);
		rc = -ENODEV;
		goto err_hidraw;
	}

	data = zalloc(sizeof(*data));

	fd = holtek_a09f_open(device);
	if (fd < 0) {
		rc = fd;
		goto err_data;
	}

	rc = holtek_a09f_find_key(device, fd, firmware, data->key);
	if (rc)
		goto err_fd;

	data->has_dpi = memcmp(data->key, holtek_a09f_dpi_key, sizeof(data->key)) == 0;
	memcpy(data->dpi, holtek_a09f_default_dpi, sizeof(data->dpi));

	rc = holtek_a09f_read_settings(fd, data, &settings);
	if (rc)
		goto err_fd;

	holtek_a09f_close(device, fd);

	ratbag_set_drv_data(device, data);

	snprintf(version, sizeof(version), "%x.%02x", firmware >> 8, firmware & 0xff);
	ratbag_device_set_firmware_version(device, version);

	ratbag_device_init_profiles(device,
				    HOLTEK_A09F_NUM_PROFILES,
				    data->has_dpi ? HOLTEK_A09F_NUM_STAGES : 0,
				    HOLTEK_A09F_NUM_BUTTONS,
				    HOLTEK_A09F_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;
		ratbag_profile_set_report_rate_list(profile, holtek_a09f_report_rates,
						    ARRAY_LENGTH(holtek_a09f_report_rates));
		profile->hz = holtek_a09f_rate_from_divider(device, settings.polling[1]);

		if (data->has_dpi) {
			data->stage = settings.stage[1];
			holtek_a09f_init_resolutions(profile, data);
		}

		ratbag_profile_for_each_led(profile, led) {
			led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
			holtek_a09f_led_from_settings(led, &settings);
		}
	}

	return 0;

err_fd:
	holtek_a09f_close(device, fd);
err_data:
	free(data);
err_hidraw:
	ratbag_close_hidraw(device);
	return rc;
}

static int
holtek_a09f_write_report_rate(struct ratbag_device *device, int fd, const uint8_t *key,
			      const struct holtek_a09f_settings *settings, unsigned int hz)
{
	for (unsigned int i = 0; i < ARRAY_LENGTH(holtek_a09f_report_rates); i++) {
		unsigned int divider;

		if (holtek_a09f_report_rates[i] != hz)
			continue;

		divider = HOLTEK_A09F_BASE_RATE / hz;
		if (divider == settings->polling[1])
			return 0;

		return holtek_a09f_write(fd, key, HOLTEK_A09F_CMD_POLLING, divider, 0, 0);
	}

	log_error(device->ratbag, "Report rate %u is not supported\n", hz);

	return -EINVAL;
}

/*
 * An effect write blacks the LED out for 4 s, so write only what differs from
 * what the mouse has. An animation libratbag has no name for reads as cycle,
 * and a speed outside the tables as the default duration. Either stays while
 * the request has the duration they read as, so a colour change keeps them
 * unless libratbag holds an older duration. A colour write applies by itself.
 * Only the solid effect has a brightness; the colour goes through as it is for
 * the others, as the mouse could not hand a scaled colour back as a brightness.
 */
static int
holtek_a09f_write_led(int fd, const uint8_t *key,
		      const struct holtek_a09f_settings *settings,
		      const struct ratbag_led *led)
{
	struct ratbag_led current;
	uint8_t color[3] = { led->color.red, led->color.green, led->color.blue };
	uint8_t current_effect[HOLTEK_A09F_EFFECT_SIZE];
	uint8_t effect[HOLTEK_A09F_EFFECT_SIZE];
	bool inexact;
	int rc;

	holtek_a09f_led_from_settings(&current, settings);

	rc = holtek_a09f_effect_from_led(&current, current_effect);
	if (rc)
		return rc;

	rc = holtek_a09f_effect_from_led(led, effect);
	if (rc)
		return rc;

	inexact = settings->effect[1] != current_effect[0] ||
		  settings->effect[2] != current_effect[1];

	if (memcmp(color, &settings->color[1], sizeof(color)) != 0) {
		rc = holtek_a09f_write(fd, key, HOLTEK_A09F_CMD_COLOR,
				       color[0], color[1], color[2]);
		if (rc)
			return rc;
	}

	if (memcmp(effect, current_effect, sizeof(effect)) != 0 ||
	    (inexact && led->ms != current.ms))
		return holtek_a09f_write(fd, key, HOLTEK_A09F_CMD_EFFECT,
					 effect[0], effect[1], effect[2]);

	return 0;
}

static uint8_t
holtek_a09f_raw_from_dpi(unsigned int dpi)
{
	if (dpi <= HOLTEK_A09F_DPI_NATIVE_MAX)
		return dpi / HOLTEK_A09F_DPI_STEP;

	return HOLTEK_A09F_DPI_DOUBLE | dpi / (2 * HOLTEK_A09F_DPI_STEP);
}

static int
holtek_a09f_write_dpi_table(int fd, const uint8_t *key, struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	uint8_t block[HOLTEK_A09F_BLOCK_SIZE] = { 0 };
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution)
		block[resolution->index] = holtek_a09f_raw_from_dpi(resolution->dpi_x);

	rc = holtek_a09f_write(fd, key, HOLTEK_A09F_CMD_DPI_TABLE, HOLTEK_A09F_NUM_STAGES,
			       HOLTEK_A09F_DPI_TABLE_SIZE, 0);
	if (rc)
		return rc;

	return holtek_a09f_write_block(fd, block);
}

/*
 * The table is only written when a value changed, compared with what we know
 * of it, so that selecting a stage does not overwrite a table libratbag did not
 * see. A table write that did not finish is repeated, as the mouse cannot say
 * what it holds.
 *
 * Every table written so far was followed by a stage write, and that goes to
 * the stage the mouse is on, as the DPI button moves it behind libratbag. A
 * client counts as having moved the stage if the active resolution is a new
 * one, or is dirty with its DPI unchanged. libratbag cannot tell more, so this
 * can be wrong while the stage is stale: a resolution selected again and given
 * a new DPI at once is taken for a new DPI alone, and setting the default on
 * the active resolution, undoing a DPI change, or retrying a commit that wrote
 * the stage and then failed, is taken for a selection.
 */
static int
holtek_a09f_write_resolutions(int fd, struct holtek_a09f_data *data,
			      const struct holtek_a09f_settings *settings,
			      struct ratbag_profile *profile)
{
	struct ratbag_resolution *resolution;
	unsigned int stage = settings->stage[1], active = data->stage;
	bool dirty = false, table_changed = data->dpi_unknown;
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		bool changed = resolution->dpi_x != data->dpi[resolution->index];

		if (resolution->dirty)
			dirty = true;
		if (changed)
			table_changed = true;

		if (resolution->is_active) {
			active = resolution->index + 1;
			if (active != data->stage || (resolution->dirty && !changed))
				stage = active;
		}
	}

	if (!dirty)
		return 0;

	if (table_changed) {
		data->dpi_unknown = true;

		rc = holtek_a09f_write_dpi_table(fd, data->key, profile);
		if (rc)
			return rc;
	} else if (stage == settings->stage[1]) {
		data->stage = active;
		return 0;
	}

	rc = holtek_a09f_write(fd, data->key, HOLTEK_A09F_CMD_STAGE, stage, 0, 0);
	if (rc)
		return rc;

	ratbag_profile_for_each_resolution(profile, resolution)
		data->dpi[resolution->index] = resolution->dpi_x;

	data->dpi_unknown = false;
	data->stage = active;

	return 0;
}

static int
holtek_a09f_commit(struct ratbag_device *device)
{
	struct holtek_a09f_data *data = ratbag_get_drv_data(device);
	struct holtek_a09f_settings settings;
	struct ratbag_profile *profile;
	struct ratbag_led *led;
	uint8_t plain[HOLTEK_A09F_PACKET_SIZE];
	int fd, rc;

	fd = holtek_a09f_open(device);
	if (fd < 0)
		return fd;

	rc = holtek_a09f_read_settings(fd, data, &settings);
	if (rc)
		goto out;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		if (profile->rate_dirty) {
			rc = holtek_a09f_write_report_rate(device, fd, data->key,
							   &settings, profile->hz);
			if (rc)
				goto out;
		}

		if (data->has_dpi) {
			rc = holtek_a09f_write_resolutions(fd, data, &settings, profile);
			if (rc)
				goto out;
		}

		ratbag_profile_for_each_led(profile, led) {
			if (!led->dirty)
				continue;

			rc = holtek_a09f_write_led(fd, data->key, &settings, led);
			if (rc)
				goto out;
		}
	}

	/* A fetched reply leaves zeros behind, which saves the next probe from guessing. */
	if (holtek_a09f_read(fd, data->key, HOLTEK_A09F_CMD_FIRMWARE, plain))
		log_debug(device->ratbag, "Cannot read the firmware version\n");

out:
	holtek_a09f_close(device, fd);

	return rc;
}

static void
holtek_a09f_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver holtek_a09f_driver = {
	.name = "Holtek 04d9:a09f",
	.id = "holtek_a09f",
	.probe = holtek_a09f_probe,
	.remove = holtek_a09f_remove,
	.commit = holtek_a09f_commit,
};
