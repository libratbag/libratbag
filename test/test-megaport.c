/* SPDX-License-Identifier: MIT */
#include "config.h"

#include <check.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "libratbag-private.h"

static struct ratbag_device device;
static struct ratbag_profile profile;
static struct ratbag_led led;
static struct ratbag_resolution resolution;
static unsigned int usage_page;
static int report_count;
static int transfer_result;
static uint8_t last_report[8];
static int close_count;

int
ratbag_hidraw_has_report(const struct ratbag_device *unused, unsigned int report_id)
{
	return report_id == 7;
}

unsigned int
ratbag_hidraw_get_usage_page(const struct ratbag_device *unused, unsigned int report_id)
{
	return usage_page;
}

int
ratbag_find_hidraw(struct ratbag_device *dev, int (*match)(struct ratbag_device *))
{
	return match(dev) ? 0 : -ENODEV;
}

int
ratbag_device_init_profiles(struct ratbag_device *dev, unsigned int profiles,
			   unsigned int resolutions, unsigned int buttons, unsigned int leds)
{
	ck_assert_uint_eq(profiles, 1);
	ck_assert_uint_eq(resolutions, 1);
	ck_assert_uint_eq(buttons, 0);
	ck_assert_uint_eq(leds, 1);
	list_init(&dev->profiles);
	list_init(&profile.leds);
	list_init(&profile.resolutions);
	list_append(&dev->profiles, &profile.link);
	list_append(&profile.leds, &led.link);
	list_append(&profile.resolutions, &resolution.link);
	resolution.profile = &profile;
	profile.device = dev;
	led.profile = &profile;
	return 0;
}

int
ratbag_hidraw_set_feature_report(struct ratbag_device *unused, unsigned char report_id,
				uint8_t *data, size_t length)
{
	ck_assert_uint_eq(report_id, 7);
	ck_assert_uint_eq(length, sizeof(last_report));
	memcpy(last_report, data, length);
	report_count++;
	return transfer_result;
}

void
ratbag_close_hidraw(struct ratbag_device *unused)
{
	close_count++;
}

static void
setup(void)
{
	memset(&device, 0, sizeof(device));
	memset(&profile, 0, sizeof(profile));
	memset(&led, 0, sizeof(led));
	memset(&resolution, 0, sizeof(resolution));
	usage_page = 0xff01;
	report_count = 0;
	transfer_result = 8;
	close_count = 0;
}

START_TEST(probe_is_read_only)
{
	ck_assert_int_eq(megaport_driver.probe(&device), 0);
	ck_assert(profile.is_active);
	ck_assert(long_bit_is_set(profile.capabilities, RATBAG_PROFILE_CAP_WRITE_ONLY));
	ck_assert_int_eq(led.colordepth, RATBAG_LED_COLORDEPTH_MONOCHROME);
	ck_assert_uint_eq(led.modes, (1U << RATBAG_LED_ON) | (1U << RATBAG_LED_OFF));
	ck_assert_int_eq(report_count, 0);
	ck_assert_int_eq(megaport_driver.commit(&device), 0);
	ck_assert_int_eq(report_count, 0);
	megaport_driver.remove(&device);
	ck_assert_int_eq(close_count, 1);
}
END_TEST

START_TEST(reject_wrong_interface)
{
	usage_page = 1;
	ck_assert_int_eq(megaport_driver.probe(&device), -ENODEV);
	ck_assert_int_eq(report_count, 0);
}
END_TEST

START_TEST(write_led_modes)
{
	const uint8_t off[] = {7, 0x09, 0, 0x93, 0, 0, 0, 0};
	const uint8_t on[] = {7, 0x09, 0, 0x92, 0, 0, 0, 0};
	ck_assert_int_eq(megaport_driver.probe(&device), 0);
	led.dirty = true;
	led.mode = RATBAG_LED_OFF;
	ck_assert_int_eq(megaport_driver.commit(&device), 0);
	ck_assert_mem_eq(last_report, off, sizeof(off));
	led.mode = RATBAG_LED_ON;
	ck_assert_int_eq(megaport_driver.commit(&device), 0);
	ck_assert_mem_eq(last_report, on, sizeof(on));
	ck_assert_int_eq(report_count, 2);
}
END_TEST

START_TEST(write_dpi_preserves_lighting)
{
	const unsigned int dpis[] = {1200, 1600, 2000, 2400, 3200};
	ck_assert_int_eq(megaport_driver.probe(&device), 0);
	resolution.dirty = true;
	for (size_t i = 0; i < ARRAY_LENGTH(dpis); i++) {
		resolution.dpi_x = resolution.dpi_y = dpis[i];
		ck_assert_int_eq(megaport_driver.commit(&device), 0);
		ck_assert_uint_eq(last_report[1], 0x09);
		ck_assert_uint_eq(last_report[2], 0);
		ck_assert_uint_eq(last_report[3], ((i + 8) << 4) | 3);
	}
	resolution.dpi_x = resolution.dpi_y = 1234;
	ck_assert_int_eq(megaport_driver.commit(&device), -EINVAL);
	ck_assert_int_eq(report_count, 5);
}
END_TEST

START_TEST(reject_unsupported_mode)
{
	ck_assert_int_eq(megaport_driver.probe(&device), 0);
	led.dirty = true;
	led.mode = RATBAG_LED_CYCLE;
	ck_assert_int_eq(megaport_driver.commit(&device), -ENOTSUP);
	ck_assert_int_eq(report_count, 0);
}
END_TEST

START_TEST(propagate_transfer_errors)
{
	ck_assert_int_eq(megaport_driver.probe(&device), 0);
	led.dirty = true;
	transfer_result = -EPIPE;
	ck_assert_int_eq(megaport_driver.commit(&device), -EPIPE);
	transfer_result = 4;
	ck_assert_int_eq(megaport_driver.commit(&device), -EIO);
}
END_TEST

int
main(void)
{
	Suite *suite = suite_create("megaport");
	TCase *tests = tcase_create("lighting");
	SRunner *runner;
	int failures;

	tcase_add_checked_fixture(tests, setup, NULL);
	tcase_add_test(tests, probe_is_read_only);
	tcase_add_test(tests, reject_wrong_interface);
	tcase_add_test(tests, write_led_modes);
	tcase_add_test(tests, write_dpi_preserves_lighting);
	tcase_add_test(tests, reject_unsupported_mode);
	tcase_add_test(tests, propagate_transfer_errors);
	suite_add_tcase(suite, tests);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_NORMAL);
	failures = srunner_ntests_failed(runner);
	srunner_free(runner);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
