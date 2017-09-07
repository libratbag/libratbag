/*
 * Copyright 2017 Red Hat, Inc
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

#include "libratbag-private.h"

#include <gio/gio.h>

/* We hardcode the number of LEDs because this driver right now only works
 * with the DeathAdder Chroma. And detecting which LEDs are available
 * is possible only via trial and error or by doing an introspection on the
 * device object and parsing the XML to extract the interface names.
 * openrazer really needs to change the dbus interface to make this more
 * generic.
 *
 * See https://github.com/openrazer/openrazer/issues/381
 */
#define NLEDS 2

struct openrazer_led {
	double brightness;
	unsigned int effect;
};

struct openrazer {
	GDBusConnection *bus;
	GDBusProxy *proxy_dpi;
	GDBusProxy *proxy_misc;
	GDBusProxy *proxy_led_logo;
	GDBusProxy *proxy_led_scroll;

	unsigned int dpi_x, dpi_y;
	struct openrazer_led led_logo;
	struct openrazer_led led_scroll;
	unsigned int poll_rate;
};

/* Take the first element from the given GVariant and return it. This frees
 * the passed-in variant, i.e. use only as v = unpack(v). The returned value
 * must be released manually.
 */
static inline GVariant *
unpack(GVariant *v)
{
	GVariantIter iter;
	GVariant *var, *next;

	g_variant_iter_init(&iter, v);
	var = g_variant_iter_next_value(&iter);
	assert(var);

	next = g_variant_iter_next_value(&iter);
	assert(!next);

	/* We unpack the called variant to return the child so
	   it can be used as v = unpack(v);
	 */
	g_variant_unref(v);

	return var;
}

static void
openrazer_read_profile(struct ratbag_profile *profile, unsigned int index)
{
	struct ratbag_device *device = profile->device;
	struct openrazer *drv_data = ratbag_get_drv_data(device);
	struct ratbag_resolution *res;
	g_autoptr(GVariant) dpimax = NULL,
			    dpi = NULL,
			    dpi_x = NULL,
			    dpi_y = NULL,
			    rate = NULL;

	profile->is_enabled = true;
	profile->is_active = true;

	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_RESOLUTION);
	ratbag_device_set_capability(device, RATBAG_DEVICE_CAP_SWITCHABLE_RESOLUTION);

	/* razer.device.dpi.getDPI returns a tuple of 2 integers */
	dpi = g_dbus_proxy_call_sync(drv_data->proxy_dpi, "getDPI", NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     -1, NULL, NULL);
	if (!dpi)
		return;

	dpi = unpack(dpi);
	dpi_x = g_variant_get_child_value(dpi, 0);
	dpi_y = g_variant_get_child_value(dpi, 1);
	drv_data->dpi_x = g_variant_get_int32(dpi_x);
	drv_data->dpi_y = g_variant_get_int32(dpi_y);

	/* razer.device.misc.getPollRate returns single integer */
	rate = g_dbus_proxy_call_sync(drv_data->proxy_misc, "getPollRate", NULL,
				      G_DBUS_CALL_FLAGS_NONE,
				      -1, NULL, NULL);
	if (!rate)
		return;

	rate = unpack(rate);
	drv_data->poll_rate = g_variant_get_int32(rate);


	res = ratbag_resolution_init(profile, 0,
				     drv_data->dpi_x,
				     drv_data->dpi_y,
				     drv_data->poll_rate);
	res->is_active = true;
	res->is_default = true;

	/* razer.device.dpi.maxDPI returns single integer */
	dpimax = g_dbus_proxy_call_sync(drv_data->proxy_dpi, "maxDPI", NULL,
					G_DBUS_CALL_FLAGS_NONE,
					-1, NULL, NULL);
	if (!dpimax)
		return;

	dpimax = unpack(dpimax);
	res->dpi_max = g_variant_get_int32(dpimax);
	res->dpi_min = 0;
}

static void
read_led(struct openrazer_led *led,
	 GDBusProxy *proxy,
	 const char *prefix)
{
	g_autoptr(GVariant) brightness = NULL,
			    effect = NULL,
			    state = NULL;
	char method[128];

	snprintf(method, sizeof(method), "get%sBrightness", prefix);
	brightness = g_dbus_proxy_call_sync(proxy, method,
					    NULL, G_DBUS_CALL_FLAGS_NONE,
					    -1, NULL, NULL);
	brightness = unpack(brightness);
	led->brightness = g_variant_get_double(brightness);

	snprintf(method, sizeof(method), "get%sEffect", prefix);
	effect = g_dbus_proxy_call_sync(proxy, method,
					NULL, G_DBUS_CALL_FLAGS_NONE,
					-1, NULL, NULL);
	effect = unpack(effect);
	led->effect = g_variant_get_byte(effect);

	/* We cannot read the colors, only set them */
}

static void
openrazer_read_led(struct ratbag_led *led)
{
	struct ratbag_device *device = led->profile->device;
	struct openrazer *drv_data = ratbag_get_drv_data(device);
	struct openrazer_led *drvled;
	GDBusProxy *proxy;
	const char *prefix;

	if (led->index == 0) {
		drvled = &drv_data->led_logo;
		proxy = drv_data->proxy_led_logo;
		prefix = "Logo";
		led->type = RATBAG_LED_TYPE_LOGO;
	} else {
		drvled = &drv_data->led_scroll;
		proxy = drv_data->proxy_led_scroll;
		prefix = "Scroll";
		led->type = RATBAG_LED_TYPE_SCROLL_WHEEL;
	}

	read_led(drvled, proxy, prefix);

	switch (drvled->effect) {
	case 0: /* LED_STATIC */
		led->mode = RATBAG_LED_ON;
		break;
	case 1: /* LED_BLINKING */
	case 2: /* LED_PULSATING */
		led->mode = RATBAG_LED_BREATHING;
		break;
	case 4: /* LED_SPECTRUM_CYCLING */
		led->mode = RATBAG_LED_CYCLE;
		break;
	default:
		led->mode = RATBAG_LED_OFF;
		break;
	}

	led->color.red = 255;
	led->color.green = 255;
	led->color.blue = 255;
	led->brightness = drvled->brightness * 255.0/100.0;
}

static inline char *
dbus_get_daemon_version(GDBusConnection *bus)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;
	const char *version;

	proxy = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_NONE, NULL,
				      "org.razer",
				      "/org/razer",
				      "razer.daemon",
				      NULL, NULL);
	if (!proxy)
		return false;

	val = g_dbus_proxy_call_sync(proxy, "version", NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     -1, NULL, NULL);
	if (!val)
		return false;

	val = unpack(val);
	version = g_variant_get_string(val, 0);

	return strdup_safe(version);
}

static inline char **
dbus_get_serials(GDBusConnection *bus)
{
	g_autoptr(GDBusProxy) proxy = NULL;
	g_autoptr(GVariant) val = NULL;

	proxy = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_NONE,
				      NULL,
				      "org.razer",
				      "/org/razer",
				      "razer.devices",
				      NULL, NULL);
	if (!proxy)
		return false;

	val = g_dbus_proxy_call_sync(proxy, "getDevices", NULL,
				     G_DBUS_CALL_FLAGS_NONE,
				     -1, NULL, NULL);
	if (!val)
		return false;

	val = unpack(val);

	return g_variant_dup_strv(val, NULL);
}

static bool
match_device_to_serial(struct ratbag_device *device,
		       const char *serial)
{
	struct udev_device *d = device->udev_device;
	const char *driver, *subsystem;
	const char *attr;

	do {
		d = udev_device_get_parent(d);
		if (!d)
			break;
		subsystem = udev_device_get_subsystem(d);
	} while (!streq(subsystem, "hid"));

	if (!d) {
		log_error(device->ratbag,
			  "openrazer: Unable to find HID parent device for %s\n",
			  udev_device_get_sysname(device->udev_device));
		return false;
	}

	driver = udev_device_get_driver(d);
	if (!streq(driver, "razermouse")) {
		log_error(device->ratbag, "openrazer: Invalid kernel driver: %s\n", driver);
		return false;
	}

	attr = udev_device_get_sysattr_value(d, "device_serial");
	if (!attr)
		return false;

	return streq(attr, serial);
}

static bool
dbus_init_proxies(struct openrazer *drv_data,
		  GDBusConnection *bus,
		  const char *object_path)
{
	g_autoptr(GDBusProxy) proxy_dpi = NULL;
	g_autoptr(GDBusProxy) proxy_misc = NULL;
	g_autoptr(GDBusProxy) proxy_led_logo = NULL;
	g_autoptr(GDBusProxy) proxy_led_scroll = NULL;

	proxy_dpi = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  "org.razer",
					  object_path,
					  "razer.device.dpi",
					  NULL, NULL);
	if (!proxy_dpi)
		return false;

	proxy_misc = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_NONE,
					  NULL,
					  "org.razer",
					  object_path,
					  "razer.device.misc",
					  NULL, NULL);
	if (!proxy_misc)
		return false;

	/* See comment for NLEDS */
	proxy_led_logo = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.razer",
					       object_path,
					       "razer.device.lighting.logo",
					       NULL, NULL);
	if (!proxy_led_logo)
		return false;

	/* See comment for NLEDS */
	proxy_led_scroll = g_dbus_proxy_new_sync(bus, G_DBUS_PROXY_FLAGS_NONE,
					       NULL,
					       "org.razer",
					       object_path,
					       "razer.device.lighting.scroll",
					       NULL, NULL);
	if (!proxy_led_scroll)
		return false;

	drv_data->proxy_dpi = g_steal_pointer(&proxy_dpi);
	drv_data->proxy_misc = g_steal_pointer(&proxy_misc);
	drv_data->proxy_led_logo = g_steal_pointer(&proxy_led_logo);
	drv_data->proxy_led_scroll = g_steal_pointer(&proxy_led_scroll);

	return true;
}

static bool
init_dbus(struct openrazer *drv_data, struct ratbag_device *device)
{
	g_autoptr(GDBusConnection) bus = NULL;
	g_auto(GStrv) serials = NULL;
	gchar **serial;
	char *version;
	g_autofree gchar *matched_serial = NULL;
	char object_path[128];

	/* Ideally we should set up a watch for the bus name here
	   so that we can react to the openrazer daemon being started after
	   us. but libratbag would need an async probe function for that.
	 */
	bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (!bus)
		return false;

	version = dbus_get_daemon_version(bus);
	if (!version)
		return false;
	log_debug(device->ratbag, "openrazer: DBus daemon version: %s\n",
		  version);

	serials = dbus_get_serials(bus);
	serial = serials;
	while (*serial) {
		if (match_device_to_serial(device, *serial)) {
			log_debug(device->ratbag,
				  "openrazer: found match for serial %s\n", *serial);
			matched_serial = strdup_safe(*serial);
			break;
		}
		serial++;
	}

	if (!matched_serial) {
		log_error(device->ratbag,
			  "openrazer: Unable to match the device serials and system devices.\n");
		return false;
	}

	/* Yep, the object path is API */
	snprintf(object_path, sizeof(object_path),
		 "/org/razer/device/%s", matched_serial);

	if (!dbus_init_proxies(drv_data, bus, object_path))
		return false;

	drv_data->bus = g_steal_pointer(&bus);

	return true;
}

static int
openrazer_probe(struct ratbag_device *device)
{
	struct openrazer *drv_data = NULL;

	drv_data = zalloc(sizeof(*drv_data));

	if (!init_dbus(drv_data, device)) {
		log_error(device->ratbag,
			  "Failed to init dbus connection\n");
		return -1;
	}

	ratbag_set_drv_data(device, drv_data);
	ratbag_device_init_profiles(device, 1, 1, 3, NLEDS);

	return 0;
}

static void
openrazer_remove(struct ratbag_device *device)
{
	struct openrazer *drv_data = ratbag_get_drv_data(device);

	g_object_unref(G_OBJECT(drv_data->bus));
	g_object_unref(G_OBJECT(drv_data->proxy_dpi));
	g_object_unref(G_OBJECT(drv_data->proxy_misc));
	g_object_unref(G_OBJECT(drv_data->proxy_led_logo));
	g_object_unref(G_OBJECT(drv_data->proxy_led_scroll));

	free(drv_data);
}

/* Openrazer's dbus daemon has the LED type encoded in the interface *and*
 * in the method name, e.g. razer.device.lighting.scroll.setScrollBrightness
 * vs. razer.device.lighting.logo.setLogoBrightness. This appears to be
 * because of some limitations of python-dbus.
 */
static void
set_led(struct ratbag_led *led, struct openrazer_led *drvled,
	GDBusProxy *proxy, const char *prefix)
{
	char method[128];
	char *rgb_setter;

	snprintf(method, sizeof(method), "set%sBrightness", prefix);
	g_dbus_proxy_call_sync(proxy, method,
			       g_variant_new("(d)",
					     led->brightness * 100.0/255.0),
			       G_DBUS_CALL_FLAGS_NONE,
			       -1, NULL, NULL);

	if (led->mode == RATBAG_LED_OFF) {
		snprintf(method, sizeof(method), "set%sActive", prefix);
		g_dbus_proxy_call_sync(proxy, method,
				       g_variant_new("(b)", false),
				       G_DBUS_CALL_FLAGS_NONE,
				       -1, NULL, NULL);
		return;
	}

	switch(led->mode) {
	case RATBAG_LED_OFF:
		abort();
		break;
	case RATBAG_LED_ON:
		rgb_setter = "Static";
		break;
	case RATBAG_LED_CYCLE:
		rgb_setter = "Spectrum";
		break;
	case RATBAG_LED_BREATHING:
		rgb_setter = "Pulsate";
		break;
	}

	snprintf(method, sizeof(method), "set%s%s", prefix, rgb_setter);
	g_dbus_proxy_call_sync(proxy, method,
			       g_variant_new("(yyy)",
					     led->color.red,
					     led->color.green,
					     led->color.blue),
			       G_DBUS_CALL_FLAGS_NONE,
			       -1, NULL, NULL);
}

static int
openrazer_commit(struct ratbag_device *device)
{
	struct openrazer *drv_data = ratbag_get_drv_data(device);
	struct ratbag_profile *profile;
	struct ratbag_led *led;
	struct ratbag_resolution *resolution;

	list_for_each(profile, &device->profiles, link) {
		if (!profile->dirty)
			continue;

		for (size_t i = 0; i < profile->resolution.num_modes; i++) {
			resolution = &profile->resolution.modes[i];
			g_dbus_proxy_call_sync(drv_data->proxy_dpi,
					       "setDPI",
					       g_variant_new("(nn)",
							     resolution->dpi_x,
							     resolution->dpi_y),
					       G_DBUS_CALL_FLAGS_NONE,
					       -1, NULL, NULL);

			g_dbus_proxy_call_sync(drv_data->proxy_misc,
					       "setPollRate",
					       g_variant_new("(n)",
							     resolution->hz),
					       G_DBUS_CALL_FLAGS_NONE,
					       -1, NULL, NULL);

			drv_data->dpi_x = resolution->dpi_x;
			drv_data->dpi_y = resolution->dpi_y;
			drv_data->poll_rate = resolution->hz;
		}

		list_for_each(led, &profile->leds, link) {
			struct openrazer_led *drvled;
			GDBusProxy *proxy;
			const char *prefix;

			if (!led->dirty)
				continue;

			if (led->index == 0) {
				drvled = &drv_data->led_logo;
				proxy = drv_data->proxy_led_logo;
				prefix = "Logo";
				led->type = RATBAG_LED_TYPE_LOGO;
			} else {
				drvled = &drv_data->led_scroll;
				proxy = drv_data->proxy_led_scroll;
				prefix = "Scroll";
				led->type = RATBAG_LED_TYPE_SCROLL_WHEEL;
			}

			set_led(led, drvled, proxy, prefix);
		}
	}

	return RATBAG_SUCCESS;
}

struct ratbag_driver openrazer_driver = {
	.name = "OpenRazer DBus bridge",
	.id = "openrazer",
	.probe = openrazer_probe,
	.remove = openrazer_remove,
	.read_profile = openrazer_read_profile,
	.read_led = openrazer_read_led,
	.commit = openrazer_commit,
};
