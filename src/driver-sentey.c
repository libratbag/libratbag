/*
 * Copyright © 2024 Libratbag Contributors
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
#include "shared-macro.h"

#define SENTAY_REPORT_ID_FEATURE 0x03
#define SENTAY_REPORT_SIZE 8

/* Command structure for Sentey GS-3910 */
struct sentay_report {
    uint8_t report_id;
    uint8_t command;
    uint8_t index;
    uint8_t value;
    uint8_t data[3];
    uint8_t suffix;
} __attribute__((packed));
_Static_assert(sizeof(struct sentay_report) == 8, "Invalid report size");

/* Button mapping commands */
#define SENTAY_CMD_BUTTON_CONFIG  0x85
#define SENTAY_CMD_PROFILE        0x88
#define SENTAY_CMD_DPI            0x87
#define SENTAY_CMD_LED            0x86

/* Button indices (0-based, but button 1 is left click which is not configurable) */
#define SENTAY_BUTTON_2_INDEX  0  /* Right click */
#define SENTAY_BUTTON_3_INDEX  1  /* Wheel click */
#define SENTAY_BUTTON_4_INDEX  2  /* Side button 1 */
#define SENTAY_BUTTON_5_INDEX  3  /* Side button 2 */
#define SENTAY_BUTTON_6_INDEX  4  /* Extra button 1 */
#define SENTAY_BUTTON_7_INDEX  5  /* Extra button 2 */
#define SENTAY_BUTTON_8_INDEX  6  /* Extra button 3 */
#define SENTAY_BUTTON_9_INDEX  7  /* Scroll Up */
#define SENTAY_BUTTON_10_INDEX 8  /* Scroll Down */

/* Button action values */
#define SENTAY_ACTION_LEFTCLICK      0x80
#define SENTAY_ACTION_RIGHTCLICK     0x00
#define SENTAY_ACTION_WHEELCLICK     0x00
#define SENTAY_ACTION_BUTTON4        0x00
#define SENTAY_ACTION_BUTTON5        0x00
#define SENTAY_ACTION_SCROLLUP       0x00
#define SENTAY_ACTION_SCROLLDOWN     0x00

/* Button function codes */
#define SENTAY_FUNC_DEFAULT          0xFF
#define SENTAY_FUNC_RIGHTCLICK       0xFF
#define SENTAY_FUNC_WHEELCLICK       0xFF
#define SENTAY_FUNC_BUTTON4          0xFF
#define SENTAY_FUNC_BUTTON5          0xFF
#define SENTAY_FUNC_SCROLLUP         0xFF
#define SENTAY_FUNC_SCROLLDOWN       0xFF

/* Suffix value observed in captures */
#define SENTAY_SUFFIX                0x12

static int
sentay_write_feature(struct ratbag_device *device, struct sentay_report *report)
{
    int ret;
    uint8_t buf[SENTAY_REPORT_SIZE];

    memcpy(buf, report, sizeof(struct sentay_report));

    ret = ratbag_hidraw_output_report(device, buf, sizeof(buf));
    if (ret < 0)
        return ret;

    return 0;
}

static int
sentay_set_button(struct ratbag_button *button, uint8_t index, uint8_t action, uint8_t func)
{
    struct ratbag_device *device = button->profile->device;
    struct sentay_report report = {
        .report_id = SENTAY_REPORT_ID_FEATURE,
        .command = SENTAY_CMD_BUTTON_CONFIG,
        .index = index,
        .value = action,
        .data = { [0] = func, [1] = 0xFF, [2] = 0x12 },
        .suffix = SENTAY_SUFFIX,
    };

    /* Adjust data based on action type */
    if (action == SENTAY_ACTION_LEFTCLICK) {
        report.data[0] = 0x80;
        report.data[1] = 0x1E;
        report.data[2] = 0xFF;
    } else {
        report.data[0] = 0x00;
        report.data[1] = 0xFF;
        report.data[2] = 0xFF;
    }

    log_debug(device->ratbag, "Sentey: Setting button %d to action 0x%02x\n", index, action);

    return sentay_write_feature(device, &report);
}

static int
sentay_set_profile(struct ratbag_profile *profile)
{
    struct ratbag_device *device = profile->device;
    struct sentay_report report = {
        .report_id = SENTAY_REPORT_ID_FEATURE,
        .command = SENTAY_CMD_PROFILE,
        .index = profile->index,
        .value = 0x00,
        .data = { 0x00, 0x00, 0x00 },
        .suffix = SENTAY_SUFFIX,
    };

    log_debug(device->ratbag, "Sentey: Switching to profile %d\n", profile->index);

    return sentay_write_feature(device, &report);
}

static int
sentay_set_dpi(struct ratbag_dpi *dpi)
{
    struct ratbag_profile *profile = dpi->profile;
    struct ratbag_device *device = profile->device;
    struct sentay_report report = {
        .report_id = SENTAY_REPORT_ID_FEATURE,
        .command = SENTAY_CMD_DPI,
        .index = dpi->index,
        .value = (dpi->dpi_x / 100) & 0xFF,
        .data = { (dpi->dpi_x / 100) >> 8, 0x00, 0x00 },
        .suffix = SENTAY_SUFFIX,
    };

    log_debug(device->ratbag, "Sentey: Setting DPI %d to %d\n", dpi->index, dpi->dpi_x);

    return sentay_write_feature(device, &report);
}

static int
sentay_commit(struct ratbag_device *device)
{
    struct ratbag_profile *profile;
    struct ratbag_button *button;
    struct ratbag_dpi *dpi;
    int rc;

    ratbag_device_for_each_profile(device, profile) {
        /* Set profile */
        rc = sentay_set_profile(profile);
        if (rc)
            return rc;

        /* Commit buttons */
        ratbag_profile_for_each_button(profile, button) {
            struct ratbag_button_action *action = &button->action;
            uint8_t index, act_type, func;

            /* Map button number to index */
            switch (button->button) {
                case 1: continue; /* Left click not configurable */
                case 2: index = SENTAY_BUTTON_2_INDEX; break;
                case 3: index = SENTAY_BUTTON_3_INDEX; break;
                case 4: index = SENTAY_BUTTON_4_INDEX; break;
                case 5: index = SENTAY_BUTTON_5_INDEX; break;
                case 6: index = SENTAY_BUTTON_6_INDEX; break;
                case 7: index = SENTAY_BUTTON_7_INDEX; break;
                case 8: index = SENTAY_BUTTON_8_INDEX; break;
                case 9: index = SENTAY_BUTTON_9_INDEX; break;
                case 10: index = SENTAY_BUTTON_10_INDEX; break;
                default: continue;
            }

            /* Map action type */
            switch (action->type) {
                case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
                    act_type = SENTAY_ACTION_LEFTCLICK;
                    func = action->action.button;
                    break;
                case RATBAG_BUTTON_ACTION_TYPE_KEY:
                    act_type = SENTAY_ACTION_RIGHTCLICK;
                    func = SENTAY_FUNC_DEFAULT;
                    break;
                case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
                    act_type = SENTAY_ACTION_RIGHTCLICK;
                    switch (action->action.special) {
                        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
                            func = SENTAY_FUNC_SCROLLUP;
                            break;
                        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
                            func = SENTAY_FUNC_SCROLLDOWN;
                            break;
                        default:
                            func = SENTAY_FUNC_DEFAULT;
                            break;
                    }
                    break;
                default:
                    act_type = SENTAY_ACTION_LEFTCLICK;
                    func = SENTAY_FUNC_DEFAULT;
                    break;
            }

            rc = sentay_set_button(button, index, act_type, func);
            if (rc)
                return rc;
        }

        /* Commit DPIs */
        ratbag_profile_for_each_dpi(profile, dpi) {
            if (!dpi->active)
                continue;

            rc = sentay_set_dpi(dpi);
            if (rc)
                return rc;
        }
    }

    return 0;
}

static int
sentay_probe(struct ratbag_device *device)
{
    int rc;
    struct ratbag_profile *profile;
    struct ratbag_button *button;
    struct ratbag_dpi *dpi;
    unsigned int dpis[] = { 2000, 4200, 6200, 8200 };

    rc = ratbag_find_hidraw(device, NULL);
    if (rc)
        return rc;

    ratbag_device_init_profiles(device,
                                5,  /* 5 profiles */
                                10, /* 10 buttons */
                                4,  /* 4 DPI levels */
                                0); /* No resolution modes */

    /* Configure profiles */
    ratbag_device_for_each_profile(device, profile) {
        profile->is_active = (profile->index == 0);

        /* Configure DPIs */
        ratbag_profile_for_each_dpi(profile, dpi) {
            if (dpi->index < ARRAY_LENGTH(dpis)) {
                dpi->dpi_x = dpis[dpi->index];
                dpi->dpi_y = dpis[dpi->index];
            }
            dpi->is_active = (dpi->index == 0);
        }

        /* Configure buttons */
        ratbag_profile_for_each_button(profile, button) {
            switch (button->button) {
                case 1:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                    button->action.action.button = 1;
                    break;
                case 2:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                    button->action.action.button = 2;
                    break;
                case 3:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                    button->action.action.button = 3;
                    break;
                case 4:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                    button->action.action.button = 4;
                    break;
                case 5:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                    button->action.action.button = 5;
                    break;
                case 6:
                case 7:
                case 8:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                    button->action.action.button = button->button;
                    break;
                case 9:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
                    button->action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP;
                    break;
                case 10:
                    button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
                    button->action.action.special = RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN;
                    break;
            }
        }
    }

    return 0;
}

static void
sentay_remove(struct ratbag_device *device)
{
}

struct ratbag_driver driver_sentey = {
    .id = "sentey",
    .name = "Sentey GS-3910",
    .probe = sentay_probe,
    .remove = sentay_remove,
    .commit = sentay_commit,
};
