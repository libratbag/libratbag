#pragma once
#include "libratbag-private.h"

void
marsgaming_command_set_current_profile(struct ratbag_device *device, unsigned int profile);

void
marsgaming_command_profile_set_polling_interval(struct ratbag_profile *profile, uint8_t polling_interval);

void
marsgaming_command_profile_set_led(struct ratbag_led *led);

void
marsgaming_command_profile_set_resolutions(struct ratbag_profile *profile);

void
marsgaming_command_profile_set_buttons(struct ratbag_profile *profile);
