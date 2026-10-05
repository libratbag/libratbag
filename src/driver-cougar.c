/*
 * COUGAR Dualblader driver
 *
 * Independent implementation for interoperability with the
 * COUGAR Dualblader gaming mouse.
 *
 * No proprietary COUGAR source code is included.
 */

#include "config.h"

#include "libratbag-enums.h"
#include "libratbag-hidraw.h"
#include "libratbag-private.h"

#include <errno.h>
#include <string.h>

#define COUGAR_DUALBLADER_NUM_PROFILES 1
#define COUGAR_DUALBLADER_NUM_BUTTONS 14

#define COUGAR_REPORT_PROFILE 0x05
#define COUGAR_REPORT_COMMAND 0x0c

#define COUGAR_PROFILE_REPORT_SIZE 2053
#define COUGAR_PROFILE_PAYLOAD_SIZE 2049
#define COUGAR_PROFILE_HEADER_SIZE 4
#define COUGAR_BUTTON_RECORD_SIZE 5

static const unsigned int cougar_button_offsets[COUGAR_DUALBLADER_NUM_BUTTONS] = {
        0x41, 0x46, 0x4b, 0x50, 0x55, 0x5a, 0x5f,
        0x64, 0x69, 0x6e, 0x73, 0x78, 0x7d, 0x82,
};

static const unsigned int cougar_report_rates[] = {
        125,
        250,
        500,
        1000,
        2000,
};

static int
cougar_decode_report_rate(uint8_t value, unsigned int *rate)
{
        switch (value) {
        case 0x00:
                *rate = 2000;
                return 0;
        case 0x01:
                *rate = 1000;
                return 0;
        case 0x02:
                *rate = 500;
                return 0;
        case 0x04:
                *rate = 250;
                return 0;
        case 0x08:
                *rate = 125;
                return 0;
        default:
                return -EINVAL;
        }
}

static int
cougar_encode_report_rate(unsigned int rate, uint8_t *value)
{
        switch (rate) {
        case 2000:
                *value = 0x00;
                return 0;
        case 1000:
                *value = 0x01;
                return 0;
        case 500:
                *value = 0x02;
                return 0;
        case 250:
                *value = 0x04;
                return 0;
        case 125:
                *value = 0x08;
                return 0;
        default:
                return -EINVAL;
        }
}

static int
cougar_test_hidraw(struct ratbag_device *device)
{
        return ratbag_hidraw_has_report(device, COUGAR_REPORT_PROFILE) &&
               ratbag_hidraw_has_report(device, COUGAR_REPORT_COMMAND);
}

static int
cougar_read_profile(struct ratbag_device *device,
                    unsigned int profile_index,
                    uint8_t *profile,
                    size_t profile_size)
{
        uint8_t request[] = {
                COUGAR_REPORT_COMMAND,
                0xc4,
                0x0c,
                0x00,
                0x00,
        };
        uint8_t raw[COUGAR_PROFILE_REPORT_SIZE] = {0};
        int rc;

        if (profile_index > 0xff)
                return -EINVAL;

        request[3] = profile_index;

        rc = ratbag_hidraw_set_feature_report(device,
                                              COUGAR_REPORT_COMMAND,
                                              request,
                                              sizeof(request));
        if (rc < 0)
                return rc;

        if (rc != sizeof(request))
                return -EIO;

        msleep(250);

        raw[0] = COUGAR_REPORT_PROFILE;

        rc = ratbag_hidraw_get_feature_report(device,
                                              COUGAR_REPORT_PROFILE,
                                              raw,
                                              sizeof(raw));
        if (rc < 0)
                return rc;

        if (rc < 2052)
                return -EIO;

        if (raw[0] != COUGAR_REPORT_PROFILE ||
            raw[1] != 0xb0) {
                log_error(device->ratbag,
                          "COUGAR: invalid profile header: "
                          "%02x %02x %02x %02x\n",
                          raw[0], raw[1], raw[2], raw[3]);
                return -EIO;
        }

        if (profile_size > sizeof(raw) - COUGAR_PROFILE_HEADER_SIZE)
                return -EINVAL;

        memcpy(profile,
               &raw[COUGAR_PROFILE_HEADER_SIZE],
               profile_size);

        return 0;
}

static int
cougar_write_profile(struct ratbag_device *device,
                     unsigned int profile_index,
                     const uint8_t *payload)
{
        uint8_t report[COUGAR_PROFILE_REPORT_SIZE] = {0};
        uint8_t apply[COUGAR_PROFILE_REPORT_SIZE] = {0};
        unsigned int sum = 0;
        unsigned int i;
        int rc;

        if (profile_index > 0xff)
                return -EINVAL;

        report[0] = COUGAR_REPORT_PROFILE;
        report[1] = 0x00;
        report[2] = profile_index;

        memcpy(&report[4],
               payload,
               COUGAR_PROFILE_PAYLOAD_SIZE);

        for (i = 0; i < COUGAR_PROFILE_PAYLOAD_SIZE; i++)
                sum += payload[i];

        report[3] = (uint8_t)(-sum);

        log_debug(device->ratbag,
                  "COUGAR: writing profile %u, checksum 0x%02x\n",
                  profile_index,
                  report[3]);

        rc = ratbag_hidraw_set_feature_report(device,
                                              COUGAR_REPORT_PROFILE,
                                              report,
                                              sizeof(report));
        if (rc < 0)
                return rc;

        if (rc != sizeof(report)) {
                log_error(device->ratbag,
                          "COUGAR: short profile write: %d\n",
                          rc);
                return -EIO;
        }

        msleep(350);

        /*
         * Apply/commit command used by the device after a profile write.
         */
        apply[0] = COUGAR_REPORT_COMMAND;
        apply[1] = 0xc4;
        apply[2] = 0x00;
        apply[3] = 0x00;
        apply[4] = 0x00;
        apply[5] = 0x00;
        apply[6] = 0x00;

        rc = ratbag_hidraw_set_feature_report(device,
                                              COUGAR_REPORT_COMMAND,
                                              apply,
                                              sizeof(apply));
        if (rc < 0)
                return rc;

        if (rc != sizeof(apply)) {
                log_error(device->ratbag,
                          "COUGAR: short apply write: %d\n",
                          rc);
                return -EIO;
        }

        msleep(400);

        return 0;
}

static void
cougar_enable_button_caps(struct ratbag_button *button)
{
        ratbag_button_enable_action_type(
                button, RATBAG_BUTTON_ACTION_TYPE_NONE);

        ratbag_button_enable_action_type(
                button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);

        ratbag_button_enable_action_type(
                button, RATBAG_BUTTON_ACTION_TYPE_KEY);

        ratbag_button_enable_action_type(
                button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
}

static void
cougar_set_unknown_action(struct ratbag_device *device,
                          struct ratbag_button *button,
                          const uint8_t *record)
{
        button->action.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
        button->action.action.special =
                RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN;

        log_debug(device->ratbag,
                  "COUGAR: button %u has unknown action "
                  "%02x %02x %02x %02x %02x\n",
                  button->index,
                  record[0], record[1], record[2],
                  record[3], record[4]);
}

static void
cougar_decode_button(struct ratbag_device *device,
                     struct ratbag_button *button,
                     const uint8_t *record)
{
        if (record[0] == 0xff &&
            record[1] == 0x00 &&
            record[2] == 0x00 &&
            record[3] == 0x00 &&
            record[4] == 0x00) {
                button->action.type = RATBAG_BUTTON_ACTION_TYPE_NONE;
                return;
        }

        if (record[0] == 0x01 &&
            record[2] == 0x00 &&
            record[3] == 0x00 &&
            record[4] == 0x00) {
                unsigned int mouse_button = 0;

                switch (record[1]) {
                case 0x01:
                        mouse_button = 1;
                        break;
                case 0x02:
                        mouse_button = 2;
                        break;
                case 0x04:
                        mouse_button = 3;
                        break;
                case 0x08:
                        mouse_button = 4;
                        break;
                case 0x10:
                        mouse_button = 5;
                        break;
                default:
                        break;
                }

                if (mouse_button) {
                        button->action.type =
                                RATBAG_BUTTON_ACTION_TYPE_BUTTON;
                        button->action.action.button = mouse_button;
                        return;
                }
        }

        /*
         * Authoritative Dualblader mapping:
         *
         * 00 00 00 00 00 = Scroll Up
         * 00 01 00 00 00 = Scroll Down
         */
        if (record[0] == 0x00 &&
            record[1] == 0x00 &&
            record[2] == 0x00 &&
            record[3] == 0x00 &&
            record[4] == 0x00) {
                button->action.type =
                        RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
                button->action.action.special =
                        RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP;
                return;
        }

        if (record[0] == 0x00 &&
            record[1] == 0x01 &&
            record[2] == 0x00 &&
            record[3] == 0x00 &&
            record[4] == 0x00) {
                button->action.type =
                        RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
                button->action.action.special =
                        RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN;
                return;
        }

        if (record[0] == 0x0b &&
            record[1] == 0x00 &&
            record[2] == 0x00 &&
            record[3] == 0x00 &&
            record[4] == 0x00) {
                button->action.type =
                        RATBAG_BUTTON_ACTION_TYPE_SPECIAL;
                button->action.action.special =
                        RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP;
                return;
        }

        if (record[0] == 0x02 &&
            record[1] == 0x00 &&
            record[3] == 0x00 &&
            record[4] == 0x00) {
                unsigned int key;

                key = ratbag_hidraw_get_keycode_from_keyboard_usage(
                        device, record[2]);

                if (key != 0) {
                        button->action.type =
                                RATBAG_BUTTON_ACTION_TYPE_KEY;
                        button->action.action.key = key;
                        return;
                }
        }

        cougar_set_unknown_action(device, button, record);
}

static int
cougar_encode_button(struct ratbag_device *device,
                     struct ratbag_button *button,
                     uint8_t *record)
{
        const struct ratbag_button_action *action = &button->action;
        uint8_t usage;

        memset(record, 0, COUGAR_BUTTON_RECORD_SIZE);

        switch (action->type) {
        case RATBAG_BUTTON_ACTION_TYPE_NONE:
                record[0] = 0xff;
                return 0;

        case RATBAG_BUTTON_ACTION_TYPE_BUTTON:
                record[0] = 0x01;

                switch (action->action.button) {
                case 1:
                        record[1] = 0x01;
                        return 0;
                case 2:
                        record[1] = 0x02;
                        return 0;
                case 3:
                        record[1] = 0x04;
                        return 0;
                case 4:
                        record[1] = 0x08;
                        return 0;
                case 5:
                        record[1] = 0x10;
                        return 0;
                default:
                        return -EINVAL;
                }

        case RATBAG_BUTTON_ACTION_TYPE_KEY:
                usage =
                        ratbag_hidraw_get_keyboard_usage_from_keycode(
                                device,
                                action->action.key);

                if (usage == 0)
                        return -EINVAL;

                record[0] = 0x02;
                record[1] = 0x00;
                record[2] = usage;
                record[3] = 0x00;
                record[4] = 0x00;
                return 0;

        case RATBAG_BUTTON_ACTION_TYPE_SPECIAL:
                switch (action->action.special) {
                case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
                        record[0] = 0x00;
                        record[1] = 0x00;
                        return 0;

                case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
                        record[0] = 0x00;
                        record[1] = 0x01;
                        return 0;

                case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
                        record[0] = 0x0b;
                        return 0;

                default:
                        return -EINVAL;
                }

        default:
                return -EINVAL;
        }
}


static int
cougar_commit(struct ratbag_device *device)
{
        struct ratbag_profile *profile;
        struct ratbag_button *button;
        uint8_t payload[COUGAR_PROFILE_PAYLOAD_SIZE] = {0};
        uint8_t verify[COUGAR_PROFILE_PAYLOAD_SIZE] = {0};
        bool changed = false;
        int rc;

        ratbag_device_for_each_profile(device, profile) {
		changed = false;


                rc = cougar_read_profile(device,
                                         profile->index,
                                         payload,
                                         sizeof(payload));
                if (rc) {
                        log_error(device->ratbag,
                                  "COUGAR: unable to read profile "
                                  "before commit: %d\n",
                                  rc);
                        return rc;
                }

                if (profile->rate_dirty) {
                        uint8_t rate_value;

                        rc = cougar_encode_report_rate(profile->hz,
                                                      &rate_value);
                        if (rc) {
                                log_error(device->ratbag,
                                          "COUGAR: unsupported polling rate %u Hz\n",
                                          profile->hz);
                                return rc;
                        }

                        log_debug(device->ratbag,
                                  "COUGAR: polling rate %u Hz -> 0x%02x\n",
                                  profile->hz,
                                  rate_value);

                        payload[0] = rate_value;
                        changed = true;
                }

                ratbag_profile_for_each_button(profile, button) {
                        uint8_t record[COUGAR_BUTTON_RECORD_SIZE];
                        unsigned int offset;

                        if (!button->dirty)
                                continue;


                        if (button->index >= COUGAR_DUALBLADER_NUM_BUTTONS)

			return -EINVAL;


                        offset = cougar_button_offsets[button->index];


                        rc = cougar_encode_button(device,
                                                  button,
                                                  record);
                        if (rc) {
                                log_error(device->ratbag,
                                          "COUGAR: unsupported action "
                                          "for button %u\n",
                                          button->index);
                                return rc;
                        }

                        log_debug(device->ratbag,
                                  "COUGAR: button %u -> "
                                  "%02x %02x %02x %02x %02x\n",
                                  button->index,
                                  record[0], record[1], record[2],
                                  record[3], record[4]);

                        memcpy(&payload[offset],
                               record,
                               COUGAR_BUTTON_RECORD_SIZE);

                        changed = true;
                }

                if (!changed)
                        continue;

                rc = cougar_write_profile(device,
                                          profile->index,
                                          payload);
                if (rc) {
                        log_error(device->ratbag,
                                  "COUGAR: profile write failed: %d\n",
                                  rc);
                        return rc;
                }

                /*
                 * Read the profile back and verify every dirty button.
                 */
                rc = cougar_read_profile(device,
                                         profile->index,
                                         verify,
                                         sizeof(verify));
                if (rc) {
                        log_error(device->ratbag,
                                  "COUGAR: profile readback failed: %d\n",
                                  rc);
                        return rc;
                }

                if (profile->rate_dirty) {
                        uint8_t expected_rate;

                        rc = cougar_encode_report_rate(profile->hz,
                                                      &expected_rate);
                        if (rc)
                                return rc;

                        if (verify[0] != expected_rate) {
                                log_error(device->ratbag,
                                          "COUGAR: polling-rate readback verification failed: "
                                          "expected 0x%02x, got 0x%02x\n",
                                          expected_rate,
                                          verify[0]);
                                return -EIO;
                        }

                        log_info(device->ratbag,
                                 "COUGAR: polling rate %u Hz verified\n",
                                 profile->hz);
                }

                ratbag_profile_for_each_button(profile, button) {
                        uint8_t expected[COUGAR_BUTTON_RECORD_SIZE];
                        unsigned int offset;

                        if (!button->dirty)
                                continue;

                        rc = cougar_encode_button(device,
                                                  button,
                                                  expected);
                        if (rc)
                                return rc;


                        if (button->index >= COUGAR_DUALBLADER_NUM_BUTTONS)

			return -EINVAL;


                        offset = cougar_button_offsets[button->index];


                        if (memcmp(&verify[offset],
                                   expected,
                                   COUGAR_BUTTON_RECORD_SIZE) != 0) {
                                log_error(device->ratbag,
                                          "COUGAR: readback verification "
                                          "failed for button %u\n",
                                          button->index);
                                return -EIO;
                        }
                }

                log_info(device->ratbag,
                         "COUGAR: profile %u written and verified\n",
                         profile->index);
        }

        return 0;
}

static int
cougar_probe(struct ratbag_device *device)
{
        struct ratbag_profile *profile;
        struct ratbag_button *button;
        unsigned int report_rate;
        uint8_t profile_data[COUGAR_PROFILE_PAYLOAD_SIZE] = {0};
        int rc;

        log_info(device->ratbag,
                 "COUGAR: probing Dualblader configuration interface\n");

        rc = ratbag_find_hidraw(device, cougar_test_hidraw);
        if (rc)
                return rc;

        log_info(device->ratbag,
                 "COUGAR: Dualblader configuration interface found\n");

        rc = cougar_read_profile(device,
                                 0,
                                 profile_data,
                                 sizeof(profile_data));
        if (rc)
                goto error;

        log_info(device->ratbag,
                 "COUGAR: onboard profile read successfully\n");

        rc = cougar_decode_report_rate(profile_data[0], &report_rate);
        if (rc) {
                log_error(device->ratbag,
                          "COUGAR: unknown polling-rate value 0x%02x\n",
                          profile_data[0]);
                goto error;
        }

        log_info(device->ratbag,
                 "COUGAR: polling rate %u Hz (value 0x%02x)\n",
                 report_rate,
                 profile_data[0]);

        rc = ratbag_device_init_profiles(device,
                                         COUGAR_DUALBLADER_NUM_PROFILES,
                                         0,
                                         COUGAR_DUALBLADER_NUM_BUTTONS,
                                         0);
        if (rc)
                goto error;

        ratbag_device_for_each_profile(device, profile) {
                profile->is_active = true;

                ratbag_profile_set_report_rate_list(
                        profile,
                        cougar_report_rates,
                        sizeof(cougar_report_rates) /
                                sizeof(cougar_report_rates[0]));

                profile->hz = report_rate;

                ratbag_profile_for_each_button(profile, button) {
                        const unsigned int offset =
                                cougar_button_offsets[button->index];

                        cougar_enable_button_caps(button);

                        cougar_decode_button(device,
                                             button,
                                             &profile_data[offset]);
                }
        }

        log_info(device->ratbag,
                 "COUGAR: initialized %u buttons from onboard profile\n",
                 COUGAR_DUALBLADER_NUM_BUTTONS);

        return 0;

error:
        ratbag_close_hidraw(device);
        return rc;
}

static void
cougar_remove(struct ratbag_device *device)
{
        ratbag_close_hidraw(device);
}

struct ratbag_driver cougar_driver = {
        .name = "COUGAR",
        .id = "cougar",
        .probe = cougar_probe,
        .remove = cougar_remove,
        .commit = cougar_commit,
};
