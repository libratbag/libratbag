#include "marsgaming-command.h"

#include "marsgaming-leds.h"
#include "marsgaming-protocol.h"

void
marsgaming_command_set_current_profile(struct ratbag_device *device, unsigned int profile)
{
	uint8_t writedata[16] = { 0x02, 0x02, 0x43, 0x00, 0x01, 0x00, 0xfa, 0xfa };
	writedata[8] = (uint8_t)profile;
	ratbag_hidraw_set_feature_report(device, writedata[0], writedata, ARRAY_LENGTH(writedata));
}

void
marsgaming_command_profile_set_polling_interval(struct ratbag_profile *profile, uint8_t polling_interval)
{
	uint8_t writedata[16] = { 0x02, 0x02, 0x48 /*lower 3 bits = profile*/, 0x00, 0x01, 0x00, 0xfa, 0xfa };
	writedata[2] |= (uint8_t)profile->index;
	writedata[8] = polling_interval;
	ratbag_hidraw_set_feature_report(profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));
}

void
marsgaming_command_profile_set_led(struct ratbag_led *led)
{
	uint8_t marsgaming_brightness;
	uint8_t marsgaming_breathing;
	switch (led->mode) {
	case RATBAG_LED_OFF:
		marsgaming_brightness = 0;
		marsgaming_breathing = MARSGAMING_LED_BREATHING_OFF;
		break;
	case RATBAG_LED_ON:
		marsgaming_brightness = led->brightness * 3 / 255;
		marsgaming_breathing = MARSGAMING_LED_BREATHING_OFF;
		break;
	case RATBAG_LED_BREATHING:
	case RATBAG_LED_CYCLE: // Not supported by the mouse, let's pretend it's breathing
		marsgaming_brightness = led->brightness * 3 / 255;
		marsgaming_breathing = led->ms / 2000;
		break;
	}
	if (led->profile->is_active) {
		uint8_t writedata[16] = { 0x02, 0x04, 0x00 /*red*/, 0x00 /*green*/, 0x00 /*blue*/, 0x00 /*brightness*/, 0x00 /*breathing*/, 0x01 };
		writedata[2] = 0xff - (uint8_t)led->color.red;
		writedata[3] = 0xff - (uint8_t)led->color.green;
		writedata[4] = 0xff - (uint8_t)led->color.blue;
		writedata[5] = marsgaming_brightness; // Brightness level
		writedata[6] = marsgaming_breathing; // Breathing mode

		ratbag_hidraw_set_feature_report(led->profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));
	}

	{
		uint8_t writedata[16] = { 0x02, 0x02, 0xf1, /*profile*/ 0x00, 0x06, 0x00 /*led_id???*/, 0xfa, 0xfa };
		writedata[3] = (uint8_t)led->profile->index;
		writedata[8] = 0xff - (uint8_t)led->color.red;
		writedata[9] = 0xff - (uint8_t)led->color.green;
		writedata[10] = 0xff - (uint8_t)led->color.blue;
		writedata[11] = marsgaming_brightness; // Brightness level
		writedata[12] = marsgaming_breathing; // Breathing mode

		ratbag_hidraw_set_feature_report(led->profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));
	}
}

void
marsgaming_command_profile_set_resolutions(struct ratbag_profile *profile)
{
	struct marsgaming_report_resolutions read_report = marsgaming_profile_get_drv_data(profile)->resolutions_report;

	// Copy the read report to adapt it for writing
	struct marsgaming_report_resolutions report;
	memcpy(&report, &read_report, sizeof(report));
	report.report_type = MARSGAMING_MM4_REPORT_TYPE_WRITE;
	report.unknown_6 = 0xfa;
	report.unknown_7 = 0xfa;

	for (int i = 0; i < report.count_resolutions; ++i) {
		// Handle endianness
		set_unaligned_le_u16((uint8_t*)&report.resolutions[i].x_res, report.resolutions[i].x_res);
		set_unaligned_le_u16((uint8_t*)&report.resolutions[i].y_res, report.resolutions[i].y_res);
	}
	ratbag_hidraw_set_feature_report(profile->device, report.usb_report_id, (uint8_t*)&report, sizeof(report));
}

void
marsgaming_command_profile_set_buttons(struct ratbag_profile *profile)
{
	struct marsgaming_report_buttons report = marsgaming_profile_get_drv_data(profile)->buttons_report;
	report.report_type = MARSGAMING_MM4_REPORT_TYPE_WRITE;
	report.unknown_6 = 0xfa;
	report.unknown_7 = 0xfa;

	ratbag_hidraw_set_feature_report(profile->device, report.usb_report_id, (uint8_t*)&report, sizeof(report));
}
