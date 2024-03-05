/*
 * Copyright © 2016 Red Hat, Inc.
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
#include "config.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#include <errno.h>
#include <stdbool.h>

#define GSKILL_PROFILE_MAX  5
#define GSKILL_NUM_DPI      5
#define GSKILL_BUTTON_MAX  10
#define GSKILL_NUM_LED      0

#define GSKILL_MAX_POLLING_RATE 1000

#define GSKILL_MIN_DPI   100
#define GSKILL_MAX_DPI  8200
#define GSKILL_DPI_UNIT   50

/* Commands */
#define GSKILL_GET_CURRENT_PROFILE_NUM 0x03
#define GSKILL_GET_SET_MACRO           0x04
#define GSKILL_GET_SET_PROFILE         0x05
#define GSKILL_GENERAL_CMD             0x0c

#define GSKILL_REPORT_SIZE_PROFILE  644
#define GSKILL_REPORT_SIZE_CMD        9
#define GSKILL_REPORT_SIZE_MACRO   2052

#define GSKILL_CHECKSUM_OFFSET 3

/* Command status codes */
#define GSKILL_CMD_SUCCESS     0xb0
#define GSKILL_CMD_IN_PROGRESS 0xb1
#define GSKILL_CMD_FAILURE     0xb2
#define GSKILL_CMD_IDLE        0xb3

/* LED groups. DPI is omitted here since it's handled specially */
#define GSKILL_LED_TYPE_LOGO  0
#define GSKILL_LED_TYPE_WHEEL 1
#define GSKILL_LED_TYPE_TAIL  2
#define GSKILL_LED_TYPE_COUNT 3

#define GSKILL_KBD_MOD_CTRL_LEFT   AS_MASK(0)
#define GSKILL_KBD_MOD_SHIFT_LEFT  AS_MASK(1)
#define GSKILL_KBD_MOD_ALT_LEFT    AS_MASK(2)
#define GSKILL_KBD_MOD_SUPER_LEFT  AS_MASK(3)
#define GSKILL_KBD_MOD_CTRL_RIGHT  AS_MASK(4)
#define GSKILL_KBD_MOD_SHIFT_RIGHT AS_MASK(5)
#define GSKILL_KBD_MOD_ALT_RIGHT   AS_MASK(6)
#define GSKILL_KBD_MOD_SUPER_RIGHT AS_MASK(7)

struct gskill_raw_dpi_level {
	uint8_t x;
	uint8_t y;
} __attribute__((packed));

struct gskill_led_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} __attribute__((packed));

struct gskill_led_values {
	uint8_t brightness;
	struct gskill_led_color color;
} __attribute__((packed));

enum gskill_led_control_type {
	GSKILL_LED_ALL_OFF         = 0x0,
	GSKILL_LED_ALL_ON          = 0x1,
	GSKILL_LED_BREATHING       = 0x2,
	GSKILL_DPI_LED_RIGHT_CYCLE = 0x3,
	GSKILL_DPI_LED_LEFT_CYCLE  = 0x4
};

struct gskill_background_led_cfg {
	uint8_t brightness;
	struct gskill_led_color dpi[4];
	struct gskill_led_color leds[GSKILL_LED_TYPE_COUNT];
} __attribute__((packed));

struct gskill_dpi_led_group_cfg {
	uint8_t duration_step;
	uint8_t duration_high;
	uint8_t duration_low;
	uint8_t cycle_num;

	struct gskill_led_values steps[12];
};

struct gskill_led_group_cfg {
	enum gskill_led_control_type type :3;
	uint8_t                           :5; /* unused */

	uint8_t duration_step;
	uint8_t duration_high;
	uint8_t duration_low;
	uint8_t cycle_num;

	struct gskill_led_values steps[12];
} __attribute__((packed));

struct gskill_dpi_led_cycle_cfg {
	enum gskill_led_control_type type :3;
	uint8_t                           :5; /* unused */

	/* Don't worry, the low/high flip-flop here is intentional */
	uint8_t duration_low;
	uint8_t duration_high;
	uint8_t cycle_num;

	struct gskill_led_values cycles[12];
} __attribute__((packed));

/*
 * We may occasionally run into codes outside this, however those codes
 * indicate functionalities that aren't too useful for us
 */
enum gskill_button_function_type {
	GSKILL_BUTTON_FUNCTION_WHEEL                = 0x00,
	GSKILL_BUTTON_FUNCTION_MOUSE                = 0x01,
	GSKILL_BUTTON_FUNCTION_KBD                  = 0x02,
	GSKILL_BUTTON_FUNCTION_CONSUMER             = 0x03,
	GSKILL_BUTTON_FUNCTION_MACRO                = 0x06,
	GSKILL_BUTTON_FUNCTION_DPI_UP               = 0x09,
	GSKILL_BUTTON_FUNCTION_DPI_DOWN             = 0x0a,
	GSKILL_BUTTON_FUNCTION_CYCLE_DPI_UP         = 0x0b,
	GSKILL_BUTTON_FUNCTION_CYCLE_DPI_DOWN       = 0x0c,
	GSKILL_BUTTON_FUNCTION_PROFILE_SWITCH       = 0x0d,
	GSKILL_BUTTON_FUNCTION_TEMPORARY_CPI_ADJUST = 0x15,
	GSKILL_BUTTON_FUNCTION_DIRECT_DPI_CHANGE    = 0x16,
	GSKILL_BUTTON_FUNCTION_CYCLE_PROFILE_UP     = 0x18,
	GSKILL_BUTTON_FUNCTION_CYCLE_PROFILE_DOWN   = 0x19,
	GSKILL_BUTTON_FUNCTION_DISABLE              = 0xff
};

struct gskill_button_cfg {
	enum gskill_button_function_type type :8;
	union {
		struct {
			enum {
				GSKILL_WHEEL_SCROLL_UP = 0,
				GSKILL_WHEEL_SCROLL_DOWN = 1,
			} direction :8;
		} wheel;

		struct {
			enum {
				GSKILL_BTN_MASK_LEFT   = AS_MASK(0),
				GSKILL_BTN_MASK_RIGHT  = AS_MASK(1),
				GSKILL_BTN_MASK_MIDDLE = AS_MASK(2),
				GSKILL_BTN_MASK_SIDE   = AS_MASK(3),
				GSKILL_BTN_MASK_EXTRA  = AS_MASK(4)
			} button_mask :8;
		} mouse;

		struct {
			uint16_t code;
		} consumer;

		struct {
			uint8_t modifier_mask;
			uint8_t hid_code;
			/*
			 * XXX: Supposedly this is supposed to have additional
			 * parts of the kbd code, however that doesn't seem to
			 * be the case in practice…
			 */
			uint16_t :16;
		} kbd;

		struct {
			uint8_t level;
		} dpi;
	} params;
} __attribute__((packed));

struct gskill_action_mapping {
	struct gskill_button_cfg config;
	struct ratbag_button_action action;
};

struct gskill_profile_report {
	uint16_t                  :16;
	uint8_t profile_num;
	uint8_t checksum;
	uint8_t polling_rate      :4;
	uint8_t angle_snap_ratio  :4;
	uint8_t liftoff_value     :5;
	bool liftoff_enabled      :1;
	bool disable_leds_in_sleep:1;
	enum {
		GSKILL_LED_PROFILE_MODE_BACKGROUND = 0,
		GSKILL_LED_PROFILE_MODE_OTHER = 1,
	} led_profile_mode :1;
	uint8_t                   :8; /* unused */

	uint8_t current_dpi_level :4;
	uint8_t dpi_num           :4;
	struct gskill_raw_dpi_level dpi_levels[GSKILL_NUM_DPI];

	/* LEDs */
	struct gskill_background_led_cfg background_lighting;
	struct gskill_dpi_led_cycle_cfg led_dpi_cycle;
	struct gskill_dpi_led_group_cfg dpi_led;
	struct gskill_led_group_cfg leds[GSKILL_LED_TYPE_COUNT];

	/* Button assignments */
	uint8_t button_function_redirections[8];
	struct gskill_button_cfg btn_cfgs[GSKILL_BUTTON_MAX];

	/* A mystery */
	uint8_t _unused1[27];

	char name[256];
} __attribute__((packed));

_Static_assert(sizeof(struct gskill_profile_report) == GSKILL_REPORT_SIZE_PROFILE,
	       "Size of gskill_profile_report is wrong");

struct gskill_macro_delay {
	uint8_t tag; /* should be 0x1 to indicate delay */
	uint16_t count;
} __attribute__((packed));

struct gskill_macro_report {
	/* yes, the report can be both at offset 0 and 1 :( */
	union {
		struct {
			uint8_t result;
			uint8_t report_id;
		} read;
		struct {
			uint8_t report_id;
			uint8_t :8; /* unused */
		} write;
	} header;

	uint8_t macro_num;
	uint8_t checksum;

	enum {
		GSKILL_MACRO_METHOD_BUTTON_PRESS      = 0x5,
		GSKILL_MACRO_METHOD_BUTTON_RELEASE    = 0x1,
		GSKILL_MACRO_METHOD_BUTTON_LOOP_START = 0x7,
		GSKILL_MACRO_METHOD_BUTTON_LOOP_END   = 0x0
	} macro_exec_method :8;

	uint8_t loop_count;
	uint8_t please_set_me_to_1_thank_you;
	uint16_t macro_length;
	uint8_t macro_name_length;
	char macro_name[256];
	uint8_t macro_content[1786];
} __attribute__((packed));

_Static_assert(sizeof(struct gskill_macro_report) == GSKILL_REPORT_SIZE_MACRO,
	       "Size of gskill_macro_report is wrong");

struct gskill_button_function_mapping {
	enum gskill_button_function_type type;
	struct ratbag_button_action action;
};

static const struct gskill_button_function_mapping gskill_button_function_mapping[] = {
	{ GSKILL_BUTTON_FUNCTION_MACRO,              BUTTON_ACTION_MACRO },
	{ GSKILL_BUTTON_FUNCTION_DPI_UP,             BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ GSKILL_BUTTON_FUNCTION_DPI_DOWN,           BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ GSKILL_BUTTON_FUNCTION_CYCLE_DPI_UP,       BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ GSKILL_BUTTON_FUNCTION_CYCLE_PROFILE_UP,   BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) },
	{ GSKILL_BUTTON_FUNCTION_CYCLE_PROFILE_DOWN, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN) },
	{ GSKILL_BUTTON_FUNCTION_DISABLE,            BUTTON_ACTION_NONE },
};

struct gskill_profile_data {
	struct gskill_profile_report report;
	uint8_t res_idx_to_dev_idx[GSKILL_NUM_DPI];

	struct gskill_macro_report macros[GSKILL_BUTTON_MAX];
};

struct gskill_data {
	uint8_t profile_count;
	struct gskill_profile_data profile_data[GSKILL_PROFILE_MAX];
};

static inline struct gskill_profile_data *
profile_to_pdata(struct ratbag_profile *profile)
{
	struct gskill_data *drv_data = profile->device->drv_data;

	return &drv_data->profile_data[profile->index];
}

static const struct ratbag_button_action *
gskill_button_function_to_action(enum gskill_button_function_type type)
{
	const struct gskill_button_function_mapping *mapping;

	ARRAY_FOR_EACH(gskill_button_function_mapping, mapping) {
		if (mapping->type == type)
			return &mapping->action;
	}

	return NULL;
}

static enum gskill_button_function_type
gskill_button_function_from_action(const struct ratbag_button_action *action)
{
	const struct gskill_button_function_mapping *mapping;

	ARRAY_FOR_EACH(gskill_button_function_mapping, mapping) {
		if (ratbag_button_action_match(&mapping->action, action))
			return mapping->type;
	}

	return GSKILL_BUTTON_FUNCTION_DISABLE;
}

static uint8_t
gskill_calculate_checksum(const uint8_t *buf, size_t len)
{
	uint8_t checksum = 0;
	unsigned i;

	for (i = GSKILL_CHECKSUM_OFFSET + 1; i < len; i++)
		checksum += buf[i];

	checksum = ~checksum + 1;

	return checksum;
}

static int
gskill_general_cmd(struct ratbag_device *device,
		   uint8_t buf[GSKILL_REPORT_SIZE_CMD]) {
	int rc;
	int retries;

	assert(buf[0] == GSKILL_GENERAL_CMD);

	rc = ratbag_hidraw_raw_request(device, GSKILL_GENERAL_CMD, buf,
				       GSKILL_REPORT_SIZE_CMD,
				       HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (rc != GSKILL_REPORT_SIZE_CMD) {
		log_error(device->ratbag,
			  "Error while sending command to mouse: %d\n", rc);
		return rc < 0 ? rc : -EPROTO;
	}

	rc = -EAGAIN;
	for (retries = 0; retries < 10 && rc == -EAGAIN; retries++) {
		/*
		 * Wait for the device to be ready
		 * Spec says this should be 10ms, but 20ms seems to get the
		 * mouse to return slightly less nonsense responses
		 */
		msleep(20);

		rc = ratbag_hidraw_raw_request(device, 0, buf,
					       GSKILL_REPORT_SIZE_CMD,
					       HID_FEATURE_REPORT,
					       HID_REQ_GET_REPORT);
		/*
		 * Sometimes the mouse just doesn't send anything when it wants
		 * to tell us it's ready. In this case rc will be 0 and this
		 * function will succeed.
		 */
		if (rc < GSKILL_REPORT_SIZE_CMD)
			break;

		/* Check the command status bit */
		switch (buf[1]) {
		case 0: /* sometimes the mouse gets lazy and just returns a
			   blank buffer on success */
		case GSKILL_CMD_SUCCESS:
			rc = 0;
			break;

		case GSKILL_CMD_IN_PROGRESS:
			rc = -EAGAIN;
			continue;

		case GSKILL_CMD_IDLE:
			log_error(device->ratbag,
				  "Command response indicates idle status? Uh huh.\n");
			rc = -EPROTO;
			break;

		case GSKILL_CMD_FAILURE:
			log_error(device->ratbag, "Command failed\n");
			rc = -EIO;
			break;

		default:
			log_error(device->ratbag,
				  "Received unknown command status from mouse: 0x%x\n",
				  buf[1]);
			rc = -EPROTO;
			break;
		}
	}

	if (rc == -EAGAIN) {
		log_error(device->ratbag,
			  "Failed to get command response from mouse after %d tries, giving up\n",
			  retries);
		rc = -ETIMEDOUT;
	} else if (rc) {
		log_error(device->ratbag,
			  "Failed to perform command on mouse: %d\n",
			  rc);

		if (rc > 0)
			rc = -EPROTO;
	}

	return rc;
}

static int
gskill_get_active_profile_idx(struct ratbag_device *device)
{
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x7,
		0x0, 0x1 };
	int rc;

	rc = gskill_general_cmd(device, buf);
	if (rc) {
		log_error(device->ratbag,
			  "Error while getting active profile number from mouse: %d\n",
			  rc);
		return rc;
	}

	return buf[3];
}

static int
gskill_set_active_profile(struct ratbag_device *device, unsigned index)
{
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x7,
		index, 0x0 };
	int rc;

	rc = gskill_general_cmd(device, buf);
	if (rc) {
		log_error(device->ratbag,
			  "Error while changing active profile on mouse: %d\n",
			  rc);
		return rc;
	}

	return 0;
}

static int
gskill_get_profile_count(struct ratbag_device *device)
{
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x12,
		0x0, 0x1 };
	int rc;

	rc = gskill_general_cmd(device, buf);
	if (rc) {
		log_error(device->ratbag,
			  "Error while getting the number of profiles: %d\n",
			  rc);
		return rc;
	}

	log_debug(device->ratbag, "Profile count: %d\n", buf[3]);

	return buf[3];
}

static int
gskill_set_profile_count(struct ratbag_device *device, unsigned int count)
{
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x12,
		count, 0x0 };
	int rc;

	log_debug(device->ratbag, "Setting profile count to %d\n", count);

	rc = gskill_general_cmd(device, buf);
	if (rc) {
		log_error(device->ratbag,
			  "Error while setting the number of profiles: %d\n",
			  rc);
		return rc;
	}

	return 0;
}

/*
 * This is used for setting the profile index argument on the mouse for both
 * reading and writing profiles
 */
static int
gskill_select_profile(struct ratbag_device *device, unsigned index, bool write)
{
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x0c,
		index, write };
	int rc;

	/*
	 * While this looks like a normal command and should have the same
	 * behavior, trying to receive the command return status from the mouse
	 * breaks reading the profile
	 */
	rc = ratbag_hidraw_raw_request(device, GSKILL_GENERAL_CMD,
				       buf, sizeof(buf), HID_FEATURE_REPORT,
				       HID_REQ_SET_REPORT);
	if (rc != sizeof(buf)) {
		log_error(device->ratbag,
			  "Error while setting profile number to read/write: %d\n",
			  rc);
		return rc < 0 ? rc : -EPROTO;
	}

	return 0;
}

/*
 * Instructs the mouse to reload the data from a profile we've just written to
 * it.
 */
static int
gskill_reload_profile_data(struct ratbag_device *device)
{
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x0 };
	int rc;

	log_debug(device->ratbag, "Asking mouse to reload profile data\n");

	rc = gskill_general_cmd(device, buf);
	if (rc) {
		log_error(device->ratbag,
			  "Failed to get mouse to reload profile data: %d\n",
			  rc);
		return rc;
	}

	return 0;
}

static int
gskill_write_profile(struct ratbag_device *device,
		     struct gskill_profile_report *report)
{
	uint8_t *buf = (uint8_t*)report;
	int rc;

	/*
	 * The G.Skill configuration software doesn't take kindly to blank
	 * profile names, so ensure we have one
	 */
	if (report->name[0] == '\0') {
		log_debug(device->ratbag,
			  "Setting profile name to \"Ratbag profile %d\"\n",
			  report->profile_num);
		rc = ratbag_utf8_to_enc(report->name, sizeof(report->name),
					"UTF-16LE",
					"Ratbag profile %d",
					report->profile_num);
		if (rc < 0)
			return rc;
	}

	report->checksum = gskill_calculate_checksum(buf, sizeof(*report));

	rc = gskill_select_profile(device, report->profile_num, true);
	if (rc)
		return rc;

	/* Wait for the device to be ready */
	msleep(200);

	rc = ratbag_hidraw_raw_request(device, GSKILL_GET_SET_PROFILE,
				       buf, sizeof(*report), HID_FEATURE_REPORT,
				       HID_REQ_SET_REPORT);
	if (rc != sizeof(*report)) {
		log_error(device->ratbag,
			  "Error while writing profile: %d\n", rc);
		return rc < 0 ? rc : -EPROTO;
	}

	return 0;
}

static int
gskill_get_firmware_version(struct ratbag_device *device) {
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x08 };
	int rc;

	rc = gskill_general_cmd(device, buf);
	if (rc) {
		log_error(device->ratbag,
			  "Failed to read the firmware version of the mouse: %d\n",
			  rc);
		return rc;
	}

	return buf[4];
}

static unsigned int
gskill_mouse_button_macro_code_to_keycode(uint8_t code)
{
	unsigned int keycode;

	switch (code & 0x0f) {
	case 0x8: keycode = BTN_LEFT;   break;
	case 0x9: keycode = BTN_RIGHT;  break;
	case 0xa: keycode = BTN_MIDDLE; break;
	case 0xb: keycode = BTN_SIDE;   break;
	case 0xc: keycode = BTN_EXTRA;  break;
	default:  keycode = 0;          break;
	}

	return keycode;
}

static uint8_t
gskill_macro_code_from_event(struct ratbag_device *device,
			     struct ratbag_macro_event *event)
{
	uint8_t macro_code;

	/*
	 * The miscellaneous keycodes are ORd with 0x70 to indicate press, 0xF0
	 * to indicate release
	 */
	if (event->type == RATBAG_MACRO_EVENT_KEY_PRESSED)
		macro_code = 0x70;
	else
		macro_code = 0xF0;

	switch (event->event.key) {
	case KEY_LEFTCTRL:   macro_code |= 0x00; break;
	case KEY_LEFTSHIFT:  macro_code |= 0x01; break;
	case KEY_LEFTALT:    macro_code |= 0x02; break;
	case KEY_LEFTMETA:   macro_code |= 0x03; break;
	case KEY_RIGHTCTRL:  macro_code |= 0x04; break;
	case KEY_RIGHTSHIFT: macro_code |= 0x05; break;
	case KEY_RIGHTALT:   macro_code |= 0x06; break;
	case KEY_RIGHTMETA:  macro_code |= 0x07; break;
	case BTN_LEFT:       macro_code |= 0x08; break;
	case BTN_RIGHT:      macro_code |= 0x09; break;
	case BTN_MIDDLE:     macro_code |= 0x0a; break;
	case BTN_SIDE:       macro_code |= 0x0b; break;
	case BTN_EXTRA:      macro_code |= 0x0c; break;
	case KEY_SCROLLDOWN: macro_code  = 0x7e; break;
	case KEY_SCROLLUP:   macro_code  = 0xfe; break;
	default:
		macro_code = ratbag_hidraw_get_keyboard_usage_from_keycode(
		    device, event->event.key);

		if (event->type == RATBAG_MACRO_EVENT_KEY_RELEASED)
			macro_code += 0x80;
		break;
	}

	return macro_code;
}

static struct ratbag_button_macro *
gskill_macro_from_report(struct ratbag_device *device,
			 struct gskill_macro_report *report)
{
	struct ratbag_button_macro *macro;
	enum ratbag_macro_event_type type;
	const uint8_t *data = (uint8_t*)&report->macro_content;
	unsigned int event_data;
	int ret, i, event_idx, increment;

	/* The macro is empty */
	if (report->macro_length == 0xff) {
		return NULL;
	} else if (report->macro_length > sizeof(report->macro_content)) {
		log_error(device->ratbag,
			  "Macro length too large (max should be %ld, we got %d)\n",
			  sizeof(report->macro_content), report->macro_length);
		return NULL;
	}

	/*
	 * Since the length is only 8 bits long, it's impossible to specify a
	 * length that's too large for the macro name
	 */
	macro = ratbag_button_macro_new(NULL);
	ret = ratbag_utf8_from_enc(report->macro_name,
				   report->macro_name_length, "UTF-16LE",
				   &macro->macro.name);
	if (ret < 0) {
		ratbag_button_macro_unref(macro);
		return NULL;
	}

	for (i = 0, event_idx = 0, increment = 1;
	     i < report->macro_length;
	     i += increment, increment = 1, event_idx++) {
		const struct gskill_macro_delay *delay;

		switch (data[i]) {
		case 0x01: /* delay */
			delay = (struct gskill_macro_delay*)&data[i];
			increment = sizeof(struct gskill_macro_delay);

			type = RATBAG_MACRO_EVENT_WAIT;
			event_data = delay->count;

			break;
		case 0x04 ... 0x6a: /* HID KBD code, press */
			type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			event_data = ratbag_hidraw_get_keycode_from_keyboard_usage(
			    device, data[i]);
			break;
		case 0x70 ... 0x77: /* KBD modifier, press */
			type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			event_data = ratbag_hidraw_get_keycode_from_keyboard_usage(
			    device, data[i] + 0x70);
			break;
		case 0x78 ... 0x7c: /* Mouse button, press */
			type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			event_data = gskill_mouse_button_macro_code_to_keycode(
			    data[i]);
			break;
		case 0x7e: /* Scroll down */
			type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			event_data = KEY_SCROLLDOWN;
			break;
		case 0x84 ... 0xef: /* HID KBD code, release */
			type = RATBAG_MACRO_EVENT_KEY_RELEASED;
			event_data = ratbag_hidraw_get_keycode_from_keyboard_usage(
			    device, data[i] - 0x80);
			break;
		case 0xf0 ... 0xf7: /* KBD modifier, release */
			type = RATBAG_MACRO_EVENT_KEY_RELEASED;
			event_data = ratbag_hidraw_get_keycode_from_keyboard_usage(
			    device, data[i] - 0x10);
			break;
		case 0xf8 ... 0xfc: /* Mouse button, release */
			type = RATBAG_MACRO_EVENT_KEY_RELEASED;
			event_data = gskill_mouse_button_macro_code_to_keycode(
			    data[i]);
			break;
		case 0xfe: /* Scroll up */
			type = RATBAG_MACRO_EVENT_KEY_PRESSED;
			event_data = KEY_SCROLLUP;
			break;
		default:
			/* should never get there */
			type = RATBAG_MACRO_EVENT_INVALID;
			event_data = 0;
		}

		ratbag_button_macro_set_event(macro, event_idx, type,
					      event_data);
	}

	return macro;
}

/* FIXME: the macro struct here should be a const, but it looks like there's a
 * couple of functions in ratbag that need to have a const qualifier added to
 * their function declarations
 */
static struct gskill_macro_report *
gskill_macro_to_report(struct ratbag_device *device,
		       struct ratbag_button_macro *macro,
		       unsigned int profile, unsigned int button)
{
	struct gskill_data *drv_data = ratbag_get_drv_data(device);
	struct gskill_macro_report *report =
		&drv_data->profile_data[profile].macros[button];
	struct gskill_macro_delay *delay;
	unsigned int event_num = ratbag_button_macro_get_num_events(macro);
	struct ratbag_macro_event *event;
	uint8_t *buf = report->macro_content;
	int profile_pos, increment, event_idx;
	ssize_t ret;

	memset(report, 0, sizeof(*report));

	/*
	 * G.Skill's configuration software will cry if we don't have a name,
	 * so make sure we assign one
	 */
	if (!macro->macro.name || macro->macro.name[0] == '\0') {
		ret = ratbag_utf8_to_enc(report->macro_name,
					 sizeof(report->macro_name), "UTF-16LE",
					 "Ratbag macro for profile %d button %d",
					 profile, button);
	} else {
		ret = ratbag_utf8_to_enc(report->macro_name,
					 sizeof(report->macro_name), "UTF-16LE",
					 "%s", macro->macro.name);
	}

	if (ret < 0)
		return NULL;
	report->macro_name_length = ret;

	report->macro_num = (profile * 10) + button;
	report->macro_exec_method = GSKILL_MACRO_METHOD_BUTTON_PRESS;
	report->loop_count = 0;
	report->please_set_me_to_1_thank_you = 1; /* No prob! Happy to help :) */

	for (profile_pos = 0, increment = 1, event_idx = 0;
	     event_idx < (signed)event_num;
	     event_idx++, profile_pos += increment, increment = 1) {
		event = &macro->macro.events[event_idx];

		switch (event->type) {
		case RATBAG_MACRO_EVENT_WAIT:
			delay = (struct gskill_macro_delay*)&buf[profile_pos];
			increment = sizeof(*delay);

			delay->tag = 1;
			delay->count = event->event.timeout;
			break;
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			buf[profile_pos] = gskill_macro_code_from_event(device,
									event);
			break;
		case RATBAG_MACRO_EVENT_INVALID:
		case RATBAG_MACRO_EVENT_NONE:
			goto out;
		}
	}

out:
	report->macro_length = profile_pos;

	return report;
}

static int
gskill_select_macro(struct ratbag_device *device,
		    unsigned profile, unsigned button, bool write)
{
	uint8_t macro_num = (profile * 10) + button;
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x0b,
		macro_num, write };
	int rc;

	/*
	 * Just like in gskill_select_profile(), we can't use the normal
	 * command handler for this
	 */
	rc = ratbag_hidraw_raw_request(device, GSKILL_GENERAL_CMD, buf,
				       sizeof(buf), HID_FEATURE_REPORT,
				       HID_REQ_SET_REPORT);
	if (rc != sizeof(buf)) {
		log_error(device->ratbag,
			  "Error while setting macro number to read/write: %d\n",
			  rc);
		return rc < 0 ? rc : -EPROTO;
	}

	return 0;
}

static struct gskill_macro_report *
gskill_read_button_macro(struct ratbag_device *device,
			 unsigned int profile, unsigned int button)
{
	struct gskill_data *drv_data = ratbag_get_drv_data(device);
	struct gskill_macro_report *report =
		&drv_data->profile_data[profile].macros[button];
	uint8_t checksum;
	int rc;

	rc = gskill_select_macro(device, profile, button, false);
	if (rc)
		return NULL;

	/* Wait for the device to be ready */
	msleep(100);

	rc = ratbag_hidraw_raw_request(device, GSKILL_GET_SET_MACRO,
				       (uint8_t*)report, sizeof(*report),
				       HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
	if (rc < (signed)sizeof(*report)) {
		log_error(device->ratbag,
			  "Failed to retrieve macro for profile %d for button %d: %d\n",
			  profile, button, rc);
		return NULL;
	}

	checksum = gskill_calculate_checksum((uint8_t*)report, sizeof(*report));
	if (checksum != report->checksum) {
		log_error(device->ratbag,
			  "Invalid checksum on macro for profile %d button %d\n",
			  profile, button);
		return NULL;
	}

	return report;
}

static int
gskill_write_button_macro(struct ratbag_device *device,
			  struct gskill_macro_report *report)
{
	unsigned int profile = report->macro_num / 10;
	unsigned int button = report->macro_num % 10;
	int rc;

	rc = gskill_select_macro(device, profile, button, true);
	if (rc)
		return rc;

	/* Wait for the device to be ready */
	msleep(200);

	memset(&report->header, 0, sizeof(report->header));
	report->header.write.report_id = 0x4;
	report->checksum = gskill_calculate_checksum((uint8_t*)report,
						     sizeof(*report));

	rc = ratbag_hidraw_raw_request(device, GSKILL_GET_SET_MACRO,
				       (uint8_t*)report, sizeof(*report),
				       HID_FEATURE_REPORT, HID_REQ_SET_REPORT);
	if (rc < 0) {
		log_error(device->ratbag,
			  "Failed to write macro for profile %d button %d to mouse: %d\n",
			  profile, button, rc);
		return rc;
	}

	return 0;
}

static void
gskill_read_resolutions(struct ratbag_profile *profile,
			struct gskill_profile_report *report)
{
	struct gskill_profile_data *pdata = profile_to_pdata(profile);
	unsigned int rates[] = { 500, 1000 }; /* let's assume that is true */
	int dpi_x, dpi_y, hz, i;

	log_debug(profile->device->ratbag,
		  "Profile %d: DPI count is %d\n",
		  profile->index, report->dpi_num);

	hz = GSKILL_MAX_POLLING_RATE / (report->polling_rate + 1);
	ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));
	profile->hz = hz;

	for (i = 0; i < report->dpi_num; i++) {
		_cleanup_resolution_ struct ratbag_resolution *resolution = NULL;

		dpi_x = report->dpi_levels[i].x * GSKILL_DPI_UNIT;
		dpi_y = report->dpi_levels[i].y * GSKILL_DPI_UNIT;

		resolution = ratbag_profile_get_resolution(profile, i);
		ratbag_resolution_set_resolution(resolution, dpi_x, dpi_y);
		resolution->is_active = (i == report->current_dpi_level);
		pdata->res_idx_to_dev_idx[i] = i;

		ratbag_resolution_set_cap(resolution,
					  RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

		ratbag_resolution_set_dpi_list_from_range(resolution,
							  GSKILL_MIN_DPI, GSKILL_MAX_DPI);
	}
}

static void
gskill_read_profile_name(struct ratbag_device *device,
			 struct gskill_profile_report *report)
{
	char *name;
	int ret;

	ret = ratbag_utf8_from_enc(report->name, sizeof(report->name),
				   "UTF-16LE", &name);
	if (ret < 0) {
		log_debug(device->ratbag,
			  "Couldn't read profile name? Error %d\n", ret);
		return;
	}

	log_debug(device->ratbag,
		  "Profile %d name: \"%s\"\n",
		  report->profile_num, name);
	free(name);
}

static void
gskill_read_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct gskill_data *drv_data = ratbag_get_drv_data(device);
	struct gskill_profile_data *pdata = profile_to_pdata(profile);
	struct gskill_profile_report *report = &pdata->report;
	uint8_t checksum;
	int rc, retries;

	if (profile->index >= drv_data->profile_count) {
		profile->is_enabled = false;
		return;
	}

	/*
	 * There's a couple of situations where after various commands, the
	 * mouse will get confused and send the wrong profile. Keep trying
	 * until we get what we want.
	 *
	 * As well, getting the wrong profile is sometimes a sign from the
	 * mouse we're doing something wrong.
	 */
	for (retries = 0; retries < 3; retries++) {
		rc = gskill_select_profile(device, profile->index, false);
		if (rc < 0)
			return;

		/* Wait for the device to be ready */
		msleep(100);

		rc = ratbag_hidraw_raw_request(device, GSKILL_GET_SET_PROFILE,
					       (uint8_t*)report,
					       sizeof(*report),
					       HID_FEATURE_REPORT,
					       HID_REQ_GET_REPORT);
		if (rc < (signed)sizeof(*report)) {
			log_error(device->ratbag,
				  "Error while requesting profile: %d\n", rc);
			return;
		}

		if (report->profile_num == profile->index)
			break;

		log_debug(device->ratbag,
			  "Mouse send wrong profile, retrying...\n");
	}

	checksum = gskill_calculate_checksum((uint8_t*)report, sizeof(*report));
	if (checksum != report->checksum) {
		log_error(device->ratbag,
			  "Warning: profile %d invalid checksum (expected %x, got %x)\n",
			  profile->index, report->checksum, checksum);
	}

	gskill_read_resolutions(profile, report);
	gskill_read_profile_name(device, report);
}

static int
gskill_update_resolutions(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct gskill_profile_data *pdata = profile_to_pdata(profile);
	struct gskill_profile_report *report = &pdata->report;
	int i;

	report->dpi_num = 0;
	memset(&report->dpi_levels, 0, sizeof(report->dpi_levels));
	memset(&pdata->res_idx_to_dev_idx, 0,
	       sizeof(pdata->res_idx_to_dev_idx));

	/*
	 * These mice start acting strange if we leave holes in the DPI levels.
	 * So only write and map the enabled DPIs, disabled DPIs will just be
	 * lost on exit
	 */
	for (i = 0; i < GSKILL_NUM_DPI; i++) {
		_cleanup_resolution_ struct ratbag_resolution *res = NULL;
		struct gskill_raw_dpi_level *level =
			&report->dpi_levels[report->dpi_num];

		res = ratbag_profile_get_resolution(profile, i);
		if (!res->dpi_x || !res->dpi_y)
			continue;

		level->x = res->dpi_x / GSKILL_DPI_UNIT;
		level->y = res->dpi_y / GSKILL_DPI_UNIT;
		pdata->res_idx_to_dev_idx[i] = report->dpi_num;

		log_debug(device->ratbag, "Profile %d res %d mapped to %d\n",
			  profile->index, res->index,
			  report->dpi_num);

		report->dpi_num++;
	}

	return 0;
}

#if 0
static int
gskill_reset_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	uint8_t buf[GSKILL_REPORT_SIZE_CMD] = { GSKILL_GENERAL_CMD, 0xc4, 0x0a,
		profile->index };
	int rc;

	rc = gskill_general_cmd(device, buf);
	if (rc < 0)
		return rc;

	log_debug(device->ratbag, "reset profile %d to factory defaults\n",
		  profile->index);

	return 0;
}
#endif

static void
gskill_read_button(struct ratbag_button *button)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct gskill_profile_report *report =
		&profile_to_pdata(profile)->report;
	struct gskill_macro_report *macro_report;
	struct ratbag_button_macro *macro;
	struct gskill_button_cfg *bcfg = &report->btn_cfgs[button->index];
	struct ratbag_button_action *act = &button->action;

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);

	/*
	 * G.Skill mice can't save disabled profiles, so buttons from disabled
	 * profiles shouldn't be set to anything
	 */
	if (!profile->is_enabled) {
		act->type = RATBAG_BUTTON_ACTION_TYPE_NONE;
		return;
	}

	/* Parse any parameters that might accompany the action type */
	switch (bcfg->type) {
	case GSKILL_BUTTON_FUNCTION_WHEEL:
		act->type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;

		if (bcfg->params.wheel.direction == GSKILL_WHEEL_SCROLL_UP)
			act->action.special =
				RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP;
		else
			act->action.special =
				RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN;
		break;
	case GSKILL_BUTTON_FUNCTION_MOUSE:
		act->type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;

		/* FIXME: There is almost no chance this is correct. */
		switch (bcfg->params.mouse.button_mask) {
		case GSKILL_BTN_MASK_LEFT:
			act->action.button = 1;
			break;
		case GSKILL_BTN_MASK_RIGHT:
			act->action.button = 3;
			break;
		case GSKILL_BTN_MASK_MIDDLE:
			act->action.button = 2;
			break;
		case GSKILL_BTN_MASK_SIDE:
			act->action.button = 15;
			break;
		case GSKILL_BTN_MASK_EXTRA:
			act->action.button = 14;
			break;
		}
		break;
	case GSKILL_BUTTON_FUNCTION_KBD:
		act->type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		act->action.key =
			ratbag_hidraw_get_keycode_from_keyboard_usage(
			    device, bcfg->params.kbd.hid_code);
		break;
	case GSKILL_BUTTON_FUNCTION_CONSUMER:
		act->type = RATBAG_BUTTON_ACTION_TYPE_KEY;
		act->action.key =
			ratbag_hidraw_get_keycode_from_consumer_usage(
			    device, bcfg->params.consumer.code);
		break;
	case GSKILL_BUTTON_FUNCTION_DPI_UP:
	case GSKILL_BUTTON_FUNCTION_DPI_DOWN:
	case GSKILL_BUTTON_FUNCTION_CYCLE_DPI_UP:
	case GSKILL_BUTTON_FUNCTION_CYCLE_PROFILE_UP:
	case GSKILL_BUTTON_FUNCTION_DISABLE:
		*act = *gskill_button_function_to_action(bcfg->type);
		break;
	case GSKILL_BUTTON_FUNCTION_MACRO:
		macro_report = gskill_read_button_macro(device,
							button->profile->index,
							button->index);
		if (!macro_report)
			goto err;

		macro = gskill_macro_from_report(device,
						 macro_report);
		if (!macro)
			goto err;

		act->type = RATBAG_BUTTON_ACTION_TYPE_MACRO;
		ratbag_button_copy_macro(button, macro);
		ratbag_button_macro_unref(macro);
		break;
	default:
		break;
	}

	return;

err:
	act->type = RATBAG_BUTTON_ACTION_TYPE_NONE;
}

static int
gskill_update_button(struct ratbag_button *button)
{
	struct ratbag_profile *profile = button->profile;
	struct ratbag_device *device = profile->device;
	struct ratbag_button_action *action = &button->action;
	struct ratbag_button_macro *macro = NULL;
	struct gskill_profile_data *pdata = profile_to_pdata(profile);
	struct gskill_button_cfg *bcfg = &pdata->report.btn_cfgs[button->index];
	uint16_t code = 0;

	macro = container_of(action->macro, macro, macro);
	memset(&bcfg->params, 0, sizeof(bcfg->params));

	switch (action->type) {
	case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
		switch (action->action.special) {
		case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
			bcfg->type = GSKILL_BUTTON_FUNCTION_WHEEL;
			bcfg->params.wheel.direction = GSKILL_WHEEL_SCROLL_UP;
			break;
		case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
			bcfg->type = GSKILL_BUTTON_FUNCTION_WHEEL;
			bcfg->params.wheel.direction = GSKILL_WHEEL_SCROLL_DOWN;
			break;
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:
		case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP:
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP:
		case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN:
			bcfg->type = gskill_button_function_from_action(action);
			break;
		default:
			return -EINVAL;
		}

		break;
	case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
		bcfg->type = GSKILL_BUTTON_FUNCTION_MOUSE;

		/* FIXME: There is almost no chance this is correct. */
		switch (action->action.button) {
		case 1:
			bcfg->params.mouse.button_mask = GSKILL_BTN_MASK_LEFT;
			break;
		case 3:
			bcfg->params.mouse.button_mask = GSKILL_BTN_MASK_RIGHT;
			break;
		case 2:
			bcfg->params.mouse.button_mask = GSKILL_BTN_MASK_MIDDLE;
			break;
		case 15:
			bcfg->params.mouse.button_mask = GSKILL_BTN_MASK_SIDE;
			break;
		case 14:
			bcfg->params.mouse.button_mask = GSKILL_BTN_MASK_EXTRA;
			break;
		default:
			return -EINVAL;
		}

		break;
	case RATBAG_BUTTON_ACTION_TYPE_KEY:
		code = ratbag_hidraw_get_keyboard_usage_from_keycode(
		    device, action->action.key);
		if (code) {
			bcfg->type = GSKILL_BUTTON_FUNCTION_KBD;
			bcfg->params.kbd.hid_code = code;
		} else {
			code = ratbag_hidraw_get_consumer_usage_from_keycode(
			    device, action->action.key);

			bcfg->type = GSKILL_BUTTON_FUNCTION_CONSUMER;
			bcfg->params.consumer.code = code;
		}
		break;
	case RATBAG_BUTTON_ACTION_TYPE_MACRO:
		bcfg->type = GSKILL_BUTTON_FUNCTION_MACRO;
		gskill_write_button_macro(
		    device, gskill_macro_to_report(device, macro,
						   profile->index,
						   button->index));

		break;
	case RATBAG_BUTTON_ACTION_TYPE_NONE:
		bcfg->type = GSKILL_BUTTON_FUNCTION_DISABLE;
		break;
	default:
		break;
	}

	return 0;
}

static int
gskill_update_profile(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag_button *button;
	struct gskill_profile_data *pdata = profile_to_pdata(profile);
	struct gskill_profile_report *report = &pdata->report;
	int rc;

	gskill_update_resolutions(profile);

	list_for_each(button, &profile->buttons, link) {
		if (!button->dirty)
			continue;

		rc = gskill_update_button(button);
		if (rc)
			return rc;
	}

	rc = gskill_write_profile(device, report);
	if (rc)
		return rc;

	return 0;
}

static int
gskill_probe(struct ratbag_device *device)
{
	struct gskill_data *drv_data = NULL;
	struct ratbag_profile *profile;
	unsigned int active_idx;
	int ret;

	ret = ratbag_open_hidraw(device);
	if (ret)
		return ret;

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	ret = gskill_get_firmware_version(device);
	if (ret < 0)
		goto err;

	log_debug(device->ratbag,
		 "Firmware version: %d\n", ret);

	ret = gskill_get_profile_count(device);
	if (ret < 0)
		goto err;
	drv_data->profile_count = ret;

	ratbag_device_init_profiles(device, GSKILL_PROFILE_MAX, GSKILL_NUM_DPI,
				    GSKILL_BUTTON_MAX, GSKILL_NUM_LED);

	ret = gskill_get_active_profile_idx(device);
	if (ret < 0)
		goto err;

	active_idx = ret;
	ratbag_device_for_each_profile(device, profile) {
		struct ratbag_button *button;

		gskill_read_profile(profile);

		ratbag_profile_for_each_button(profile, button)
			gskill_read_button(button);

		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_DISABLE);

		if (profile->index == active_idx)
			profile->is_active = true;
	}

	return 0;

err:
	if (drv_data) {
		ratbag_set_drv_data(device, NULL);
		free(drv_data);
	}

	ratbag_close_hidraw(device);
	return ret;
}

static int
gskill_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct gskill_data *drv_data = ratbag_get_drv_data(device);
	struct gskill_profile_report *report;
	uint8_t profile_count = 0, new_idx, i;
	bool reload = false;
	int rc;

	/*
	 * G.Skill mice only have a concept of how many profiles are enabled,
	 * not which ones are and aren't enabled. So in order to provide the
	 * ability to disable individual profiles we need to only write the
	 * enabled profiles and make sure no holes are left in between profiles
	 */
	for (i = 0; i < GSKILL_PROFILE_MAX; i++) {
		profile = ratbag_device_get_profile(device, i);

		if (!profile->is_enabled)
			continue;

		report = &drv_data->profile_data[profile->index].report;
		new_idx = profile_count++;

		if (new_idx == report->profile_num)
			continue;

		log_debug(device->ratbag,
			  "Profile %d remapped to %d\n",
			  profile->index, new_idx);

		profile->dirty = true;
		report->profile_num = new_idx;
	}

	if (profile_count != drv_data->profile_count) {
		rc = gskill_set_profile_count(device, profile_count);
		if (rc < 0)
			return rc;
		drv_data->profile_count = profile_count;
	}

	list_for_each(profile, &device->profiles, link) {
		if (!profile->is_enabled || !profile->dirty)
			continue;

		log_debug(device->ratbag,
			  "Profile %d changed, rewriting\n", profile->index);
		reload = true;

		rc = gskill_update_profile(profile);
		if (rc)
			return rc;
	}

	if (reload) {
		rc = gskill_reload_profile_data(device);
		if (rc)
			return rc;
	}

	return 0;
}

static void
gskill_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
	free(ratbag_get_drv_data(device));
}

struct ratbag_driver gskill_driver = {
	.name = "G.Skill Ripjaws MX780",
	.id = "gskill",
	.probe = gskill_probe,
	.remove = gskill_remove,
	.commit = gskill_commit,
	.set_active_profile = gskill_set_active_profile,
};
