/*
 * Copyright © 2026 HarukaYamamoto0 (reverse engineering)
 * Copyright © 2026 kmavrov (libratbag C driver)
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
 * Driver for the Attack Shark X11 gaming mouse (wired and wireless 2.4GHz).
 *
 * Protocol reverse-engineered from the HarukaYamamoto0 (https://github.com/HarukaYamamoto0) TypeScript driver:
 *   https://github.com/HarukaYamamoto0/attack-shark-x11-driver
 *
 * The device exposes 4 HID interfaces. Configuration is done through
 * Interface 2 (hidraw, vendor-specific) via HID SET_FEATURE reports.
 * The driver uses a write-only model (no read-back from device RAM).
 *
 * VID: 0x1d57 (Xenta / Beken)
 * PID: 0xfa55 (wired), 0xfa60 (2.4 GHz wireless adapter)
 */

#include "config.h"
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include "libratbag-private.h"
#include "libratbag-hidraw.h"
#include "shared-macro.h"

/* ── Device identifiers ─────────────────────────────────────────────────── */

#define ATTACKSHARK_PID_WIRED       0xfa55
#define ATTACKSHARK_PID_WIRELESS    0xfa60

/* ── HID report IDs (first byte of every packet) ────────────────────────── */

#define ATTACKSHARK_REPORT_DPI      0x04
#define ATTACKSHARK_REPORT_PREFS    0x05
#define ATTACKSHARK_REPORT_RATE     0x06
#define ATTACKSHARK_REPORT_BUTTONS  0x08

/* ── Buffer sizes (wired uses truncated packets, wireless uses full) ──────── */

#define ATTACKSHARK_DPI_SIZE_WIRED      52
#define ATTACKSHARK_DPI_SIZE_WIRELESS   56
#define ATTACKSHARK_PREFS_SIZE_WIRED    13
#define ATTACKSHARK_PREFS_SIZE_WIRELESS 15
#define ATTACKSHARK_RATE_SIZE           9
#define ATTACKSHARK_BUTTONS_SIZE        59

/* ── Device topology ─────────────────────────────────────────────────────── */

#define ATTACKSHARK_NUM_PROFILES        1
#define ATTACKSHARK_NUM_RESOLUTIONS     6   /* DPI stages */
#define ATTACKSHARK_NUM_BUTTONS         8
#define ATTACKSHARK_NUM_LEDS            1

/* ── Timing ──────────────────────────────────────────────────────────────── */

/* Firmware stops responding if packets arrive faster than ~250 ms apart. */
#define ATTACKSHARK_TRANSFER_DELAY_US   250000u

/* ── Firmware action codes (used in the button-mapping packet) ───────────── */

enum attackshark_fw_action {
	AS_ACT_DISABLE      = 0x01,
	AS_ACT_LEFT         = 0x02,
	AS_ACT_RIGHT        = 0x03,
	AS_ACT_MIDDLE       = 0x04,
	AS_ACT_BACKWARD     = 0x05,
	AS_ACT_FORWARD      = 0x06,
	AS_ACT_DBLCLICK     = 0x07,
	AS_ACT_SCROLL_UP    = 0x09,
	AS_ACT_SCROLL_DOWN  = 0x0a,
	AS_ACT_DPI_CYCLE    = 0x0d,
	AS_ACT_DPI_PLUS     = 0x0e,
	AS_ACT_DPI_MINUS    = 0x0f,
	AS_ACT_KEYBOARD     = 0x11,
};

/* ── Light-mode codes ────────────────────────────────────────────────────── */

enum attackshark_light_mode {
	AS_LIGHT_OFF            = 0x00,
	AS_LIGHT_STATIC         = 0x10,
	AS_LIGHT_BREATHING      = 0x20,
	AS_LIGHT_NEON           = 0x30,  /* rainbow cycle */
	AS_LIGHT_COLOR_BREATHING= 0x40,
};

/* ── DPI lookup table ────────────────────────────────────────────────────── *
 *
 * Each entry maps a DPI value to its firmware byte plus two flags:
 *   high_flag  – set when DPI is in [10100,12000] or [20100,22000]
 *   stage_mask – set when DPI > 12000
 *
 * Values share encoded bytes across ranges; the flags disambiguate them.
 *
 * Source: AttackTS/src/tables/dpi-map.ts
 */

struct as_dpi_entry {
	unsigned int  dpi;
	uint8_t       encoded;
	bool          high_flag;   /* bytes 16-21 in DPI packet */
	bool          stage_mask;  /* bytes 6-7 bitmask in DPI packet */
};

static const struct as_dpi_entry dpi_table[] = {
	/* ── 50 – 10000 DPI (step 50) ─────────────────── */
	{   50, 0x01, false, false },
	{  100, 0x02, false, false },
	{  150, 0x03, false, false },
	{  200, 0x04, false, false },
	{  250, 0x05, false, false },
	{  300, 0x06, false, false },
	{  350, 0x08, false, false },
	{  400, 0x09, false, false },
	{  450, 0x0a, false, false },
	{  500, 0x0b, false, false },
	{  550, 0x0c, false, false },
	{  600, 0x0e, false, false },
	{  650, 0x0f, false, false },
	{  700, 0x10, false, false },
	{  750, 0x11, false, false },
	{  800, 0x12, false, false },
	{  850, 0x13, false, false },
	{  900, 0x15, false, false },
	{  950, 0x16, false, false },
	{ 1000, 0x17, false, false },
	{ 1050, 0x18, false, false },
	{ 1100, 0x19, false, false },
	{ 1150, 0x1b, false, false },
	{ 1200, 0x1c, false, false },
	{ 1250, 0x1d, false, false },
	{ 1300, 0x1e, false, false },
	{ 1350, 0x1f, false, false },
	{ 1400, 0x20, false, false },
	{ 1450, 0x22, false, false },
	{ 1500, 0x23, false, false },
	{ 1550, 0x24, false, false },
	{ 1600, 0x25, false, false },
	{ 1650, 0x26, false, false },
	{ 1700, 0x27, false, false },
	{ 1750, 0x29, false, false },
	{ 1800, 0x2a, false, false },
	{ 1850, 0x2b, false, false },
	{ 1900, 0x2c, false, false },
	{ 1950, 0x2d, false, false },
	{ 2000, 0x2f, false, false },
	{ 2050, 0x30, false, false },
	{ 2100, 0x31, false, false },
	{ 2150, 0x32, false, false },
	{ 2200, 0x33, false, false },
	{ 2250, 0x34, false, false },
	{ 2300, 0x36, false, false },
	{ 2350, 0x37, false, false },
	{ 2400, 0x38, false, false },
	{ 2450, 0x39, false, false },
	{ 2500, 0x3a, false, false },
	{ 2550, 0x3b, false, false },
	{ 2600, 0x3d, false, false },
	{ 2650, 0x3e, false, false },
	{ 2700, 0x3f, false, false },
	{ 2750, 0x40, false, false },
	{ 2800, 0x41, false, false },
	{ 2850, 0x43, false, false },
	{ 2900, 0x44, false, false },
	{ 2950, 0x45, false, false },
	{ 3000, 0x46, false, false },
	{ 3050, 0x47, false, false },
	{ 3100, 0x48, false, false },
	{ 3150, 0x4a, false, false },
	{ 3200, 0x4b, false, false },
	{ 3250, 0x4c, false, false },
	{ 3300, 0x4d, false, false },
	{ 3350, 0x4e, false, false },
	{ 3400, 0x4f, false, false },
	{ 3450, 0x51, false, false },
	{ 3500, 0x52, false, false },
	{ 3550, 0x53, false, false },
	{ 3600, 0x54, false, false },
	{ 3650, 0x55, false, false },
	{ 3700, 0x57, false, false },
	{ 3750, 0x58, false, false },
	{ 3800, 0x59, false, false },
	{ 3850, 0x5a, false, false },
	{ 3900, 0x5b, false, false },
	{ 3950, 0x5c, false, false },
	{ 4000, 0x5e, false, false },
	{ 4050, 0x5f, false, false },
	{ 4100, 0x60, false, false },
	{ 4150, 0x61, false, false },
	{ 4200, 0x62, false, false },
	{ 4250, 0x63, false, false },
	{ 4300, 0x65, false, false },
	{ 4350, 0x66, false, false },
	{ 4400, 0x67, false, false },
	{ 4450, 0x68, false, false },
	{ 4500, 0x69, false, false },
	{ 4550, 0x6b, false, false },
	{ 4600, 0x6c, false, false },
	{ 4650, 0x6d, false, false },
	{ 4700, 0x6e, false, false },
	{ 4750, 0x6f, false, false },
	{ 4800, 0x70, false, false },
	{ 4850, 0x72, false, false },
	{ 4900, 0x73, false, false },
	{ 4950, 0x74, false, false },
	{ 5000, 0x75, false, false },
	{ 5050, 0x76, false, false },
	{ 5100, 0x77, false, false },
	{ 5150, 0x79, false, false },
	{ 5200, 0x7a, false, false },
	{ 5250, 0x7b, false, false },
	{ 5300, 0x7c, false, false },
	{ 5350, 0x7d, false, false },
	{ 5400, 0x7f, false, false },
	{ 5450, 0x80, false, false },
	{ 5500, 0x81, false, false },
	{ 5550, 0x82, false, false },
	{ 5600, 0x83, false, false },
	{ 5650, 0x84, false, false },
	{ 5700, 0x86, false, false },
	{ 5750, 0x87, false, false },
	{ 5800, 0x88, false, false },
	{ 5850, 0x89, false, false },
	{ 5900, 0x8a, false, false },
	{ 5950, 0x8b, false, false },
	{ 6000, 0x8d, false, false },
	{ 6050, 0x8e, false, false },
	{ 6100, 0x8f, false, false },
	{ 6150, 0x90, false, false },
	{ 6200, 0x91, false, false },
	{ 6250, 0x93, false, false },
	{ 6300, 0x94, false, false },
	{ 6350, 0x95, false, false },
	{ 6400, 0x96, false, false },
	{ 6450, 0x97, false, false },
	{ 6500, 0x98, false, false },
	{ 6550, 0x9a, false, false },
	{ 6600, 0x9b, false, false },
	{ 6650, 0x9c, false, false },
	{ 6700, 0x9d, false, false },
	{ 6750, 0x9e, false, false },
	{ 6800, 0x9f, false, false },
	{ 6850, 0xa1, false, false },
	{ 6900, 0xa2, false, false },
	{ 6950, 0xa3, false, false },
	{ 7000, 0xa4, false, false },
	{ 7050, 0xa5, false, false },
	{ 7100, 0xa7, false, false },
	{ 7150, 0xa8, false, false },
	{ 7200, 0xa9, false, false },
	{ 7250, 0xaa, false, false },
	{ 7300, 0xab, false, false },
	{ 7350, 0xac, false, false },
	{ 7400, 0xae, false, false },
	{ 7450, 0xaf, false, false },
	{ 7500, 0xb0, false, false },
	{ 7550, 0xb1, false, false },
	{ 7600, 0xb2, false, false },
	{ 7650, 0xb3, false, false },
	{ 7700, 0xb5, false, false },
	{ 7750, 0xb6, false, false },
	{ 7800, 0xb7, false, false },
	{ 7850, 0xb8, false, false },
	{ 7900, 0xb9, false, false },
	{ 7950, 0xbb, false, false },
	{ 8000, 0xbc, false, false },
	{ 8050, 0xbd, false, false },
	{ 8100, 0xbe, false, false },
	{ 8150, 0xbf, false, false },
	{ 8200, 0xc0, false, false },
	{ 8250, 0xc2, false, false },
	{ 8300, 0xc3, false, false },
	{ 8350, 0xc4, false, false },
	{ 8400, 0xc5, false, false },
	{ 8450, 0xc6, false, false },
	{ 8500, 0xc7, false, false },
	{ 8550, 0xc9, false, false },
	{ 8600, 0xca, false, false },
	{ 8650, 0xcb, false, false },
	{ 8700, 0xcc, false, false },
	{ 8750, 0xcd, false, false },
	{ 8800, 0xcf, false, false },
	{ 8850, 0xd0, false, false },
	{ 8900, 0xd1, false, false },
	{ 8950, 0xd2, false, false },
	{ 9000, 0xd3, false, false },
	{ 9050, 0xd4, false, false },
	{ 9100, 0xd6, false, false },
	{ 9150, 0xd7, false, false },
	{ 9200, 0xd8, false, false },
	{ 9250, 0xd9, false, false },
	{ 9300, 0xda, false, false },
	{ 9350, 0xdb, false, false },
	{ 9400, 0xdd, false, false },
	{ 9450, 0xde, false, false },
	{ 9500, 0xdf, false, false },
	{ 9550, 0xe0, false, false },
	{ 9600, 0xe1, false, false },
	{ 9650, 0xe3, false, false },
	{ 9700, 0xe4, false, false },
	{ 9750, 0xe5, false, false },
	{ 9800, 0xe6, false, false },
	{ 9850, 0xe7, false, false },
	{ 9900, 0xe8, false, false },
	{ 9950, 0xea, false, false },
	{ 10000, 0xeb, false, false },
	/* ── 10100 – 12000 DPI (step 100, high_flag=true, stage_mask=false) ── */
	{ 10100, 0x76, true,  false },
	{ 10200, 0x77, true,  false },
	{ 10300, 0x79, true,  false },
	{ 10400, 0x7a, true,  false },
	{ 10500, 0x7b, true,  false },
	{ 10600, 0x7c, true,  false },
	{ 10700, 0x7d, true,  false },
	{ 10800, 0x7f, true,  false },
	{ 10900, 0x80, true,  false },
	{ 11000, 0x81, true,  false },
	{ 11100, 0x82, true,  false },
	{ 11200, 0x83, true,  false },
	{ 11300, 0x84, true,  false },
	{ 11400, 0x86, true,  false },
	{ 11500, 0x87, true,  false },
	{ 11600, 0x88, true,  false },
	{ 11700, 0x89, true,  false },
	{ 11800, 0x8a, true,  false },
	{ 11900, 0x8b, true,  false },
	{ 12000, 0x8d, true,  false },
	/* ── 12100 – 20000 DPI (step 100, high_flag=false, stage_mask=true) ── */
	{ 12100, 0x8e, false, true },
	{ 12200, 0x8f, false, true },
	{ 12300, 0x90, false, true },
	{ 12400, 0x91, false, true },
	{ 12500, 0x93, false, true },
	{ 12600, 0x94, false, true },
	{ 12700, 0x95, false, true },
	{ 12800, 0x96, false, true },
	{ 12900, 0x97, false, true },
	{ 13000, 0x98, false, true },
	{ 13100, 0x9a, false, true },
	{ 13200, 0x9b, false, true },
	{ 13300, 0x9c, false, true },
	{ 13400, 0x9d, false, true },
	{ 13500, 0x9e, false, true },
	{ 13600, 0x9f, false, true },
	{ 13700, 0xa1, false, true },
	{ 13800, 0xa2, false, true },
	{ 13900, 0xa3, false, true },
	{ 14000, 0xa4, false, true },
	{ 14100, 0xa5, false, true },
	{ 14200, 0xa7, false, true },
	{ 14300, 0xa8, false, true },
	{ 14400, 0xa9, false, true },
	{ 14500, 0xaa, false, true },
	{ 14600, 0xab, false, true },
	{ 14700, 0xac, false, true },
	{ 14800, 0xae, false, true },
	{ 14900, 0xaf, false, true },
	{ 15000, 0xb0, false, true },
	{ 15100, 0xb1, false, true },
	{ 15200, 0xb2, false, true },
	{ 15300, 0xb3, false, true },
	{ 15400, 0xb5, false, true },
	{ 15500, 0xb6, false, true },
	{ 15600, 0xb7, false, true },
	{ 15700, 0xb8, false, true },
	{ 15800, 0xb9, false, true },
	{ 15900, 0xbb, false, true },
	{ 16000, 0xbc, false, true },
	{ 16100, 0xbd, false, true },
	{ 16200, 0xbe, false, true },
	{ 16300, 0xbf, false, true },
	{ 16400, 0xc0, false, true },
	{ 16500, 0xc2, false, true },
	{ 16600, 0xc3, false, true },
	{ 16700, 0xc4, false, true },
	{ 16800, 0xc5, false, true },
	{ 16900, 0xc6, false, true },
	{ 17000, 0xc7, false, true },
	{ 17100, 0xc9, false, true },
	{ 17200, 0xca, false, true },
	{ 17300, 0xcb, false, true },
	{ 17400, 0xcc, false, true },
	{ 17500, 0xcd, false, true },
	{ 17600, 0xcf, false, true },
	{ 17700, 0xd0, false, true },
	{ 17800, 0xd1, false, true },
	{ 17900, 0xd2, false, true },
	{ 18000, 0xd3, false, true },
	{ 18100, 0xd4, false, true },
	{ 18200, 0xd6, false, true },
	{ 18300, 0xd7, false, true },
	{ 18400, 0xd8, false, true },
	{ 18500, 0xd9, false, true },
	{ 18600, 0xda, false, true },
	{ 18700, 0xdb, false, true },
	{ 18800, 0xdd, false, true },
	{ 18900, 0xde, false, true },
	{ 19000, 0xdf, false, true },
	{ 19100, 0xe0, false, true },
	{ 19200, 0xe1, false, true },
	{ 19300, 0xe3, false, true },
	{ 19400, 0xe4, false, true },
	{ 19500, 0xe5, false, true },
	{ 19600, 0xe6, false, true },
	{ 19700, 0xe7, false, true },
	{ 19800, 0xe8, false, true },
	{ 19900, 0xea, false, true },
	{ 20000, 0xeb, false, true },
	/* ── 20100 – 22000 DPI (step 100, high_flag=true, stage_mask=true) ── */
	{ 20100, 0xeb, true,  true },
	{ 20200, 0x76, true,  true },
	{ 20300, 0x76, true,  true },
	{ 20400, 0x77, true,  true },
	{ 20500, 0x77, true,  true },
	{ 20600, 0x79, true,  true },
	{ 20700, 0x79, true,  true },
	{ 20800, 0x7a, true,  true },
	{ 20900, 0x7a, true,  true },
	{ 21000, 0x7b, true,  true },
	{ 21100, 0x7b, true,  true },
	{ 21200, 0x7c, true,  true },
	{ 21300, 0x7c, true,  true },
	{ 21400, 0x7d, true,  true },
	{ 21500, 0x7d, true,  true },
	{ 21600, 0x7f, true,  true },
	{ 21700, 0x7f, true,  true },
	{ 21800, 0x80, true,  true },
	{ 21900, 0x80, true,  true },
	{ 22000, 0x81, true,  true },
};

/* ── Button-slot offsets in the button-mapping packet ────────────────────── *
 * Each button occupies 3 bytes at these offsets: [action, modifier, keycode].
 * Indices match the ratbag button index (0-7). */

static const int button_offsets[ATTACKSHARK_NUM_BUTTONS] = {
	 3,  /* 0 – LEFT       */
	 6,  /* 1 – RIGHT      */
	 9,  /* 2 – MIDDLE     */
	21,  /* 3 – FORWARD    */
	24,  /* 4 – BACKWARD   */
	18,  /* 5 – DPI        */
	51,  /* 6 – SCROLL UP  */
	54,  /* 7 – SCROLL DOWN*/
};

/* ── Polling rates ───────────────────────────────────────────────────────── */

static const unsigned int as_rates[]   = { 125, 250, 500, 1000 };
static const uint8_t      as_rate_enc[]= { 0x08, 0x04, 0x02, 0x01 };

/* ── Debounce values (ms) ────────────────────────────────────────────────── */
/* 8 representative values from the 4-50 ms range (struct limit: 8 entries) */
static const unsigned int as_debounces[] = { 4, 8, 12, 16, 20, 28, 38, 50 };

/* ── Default settings (used on probe since the device is write-only) ─────── */

static const unsigned int as_default_dpi[ATTACKSHARK_NUM_RESOLUTIONS] =
	{ 800, 1600, 2400, 3200, 5000, 22000 };
#define AS_DEFAULT_ACTIVE_STAGE 1   /* 0-indexed → stage 2 in firmware (1-indexed) */
#define AS_DEFAULT_RATE         1000
#define AS_DEFAULT_DEBOUNCE     4
#define AS_DEFAULT_ANGLE_SNAP   0
#define AS_DEFAULT_LED_MODE     RATBAG_LED_OFF
#define AS_DEFAULT_R            0
#define AS_DEFAULT_G            255
#define AS_DEFAULT_B            0
#define AS_DEFAULT_LED_MS       3000  /* mid-speed (maps to ledSpeed 3) */

/* ── Per-device driver state ─────────────────────────────────────────────── */

struct attackshark_data {
	bool is_wireless;
	/* Cached values of last committed state (for rebuilding full packets) */
	unsigned int dpi[ATTACKSHARK_NUM_RESOLUTIONS];
	unsigned int active_stage;  /* 0-indexed */
	bool         angle_snap;
	unsigned int rate;
	unsigned int debounce;
	/* LED */
	enum ratbag_led_mode led_mode;
	struct ratbag_color  led_color;
	unsigned int         led_ms;
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Encoding helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static const struct as_dpi_entry *
attackshark_find_dpi_entry(unsigned int dpi)
{
	const struct as_dpi_entry *best = &dpi_table[0];
	for (size_t i = 0; i < ARRAY_LENGTH(dpi_table); i++) {
		if (dpi_table[i].dpi >= dpi) {
			best = &dpi_table[i];
			break;
		}
		/* Track last entry as fallback */
		best = &dpi_table[i];
	}
	return best;
}

static uint8_t
attackshark_encode_rate(unsigned int hz)
{
	for (size_t i = 0; i < ARRAY_LENGTH(as_rates); i++)
		if (as_rates[i] == hz)
			return as_rate_enc[i];
	return as_rate_enc[ARRAY_LENGTH(as_rate_enc) - 1]; /* default 1000 Hz */
}

/*
 * Map a ratbag LED mode to the closest Attack Shark light-mode byte.
 * The device has more modes than ratbag; we map the four ratbag modes.
 */
static enum attackshark_light_mode
attackshark_encode_led_mode(enum ratbag_led_mode mode)
{
	switch (mode) {
	case RATBAG_LED_OFF:       return AS_LIGHT_OFF;
	case RATBAG_LED_ON:        return AS_LIGHT_STATIC;
	case RATBAG_LED_BREATHING: return AS_LIGHT_BREATHING;
	case RATBAG_LED_CYCLE:     return AS_LIGHT_NEON;
	default:                   return AS_LIGHT_OFF;
	}
}

/*
 * Map led->ms to a hardware LED speed (1–5, 5=fastest).
 * We define: ms=1000 → speed 5, ms=5000 → speed 1.
 */
static uint8_t
attackshark_ms_to_led_speed(unsigned int ms)
{
	if (ms <= 1000) return 5;
	if (ms <= 2000) return 4;
	if (ms <= 3000) return 3;
	if (ms <= 4000) return 2;
	return 1;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * Packet builders
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Build the DPI configuration packet (report 0x04).
 *
 * Packet layout (wired: 52 bytes, wireless: 56 bytes):
 *   [0]   = 0x04  (report ID)
 *   [1]   = 0x38
 *   [2]   = 0x01
 *   [3]   = angle_snap (0/1)
 *   [4]   = ripple_control (always 0x01)
 *   [5]   = 0x3F
 *   [6]   = stage_mask (bits 0-5 set if corresponding stage DPI > 12000)
 *   [7]   = stage_mask (copy)
 *   [8-13]= encoded DPI for stages 0-5
 *   [14-15]= 0x00
 *   [16-21]= high_flag per stage (0x01 if DPI in [10100,12000] or [20100,22000])
 *   [22-23]= 0x00
 *   [24]  = active stage index (1-indexed)
 *   [25-49]= fixed bytes
 *   [50-51]= 16-bit big-endian checksum (sum of bytes 3-49)
 *   [52-55]= 0x00 padding (wireless only)
 */
static void
attackshark_build_dpi_packet(uint8_t *buf, size_t bufsz,
			     const struct attackshark_data *drv)
{
	memset(buf, 0, bufsz);

	buf[0]  = ATTACKSHARK_REPORT_DPI;
	buf[1]  = 0x38;
	buf[2]  = 0x01;
	buf[3]  = drv->angle_snap ? 0x01 : 0x00;
	buf[4]  = 0x01;  /* ripple control always on */
	buf[5]  = 0x3f;

	uint8_t stage_mask = 0;
	for (unsigned int i = 0; i < ATTACKSHARK_NUM_RESOLUTIONS; i++) {
		const struct as_dpi_entry *e = attackshark_find_dpi_entry(drv->dpi[i]);
		buf[8 + i]  = e->encoded;
		buf[16 + i] = e->high_flag ? 0x01 : 0x00;
		if (e->stage_mask)
			stage_mask |= (uint8_t)(1u << i);
	}
	buf[6] = stage_mask;
	buf[7] = stage_mask;

	buf[24] = (uint8_t)(drv->active_stage + 1);  /* firmware is 1-indexed */

	/* Fixed data bytes 25-49 (from reverse-engineered defaults) */
	buf[25] = 0xff;
	buf[29] = 0xff;
	buf[33] = 0xff;
	buf[34] = 0xff;
	buf[35] = 0xff;
	buf[38] = 0xff;
	buf[39] = 0xff;
	buf[40] = 0xff;
	buf[42] = 0xff;
	buf[43] = 0xff;
	buf[44] = 0x40;
	buf[46] = 0xff;
	buf[47] = 0xff;
	buf[48] = 0xff;
	buf[49] = 0x02;

	/* Checksum: 16-bit sum of bytes 3-49, stored big-endian at [50-51] */
	uint16_t sum = 0;
	for (int i = 3; i <= 49; i++)
		sum += buf[i];
	buf[50] = (uint8_t)(sum >> 8);
	buf[51] = (uint8_t)(sum & 0xff);

	/* Bytes 52-55 remain 0x00 (already cleared by memset); wired uses only 52 bytes */
}

/*
 * Build the user-preferences packet (report 0x05).
 *
 * Packet layout (wired: 13 bytes, wireless: 15 bytes):
 *   [0]  = 0x05  (report ID)
 *   [1]  = 0x0F
 *   [2]  = 0x01
 *   [3]  = light mode byte
 *   [4]  = (deep_sleep_bucket << 4) | hw_led_speed  (hw_speed = 6 - user_speed)
 *   [5]  = 0x08 + deep_sleep_minutes * 0x10  (mod 256)
 *   [6]  = RGB red
 *   [7]  = RGB green
 *   [8]  = RGB blue
 *   [9]  = sleep_time * 2  (0.5 min steps → 1-step units)
 *   [10] = (debounce_ms - 4) / 2 + 2
 *   [11] = state_flag (count of RGB channels >= 0x64, +1 if BreathingDpi)
 *   [12] = checksum (sum of bytes 3-10, mod 256)
 *   [13-14] = 0x00 (wireless padding)
 */
static void
attackshark_build_prefs_packet(uint8_t *buf, size_t bufsz,
			       const struct attackshark_data *drv)
{
	/* Fixed deep sleep of 10 minutes and sleep timer of 0.5 min.
	 * These are not yet exposed via a ratbag API; use sensible defaults. */
	const unsigned int deep_sleep_min = 10;
	const unsigned int sleep_min_half = 4;  /* 0.5 min → value 1 */

	uint8_t led_speed = attackshark_ms_to_led_speed(drv->led_ms);
	uint8_t hw_speed  = (uint8_t)(6 - led_speed);
	uint8_t bucket    = (uint8_t)((deep_sleep_min - 1) / 16);
	enum attackshark_light_mode lm = attackshark_encode_led_mode(drv->led_mode);

	memset(buf, 0, bufsz);

	buf[0]  = ATTACKSHARK_REPORT_PREFS;
	buf[1]  = 0x0f;
	buf[2]  = 0x01;
	buf[3]  = (uint8_t)lm;
	buf[4]  = (uint8_t)((bucket << 4) | (hw_speed & 0x0f));
	buf[5]  = (uint8_t)((0x08 + deep_sleep_min * 0x10) & 0xff);
	buf[6]  = drv->led_color.red;
	buf[7]  = drv->led_color.green;
	buf[8]  = drv->led_color.blue;
	buf[9]  = (uint8_t)sleep_min_half;
	buf[10] = (uint8_t)((drv->debounce - 4) / 2 + 2);

	/* State flag: count of RGB channels >= 0x64 */
	uint8_t state = 0;
	if (drv->led_color.red   >= 0x64) state++;
	if (drv->led_color.green >= 0x64) state++;
	if (drv->led_color.blue  >= 0x64) state++;
	buf[11] = state;

	/* Checksum: sum of bytes 3-10 */
	uint8_t cksum = 0;
	for (int i = 3; i <= 10; i++)
		cksum = (uint8_t)(cksum + buf[i]);
	buf[12] = cksum;

	/* Bytes 13-14 already 0x00 */
}

/*
 * Build the polling-rate packet (report 0x06).
 *
 * Packet layout (9 bytes, same for wired and wireless):
 *   [0] = 0x06  (report ID)
 *   [1] = 0x09
 *   [2] = 0x01
 *   [3] = rate byte (0x01/0x02/0x04/0x08 for 1000/500/250/125 Hz)
 *   [4] = 0xFF - buf[3]  (checksum)
 *   [5-8] = 0x00
 */
static void
attackshark_build_rate_packet(uint8_t *buf, unsigned int hz)
{
	memset(buf, 0, ATTACKSHARK_RATE_SIZE);
	buf[0] = ATTACKSHARK_REPORT_RATE;
	buf[1] = 0x09;
	buf[2] = 0x01;
	buf[3] = attackshark_encode_rate(hz);
	buf[4] = (uint8_t)(0xff - buf[3]);
}

/*
 * Build the button-mapping packet (report 0x08).
 *
 * Packet layout (59 bytes, same for wired and wireless):
 *   [0]  = 0x08  (report ID)
 *   [1]  = 0x3B  (length)
 *   [2]  = 0x01  (protocol version)
 *   [3-57]= 18 button slots × 3 bytes each [action, modifier, keycode]
 *   [58] = checksum: (sum of bytes 2-57) - 1, mod 256
 *
 * Slot ordering (offset → button):
 *   3  LEFT | 6  RIGHT | 9  MIDDLE | 18 DPI | 21 FORWARD | 24 BACKWARD
 *   51 SCROLL_UP | 54 SCROLL_DOWN
 *   All other slots default to [0x01, 0x00, 0x00] (disabled).
 */
static void
attackshark_build_buttons_packet(uint8_t *buf,
				 struct ratbag_device *device,
				 struct ratbag_profile *profile)
{
	memset(buf, 0, ATTACKSHARK_BUTTONS_SIZE);

	buf[0] = ATTACKSHARK_REPORT_BUTTONS;
	buf[1] = 0x3b;
	buf[2] = 0x01;

	/* Default all 18 slots to "disabled" [0x01, 0x00, 0x00] */
	for (int slot = 3; slot <= 54; slot += 3)
		buf[slot] = AS_ACT_DISABLE;

	/* Fixed assignments that are not remappable (DPI cycle, scroll) */
	buf[18] = AS_ACT_DPI_CYCLE;   /* DPI button default */
	buf[51] = AS_ACT_SCROLL_UP;
	buf[54] = AS_ACT_SCROLL_DOWN;

	struct ratbag_button *button;
	ratbag_profile_for_each_button(profile, button) {
		int off = button_offsets[button->index];
		uint8_t action   = AS_ACT_DISABLE;
		uint8_t modifier = 0x00;
		uint8_t keycode  = 0x00;

		switch (button->action.type) {
		case RATBAG_BUTTON_ACTION_TYPE_NONE:
			action = AS_ACT_DISABLE;
			break;

		case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
			switch (button->action.action.button) {
			case 1: action = AS_ACT_LEFT;       break;
			case 2: action = AS_ACT_RIGHT;      break;
			case 3: action = AS_ACT_MIDDLE;     break;
			case 4: action = AS_ACT_BACKWARD;   break;
			case 5: action = AS_ACT_FORWARD;    break;
			case 6: action = AS_ACT_SCROLL_UP;  break;
			case 7: action = AS_ACT_SCROLL_DOWN;break;
			default: action = AS_ACT_DISABLE;   break;
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
			switch (button->action.action.special) {
			case RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK:
				action = AS_ACT_DBLCLICK;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
				action = AS_ACT_SCROLL_UP;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
				action = AS_ACT_SCROLL_DOWN;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
				action = AS_ACT_DPI_CYCLE;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:
				action = AS_ACT_DPI_PLUS;
				break;
			case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:
				action = AS_ACT_DPI_MINUS;
				break;
			default:
				action = AS_ACT_DISABLE;
				break;
			}
			break;

		case RATBAG_BUTTON_ACTION_TYPE_KEY: {
			uint8_t hid = ratbag_hidraw_get_keyboard_usage_from_keycode(
					device, button->action.action.key);
			if (hid == 0) {
				action = AS_ACT_DISABLE;
				break;
			}
			action   = AS_ACT_KEYBOARD;
			modifier = 0x00; /* ratbag does not expose modifiers separately */
			keycode  = hid;
			break;
		}

		default:
			action = AS_ACT_DISABLE;
			break;
		}

		buf[off]     = action;
		buf[off + 1] = modifier;
		buf[off + 2] = keycode;
	}

	/* Checksum: (sum of bytes 2-57) - 1, mod 256 */
	uint8_t sum = 0;
	for (int i = 2; i <= 57; i++)
		sum = (uint8_t)(sum + buf[i]);
	buf[58] = (uint8_t)(sum - 1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HID send helpers
 * ═══════════════════════════════════════════════════════════════════════════ */

static int
attackshark_send(struct ratbag_device *device,
		 uint8_t report_id, uint8_t *buf, size_t len)
{
	/* Debug: print the full packet so mismatches are easy to spot */
	char hexbuf[len * 3 + 1];
	for (size_t i = 0; i < len; i++)
		snprintf(hexbuf + i * 3, 4, "%02x ", buf[i]);
	log_debug(device->ratbag,
		  "attackshark: send report 0x%02x (%zu bytes): %s\n",
		  report_id, len, hexbuf);

	int rc = ratbag_hidraw_set_feature_report(device, report_id, buf, len);
	if (rc < 0) {
		log_error(device->ratbag,
			  "attackshark: failed to send report 0x%02x: %d\n",
			  report_id, rc);
		return rc;
	}
	usleep(ATTACKSHARK_TRANSFER_DELAY_US);
	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * HID interface matching
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * The device exposes 4 HID interfaces.  We need Interface 2
 * (the vendor-specific configuration interface, hidraw index 2 from the
 * kernel's perspective) which has report IDs 0x04, 0x05, 0x06, and 0x08.
 */
static int
attackshark_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, ATTACKSHARK_REPORT_DPI) &&
	       ratbag_hidraw_has_report(device, ATTACKSHARK_REPORT_PREFS) &&
	       ratbag_hidraw_has_report(device, ATTACKSHARK_REPORT_RATE);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Probe
 * ═══════════════════════════════════════════════════════════════════════════ */

static int
attackshark_probe(struct ratbag_device *device)
{
	int rc;
	struct attackshark_data *drv;
	struct ratbag_profile   *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_button    *button;
	struct ratbag_led       *led;

	/* Find and open the vendor configuration interface */
	rc = ratbag_find_hidraw(device, attackshark_test_hidraw);
	if (rc)
		return rc;

	/* Allocate per-device state */
	drv = zalloc(sizeof(*drv));
	drv->is_wireless = (device->ids.product == ATTACKSHARK_PID_WIRELESS);

	/* Initialise cached state to firmware defaults */
	for (unsigned int i = 0; i < ATTACKSHARK_NUM_RESOLUTIONS; i++)
		drv->dpi[i] = as_default_dpi[i];
	drv->active_stage  = AS_DEFAULT_ACTIVE_STAGE;
	drv->angle_snap    = AS_DEFAULT_ANGLE_SNAP;
	drv->rate          = AS_DEFAULT_RATE;
	drv->debounce      = AS_DEFAULT_DEBOUNCE;
	drv->led_mode      = AS_DEFAULT_LED_MODE;
	drv->led_color.red   = AS_DEFAULT_R;
	drv->led_color.green = AS_DEFAULT_G;
	drv->led_color.blue  = AS_DEFAULT_B;
	drv->led_ms        = AS_DEFAULT_LED_MS;

	ratbag_set_drv_data(device, drv);

	/* Initialise the ratbag profile/resolution/button/LED tree */
	ratbag_device_init_profiles(device,
				    ATTACKSHARK_NUM_PROFILES,
				    ATTACKSHARK_NUM_RESOLUTIONS,
				    ATTACKSHARK_NUM_BUTTONS,
				    ATTACKSHARK_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;

		/* Write-only: we can commit but cannot read back state */
		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);

		/* Polling rates */
		ratbag_profile_set_report_rate_list(profile, as_rates,
						    ARRAY_LENGTH(as_rates));
		profile->hz = AS_DEFAULT_RATE;

		/* Angle snapping */
		profile->angle_snapping = AS_DEFAULT_ANGLE_SNAP;

		/* Debounce */
		ratbag_profile_set_debounce_list(profile, as_debounces,
						 ARRAY_LENGTH(as_debounces));
		profile->debounce = AS_DEFAULT_DEBOUNCE;

		/* DPI resolutions */
		ratbag_profile_for_each_resolution(profile, resolution) {
			ratbag_resolution_set_dpi_list_from_range(resolution, 50, 22000);
			resolution->dpi_x = resolution->dpi_y =
				as_default_dpi[resolution->index];
			resolution->is_active  = (resolution->index == AS_DEFAULT_ACTIVE_STAGE);
			resolution->is_default = (resolution->index == AS_DEFAULT_ACTIVE_STAGE);
		}

		/* Buttons */
		ratbag_profile_for_each_button(profile, button) {
			ratbag_button_enable_action_type(button,
				RATBAG_BUTTON_ACTION_TYPE_NONE);
			ratbag_button_enable_action_type(button,
				RATBAG_BUTTON_ACTION_TYPE_BUTTON);
			ratbag_button_enable_action_type(button,
				RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
			ratbag_button_enable_action_type(button,
				RATBAG_BUTTON_ACTION_TYPE_KEY);

			/* Set default actions */
			switch (button->index) {
			case 0:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = 1; /* left */
				break;
			case 1:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = 2; /* right */
				break;
			case 2:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = 3; /* middle */
				break;
			case 3:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = 5; /* forward */
				break;
			case 4:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
				button->action.action.button = 4; /* backward */
				break;
			case 5:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
				button->action.action.special =
					RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
				break;
			case 6:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
				button->action.action.special =
					RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP;
				break;
			case 7:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
				button->action.action.special =
					RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN;
				break;
			default:
				button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
				break;
			}
		}

		/* LED */
		ratbag_profile_for_each_led(profile, led) {
			led->colordepth      = RATBAG_LED_COLORDEPTH_RGB_888;
			led->mode            = AS_DEFAULT_LED_MODE;
			led->color.red       = AS_DEFAULT_R;
			led->color.green     = AS_DEFAULT_G;
			led->color.blue      = AS_DEFAULT_B;
			led->ms              = AS_DEFAULT_LED_MS;
			led->brightness      = 255;

			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
			ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
			ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
		}
	}

	log_debug(device->ratbag,
		  "attackshark: probed %s device (PID 0x%04x)\n",
		  drv->is_wireless ? "wireless" : "wired",
		  device->ids.product);

	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Commit
 * ═══════════════════════════════════════════════════════════════════════════ */

static int
attackshark_commit(struct ratbag_device *device)
{
	struct attackshark_data *drv = ratbag_get_drv_data(device);
	struct ratbag_profile   *profile;
	int rc;

	ratbag_device_for_each_profile(device, profile) {
		if (!profile->dirty)
			continue;

		/* ── Update cached DPI values from dirty resolutions ─── */
		bool dpi_changed = false;
		struct ratbag_resolution *resolution;
		ratbag_profile_for_each_resolution(profile, resolution) {
			if (resolution->dirty) {
				drv->dpi[resolution->index] = resolution->dpi_x;
				dpi_changed = true;
			}
			if (resolution->is_active)
				drv->active_stage = resolution->index;
		}

		/* ── Update angle snap ─────────────────────────────────── */
		if (profile->angle_snapping_dirty) {
			drv->angle_snap = (profile->angle_snapping != 0);
			dpi_changed = true;  /* angle snap is in the DPI packet */
		}

		/* ── Send DPI packet if anything relevant changed ────────── */
		if (dpi_changed || profile->angle_snapping_dirty) {
			size_t sz = drv->is_wireless
				? ATTACKSHARK_DPI_SIZE_WIRELESS
				: ATTACKSHARK_DPI_SIZE_WIRED;
			uint8_t buf[ATTACKSHARK_DPI_SIZE_WIRELESS];
			attackshark_build_dpi_packet(buf, sz, drv);
			rc = attackshark_send(device, ATTACKSHARK_REPORT_DPI, buf, sz);
			if (rc)
				return rc;
		}

		/* ── Polling rate ────────────────────────────────────────── */
		if (profile->rate_dirty) {
			drv->rate = profile->hz;
			uint8_t buf[ATTACKSHARK_RATE_SIZE];
			attackshark_build_rate_packet(buf, drv->rate);
			rc = attackshark_send(device, ATTACKSHARK_REPORT_RATE, buf,
					      ATTACKSHARK_RATE_SIZE);
			if (rc)
				return rc;
		}

		/* ── Debounce + LED (both live in the user-prefs packet) ── */
		bool prefs_changed = false;

		if (profile->debounce_dirty) {
			drv->debounce = profile->debounce;
			prefs_changed = true;
		}

		struct ratbag_led *led;
		ratbag_profile_for_each_led(profile, led) {
			if (!led->dirty)
				continue;
			drv->led_mode        = led->mode;
			drv->led_color.red   = led->color.red;
			drv->led_color.green = led->color.green;
			drv->led_color.blue  = led->color.blue;
			drv->led_ms          = led->ms;
			prefs_changed = true;
		}

		if (prefs_changed) {
			size_t sz = drv->is_wireless
				? ATTACKSHARK_PREFS_SIZE_WIRELESS
				: ATTACKSHARK_PREFS_SIZE_WIRED;
			uint8_t buf[ATTACKSHARK_PREFS_SIZE_WIRELESS];
			attackshark_build_prefs_packet(buf, sz, drv);
			rc = attackshark_send(device, ATTACKSHARK_REPORT_PREFS, buf, sz);
			if (rc)
				return rc;
		}

		/* ── Button mapping ──────────────────────────────────────── */
		/* Always send the button packet on any commit so that the firmware
		 * is kept in sync with the ratbag button map (write-only device
		 * never reads back state, so we push the full map every time). */
		{
			uint8_t buf[ATTACKSHARK_BUTTONS_SIZE];
			attackshark_build_buttons_packet(buf, device, profile);
			rc = attackshark_send(device, ATTACKSHARK_REPORT_BUTTONS, buf,
					      ATTACKSHARK_BUTTONS_SIZE);
			if (rc)
				return rc;
		}
	}

	return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Remove
 * ═══════════════════════════════════════════════════════════════════════════ */

static void
attackshark_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Driver registration
 * ═══════════════════════════════════════════════════════════════════════════ */

struct ratbag_driver attackshark_driver = {
	.name   = "Attack Shark",
	.id     = "attackshark",
	.probe  = attackshark_probe,
	.remove = attackshark_remove,
	.commit = attackshark_commit,
};
