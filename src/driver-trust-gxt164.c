/*
 * Copyright © 2025 Bohdan Chepurnyi
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

#include <errno.h>
#include <linux/input-event-codes.h>

#include "libratbag-enums.h"
#include "libratbag-private.h"
#include "libratbag.h"

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
#define GXT_164_ACTION_WHEEL_UP             0x11
#define GXT_164_ACTION_WHEEL_DOWN           0x12
#define GXT_164_ACTION_WHEEL_LEFT           0x13
#define GXT_164_ACTION_WHEEL_RIGHT          0x14
#define GXT_164_ACTION_RESOLUTION_CYCLE_UP  0x20
#define GXT_164_ACTION_RESOLUTION_UP        0x21
#define GXT_164_ACTION_RESOLUTION_DOWN      0x22
#define GXT_164_ACTION_DPI_PRESICION        0x23
#define GXT_164_ACTION_PROFILE_CYCLE_UP     0x26
#define GXT_164_ACTION_PROFILE_UP           0x27
#define GXT_164_ACTION_PROFILE_DOWN         0x28

#define GXT_164_MACRO_COUNT             30
#define GXT_164_MACRO_EVENT_COUNT       34
#define GXT_164_MACRO_SIZE              200
#define GXT_164_MACRO_BASE_ADDRESS      0xC00

#define GXT_164_MACRO_KEY_PRESS         0x84
#define GXT_164_MACRO_KEY_RELEASE       0x04

// Key press or release in a macro
struct gxt_164_macro_event {
    // 0x84 - press
    // 0x04 - release
    uint8_t press_type; // is this a button press or a button release

    uint8_t key;        // HID code of the triggered key

    uint8_t padding;    // padding (?)
    
    // 6 if there is a following event, 
    // 0 if it's the last event
    uint8_t next_size;  // size of the next event 

    uint16_t delay;     // next event delay
};
_Static_assert(sizeof(struct gxt_164_macro_event) == sizeof(uint8_t[6]), "Invalid size");

// Contains information about a macro and its raw bytes
struct gxt_164_macro {
    struct gxt_164_macro_event events[GXT_164_MACRO_EVENT_COUNT];  // raw macro bytes
};

// Additional saved data for this driver
struct gxt_164_data {
    uint8_t current_slot_index;     // which macro slot to write to next
};

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
 * Read a given macro slot and store it in the provided macro struct.
 * If the read succeds, function changes out_macro->events,
 * even if the macro data recieved is not valid.
 * If the read doesn't succeed, no changes are made.
 *
 * @param device Ratbag device
 * @param slot_index Index of the macro slot (0 <= slot_index < 30)
 * @param out_macro Pointer to the output macro struct
 * 
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_read_macro(struct ratbag_device *device, uint8_t slot_index, struct gxt_164_macro *out_macro){
    if(!device || slot_index >= 30){
        return -EINVAL;
    }

    int rc = 0;
    uint16_t slot_address = GXT_164_MACRO_BASE_ADDRESS + slot_index*GXT_164_MACRO_SIZE;
    uint8_t req_buf[16] = {
        0x02, 0x05, 0xBB, 0xAA,     // read macro cmd
        slot_address & 0xFF,                    // slot address low
        (slot_address & 0xFF00) >> 8,           // slot address high
        GXT_164_MACRO_SIZE                      // slot size
    };
    
    /* start reading currently saved macro */
    rc = ratbag_hidraw_set_feature_report(device, req_buf[0], req_buf, 16);
    if(rc < 0){
        log_error(device->ratbag, "Error while sending read macro slot %d request! Error: %d\n", slot_index, rc);
        return rc;
    }

    uint8_t res_buf[256] = {0};

    /* read mouse's response (a saved macro in a given slot) */
    rc = ratbag_hidraw_get_feature_report(device, 0x04, res_buf, 256);
    if(rc < 0){
        log_error(device->ratbag, "Error while reading macro slot %d! Error: %d\n", slot_index, rc);
        return rc;
    }

    memcpy(out_macro->events, (res_buf+8), sizeof(out_macro->events));

    return 0;
}

/**
 * Parse a given ratbag macro and create a gxt_164_macro.
 *
 * @param device Ratbag device
 * @param macro Ratbag macro to parse
 * @param out_macro Pointer to the output macro
 *
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_parse_macro(struct ratbag_device* device,
                    struct ratbag_macro* macro, 
                    struct gxt_164_macro* out_macro)
{
    if(!device || !macro || !out_macro){
        return -EINVAL;
    }

    if(macro->events[34].type != RATBAG_MACRO_EVENT_NONE){
        // too many events; make button inactive
        log_error(device->ratbag, "Too many events in a macro (max %d)", 
                                            GXT_164_MACRO_EVENT_COUNT);
        return -EINVAL;
    }

    struct gxt_164_macro temp_macro = {};
    int rc = 0;
    int event_index = 0;
    bool should_end = false;
    for(int i=0; i < GXT_164_MACRO_EVENT_COUNT && !should_end; i++){
        switch(macro->events[i].type){
            case RATBAG_MACRO_EVENT_NONE: {
                should_end = true;
                break;
            }
            case RATBAG_MACRO_EVENT_INVALID: {
                log_error(device->ratbag, "Error while parsing macro: "
                                            "Macro has an INVALID event.\n");
                return -EINVAL;
            }
            case RATBAG_MACRO_EVENT_KEY_PRESSED: {
                unsigned int key = macro->events[i].event.key;
                rc = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
                if(rc == 0){
                    log_error(device->ratbag, "Error while parsing macro: "
                                            "couldn't find HID keyboard usage for the keycode: %d\n", 
                                            key);
                    return -EINVAL;
                }
                
                temp_macro.events[event_index].press_type = GXT_164_MACRO_KEY_PRESS;
                temp_macro.events[event_index].key = rc;
                temp_macro.events[event_index].delay = 50;  // default delay = 50ms
                
                if(event_index != 0){
                    temp_macro.events[event_index-1].next_size = 0x06;
                }
                
                event_index++;
                break;
            }
            case RATBAG_MACRO_EVENT_KEY_RELEASED: {
                unsigned int key = macro->events[i].event.key;
                rc = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
                if(rc == 0){
                    log_error(device->ratbag, "Error while parsing macro: "
                                            "couldn't find HID keyboard usage for the keycode: %d\n", 
                                            key);
                    return -EINVAL;
                }
                
                temp_macro.events[event_index].press_type = GXT_164_MACRO_KEY_RELEASE;
                temp_macro.events[event_index].key = rc;
                temp_macro.events[event_index].delay = 50;  // default delay = 50ms
                
                if(event_index != 0){
                    temp_macro.events[event_index-1].next_size = 0x06;
                }
                
                event_index++;
                break;
            }
            case RATBAG_MACRO_EVENT_WAIT: {
                if(event_index == 0){
                    log_debug(device->ratbag, "RATBAG_MACRO_EVENT_WAIT as a first event. Ignoring.\n");
                    break;
                }

                unsigned int delay = macro->events[i].event.timeout;
                if(delay > 0xFFFF){
                    delay = 0xFFFF;
                }
                temp_macro.events[event_index-1].delay = delay;

                break;
            }
        }
    }

    if(event_index == 0){
        // refuse parsing macros with 0 events
        return -EINVAL;
    }

    temp_macro.events[event_index-1].delay = 0;

    memcpy(out_macro, &temp_macro, sizeof(*out_macro));
    return 0;
}

/**
 * Validates a given macro's  event sequence
 *
 * @param macro Macro to check
 *
 * @return 0 if macro is valid, 1 if macro isn't valid
*/
static int
gxt_164_validate_macro(struct gxt_164_macro *macro){
    struct gxt_164_macro_event* macro_events = macro->events;
    bool is_macro_valid = true; // is this a valid macro
    bool events_ended = false;  // have all non-zero events ended

    if(macro_events[0].press_type == 0){
        if(macro_events[0].key == 0
            && macro_events[0].next_size == 0
            && macro_events[0].delay == 0)
        {
            events_ended = true;
        } else {
            return -EINVAL;
        }
    }
    
    for(int i=0; i < GXT_164_MACRO_EVENT_COUNT; i++){
        if(macro_events[i].padding != 0){
            // non-zero padding (don't know if it matters, tbh)
            is_macro_valid = false;
            break;
        }
        
        if(!events_ended){
            if(macro_events[i].press_type != GXT_164_MACRO_KEY_PRESS 
                && macro_events[i].press_type != GXT_164_MACRO_KEY_RELEASE)
            {
                // bad press_type
                is_macro_valid = false;
                break;
            }
            
            if(macro_events[i].next_size == 0){
                events_ended = true;
            } else if(macro_events[i].next_size != 0x06){
                // bad next_size
                is_macro_valid = false;
                break;
            }
        } else {
            if(macro_events[i].press_type != 0
                || macro_events[i].key != 0
                || macro_events[i].next_size != 0
                || macro_events[i].delay != 0)
            {
                // non-empty events after the macro end
                is_macro_valid = false;
                break;
            }
        }
    }

    return (is_macro_valid)?(0):(-EINVAL);
}

/**
 * Write a given macro to the mouse memory.
 *
 * @param device Mouse ratbag device
 * @param macro Macro to write
 *
 * @return Written macro slot index on success or a negative errno on error
*/
static int
gxt_164_write_macro(struct ratbag_device *device, struct gxt_164_macro *macro){
    if(!device || !macro){
        return -EINVAL;
    }

    struct gxt_164_data* drv_data = ratbag_get_drv_data(device);
    if(!drv_data){
        log_error(device->ratbag, "drv_data was not initialized before commiting.\n");
        return -EINVAL;
    }

    int rc = gxt_164_validate_macro(macro);
    if(rc){
        log_error(device->ratbag, "Trying to upload an invalid macro.\n");
        return -EINVAL;
    }

    int offset = drv_data->current_slot_index*GXT_164_MACRO_SIZE;
    int slot_address = GXT_164_MACRO_BASE_ADDRESS + offset;

    uint8_t buf[256] = {
        0x04, 0x04, 0xBB, 0xAA,  // write macro cmd
        slot_address & 0xFF,                 // slot address low
        (slot_address & 0xFF00) >> 8,        // slot address high
        GXT_164_MACRO_SIZE                   // slot size
    };
    
    memcpy(buf+8, macro->events, GXT_164_MACRO_SIZE);
    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, 256);
    if(rc < 0){
        log_error(device->ratbag, "Error writing macro to the device: %s (%d)", 
                                            strerror(-rc), rc);
        return rc;
    }

    int uploaded_index = drv_data->current_slot_index;
    drv_data->current_slot_index++;
    if(drv_data->current_slot_index >= GXT_164_MACRO_COUNT){
        drv_data->current_slot_index = 0;
    }
    
    return uploaded_index;
}


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
    
    // Check if this is the right hidraw
    rc = ratbag_hidraw_has_report(device, 0x02);
    if(!rc){
        ratbag_close_hidraw(device);
        return -ENODEV;
    }

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
            ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_DISABLE);

            //? Set possible dpi values
            ratbag_resolution_set_dpi_list(resolution,
                                        dpis,
                                        ARRAY_LENGTH(dpis));

            //? Set default resolution value
            resolution->dpi_x = 800 + (resolution->index * 200);
            resolution->dpi_y = resolution->dpi_x;
        }

        ratbag_profile_for_each_button(profile, button){
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_NONE);
            ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);

            ratbag_button_set_action(button, &default_actions[button->index]);
        }

        ratbag_profile_for_each_led(profile, led){
            //? set default led options
            led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
            led->mode = RATBAG_LED_CYCLE;
            led->color.red = 255;   // color = red
            led->color.blue = 0;
            led->color.green = 0;
            led->brightness = 255;  // max brightness
            led->ms = 1200;         // medium speed

            //? set led capabilities
            ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
            ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
            ratbag_led_set_mode_capability(led, RATBAG_LED_CYCLE);
            ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
        }
    }

    struct gxt_164_data* drv_data = NULL;
    drv_data = zalloc(sizeof(*drv_data));
    // Select a random slot for writing
    // Otherwise it will almost always write to the first(or n-th) slot
    drv_data->current_slot_index = rand()%GXT_164_MACRO_COUNT;
    ratbag_set_drv_data(device, drv_data);

    return 0;
}

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
    if(ms <= 1000)
        return GXT_164_LED_SPEED_FAST;
    if(1000 < ms && ms <= 4500)
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
 * 
 * @param button Ratbag button
 * 
 * @return GXT_164 button index on success or negative errno on error
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
        return -EINVAL;
    }
}

/**
 * Get GXT_164 special action from its ratbag value 
 * 
 * @param button Ratbag special action code
 * 
 * @return GXT_164 special action code on success or negative errno on error
 */
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
        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP:
            return GXT_164_ACTION_WHEEL_UP;
        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN:
            return GXT_164_ACTION_WHEEL_DOWN;
        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT:
            return GXT_164_ACTION_WHEEL_LEFT;
        case RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT:
            return GXT_164_ACTION_WHEEL_RIGHT;
        case RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE:
            return GXT_164_ACTION_DPI_PRESICION;
        default:
            return -EINVAL;
    }
}

/**
 * Checks if a given profile has other changes aside from is_active.
 *
 * @param profile Ratbag profile to check
 *
 * @return 1 if there are no changes or only is_active has changed; 0 otherwise
*/
static int
gxt_164_is_only_active_dirty(struct ratbag_profile* profile){
    struct ratbag_led* led;
    struct ratbag_button* button;
    struct ratbag_resolution* resolution;
    bool only_is_active_dirty = true;
    
    // only_is_active_dirty = only_is_active_dirty && !(profile->angle_snapping_dirty);
    // only_is_active_dirty = only_is_active_dirty && !(profile->debounce_dirty);
    only_is_active_dirty = only_is_active_dirty && !(profile->rate_dirty);

    ratbag_profile_for_each_led(profile, led){
        only_is_active_dirty = only_is_active_dirty && !(led->dirty);
    }
    ratbag_profile_for_each_button(profile, button){
        only_is_active_dirty = only_is_active_dirty && !(button->dirty);
    }
    ratbag_profile_for_each_resolution(profile, resolution){
        only_is_active_dirty = only_is_active_dirty && !(resolution->dirty);
    }
    
    return only_is_active_dirty;
}

/**
 * Write ALL the settings of a given profile.
 *
 * @return 0 on success or negative errno on error
*/
static int
gxt_164_write_profile_full(struct ratbag_device* device,
                           struct ratbag_profile* profile)
{
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    int rc;

    led = ratbag_profile_get_led(profile, 0);
    if(!led){
        log_error(device->ratbag, "Error while commiting profile %d: "
                                    "couldn't get LED (maybe it isn't initialized)\n", 
                                    profile->index);
        return -EINVAL;
    }

    rc = gxt_164_get_profile_id_from_index(profile->index);
    if(rc < 0){
        log_error(device->ratbag, "Error while commiting profile %d: "
                                    "wrong profile index encountered: %d\n", 
                                    profile->index, profile->index);
        return rc;
    }
    
    uint8_t buf[1024] = {
        // Profile commit command
        0x05, 0x04, 0xBB, 0xAA,
        // Profile id
        (rc & 0xff00) >> 8,
        (rc & 0x00ff),
        // Profile commit length (395 = 0x018b)
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
        // TODO check if there is a left click present among buttons
        // if not - forcefuly asign it to the first one
        switch(ratbag_button_get_action_type(button)){
            case RATBAG_BUTTON_ACTION_TYPE_NONE:    // disabled
                buf_index += 8;
                break;
            case RATBAG_BUTTON_ACTION_TYPE_BUTTON:{ // mouse button
                rc = button->action.action.button;;
                rc = gxt_164_get_button_from_code(rc);
                if(rc < 0){
                    log_error(device->ratbag, "Wrong mouse button in action: %d. Aborting profile write.\n",
                                                button->action.action.button);
                    return -EINVAL;
                }

                // 0x01, button_id, 0x00, 0x00
                buf[buf_index++] = 0x01;
                buf[buf_index++] = rc;
                buf_index += 2;     // buf[buf_index++] = 0x00;

                // Action activation type and timing
                buf[buf_index++] = 0x00;    // play once on key press

                // Action activation count
                buf[buf_index++] = 0x01;

                // Action activation delay (uint16_t)
                buf_index += 2; // no delay

                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_KEY: {   // keyboard key
                unsigned int key = button->action.action.key;
                rc = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
                if(rc == 0){
                    log_error(device->ratbag, "Error while commiting profile %d: "
                                                "couldn't find HID keyboard usage for the keycode: %d \n", 
                                                profile->index, key);
                    return -EINVAL;
                }

                buf[buf_index++] = 0x02;    // action = key
                buf[buf_index++] = 0x00;    // modifier = none
                buf[buf_index++] = rc;
                buf_index++;    // buf[buf_index++] = 0x00;

                // Action activation type and timing
                buf[buf_index++] = 0x00;    // play once on key press

                // Action activation count
                buf[buf_index++] = 0x01;

                // Action activation delay (uint16_t)
                buf_index += 2;

                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_MACRO: {
                // TODO maybe cache the results
                unsigned int key = 0;
                unsigned int modifiers = 0;
                rc = ratbag_action_keycode_from_macro(&(button->action), &key, &modifiers);
                if(rc == 1){
                    /* upload key+modifier instead of macro */
                    log_debug(device->ratbag, "Macro can be converted to key+modifiers...\n");
                    if(modifiers){
                        // ignore modifier position (left-right)
                        modifiers = (0x0F & modifiers) | ((0xF0 & modifiers) >> 4);
                    }

                    key = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
                    if(key){
                        log_debug(device->ratbag, "Macro converted into key(%d) and modifiers(%d).\n", key, modifiers);
                        
                        buf[buf_index++] = 0x02;
                        buf[buf_index++] = modifiers;
                        buf[buf_index++] = key;
                        buf_index++;

                        buf_index++;                // Play once on key press
                        buf[buf_index++] = 0x01;    // Action activation count
                        buf_index += 2;             // Action activation delay (uint16_t)
                        
                        break;
                    }
                    log_debug(device->ratbag, "Failed to convert: couldn't get key hid code.\n");
                }

                if(button->dirty){
                    struct ratbag_macro_event* events = button->action.macro->events;
                    struct gxt_164_macro temp_macro = {};
                    
                    if(events[34].type != RATBAG_MACRO_EVENT_NONE){
                        // too many events; make button inactive
                        log_error(device->ratbag, "Too many events in a macro (max %d)\n", 
                                                            GXT_164_MACRO_EVENT_COUNT);
                        return -EINVAL;
                    }

                    rc = gxt_164_parse_macro(device, button->action.macro, &temp_macro);
                    if(rc < 0){
                        log_error(device->ratbag, "Macro couldn't be parsed.\n");
                        return -EINVAL;
                    }

                    rc = gxt_164_write_macro(device, &temp_macro);
                    if(rc < 0){
                        log_error(device->ratbag, "Macro couldn't be written. Disabling the button.\n");
                        return -EINVAL;
                    }

                    buf[buf_index++] = 0x04;
                    buf_index++;
                    buf[buf_index++] = rc;  // macro slot index
                    buf[buf_index++] = 0x51;

                    buf[buf_index++] = 0x00;    // play once on key press
                    buf[buf_index++] = 0x01;    // Action activation count
                    buf_index += 2; // Action activation delay (uint16_t)
                }
                
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
                    rc = gxt_164_get_special_mapped(rc);
                    if(rc < 0){
                        return -EINVAL;
                    }
                    
                    // 0x01, special_id, 0x00, 0x00
                    buf[buf_index++] = 0x01;
                    buf[buf_index++] = rc;
                    buf_index += 2;

                    // Action activation type and timing
                    buf[buf_index++] = 0x00;    // play once on key press

                    // Action activation count
                    buf[buf_index++] = 0x01;

                    // Action activation delay (uint16_t)
                    buf_index += 2; // no delay
                }
                break;
            }
            case RATBAG_BUTTON_ACTION_TYPE_UNKNOWN:{
                return -EINVAL;
            }
        }

    }

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

    return 0;
}

/**
 * Write a given led settings to the mouse memory.
 * Writes to the currently active profile.
 *
 * @param device Ratbag device
 * @param led Led settings to write
 * 
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_write_led(struct ratbag_device* device, 
                  struct ratbag_led* led)
{
    if(!device || !led){
        return -EINVAL;
    }

    int mode = gxt_164_get_led_mode_mapped(led->mode);
    if(mode < 0){
        return mode;
    }

    int speed = gxt_164_get_led_speed_from_ms(led->ms);
    int brightness = gxt_164_get_led_brightness_from_value(led->brightness);

    uint8_t buf[64] = {
        0x03, 0x06, 0xBB, 0xAA, // led write cmd (1)
        0x2a, 0x00, 0x0a, 0x00, // led write cmd (2)
        led->color.red,
        led->color.green,
        led->color.blue,
        mode,
        0x00,
        brightness,
        speed
    };

    int rc = 0;
    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, 64);
    if(rc < 0){
        log_error(device->ratbag, "Error while writing LED: %s (%d)", strerror(-rc), rc);
        return rc;
    }

    return 0;
}

/**
 * Write a given dpi settings to the mouse memory.
 * Writes to the currently active profile.
 *
 * @param device Ratbag device
 * @param resolution DPI settings to write
 * 
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_write_dpi(struct ratbag_device* device,
                  struct ratbag_resolution* resolution)
{
    if(!device || !resolution){
        return -EINVAL;
    }

    unsigned int dpi_index = resolution->index;
    if(dpi_index > GXT_164_NUM_DPI){
        return -EINVAL;
    }

    int dpi_x = resolution->dpi_x;
    int dpi_y = resolution->dpi_y;
    if(dpi_x > GXT_164_MAX_DPI || dpi_x < GXT_164_MIN_DPI
      || dpi_y > GXT_164_MAX_DPI || dpi_y < GXT_164_MIN_DPI)
    {
        return -EINVAL;
    }

    uint8_t buf[16] = {
        0x02, 0x06, 0xBB, 0xAA,             // dpi write cmd (1)
        (0x34 + dpi_index), 0x00, 0x08, 0x00, // dpi write cmd (2)
        (resolution->is_disabled == 0),
        dpi_x / 50, 0x00,
        dpi_y / 50, 0x00,
        500 / 50    // dpi_precision (don't know what is does)
    };

    int rc = 0;
    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, 16);
    if(rc < 0){
        log_error(device->ratbag, "Error while writing DPI: %s (%d)", strerror(-rc), rc);
        return rc;
    }

    return 0;
}

/**
 * Write a given polling rate to the mouse memory.
 * Writes to the currently active profile.
 *
 * @param device Ratbag device
 * @param rate_hz Polling rate to write
 * 
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_write_polling_rate(struct ratbag_device* device,
                           unsigned int rate_hz)
{
    if(!device){
        return -EINVAL;
    }

    int rate_index = gxt_164_get_report_rate_from_hz(rate_hz);
    if(rate_index < 0){
        return -EINVAL;
    }

    uint8_t buf[16] = {
        0x02, 0x06, 0xBB, 0xAA,
        0x28, 0x00, 0x01, 0x00,
        rate_index
    };

    int rc = 0;
    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, 16);
    if(rc < 0){
        log_error(device->ratbag, "Error while writing polling rate: %s (%d)", strerror(-rc), rc);
        return rc;
    }

    return 0;
}

/**
 * Set as current a dpi with a given index.
 * Writes to the currently active profile.
 *
 * @param device Ratbag device
 * @param rate_hz Polling rate to write
 * 
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_set_active_dpi(struct ratbag_device* device,
                        unsigned int dpi_index)
{
    if(!device || dpi_index >= GXT_164_NUM_DPI){
        return -EINVAL;
    }

    uint8_t buf[16] = {
        0x02, 0x06, 0xBB, 0xAA,
        0x32, 0x00, 0x01, 0x00,
        dpi_index
    };

    int rc = 0;
    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, 16);
    if(rc < 0){
        log_error(device->ratbag, "Error while changing active DPI: %s (%d)", strerror(-rc), rc);
        return rc;
    }

    return 0;
}

/**
 * Write a new button action.
 * Writes to the currently active profile.
 *
 * @param device Ratbag device
 * @param button Button to write to the device.
 *
 * @return 0 on success or a negative errno on error
*/
static int
gxt_164_write_button(struct ratbag_device* device,
                     struct ratbag_button* button)
{
    if(!device || !button){
        return -EINVAL;
    }
    
    int rc = 0;
    const unsigned int base_index = 0x3E;
    uint8_t buf[16] = {
        0x02, 0x06, 0xBB, 0xAA,
        base_index + button->index,
        0x00, 0x08, 0x00
    };

    // Action activation type and timing
    /*
        * Trust GXT 164 Sikanda supports binding an action which presses a given
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
    // buf[12] = action_activation_type + action_timing;


    switch(button->action.type){
        case RATBAG_BUTTON_ACTION_TYPE_NONE: {
            break;
        }
        case RATBAG_BUTTON_ACTION_TYPE_BUTTON: {
            rc = gxt_164_get_button_from_code(button->action.action.button);
            if(rc < 0){
                log_error(device->ratbag, "Wrong mouse button in action: %d. Aborting button write.\n",
                                            button->action.action.button);
                return -EINVAL;
            }
            
            /* 0x01, button_id, 0x00, 0x00 */
            buf[8] = 0x01;  
            buf[9] = rc;    
            // buf[10] = 0x00;
            // buf[11] = 0x00;

            // buf[12] = 0x00;     // play once on key press
            buf[13] = 0x01;     // action activation count
            // buf[14] = 0x00 // Action activation delay (LOW)
            // buf[15] = 0x00 // Action activation delay (HIGH)

            break;
        }
        case RATBAG_BUTTON_ACTION_TYPE_KEY: {
            unsigned int key = button->action.action.key;
            rc = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
            if(rc == 0){
                log_error(device->ratbag, "Error while writing button: "
                                            "couldn't find HID keyboard usage for the keycode: %d."
                                            "Aborting button write.\n", 
                                            key);
                return -EINVAL;
            }

            /* 0x02, modifier, HID_key, 0x00*/
            buf[8] = 0x02;
            // buf[9] = 0x00;      // modifier = none
            buf[10] = rc;       // HID keycode
            // buf[11] = 0x00;
            
            // buf[12] = 0x00;    // play once on key press
            buf[13] = 0x01;    // action activation count
            // buf[14] = 0x00 // Action activation delay (LOW)
            // buf[15] = 0x00 // Action activation delay (HIGH)

            break;
        }
        case RATBAG_BUTTON_ACTION_TYPE_SPECIAL: {
            rc = button->action.action.special;
                
            if(rc == RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK){
                // Special case for double left click
                /* 0x01, 0x01, 0x00, 0x00 (left click action) */
                buf[8] = 0x01;
                buf[9] = 0x01;
                // buf[10] = 0x00;
                // buf[11] = 0x00;

                buf[12] = 0x01;     // play n times on key press
                buf[13] = 0x02;     // action activation count

                // Action activation delay (uint16_t)
                buf[14] = 0x32;    // 50ms delay
                buf[15] = 0x00;     
            } else {
                rc = gxt_164_get_special_mapped(rc);
                if(rc < 0){
                    log_error(device->ratbag, "Error while writing button: "
                                            "couldn't find special for: %d."
                                            "Aborting button write.\n", 
                                            button->action.action.special);
                    return -EINVAL;
                }

                /* 0x01, special_id, 0x00, 0x00 */
                buf[8] = 0x01;
                buf[9] = rc;        // special action id
                // buf[10] = 0x00;
                // buf[11] = 0x00;

                // buf[12] = 0x00;    // play once on key press
                buf[13] = 0x01;     // action activation count
                // buf[14] = 0x00    // Action activation delay (LOW)
                // buf[15] = 0x00    // Action activation delay (HIGH)
            }

            break;
        }
        case RATBAG_BUTTON_ACTION_TYPE_MACRO: {
            unsigned int key = 0;
            unsigned int modifiers = 0;
            rc = ratbag_action_keycode_from_macro(&(button->action), &key, &modifiers);
            if(rc == 1){
                /* upload key+modifier instead of macro */
                log_debug(device->ratbag, "Macro can be converted to key+modifiers...\n");
                if(modifiers){
                    // ignore modifier position (left-right)
                    modifiers = (0x0F & modifiers) | ((0xF0 & modifiers) >> 4);
                }

                key = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key);
                if(key){
                    log_debug(device->ratbag, "Macro converted into key(%d) and modifiers(%d).\n", key, modifiers);
                    
                    buf[8] = 0x02;
                    buf[9] = modifiers;
                    buf[10] = key;
                    // buf[11] = 0x00;

                    // buf[11] = 0x00;     // play once on key press
                    buf[12] = 0x01;     // Action activation count
                    // buf[14] = 0x00    // Action activation delay (LOW)
                    // buf[15] = 0x00    // Action activation delay (HIGH)
                    
                    break;
                }
                log_debug(device->ratbag, "Failed to convert: couldn't get the key hid code.\n");
            }

            /* Macro is not just a key+modifiers */
            struct gxt_164_macro temp_macro = {};
            
            rc = gxt_164_parse_macro(device, button->action.macro, &temp_macro);
            if(rc < 0){
                log_error(device->ratbag, "Macro couldn't be parsed. Aborting button write.\n");
                return -EINVAL;
            }

            rc = gxt_164_write_macro(device, &temp_macro);
            if(rc < 0){
                log_error(device->ratbag, "Macro couldn't be written. Aborting button write.\n");
                return -EINVAL;
            }

            buf[8] = 0x04;
            // buf[9] = 0x00;
            buf[10] = rc;  // macro slot index
            buf[11] = 0x51;

            // buf[12] = 0x00;    // play once on key press
            buf[13] = 0x01;    // Action activation count
            // buf[14] = 0x00    // Action activation delay (LOW)
            // buf[15] = 0x00    // Action activation delay (HIGH)
            
        
            break;
        }
        case RATBAG_BUTTON_ACTION_TYPE_UNKNOWN: {
            return -EINVAL;    
        }
    }

    rc = ratbag_hidraw_set_feature_report(device, buf[0], buf, 16);
    if(rc < 0){
        log_error(device->ratbag, "Error while changing writing button: %s (%d)", strerror(-rc), rc);
        return rc;
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
        0x02, 0x06, 0xBB, 0xAA, 
        0x04, 0x00, 0x01, 0x00,
        
        // Profile index
        index
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
 * Write CHANGED settings of a given profile.
 *
 * @return 0 on success or negative errno on error
*/
static int
gxt_164_write_profile_changes(struct ratbag_device* device,
                              struct ratbag_profile* profile)
{
    struct ratbag_resolution *resolution;
    struct ratbag_button *button;
    struct ratbag_led *led;
    int rc = 0;

    ratbag_profile_for_each_led(profile, led){
        if(!led->dirty){
            continue;
        }
        
        rc = gxt_164_write_led(device, led);
        if(rc < 0){
            log_error(device->ratbag, "Couldn't write LED.\n");
        }
    }

    ratbag_profile_for_each_resolution(profile, resolution){
        if(!resolution->dirty){
            continue;
        }
        
        if(resolution->is_active){
            rc = gxt_164_set_active_dpi(device, resolution->index);
            if(rc < 0){
                log_error(device->ratbag, "Couldn't change active dpi to %d.\n", resolution->index);
            }
        }

        rc = gxt_164_write_dpi(device, resolution);
        if(rc < 0){
            log_error(device->ratbag, "Couldn't write dpi %d.\n", resolution->index);
        }
    }

    if(profile->rate_dirty){
        rc = gxt_164_write_polling_rate(device, profile->hz);
        if(rc < 0){
            log_error(device->ratbag, "Couldn't write polling rate.\n");
        }
    }

    // TODO: make sure there is always LEFT_CLICK among the button actions (?)

    ratbag_profile_for_each_button(profile, button){
        if(!button->dirty){
            continue;
        }
        
        rc = gxt_164_write_button(device, button);
        if(rc < 0){
            log_error(device->ratbag, "Couldn't write button %d.\n", button->index);
        }
    }

    return 0;
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
    struct ratbag_profile* active_profile = NULL;
    int rc = 0;

    ratbag_device_for_each_profile(device, profile){
        if(!profile->dirty)
            continue;
        
        if(gxt_164_is_only_active_dirty(profile)){
            continue;
        }

        if(profile->is_active){
            active_profile = profile;
            continue;
        }

        /* Profile must be active in order to write changes */
        rc = trust_gxt_164_set_active_profile(device, profile->index);
        if(rc < 0){
            log_error(device->ratbag, "Profile %d couldn't be written.\n", profile->index);
            continue;
        }

        rc = gxt_164_write_profile_changes(device, profile);
        if(rc < 0){
            log_error(device->ratbag, "Profile %d couldn't be written.\n", profile->index);
            continue;
        }
    }

    if(!active_profile){
        // only is_active has changed, let libratbag handle that
        return 0;
    }
    
    rc = trust_gxt_164_set_active_profile(device, active_profile->index);
    if(rc < 0){
        log_error(device->ratbag, "Active profile %d couldn't be written.\n", active_profile->index);
        return rc;
    }

    rc = gxt_164_write_profile_changes(device, active_profile);
    if(rc < 0){
        log_error(device->ratbag, "Active profile %d couldn't be written.\n", active_profile->index);
        return rc;
    }

    active_profile->is_active_dirty = false;

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
    free(ratbag_get_drv_data(device));

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
