/* SPDX-License-Identifier: MIT */

#include <config.h>
#include <check.h>
#include <limits.h>

/* Exercise the driver itself with an in-memory feature-report transport. */
#define logitech_g600_driver test_logitech_g600_driver
#define ratbag_hidraw_raw_request test_g600_raw_request
#include "../src/driver-logitech-g600.c"
#undef ratbag_hidraw_raw_request
#undef logitech_g600_driver

static struct ratbag *context;
static struct ratbag_device *device;
static struct ratbag_profile *profile;
static struct logitech_g600_profile_report hardware[3];
static unsigned int writes;
static int write_result;
static unsigned int active_writes;
static int active_write_result;

int
test_g600_raw_request(struct ratbag_device *dev, unsigned char reportnum,
		      uint8_t *buf, size_t len, unsigned char rtype, int reqtype)
{
	ck_assert_ptr_eq(dev, device);
	ck_assert_int_eq(rtype, HID_FEATURE_REPORT);
	if (len == 4) {
		ck_assert_int_eq(reportnum, LOGITECH_G600_REPORT_ID_SET_ACTIVE);
		ck_assert_int_eq(reqtype, HID_REQ_SET_REPORT);
		active_writes++;
		return active_write_result == INT_MAX ? (int)len : active_write_result;
	}
	ck_assert_uint_ge(reportnum, LOGITECH_G600_REPORT_ID_PROFILE_0);
	ck_assert_uint_le(reportnum, LOGITECH_G600_REPORT_ID_PROFILE_2);
	ck_assert_uint_eq(len, sizeof(hardware[0]));
	unsigned int index = reportnum - LOGITECH_G600_REPORT_ID_PROFILE_0;
	if (reqtype == HID_REQ_GET_REPORT) {
		memcpy(buf, &hardware[index], len);
	} else {
		ck_assert_int_eq(reqtype, HID_REQ_SET_REPORT);
		writes++;
		if (write_result != INT_MAX)
			return write_result;
		memcpy(&hardware[index], buf, len);
	}
	return len;
}

static int
open_restricted(const char *path, int flags, void *userdata)
{
	ck_abort_msg("Unexpected device access: %s", path);
	return -EPERM;
}

static void
close_restricted(int fd, void *userdata)
{
	ck_abort_msg("Unexpected device close: %d", fd);
}

static const struct ratbag_interface interface = {
	.open_restricted = open_restricted,
	.close_restricted = close_restricted,
};

static void
setup(void)
{
	struct input_id id = { .bustype = BUS_USB, .vendor = 0x046d, .product = 0xc24a };
	context = ratbag_create_context(&interface, NULL);
	ck_assert_ptr_nonnull(context);
	device = ratbag_device_new(context, NULL, "G600 test", &id);
	ck_assert_ptr_nonnull(device);
	device->driver = &test_logitech_g600_driver;
	ratbag_device_init_profiles(device, 3, 4, 41, 1);
	ratbag_set_drv_data(device, zalloc(sizeof(struct logitech_g600_data)));
	writes = 0;
	write_result = INT_MAX;
	active_writes = 0;
	active_write_result = INT_MAX;

	for (unsigned int p = 0; p < ARRAY_LENGTH(hardware); p++) {
		struct logitech_g600_profile_report *report = &hardware[p];
		memset(report, 0, sizeof(*report));
		report->id = LOGITECH_G600_REPORT_ID_PROFILE_0 + p;
		report->led_red = 0x12;
		report->led_green = 0x34;
		report->led_blue = 0x56;
		report->led_effect = LOGITECH_G600_LED_BREATHE;
		report->led_duration = 4;
		memset(report->unknown1, 0xa1 + p, sizeof(report->unknown1));
		memset(report->unknown2, 0xb1 + p, sizeof(report->unknown2));
		report->dpi_shift = 25;
		report->dpi_default = 1;
		report->dpi[0] = 24;
		report->dpi[1] = 25;
		for (unsigned int i = 0; i < 20; i++) {
			report->buttons[i].key = 0x04 + i;
			report->g_shift_buttons[i].modifier = 1;
			report->g_shift_buttons[i].key = 0x1e + i;
		}
		report->buttons[0] = (struct logitech_g600_button) { .code = 1 };
		/* An assignment the driver cannot decode must survive unrelated edits. */
		report->buttons[19] = (struct logitech_g600_button) { .code = 0x16 };
		memset(report->g_shift_color, 0xd0 + p, sizeof(report->g_shift_color));
		struct ratbag_profile *current = ratbag_device_get_profile(device, p);
		logitech_g600_read_profile(current);
		ratbag_profile_unref(current);
	}
	profile = ratbag_device_get_profile(device, 0);
}

static void
teardown(void)
{
	ratbag_profile_unref(profile);
	free(ratbag_get_drv_data(device));
	device->driver = NULL;
	ck_assert_int_eq(device->refcount, 1);
	ratbag_device_unref(device);
	ck_assert_int_eq(context->refcount, 1);
	ratbag_unref(context);
}

static void
assert_report(const struct logitech_g600_profile_report *expected)
{
	struct logitech_g600_data *data = ratbag_get_drv_data(device);
	ck_assert_int_eq(memcmp(&hardware[0], expected, sizeof(*expected)), 0);
	ck_assert_int_eq(memcmp(&data->profile_data[0].report, expected, sizeof(*expected)), 0);
}

START_TEST(read_button_layers)
{
	_cleanup_button_ struct ratbag_button *button = ratbag_profile_get_button(profile, 0);
	ck_assert_int_eq(ratbag_button_get_button(button), 1);
	_cleanup_button_ struct ratbag_button *first_shift = ratbag_profile_get_button(profile, 21);
	ck_assert_int_eq(first_shift->action.macro->events[0].event.key, KEY_LEFTCTRL);
	ck_assert_int_eq(first_shift->action.macro->events[1].event.key, KEY_1);
	_cleanup_button_ struct ratbag_button *last_shift = ratbag_profile_get_button(profile, 40);
	ck_assert_int_eq(last_shift->action.macro->events[1].event.key, KEY_BACKSLASH);
	_cleanup_button_ struct ratbag_button *color = ratbag_profile_get_button(profile, 20);
	ck_assert_uint_eq(color->action_caps, 0);
	ck_assert_int_eq(ratbag_button_set_button(color, 1), RATBAG_ERROR_CAPABILITY);
}
END_TEST

START_TEST(dpi_edit_preserves_report)
{
	struct logitech_g600_profile_report expected = hardware[0];
	_cleanup_resolution_ struct ratbag_resolution *resolution =
		ratbag_profile_get_resolution(profile, 0);
	ck_assert_int_eq(ratbag_resolution_set_dpi(resolution, 1300), RATBAG_SUCCESS);
	expected.dpi[0] = 26;
	ck_assert_int_eq(ratbag_device_commit(device), RATBAG_SUCCESS);
	ck_assert_uint_eq(writes, 1);
	assert_report(&expected);
}
END_TEST

START_TEST(edit_normal_and_shifted_button)
{
	unsigned int index = _i == 0 ? 19 : (_i == 1 ? 21 : 40);
	_cleanup_button_ struct ratbag_button *button = ratbag_profile_get_button(profile, index);
	struct logitech_g600_profile_report expected = hardware[0];
	struct logitech_g600_button *raw = index < 20 ? &expected.buttons[index] :
		&expected.g_shift_buttons[index - 21];
	*raw = (struct logitech_g600_button) { .code = 5 };
	ck_assert_int_eq(ratbag_button_set_button(button, 5), RATBAG_SUCCESS);
	ck_assert_int_eq(ratbag_device_commit(device), RATBAG_SUCCESS);
	assert_report(&expected);
}
END_TEST

START_TEST(edit_led_preserves_shift_color)
{
	struct logitech_g600_profile_report expected = hardware[0];
	_cleanup_led_ struct ratbag_led *led = ratbag_profile_get_led(profile, 0);
	struct ratbag_color color = { .red = 0x65, .green = 0x43, .blue = 0x21 };
	ck_assert_int_eq(ratbag_led_set_color(led, color), RATBAG_SUCCESS);
	expected.led_red = 0x65;
	expected.led_green = 0x43;
	expected.led_blue = 0x21;
	ck_assert_int_eq(ratbag_device_commit(device), RATBAG_SUCCESS);
	assert_report(&expected);
}
END_TEST

START_TEST(disabled_dpi_roundtrip)
{
	struct logitech_g600_profile_report expected = hardware[0];
	_cleanup_resolution_ struct ratbag_resolution *resolution =
		ratbag_profile_get_resolution(profile, 2);
	ck_assert(ratbag_resolution_has_capability(resolution, RATBAG_RESOLUTION_CAP_DISABLE));
	ck_assert(ratbag_resolution_is_disabled(resolution));
	ck_assert_int_eq(ratbag_resolution_set_disabled(resolution, false), RATBAG_SUCCESS);
	ck_assert_int_eq(ratbag_resolution_set_dpi(resolution, 1250), RATBAG_SUCCESS);
	expected.dpi[2] = 25;
	ck_assert_int_eq(ratbag_device_commit(device), RATBAG_SUCCESS);
	assert_report(&expected);
	ck_assert_int_eq(ratbag_resolution_set_disabled(resolution, true), RATBAG_SUCCESS);
	expected.dpi[2] = 0;
	ck_assert_int_eq(ratbag_device_commit(device), RATBAG_SUCCESS);
	assert_report(&expected);
	logitech_g600_read_profile(profile);
	ck_assert(ratbag_resolution_is_disabled(resolution));
}
END_TEST

START_TEST(exact_dpi_steps)
{
	unsigned int dpis[300];
	_cleanup_resolution_ struct ratbag_resolution *resolution =
		ratbag_profile_get_resolution(profile, 0);
	unsigned int count = ratbag_resolution_get_dpi_list(resolution, dpis, ARRAY_LENGTH(dpis));
	ck_assert_uint_eq(count, 161);
	for (unsigned int i = 0; i < count; i++)
		ck_assert_uint_eq(dpis[i], 200 + i * 50);
}
END_TEST

static void
set_macro(unsigned int index, const unsigned int *keys,
	  const enum ratbag_macro_event_type *types, unsigned int count)
{
	_cleanup_button_ struct ratbag_button *button = ratbag_profile_get_button(profile, index);
	struct ratbag_button_macro *macro = ratbag_button_macro_new("test");
	for (unsigned int i = 0; i < count; i++)
		ck_assert_int_eq(ratbag_button_macro_set_event(macro, i, types[i], keys[i]), RATBAG_SUCCESS);
	ck_assert_int_eq(ratbag_button_set_macro(button, macro), RATBAG_SUCCESS);
	ratbag_button_macro_unref(macro);
}

START_TEST(modified_key_macro)
{
	struct logitech_g600_profile_report expected = hardware[0];
	const unsigned int keys[] = { KEY_LEFTCTRL, KEY_T, KEY_T, KEY_LEFTCTRL };
	const enum ratbag_macro_event_type types[] = {
		RATBAG_MACRO_EVENT_KEY_PRESSED, RATBAG_MACRO_EVENT_KEY_PRESSED,
		RATBAG_MACRO_EVENT_KEY_RELEASED, RATBAG_MACRO_EVENT_KEY_RELEASED,
	};
	set_macro(31, keys, types, ARRAY_LENGTH(keys));
	expected.g_shift_buttons[10] = (struct logitech_g600_button) { .modifier = 1, .key = 0x17 };
	ck_assert_int_eq(ratbag_device_commit(device), RATBAG_SUCCESS);
	assert_report(&expected);
}
END_TEST

START_TEST(invalid_macro_is_transactional)
{
	struct logitech_g600_profile_report expected = hardware[0];
	_cleanup_resolution_ struct ratbag_resolution *resolution =
		ratbag_profile_get_resolution(profile, 0);
	_cleanup_button_ struct ratbag_button *button = ratbag_profile_get_button(profile, 8);
	const unsigned int keys[][4] = {
		{ KEY_T },
		{ KEY_T, KEY_T, KEY_Y, KEY_Y },
		{ KEY_MAX, KEY_MAX },
	};
	const enum ratbag_macro_event_type types[] = {
		RATBAG_MACRO_EVENT_KEY_PRESSED, RATBAG_MACRO_EVENT_KEY_RELEASED,
		RATBAG_MACRO_EVENT_KEY_PRESSED, RATBAG_MACRO_EVENT_KEY_RELEASED,
	};
	const unsigned int counts[] = { 1, 4, 2 };
	ck_assert_int_eq(ratbag_resolution_set_dpi(resolution, 1300), RATBAG_SUCCESS);
	ck_assert_int_eq(ratbag_button_set_button(button, 2), RATBAG_SUCCESS);
	set_macro(10, keys[_i], types, counts[_i]);
	ck_assert_int_eq(logitech_g600_write_profile(profile), -EINVAL);
	ck_assert_uint_eq(writes, 0);
	assert_report(&expected);
}
END_TEST

START_TEST(transport_failure_preserves_cache)
{
	const int failures[] = { -EPIPE, 0, 153 };
	struct logitech_g600_profile_report expected = hardware[0];
	_cleanup_resolution_ struct ratbag_resolution *resolution =
		ratbag_profile_get_resolution(profile, 0);
	ck_assert_int_eq(ratbag_resolution_set_dpi(resolution, 1300), RATBAG_SUCCESS);
	write_result = failures[_i];
	ck_assert_int_eq(logitech_g600_write_profile(profile), write_result < 0 ? write_result : -EIO);
	ck_assert_uint_eq(writes, 1);
	assert_report(&expected);
}
END_TEST

START_TEST(short_active_transfer_is_error)
{
	active_write_result = _i;
	ck_assert_int_eq(logitech_g600_set_current_resolution(device, 0), -EIO);
	ck_assert_int_eq(logitech_g600_set_active_profile(device, 0), -EIO);
}
END_TEST

START_TEST(active_write_error_propagates)
{
	struct logitech_g600_profile_report expected = hardware[0];
	_cleanup_resolution_ struct ratbag_resolution *resolution =
		ratbag_profile_get_resolution(profile, 0);
	profile->is_active = true;
	ck_assert_int_eq(ratbag_resolution_set_dpi(resolution, 1300), RATBAG_SUCCESS);
	expected.dpi[0] = 26;
	active_write_result = _i == 0 ? -EPIPE : 3;
	ck_assert_int_eq(logitech_g600_write_profile(profile), _i == 0 ? -EPIPE : -EIO);
	ck_assert_uint_eq(writes, 1);
	ck_assert_uint_eq(active_writes, 1);
	/* The profile write succeeded even though selecting its DPI failed. */
	assert_report(&expected);
}
END_TEST

int
main(void)
{
	Suite *suite = suite_create("Logitech G600");
	TCase *tc = tcase_create("feature reports");
	SRunner *runner;
	int failed;
	tcase_add_checked_fixture(tc, setup, teardown);
	tcase_add_test(tc, read_button_layers);
	tcase_add_test(tc, dpi_edit_preserves_report);
	tcase_add_loop_test(tc, edit_normal_and_shifted_button, 0, 3);
	tcase_add_test(tc, edit_led_preserves_shift_color);
	tcase_add_test(tc, disabled_dpi_roundtrip);
	tcase_add_test(tc, exact_dpi_steps);
	tcase_add_test(tc, modified_key_macro);
	tcase_add_loop_test(tc, invalid_macro_is_transactional, 0, 3);
	tcase_add_loop_test(tc, transport_failure_preserves_cache, 0, 3);
	tcase_add_loop_test(tc, short_active_transfer_is_error, 0, 4);
	tcase_add_loop_test(tc, active_write_error_propagates, 0, 2);
	suite_add_tcase(suite, tc);
	runner = srunner_create(suite);
	srunner_run_all(runner, CK_NORMAL);
	failed = srunner_ntests_failed(runner);
	srunner_free(runner);
	return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
