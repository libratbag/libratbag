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

#include "libratbag.h"
#include "libratbag-private.h"
#include "libratbag-data.h"
#include "hidpp20.h"
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
	ROCCAT_EMP,
	ETEKCITY,
	GSKILL,
	LOGITECH_G300,
	LOGITECH_G600,
	STEELSERIES,
	SINOWEALTH,
	SINOWEALTH_NUBWO,
	OPENINPUT,
};

struct data_hidpp20 {
	int index;
	enum hidpp20_quirk quirk;
	int led_count;
};

struct data_hidpp10 {
	int index;
	int profile_count;
	char *profile_type;

	struct dpi_list *dpi_list;
	struct dpi_range *dpi_range;
	int led_count;
};

struct data_steelseries {
	int device_version;
	int button_count;
	int led_count;
	struct dpi_list *dpi_list;
	struct dpi_range *dpi_range;
	int macro_length;
	int mono_led;
	int short_button;
};

struct ratbag_device_data {
	int refcount;
	char *name;
	char *driver;

	enum driver drivertype;

	union {
		struct data_hidpp20 hidpp20;
		struct data_hidpp10 hidpp10;
		struct data_steelseries steelseries;
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

	data->hidpp20.index = -1;
	data->hidpp20.led_count = -1;

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
init_data_steelseries(struct ratbag *ratbag,
		  GKeyFile *keyfile,
		  struct ratbag_device_data *data)
{
	const char *group = "Driver/steelseries";
	GError *error = NULL;
	_cleanup_(freep) char *str = NULL;
	int num;

	data->steelseries.device_version = -1;
	data->steelseries.button_count = -1;
	data->steelseries.led_count = -1;
	data->steelseries.dpi_list = NULL;
	data->steelseries.dpi_range = NULL;
	data->steelseries.mono_led = 0;
	data->steelseries.short_button = 0;

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

	str = g_key_file_get_string(keyfile, group, "DpiRange", NULL);
	if (str) {
		data->steelseries.dpi_range = dpi_range_from_string(str);
	} else {
		str = g_key_file_get_string(keyfile, group, "DpiList", NULL);
		if (str)
			data->steelseries.dpi_list = dpi_list_from_string(str);
	}

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "MacroLength", &error);
	if (num > 0 || !error)
		data->steelseries.macro_length = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "MonoLed", &error);
	if (num > 0 || !error)
		data->steelseries.mono_led = num;
	if (error)
		g_error_free(error);

	error = NULL;
	num = g_key_file_get_integer(keyfile, group, "ShortButton", &error);
	if (num > 0 || !error)
		data->steelseries.short_button = num;
	if (error)
		g_error_free(error);
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
	{ ROCCAT_EMP, "roccat-kone-emp", NULL },
	{ ETEKCITY, "etekcity", NULL},
	{ GSKILL, "gskill", NULL },
	{ LOGITECH_G300, "logitech_g300", NULL},
	{ LOGITECH_G600, "logitech_g600", NULL},
	{ STEELSERIES, "steelseries", init_data_steelseries },
	{ SINOWEALTH, "sinowealth", NULL },
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
		free(data->hidpp10.profile_type);
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
ratbag_device_data_hidpp20_get_led_count(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.led_count;
}

enum hidpp20_quirk
ratbag_device_data_hidpp20_get_quirk(const struct ratbag_device_data *data)
{
	assert(data->drivertype == HIDPP20);

	return data->hidpp20.quirk;
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

int
ratbag_device_data_steelseries_get_mono_led(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.mono_led;
}

int
ratbag_device_data_steelseries_get_short_button(const struct ratbag_device_data *data)
{
	assert(data->drivertype == STEELSERIES);

	return data->steelseries.short_button;
}
