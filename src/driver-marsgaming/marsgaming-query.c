#include "marsgaming-query.h"

uint8_t
marsgaming_query_current_profile(struct ratbag_device *device)
{
	uint8_t writedata[16] = { 0x02, 0x03, 0x43, 0x00, 0x01, 0x00, 0xfa, 0xfa };
	ratbag_hidraw_set_feature_report(device, writedata[0], writedata, ARRAY_LENGTH(writedata));

	uint8_t readdata[16] = { 0 };
	ratbag_hidraw_get_feature_report(device, 0x02, readdata, ARRAY_LENGTH(readdata));

	return readdata[8];
}

struct marsgaming_report_resolutions
marsgaming_query_profile_resolutions(struct ratbag_profile *profile)
{
	uint8_t writedata[16] = { 0x02, 0x03, 0x4f, (uint8_t)profile->index, 0x2a, 0x00, 0xfa, 0xfa };
	ratbag_hidraw_set_feature_report(profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));

	struct marsgaming_report_resolutions report = { 0 };
	ratbag_hidraw_get_feature_report(profile->device, 0x03, (uint8_t*)&report, sizeof(report));

	// We need to match endianness of the uint16_t values
	for (int i = 0; i < report.count_resolutions; ++i) {
		report.resolutions[i].x_res = get_unaligned_le_u16((uint8_t*)&report.resolutions[i].x_res);
		report.resolutions[i].y_res = get_unaligned_le_u16((uint8_t*)&report.resolutions[i].y_res);
	}

	return report;
}

struct marsgaming_report_buttons
marsgaming_query_profile_buttons(struct ratbag_profile *profile)
{
	uint8_t writedata[16] = { 0x02, 0x03, 0x90, (uint8_t)profile->index, 0x4d, 0x00, 0xfa, 0xfa };
	ratbag_hidraw_set_feature_report(profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));

	struct marsgaming_report_buttons report = { 0 };
	ratbag_hidraw_get_feature_report(profile->device, 0x04, (uint8_t*)&report, sizeof(report));

	return report;
}

uint8_t
marsgaming_query_profile_polling_interval(struct ratbag_profile *profile)
{
	// Lower 3 bits of 3rd byte is the profile number
	uint8_t writedata[16] = { 0x02, 0x03, 0x48 | (uint8_t)profile->index, 0x00, 0x01, 0x00, 0xfa, 0xfa };
	ratbag_hidraw_set_feature_report(profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));

	uint8_t readdata[16] = { 0 };
	ratbag_hidraw_get_feature_report(profile->device, 0x02, readdata, ARRAY_LENGTH(readdata));

	return readdata[8];
}

struct marsgaming_report_led
marsgaming_query_profile_led(struct ratbag_profile *profile)
{
	uint8_t writedata[16] = { 0x02, 0x03, 0xf1, (uint8_t)profile->index, 0x06, 0x00 /*led_id???*/, 0xfa, 0xfa };
	ratbag_hidraw_set_feature_report(profile->device, writedata[0], writedata, ARRAY_LENGTH(writedata));

	struct marsgaming_report_led report = { 0 };
	ratbag_hidraw_get_feature_report(profile->device, 0x04, (uint8_t*)&report, sizeof(report));

	return report;
}
