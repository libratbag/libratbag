/*
 * Copyright 2013-2015 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * Copyright 2013-2015 Red Hat, Inc
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

/*
 * Based on the HID++ 1.0 documentation provided by Nestor Lopez Casado at:
 *   https://drive.google.com/folderview?id=0BxbRzx7vEV7eWmgwazJ3NUFfQ28&usp=sharing
 */

/*
 * for this driver to work, you need a kernel >= v3.19 or one which contains
 * 925f0f3ed24f98b40c28627e74ff3e7f9d1e28bc ("HID: logitech-dj: allow transfer
 * of HID++ reports from/to the correct dj device")
 */

#include "config.h"

#include <linux/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hidpp20.h"

#include "libratbag-private.h"
#include "libratbag-hidraw.h"

#define HIDPP_CAP_RESOLUTION_2200			(1 << 0)
#define HIDPP_CAP_SWITCHABLE_RESOLUTION_2201		(1 << 1)
#define HIDPP_CAP_BUTTON_KEY_1b04			(1 << 2)
#define HIDPP_CAP_BATTERY_LEVEL_1000			(1 << 3)
#define HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00		(1 << 4)

struct hidpp20drv_data {
	unsigned proto_major;
	unsigned proto_minor;
	unsigned long capabilities;
	unsigned num_sensors;
	struct hidpp20_sensor *sensors;
	unsigned num_controls;
	struct hidpp20_control_id *controls;
};

static void
hidpp20drv_read_button(struct ratbag_button *button)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_control_id *control;
	const struct ratbag_button_action *action;
	uint16_t mapping;

	if (!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04))
		return;

	control = &drv_data->controls[button->index];
	mapping = control->control_id;
	if (control->reporting.divert || control->reporting.persist)
		mapping = control->reporting.remapped;
	log_raw(device->ratbag,
		  " - button%d: %s (%02x) %s%s:%d\n",
		  button->index,
		  hidpp20_1b04_get_logical_mapping_name(mapping),
		  mapping,
		  control->reporting.divert || control->reporting.persist ? "(redirected) " : "",
		  __FILE__, __LINE__);
	button->type = hidpp20_1b04_get_physical_mapping(control->task_id);
	action = hidpp20_1b04_get_logical_mapping(mapping);
	if (action)
		button->action = *action;

	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_BUTTON);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_KEY);
	ratbag_button_enable_action_type(button, RATBAG_BUTTON_ACTION_TYPE_SPECIAL);
}

static int
hidpp20drv_write_button(struct ratbag_button *button,
			const struct ratbag_button_action *action)
{
	struct ratbag_device *device = button->profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_control_id *control;
	uint16_t mapping;
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04))
		return -ENOTSUP;

	control = &drv_data->controls[button->index];
	mapping = hidpp20_1b04_get_logical_control_id(action);
	if (!mapping)
		return -EINVAL;

	control->reporting.divert = 1;
	control->reporting.remapped = mapping;
	control->reporting.updated = 1;

	rc = hidpp20_special_key_mouse_set_control(device, control);
	if (rc == ERR_INVALID_ADDRESS)
		return -EINVAL;

	if (rc)
		log_error(device->ratbag,
			  "Error while writing profile: '%s' (%d)\n",
			  strerror(-rc),
			  rc);

	return rc;
}

static int
hidpp20drv_has_capability(const struct ratbag_device *device,
			  enum ratbag_device_capability cap)
{
	/*
	 * We need to force the non const, but we will not change anything,
	 * promise!
	 */
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data((struct ratbag_device *)device);

	switch (cap) {
	case RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION:
		return !!(drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201);
	case RATBAG_DEVICE_CAP_BUTTON_KEY:
		return !!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04);
	default:
		return 0;
	}
	return 0;
}

static int
hidpp20drv_current_profile(struct ratbag_device *device)
{
	return 0;
}

static int
hidpp20drv_set_current_profile(struct ratbag_device *device, unsigned int index)
{
	return -ENOTSUP;
}

static int
hidpp20drv_set_default_profile(struct ratbag_device *device, unsigned int index)
{
	return -ENOTSUP;
}

static int
hidpp20drv_read_resolution_dpi(struct ratbag_profile *profile)
{
	struct ratbag_device *device = profile->device;
	struct ratbag *ratbag = device->ratbag;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *res;
	int rc;
	unsigned int i;

	if (drv_data->capabilities & HIDPP_CAP_RESOLUTION_2200) {
		uint16_t resolution;
		uint8_t flags;

		profile->resolution.num_modes = 1;
		rc = hidpp20_mousepointer_get_mousepointer_info(device, &resolution, &flags);
		if (rc) {
			log_error(ratbag,
				  "Error while requesting resolution: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		}

		return 0;
	}

	if (drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201) {
		free(drv_data->sensors);
		drv_data->sensors = NULL;
		drv_data->num_sensors = 0;
		rc = hidpp20_adjustable_dpi_get_sensors(device, &drv_data->sensors);
		if (rc < 0) {
			log_error(ratbag,
				  "Error while requesting resolution: %s (%d)\n",
				  strerror(-rc), rc);
			return rc;
		} else if (rc == 0) {
			log_error(ratbag, "Error, no compatible sensors found.\n");
			return -ENODEV;
		}
		log_info(ratbag,
			 "device is at %d dpi (variable between %d and %d).\n",
			 drv_data->sensors[0].dpi,
			 drv_data->sensors[0].dpi_min,
			 drv_data->sensors[0].dpi_max);
		drv_data->num_sensors = rc;
		if (drv_data->num_sensors > MAX_RESOLUTIONS)
			drv_data->num_sensors = MAX_RESOLUTIONS;
		profile->resolution.num_modes = drv_data->num_sensors;
		for (i = 0; i < profile->resolution.num_modes; i++) {
			int dpi = drv_data->sensors[i].dpi;
			/* FIXME: retrieve the refresh rate */
			res = ratbag_resolution_init(profile, i, dpi, dpi, 0);

			/* FIXME: we mark all resolutions as active because
			 * they are from different sensors */
			res->is_active = true;
		}

		return 0;
	}

	return 0;
}

static int
hidpp20drv_write_resolution_dpi(struct ratbag_resolution *resolution,
				int dpi_x, int dpi_y)
{
	struct ratbag_profile *profile = resolution->profile;
	struct ratbag_device *device = profile->device;
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct hidpp20_sensor *sensor;
	int rc, i;
	int dpi = dpi_x; /* dpi_x == dpi_y if we don't have the individual resolution cap */

	if (!(drv_data->capabilities & HIDPP_CAP_SWITCHABLE_RESOLUTION_2201))
		return -ENOTSUP;

	if (!drv_data->num_sensors)
		return -ENOTSUP;

	/* just for clarity, we use the first available sensor only */
	sensor = &drv_data->sensors[0];

	/* validate that the sensor accepts the given DPI */
	rc = -EINVAL;
	if (dpi < sensor->dpi_min || dpi > sensor->dpi_max)
		goto out;
	if (sensor->dpi_steps) {
		for (i = sensor->dpi_min; i < dpi; i += sensor->dpi_steps) {
		}
		if (i != dpi)
			goto out;
	} else {
		i = 0;
		while (sensor->dpi_list[i]) {
			if (sensor->dpi_list[i] == dpi)
				break;
		}
		if (sensor->dpi_list[i] != dpi)
			goto out;
	}

	rc = hidpp20_adjustable_dpi_set_sensor_dpi(device, sensor, dpi);

out:
	return rc;
}

static int
hidpp20drv_read_special_key_mouse(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_BUTTON_KEY_1b04))
		return 0;

	free(drv_data->controls);
	drv_data->controls = NULL;
	drv_data->num_controls = 0;
	rc = hidpp20_special_key_mouse_get_controls(device, &drv_data->controls);
	if (rc > 0) {
		drv_data->num_controls = rc;
		rc = 0;
	}

	return rc;
}

static int
hidpp20drv_read_kbd_reprogrammable_key(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	int rc;

	if (!(drv_data->capabilities & HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00))
		return 0;

	free(drv_data->controls);
	drv_data->controls = NULL;
	drv_data->num_controls = 0;
	rc = hidpp20_kbd_reprogrammable_keys_get_controls(device, &drv_data->controls);
	if (rc > 0) {
		drv_data->num_controls = rc;
		rc = 0;
	}

	return rc;
}

static void
hidpp20drv_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;

	hidpp20drv_read_resolution_dpi(profile);
	hidpp20drv_read_special_key_mouse(device);

	profile->is_active = false;
	if ((int)index == hidpp20drv_current_profile(device))
		profile->is_active = true;
}

static int
hidpp20drv_write_profile(struct ratbag_profile *profile)
{
	return 0;
}

static int
hidpp20drv_init_feature(struct ratbag_device *device, uint16_t feature)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);
	struct ratbag *ratbag = device->ratbag;
	int rc;

	switch (feature) {
	case HIDPP_PAGE_ROOT:
	case HIDPP_PAGE_FEATURE_SET:
		/* these features are mandatory and already handled */
		break;
	case HIDPP_PAGE_MOUSE_POINTER_BASIC: {
		drv_data->capabilities |= HIDPP_CAP_RESOLUTION_2200;
		break;
	}
	case HIDPP_PAGE_ADJUSTABLE_DPI: {
		log_debug(ratbag, "device has adjustable dpi\n");
		drv_data->capabilities |= HIDPP_CAP_SWITCHABLE_RESOLUTION_2201;
		break;
	}
	case HIDPP_PAGE_SPECIAL_KEYS_BUTTONS: {
		log_debug(ratbag, "device has programmable keys/buttons\n");
		drv_data->capabilities |= HIDPP_CAP_BUTTON_KEY_1b04;
		/* we read the profile once to get the correct number of
		 * supported buttons. */
		if (!hidpp20drv_read_special_key_mouse(device))
			device->num_buttons = drv_data->num_controls;
		break;
	}
	case HIDPP_PAGE_BATTERY_LEVEL_STATUS: {
		uint16_t level, next_level;
		enum hidpp20_battery_status status;

		rc = hidpp20_batterylevel_get_battery_level(device, &level, &next_level);
		if (rc < 0)
			return rc;
		status = rc;

		log_debug(ratbag, "device battery level is %d%% (next %d%%), status %d \n",
			  level, next_level, status);

		drv_data->capabilities |= HIDPP_CAP_BATTERY_LEVEL_1000;
		break;
	}
	case HIDPP_PAGE_KBD_REPROGRAMMABLE_KEYS: {
		log_debug(ratbag, "device has programmable keys/buttons\n");
		drv_data->capabilities |= HIDPP_CAP_KBD_REPROGRAMMABLE_KEYS_1b00;

		/* we read the profile once to get the correct number of
		 * supported buttons. */
		if (!hidpp20drv_read_kbd_reprogrammable_key(device))
			device->num_buttons = drv_data->num_controls;
		break;
	}
	default:
		log_raw(device->ratbag, "unknown feature 0x%04x\n", feature);
	}
	return 0;
}

static int
hidpp20drv_20_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	struct hidpp20_feature *feature_list;
	int rc, i;

	rc = hidpp20_feature_set_get(device, &feature_list);
	if (rc < 0)
		return rc;

	if (rc > 0) {
		log_raw(device->ratbag, "'%s' has %d features\n", ratbag_device_get_name(device), rc);
		for (i = 0; i < rc; i++) {
			log_raw(device->ratbag, "Init feature %s (0x%04x) \n",
				hidpp20_feature_get_name(feature_list[i].feature),
				feature_list[i].feature);
			hidpp20drv_init_feature(device, feature_list[i].feature);
		}
	}

	free(feature_list);

	return 0;

}

static int
hidpp20drv_probe(struct ratbag_device *device, const struct ratbag_id id)
{
	int rc;
	struct hidpp20drv_data *drv_data;

	rc = ratbag_open_hidraw(device);
	if (rc) {
		log_error(device->ratbag,
			  "Can't open corresponding hidraw node: '%s' (%d)\n",
			  strerror(-rc),
			  rc);
		return -ENODEV;
	}

	drv_data = zalloc(sizeof(*drv_data));
	ratbag_set_drv_data(device, drv_data);

	drv_data->proto_major = 1;
	drv_data->proto_minor = 0;

	rc = hidpp20_root_get_protocol_version(device, &drv_data->proto_major, &drv_data->proto_minor);
	if (rc) {
		/* communication error, best to ignore the device */
		rc = -EINVAL;
		goto err;
	}

	log_debug(device->ratbag, "'%s' is using protocol v%d.%d\n", ratbag_device_get_name(device), drv_data->proto_major, drv_data->proto_minor);

	if (drv_data->proto_major >= 2) {
		rc = hidpp20drv_20_probe(device, id);
		if (rc)
			goto err;
	}

	ratbag_device_init_profiles(device, 1,
				    device->num_buttons ? device->num_buttons : 8);

	return rc;
err:
	free(drv_data);
	ratbag_set_drv_data(device, NULL);
	return rc;
}

static void
hidpp20drv_remove(struct ratbag_device *device)
{
	struct hidpp20drv_data *drv_data = ratbag_get_drv_data(device);

	free(drv_data->controls);
	free(drv_data->sensors);
	free(drv_data);
}

#define USB_VENDOR_ID_LOGITECH			0x046d
#define LOGITECH_DEVICE(_bus, _pid)		\
	{ .bustype = (_bus),			\
	  .vendor = USB_VENDOR_ID_LOGITECH,	\
	  .product = (_pid),			\
	  .version = VERSION_ANY }

static const struct ratbag_id hidpp20drv_table[] = {
	/* MX Master over unifying */
	{ .id = LOGITECH_DEVICE(BUS_USB, 0x4041),
	  .svg_filename = "logitech-mx_master.svg" },

	/* MX Master over bluetooth */
	{ .id = LOGITECH_DEVICE(BUS_BLUETOOTH, 0xb012),
	  .svg_filename = "logitech-mx_master.svg" },

	/* T650 over unifying */
	{ .id = LOGITECH_DEVICE(BUS_USB, 0x4101) },

	/* M325 over unifying */
	{ .id = LOGITECH_DEVICE(BUS_USB, 0x400a) },

	{ },
};

struct ratbag_driver hidpp20_driver = {
	.name = "Logitech HID++2.0",
	.table_ids = hidpp20drv_table,
	.probe = hidpp20drv_probe,
	.remove = hidpp20drv_remove,
	.read_profile = hidpp20drv_read_profile,
	.write_profile = hidpp20drv_write_profile,
	.set_active_profile = hidpp20drv_set_current_profile,
	.set_default_profile = hidpp20drv_set_default_profile,
	.has_capability = hidpp20drv_has_capability,
	.read_button = hidpp20drv_read_button,
	.write_button = hidpp20drv_write_button,
	.write_resolution_dpi = hidpp20drv_write_resolution_dpi,
};
