/*
 * Copyright © 2026 Fawaz Alghzawi
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
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Mionix Castor driver.
 *
 * Protocol reverse-engineered from scratch; see
 * https://github.com/fawaz7/castty (research/PROTOCOL.md) for the full
 * field map and the captures every claim is checked against.
 *
 * Lift-off distance ([86]) and angle tuning ([104]) are decoded and
 * preserved across a read/modify/write, but deliberately not exposed:
 * libratbag has no API for either, so there is nothing to map them onto.
 */

#include "config.h"

#include <errno.h>
#include <string.h>

/* libratbag-private.h pulls in libratbag-hidraw.h for us */
#include "libratbag-private.h"
#include "driver-mionix-blob.h"

#define MIONIX_REPORT_CMD		0x60
#define MIONIX_REPORT_PROFILE		0x61
#define MIONIX_CMD_FRAME_LEN		64

#define MIONIX_CMD_COMMIT		0x04
#define MIONIX_CMD_READ_PROFILE		0x07
#define MIONIX_CMD_WRITE_PROFILE	0x08

#define MIONIX_USAGE_PAGE		0xFF01

/*
 * The device answers a read with an all-zero buffer, rather than refusing, for
 * a window after enumeration. castty's research/PROTOCOL.md measured it still
 * doing so "about five seconds in", so a budget shorter than that fails probe
 * on any mouse that was simply plugged in a moment ago -- or on a ratbagd
 * started at boot alongside the device. Retry against a wall-clock deadline
 * comfortably past the measured five seconds rather than a fixed attempt count,
 * so a slow ioctl cannot eat the budget.
 */
#define MIONIX_READ_WARMUP_MEASURED_MS	5000
#define MIONIX_READ_DEADLINE_MS		8000
#define MIONIX_READ_SETTLE_MS		50
#define MIONIX_READ_RETRY_MS		100

/*
 * The retry loop needs a device and a hidraw fd, so it cannot be exercised from
 * the offline test suite. Pin the one thing that actually regressed -- a budget
 * shorter than the measured warm-up window -- where the compiler can see it.
 */
_Static_assert(MIONIX_READ_DEADLINE_MS > MIONIX_READ_WARMUP_MEASURED_MS,
	       "read budget must outlast the measured warm-up window");

struct mionix_data {
	uint8_t profiles[MIONIX_NUM_PROFILES][MIONIX_BLOB_LEN];
};

/*
 * The only rates the polling divisor at [37] can express. Advertised to
 * libratbag on read and used to reject anything else on write: the codec
 * silently leaves an unsupported rate alone, which would otherwise be
 * indistinguishable from "nothing changed".
 */
static const unsigned int mionix_report_rates[] = { 125, 250, 500, 1000 };

static bool
mionix_rate_is_supported(unsigned int hz)
{
	unsigned int i;

	for (i = 0; i < ARRAY_LENGTH(mionix_report_rates); i++) {
		if (mionix_report_rates[i] == hz)
			return true;
	}

	return false;
}

static int
mionix_match_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_get_usage_page(device, MIONIX_REPORT_CMD) ==
	       MIONIX_USAGE_PAGE;
}

static int
mionix_read_profile(struct ratbag_device *device, unsigned int index)
{
	struct mionix_data *drv_data = ratbag_get_drv_data(device);
	uint8_t cmd[MIONIX_CMD_FRAME_LEN] = { 0 };
	uint8_t *blob = drv_data->profiles[index];
	unsigned int attempt = 0;
	uint64_t ts, ts_end;
	int rc;

	cmd[0] = MIONIX_REPORT_CMD;
	cmd[MIONIX_OFF_CMD] = MIONIX_CMD_READ_PROFILE;
	cmd[MIONIX_OFF_PROFILE_INDEX] = (uint8_t)index;

	ts = now(CLOCK_MONOTONIC_RAW) / 1000 / 1000;
	ts_end = ts + MIONIX_READ_DEADLINE_MS;

	do {
		attempt++;

		rc = ratbag_hidraw_set_feature_report(device, MIONIX_REPORT_CMD,
						     cmd, sizeof(cmd));
		if (rc < 0)
			return rc;

		msleep(MIONIX_READ_SETTLE_MS);

		rc = ratbag_hidraw_get_feature_report(device, MIONIX_REPORT_PROFILE,
						     blob, MIONIX_BLOB_LEN);
		if (rc < 0)
			return rc;

		/*
		 * Validate against what actually arrived, not against the
		 * buffer size: ratbag_hidraw_raw_request() copies only the
		 * bytes the ioctl returned, so a short read leaves the tail
		 * of the buffer at whatever it held before -- zeros on the
		 * first attempt. Every field the validator inspects lives in
		 * the first ~110 bytes, so a truncated reply would otherwise
		 * pass with the 880-byte macro region blank, and that blank
		 * region would then be written back to flash.
		 */
		if (mionix_blob_is_valid(blob, (size_t)rc))
			return 0;

		/*
		 * For a few seconds after enumeration the device answers with
		 * zeros instead of refusing. Writing a field into that buffer
		 * and committing it would erase the profile, so never accept
		 * an unvalidated read.
		 */
		msleep(MIONIX_READ_RETRY_MS);

		ts = now(CLOCK_MONOTONIC_RAW) / 1000 / 1000;
	} while (ts < ts_end);

	log_error(device->ratbag,
		  "mionix: profile %u never returned a valid blob (%u attempts over %ums)\n",
		  index, attempt, MIONIX_READ_DEADLINE_MS);
	return -ENODEV;
}

static enum ratbag_led_mode
mionix_mode_to_ratbag(uint8_t mode)
{
	if (MIONIX_MODE_RAINBOW(mode))
		return RATBAG_LED_CYCLE;

	switch (MIONIX_MODE_ANIMATION(mode)) {
	case MIONIX_ANIM_SOLID:
	case MIONIX_ANIM_BLINKING:	/* no libratbag equivalent */
		return RATBAG_LED_ON;
	case MIONIX_ANIM_PULSATING:	/* no libratbag equivalent */
	case MIONIX_ANIM_BREATHING:
		return RATBAG_LED_BREATHING;
	default:
		return RATBAG_LED_ON;
	}
}

static uint8_t
mionix_mode_from_ratbag(enum ratbag_led_mode mode, uint8_t current)
{
	switch (mode) {
	case RATBAG_LED_CYCLE:
		/* keep the animation, turn rainbow on */
		return MIONIX_MODE_MAKE(MIONIX_MODE_ANIMATION(current), true);
	case RATBAG_LED_BREATHING:
		return MIONIX_MODE_MAKE(MIONIX_ANIM_BREATHING, false);
	case RATBAG_LED_OFF:
	case RATBAG_LED_ON:
	default:
		return MIONIX_MODE_MAKE(MIONIX_ANIM_SOLID, false);
	}
}

static void
mionix_read_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct mionix_data *drv_data = ratbag_get_drv_data(device);
	const uint8_t *blob = drv_data->profiles[button->profile->index];
	struct ratbag_button_action action = { 0 };
	struct mionix_button raw;

	mionix_blob_get_button(blob, button->index, &raw);

	switch (raw.type) {
	case MIONIX_BTN_TYPE_MOUSE:
		action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
		/* param is a bitmask: 01 left, 02 right, 04 middle, 08/10 side */
		switch (raw.param) {
		case 0x01: action.action.button = 1; break;
		case 0x02: action.action.button = 2; break;
		case 0x04: action.action.button = 3; break;
		case 0x08: action.action.button = 4; break;
		case 0x10: action.action.button = 5; break;
		default:   action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN; break;
		}
		break;
	case MIONIX_BTN_TYPE_SCROLL:
		action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		action.action.special = (raw.param == 0x01) ?
			RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP :
			RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN;
		break;
	case MIONIX_BTN_TYPE_KEY:
		/*
		 * The blob stores a HID keyboard usage code, not a Linux
		 * keycode; ratbag_button_action.action.key wants the latter.
		 * A usage with no mapping comes back 0 (HID_KEY_RESERVED is
		 * itself mapped to 0), which is not a real key -- report
		 * UNKNOWN rather than a bogus KEY_RESERVED assignment.
		 */
		action.action.key = ratbag_hidraw_get_keycode_from_keyboard_usage(device, raw.param);
		if (action.action.key == 0)
			action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		else
			action.type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		break;
	case MIONIX_BTN_TYPE_PROFILE:
		action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		switch (raw.param) {
		case MIONIX_SWITCH_UP:
			action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP;
			break;
		case MIONIX_SWITCH_DOWN:
			action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN;
			break;
		default:
			/*
			 * The hardware has one roll parameter, not separate
			 * cycle-up/cycle-down values, so MIONIX_SWITCH_ROLL
			 * always reads back as CYCLE_UP; writing CYCLE_DOWN
			 * (see mionix_button_from_ratbag) is not distinguishable
			 * on read. Intentional, not a bug.
			 */
			action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP;
			break;
		}
		break;
	case MIONIX_BTN_TYPE_DPI:
		action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
		switch (raw.param) {
		case MIONIX_SWITCH_UP:
			action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP;
			break;
		case MIONIX_SWITCH_DOWN:
			action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN;
			break;
		default:
			/* see the profile case above: one hardware roll value,
			 * always reported as CYCLE_UP */
			action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
			break;
		}
		break;
	case MIONIX_BTN_TYPE_DISABLED:
		action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		break;
	case MIONIX_BTN_TYPE_MACRO:
	default:
		/* macros are preserved on write but not exposed */
		action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
		break;
	}

	ratbag_button_set_action(button, &action);

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	/*
	 * Deliberately not RATBAG_BUTTON_ACTION_TYPE_MACRO: macro assignment
	 * is rejected with -ENOTSUP (see mionix_button_from_ratbag) rather
	 * than silently dropped, so advertising it as a capability would
	 * invite a failure instead of a clean refusal.
	 */
}

static int
mionix_button_from_ratbag(struct ratbag_device *device,
			  const struct ratbag_button_action *action,
			  struct mionix_button *out)
{
	uint8_t code;

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		out->type = MIONIX_BTN_TYPE_DISABLED;
		out->param = 0x00;
		return 0;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		out->type = MIONIX_BTN_TYPE_MOUSE;
		switch (action->action.button) {
		case 1: out->param = 0x01; return 0;
		case 2: out->param = 0x02; return 0;
		case 3: out->param = 0x04; return 0;
		case 4: out->param = 0x08; return 0;
		case 5: out->param = 0x10; return 0;
		default: return -EINVAL;
		}
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		/* action->action.key is a Linux keycode; the blob wants a HID
		 * keyboard usage code. A keycode with no HID usage comes back
		 * 0, which is not a valid usage -- reject rather than write a
		 * bogus code. */
		code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, action->action.key);
		if (code == 0)
			return -EINVAL;
		out->type = MIONIX_BTN_TYPE_KEY;
		out->param = code;
		return 0;
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		switch (action->action.special) {
		case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
			out->type = MIONIX_BTN_TYPE_SCROLL;
			out->param = 0x01;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
			out->type = MIONIX_BTN_TYPE_SCROLL;
			out->param = 0xFF;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP:
			out->type = MIONIX_BTN_TYPE_PROFILE;
			out->param = MIONIX_SWITCH_UP;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN:
			out->type = MIONIX_BTN_TYPE_PROFILE;
			out->param = MIONIX_SWITCH_DOWN;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP:
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_DOWN:
			/*
			 * Only one hardware roll parameter exists, so both
			 * cycle directions write the same byte; reading it
			 * back always yields CYCLE_UP (see mionix_read_button).
			 */
			out->type = MIONIX_BTN_TYPE_PROFILE;
			out->param = MIONIX_SWITCH_ROLL;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:
			out->type = MIONIX_BTN_TYPE_DPI;
			out->param = MIONIX_SWITCH_UP;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:
			out->type = MIONIX_BTN_TYPE_DPI;
			out->param = MIONIX_SWITCH_DOWN;
			return 0;
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN:
			/* see the profile case above: one hardware roll value */
			out->type = MIONIX_BTN_TYPE_DPI;
			out->param = MIONIX_SWITCH_ROLL;
			return 0;
		default:
			return -EINVAL;
		}
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
	default:
		return -ENOTSUP;
	}
}

static void
mionix_init_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct mionix_data *drv_data = ratbag_get_drv_data(device);
	const uint8_t *blob = drv_data->profiles[profile->index];
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;
	struct ratbag_button *button;
	uint8_t active_dpi = mionix_blob_get_active_dpi(blob);
	uint8_t mode = mionix_blob_get_mode(blob);
	char name[MIONIX_NAME_LEN + 1];

	ratbag_profile_set_report_rate_list(profile, mionix_report_rates,
					    ARRAY_LENGTH(mionix_report_rates));
	profile->hz = mionix_blob_get_report_rate(blob);

	/*
	 * The device stores angle snapping as a strength level 0-15, but
	 * libratbag's AngleSnapping is a boolean carried in an int: doc/dbus.rst
	 * defines it as "1 or 0, or -1", ratbagctl prints bool(), and
	 * driver-asus.c passes a bool to its setter. So report !!level rather
	 * than the raw byte -- a device at level 9 must not be reported as 9 --
	 * and keep the level itself in the cached blob, where mionix_write_profile()
	 * reads it back when it is asked to enable snapping.
	 */
	profile->angle_snapping = !!mionix_blob_get_angle_snapping(blob);

	/*
	 * [66] is constant 0x02 in every capture, and its meaning is only
	 * inferred; a value outside the three DPI steps would leave libratbag
	 * with no active resolution at all, which it does not expect.
	 */
	if (active_dpi >= MIONIX_NUM_RESOLUTIONS) {
		log_info(device->ratbag,
			 "mionix: profile %u: active DPI step %u is out of range; using step 0\n",
			 profile->index, active_dpi);
		active_dpi = 0;
	}

	/*
	 * Logged, not exposed as profile->name: a non-NULL name is how
	 * libratbag advertises a renameable profile, and this codec has no
	 * name setter, so setting it would let a client rename the profile,
	 * mark it dirty, and have the rename silently fail to persist.
	 */
	mionix_blob_get_name(blob, name, sizeof(name));
	log_debug(device->ratbag, "mionix: profile %u is '%s'\n",
		  profile->index, name);

	ratbag_profile_for_each_resolution(profile, resolution) {
		uint16_t x, y;

		mionix_blob_get_resolution(blob, resolution->index, &x, &y);
		ratbag_resolution_set_resolution(resolution, x, y);
		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
		ratbag_resolution_set_dpi_list_from_range(resolution,
							 MIONIX_DPI_MIN,
							 MIONIX_DPI_MAX);
		resolution->is_active = (resolution->index == active_dpi);
	}

	ratbag_profile_for_each_led(profile, led) {
		struct mionix_led colour;

		mionix_blob_get_led(blob, led->index, &colour);
		led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
		led->color.red = colour.red;
		led->color.green = colour.green;
		led->color.blue = colour.blue;
		/*
		 * RATBAG_LED_OFF never round-trips: the hardware has no off
		 * bit, so the write path expresses "off" as colour 0,0,0 and
		 * that reads back as RATBAG_LED_ON with a black colour.
		 */
		led->mode = mionix_mode_to_ratbag(mode);

		ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
		ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
		ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
	}

	ratbag_profile_for_each_button(profile, button)
		mionix_read_button(button);
}

static int
mionix_probe(struct ratbag_device *device)
{
	struct mionix_data *drv_data;
	struct ratbag_profile *profile;
	int rc;

	rc = ratbag_find_hidraw(device, mionix_match_hidraw);
	if (rc)
		return rc;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	ratbag_device_init_profiles(device,
				    MIONIX_NUM_PROFILES,
				    MIONIX_NUM_RESOLUTIONS,
				    MIONIX_NUM_BUTTONS,
				    MIONIX_NUM_LEDS);

	ratbag_device_for_each_profile(device, profile) {
		rc = mionix_read_profile(device, profile->index);
		if (rc)
			goto err;

		mionix_init_profile(profile);
	}

	/*
	 * The device has no command to report which profile is currently
	 * live: the commit frame (see castty's research/PROTOCOL.md) always
	 * carries a profile index, so committing any profile makes it the
	 * active one, but nothing lets the host read that index back
	 * afterwards, and none of the five blobs read above carry an
	 * activeness marker of their own.
	 *
	 * libratbag requires exactly one profile flagged active, so this is
	 * a declared default -- profile 0 -- not a detection. A user who is
	 * physically on a different profile and edits the one libratbag
	 * reports as active will be switched onto profile 0 the next time
	 * anything is committed; that follows from the protocol, not from a
	 * choice this driver makes.
	 */
	ratbag_device_for_each_profile(device, profile) {
		if (profile->index == 0) {
			profile->is_active = true;
			break;
		}
	}

	return 0;

err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	ratbag_close_hidraw(device);
	return rc;
}

static void
mionix_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
	ratbag_set_drv_data(device, NULL);
}

/*
 * Patch a copy of the cached blob and, if that changed anything, push the
 * copy to the device. Writing does not make the profile live: the commit
 * that does that is sent once, by mionix_commit(), after every dirty
 * profile has been written.
 *
 * drv_data->profiles[index] means "what the device holds" -- it was read
 * back from flash, and every byte this driver does not decode, the macro
 * region included, is trusted and rewritten verbatim on the next write.
 * So the patches go into a scratch buffer and the cache is updated only
 * once both device writes have succeeded. Patching the cache in place
 * would break that invariant the moment a write failed: the cache would
 * describe a device state that never happened, and the retry's snapshot
 * comparison would then find nothing to do and strand the change.
 *
 * *written is set only when bytes actually went to the device.
 */
static int
mionix_write_profile(struct ratbag_device *device, struct ratbag_profile *profile,
		     bool *written)
{
	struct mionix_data *drv_data = ratbag_get_drv_data(device);
	uint8_t *cache = drv_data->profiles[profile->index];
	uint8_t scratch[MIONIX_BLOB_LEN];
	uint8_t frame[MIONIX_BLOB_LEN] = { 0 };
	struct mionix_button raw_buttons[MIONIX_NUM_BUTTONS];
	bool button_dirty[MIONIX_NUM_BUTTONS];
	struct ratbag_resolution *resolution;
	struct ratbag_button *button;
	struct ratbag_led *led;
	uint8_t framing[5];
	uint8_t level;
	uint8_t last_mode = 0;
	bool have_mode = false;
	unsigned int i;
	int rc;

	*written = false;

	/*
	 * Encode every dirty button before anything is patched. A keycode with
	 * no HID usage makes mionix_button_from_ratbag() fail, and any client
	 * can ask for one; there is no point building a frame we already know
	 * we will refuse to send.
	 */
	memset(raw_buttons, 0, sizeof(raw_buttons));
	memset(button_dirty, 0, sizeof(button_dirty));

	ratbag_profile_for_each_button(profile, button) {
		if (!button->dirty)
			continue;

		if (button->index >= MIONIX_NUM_BUTTONS)
			continue;

		rc = mionix_button_from_ratbag(device, &button->action,
					       &raw_buttons[button->index]);
		if (rc) {
			log_error(device->ratbag,
				  "mionix: profile %u button %u: cannot encode action type %d (%s); nothing written\n",
				  profile->index, button->index,
				  button->action.type,
				  rc == -ENOTSUP ? "unsupported action" :
						   "no hardware equivalent");
			return rc;
		}

		button_dirty[button->index] = true;
	}

	/*
	 * Reject an unsupported report rate up front, for the same reason the
	 * buttons are encoded up front. Nothing above this driver filters it:
	 * ratbagd only clamps to 125..8000 and ratbag_profile_set_report_rate()
	 * does not check the advertised list. mionix_blob_set_report_rate()
	 * leaves the stored divisor alone for a rate it cannot express, which
	 * would be indistinguishable from "nothing changed" -- the memcmp below
	 * would find no difference, this function would return 0, and libratbag
	 * would clear every dirty flag. The client would then be told 333 Hz had
	 * been applied to a mouse still running at 1000.
	 */
	if (profile->rate_dirty && !mionix_rate_is_supported(profile->hz)) {
		log_error(device->ratbag,
			  "mionix: profile %u: report rate %u Hz is not one of 125/250/500/1000; nothing written\n",
			  profile->index, profile->hz);
		return -EINVAL;
	}

	/*
	 * Everything from here patches the scratch copy, never the cache. See
	 * the comment above the function.
	 */
	memcpy(scratch, cache, MIONIX_BLOB_LEN);

	/* Patch only what we own; everything else survives untouched. */
	ratbag_profile_for_each_resolution(profile, resolution) {
		if (!resolution->dirty)
			continue;
		mionix_blob_set_resolution(scratch, resolution->index,
					   (uint16_t)resolution->dpi_x,
					   (uint16_t)resolution->dpi_y);
	}

	if (profile->rate_dirty)
		mionix_blob_set_report_rate(scratch, profile->hz);

	if (profile->angle_snapping_dirty) {
		/*
		 * A boolean request onto a 0-15 level (see mionix_init_profile).
		 * Enabling must not quietly change the strength the device was
		 * already carrying, so reuse the stored level and only fall back
		 * to a default when snapping is currently off. Anything <= 0 --
		 * including the -1 that means "unsupported" -- reads as off.
		 */
		level = mionix_blob_get_angle_snapping(scratch);

		if (profile->angle_snapping <= 0)
			level = 0;
		else if (level == 0)
			level = MIONIX_ANGLE_SNAPPING_ON;

		log_debug(device->ratbag,
			  "mionix: profile %u: angle snapping %s, level %u\n",
			  profile->index, level ? "on" : "off", level);
		mionix_blob_set_angle_snapping(scratch, level);
	}

	ratbag_profile_for_each_led(profile, led) {
		struct mionix_led colour;
		uint8_t mode;

		if (!led->dirty)
			continue;

		if (led->mode == RATBAG_LED_OFF) {
			colour.red = colour.green = colour.blue = 0;
		} else {
			colour.red = led->color.red;
			colour.green = led->color.green;
			colour.blue = led->color.blue;
		}

		mionix_blob_set_led(scratch, led->index, &colour);

		mode = mionix_mode_from_ratbag(led->mode,
					       mionix_blob_get_mode(scratch));
		/*
		 * The mode byte is global -- one byte for the whole device --
		 * so two dirty LEDs asking for different modes cannot both be
		 * honoured; the last one processed wins. That mirrors the
		 * hardware, but say so rather than let the first request
		 * vanish silently.
		 */
		if (have_mode && mode != last_mode)
			log_info(device->ratbag,
				 "mionix: profile %u: LED %u wants mode 0x%02x but the mode byte is global; 0x%02x from an earlier LED is overridden\n",
				 profile->index, led->index, mode, last_mode);

		mionix_blob_set_mode(scratch, mode);
		last_mode = mode;
		have_mode = true;
	}

	for (i = 0; i < MIONIX_NUM_BUTTONS; i++) {
		if (!button_dirty[i])
			continue;

		mionix_blob_set_button(scratch, i, &raw_buttons[i]);
	}

	/*
	 * ratbagd exposes controls this driver does not map (debounce, LED
	 * brightness and effect duration, Resolution.SetDefault); each of them
	 * marks the profile dirty. A commit that changes nothing we encode
	 * must cost no flash wear, and the cache is untouched either way.
	 */
	if (memcmp(scratch, cache, MIONIX_BLOB_LEN) == 0) {
		log_debug(device->ratbag,
			  "mionix: profile %u is dirty but nothing this driver maps changed; not writing\n",
			  profile->index);
		return 0;
	}

	/*
	 * mionix_blob_prepare_write() stamps the write framing -- bytes 0, 1, 5,
	 * 6 and 16 -- into the scratch copy. Those five bytes are the one part
	 * of the frame that is not what the device would hand back on a read
	 * (a read reply leads 00 01 and carries 0 in all three), so keep the
	 * cache's own copies and restore them after the adopt below. Nothing
	 * reads them today, but the cache is only useful because it is byte-
	 * exact what the device holds, and re-reading or re-validating it later
	 * is the obvious next change.
	 */
	framing[0] = cache[0];
	framing[1] = cache[MIONIX_OFF_CMD];
	framing[2] = cache[MIONIX_OFF_PROFILE_INDEX];
	framing[3] = cache[MIONIX_OFF_TERMINATOR_FLAG];
	framing[4] = cache[MIONIX_OFF_PROFILE_INDEX_DUP];

	mionix_blob_prepare_write(scratch, (uint8_t)profile->index);

	rc = ratbag_hidraw_set_feature_report(device, MIONIX_REPORT_PROFILE,
					      scratch, MIONIX_BLOB_LEN);
	if (rc < 0)
		return rc;

	msleep(50);

	/* short terminator form */
	frame[0] = MIONIX_REPORT_PROFILE;
	frame[MIONIX_OFF_CMD] = MIONIX_CMD_WRITE_PROFILE;
	frame[MIONIX_OFF_PROFILE_INDEX] = (uint8_t)profile->index;
	frame[MIONIX_OFF_TERMINATOR_FLAG] = 0x01;

	rc = ratbag_hidraw_set_feature_report(device, MIONIX_REPORT_PROFILE,
					      frame, MIONIX_BLOB_LEN);
	if (rc < 0)
		return rc;

	msleep(50);

	/*
	 * Both writes landed, so the device now holds what the scratch buffer
	 * holds: adopt it. Until this point every early return above has left
	 * the cache describing the device exactly as it still is, so a retry
	 * sees a real difference and writes again instead of concluding there
	 * is nothing to do.
	 */
	memcpy(cache, scratch, MIONIX_BLOB_LEN);

	cache[0] = framing[0];
	cache[MIONIX_OFF_CMD] = framing[1];
	cache[MIONIX_OFF_PROFILE_INDEX] = framing[2];
	cache[MIONIX_OFF_TERMINATOR_FLAG] = framing[3];
	cache[MIONIX_OFF_PROFILE_INDEX_DUP] = framing[4];

	*written = true;

	return 0;
}

static int
mionix_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	uint8_t cmd[MIONIX_CMD_FRAME_LEN] = { 0 };
	int rc;

	cmd[0] = MIONIX_REPORT_CMD;
	cmd[MIONIX_OFF_CMD] = MIONIX_CMD_COMMIT;
	cmd[MIONIX_OFF_PROFILE_INDEX] = (uint8_t)index;

	rc = ratbag_hidraw_set_feature_report(device, MIONIX_REPORT_CMD,
					      cmd, sizeof(cmd));
	return rc < 0 ? rc : 0;
}

static int
mionix_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	unsigned int commit_index = 0;
	bool found_active = false;
	bool skip_apply = false;
	bool written = false;
	int first_error = 0;
	int rc;

	/*
	 * Attempt every dirty profile, and keep the first error instead of
	 * returning on it. A profile that writes successfully has only staged
	 * its blob: nothing takes effect until the single apply frame below.
	 * Returning early on a later profile's failure would leave the earlier
	 * one staged but inert, and it would not self-correct -- the caller
	 * retries with both profiles still dirty, the same profile fails at
	 * the same point, and the snapshot guard in mionix_write_profile() now
	 * sees the already-patched cached blob as unchanged, so the good
	 * profile is never rewritten or applied again.
	 */
	ratbag_device_for_each_profile(device, profile) {
		bool profile_written = false;

		if (!profile->dirty)
			continue;

		rc = mionix_write_profile(device, profile, &profile_written);
		if (rc && !first_error)
			first_error = rc;

		written |= profile_written;
	}

	/*
	 * Nothing reached the device, so there is nothing to make live: issue
	 * no commit at all rather than an apply that only costs flash wear.
	 */
	if (!written)
		return first_error;

	/*
	 * One commit for the whole batch, matching the vendor's own captured
	 * sequence: blob plus terminator per profile, then a single apply.
	 *
	 * Byte 5 of that frame is the profile the mouse switches to, so the
	 * index chosen here decides what the user is left on. It must be the
	 * profile libratbag considers active, never the last one written --
	 * editing a setting on profile 3 must not move the user onto it. An
	 * explicit profile switch arrives separately, through
	 * set_active_profile, which ratbag_device_commit() calls for us.
	 */
	ratbag_device_for_each_profile(device, profile) {
		if (profile->is_active) {
			commit_index = profile->index;
			found_active = true;

			/*
			 * ratbag_device_commit() sends set_active_profile for
			 * the active profile immediately after this function
			 * returns, whenever the switch is what made it dirty --
			 * and that is the very same 0x60/0x04 frame with the
			 * very same index. Let it do the apply rather than
			 * spend a second flash write on a duplicate.
			 */
			skip_apply = profile->is_active_dirty;
			break;
		}
	}

	if (!found_active)
		log_bug_libratbag(device->ratbag,
				  "mionix: no profile is marked active; applying profile 0\n");

	/*
	 * Only on a clean batch, though: ratbag_device_commit() returns as soon
	 * as this function reports an error and never reaches its own
	 * set_active_profile() call, so deferring to it after a failure would
	 * leave the profiles that did write staged and inert -- the very thing
	 * the loop above exists to prevent.
	 */
	if (skip_apply && !first_error) {
		log_debug(device->ratbag,
			  "mionix: profile %u is about to be activated by libratbag; skipping the duplicate apply\n",
			  commit_index);
		return 0;
	}

	/*
	 * A partial batch still has to be applied -- the profiles that did
	 * write are on the device and only the apply makes them live. Report
	 * the failure afterwards; stranding staged data would be worse.
	 */
	rc = mionix_set_current_profile(device, commit_index);
	if (rc && !first_error)
		first_error = rc;

	return first_error;
}

struct ratbag_driver mionix_driver = {
	.name = "Mionix Castor",
	.id = "mionix",
	.probe = mionix_probe,
	.remove = mionix_remove,
	.commit = mionix_commit,
	.set_active_profile = mionix_set_current_profile,
};
