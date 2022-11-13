/*
 * Copyright © 2017 Red Hat, Inc.
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

#include "config.h"

#include <linux/input.h>

#include <assert.h>
#include <stdlib.h>
#include <glib.h>
#include <limits.h>

#include "asus.h"
#include "driver-sinowealth.h"
#include "driver-steelseries.h"
#include "libratbag.h"
#include "libratbag-private.h"
#include "libratbag-data.h"
#include "hidpp20.h"
#include "shared-macro.h"
#include "usb-ids.h"

#define GROUP_DEVICE "Device"

DEFINE_TRIVIAL_CLEANUP_FUNC(GKeyFile *, g_key_file_free);
DEFINE_TRIVIAL_CLEANUP_FUNC(GError *, g_error_free);
DEFINE_TRIVIAL_CLEANUP_FUNC(char **, g_strfreev);

enum driver {
	NONE = 0,
	HIDPP10,
	HIDPP20,
	ROCCAT,
	ROCCAT_KONE_PURE,
	ROCCAT_EMP,
	ETEKCITY,
	GSKILL,
	LOGITECH_G300,
	LOGITECH_G600,
	STEELSERIES,
	ASUS,
	SINOWEALTH,
	SINOWEALTH_NUBWO,
	OPENINPUT,
};

struct data_hidpp20 {
	int index;
	enum hidpp20_quirk quirk;
	int led_count;
	int report_rate;
	int button_count;
};

struct data_hidpp10 {
	int index;
	int profile_count;
	char *profile_type;

	struct dpi_list *dpi_list;
	struct dpi_range *dpi_range;
	int led_count;
};

struct data_sinowealth {
	struct list supported_devices;
};

struct data_steelseries {
	int device_version;
	int button_count;
	int led_count;
	struct dpi_list *dpi_list;
	struct dpi_range *dpi_range;
	int macro_length;
	enum steelseries_quirk quirk;
};

struct data_asus {
	int profile_count;
	int button_count;
	int8_t button_mapping[ASUS_MAX_NUM_BUTTON];
	int led_count;
	int dpi_count;
	int is_wireless;
	struct dpi_range *dpi_range;
	uint32_t quirks;
};

struct ratbag_device_data {
	int refcount;
	char *name;
	char *driver;

	enum driver drivertype;

	union {
		struct data_hidpp20 hidpp20;
		struct data_hidpp10 hidpp10;
		struct data_sinowealth sinowealth;
		struct data_steelseries steelseries;
		struct data_asus asus;
	};

	enum ratbag_led_type led_types[20];
	size_t nled_types;
};

static void
init_data_hidpp10(struct ratbag *ratbag,
		  GKeyFile *keyfile,
		  struct ratbag_device_data *data)
{
	const char *group = "Driver/hidpp10";
	char *profile_type;
	GError *error = NULL;
	_cleanup_(freep) char *str = NULL;
	int num;

	data->hidpp10.index = -1;
	data->hidpp10.profile_count = -1;
	data->hidpp10.profile_type = NULL;
	data->hidpp10.led_count = -1;

	num = g_key_file_get_integer(keyfile, group, "DeviceIndex", &error);
	if (num != 0 || !error)
		data->hidpp10.index = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "Profiles", &error);
	if (num > 0 || !error)
		data->hidpp10.profile_count = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "Leds", &error);
	if (num > 0 || !error)
		data->hidpp10.led_count = num;
	if (error)
		g_error_free(error);

	profile_type = g_key_file_get_string(keyfile, group, "ProfileType", NULL);
	if (profile_type)
		data->hidpp10.profile_type = profile_type;

	str = g_key_file_get_string(keyfile, group, "DpiRange", NULL);
	if (str) {
		data->hidpp10.dpi_range = dpi_range_from_string(str);
	} else {
		str = g_key_file_get_string(keyfile, group, "DpiList", NULL);
		if (str)
			data->hidpp10.dpi_list = dpi_list_from_string(str);
	}
}

static void
init_data_hidpp20(struct ratbag *ratbag,
		  GKeyFile *keyfile,
		  struct ratbag_device_data *data)
{
	const char *group = "Driver/hidpp20";
	GError *error = NULL;
	int num;
	char *str;

	data->hidpp20.button_count = -1;
	data->hidpp20.index = -1;
	data->hidpp20.led_count = -1;
	data->hidpp20.report_rate = -1;

	num = g_key_file_get_integer(keyfile, group, "Buttons", &error);
	if (num > 0 || !error)
		data->hidpp20.button_count = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "DeviceIndex", &error);
	if (num != 0 || !error)
		data->hidpp20.index = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "Leds", &error);
	if (!error)
		data->hidpp20.led_count = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "ReportRate", &error);
	if (num > 0 || !error)
		data->hidpp20.report_rate = num;
	if (error)
		g_error_free(error);

	str = g_key_file_get_string(keyfile, group, "Quirk", NULL);
	data->hidpp20.quirk = HIDPP20_QUIRK_NONE;
	if (str) {
		if (streq(str, "G305"))
			data->hidpp20.quirk = HIDPP20_QUIRK_G305;
		else if(streq(str, "G602"))
			data->hidpp20.quirk = HIDPP20_QUIRK_G602;
	}
}

static void
init_data_sinowealth(struct ratbag *ratbag,
		  GKeyFile *keyfile,
		  struct ratbag_device_data *data)
{
	const char *devices_group = "Driver/sinowealth/devices/";

	GError *error = NULL;

	size_t group_count = 0;
	_cleanup_(g_strfreevp) char **groups = g_key_file_get_groups(keyfile, &group_count);

	list_init(&data->sinowealth.supported_devices);

	for (size_t i = 0; i < group_count; ++i) {
		const char *device_group = groups[i];
		if (startswith(device_group, devices_group) == NULL)
			continue;

		struct sinowealth_device_data *device = zalloc(sizeof(struct sinowealth_device_data));

		device->button_count = g_key_file_get_integer(keyfile, device_group, "ButtonCount", &error);
		g_clear_error(&error);

		device->device_name = g_key_file_get_string(keyfile, device_group, "DeviceName", &error);
		g_clear_error(&error);

		device->fw_version = g_key_file_get_string(keyfile, device_group, "FwVersion", &error);
		g_clear_error(&error);

		_cleanup_free_ char *led_type_str = g_key_file_get_string(keyfile, device_group, "LedType", &error);
		if (led_type_str) {
			if (streq(led_type_str, "RGB")) {
				device->led_type = SINOWEALTH_LED_TYPE_RGB;
			} else if (streq(led_type_str, "RBG")) {
				device->led_type = SINOWEALTH_LED_TYPE_RBG;
			} else if (streq(led_type_str, "None")) {
				device->led_type = SINOWEALTH_LED_TYPE_NONE;
			} else {
				log_error(ratbag, "Unknown LED type '%s' in group '%s'\n", led_type_str, device_group);

				device->led_type = SINOWEALTH_LED_TYPE_NONE;
			}
		}
		g_clear_error(&error);

		list_insert(&data->sinowealth.supported_devices, &device->link);
	}
}

static void
init_data_steelseries(struct ratbag *ratbag,
		  GKeyFile *keyfile,
		  struct ratbag_device_data *data)
{
	const char *group = "Driver/steelseries";
	GError *error = NULL;
	_cleanup_(freep) char *dpi_range = NULL;
	_cleanup_(freep) char *quirk = NULL;
	int num;

	data->steelseries.device_version = -1;
	data->steelseries.button_count = -1;
	data->steelseries.led_count = -1;
	data->steelseries.dpi_list = NULL;
	data->steelseries.dpi_range = NULL;
	data->steelseries.quirk = STEELSERIES_QURIK_NONE;

	num = g_key_file_get_integer(keyfile, group, "Buttons", &error);
	if (num != 0 || !error)
		data->steelseries.button_count = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "Leds", &error);
	if (num > 0 || !error)
		data->steelseries.led_count = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "DeviceVersion", &error);
	if (num > 0 || !error)
		data->steelseries.device_version = num;
	if (error)
		g_error_free(error);

	dpi_range = g_key_file_get_string(keyfile, group, "DpiRange", NULL);
	if (dpi_range) {
		data->steelseries.dpi_range = dpi_range_from_string(dpi_range);
	} else {
		dpi_range = g_key_file_get_string(keyfile, group, "DpiList", NULL);
		if (dpi_range)
			data->steelseries.dpi_list = dpi_list_from_string(dpi_range);
	}

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "MacroLength", &error);
	if (num > 0 || !error)
		data->steelseries.macro_length = num;
	if (error)
		g_error_free(error);

	quirk = g_key_file_get_string(keyfile, group, "Quirk", NULL);
	if (quirk) {
		if (streq(quirk, "Rival100"))
			data->steelseries.quirk = STEELSERIES_QUIRK_RIVAL100;
		if (streq(quirk, "SenseiRAW"))
			data->steelseries.quirk = STEELSERIES_QUIRK_SENSEIRAW;
	}
}

static void
init_data_asus(struct ratbag *ratbag,
		GKeyFile *keyfile,
		struct ratbag_device_data *data)
{
	const char *group = "Driver/asus";
	GError *error = NULL;

	data->asus.profile_count = -1;
	data->asus.button_count = -1;
	data->asus.led_count = -1;
	data->asus.dpi_count = -1;
	data->asus.dpi_range = NULL;
	data->asus.is_wireless = -1;
	data->asus.quirks = 0;
	for (unsigned int i = 0; i < ASUS_MAX_NUM_BUTTON; i++)
		data->asus.button_mapping[i] = -1;

	int profiles = g_key_file_get_integer(keyfile, group, "Profiles", &error);
	if (!error && profiles >= 0)
		data->asus.profile_count = profiles;
	g_clear_error(&error);

	int buttons = g_key_file_get_integer(keyfile, group, "Buttons", &error);
	if (!error && buttons >= 0 && buttons <= ASUS_MAX_NUM_BUTTON)
		data->asus.button_count = buttons;
	g_clear_error(&error);

	gsize button_mapping_count = 0;
	_cleanup_(g_strfreevp) char **button_mapping = g_key_file_get_string_list(keyfile, group, "ButtonMapping", &button_mapping_count, &error);
	if (!error && button_mapping) {
		for (unsigned int i = 0; i < button_mapping_count; i++) {
			data->asus.button_mapping[i] = (int8_t) strtoul(button_mapping[i], NULL, 16);
		}
	}
	g_clear_error(&error);

        int leds = g_key_file_get_integer(keyfile, group, "Leds", &error);
	if (!error && leds >= 0 && leds <= ASUS_MAX_NUM_LED)
		data->asus.led_count = leds;
	g_clear_error(&error);

        int dpis = g_key_file_get_integer(keyfile, group, "Dpis", &error);
	if (!error && dpis >= 2 && dpis <= ASUS_MAX_NUM_DPI)
		data->asus.dpi_count = dpis;
	g_clear_error(&error);

	_cleanup_(freep) char *dpi_range = g_key_file_get_string(keyfile, group, "DpiRange", &error);
	if (!error && dpi_range)
		data->asus.dpi_range = dpi_range_from_string(dpi_range);
	g_clear_error(&error);

        int wireless = g_key_file_get_integer(keyfile, group, "Wireless", &error);
	if (!error && (wireless == 0 || wireless == 1))
		data->asus.is_wireless = wireless;
	g_clear_error(&error);

        gsize quirks_count = 0;
	_cleanup_(g_strfreevp) char **quirks = g_key_file_get_string_list(keyfile, group, "Quirks", &quirks_count, &error);
	if (!error && quirks) {
		for (unsigned int i = 0; i < quirks_count; i++) {
			if (streq(quirks[i], "DOUBLE_DPI")) {
				data->asus.quirks |= ASUS_QUIRK_DOUBLE_DPI;
			} else if (streq(quirks[i], "STRIX_PROFILE")) {
				data->asus.quirks |= ASUS_QUIRK_STRIX_PROFILE;
			} else {
				log_debug(ratbag, "%s is invalid quirk. Ignoring...\n", quirks[i]);
			}
		}
	}
	g_clear_error(&error);
}

static const struct driver_map {
	enum driver map;
	const char *driver;
	void (*init)(struct ratbag *ratbag,
		     GKeyFile *keyfile,
		     struct ratbag_device_data *data);
} driver_map[] = {
	{ HIDPP10, "hidpp10", init_data_hidpp10 },
	{ HIDPP20, "hidpp20", init_data_hidpp20 },
	{ ROCCAT, "roccat", NULL },
	{ ROCCAT_KONE_PURE, "roccat-kone-pure", NULL },
	{ ROCCAT_EMP, "roccat-kone-emp", NULL },
	{ ETEKCITY, "etekcity", NULL},
	{ GSKILL, "gskill", NULL },
	{ LOGITECH_G300, "logitech_g300", NULL},
	{ LOGITECH_G600, "logitech_g600", NULL},
	{ STEELSERIES, "steelseries", init_data_steelseries },
	{ ASUS, "asus", init_data_asus },
	{ SINOWEALTH, "sinowealth", init_data_sinowealth },
	{ SINOWEALTH_NUBWO, "sinowealth_nubwo", NULL},
	{ OPENINPUT, "openinput", NULL },
};

const char *
ratbag_device_data_get_driver(const struct ratbag_device_data *data)
{
	return data->driver;
}

const char *
ratbag_device_data_get_name(const struct ratbag_device_data *data)
{
	return data->name;
}

enum ratbag_led_type
ratbag_device_data_get_led_type(const struct ratbag_device_data *data,
				unsigned int index)
{
	assert(index < ARRAY_LENGTH(data->led_types));

	return data->led_types[index];
}

struct ratbag_device_data *
ratbag_device_data_ref(struct ratbag_device_data *data)
{
	data->refcount++;
	return data;
}

static void
ratbag_device_data_destroy(struct ratbag_device_data *data)
{
	switch (data->drivertype) {
	case HIDPP10:
		if (data->hidpp10.dpi_list) {
			free(data->hidpp10.dpi_list->entries);
			free(data->hidpp10.dpi_list);
		}

		free(data->hidpp10.dpi_range);
		free(data->hidpp10.profile_type);
		break;
	case SINOWEALTH: {
		struct sinowealth_device_data *device_data = NULL;
		struct sinowealth_device_data *device_data_next = NULL;

		list_for_each_safe(device_data, device_data_next, &data->sinowealth.supported_devices, link) {
			free(device_data->device_name);
			free(device_data->fw_version);
			free(device_data);
		}

		break;
	}
	case STEELSERIES:
		if (data->steelseries.dpi_list) {
			free(data->steelseries.dpi_list->entries);
			free(data->steelseries.dpi_list);
		}

		free(data->steelseries.dpi_range);
		break;
	default:
		break;
	}
	free(data->name);
	free(data->driver);
	free(data);
}

struct ratbag_device_data *
ratbag_device_data_unref(struct ratbag_device_data *data)
{
	if (data == NULL)
		return NULL;

	assert(data->refcount > 0);
	data->refcount--;

	if (data->refcount == 0)
		ratbag_device_data_destroy(data);

	return NULL;
}

DEFINE_TRIVIAL_CLEANUP_FUNC(struct ratbag_device_data *, ratbag_device_data_unref);

static int
parse_ledtypes(char **strv, enum ratbag_led_type *types, size_t ntypes)
{
	unsigned int i;
	int count = 0;

	for (i = 0; i < ntypes; i++)
		types[i] = RATBAG_LED_TYPE_UNKNOWN;

	if (!strv)
		return count;

	i = 0;
	while(strv[i]) {
		const char *s = strv[i];

		if (streq(s, "logo"))
			types[i] = RATBAG_LED_TYPE_LOGO;
		else if (streq(s, "side"))
			types[i] = RATBAG_LED_TYPE_SIDE;
		else if (streq(s, "dpi"))
			types[i] = RATBAG_LED_TYPE_SIDE;
		else if (streq(s, "battery"))
			types[i] = RATBAG_LED_TYPE_SIDE;
		else if (streq(s, "wheel"))
			types[i] = RATBAG_LED_TYPE_WHEEL;
		else if (streq(s, "switches"))
			types[i] = RATBAG_LED_TYPE_SWITCHES;
		else
			return -1;

		count++;
		i++;
	}

	return count;
}

static bool
match(const struct input_id *id, char **strv)
{
	const char *bus;
	char str[64];
	int i = 0;

	switch (id->bustype) {
	case BUS_USB: bus = "usb"; break;
	case BUS_BLUETOOTH: bus = "bluetooth"; break;
	default:
	    return false;
	}

	snprintf(str, sizeof(str), "%s:%04x:%04x", bus, id->vendor, id->product);

	while (strv[i]) {
		if (streq(strv[i], str))
			return true;
		i++;
	}

	return false;
}

static bool
file_data_matches(struct ratbag *ratbag,
		  const char *path, const struct input_id *id,
		  struct ratbag_device_data **data_out)
{
	_cleanup_(g_key_file_freep) GKeyFile *keyfile = NULL;
	_cleanup_(g_error_freep) GError *error = NULL;
	_cleanup_(g_strfreevp) char **match_strv = NULL;
	_cleanup_(g_strfreevp) char **ledtypes_strv = NULL;
	_cleanup_(ratbag_device_data_unrefp) struct ratbag_device_data *data = NULL;
	int rc;

	keyfile = g_key_file_new();
	rc = g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error);
	if (!rc) {
		log_error(ratbag, "Failed to parse keyfile %s: %s\n", path, error->message);
		return false;
	}

	match_strv = g_key_file_get_string_list(keyfile, GROUP_DEVICE, "DeviceMatch", NULL, NULL);
	if (!match_strv) {
		log_error(ratbag, "Missing DeviceMatch in %s\n", basename(path));
		return false;
	}

	if (!match(id, match_strv))
		return false;

	data = zalloc(sizeof(*data));
	data->refcount = 1;
	data->name = g_key_file_get_string(keyfile, GROUP_DEVICE, "Name", NULL);
	if (!data->name) {
		return false; // ignore_clang_sa_mem_leak
	}

	data->driver = g_key_file_get_string(keyfile, GROUP_DEVICE, "Driver", NULL);
	if (!data->driver) {
		log_error(ratbag, "Missing Driver in %s\n", basename(path));
		return false;
	} else {
		const struct driver_map *map;

		data->drivertype = NONE;
		ARRAY_FOR_EACH(driver_map, map) {
			if (streq(map->driver, data->driver)) {
				data->drivertype = map->map;
				if (map->init)
					map->init(ratbag, keyfile, data);
				break;
			}
		}

		if (data->drivertype == NONE) {
			log_error(ratbag, "Unknown driver %s in %s\n",
				  data->driver, basename(path));
			return false;
		}
	}

	ledtypes_strv = g_key_file_get_string_list(keyfile, GROUP_DEVICE, "LedTypes", NULL, NULL);
	if (parse_ledtypes(ledtypes_strv, data->led_types, ARRAY_LENGTH(data->led_types)) < 0) {
		log_error(ratbag, "Invalid LedTypes string in '%s'\n", basename(path));
		return false;
	}

	*data_out = data;
	data = NULL;

	return true;
}

static int
filter_device_files(const struct dirent *entry)
{
	const char *SUFFIX = ".device";
	const char *name = entry->d_name;
	int len, slen;

	if (!name || name[0] == '.')
		return 0;

	len = strlen(name);
	slen = strlen(SUFFIX);
	if (len <= slen)
		return 0;

	return streq(&name[len - slen], SUFFIX);
}

struct ratbag_device_data *
ratbag_device_data_new_for_id(struct ratbag *ratbag, const struct input_id *id)
{
	struct ratbag_device_data *data = NULL;
	struct dirent **files;
	int n, nfiles;
	const char *datadir;

	datadir = getenv("LIBRATBAG_DATA_DIR");
	if (!datadir)
		datadir = LIBRATBAG_DATA_DIR;
	log_debug(ratbag, "Using data directory '%s'\n", datadir);

	n = scandir(datadir, &files, filter_device_files, alphasort);
	if (n <= 0) {
		log_error(ratbag, "Unable to locate device files in %s: %s\n",
			  datadir, n == 0 ? "No files found" : strerror(errno));
		return NULL;
	}

	nfiles = n;
	while(n--) {
		_cleanup_(freep) char *file = NULL;
		int rc;

		rc = xasprintf(&file, "%s/%s", datadir, files[n]->d_name);
		if (rc == -1)
			goto out;
		if (file_data_matches(ratbag, file, id, &data))
			goto out;
	}

	if (id->vendor == USB_VENDOR_ID_LOGITECH && (id->product & 0xff00) == 0xc500)
		log_debug(ratbag, "%04x:%04x is a Logitech receiver, not a device. Ignoring...\n", id->vendor, id->product);
	else if (!data)
		log_debug(ratbag, "No data file found for %04x:%04x\n", id->vendor, id->product);

out:
	while(nfiles--)
		free(files[nfiles]);
	free(files);

	return data;
}


/* HID++ 1.0 */

int
ratbag_device_data_hidpp10_get_index(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP10);

	return data->hidpp10.index;
}

int
ratbag_device_data_hidpp10_get_profile_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP10);

	return data->hidpp10.profile_count;
}

const char *
ratbag_device_data_hidpp10_get_profile_type(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP10);

	return data->hidpp10.profile_type;
}

struct dpi_list *
ratbag_device_data_hidpp10_get_dpi_list(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP10);

	return data->hidpp10.dpi_list;
}

struct dpi_range *
ratbag_device_data_hidpp10_get_dpi_range(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP10);

	return data->hidpp10.dpi_range;
}

int
ratbag_device_data_hidpp10_get_led_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP10);

	return data->hidpp10.led_count;
}

/* HID++ 2.0 */

int
ratbag_device_data_hidpp20_get_index(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.index;
}

int
ratbag_device_data_hidpp20_get_button_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.button_count;
}

int
ratbag_device_data_hidpp20_get_led_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.led_count;
}

int
ratbag_device_data_hidpp20_get_report_rate(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.report_rate;
}

enum hidpp20_quirk
ratbag_device_data_hidpp20_get_quirk(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.quirk;
}

/* SinoWealth */

const struct list *
ratbag_device_data_sinowealth_get_supported_devices(const struct ratbag_device_data *data)
{
	assert(data->drivertype == SINOWEALTH);

	return &data->sinowealth.supported_devices;
}

/* SteelSeries */

int
ratbag_device_data_steelseries_get_device_version(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.device_version;
}

int
ratbag_device_data_steelseries_get_button_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.button_count;
}

int
ratbag_device_data_steelseries_get_led_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.led_count;
}

struct dpi_list *
ratbag_device_data_steelseries_get_dpi_list(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.dpi_list;
}

struct dpi_range *
ratbag_device_data_steelseries_get_dpi_range(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.dpi_range;
}

int
ratbag_device_data_steelseries_get_macro_length(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.macro_length;
}

enum steelseries_quirk
ratbag_device_data_steelseries_get_quirk(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.quirk;
}

/* ASUS */

int
ratbag_device_data_asus_get_profile_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.profile_count;
}

int
ratbag_device_data_asus_get_button_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.button_count;
}

const int8_t *
ratbag_device_data_asus_get_button_mapping(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.button_mapping;
}

int
ratbag_device_data_asus_get_led_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.led_count;
}

int
ratbag_device_data_asus_get_dpi_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.dpi_count;
}

struct dpi_range *
ratbag_device_data_asus_get_dpi_range(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.dpi_range;
}

int
ratbag_device_data_asus_is_wireless(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.is_wireless;
}

uint32_t
ratbag_device_data_asus_get_quirks(const struct ratbag_device_data *data)
{
	assert(data->drivertype == ASUS);
	return data->asus.quirks;
}
