// #include "config.h" // don't know what this is
#include <errno.h>
#include <linux/input-event-codes.h>

// #include "driver-steelseries.h"
#include "libratbag-enums.h"
#include "libratbag-private.h"
#include "libratbag.h"
// #include "libratbag-hidraw.h"
// #include "libratbag-data.h"

#define GXT_164_NUM_PROFILES    4
#define GXT_164_NUM_BUTTONS     13
#define GXT_164_NUM_LEDS        1

#define GXT_164_NUM_DPI     4
#define GXT_164_NUM_RATES   5

#define GXT_164_MIN_DPI     100
#define GXT_164_MAX_DPI     5000
#define GXT_164_DPI_STEP    100

/*
 * LED brightness
*/
#define GXT_164_LED_BRIGHTNESS_DIM      1
#define GXT_164_LED_BRIGHTNESS_MEDIUM   2
#define GXT_164_LED_BRIGHTNESS_BRIGHT   3

/*
 * LED modes
 * (modes which have the same ID in ratbag are omitted)
*/
#define GXT_164_LED_BREATHING   0x02
#define GXT_164_LED_COLOR_SHIFT 0x03

/*
 * LED blinking speed
*/
#define GXT_164_LED_SPEED_SLOW      0x05
#define GXT_164_LED_SPEED_MEDIUM    0x03
#define GXT_164_LED_SPEED_FAST      0x01

/*
 * Profile IDs used for commiting
 */
#define GXT_164_PROFILE_0   0x1b01
#define GXT_164_PROFILE_1   0xa602
#define GXT_164_PROFILE_2   0x3104 
#define GXT_164_PROFILE_3   0xbc05

/*
 * Special action IDs
*/
#define GXT_164_ACTION_RESOLUTION_CYCLE_UP  0x20
#define GXT_164_ACTION_RESOLUTION_UP        0x21
#define GXT_164_ACTION_RESOLUTION_DOWN      0x22
#define GXT_164_ACTION_PROFILE_CYCLE_UP     0x26
#define GXT_164_ACTION_PROFILE_UP           0x27
#define GXT_164_ACTION_PROFILE_DOWN         0x28

/**
 * Check version string of a device
 * 
 * @param buf Data recieved from the device
 * @param len Length of buf
 * 
 * @return 0 on success or or negative errno on error
 */
// static int
// gxt_164_check_version(uint8_t *buf, size_t len, struct ratbag_device* dev){
//     const char* version_string = "SST-ZMA-V1.0.3";
    
//     size_t i; 
//     for(i = 0; i < len; i++){
//         if(buf[i] == (uint8_t)'S')
//             break;
//     }

//     for(size_t s=0; i < len && s < 14; s++, i++){
//         // //! Debug
//         // log_debug(dev->ratbag, "Comparing: %c %c", buf[i], version_string[s]);
//         if(buf[i] != version_string[s]){
//             return -EINVAL;
//         }
//     }

//     return 0;
// }

/**
 * Probe the Trust GXT 164 mouse
 * 
 * @param device Ratbag device
 * 
 * @return 0 on success or a negative errno on error
 */
static int
trust_gxt_164_probe(struct ratbag_device *device){
    //? Create variales for profile, led, etc.
    struct ratbag_profile *profile;
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    int rc; // result code of a function call

    log_debug(device->ratbag,
            "### Starting Trust GTX 164 driver probe ###\n");

    //? Open HID device
    rc = ratbag_open_hidraw(device);
	if (rc)
		return rc;

    log_debug(device->ratbag,
            "Opened the hidraw device.\n");

    //? Get firmware version
    // TODO: check if version == SST-ZMA-V1.0.3
    
    // TODO: send GET_REPORT (maybe through ratbag_hidraw_raw_request(...))
    // TODO: send SET_REPORT (maybe through ratbag_hidraw_raw_request(...))

    // uint8_t buf[256];
    //return ratbag_hidraw_raw_request(device, reportnum, buf, len,
    //                              HID_FEATURE_REPORT, HID_REQ_GET_REPORT);
    // rc = ratbag_hidraw_get_feature_report(device, 0x04, buf, ARRAY_LENGTH(buf));
    // if (rc < 0){
    //         log_error(device->ratbag, "Error while probing for device version: %s (%d)\n", strerror(-rc), rc);
    //         ratbag_close_hidraw(device);
    //         return rc;
    // } else {
    //     log_debug(device->ratbag, "Received magic string: %s\n", (buf+48));
    // }
    // if (rc != ARRAY_LENGTH(buf)){
    //     log_error(device->ratbag, "Unexpected amount of written data: %d (instead of %u)\n", rc, (int)ARRAY_LENGTH(buf));
    //     ratbag_close_hidraw(device);
    //     return -EIO;
    // }

    // rc = gxt_164_check_version(buf, ARRAY_LENGTH(buf), device);
    // if(rc){
    //     log_error(device->ratbag, "Device versions did not match\n");
    //     ratbag_close_hidraw(device);
    //     return -ENODEV;
    // }


    //? Some default options 
    unsigned int rates[GXT_164_NUM_RATES] = {125, 250, 333, 500, 1000};

    unsigned int dpis[50];  // dpis list
    for(size_t i=0; i < ARRAY_LENGTH(dpis); i++){
        dpis[i] = GXT_164_MIN_DPI + 100*i;
    }

    struct ratbag_button_action default_actions[GXT_164_NUM_BUTTONS] = {
        // LMB -> Left Click
        {.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 1},
        // RMB -> Right Click
        {.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 2},
        // MMB -> Middle Click
        {.type = RATBAG_BUTTON_ACTION_TYPE_BUTTON, .action.button = 3},
        // DPI_UP
        {.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
         .action.special = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP},
        // DPI_DOWN
        {.type = RATBAG_BUTTON_ACTION_TYPE_SPECIAL,
         .action.key = RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN},
        // Side buttons 1 - 8
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_1},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_2},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_3},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_4},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_5},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_6},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_7},
        {.type = RATBAG_BUTTON_ACTION_TYPE_KEY, .action.key = KEY_8}
    };

    //? Initializing profiles
    ratbag_device_init_profiles(device,
                        GXT_164_NUM_PROFILES,
                        GXT_164_NUM_DPI,
                        GXT_164_NUM_BUTTONS,
                        GXT_164_NUM_LEDS);


    ratbag_device_for_each_profile(device, profile) {
        if(profile->index == 0)
            profile->is_active = true;
        profile->is_enabled = true;

        /* Afaik, Trust GXT 164 doesn't support reading the current settings */
        ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);

        ratbag_profile_set_report_rate_list(profile, 
                                        rates, 
                                        GXT_164_NUM_RATES);

        //? Set report rate to 500 hz
        profile->hz = 500;

        ratbag_profile_for_each_resolution(profile, resolution) {
            //? Set resolution with index 1 active
            if(resolution->index == 1) {
                resolution->is_active = true;
                resolution->is_default = true;
            }

            ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);

            //? Set possible dpi values
            ratbag_resolution_set_dpi_list(resolution,
                                        dpis,
                                        ARRAY_LENGTH(dpis));

            //? Set default resolution value
            resolution->dpi_x = 800 + (resolution->index * 200);
            resolution->dpi_y = resolution->dpi_x;
        }

        ratbag_profile_for_each_button(profile, button){
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL); // TODO
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
            // ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);   // TODO
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

            ratbag_button_set_action(button, &default_actions[button->index]);
        }

        ratbag_profile_for_each_led(profile, led){
            //? set default led options
            led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
            led->mode = RATBAG_LED_CYCLE;
            led->color.red = 255;
            led->color.blue = 255;
            led->color.green = 255;
            led->brightness = 255;

            //? set led capabilities
            ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
            ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
            ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
            ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
        }
    }

    return 0;

// err:
//     free(drv_data);
//     ratbag_set_drv_data(device, NULL);
//     return rc;
}

/**
 * Commit led color, mode, brightness and change speed
 * 
 * @param led Ratbag led
 * 
 * @return 0 on success or a negative errno on error
 */
// static int
// trust_gxt_164_commit_led(struct ratbag_led *led){
// }

/**
 * Get profile id from its index
 * 
 * @param index Profile index
 * 
 * @return Profile id if success or a negative errno on error
 */
static int
gxt_164_get_profile_id_from_index(unsigned int index){
    if(index >= GXT_164_NUM_PROFILES){
        return -EINVAL;
    }

    uint16_t profile_index_map[GXT_164_NUM_PROFILES] = {
        GXT_164_PROFILE_0,
        GXT_164_PROFILE_1,
        GXT_164_PROFILE_2,
        GXT_164_PROFILE_3
    };

    return profile_index_map[index];
}

/**
 * Get report rate index from its value in hz
 * 
 * @param hz Report rate in hz
 * 
 * @return Report rate index on success or a negative errno on error
 */
static int
gxt_164_get_report_rate_from_hz(unsigned int hz){
    switch (hz) {
    case 125:
        return 8;
    case 250:
        return 4;
    case 333:
        return 3;
    case 500:
        return 2;
    case 1000:
        return 1;
    default:
        return -EINVAL;
    }
}

/**
 * Map LED mode from ratbatg to GXT_164
 * 
 * @param mode Ratbag mode
 * 
 * @return GXT_164 LED mode or a negative errno on error
 */
static int
gxt_164_get_led_mode_mapped(unsigned int mode){
    switch(mode){
    case RATBAG_LED_OFF:
    case RATBAG_LED_ON:
        return mode;
    case RATBAG_LED_BREATHING:
        return GXT_164_LED_BREATHING;
    case RATBAG_LED_CYCLE:
        return GXT_164_LED_COLOR_SHIFT;
    default:
        return -EINVAL;
    }
}

/**
 * Get LED speed index from its value in ms
 * 
 * @param ms LED duration in ms
 * 
 * @return LED speed index
 */
static int
gxt_164_get_led_speed_from_ms(unsigned int ms){
    /* ms should be in range 0 - 10000 */
    if(ms <= 3333)
        return GXT_164_LED_SPEED_FAST;
    if(3333 < ms && ms <= 6666)
        return GXT_164_LED_SPEED_MEDIUM;

    return GXT_164_LED_SPEED_SLOW;
}

/**
 * Get LED brightness index from its value
 * 
 * @param ms LED brightness in range 0 - 255
 * 
 * @return LED brightness index
 */
static int
gxt_164_get_led_brightness_from_value(unsigned int brightness){
    /* brightness should be in range 0 - 255 */
    if(brightness <= 85)
        return GXT_164_LED_BRIGHTNESS_DIM;

    if(85 < brightness && brightness <= 170)
        return GXT_164_LED_BRIGHTNESS_MEDIUM;

    return GXT_164_LED_BRIGHTNESS_BRIGHT;
}

/**
 * Get mouse button index from its ratbag value
 * (Usage: )
 * 
 * @param button Ratbag button
 * 
 * @return GXT_164 button index
 */
static int
gxt_164_get_button_from_code(unsigned int button){
    switch(button){
    case 1: // left click
    case 2: // right click
        return button;
    case 3: // middle click
        return 0x04;
    default:
        // LEFT_CLICK if button is unknown
        return 0x01;
    }
}

static int
gxt_164_get_special_mapped(unsigned int special){
    switch(special){
        case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN:
            return GXT_164_ACTION_RESOLUTION_DOWN;
        case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP:
            return GXT_164_ACTION_RESOLUTION_UP;
        case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP:
            return GXT_164_ACTION_RESOLUTION_CYCLE_UP;
        case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP:
            return GXT_164_ACTION_PROFILE_CYCLE_UP;
        case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP:
            return GXT_164_ACTION_PROFILE_UP;
        case RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN:
            return GXT_164_ACTION_PROFILE_DOWN;
        default:
            // LEFT_CLICK if action is unknown
            return 0x01;
    }
}

/**
 * Write changes to the device
 *
 * @param device Ratbag device
 * 
 * @return 0 on success or a negative errno on error
*/
static int
trust_gxt_164_commit(struct ratbag_device *device){
    struct ratbag_profile* profile;
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    int rc;

    //? Commit macros, if there are any (changed/dirty)
    // TODO

    ratbag_device_for_each_profile(device, profile){
        if(!profile->dirty){
            continue;
        }

        led = ratbag_profile_get_led(profile, 0);
        if(!led){
            log_error(device->ratbag, "Error while commiting profile %d: "
                                      "couldn't get LED (maybe it isn't initialized)\n", 
                                      profile->index);
            return rc;
        }

        // bool only_is_active_dirty = profile->is_active_dirty;
        bool only_is_active_dirty = true;
        
        // only_is_active_dirty = only_is_active_dirty && !(profile->angle_snapping_dirty);
        // only_is_active_dirty = only_is_active_dirty && !(profile->debounce_dirty);
        only_is_active_dirty = only_is_active_dirty && !(profile->rate_dirty);
        only_is_active_dirty = only_is_active_dirty && !(led->dirty);
        
        ratbag_profile_for_each_button(profile, button){
            only_is_active_dirty = only_is_active_dirty && !(button->dirty);
        }
        ratbag_profile_for_each_resolution(profile, resolution){
            only_is_active_dirty = only_is_active_dirty && !(resolution->dirty);
        }
        if(only_is_active_dirty){
            continue;
        }

        rc = gxt_164_get_profile_id_from_index(profile->index);
        if(rc < 0){
            log_error(device->ratbag, "Error while commiting profile %d: "
                                      "wrong profile index encountered: %d\n", 
                                      profile->index, profile->index);
            return rc;
        }
        
        uint8_t buf[1024] = {
            // Profile commit command id
            0x05, 0x04, 0xBB, 0xAA,
            // Profile id
            (rc & 0xff00) >> 8,
            (rc & 0x00ff),
            // Profile commit length (395 = 0x18b)
            0x8b, 0x01
        };
        unsigned int buf_index = 8;

        rc = gxt_164_get_report_rate_from_hz(profile->hz);
        if(rc < 0){
            log_error(device->ratbag, "Error while commiting profile %d: "
                                      "wrong report rate encountered: %d\n", 
                                      profile->index, profile->hz);
            return rc;
        }
        // Report rate index
        buf[buf_index++] = rc;

        // Wheel speed (it never changes for some reason?)
        buf[buf_index++] = 0x01;

        

        /*
         * Color settings section
        */

        // LED color
        buf[buf_index++] = led->color.red;
        buf[buf_index++] = led->color.green;
        buf[buf_index++] = led->color.blue;

        // LED mode
        rc = gxt_164_get_led_mode_mapped(led->mode);
        if(rc < 0){
            log_error(device->ratbag, "Error while commiting profile %d: "
                                      "wrong value for LED mode encountered: %d \n", 
                                      profile->index, led->mode);
            return rc;
        }
        buf[buf_index++] = rc;

        buf_index++;    // buf[buf_index++] = 0x00;
        
        // LED brightness
        rc = gxt_164_get_led_brightness_from_value(led->brightness);
        buf[buf_index++] = rc;

        // LED speed
        rc = gxt_164_get_led_speed_from_ms(led->ms);
        buf[buf_index++] = rc;

        // 63 byte padding
        buf_index += 63;

        /*
         * DPI settings section
        */
        // resolution number
        buf[buf_index++] = GXT_164_NUM_DPI;

        // active resolution index
        ratbag_profile_for_each_resolution(profile, resolution){
            if(resolution->is_active){
                if(resolution->index >= GXT_164_NUM_DPI){
                    log_error(device->ratbag, "Error while commiting profile %d: "
                                              "wrong value for resolution index encountered: %d \n", 
                                              profile->index, resolution->index);
                    return -EINVAL;
                }
                buf[buf_index++] = resolution->index;
                break;
            }
        }

        // DPI sensor (haven't seen it change)
        buf[buf_index++] = 0x05;

        ratbag_profile_for_each_resolution(profile, resolution){
            // is resolution enabled (I don't think this mouse supports turning it off)
            buf[buf_index++] = 0x01;

            // uint16_t dpi_x = real_dpi_x / 50
            buf[buf_index++] = resolution->dpi_x / 50;
            buf_index++;    // buf[buf_index++] = 0x00;
            
            // uint16_t dpi_y = real_dpi_y / 50
            buf[buf_index++] = resolution->dpi_y / 50;
            buf_index++;    // buf[buf_index++] = 0x00;
            
            /*
             * Trust GXT 164 mice (or at least some versions of it) have a feature
             * called "DPI Precision". According to manufacturer it is connected to 
             * DPI somehow, but I'm not sure what it does exactly.
             * It was decided to set it to a middle value for this field (from range 0 - 1000) 
            */
            // uint16_t dpi_precision = real_dpi_precision / 50
            buf[buf_index++] = 500 / 50;
            buf_index++;    // buf[buf_index++] = 0x00;

            // padding
            buf_index++;    // buf[buf_index++] = 0x00;
        }

        // 48 byte padding
        buf_index += 48;

        /*
         * Button setting section
        */
        ratbag_profile_for_each_button(profile, button){
            switch(ratbag_button_get_action_type(button)){
            case RATBAG_BUTTON_ACTION_TYPE_NONE:    // disabled
                buf_index += 8;
                break;
            case RATBAG_BUTTON_ACTION_TYPE_BUTTON:{ // mouse button
                // 0x01, button_id, 0x00, 0x00
                buf[buf_index++] = 0x01;
                rc = ratbag_button_get_button(button);
                log_debug(device->ratbag, "MOUSE_BUTTON: %d\n\n", rc);
                rc = gxt_164_get_button_from_code(rc);

                // mouse button index
                buf[buf_index++] = rc;
                buf_index += 2;     // buf[buf_index++] = 0x00;

                // Action activation type and timing
                /*
                 * Trust GXT 164 also supports binding an action which presses a given
                 * key (+ modifier) N times, or repeats it until released or pressed again.
                 *  0 - PLAY_ONCE
                 *  1 - PLAY_N_TIMES
                 *  2 - REPEAT_WHILE_PRESSED
                 *  3 - TOGGLE_AUTO_REPEAT
                 *  4 - TOGGLE_HOLD
                 * 
                 * action_timing
                 * It's also possible to control when the action will be activated:
                 *  0x00 - KEY_PRESS
                 *  0x80 - KEY_RELEASE
                */
                // value = action_activation_type + action_timing;
                buf[buf_index++] = 0x00;    // play once on key press

                // Action activation count
                buf[buf_index++] = 0x01;

                // Action activation delay (uint16_t)
                buf_index += 2; // no delay

                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_KEY: {   // keyboard key
                buf[buf_index++] = 0x02;    // action = key
                buf[buf_index++] = 0x00;    // modifier = none
                
                unsigned int key = ratbag_button_get_key(button);
                rc = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
                if(rc == 0){
                    log_error(device->ratbag, "Error while commiting profile %d: "
                                              "couldn't find HID keyboard usage for the keycode: %d \n", 
                                              profile->index, resolution->index);
                    return -EINVAL;
                }
                // HID keycode
                buf[buf_index++] = rc;
                buf_index++;    // buf[buf_index++] = 0x00;

                // Action activation type and timing
                /*
                 * Trust GXT 164 also supports binding an action which presses a given
                 * key (+ modifier) N times, or repeats it until released or pressed again.
                 *  0 - PLAY_ONCE
                 *  1 - PLAY_N_TIMES
                 *  2 - REPEAT_WHILE_PRESSED
                 *  3 - TOGGLE_AUTO_REPEAT
                 *  4 - TOGGLE_HOLD
                 * 
                 * action_timing
                 * It's also possible to control when the action will be activated:
                 *  0x00 - KEY_PRESS
                 *  0x80 - KEY_RELEASE
                */
                // value = action_activation_type + action_timing;
                buf[buf_index++] = 0x00;    // play once on key press

                // Action activation count
                buf[buf_index++] = 0x01;

                // Action activation delay (uint16_t)
                buf_index += 2;

                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_MACRO: {
                // TODO
                buf_index += 8;  //! Until macro is implemented
                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_SPECIAL: {
                rc = ratbag_button_get_special(button);
                
                if(rc == RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK){
                    // Special case for double left click
                    // 0x01, 0x01, 0x00, 0x00 - left click
                    buf[buf_index++] = 0x01;
                    buf[buf_index++] = 0x01;
                    buf_index += 2;

                    // value = action_activation_type + action_timing;
                    buf[buf_index++] = 0x01;    // play n times on key press

                    // Action activation count
                    buf[buf_index++] = 0x02;

                    // Action activation delay (uint16_t)
                    buf[buf_index++] = 0x32;    // 50ms delay
                    buf_index++;
                } else {
                    // 0x01, special_id, 0x00, 0x00
                    buf[buf_index++] = 0x01;
                    rc = gxt_164_get_special_mapped(rc);

                    // mouse button index
                    buf[buf_index++] = rc;
                    buf_index += 2;

                    // Action activation type and timing
                    /*
                    * Trust GXT 164 also supports binding an action which presses a given
                    * key (+ modifier) N times, or repeats it until released or pressed again.
                    *  0 - PLAY_ONCE
                    *  1 - PLAY_N_TIMES
                    *  2 - REPEAT_WHILE_PRESSED
                    *  3 - TOGGLE_AUTO_REPEAT
                    *  4 - TOGGLE_HOLD
                    * 
                    * action_timing
                    * It's also possible to control when the action will be activated:
                    *  0x00 - KEY_PRESS
                    *  0x80 - KEY_RELEASE
                    */
                    // value = action_activation_type + action_timing;
                    buf[buf_index++] = 0x00;    // play once on key press

                    // Action activation count
                    buf[buf_index++] = 0x01;

                    // Action activation delay (uint16_t)
                    buf_index += 2; // no delay
                }
                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_UNKNOWN:{
                buf_index += 8;
                break;
            }
            }

        }

        /*
         * Packet data end
        */
        buf[buf_index++] = 0x01;
        buf[buf_index++] = 0x26;

        rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, ARRAY_LENGTH(buf));
        if (rc < 0){
            log_error(device->ratbag, "Error while changing active profile: %s (%d)\n", strerror(-rc), rc);
            return rc;
        }
        if (rc != ARRAY_LENGTH(buf)){
            log_error(device->ratbag, "Unexpected amount of written data: %d (instead of %u)\n", rc, (int)ARRAY_LENGTH(buf));
            return -EIO;
        }

        if(profile->is_active) profile->is_active_dirty = true;
    }

    return 0;
}

/**
 * Set active profile by its index
 *
 * @param device Ratbag device
 * @param index New active profile index
 * 
 * @return 0 on success or a negative errno on error
*/
static int
trust_gxt_164_set_active_profile(struct ratbag_device *device, unsigned int index){
    if(index >= GXT_164_NUM_PROFILES)
        return -EINVAL;
    
    int rc;
    uint8_t buf[16] = {
        /*
        * Although some parts of this command *can* change,
        * I haven't seen it yet. So it's a constant until 
        * a problem arises.
        */
        /* Profile change command id */
        0x02, 0x06, 0xBB, 0xAA, 0x04, 0x00, 0x01, 0x00,

        /* 64-bit profile index */
        index, 0, 0, 0, 0, 0, 0, 0
    };

    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, ARRAY_LENGTH(buf));
    if (rc < 0){
        log_error(device->ratbag, "Error while changing active profile: %s (%d)\n", strerror(-rc), rc);
        return rc;
    }
    if (rc != ARRAY_LENGTH(buf)){
        log_error(device->ratbag, "Unexpected amount of written data: %d (instead of %u)\n", rc, (int)ARRAY_LENGTH(buf));
		return -EIO;
    }

    log_debug(device->ratbag, "Changed active profile to profile %d\n", index);

    // struct ratbag_profile* profile = ratbag_device_get_profile(device, index);
    // profile->is_active_dirty = false;

    return 0;
}


/**
 * Remove initialized earlier ratbag device
 * 
 * @param device Ratbag device
 */
static void
trust_gxt_164_remove(struct ratbag_device *device){
    log_debug(device->ratbag,
        "Closing device hidraw.\n");
    
    ratbag_close_hidraw(device);
    // free(ratbag_get_drv_data(device));

    log_debug(device->ratbag,
        "### Trust GTX 164 driver finished ###\n");
}



struct ratbag_driver trust_gxt164_driver = {
	.name = "Trust GXT 164 Sikanda MMO Mouse Driver",
	.id = "trust_gxt_164",
	.probe = trust_gxt_164_probe,
	.remove = trust_gxt_164_remove,
	.commit = trust_gxt_164_commit,
	.set_active_profile = trust_gxt_164_set_active_profile
};
