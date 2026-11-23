/*
 * Copyright © 2026 Padre Adamo
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
 * Driver for the SOAI C18 gaming mouse (USB 12c9:2003).
 *
 * Wire protocol fully reverse-engineered from live USB capture; see
 * https://github.com/<user>/mouse (PROTOCOL.md) for the raw evidence this
 * driver is built from. Summary of the parts relevant to this file:
 *
 * - Every config change is wrapped in a transaction: an 8-byte "begin"
 *   command, one or more command/data packets, and an 8-byte "select
 *   profile / commit" command that both applies the change and closes the
 *   transaction.
 * - 8-byte control commands go out as an HID SET_REPORT Feature request
 *   with report ID 0 (unnumbered report); checksum = 0xFF - sum(byte[0..6]),
 *   i.e. all 8 bytes always sum to 0xFF.
 * - Larger 64-byte payloads (LED, DPI table, button table, macro content)
 *   go out on the interrupt OUT endpoint, also as an unnumbered report.
 * - The device has no readback of actual DPI/button/profile *table
 *   contents* - every write replaces a whole table. (It does have a
 *   generic command-echo/status counter on GET_REPORT, discovered during
 *   macro-playback debugging, but that's not a way to read back
 *   configuration - see PROTOCOL.md.) This driver therefore caches each
 *   profile's full DPI/button tables in drv_data, seeded from known
 *   factory-default templates, and always writes the complete table
 *   on commit - never a partial/single-field update.
 */

#include "config.h"

#include <assert.h>
#include <errno.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define C18_NUM_PROFILES	2
#define C18_NUM_BUTTONS		11
#define C18_NUM_RESOLUTIONS	5
#define C18_NUM_LEDS		1

/* Opcodes for the 8-byte control commands (see PROTOCOL.md) */
#define C18_OP_BEGIN			0x01
#define C18_OP_SELECT_COMMIT		0x02
#define C18_OP_SET_POLLING		0x03
#define C18_OP_SET_DPI_STAGE		0x04
#define C18_OP_PREPARE_TABLE		0x0c /* shared by LED and DPI-table writes */
#define C18_OP_PREPARE_BUTTON_TABLE	0x0d
#define C18_OP_PREPARE_EXT_DATA		0x0f /* macro content upload trigger */

/* Only 1-2 packet macros (<=48 events) have been verified against real
 * hardware - see PROTOCOL.md's "Multi-packet macros" open item. Cap set
 * generously above that to leave room, not because more is confirmed safe.
 */
#define C18_MAX_MACRO_KEYS	96
#define C18_MACRO_BUF_LEN	(2 + C18_MAX_MACRO_KEYS * 4)

/* Base ID handed out for driver-created macros; anything below this is left
 * alone in case the vendor Windows software previously assigned low IDs to
 * this profile (we have no way to read what, if anything, it used - see
 * PROTOCOL.md's "Macro ID assignment" section: the device does zero
 * validation, any unused ID works, this is purely a host-side convention).
 */
#define C18_MACRO_ID_BASE	0x10

/* Settle delay after each 64-byte write specifically during macro-content
 * upload (button-table write, macro content packet(s), and their ack
 * packets). ratbag_hidraw_output_report() goes through the kernel hidraw
 * write() syscall for the interrupt OUT report, which - unlike
 * libusb_interrupt_transfer() (blocking, waits for the actual USB transfer
 * to complete) - does not guarantee the packet has finished going out on
 * the wire before it returns. Confirmed via hardware testing: a
 * byte-for-byte identical macro-upload transaction plays back correctly
 * over raw libusb with only 20-40ms between packets, but silently fails
 * over this driver's hidraw transport with the same delays - only works
 * once the delay around macro-content packets specifically is increased to
 * this value. Not narrowed down further than "large enough to reliably
 * work" - a smaller sufficient value likely exists but wasn't found.
 */
#define C18_MACRO_SETTLE_MS	200

/* c18ctl.c's proven-working STATIC LED template (also the shared header for
 * the DPI-table packet - see PROTOCOL.md's note on the shared 64-byte
 * "config packet" format/pipeline reused across LED and DPI subsystems).
 */
static const uint8_t c18_base_payload[64] = {
	0x0f,0x00,0x1e,0x0a,0x19,0x19,0x05,0x01,0x01,0x03,0x64,0x64,0x01,0xc0,0xf0,0x03,
	0x01,0x01,0x19,0x19,0x22,0x39,0x5c,0xd0,0xf3,0x5c,0x73,0x00,0x22,0x39,0x5c,0xd0,
	0xf3,0x5c,0x73,0x00,0x1f,0xff,0x00,0x00,
	0x00,0x00,0x31, 0x00,0x00,0x31, 0x00,0x00,0x31, 0x00,0x00,0x31,
	0x00,0x00,0x31, 0x00,0x00,0x31, 0x00,0x00,0x31, 0x00,0x00,0x31
};

/* Fixed 8-triplet palette observed identically across every non-static
 * effect capture (breathing/flowing/wave/neon at all speeds); used as the
 * tail for those modes since custom coloring hasn't been confirmed to work
 * there.
 */
static const uint8_t c18_effect_tail_default[24] = {
	0x31,0x00,0x00, 0x00,0x31,0x00, 0x00,0x00,0x31, 0x31,0x31,0x00,
	0x31,0x00,0x31, 0x00,0x31,0x31, 0x31,0x31,0x31, 0x31,0x31,0x31
};

/* Per-profile factory-default DPI table (script-verified from a Restore
 * Defaults capture). Bytes 7-9/40-63 are confirmed fixed-but-undecoded
 * per-profile constants (see PROTOCOL.md) - preserved as-is, never
 * reconstructed.
 */
static const uint8_t c18_dpi_default[C18_NUM_PROFILES][64] = {
	{
		0x0f,0x00,0x1e,0x0a,0x19,0x19,0x05,0x04,0x06,0x10,0x64,0x64,0x01,0xc0,0xf0,0x03,
		0x01,0x01,0x19,0x19,0x22,0x39,0x5c,0xd0,0xf3,0x5c,0x73,0x00,0x22,0x39,0x5c,0xd0,
		0xf3,0x5c,0x73,0x00,0x1f,0xff,0x00,0x00,0x31,0x00,0x00,0x00,0x31,0x00,0x00,0x00,
		0x31,0x31,0x31,0x00,0x31,0x00,0x31,0x00,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31
	},
	{
		0x0f,0x00,0x1e,0x0a,0x19,0x19,0x05,0x01,0x01,0x03,0x64,0x64,0x01,0xc0,0xf0,0x03,
		0x01,0x01,0x19,0x19,0x22,0x39,0x5c,0xd0,0xf3,0x5c,0x73,0x00,0x22,0x39,0x5c,0xd0,
		0xf3,0x5c,0x73,0x00,0x1f,0xff,0x00,0x00,0x00,0x00,0x31,0x00,0x00,0x31,0x00,0x00,
		0x31,0x00,0x00,0x31,0x00,0x00,0x31,0x00,0x00,0x31,0x00,0x00,0x31,0x00,0x00,0x31
	},
};

/* Byte offsets of each DPI stage's X/Y code within the DPI table packet,
 * indexed 0-4 for ratbag's 0-based resolution->index (device stage 1-5).
 */
static const struct { int x_off, y_off; } c18_dpi_stage_offsets[C18_NUM_RESOLUTIONS] = {
	{20, 28}, {21, 29}, {22, 30}, {23, 31}, {24, 32},
};

/* DPI value -> device byte code. Non-linear sensor register encoding; use
 * linear interpolation between confirmed points.
 */
static const struct { int dpi; uint8_t byte; } c18_dpi_calib[] = {
	{200,0x04},{300,0x06},{400,0x08},{600,0x0d},{800,0x12},{1200,0x1b},{1600,0x24},
	{2000,0x2e},{2300,0x34},{2400,0x37},{2600,0x3b},{3200,0x49},{4800,0x6f},
	{6400,0xc9},{6600,0xcc},{10000,0xf3},
};

static uint8_t
c18_dpi_to_byte(int dpi)
{
	unsigned i;
	int d0, d1, b0, b1;

	if (dpi < c18_dpi_calib[0].dpi)
		dpi = c18_dpi_calib[0].dpi;
	if (dpi > c18_dpi_calib[ARRAY_LENGTH(c18_dpi_calib) - 1].dpi)
		dpi = c18_dpi_calib[ARRAY_LENGTH(c18_dpi_calib) - 1].dpi;

	for (i = 0; i < ARRAY_LENGTH(c18_dpi_calib) - 1; i++) {
		d0 = c18_dpi_calib[i].dpi;
		d1 = c18_dpi_calib[i + 1].dpi;
		if (dpi >= d0 && dpi <= d1) {
			b0 = c18_dpi_calib[i].byte;
			b1 = c18_dpi_calib[i + 1].byte;
			if (d0 == d1)
				return (uint8_t)b0;
			return (uint8_t)(b0 + (dpi - d0) * (b1 - b0) / (d1 - d0));
		}
	}
	return c18_dpi_calib[ARRAY_LENGTH(c18_dpi_calib) - 1].byte;
}

/* Default button table (script-verified from a real capture, cross-checked
 * against the vendor software's default-layout diagram, all 11 buttons).
 * Button 11 sits out of numeric sequence, between slot 6 and slot 7; bytes
 * 28-31/48-63 are a fixed/reserved trailer.
 */
static const uint8_t c18_button_default_table[64] = {
	0x01,0x00,0xf0,0x00,0x01,0x00,0xf1,0x00,0x01,0x00,0xf2,0x00,0x01,0x00,0xf4,0x00,
	0x01,0x00,0xf3,0x00,0x07,0x00,0x03,0x00,0x00,0x00,0x2c,0x00,0x00,0x00,0x00,0x00,
	0x00,0x00,0x1a,0x00,0x00,0x00,0x16,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x07,0x00,
	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x01,0x00,0x04,0x00,0x02,0x00
};

/* Byte offset of a button slot's [type LE][value LE] pair, slot is 1-11. */
static int
c18_button_slot_offset(unsigned int slot)
{
	switch (slot) {
	case  1: return  0;
	case  2: return  4;
	case  3: return  8;
	case  4: return 12;
	case  5: return 16;
	case  6: return 20;
	case 11: return 24;
	case  7: return 32;
	case  8: return 36;
	case  9: return 40;
	case 10: return 44;
	default: return -1;
	}
}

struct c18_macro {
	bool used;
	uint8_t id;
	unsigned int button_index;
	uint8_t data[C18_MACRO_BUF_LEN];
	unsigned int len;
};

struct c18_data {
	uint8_t dpi_table[C18_NUM_PROFILES][64];
	uint8_t button_table[C18_NUM_PROFILES][64];
	struct c18_macro macros[C18_NUM_PROFILES][C18_NUM_BUTTONS];
	uint8_t next_macro_id[C18_NUM_PROFILES];
};

/* ---- wire helpers ----
 *
 * The device uses HID report ID 0 (unnumbered reports) for both the 8-byte
 * control commands and the 64-byte endpoint writes. The kernel hidraw ABI
 * requires a dummy leading 0x00 "report number" byte ahead of an unnumbered
 * report's actual content for both HIDIOCSFEATURE and write() - the kernel
 * strips it before putting the report on the wire. So every helper here
 * allocates a buffer one byte larger than the real payload, with buf[0] = 0
 * and the real content starting at buf[1]. Get this wrong and every write
 * is silently misaligned - this was verified against c18ctl.c's raw-libusb
 * behavior, which sends exactly these bytes with no ID at all on the wire.
 */

static uint8_t
c18_checksum8(const uint8_t b[7])
{
	unsigned int sum = 0;
	int i;

	for (i = 0; i < 7; i++)
		sum += b[i];
	return (uint8_t)(0xFF - (sum & 0xFF));
}

static void
c18_build_cmd(uint8_t out[8], uint8_t opcode, uint8_t b1, uint8_t b2)
{
	out[0] = opcode;
	out[1] = b1;
	out[2] = b2;
	out[3] = out[4] = out[5] = out[6] = 0x00;
	out[7] = c18_checksum8(out);
}

static int
c18_send_cmd(struct ratbag_device *device, uint8_t opcode, uint8_t b1, uint8_t b2)
{
	uint8_t buf[8];
	int rc;

	c18_build_cmd(buf, opcode, b1, b2);
	/* The device's Feature report is genuinely unnumbered (no Report ID
	 * item in its descriptor) - buf[0] (the opcode) IS the first real
	 * byte on the wire, not a placeholder. ratbag_hidraw_raw_request()
	 * always does buf[0] = reportnum before the ioctl, so reportnum must
	 * equal buf[0] itself to leave it unchanged - same pattern as
	 * driver-gskill.c's GSKILL_GENERAL_CMD usage. An earlier version of
	 * this code prepended a synthetic 0x00 report-number byte (the usual
	 * hidraw convention for unnumbered devices) and got an immediate
	 * EPIPE/STALL from the device - that convention doesn't apply here.
	 */
	rc = ratbag_hidraw_raw_request(device, opcode, buf, sizeof(buf),
					HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	msleep(20);
	if (rc < 0) {
		log_error(device->ratbag,
			  "c18: SET_REPORT feature request failed for opcode 0x%02x: %s (%d)\n",
			  opcode, strerror(-rc), rc);
		return rc;
	}
	if (rc != (int)sizeof(buf)) {
		log_error(device->ratbag,
			  "c18: SET_REPORT feature request for opcode 0x%02x wrote %d/%zu bytes\n",
			  opcode, rc, sizeof(buf));
		return -EIO;
	}
	return 0;
}

static int
c18_output64(struct ratbag_device *device, const uint8_t payload[64])
{
	uint8_t buf[65];
	int rc;

	/* Root-caused via a real usbmon capture: ratbag_hidraw_output_report()
	 * is a plain write(), and the kernel's hidraw write() path strips
	 * buf[0] as an (unnumbered-report) ID placeholder whenever it is
	 * literally 0x00 - but passes a nonzero buf[0] through untouched. This
	 * device's payloads that never start with 0x00 (button table, DPI/LED,
	 * primer) were never affected and have worked correctly all along.
	 * But the macro protocol's own "00 01" content header, and the
	 * all-zero ack packets, DO start with 0x00 - so the kernel silently
	 * stripped that real leading byte before it ever reached the wire,
	 * corrupting the macro header into garbage the device would then
	 * reject/ignore (confirmed: usbmon showed a genuine 63-byte URB, not
	 * just a capture artifact, exactly for these payloads, while the
	 * button table write - buf[0]=0x01 - went out as a correct 64 bytes).
	 * Fix: always prepend a real dummy 0x00 so the kernel has something
	 * harmless to strip instead of real data, and send 65 bytes total -
	 * the standard hidraw convention for unnumbered reports, which turned
	 * out to genuinely apply here for the write()/Output-report path
	 * specifically (unlike the ioctl/Feature-report path in c18_send_cmd,
	 * where the same convention caused an EPIPE/STALL - these are
	 * different kernel code paths with different real behavior, confirmed
	 * empirically both times rather than assumed from documentation).
	 */
	buf[0] = 0x00;
	memcpy(buf + 1, payload, 64);
	rc = ratbag_hidraw_output_report(device, buf, sizeof(buf));
	msleep(20);
	if (rc < 0)
		log_error(device->ratbag,
			  "c18: output report failed: %s (%d)\n", strerror(-rc), rc);
	return rc;
}

/* Shared by LED and DPI-table writes: prepare (0x0c) + a 64-byte primer
 * (ff ff 00...) + the real 64-byte payload, all in one transaction.
 */
static int
c18_write_table64(struct ratbag_device *device, unsigned int wire_profile,
		   const uint8_t payload[64])
{
	uint8_t primer[64] = {0};
	int rc;

	primer[0] = 0xff;
	primer[1] = 0xff;

	rc = c18_send_cmd(device, C18_OP_PREPARE_TABLE, wire_profile, 0x80);
	if (rc)
		return rc;
	rc = c18_output64(device, primer);
	if (rc)
		return rc;
	return c18_output64(device, payload);
}

/* ---- DPI ---- */

static int
c18_write_resolution(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct c18_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *resolution;
	uint8_t *table = drv_data->dpi_table[profile->index];
	unsigned int active_stage = 1;
	int rc;

	ratbag_profile_for_each_resolution(profile, resolution) {
		/* Only patch the stage(s) actually being changed - the cached
		 * table's other stage bytes were seeded from a lossy reverse
		 * approximation (see c18_read_resolutions) and would drift to
		 * the nearest calibration-table point if round-tripped back
		 * through c18_dpi_to_byte() on every write, even though the
		 * user never touched them. Same "patch dirty, preserve the
		 * rest byte-exact" pattern as c18_write_button().
		 */
		if (resolution->dirty) {
			uint8_t code = c18_dpi_to_byte(resolution->dpi_x);

			table[c18_dpi_stage_offsets[resolution->index].x_off] = code;
			table[c18_dpi_stage_offsets[resolution->index].y_off] = code;
		}
		if (resolution->is_active)
			active_stage = resolution->index + 1;
	}

	rc = c18_write_table64(device, profile->index + 1, table);
	if (rc)
		return rc;

	return c18_send_cmd(device, C18_OP_SET_DPI_STAGE, profile->index + 1,
			     (uint8_t)active_stage);
}

static void
c18_read_resolutions(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct c18_data *drv_data = ratbag_get_drv_data(device);
	const uint8_t *table = drv_data->dpi_table[profile->index];
	struct ratbag_resolution *resolution;

	ratbag_profile_set_report_rate_list(profile,
					     (unsigned int[]){125, 250, 500, 1000}, 4);

	ratbag_profile_for_each_resolution(profile, resolution) {
		int x_off = c18_dpi_stage_offsets[resolution->index].x_off;
		int dpi = 0;
		unsigned int i;

		/* No readback of DPI exists either, so approximate by
		 * reversing the calibration table (nearest byte match) -
		 * good enough to show a sane value, not guaranteed exact.
		 */
		for (i = 0; i < ARRAY_LENGTH(c18_dpi_calib); i++) {
			if (c18_dpi_calib[i].byte >= table[x_off]) {
				dpi = c18_dpi_calib[i].dpi;
				break;
			}
		}
		if (!dpi)
			dpi = c18_dpi_calib[ARRAY_LENGTH(c18_dpi_calib) - 1].dpi;

		ratbag_resolution_set_resolution(resolution, dpi, dpi);
		ratbag_resolution_set_dpi_list_from_range(resolution, 200, 10000);

		/* No readback of the active stage exists either (opcode 0x04
		 * is write-only); default to stage 1, matching the device's
		 * own factory-default active stage.
		 */
		resolution->is_active = resolution->index == 0;
	}
}

/* ---- LED ---- */

static int
c18_find_mode_block(const uint8_t payload[64])
{
	const uint8_t pat[] = {0x19, 0x19, 0x05};
	int i;

	for (i = 0; i <= 64 - (int)sizeof(pat) - 3; i++) {
		if (memcmp(payload + i, pat, sizeof(pat)) == 0)
			return i;
	}
	return -1;
}

static void
c18_set_tail_rgb8(uint8_t payload[64], uint8_t r, uint8_t g, uint8_t b)
{
	int i;

	for (i = 0; i < 8; i++) {
		payload[40 + i * 3 + 0] = r;
		payload[40 + i * 3 + 1] = g;
		payload[40 + i * 3 + 2] = b;
	}
}

/* Coarse brightness->tier mapping (High=0x01/0x31, Mid=0x02/0x20,
 * Low=0x03/0x10, confirmed values, but the 0-255 threshold split itself is
 * an approximation - refine once tested against real hardware).
 */
static void
c18_brightness_to_tier(unsigned int brightness, uint8_t *tier, uint8_t *onval)
{
	if (brightness > 170) {
		*tier = 0x01; *onval = 0x31;
	} else if (brightness > 85) {
		*tier = 0x02; *onval = 0x20;
	} else {
		*tier = 0x03; *onval = 0x10;
	}
}

/* Coarse duration(ms)->speed mapping shared by the non-static effects;
 * thresholds are a first approximation, not calibrated against hardware.
 */
static uint8_t
c18_speed_generic(unsigned int ms, uint8_t fast, uint8_t mid, uint8_t slow)
{
	if (ms != 0 && ms < 400)
		return fast;
	if (ms < 800)
		return mid;
	return slow;
}

static int
c18_write_led(struct ratbag_led *led)
{
	struct ratbag_profile *profile = led->profile;
	struct ratbag_device *device = profile->device;
	uint8_t payload[64];
	int idx;

	memcpy(payload, c18_base_payload, 64);
	idx = c18_find_mode_block(payload);
	if (idx < 0)
		return -EIO;

	switch (led->mode) {
	case RATBAG_LED_OFF:
		payload[idx + 3] = 0x00;
		payload[idx + 4] = 0x00;
		payload[idx + 5] = 0x00;
		c18_set_tail_rgb8(payload, 0, 0, 0);
		break;
	case RATBAG_LED_ON: {
		uint8_t tier, onval;

		c18_brightness_to_tier(led->brightness, &tier, &onval);
		payload[idx + 3] = 0x01; /* static */
		payload[idx + 4] = tier;
		payload[idx + 5] = 0x03;
		c18_set_tail_rgb8(payload,
				  led->color.red ? onval : 0,
				  led->color.green ? onval : 0,
				  led->color.blue ? onval : 0);
		break;
	}
	case RATBAG_LED_CYCLE:
		/* Closest match for a colour-cycling effect with no fixed
		 * colour input - "flowing" in the vendor UI.
		 */
		payload[idx + 3] = 0x06;
		payload[idx + 4] = c18_speed_generic(led->ms, 0x3c, 0x5f, 0x78);
		payload[idx + 5] = 0x0a;
		memcpy(payload + 40, c18_effect_tail_default, 24);
		break;
	case RATBAG_LED_BREATHING: {
		uint8_t tier, onval;

		c18_brightness_to_tier(led->brightness, &tier, &onval);
		payload[idx + 3] = 0x02;
		payload[idx + 4] = c18_speed_generic(led->ms, 0x04, 0x08, 0x0c);
		payload[idx + 5] = 0x0a;
		/* Apply the requested colour the same way static mode does -
		 * confirmed working on real hardware (every capture on file
		 * had used the fixed default palette instead, but a custom
		 * colour does take effect here too).
		 */
		c18_set_tail_rgb8(payload,
				  led->color.red ? onval : 0,
				  led->color.green ? onval : 0,
				  led->color.blue ? onval : 0);
		break;
	}
	default:
		return -ENOTSUP;
	}

	return c18_write_table64(device, profile->index + 1, payload);
}

static void
c18_read_led(struct ratbag_led *led)
{
	ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
	ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
	ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
	ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);

	/* No readback exists; seed a sane default (matches the c18_base_payload
	 * template's own encoded state: static/high/white).
	 */
	led->mode = RATBAG_LED_ON;
	led->color.red = led->color.green = led->color.blue = 255;
	led->brightness = 255;
	led->ms = 0;
}

/* ---- Buttons ---- */

struct c18_raw_action {
	uint16_t type;
	uint16_t value;
	struct ratbag_button_action action;
};

/* Native mouse-click functions (type 0x0001). "Menu" (0xf1) is mapped to
 * physical button 2 as a best-effort guess (that slot's factory default is
 * Menu, and button 2 is very likely the hardware right-click position) -
 * unverified against real hardware, check during bring-up.
 */
static const struct c18_raw_action c18_special_actions[] = {
	{ 0x0000, 0x0000, BUTTON_ACTION_NONE },
	{ 0x0001, 0x00f0, BUTTON_ACTION_BUTTON(1) },
	{ 0x0001, 0x00f1, BUTTON_ACTION_BUTTON(2) },
	{ 0x0001, 0x00f2, BUTTON_ACTION_BUTTON(3) },
	{ 0x0001, 0x00f3, BUTTON_ACTION_BUTTON(4) },
	{ 0x0001, 0x00f4, BUTTON_ACTION_BUTTON(5) },
	{ 0x0007, 0x0001, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ 0x0007, 0x0002, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ 0x0007, 0x0003, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ 0x0008, 0x0001, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP) },
	{ 0x0008, 0x0002, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
	{ 0x0008, 0x0003, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	/* Delay=50ms/Times=2, the confirmed factory Double-Click encoding */
	{ 0xf00a, 0x0232, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },
};

static uint8_t
c18_modifiers_to_wire(unsigned int modifiers)
{
	uint8_t mask = 0;

	if (modifiers & (MODIFIER_LEFTCTRL | MODIFIER_RIGHTCTRL))
		mask |= 0x01;
	if (modifiers & (MODIFIER_LEFTSHIFT | MODIFIER_RIGHTSHIFT))
		mask |= 0x02;
	if (modifiers & (MODIFIER_LEFTALT | MODIFIER_RIGHTALT))
		mask |= 0x04;
	if (modifiers & (MODIFIER_LEFTMETA | MODIFIER_RIGHTMETA))
		mask |= 0x08;
	return mask;
}

static void
c18_build_shortcut_macro(struct ratbag_button *button, struct ratbag_device *device,
			  uint8_t mods, uint8_t key1_usage, uint8_t key2_usage)
{
	struct ratbag_button_action stub = BUTTON_ACTION_MACRO;
	struct ratbag_button_macro *m;
	unsigned int i = 0;
	unsigned int k1 = ratbag_hidraw_get_keycode_from_keyboard_usage(device, key1_usage);

	ratbag_button_set_action(button, &stub);

	m = ratbag_button_macro_new("shortcut");
	if (mods & 0x01)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_PRESSED, KEY_LEFTCTRL);
	if (mods & 0x02)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_PRESSED, KEY_LEFTSHIFT);
	if (mods & 0x04)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_PRESSED, KEY_LEFTALT);
	if (mods & 0x08)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_PRESSED, KEY_LEFTMETA);

	ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_PRESSED, k1);
	ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_RELEASED, k1);

	if (key2_usage) {
		unsigned int k2 = ratbag_hidraw_get_keycode_from_keyboard_usage(device, key2_usage);

		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_PRESSED, k2);
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_RELEASED, k2);
	}

	if (mods & 0x08)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_RELEASED, KEY_LEFTMETA);
	if (mods & 0x04)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_RELEASED, KEY_LEFTALT);
	if (mods & 0x02)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_RELEASED, KEY_LEFTSHIFT);
	if (mods & 0x01)
		ratbag_button_macro_set_event(m, i++, RATBAG_MACRO_EVENT_KEY_RELEASED, KEY_LEFTCTRL);

	ratbag_button_copy_macro(button, m);
	ratbag_button_macro_unref(m);
}

static void
c18_build_macro_from_raw(struct ratbag_button *button, const struct c18_macro *cm)
{
	struct ratbag_button_action stub = BUTTON_ACTION_MACRO;
	struct ratbag_device *device = button->profile->device;
	struct ratbag_button_macro *m;
	unsigned int ev = 0;
	unsigned int off;

	ratbag_button_set_action(button, &stub);

	if (!cm || cm->len < 2)
		return;

	m = ratbag_button_macro_new("macro");
	for (off = 2; off + 1 < cm->len; off += 2) {
		uint8_t event_byte = cm->data[off];
		uint8_t usage = cm->data[off + 1];
		unsigned int keycode;

		/* Wait-block: [00 03][00 <raw>] - see c18_wait_ms_to_raw(). The
		 * timing byte is never 0 in content this driver itself staged
		 * (starts at 1, wraps 0x7f->1), so this marker is unambiguous
		 * against a real keystroke event within self-generated content.
		 */
		if (event_byte == 0x00 && usage == 0x03 && off + 3 < cm->len) {
			uint8_t raw = cm->data[off + 3];

			ratbag_button_macro_set_event(m, ev++,
				RATBAG_MACRO_EVENT_WAIT, raw * 50u);
			off += 2;
			continue;
		}

		keycode = ratbag_hidraw_get_keycode_from_keyboard_usage(device, usage);

		ratbag_button_macro_set_event(m, ev++,
			(event_byte & 0x80) ? RATBAG_MACRO_EVENT_KEY_RELEASED
					     : RATBAG_MACRO_EVENT_KEY_PRESSED,
			keycode);
	}
	ratbag_button_copy_macro(button, m);
	ratbag_button_macro_unref(m);
}

static void
c18_read_button(struct ratbag_button *button)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct c18_data *drv_data = ratbag_get_drv_data(device);
	const uint8_t *table = drv_data->button_table[profile->index];
	int off = c18_button_slot_offset(button->index + 1);
	uint16_t type, value;
	const struct c18_raw_action *a;

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	if (off < 0)
		return;

	type = (uint16_t)(table[off] | (table[off + 1] << 8));
	value = (uint16_t)(table[off + 2] | (table[off + 3] << 8));

	if ((type & 0x00ff) == 0x0009) {
		/* macro reference */
		c18_build_macro_from_raw(button, &drv_data->macros[profile->index][button->index]);
		return;
	}

	if ((type & 0x00ff) == 0x0000 && type != 0) {
		/* modifier shortcut and/or two-key sequence -> synthetic macro */
		uint8_t mods = (uint8_t)(type >> 8);
		uint8_t key1 = value & 0xff;
		uint8_t key2 = value >> 8;

		c18_build_shortcut_macro(button, device, mods, key1, key2);
		return;
	}

	if (type == 0x0000 && value != 0x0000) {
		/* plain key, no modifier */
		struct ratbag_button_action action = {
			.type = RATBAG_BUTTON_ACTION_TYPE_KEY,
			.action.key = ratbag_hidraw_get_keycode_from_keyboard_usage(device, value & 0xff),
		};
		ratbag_button_set_action(button, &action);
		return;
	}

	if (type == 0x0003) {
		struct ratbag_button_action action = {
			.type = RATBAG_BUTTON_ACTION_TYPE_KEY,
			.action.key = ratbag_hidraw_get_keycode_from_consumer_usage(device, value),
		};
		ratbag_button_set_action(button, &action);
		return;
	}

	ARRAY_FOR_EACH(c18_special_actions, a) {
		if (a->type == type && a->value == value) {
			ratbag_button_set_action(button, &a->action);
			return;
		}
	}

	/* Unrecognized raw value (e.g. Exact Control / Armoury reference,
	 * neither of which has a generic libratbag representation) - leave
	 * as NONE rather than guessing.
	 */
}

static int
c18_native_click_from_button(unsigned int num, uint16_t *value)
{
	static const uint16_t map[] = { 0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4 };

	if (num < 1 || num > ARRAY_LENGTH(map))
		return -EINVAL;
	*value = map[num - 1];
	return 0;
}

static int
c18_alloc_macro_id(struct c18_data *drv_data, unsigned int profile_index)
{
	if (drv_data->next_macro_id[profile_index] == 0)
		drv_data->next_macro_id[profile_index] = C18_MACRO_ID_BASE;
	if (drv_data->next_macro_id[profile_index] >= 0xfe)
		return -ENOSPC;
	return drv_data->next_macro_id[profile_index]++;
}

/* event_byte's low 7 bits are a hold-duration timing value, NOT safely
 * zeroable as PROTOCOL.md originally assumed. Confirmed via hardware
 * testing: a value of exactly 0 makes the device's macro player either
 * silently no-op (repeat_mode 0x00) or hang the device outright (modes
 * 0x01/0x02) on button press, even though the upload transaction itself
 * always completes and is acknowledged cleanly (see the device's
 * newly-discovered GET_REPORT status echo). A UNIFORM non-zero value
 * (every event using the same constant) reproduces the same failure -
 * confirmed by testing 0x14 for every event. What actually works: small,
 * non-zero, NON-REPEATING values, incrementing across events - matching
 * the shape of PROTOCOL.md's real captured "no delay" macro (values
 * 1,1,1,2, never 0, never repeated in sequence). Exact required precision
 * beyond that is unconfirmed - this is a reasonable working default, not a
 * calibrated value.
 */

/* Wait-block value: raw = round(delay_ms / 50), clamped to 0x7f (~6350ms,
 * the device's confirmed max representable wait) - PROTOCOL.md's "Wait-block
 * formula" section, calibrated exactly against the vendor software's own
 * displayed timing values.
 */
static uint8_t
c18_wait_ms_to_raw(unsigned int delay_ms)
{
	unsigned int raw = (delay_ms + 25) / 50; /* round to nearest */

	if (raw > 0x7f)
		raw = 0x7f;

	return (uint8_t)raw;
}

/* Builds the macro's "00 01" header + event stream into drv_data, ready for
 * c18_upload_macro_content() at commit time. Mirrors c18ctl.c's
 * build_macro_events(): keystrokes are a press/release pair per PROTOCOL.md's
 * Macros section, wait events are the fixed 4-byte `[00 03][00 <raw>]` block
 * per PROTOCOL.md's "Wait-block formula" section.
 */
static int
c18_stage_macro_content(struct ratbag_button *button, struct c18_macro *cm)
{
	struct ratbag_device *device = button->profile->device;
	const struct ratbag_button_action *action = &button->action;
	unsigned int i;
	unsigned int off = 2;
	uint8_t timing = 0x01;

	cm->data[0] = 0x00;
	cm->data[1] = 0x01;

	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		enum ratbag_macro_event_type type = action->macro->events[i].type;
		uint8_t usage;

		if (type == RATBAG_MACRO_EVENT_INVALID)
			return -EINVAL;
		if (type == RATBAG_MACRO_EVENT_NONE)
			break;
		if (type == RATBAG_MACRO_EVENT_WAIT) {
			if (off + 4 > C18_MACRO_BUF_LEN)
				return -ENOSPC;
			cm->data[off++] = 0x00;
			cm->data[off++] = 0x03;
			cm->data[off++] = 0x00;
			cm->data[off++] = c18_wait_ms_to_raw(action->macro->events[i].event.timeout);
			continue;
		}

		if (off + 2 > C18_MACRO_BUF_LEN)
			return -ENOSPC;

		usage = ratbag_hidraw_get_keyboard_usage_from_keycode(device,
				action->macro->events[i].event.key);
		cm->data[off++] = (type == RATBAG_MACRO_EVENT_KEY_RELEASED)
					  ? (uint8_t)(0x80 | timing)
					  : timing;
		cm->data[off++] = usage;
		/* stays within the 7-bit timing field and never wraps to 0 */
		timing = (timing >= 0x7f) ? 1 : (uint8_t)(timing + 1);
	}

	cm->len = off;
	return 0;
}

static int
c18_write_button(struct ratbag_button *button, bool *needs_macro_upload)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct c18_data *drv_data = ratbag_get_drv_data(device);
	const struct ratbag_button_action *action = &button->action;
	uint8_t *table = drv_data->button_table[profile->index];
	int off = c18_button_slot_offset(button->index + 1);
	uint16_t type = 0, value = 0;
	unsigned int key, modifiers;
	int rc;

	if (off < 0)
		return -EINVAL;

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		type = 0x0000;
		value = 0x0000;
		break;

	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		rc = c18_native_click_from_button(action->action.button, &value);
		if (rc)
			return rc;
		type = 0x0001;
		break;

	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL: {
		const struct c18_raw_action *a;
		bool found = false;

		ARRAY_FOR_EACH(c18_special_actions, a) {
			if (a->action.type == RATBAG_BUTTON_ACTION_TYPE_SPECIAL &&
			    a->action.action.special == action->action.special) {
				type = a->type;
				value = a->value;
				found = true;
				break;
			}
		}
		if (!found)
			return -ENOTSUP;
		break;
	}

	case RATBAG_BUTTON_ACTION_TYPE_KEY: {
		uint8_t usage = ratbag_hidraw_get_keyboard_usage_from_keycode(device, action->action.key);

		if (usage) {
			type = 0x0000;
			value = usage;
		} else {
			type = 0x0003;
			value = ratbag_hidraw_get_consumer_usage_from_keycode(device, action->action.key);
		}
		break;
	}

	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		if (ratbag_action_keycode_from_macro(action, &key, &modifiers) == 0) {
			/* single key + modifier(s): our compact inline encoding */
			uint8_t usage = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);

			type = (uint16_t)(c18_modifiers_to_wire(modifiers) << 8);
			value = usage;
		} else {
			/* genuine multi-key macro: needs real content upload */
			struct c18_macro *cm = &drv_data->macros[profile->index][button->index];
			int id;

			if (!cm->used) {
				id = c18_alloc_macro_id(drv_data, profile->index);
				if (id < 0)
					return id;
				cm->id = (uint8_t)id;
				cm->used = true;
			}
			cm->button_index = button->index;

			rc = c18_stage_macro_content(button, cm);
			if (rc)
				return rc;

			type = (uint16_t)(0x0009 | (0x00 << 8)); /* repeat mode: "once" (no core concept for the other 2 modes) */
			value = cm->id;
			*needs_macro_upload = true;
		}
		break;

	default:
		return -ENOTSUP;
	}

	table[off + 0] = type & 0xff;
	table[off + 1] = type >> 8;
	table[off + 2] = value & 0xff;
	table[off + 3] = value >> 8;
	return 0;
}

/* Uploads one macro's content as one or more 64-byte writes, zero-padding
 * the final packet - mirrors c18ctl.c's send_macro_content(). Only the
 * first packet carries the "00 01" header; continuation packets are a raw
 * splice of the event stream with no header of their own.
 */
static int
c18_upload_macro_content(struct ratbag_device *device, const struct c18_macro *cm)
{
	unsigned int num_packets = (cm->len + 63) / 64;
	unsigned int i;
	int rc;

	if (num_packets > 2) {
		log_error(device->ratbag,
			  "macro needs %u packets; only 1-2 packet macros have been "
			  "verified against real hardware (see PROTOCOL.md open items)\n",
			  num_packets);
	}

	rc = c18_send_cmd(device, C18_OP_PREPARE_EXT_DATA, cm->id, 0x80);
	if (rc)
		return rc;

	for (i = 0; i < num_packets; i++) {
		uint8_t pkt[64] = {0};
		unsigned int chunk = cm->len - i * 64;

		if (chunk > 64)
			chunk = 64;
		memcpy(pkt, cm->data + i * 64, chunk);
		rc = c18_output64(device, pkt);
		if (rc)
			return rc;
		msleep(C18_MACRO_SETTLE_MS);
	}

	/* ack packet closing the content upload, per the worked example. */
	uint8_t zero[64] = {0};

	rc = c18_output64(device, zero);
	msleep(C18_MACRO_SETTLE_MS);
	if (rc)
		return rc;
	return 0;
}

/* ---- probe / commit / remove ---- */

/* MI_02 (the config interface) is the only sibling whose *default,
 * unnumbered* report (report ID 0) sits on the vendor usage page - its
 * whole descriptor has no Report ID items at all. ratbag_hidraw_has_vendor_page()
 * alone is too loose here: MI_01 (system controller/consumer control, which
 * does use numbered reports 1/2/3/6) also carries some vendor-page-tagged
 * sub-report and produces a false-positive match, sending our config
 * writes to the wrong interface (confirmed via a real EPIPE/STALL against
 * hardware - MI_01 doesn't understand these commands).
 */
static int
c18_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_get_usage_page(device, 0) == 0xff00 &&
	       !ratbag_hidraw_has_report(device, 1);
}

/* Finding this device's vendor-page sibling hidraw node can transiently
 * fail with ENODEV if udev hasn't finished creating all 3 of the
 * composite device's hidraw nodes yet at the moment a probe runs -
 * confirmed directly during development: a probe log showed the vendor
 * interface's node genuinely absent from the sibling search on one probe
 * path (only 2 of 3 siblings visible), while a second, independent probe
 * (udev fires one per interface, so a fast composite device can trigger
 * several near-simultaneous probes) found it immediately. This project's
 * own testing workflow - repeatedly killing one program and immediately
 * claiming the device with another - triggers this far more often than
 * any real deployment would, but retrying costs nothing at probe time.
 */
#define C18_PROBE_RETRIES	10
#define C18_PROBE_RETRY_DELAY_MS 300

static int
c18_probe(struct ratbag_device *device)
{
	struct c18_data *drv_data;
	struct ratbag_profile *profile;
	int rc;
	unsigned int attempt;

	for (attempt = 0; attempt < C18_PROBE_RETRIES; attempt++) {
		rc = ratbag_find_hidraw(device, c18_test_hidraw);
		if (rc == 0)
			break;
		if (attempt + 1 < C18_PROBE_RETRIES) {
			log_debug(device->ratbag,
				  "c18: hidraw open failed (%s), retrying (%u/%u)\n",
				  strerror(-rc), attempt + 1, C18_PROBE_RETRIES);
			msleep(C18_PROBE_RETRY_DELAY_MS);
		}
	}
	if (rc)
		return rc;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	memcpy(drv_data->dpi_table, c18_dpi_default, sizeof(drv_data->dpi_table));

	unsigned int p;
	for (p = 0; p < C18_NUM_PROFILES; p++)
		memcpy(drv_data->button_table[p], c18_button_default_table, 64);

	ratbag_device_init_profiles(device, C18_NUM_PROFILES, C18_NUM_RESOLUTIONS,
				     C18_NUM_BUTTONS, C18_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		struct ratbag_button *button;
		struct ratbag_led *led;

		profile->hz = 1000;
		c18_read_resolutions(profile);

		ratbag_profile_for_each_button(profile, button)
			c18_read_button(button);

		ratbag_profile_for_each_led(profile, led)
			c18_read_led(led);
	}

	/* No readback of the active profile exists; default to profile 1 -
	 * matches the device's own factory-default active profile.
	 */
	list_for_each(profile, &device->profiles, link) {
		profile->is_active = profile->index == 0;
	}

	return 0;
}

static void
c18_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

static int
c18_set_active_profile(struct ratbag_device *device, unsigned int index)
{
	int rc;

	rc = c18_send_cmd(device, C18_OP_BEGIN, 0x88, 0x00);
	if (rc)
		return rc;
	return c18_send_cmd(device, C18_OP_SELECT_COMMIT, (uint8_t)(index + 1), 0x00);
}

static int
c18_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		struct ratbag_resolution *resolution;
		struct ratbag_button *button;
		struct ratbag_led *led;
		bool resolutions_dirty = false;
		bool buttons_dirty = false;
		bool needs_macro_upload = false;
		uint8_t wire_profile = (uint8_t)(profile->index + 1);

		if (!profile->dirty)
			continue;

		rc = c18_send_cmd(device, C18_OP_BEGIN, 0x88, 0x00);
		if (rc)
			return rc;

		ratbag_profile_for_each_resolution(profile, resolution) {
			if (resolution->dirty)
				resolutions_dirty = true;
		}
		if (resolutions_dirty) {
			rc = c18_write_resolution(profile);
			if (rc)
				return rc;
		}

		ratbag_profile_for_each_button(profile, button) {
			if (!button->dirty)
				continue;
			buttons_dirty = true;
			rc = c18_write_button(button, &needs_macro_upload);
			if (rc)
				return rc;
		}
		if (buttons_dirty) {
			struct c18_data *drv_data = ratbag_get_drv_data(device);

			rc = c18_send_cmd(device, C18_OP_PREPARE_BUTTON_TABLE, wire_profile, 0x80);
			if (rc)
				return rc;
			rc = c18_output64(device, drv_data->button_table[profile->index]);
			if (rc)
				return rc;

			if (needs_macro_upload) {
				uint8_t zero[64] = {0};
				unsigned int bi;

				/* Longer settle delay needed on this path specifically -
				 * see C18_MACRO_SETTLE_MS. Plain button writes (no macro
				 * involved) work fine with the normal short delay inside
				 * c18_output64(), already exercised successfully many
				 * times without this.
				 */
				msleep(C18_MACRO_SETTLE_MS);

				rc = c18_output64(device, zero);
				msleep(C18_MACRO_SETTLE_MS);
				if (rc)
					return rc;

				for (bi = 0; bi < C18_NUM_BUTTONS; bi++) {
					struct c18_macro *cm = &drv_data->macros[profile->index][bi];

					if (!cm->used || cm->len == 0)
						continue;
					rc = c18_upload_macro_content(device, cm);
					if (rc)
						return rc;
				}
			}
		}

		ratbag_profile_for_each_led(profile, led) {
			if (!led->dirty)
				continue;
			rc = c18_write_led(led);
			if (rc)
				return rc;
		}

		if (profile->rate_dirty) {
			static const struct { unsigned int hz; uint8_t byte; } rates[] = {
				{1000, 0x01}, {500, 0x02}, {250, 0x04}, {125, 0x08},
			};
			unsigned int i;
			uint8_t byte = 0x01;

			for (i = 0; i < ARRAY_LENGTH(rates); i++) {
				if (rates[i].hz == profile->hz) {
					byte = rates[i].byte;
					break;
				}
			}
			rc = c18_send_cmd(device, C18_OP_SET_POLLING, wire_profile, byte);
			if (rc)
				return rc;
		}

		rc = c18_send_cmd(device, C18_OP_SELECT_COMMIT, wire_profile, 0x00);
		if (rc)
			return rc;
	}

	return 0;
}

struct ratbag_driver c18_driver = {
	.name = "SOAI C18",
	.id = "c18",
	.probe = c18_probe,
	.remove = c18_remove,
	.commit = c18_commit,
	.set_active_profile = c18_set_active_profile,
};
