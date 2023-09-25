#pragma once
#include "marsgaming-protocol.h"
#include "libratbag-private.h"

uint8_t
marsgaming_query_current_profile(struct ratbag_device *device);

struct marsgaming_report_resolutions
marsgaming_query_profile_resolutions(struct ratbag_profile *profile);

struct marsgaming_report_buttons
marsgaming_query_profile_buttons(struct ratbag_profile *profile);

uint8_t
marsgaming_query_profile_polling_interval(struct ratbag_profile *profile);

struct marsgaming_report_led
marsgaming_query_profile_led(struct ratbag_profile *profile);
