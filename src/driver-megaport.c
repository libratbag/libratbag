/* SPDX-License-Identifier: MIT */
/* Lighting and DPI controls for Megaport USB mice.
 * The OEM 18f8:0f99 protocol packs the DPI index in the high nibble and
 * LED mode in the low nibble of byte 3 of feature report 07 09.
 * Current settings cannot be read and are lost when USB power is removed.
 */

#include "config.h"

#include <errno.h>

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define MEGAPORT_REPORT_ID 0x07
#define MEGAPORT_USAGE_PAGE 0xff01
#define MEGAPORT_LED_ON 0x02
#define MEGAPORT_LED_OFF 0x03

static const unsigned int megaport_dpis[] = { 1200, 1600, 2000, 2400, 3200 };

static int
megaport_test_hidraw(struct ratbag_device *device)
{
	return ratbag_hidraw_has_report(device, MEGAPORT_REPORT_ID) &&
	       ratbag_hidraw_get_usage_page(device, MEGAPORT_REPORT_ID) == MEGAPORT_USAGE_PAGE;
}

static int
megaport_probe(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;
	/* The supported low-speed mouse has a 10 ms interrupt interval. */
	const unsigned int rates[] = { 100 };
	int rc;

	rc = ratbag_find_hidraw(device, megaport_test_hidraw);
	if (rc < 0)
		return rc;

	rc = ratbag_device_init_profiles(device, 1, 1, 0, 1);
	if (rc < 0)
		return rc;

	ratbag_device_for_each_profile(device, profile) {
		profile->is_active = true;
		profile->hz = rates[0];
		ratbag_profile_set_report_rate_list(profile, rates, ARRAY_LENGTH(rates));
		/* GET_FEATURE returns zeroes, so the current settings are unknown. */
		ratbag_profile_set_cap(profile, RATBAG_PROFILE_CAP_WRITE_ONLY);

		ratbag_profile_for_each_resolution(profile, resolution) {
			resolution->dpi_x = resolution->dpi_y = 1600;
			resolution->is_active = true;
			resolution->is_default = true;
			ratbag_resolution_set_dpi_list(resolution, megaport_dpis,
						       ARRAY_LENGTH(megaport_dpis));
		}

		ratbag_profile_for_each_led(profile, led) {
			led->mode = RATBAG_LED_OFF;
			led->colordepth = RATBAG_LED_COLORDEPTH_MONOCHROME;
			led->color.red = led->color.green = led->color.blue = 255;
			led->brightness = 255;
			ratbag_led_set_mode_capability(led, RATBAG_LED_OFF);
			ratbag_led_set_mode_capability(led, RATBAG_LED_ON);
		}
	}

	return 0;
}

static void
megaport_remove(struct ratbag_device *device)
{
	ratbag_close_hidraw(device);
}

static int
megaport_commit(struct ratbag_device *device)
{
	struct ratbag_profile *profile;
	struct ratbag_resolution *resolution;
	struct ratbag_led *led;

	ratbag_device_for_each_profile(device, profile) {
		ratbag_profile_for_each_resolution(profile, resolution) {
			size_t dpi;

			for (dpi = 0; dpi < ARRAY_LENGTH(megaport_dpis); dpi++) {
				if (resolution->dpi_x == megaport_dpis[dpi])
					break;
			}
			if (dpi == ARRAY_LENGTH(megaport_dpis) ||
			    resolution->dpi_x != resolution->dpi_y)
				return -EINVAL;

			ratbag_profile_for_each_led(profile, led) {
				/* DPI and lighting share a write-only report. Apply both
				 * cached settings to hardware preset zero together. */
				uint8_t report[8] = { MEGAPORT_REPORT_ID, 0x09, 0x00 };
				int rc;

				if (!led->dirty && !resolution->dirty)
					continue;

				switch (led->mode) {
				case RATBAG_LED_OFF:
					report[3] = MEGAPORT_LED_OFF;
					break;
				case RATBAG_LED_ON:
					report[3] = MEGAPORT_LED_ON;
					break;
				default:
					return -ENOTSUP;
				}
				report[3] |= (dpi + 8) << 4;

				rc = ratbag_hidraw_set_feature_report(device, MEGAPORT_REPORT_ID,
							     report, sizeof(report));
				if (rc < 0)
					return rc;
				if (rc != sizeof(report))
					return -EIO;
			}
		}
	}

	return 0;
}

struct ratbag_driver megaport_driver = {
	.name = "Megaport mouse driver",
	.id = "megaport",
	.probe = megaport_probe,
	.remove = megaport_remove,
	.commit = megaport_commit,
};
