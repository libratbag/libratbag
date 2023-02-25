/*
 * Copyright © 2019 Red Hat, Inc.
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

/* JSON parser for ratbag test devices. The JSON format is not fixed and may
   change at any time, but basically looks like this:

   {
     "profiles": [
       {
         "is_active": bool,
         "is_default": bool,
	 "is_disabled": bool,
	 "rate": int,
	 "report_rates": [ int, ...],
	 "capabilities": [int, ...] # profile capabilities
	 "resolutions": [
	   {
	     "xres": int,
	     "yres": int,
	     "dpi_min", int,
	     "dpi_max", int,
	     "is_active": bool,
	     "is_default": bool,
	     "is_disabled": bool,
	     "capabilities": [int, ...] # resolution capabilities
	   }
	 ]
	 "buttons": [
		 "action_type": "<enum>" # none, button, key, special, macro, unknown
		 "button": int, # if button
		 "key": int, # if key
		 "special": "<enum>", # if special, for values see see special_lookup()
		 "macro": ["+B", "-B", "t400"] # if macro. uppercase only

	 ]
	 "leds": [ ]
       },
       {
	 ... next profile ...
       }
     ]
   }

   ratbagd uses a minimum sane device (1 profile, 1 resolution, etc.) and any
   JSON instructions get merged into that device. Which means you usually
   only need to set those bits you care about being checked.
 */

#include "libratbag-util.h"
#include "ratbagd-json.h"
#include "ratbagd-test.h"
#include <json-glib/json-glib.h>
#include <libevdev/libevdev.h>

/* Hack because we don't do proper data passing */
static int num_resolutions;
static int num_buttons;
static int num_leds;

static int parse_error = 0;

#define parser_error(element_) { \
	log_error("json: parser error: %s:%d: element '%s'\n", __func__, __LINE__, (element_)); \
	parse_error = -EINVAL; \
	goto out; \
}

static void parse_resolution_member(JsonObject *obj, const gchar *name,
				    JsonNode *node, void *data)
{
	struct ratbag_test_resolution *resolution = data;

	if (streq(name, "xres")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 20000)
			parser_error("xres");

		resolution->xres = v;
		log_verbose("json:    xres: %d\n", v);
	} else if (streq(name, "yres")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 20000)
			parser_error("yres");

		resolution->yres = v;
		log_verbose("json:    yres: %d\n", v);
	} else if (streq(name, "dpi_min")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 20000)
			parser_error("dpi_min");

		resolution->dpi_min = v;
		log_verbose("json:    dpi_min: %d\n", v);
	} else if (streq(name, "dpi_max")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 20000)
			parser_error("dpi_min");

		resolution->dpi_max = v;
		log_verbose("json:    dpi_max: %d\n", v);
	} else if (streq(name, "is_active")) {
		gboolean v = json_object_get_boolean_member(obj, name);
		resolution->active = v;
		log_verbose("json:    is_active: %d\n", v);
	} else if (streq(name, "is_default")) {
		gboolean v = json_object_get_boolean_member(obj, name);
		resolution->dflt = v;
		log_verbose("json:    is_default: %d\n", v);
	} else if (streq(name, "is_disabled")) {
		gboolean v = json_object_get_boolean_member(obj, name);
		resolution->disabled = v;
		log_verbose("json:    is_disabled: %d\n", v);
	} else if (streq(name, "capabilities")) {
		JsonArray *a = json_object_get_array_member(obj, name);

		assert(json_array_get_length(a) < ARRAY_LENGTH(resolution->caps));
		for (size_t s = 0; s < json_array_get_length(a); s++) {
			int v = json_array_get_int_element(a, s);
			if (v > RATBAG_RESOLUTION_CAP_DISABLE)
				parser_error("capabilities");
			resolution->caps[s] = v;
		}

		log_verbose("json:    caps: %d %d %d %d %d...\n",
			    resolution->caps[0],
			    resolution->caps[1],
			    resolution->caps[2],
			    resolution->caps[3],
			    resolution->caps[4]);
	} else {
		log_error("json:    unknown resolution key '%s'\n", name);
		parse_error = -EINVAL;
	}
out:
	return;
}

static void parse_resolution(JsonNode *node,
			     struct ratbag_test_resolution *resolution)
{
	JsonObject *obj = json_node_get_object(node);

	json_object_foreach_member(obj, parse_resolution_member, resolution);
}

static void parse_led_member(JsonObject *obj, const gchar *name,
			     JsonNode *node, void *data)
{
	struct ratbag_test_led *led = data;

	if (streq(name, "mode")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > RATBAG_LED_BREATHING)
			parser_error("mode");

		led->mode = v;
		log_verbose("json:    mode: %d\n", v);
	} else if (streq(name, "duration")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 10000)
			parser_error("duration");

		led->ms = v;
		log_verbose("json:    duration: %d\n", v);
	} else if (streq(name, "brightness")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 100)
			parser_error("brightness");

		led->brightness = v;
		log_verbose("json:    brightness: %d\n", v);
	} else if (streq(name, "color")) {
		JsonArray *a = json_object_get_array_member(obj, name);

		assert(json_array_get_length(a) == 3);
		led->color.red   = json_array_get_int_element(a, 0);
		led->color.green = json_array_get_int_element(a, 1);
		led->color.blue  = json_array_get_int_element(a, 2);
		log_verbose("json:    color: %02x%02x%02x\n",
			    led->color.red,
			    led->color.green,
			    led->color.blue);
	} else {
		log_error("json:    unknown led key '%s'\n", name);
		parse_error = -EINVAL;
	}
out:
	return;
}

static void parse_led(JsonNode *node, struct ratbag_test_led *led)
{
	JsonObject *obj = json_node_get_object(node);

	json_object_foreach_member(obj, parse_led_member, led);
}

static enum ratbag_button_action_special
special_lookup(const char *string)
{
	const struct {
		const char *key;
		enum ratbag_button_action_special special;
	} lut[] = {
		{ "invalid", RATBAG_BUTTON_ACTION_SPECIAL_INVALID},
		{ "unknown", RATBAG_BUTTON_ACTION_SPECIAL_UNKNOWN},
		{ "doubleclick", RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK},
		{ "wheel-left", RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT},
		{ "wheel-right", RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT},
		{ "wheel-up", RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP},
		{ "wheel-down", RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN},
		{ "ratchet-mode-switch", RATBAG_BUTTON_ACTION_SPECIAL_RATCHET_MODE_SWITCH},
		{ "resolution-cycle-up", RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP},
		{ "resolution-cycle-down", RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN},
		{ "resolution-up", RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP},
		{ "resolution-down", RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN},
		{ "resolution-alternate", RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_ALTERNATE},
		{ "resolution-default", RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DEFAULT},
		{ "profile-cycle-up", RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP},
		{ "profile-cycle-down", RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_DOWN},
		{ "profile-up", RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_UP},
		{ "profile-down", RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_DOWN},
		{ "second-mode", RATBAG_BUTTON_ACTION_SPECIAL_SECOND_MODE},
		{ "battery-level", RATBAG_BUTTON_ACTION_SPECIAL_BATTERY_LEVEL},
	};

	for (size_t i = 0; i < ARRAY_LENGTH(lut); i++) {
		if (streq(string, lut[i].key))
			return lut[i].special;
	}

	parser_error("special");

out:
	return RATBAG_BUTTON_ACTION_SPECIAL_INVALID;
}

static enum ratbag_button_action_type
action_type_lookup(const char *string)
{
	const struct {
		const char *key;
		enum ratbag_button_action_type type;
	} lut[] = {
		{ "none", RATBAG_BUTTON_ACTION_TYPE_NONE},
		{ "button", RATBAG_BUTTON_ACTION_TYPE_BUTTON},
		{ "special", RATBAG_BUTTON_ACTION_TYPE_SPECIAL},
		{ "key", RATBAG_BUTTON_ACTION_TYPE_KEY},
		{ "macro", RATBAG_BUTTON_ACTION_TYPE_MACRO},
		{ "unknown", RATBAG_BUTTON_ACTION_TYPE_UNKNOWN},
	};

	for (size_t i = 0; i < ARRAY_LENGTH(lut); i++) {
		if (streq(string, lut[i].key))
			return lut[i].type;
	}

	parser_error("action_type");

out:
	return RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;
}

static inline struct ratbag_test_macro_event
parse_macro(const char *m)
{
	struct ratbag_test_macro_event event = {
		.type = RATBAG_MACRO_EVENT_INVALID,
		.value = 0,
	};
	char keyname[32];

	if (strlen(m) < 2)
		goto out;

	switch(m[0]) {
	case '+':
		event.type = RATBAG_MACRO_EVENT_KEY_PRESSED;
		snprintf(keyname, sizeof(keyname), "KEY_%s", m + 1);
		event.value = libevdev_event_code_from_name(EV_KEY,
							    keyname);
		log_verbose("json:     macro: +%s\n", keyname);
		break;
	case '-':
		event.type = RATBAG_MACRO_EVENT_KEY_RELEASED;
		snprintf(keyname, sizeof(keyname), "KEY_%s", m + 1);
		event.value = libevdev_event_code_from_name(EV_KEY,
							    keyname);
		log_verbose("json:     macro: -%s\n", keyname);
		break;
	case 't':
		event.type = RATBAG_MACRO_EVENT_WAIT;
		event.value = atoi(m+1);
		log_verbose("json:     macro: t%d\n", event.value);
		break;
	default:
		parser_error("macro");
		break;
	}

out:
	return event;
}


static void parse_button_member(JsonObject *obj, const gchar *name,
				JsonNode *node, void *data)
{
	struct ratbag_test_button *button = data;

	if (streq(name, "action_type")) {
		const gchar* v = json_object_get_string_member(obj, name);
		button->action_type = action_type_lookup(v);
		log_verbose("json:    action_type: %s\n", v);
	} else if (streq(name, "button")) {
		gint v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 32)
			parser_error("button");

		button->button = v;
		log_verbose("json:    button: %d\n", v);
	} else if (streq(name, "key")) {
		gint v = json_object_get_int_member(obj, name);

		if (v < 0 || v > KEY_MAX)
			parser_error("key");

		button->key = v;
		log_verbose("json:    key: %d\n", v);
	} else if (streq(name, "special")) {
		const gchar *v = json_object_get_string_member(obj, name);
		button->special = special_lookup(v);
		log_verbose("json:    special: %s\n", v);
	} else if (streq(name, "macro")) {
		JsonArray *a = json_object_get_array_member(obj, name);

		assert(json_array_get_length(a) < ARRAY_LENGTH(button->macro));
		for (size_t s = 0; s < json_array_get_length(a); s++) {
			const gchar *v = json_array_get_string_element(a, s);
			button->macro[s] = parse_macro(v);
		}
	} else {
		log_error("json: unknown button key '%s'\n", name);
		parse_error = -EINVAL;
	}
out:
	return;
}

static void parse_button(JsonNode *node, struct ratbag_test_button *button)
{
	JsonObject *obj = json_node_get_object(node);

	json_object_foreach_member(obj, parse_button_member, button);
}

static void parse_profile_member(JsonObject *obj, const gchar *name,
				 JsonNode *node, void *data)
{
	struct ratbag_test_profile *profile = data;

	if (streq(name, "name")) {
		const char *v = json_object_get_string_member(obj, name);

		if (v == NULL)
			parser_error("name");

		free(profile->name);
		profile->name = strdup_safe(v);
		log_verbose("name: %s\n", v);
	} else if (streq(name, "is_default")) {
		gboolean v = json_object_get_boolean_member(obj, name);
		profile->dflt = v;
		log_verbose("json:  is_default: %d\n", v);
	} else if (streq(name, "is_active")) {
		gboolean v = json_object_get_boolean_member(obj, name);
		profile->active = v;
		log_verbose("json:  is_active: %d\n", v);
	} else if (streq(name, "is_disabled")) {
		gboolean v = json_object_get_boolean_member(obj, name);
		profile->disabled = v;
		log_verbose("json:  is_disabled: %d\n", v);
	} else if (streq(name, "rate")) {
		gboolean v = json_object_get_int_member(obj, name);

		if (v < 0 || v > 20000)
			parser_error("rate");

		profile->hz = v;
		log_verbose("json:  rate: %d\n", v);
	} else if (streq(name, "report_rates")) {
		JsonArray *a = json_object_get_array_member(obj, name);
		assert(json_array_get_length(a) < ARRAY_LENGTH(profile->report_rates));
		for (size_t s = 0; s < json_array_get_length(a); s++) {
			int v = json_array_get_int_element(a, s);
			if (v < 0 || v > 20000)
				parser_error("report_rate");
			profile->report_rates[s] = v;
		}

		log_verbose("json:  report rates: %d %d %d %d %d\n",
			    profile->report_rates[0],
			    profile->report_rates[1],
			    profile->report_rates[2],
			    profile->report_rates[3],
			    profile->report_rates[4]);
	} else if (streq(name, "capabilities")) {
		JsonArray *a = json_object_get_array_member(obj, name);

		assert(json_array_get_length(a) < ARRAY_LENGTH(profile->caps));
		for (size_t s = 0; s < json_array_get_length(a); s++) {
			int v = json_array_get_int_element(a, s);
			profile->caps[s] = v;
		}

		log_verbose("json:  caps: %d %d %d %d %d...\n",
			    profile->caps[0],
			    profile->caps[1],
			    profile->caps[2],
			    profile->caps[3],
			    profile->caps[4]);
	} else if (streq(name, "resolutions")) {
		JsonArray *a = json_object_get_array_member(obj, name);
		GList *list = json_array_get_elements(a);
		GList *l = list;
		int idx = 0;
		while (l != NULL) {
			log_verbose("json:  processing resolution %d\n", idx);
			parse_resolution(l->data, &profile->resolutions[idx]);
			l = g_list_next(l);
			idx++;
		}
		g_list_free(list);
		num_resolutions = max(idx, num_resolutions);
	} else if (streq(name, "leds")) {
		JsonArray *a = json_object_get_array_member(obj, name);
		GList *list = json_array_get_elements(a);
		GList *l = list;
		int idx = 0;
		while (l != NULL) {
			log_verbose("json:  processing LED %d\n", idx);
			parse_led(l->data, &profile->leds[idx]);
			l = g_list_next(l);
			idx++;
		}
		g_list_free(list);
		num_leds = max(idx, num_leds);
	} else if (streq(name, "buttons")) {
		JsonArray *a = json_object_get_array_member(obj, name);
		GList *list = json_array_get_elements(a);
		GList *l = list;
		int idx = 0;
		while (l != NULL) {
			log_verbose("json:  processing button %d\n", idx);
			parse_button(l->data, &profile->buttons[idx]);
			l = g_list_next(l);
			idx++;
		}
		g_list_free(list);
		num_buttons = max(idx, num_buttons);
	} else {
		log_error("json: unknown profile key '%s'\n", name);
		parse_error = -EINVAL;
	}
out:
	return;
}

static void parse_profile(JsonNode *node,
			 struct ratbag_test_profile *profile)
{
	JsonObject *obj = json_node_get_object(node);

	json_object_foreach_member(obj, parse_profile_member, profile);
}

/* declared here because this isn't really public API, we just need to
 * access it from the tests */
int ratbagd_parse_json(const char *data, struct ratbag_test_device *device)
{
	g_autoptr(JsonParser) parser = NULL;
	JsonNode *root;
	JsonObject *obj;
	JsonArray *arr;
	int r = -EINVAL;
	g_autoptr(GError) error = NULL;
	g_autoptr(GList) list = NULL;

	error = 0;
	parser = json_parser_new();
	if (!json_parser_load_from_data(parser, data, strlen(data), &error)) {
		log_error("Failed to load JSON: %s\n", error->message);
		r = -EINVAL;
		goto out;
	}

	log_verbose("json: data: %s\n", data);

	root = json_parser_get_root(parser);
	if (JSON_NODE_TYPE(root) != JSON_NODE_OBJECT)
		parser_error("root");

	obj = json_node_get_object(root);
	arr = json_object_get_array_member(obj, "profiles");
	if (!arr)
		parser_error("profiles");

	/* Our test device is preloaded with sane defaults, let's keep those */
	num_resolutions = device->num_resolutions;
	num_buttons = device->num_buttons;
	num_leds = device->num_leds;

	list = json_array_get_elements(arr);
	GList *l = list;
	int idx = 0;
	while (l != NULL) {
		log_verbose("json: processing profile %d\n", idx);
		parse_profile(l->data, &device->profiles[idx]);
		if (parse_error != 0)
			goto out;;
		l = g_list_next(l);
		idx++;
	}
	device->num_profiles = idx;
	device->num_resolutions = num_resolutions;
	device->num_buttons = num_buttons;
	device->num_leds = num_leds;

	r = 0;
out:
	return r;
}
