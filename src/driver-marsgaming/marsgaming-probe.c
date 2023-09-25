#include "marsgaming-probe.h"

#include "marsgaming-definitions.h"
#include "marsgaming-query.h"

static void
marsgaming_probe_button_action(struct ratbag_button *button,
			       const struct marsgaming_button_info *button_info)
{
	struct ratbag_button_action action = marsgaming_parse_button_to_action(button, button_info);
	ratbag_button_set_action(button, &action);
}

static void
marsgaming_probe_profile_leds(struct ratbag_profile *profile)
{
	struct marsgaming_profile_drv_data *profile_data = marsgaming_profile_get_drv_data(profile);
	profile_data->led_report = marsgaming_query_profile_led(profile);
	struct ratbag_led *led;
	ratbag_profile_for_each_led(profile, led) {
		ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
		ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		ratbag_led_set_mode_capability(led, RATBAG_LED_BREATHING);
		led->colordepth = RATBAG_LED_COLORDEPTH_RGB_888;
		struct marsgaming_report_led report = profile_data->led_report;
		led->color = marsgaming_led_color_to_ratbag(report.led.color);
		led->brightness = report.led.brightness * (255 / 3);
		if (report.led.brightness == 0) {
			led->mode = RATBAG_LED_OFF;
		} else if (report.led.breathing_speed == 0 || report.led.breathing_speed >= 10) {
			led->mode = RATBAG_LED_ON;
		} else { // Breathing mode
			led->mode = RATBAG_LED_BREATHING;
			led->ms = report.led.breathing_speed * 2000;
		}
	}
}

static void
marsgaming_probe_button(struct ratbag_button *button, struct marsgaming_button_info *button_info)
{
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_MACRO);
	marsgaming_probe_button_action(button, button_info);
}

static void
marsgaming_probe_profile_buttons(struct ratbag_profile *profile)
{
	struct marsgaming_profile_drv_data *profile_data = marsgaming_profile_get_drv_data(profile);
	profile_data->buttons_report = marsgaming_query_profile_buttons(profile);
	struct ratbag_button *button;
	ratbag_profile_for_each_button(profile, button) {
		marsgaming_probe_button(button, &profile_data->buttons_report.buttons[button->index]);
	}
}

static void
marsgaming_probe_profile_resolutions(struct ratbag_profile *profile)
{
	struct marsgaming_profile_drv_data *profile_data = marsgaming_profile_get_drv_data(profile);
	profile_data->resolutions_report = marsgaming_query_profile_resolutions(profile);
	struct ratbag_resolution *resolution;
	ratbag_profile_for_each_resolution(profile, resolution) {
		struct marsgaming_report_resolution_info queried_resolution = profile_data->resolutions_report.resolutions[resolution->index];
		ratbag_resolution_set_dpi_list_from_range(resolution,
							  MARSGAMING_MM4_RES_MIN, MARSGAMING_MM4_RES_MAX);

		resolution->is_active = profile_data->resolutions_report.current_resolution == resolution->index;
		resolution->dpi_x = queried_resolution.x_res * MARSGAMING_MM4_RES_SCALING;
		resolution->dpi_y = queried_resolution.y_res * MARSGAMING_MM4_RES_SCALING;
		ratbag_resolution_set_cap(resolution, RATBAG_RESOLUTION_CAP_SEPARATE_XY_RESOLUTION);
	}
}

static void
marsgaming_probe_profile_report_rate(struct ratbag_profile *profile)
{
	static const unsigned int rates[] = { 125, 250, 500, 1000 };
	ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));
	const uint8_t interval = marsgaming_query_profile_polling_interval(profile);
	profile->hz = 1000 / interval;
}

static void
marsgaming_probe_profiles(struct ratbag_device *device)
{
	uint8_t current_profile = marsgaming_query_current_profile(device);
	struct ratbag_profile *profile;
	ratbag_device_for_each_profile(device, profile) {
		profile->drv_data = zalloc(sizeof(struct marsgaming_profile_drv_data));
		profile->is_active = (profile->index == current_profile);

		marsgaming_probe_profile_report_rate(profile);
		marsgaming_probe_profile_resolutions(profile);
		marsgaming_probe_profile_buttons(profile);
		marsgaming_probe_profile_leds(profile);
	}
}

static void
marsgaming_initialize_device(struct ratbag_device *device)
{
	ratbag_device_init_profiles(device,
				    MARSGAMING_MM4_NUM_PROFILES,
				    MARSGAMING_MM4_NUM_RESOLUTIONS_PER_PROFILE,
				    MARSGAMING_MM4_NUM_BUTTONS,
				    MARSGAMING_MM4_NUM_LED);
}

static int
marsgaming_sanity_check(struct ratbag_device *device)
{
	int rc = ratbag_open_hidraw(device);
	if (rc)
		return rc;

	const uint8_t available_reports[] = { 0x02, 0x03, 0x04 };
	for (size_t report_id = 0; report_id < ARRAY_LENGTH(available_reports); ++report_id) {
		if (!ratbag_hidraw_has_report(device, available_reports[report_id])) {
			ratbag_close_hidraw(device);
			return -ENODEV;
		}
	}

	return 0;
}

int
marsgaming_probe(struct ratbag_device *device)
{
	int rc = marsgaming_sanity_check(device);
	if (rc < 0)
		return rc;

	marsgaming_initialize_device(device);

	marsgaming_probe_profiles(device);

	marsgaming_release_device(device);

	return 0;
}

void
marsgaming_release_device(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}
