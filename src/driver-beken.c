/*
 * Copyright © 2026 Joko Saptono
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

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define BEKEN_REPORT_ID_BATTERY  0x01
#define BEKEN_REPORT_ID_DPI      0x04
#define BEKEN_REPORT_ID_PARAM    0x05
#define BEKEN_REPORT_ID_RATE     0x06
#define BEKEN_REPORT_ID_BUTTON   0x08
#define BEKEN_REPORT_ID_APPLY    0x0c

#define BEKEN_NUM_DPI_SLOTS      6
#define BEKEN_NUM_BUTTONS        6
#define BEKEN_DEFAULT_DPI_INDEX  1  /* Index of default active DPI (1600 DPI) */

/* Default DPI values from Windows software: 800, 1600, 2400, 3200, 8000, 24000 */
static const unsigned int beken_default_dpis[BEKEN_NUM_DPI_SLOTS] = {
	800, 1600, 2400, 3200, 8000, 24000
};

/* Default button actions: Left, Right, Middle, Forward, Back, DPI Cycle */
static const struct ratbag_button_action beken_default_actions[BEKEN_NUM_BUTTONS] = {
	BUTTON_ACTION_BUTTON(1),  /* Left Click */
	BUTTON_ACTION_BUTTON(2),  /* Right Click */
	BUTTON_ACTION_BUTTON(3),  /* Middle Click */
	BUTTON_ACTION_BUTTON(5),  /* Forward (piper 5 is forward) */
	BUTTON_ACTION_BUTTON(4),  /* Back (piper 4 is backward) */
	BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP),
};

/* Beken button byte <-> ratbag action mapping */
struct beken_button_mapping {
	uint8_t raw;
	struct ratbag_button_action action;
};

static const struct beken_button_mapping beken_button_map[] = {
	{ 0x02, BUTTON_ACTION_BUTTON(1) },  /* Left Click */
	{ 0x03, BUTTON_ACTION_BUTTON(2) },  /* Right Click */
	{ 0x04, BUTTON_ACTION_BUTTON(3) },  /* Middle Click */
	{ 0x05, BUTTON_ACTION_BUTTON(4) },  /* Back (piper action 4 is backward) */
	{ 0x06, BUTTON_ACTION_BUTTON(5) },  /* Forward (piper action 5 is forward) */
	{ 0x0d, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
};

/* 1. Struct DPI - HID descriptor: 51 data bytes + 1 report_id = 52 total */
struct beken_dpi_report {
    uint8_t report_id;         /* [0] 0x04 */
    uint8_t command;           /* [1] 0x38 */
    uint8_t profile;           /* [2] 0x01 */
    uint8_t sensitivity_x;     /* [3] 0x00 */
    uint8_t sensitivity_y;     /* [4] 0x01 */
    uint8_t dpi_on_off_flag;   /* [5] Bitmask */
    uint8_t dpi_x_double_flag; /* [6] Bitmask for DPI > 12000 */
    uint8_t dpi_y_double_flag; /* [7] Bitmask for DPI > 12000 */
    uint8_t dpi_low_bytes[8];  /* [8..15] Low bytes of (DPI-50)/50 */
    uint8_t dpi_high_bytes[8]; /* [16..23] High bytes of (DPI-50)/50 */
    uint8_t active_dpi;        /* [24] Active index (1-based) */
    uint8_t color_rgb[24];     /* [25..48] 8 * RGB */
    uint8_t dpi_indication_type; /* [49] 2 */
    uint8_t checksum_hi;       /* [50] High byte of sum */
    uint8_t checksum_lo;       /* [51] Low byte of sum */
} __attribute__((packed));

/* 2. Struct Parameter - HID descriptor: 12 data bytes + 1 report_id = 13 total */
struct beken_parameter_report {
    uint8_t report_id;         /* [0] 0x05 */
    uint8_t command;           /* [1] 0x0f */
    uint8_t profile;           /* [2] 0x01 */
    uint8_t unknown1;          /* [3] 0x00 */
    uint8_t unknown2;          /* [4] 0x03 */
    uint8_t debounce_time;     /* [5] */
    uint8_t unknown4[4];       /* [6..9] */
    uint8_t flags;             /* [10] Bit 0x02=Angle Snapping, 0x04=Ripple Control */
    uint8_t unknown5;          /* [11] */
    uint8_t checksum;          /* [12] buf[4] + buf[5] + buf[10] */
} __attribute__((packed));

struct beken_rate_report {
    uint8_t report_id;         /* [0] 0x06 */
    uint8_t command;           /* [1] 0x09 */
    uint8_t profile;           /* [2] 0x01 */
    uint8_t interval;          /* [3] 0x01=1000Hz, 0x02=500Hz, 0x04=250Hz, 0x08=125Hz */
    uint8_t checksum;          /* [4] ~interval & 0xFF */
    uint8_t unknown[4];        /* [5..8] padding */
} __attribute__((packed));

struct beken_button_report {
    uint8_t report_id;         /* [0] 0x08 */
    uint8_t command;           /* [1] 0x3b */
    uint8_t profile;           /* [2] 0x01 */
    uint8_t buttons[54];       /* [3..56] 18 Buttons * 3 bytes */
    uint8_t checksum_hi;       /* [57] High byte of sum */
    uint8_t checksum_lo;       /* [58] Low byte of sum */
    uint8_t padding[5];        /* [59..63] Padding up to 64 bytes */
} __attribute__((packed));

struct beken_data {
    struct beken_dpi_report dpi_cache;
    struct beken_parameter_report param_cache;
    struct beken_rate_report rate_cache;
    struct beken_button_report button_cache;
    unsigned int active_dpi_index;
    unsigned int current_hz;
};

/* --- CHECKSUM ALGORITHMS --- */

static void beken_calc_btn_checksum(struct beken_button_report *report) {
    uint16_t sum = 0;
    uint8_t *buf = (uint8_t *)report;
    for (int i = 3; i <= 56; i++)
        sum += buf[i];
    report->checksum_hi = (sum & 0xFF00) >> 8;
    report->checksum_lo = sum & 0xFF;
}

static void beken_calc_dpi_checksum(struct beken_dpi_report *report) {
    uint16_t sum = 0;
    uint8_t *buf = (uint8_t *)report;
    /* Sum bytes [3] through [49] */
    for (int i = 3; i <= 49; i++)
        sum += buf[i];
    report->checksum_hi = (sum & 0xFF00) >> 8;
    report->checksum_lo = sum & 0xFF;
}

static void beken_calc_param_checksum(struct beken_parameter_report *report) {
    /* Simple checksum: sum of unknown2, debounce_time, and flags */
    report->checksum = report->unknown2 + report->debounce_time + report->flags;
}

/* --- DATA CONVERSION --- */

static void beken_dpi_to_bytes(unsigned int dpi, uint8_t *low, uint8_t *high) {
    if (dpi == 0) {
        *low = 0;
        *high = 0;
        return;
    }
    uint16_t val = (dpi - 50) / 50;
    *low = val & 0xFF;
    *high = (val >> 8) & 0xFF;
}

static unsigned int beken_bytes_to_dpi(uint8_t low, uint8_t high) {
    uint16_t val = ((uint16_t)high << 8) | low;
    if (val == 0) return 0;
    return (val * 50) + 50;
}

static const struct ratbag_button_action *
beken_raw_to_action(uint8_t raw) {
    const struct beken_button_mapping *m;
    ARRAY_FOR_EACH(beken_button_map, m) {
        if (m->raw == raw)
            return &m->action;
    }
    return NULL;
}

static uint8_t
beken_action_to_raw(const struct ratbag_button_action *action) {
    const struct beken_button_mapping *m;
    ARRAY_FOR_EACH(beken_button_map, m) {
        if (ratbag_button_action_match(&m->action, action))
            return m->raw;
    }
    return 0x00;
}

static unsigned int
beken_interval_to_hz(uint8_t interval) {
    switch (interval) {
    case 0x01: return 1000;
    case 0x02: return 500;
    case 0x04: return 250;
    case 0x08: return 125;
    default:   return 1000;
    }
}

static uint8_t
beken_hz_to_interval(unsigned int hz) {
    if (hz >= 1000) return 0x01;
    if (hz >= 500)  return 0x02;
    if (hz >= 250)  return 0x04;
    return 0x08;
}

/* --- DRIVER LOGIC --- */

static int beken_test_hidraw(struct ratbag_device *device) {
    if (ratbag_hidraw_has_report(device, BEKEN_REPORT_ID_DPI) &&
        ratbag_hidraw_has_report(device, BEKEN_REPORT_ID_RATE) &&
        ratbag_hidraw_has_report(device, BEKEN_REPORT_ID_APPLY)) {
        return 1;
    }
    return 0;
}

static void
beken_init_defaults(struct beken_data *drv_data) {
    /* Initialize DPI cache with known-good defaults */
    memset(&drv_data->dpi_cache, 0, sizeof(drv_data->dpi_cache));
    drv_data->dpi_cache.report_id = BEKEN_REPORT_ID_DPI;
    drv_data->dpi_cache.command = 0x38;
    drv_data->dpi_cache.profile = 0x01;
    drv_data->dpi_cache.sensitivity_x = 0x00;
    drv_data->dpi_cache.sensitivity_y = 0x00; /* Was 1, which inverted scroll wheel */
    drv_data->dpi_cache.dpi_on_off_flag = 0x3F; /* 6 slots */
    drv_data->dpi_cache.dpi_indication_type = 2;
    
    for (int i = 0; i < BEKEN_NUM_DPI_SLOTS; i++) {
        beken_dpi_to_bytes(beken_default_dpis[i], 
                           &drv_data->dpi_cache.dpi_low_bytes[i], 
                           &drv_data->dpi_cache.dpi_high_bytes[i]);
    }
    
    drv_data->dpi_cache.active_dpi = 0x02; /* 2nd slot (1600 DPI) */
    
    /* Set some basic default colors just in case */
    drv_data->dpi_cache.color_rgb[0] = 255;
    drv_data->dpi_cache.color_rgb[4] = 255;
    drv_data->dpi_cache.color_rgb[8] = 255;
    
    beken_calc_dpi_checksum(&drv_data->dpi_cache);

    /* Initialize rate cache */
    drv_data->rate_cache.report_id = BEKEN_REPORT_ID_RATE;
    drv_data->rate_cache.command = 0x09;
    drv_data->rate_cache.profile = 0x01;
    drv_data->rate_cache.interval = 0x01; /* 1000 Hz default */
    drv_data->rate_cache.checksum = 0xFE;

    /* Initialize param cache with known-good defaults (13 bytes) */
    uint8_t default_param[13] = {
        0x05, 0x0f, 0x01, 0x00, 0x03, 0x04,
        0x00, 0x00, 0xff, 0x00, 0x04, 0x00,
        0x9f
    };
    memcpy(&drv_data->param_cache, default_param, sizeof(default_param));

    /* Initialize button cache */
    memset(&drv_data->button_cache, 0, sizeof(drv_data->button_cache));
    drv_data->button_cache.report_id = BEKEN_REPORT_ID_BUTTON;
    drv_data->button_cache.command = 0x3b;
    drv_data->button_cache.profile = 0x01;
    /* IGNIX F5 Hardware Matrix:
       0: Left, 1: Right, 2: Middle, 3: DPI, 4: Back, 5: Forward
       6: Mode Key, 7-15: Off, 16: Scroll Up, 17: Scroll Down */
    drv_data->button_cache.buttons[0 * 3] = 0x02; /* Left */
    drv_data->button_cache.buttons[1 * 3] = 0x03; /* Right */
    drv_data->button_cache.buttons[2 * 3] = 0x04; /* Middle */
    drv_data->button_cache.buttons[3 * 3] = 0x0d; /* DPI Cycle */
    drv_data->button_cache.buttons[4 * 3] = 0x05; /* Back */
    drv_data->button_cache.buttons[5 * 3] = 0x06; /* Forward */
    
    drv_data->button_cache.buttons[6 * 3] = 60; /* Mode Key */
    for (int i = 7; i <= 15; i++) {
        drv_data->button_cache.buttons[i * 3] = 0x01; /* Off */
    }
    drv_data->button_cache.buttons[16 * 3] = 10; /* Scroll Up */
    drv_data->button_cache.buttons[17 * 3] = 9;  /* Scroll Down */
    beken_calc_btn_checksum(&drv_data->button_cache);
    
    drv_data->active_dpi_index = BEKEN_DEFAULT_DPI_INDEX;
    drv_data->current_hz = 1000;
}

static bool
beken_report_is_valid(uint8_t *buf, size_t len) {
    /* Check if the returned data has any non-zero bytes beyond report ID */
    for (size_t i = 1; i < len; i++) {
        if (buf[i] != 0)
            return true;
    }
    return false;
}

static int beken_probe(struct ratbag_device *device) {
    int rc;
    struct ratbag_profile *profile;
    struct ratbag_resolution *res;
    struct ratbag_button *button;

    rc = ratbag_find_hidraw(device, beken_test_hidraw);
    if (rc)
        return -ENODEV;

    struct beken_data *drv_data = zalloc(sizeof(*drv_data));
    if (!drv_data)
        return -ENOMEM;
    ratbag_set_drv_data(device, drv_data);

    /* Set known-good defaults first */
    beken_init_defaults(drv_data);

    /* Try to read battery */
    uint8_t battery_buf[7] = { BEKEN_REPORT_ID_BATTERY };
    rc = ratbag_hidraw_get_feature_report(device, BEKEN_REPORT_ID_BATTERY,
                                          battery_buf, sizeof(battery_buf));
    if (rc >= 7 && battery_buf[6] > 0) {
        log_info(device->ratbag, "Ignix F5 - Battery: %d%%\n", battery_buf[6]);
    }

    /* Initialize profiles: 1 profile, 6 DPI slots, 6 buttons, 0 LEDs */
    ratbag_device_init_profiles(device, 1, BEKEN_NUM_DPI_SLOTS,
                                BEKEN_NUM_BUTTONS, 0);
    profile = ratbag_device_get_profile(device, 0);
    profile->name = strdup("Default Profile");

    /* Set up report rates */
    unsigned int rates[] = {125, 250, 500, 1000};
    ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));

    /* Set up DPI list for each resolution slot */
    ratbag_profile_for_each_resolution(profile, res) {
        ratbag_resolution_set_dpi_list_from_range(res, 50, 24000);
        ratbag_resolution_set_cap(res, RATBAG_RESOLUTION_CAP_DISABLE);
    }

    /* Set up button capabilities */
    ratbag_profile_for_each_button(profile, button) {
        ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
        ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
        ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
        ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
    }

    /* Set up debounce */
    static const unsigned int debounce_times[] = { 4, 6, 8, 10, 12, 14, 16 };
    ratbag_profile_set_debounce_list(profile, debounce_times,
                                     ARRAY_LENGTH(debounce_times));

    /* --- Try reading from device, fall back to defaults --- */

    /* Probe polling rate */
    uint8_t rate_buf[9] = { BEKEN_REPORT_ID_RATE };
    rc = ratbag_hidraw_get_feature_report(device, BEKEN_REPORT_ID_RATE,
                                          rate_buf, sizeof(rate_buf));
    if (rc > 0 && beken_report_is_valid(rate_buf, rc)) {
        memcpy(&drv_data->rate_cache, rate_buf, min((size_t)rc, sizeof(drv_data->rate_cache)));
        drv_data->current_hz = beken_interval_to_hz(drv_data->rate_cache.interval);
        log_debug(device->ratbag, "Beken: read polling rate %d Hz\n", drv_data->current_hz);
    }
    profile->hz = drv_data->current_hz;

    /* Probe DPI */
    uint8_t dpi_buf[52] = { BEKEN_REPORT_ID_DPI };
    rc = ratbag_hidraw_get_feature_report(device, BEKEN_REPORT_ID_DPI,
                                          dpi_buf, sizeof(dpi_buf));
    if (rc > 0 && beken_report_is_valid(dpi_buf, rc)) {
        memcpy(&drv_data->dpi_cache, dpi_buf, min((size_t)rc, sizeof(drv_data->dpi_cache)));
        log_debug(device->ratbag, "Beken: read DPI config from device\n");
    }

    /* Apply DPI values to resolutions */
    ratbag_profile_for_each_resolution(profile, res) {
        unsigned int dpi = 0;
        if (res->index < BEKEN_NUM_DPI_SLOTS) {
            uint8_t low = drv_data->dpi_cache.dpi_low_bytes[res->index];
            uint8_t high = drv_data->dpi_cache.dpi_high_bytes[res->index];
            dpi = beken_bytes_to_dpi(low, high);
        } else {
            dpi = beken_default_dpis[res->index];
        }
        ratbag_resolution_set_resolution(res, dpi, dpi);

        /* Set active and default resolution */
        if (res->index == drv_data->active_dpi_index) {
            res->is_active = true;
            res->is_default = true;
        }
    }

    /* Probe debounce */
    uint8_t param_buf[13] = { BEKEN_REPORT_ID_PARAM };
    rc = ratbag_hidraw_get_feature_report(device, BEKEN_REPORT_ID_PARAM,
                                          param_buf, sizeof(param_buf));
    if (rc > 0 && beken_report_is_valid(param_buf, rc)) {
        memcpy(&drv_data->param_cache, param_buf, min((size_t)rc, sizeof(drv_data->param_cache)));
        profile->debounce = drv_data->param_cache.debounce_time * 2;
        profile->angle_snapping = (drv_data->param_cache.flags & 0x02) ? 1 : 0;
        log_debug(device->ratbag, "Beken: read debounce %d ms\n", profile->debounce);
    } else {
        profile->debounce = drv_data->param_cache.debounce_time * 2; /* from defaults */
        profile->angle_snapping = (drv_data->param_cache.flags & 0x02) ? 1 : 0; /* from defaults */
    }

    /* Probe buttons */
    uint8_t btn_buf[59] = { BEKEN_REPORT_ID_BUTTON };
    rc = ratbag_hidraw_get_feature_report(device, BEKEN_REPORT_ID_BUTTON,
                                          btn_buf, sizeof(btn_buf));
    if (rc > 0 && beken_report_is_valid(btn_buf, rc)) {
        memcpy(&drv_data->button_cache, btn_buf, min((size_t)rc, sizeof(drv_data->button_cache)));
        log_debug(device->ratbag, "Beken: read button config from device\n");
    }

    /* Apply button actions */
    int ratbag_to_hw_map[BEKEN_NUM_BUTTONS] = { 0, 1, 2, 4, 5, 3 };
    ratbag_profile_for_each_button(profile, button) {
        const struct ratbag_button_action *action = NULL;
        if (button->index < BEKEN_NUM_BUTTONS) {
            int hw_index = ratbag_to_hw_map[button->index];
            uint8_t raw = drv_data->button_cache.buttons[hw_index * 3];
            if (raw != 0)
                action = beken_raw_to_action(raw);
        }
        if (action)
            ratbag_button_set_action(button, action);
        else if (button->index < BEKEN_NUM_BUTTONS)
            ratbag_button_set_action(button, &beken_default_actions[button->index]);
    }

    /* Mark profile as active and clean */
    profile->is_active = true;
    profile->dirty = false;
    ratbag_profile_for_each_resolution(profile, res)
        res->dirty = false;
    ratbag_profile_for_each_button(profile, button)
        button->dirty = false;

    log_info(device->ratbag, "Ignix F5 probe complete: %d Hz, DPI[%d]=%d\n",
             drv_data->current_hz, drv_data->active_dpi_index,
             beken_bytes_to_dpi(
                 drv_data->dpi_cache.dpi_low_bytes[drv_data->active_dpi_index],
                 drv_data->dpi_cache.dpi_high_bytes[drv_data->active_dpi_index]
             ));

    return 0;
}

static int
beken_write_report(struct ratbag_device *device, uint8_t *buf, size_t len) {
    int rc;
    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, len);
    if (rc < 0) {
        log_error(device->ratbag,
                  "Beken: set_feature_report(0x%02x) failed: %d (%s)\n",
                  buf[0], rc, strerror(-rc));
        return rc;
    }
    log_debug(device->ratbag,
              "Beken: set_feature_report(0x%02x) sent %d bytes OK\n",
              buf[0], rc);
    msleep(80);
    return 0;
}

static int beken_commit(struct ratbag_device *device) {
    struct ratbag_profile *profile;
    struct beken_data *drv_data = ratbag_get_drv_data(device);
    int rc;

    /* Send WebHID Unlock/Init sequence just in case */
    uint8_t unlock1[7] = { 0x80, 0x01, 0x03, 0x20, 0x50, 0x00, 0x04 };
    uint8_t unlock2[7] = { 0x80, 0x01, 0x03, 0x20, 0x41, 0x02, 0x64 };
    ratbag_hidraw_output_report(device, unlock1, sizeof(unlock1));
    ratbag_hidraw_output_report(device, unlock2, sizeof(unlock2));
    msleep(50);

    ratbag_device_for_each_profile(device, profile) {
        if (!profile->dirty)
            continue;

    /* --- COMMIT DPI --- */
    struct ratbag_resolution *res;
    bool dpi_changed = false;
    ratbag_profile_for_each_resolution(profile, res) {
        if (res->dirty) { dpi_changed = true; break; }
    }
    if (dpi_changed) {
        ratbag_profile_for_each_resolution(profile, res) {
            if (res->index < BEKEN_NUM_DPI_SLOTS) {
                beken_dpi_to_bytes(
                    res->dpi_x, 
                    &drv_data->dpi_cache.dpi_low_bytes[res->index], 
                    &drv_data->dpi_cache.dpi_high_bytes[res->index]
                );
                
                if (res->dpi_x > 12000) {
                    drv_data->dpi_cache.dpi_x_double_flag |= (1 << res->index);
                    drv_data->dpi_cache.dpi_y_double_flag |= (1 << res->index);
                } else {
                    drv_data->dpi_cache.dpi_x_double_flag &= ~(1 << res->index);
                    drv_data->dpi_cache.dpi_y_double_flag &= ~(1 << res->index);
                }
            }
            if (res->is_active) {
                drv_data->active_dpi_index = res->index;
                drv_data->dpi_cache.active_dpi = res->index + 1;
            }
        }
        
        beken_calc_dpi_checksum(&drv_data->dpi_cache);;
        rc = beken_write_report(device, (uint8_t*)&drv_data->dpi_cache, sizeof(drv_data->dpi_cache));
        if (rc < 0) { log_error(device->ratbag, "Beken: DPI commit failed: %d\n", rc); return rc; }
        log_info(device->ratbag, "Beken: DPI config updated\n");
    }

    /* --- COMMIT ANGLE SNAPPING --- */
    if (profile->angle_snapping_dirty) {
        if (profile->angle_snapping) {
            drv_data->param_cache.flags |= 0x02;
        } else {
            drv_data->param_cache.flags &= ~0x02;
        }
        beken_calc_param_checksum(&drv_data->param_cache);
        log_debug(device->ratbag, "Beken: Committing Angle Snapping: %d (flags: %02x)\n", profile->angle_snapping, drv_data->param_cache.flags);
        rc = beken_write_report(device, (uint8_t*)&drv_data->param_cache, sizeof(drv_data->param_cache));
        if (rc < 0) { log_error(device->ratbag, "Beken: Param commit failed: %d\n", rc); return rc; }
    }

    /* --- COMMIT DEBOUNCE --- */
    if (profile->debounce_dirty) {
        drv_data->param_cache.debounce_time = profile->debounce / 2;
        beken_calc_param_checksum(&drv_data->param_cache);
        rc = beken_write_report(device, (uint8_t*)&drv_data->param_cache, sizeof(drv_data->param_cache));
        if (rc < 0) { log_error(device->ratbag, "Beken: Param commit failed: %d\n", rc); return rc; }
    }

    /* --- COMMIT RATE --- */
    if (profile->rate_dirty) {
        drv_data->rate_cache.interval = beken_hz_to_interval(profile->hz);
        drv_data->rate_cache.checksum = ~drv_data->rate_cache.interval & 0xFF;
        rc = beken_write_report(device, (uint8_t*)&drv_data->rate_cache, sizeof(drv_data->rate_cache));
        if (rc < 0) { log_error(device->ratbag, "Beken: RATE commit failed: %d\n", rc); return rc; }
        drv_data->current_hz = profile->hz;
        log_info(device->ratbag, "Beken: polling rate set to %d Hz\n", profile->hz);
    }

    /* --- COMMIT BUTTON --- */
    struct ratbag_button *button;
    bool button_changed = false;
    ratbag_profile_for_each_button(profile, button) {
        if (button->dirty) { button_changed = true; break; }
    }
    if (button_changed) {
        /* Map ratbag button indices to IGNIX F5 hardware indices
           Ratbag: 0=Left, 1=Right, 2=Middle, 3=Back, 4=Forward, 5=DPI
           Hardware: 0=Left, 1=Right, 2=Middle, 3=DPI, 4=Back, 5=Forward */
        int ratbag_to_hw_map[BEKEN_NUM_BUTTONS] = { 0, 1, 2, 4, 5, 3 };
        
        ratbag_profile_for_each_button(profile, button) {
            if (button->index < BEKEN_NUM_BUTTONS) {
                int hw_index = ratbag_to_hw_map[button->index];
                drv_data->button_cache.buttons[hw_index * 3] = beken_action_to_raw(&button->action);
            }
        }
        beken_calc_btn_checksum(&drv_data->button_cache);
        rc = beken_write_report(device, (uint8_t*)&drv_data->button_cache, sizeof(drv_data->button_cache));
        if (rc < 0) { log_error(device->ratbag, "Beken: BUTTON commit failed: %d\n", rc); return rc; }
        log_info(device->ratbag, "Beken: button config updated\n");
    }

    log_info(device->ratbag, "Beken: configuration committed to device\n");
    
    } /* End of ratbag_device_for_each_profile */
    return 0;
}

static void beken_remove(struct ratbag_device *device) {
    struct beken_data *drv_data = ratbag_get_drv_data(device);
    ratbag_set_drv_data(device, NULL);
    free(drv_data);
}

struct ratbag_driver beken_driver = {
    .name = "Beken BK3633",
    .id = "beken",
    .probe = beken_probe,
    .remove = beken_remove,
    .commit = beken_commit,
};