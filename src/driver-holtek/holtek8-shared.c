// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Michał Lubas
 */

#include <poll.h>
#include <linux/hidraw.h>
#include "holtek8-shared.h"
#include "libratbag-data.h"

#define HOLTEK8_CMD_ECHO 0x0
#define HOLTEK8_MAX_CHUNK_SIZE 64
#define HOLTEK8_POLL_TIME_MS 1
#define HOLTEK8_POLL_RETRY_LIMIT 10

#define HOLTEK8_MACRO_CMD_WAIT 0x01
#define HOLTEK8_MACRO_CMD_MOUSE 0xfa
#define HOLTEK8_MACRO_CMD_JUMP 0xfe

#define HOLTEK8A_CMD_WRITE_MACRO_DATA 0x13
#define HOLTEK8B_CMD_WRITE_MACRO_DATA 0x0f
#define HOLTEK8A_CMD_READ_MACRO_DATA 0x93
#define HOLTEK8B_CMD_READ_MACRO_DATA 0x8f
#define HOLTEK8A_MAX_MACRO_INDEX 9
#define HOLTEK8B_MAX_MACRO_INDEX 50
#define HOLTEK8A_MACRO_DELAY_MS 10
#define HOLTEK8B_MACRO_DELAY_MS 8


static const struct holtek8_sensor_config holtek8_sensor_configurations[] = {
	{ HOLTEK8_SENSOR_UNKNOWN, "", 200, 2000, 100, false, false }, //fallback
	{ HOLTEK8_SENSOR_PAW3333, "PAW3333", 200, 8000, 100, false, false },
	{ HOLTEK8_SENSOR_PMW3320, "PMW3320", 250, 3500, 250, false, false },
};

struct holtek8_button_mapping {
	struct holtek8_button_data data;
	struct ratbag_button_action action;
};

static const struct holtek8_button_mapping holtek8_button_map[] = {
	{ { HOLTEK8_BUTTON_TYPE_MOUSE, { .mouse.button = HOLTEK8_BUTTON_MOUSE_LEFT } },   BUTTON_ACTION_BUTTON(1) },
	{ { HOLTEK8_BUTTON_TYPE_MOUSE, { .mouse.button = HOLTEK8_BUTTON_MOUSE_RIGHT } },  BUTTON_ACTION_BUTTON(2) },
	{ { HOLTEK8_BUTTON_TYPE_MOUSE, { .mouse.button = HOLTEK8_BUTTON_MOUSE_MIDDLE } }, BUTTON_ACTION_BUTTON(3) },
	{ { HOLTEK8_BUTTON_TYPE_MOUSE, { .mouse.button = HOLTEK8_BUTTON_MOUSE_MB4 } },    BUTTON_ACTION_BUTTON(4) },
	{ { HOLTEK8_BUTTON_TYPE_MOUSE, { .mouse.button = HOLTEK8_BUTTON_MOUSE_MB5 } },    BUTTON_ACTION_BUTTON(5) },

	{ { HOLTEK8_BUTTON_TYPE_SCROLL, { .scroll.event = HOLTEK8_BUTTON_SCROLL_UP } },    BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_UP) },
	{ { HOLTEK8_BUTTON_TYPE_SCROLL, { .scroll.event = HOLTEK8_BUTTON_SCROLL_DOWN } },  BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_DOWN) },
	{ { HOLTEK8_BUTTON_TYPE_SCROLL, { .scroll.event = HOLTEK8_BUTTON_SCROLL_LEFT } },  BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_LEFT) },
	{ { HOLTEK8_BUTTON_TYPE_SCROLL, { .scroll.event = HOLTEK8_BUTTON_SCROLL_RIGHT } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_WHEEL_RIGHT) },

	{ { HOLTEK8_BUTTON_TYPE_DPI, { .dpi.event = HOLTEK8_BUTTON_DPI_UP } },    BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ { HOLTEK8_BUTTON_TYPE_DPI, { .dpi.event = HOLTEK8_BUTTON_DPI_DOWN } },  BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ { HOLTEK8_BUTTON_TYPE_DPI, { .dpi.event = HOLTEK8_BUTTON_DPI_CYCLE } }, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },

	{ { HOLTEK8_BUTTON_TYPE_MULTICLICK, { .multiclick.hid_key = HOLTEK8_BUTTON_MOUSE_LEFT, .multiclick.delay = 50, .multiclick.count = 2 } },
		BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_DOUBLECLICK) },

	{ { 0 }, BUTTON_ACTION_NONE },
	{ { 0x0c, { { 0 } } }, BUTTON_ACTION_BUTTON(1) },
};

struct holtek8_report_rate_mapping {
	uint8_t raw;
	unsigned int report_rate;
};

static const struct holtek8_report_rate_mapping holtek8_report_rate_map[] = {
	{ 0x1, 1000 },
	{ 0x2, 500 },
	{ 0x4, 250 },
	{ 0x8, 125 },
};

uint8_t
holtek8_report_rate_to_raw(unsigned int report_rate)
{
	const struct holtek8_report_rate_mapping *mapping = NULL;
	ARRAY_FOR_EACH(holtek8_report_rate_map, mapping)
		if (mapping->report_rate == report_rate)
			return mapping->raw;
	return 0;
}

unsigned int
holtek8_raw_to_report_rate(uint8_t raw)
{
	const struct holtek8_report_rate_mapping *mapping = NULL;
	ARRAY_FOR_EACH(holtek8_report_rate_map, mapping)
		if (mapping->raw == raw)
			return mapping->report_rate;
	return 0;
}

uint16_t
holtek8_dpi_to_raw(struct ratbag_device *device, unsigned int dpi)
{
	struct holtek8_data *drv_data = device->drv_data;
	const struct holtek8_sensor_config *sensor_cfg = drv_data->sensor_cfg;

	if (dpi < sensor_cfg->dpi_min)
		dpi = sensor_cfg->dpi_min;

	if (dpi > sensor_cfg->dpi_max)
		dpi = sensor_cfg->dpi_max;

	uint16_t raw = dpi / sensor_cfg->dpi_step;

	if (sensor_cfg->zero_indexed)
		raw -= 1;

	return raw;
}

unsigned int
holtek8_raw_to_dpi(struct ratbag_device *device, uint16_t raw)
{
	struct holtek8_data *drv_data = device->drv_data;
	const struct holtek8_sensor_config *sensor_cfg = drv_data->sensor_cfg;

	if (sensor_cfg->zero_indexed)
		raw += 1;

	return raw * sensor_cfg->dpi_step;
}

static bool
holtek8_button_data_is_equal(const struct holtek8_button_data *lhs, const struct holtek8_button_data *rhs)
{
	if (lhs->type != rhs->type)
		return false;

	for (unsigned int i = 0; i < sizeof(lhs->data); ++i) {
		if (lhs->data[i] != rhs->data[i]) {
			return false;
		}
	}

	return true;
}

static bool
holtek8_button_map_action_to_raw(const struct ratbag_button_action *action, struct holtek8_button_data *data)
{
	const struct holtek8_button_mapping *mapping = NULL;

	ARRAY_FOR_EACH(holtek8_button_map, mapping) {
		if (!ratbag_button_action_match(&mapping->action, action)) {
			continue;
		}

		*data = mapping->data;
		return true;
	}

	return false;
}

static const struct ratbag_button_action *
holtek8_button_map_raw_to_action(const struct holtek8_button_data *data)
{
	const struct holtek8_button_mapping *mapping = NULL;

	ARRAY_FOR_EACH(holtek8_button_map, mapping) {
		if (!holtek8_button_data_is_equal(data, &mapping->data))
			continue;

		return &mapping->action;
	}

	return NULL;
}

struct holtek8_modifier_mapping {
	enum holtek8_modifiers modifiers;
	unsigned int key;
};

static const struct holtek8_modifier_mapping holtek8_modifier_map[] = {
	{ HOLTEK8_MODIFIER_LEFTCTRL, KEY_LEFTCTRL },
	{ HOLTEK8_MODIFIER_LEFTSHIFT, KEY_LEFTSHIFT },
	{ HOLTEK8_MODIFIER_LEFTALT, KEY_LEFTALT },
	{ HOLTEK8_MODIFIER_LEFTMETA, KEY_LEFTMETA },
	{ HOLTEK8_MODIFIER_RIGHTCTRL, KEY_RIGHTCTRL },
	{ HOLTEK8_MODIFIER_RIGHTSHIFT, KEY_RIGHTSHIFT },
	{ HOLTEK8_MODIFIER_RIGHTALT, KEY_RIGHTALT },
	{ HOLTEK8_MODIFIER_RIGHTMETA, KEY_RIGHTMETA },
};

/*
 * Reads macro events from the device memory.
 *
 * Macros in memory are divided into pages. If the device supports jumping between
 * pages, tries to follow jumps to read multipaged macros.
 *
 * @param macro_events An array of macro events
 * @param macro_events_size Array length of macro_events
 * @param macro_idx An index in device of a macro to read
 * @return 0 on success or a negative errno.
 */
static int
holtek8_read_macro_data(struct ratbag_device *device, union holtek8_macro_event *macro_events, size_t macro_events_size, uint8_t macro_idx)
{
	int rc;
	struct holtek8_data *drv_data = device->drv_data;
	struct holtek8_feature_report report = {0, 0, {macro_idx}, 0};
	struct holtek8_macro_data macro_data;
	size_t data_i = 0, events_i = 0;
	bool single_page_macros = false;

	switch (drv_data->api_version) {
		case HOLTEK8_API_A:
			report.command = HOLTEK8A_CMD_READ_MACRO_DATA;
			single_page_macros = true;
			break;
		case HOLTEK8_API_B:
			report.command = HOLTEK8B_CMD_READ_MACRO_DATA;
			break;
	}

	rc = holtek8_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8_read_chunked(device, (uint8_t*) &macro_data, HOLTEK8_MACRO_DATA_SIZE, NULL);
	if (rc < 0)
		return rc;

	while (events_i < macro_events_size) {
		union holtek8_macro_event *ev = &macro_data.event[data_i];

		if (ev->argument == 0 && ev->command == 0) {
			return 0; // macro terminator reached
		}
		if (ev->command == HOLTEK8_MACRO_CMD_JUMP) {
			if (single_page_macros)
				return 0;

			report.arg[0] = ev->argument;
			rc = holtek8_set_feature_report(device, &report);
			if (rc < 0)
				return rc;

			rc = holtek8_read_chunked(device, (uint8_t*) &macro_data, HOLTEK8_MACRO_DATA_SIZE, NULL);
			if (rc < 0)
				return rc;

			data_i = 0;
		}
		else {
			macro_events[events_i] = *ev;
			events_i++; data_i++;

			if (data_i >= ARRAY_LENGTH(macro_data.event))
				return 0; // end of data reached
		}
	}

	return -EOVERFLOW;
}

/*
 * Writes macro events to the device memory.
 *
 * Macros in memory are divided into pages. If the device supports jumping between
 * pages, try to split events between multiple pages if they don't fit in one.
 *
 * @param macro_events An array of macro events
 * @param macro_events_size Array length of macro_events
 * @return An index of a first page on success or a negative errno.
 */
static int
holtek8_write_macro_data(struct ratbag_device *device, union holtek8_macro_event *macro_events, size_t macro_events_size)
{
	int rc;
	struct holtek8_data *drv_data = device->drv_data;
	struct holtek8_feature_report report = {0};
	struct holtek8_macro_data macro_data = { .repeat_count = {0,1} };
	size_t i, data_i = 0, events_i = 0;
	unsigned int max_macro_index;
	unsigned int events_to_write = 0;
	unsigned int free_pages, pages_to_write;
	uint8_t first_page = drv_data->macro_index;
	bool single_page_macros = false;

	switch (drv_data->api_version) {
		case HOLTEK8_API_A:
			report.command = HOLTEK8A_CMD_WRITE_MACRO_DATA;
			max_macro_index = HOLTEK8A_MAX_MACRO_INDEX;
			single_page_macros = true;
			break;
		case HOLTEK8_API_B:
			report.command = HOLTEK8B_CMD_WRITE_MACRO_DATA;
			max_macro_index = HOLTEK8B_MAX_MACRO_INDEX;
			break;
	}

	report.arg[1] = HOLTEK8_MACRO_DATA_SIZE;

	for (i = 0; i < macro_events_size; ++i) {
		union holtek8_macro_event *macro_event = &macro_events[i];
		if (macro_event->command == 0 && macro_event->argument == 0) {
			break;
		}
		events_to_write += 1;
	}

	if (events_to_write == 0 || events_to_write > HOLTEK8_MAX_MACRO_EVENTS)
		return -EINVAL;

	free_pages = max_macro_index - drv_data->macro_index + 1;
	pages_to_write = events_to_write / (ARRAY_LENGTH(macro_data.event) - 1);

	if (events_to_write % (ARRAY_LENGTH(macro_data.event) - 1) != 0)
		pages_to_write += 1;

	if (single_page_macros && pages_to_write > 1)
		return -ENOMEM;

	if (pages_to_write > free_pages)
		return -ENOMEM;

	while (events_i < events_to_write) {
		union holtek8_macro_event *ev = &macro_data.event[data_i];

		if (data_i == (ARRAY_LENGTH(macro_data.event) - 1)) {
			ev->argument = drv_data->macro_index + 1;
			ev->command = HOLTEK8_MACRO_CMD_JUMP;

			assert(drv_data->macro_index <= max_macro_index && drv_data->macro_index > 0);
			report.arg[0] = drv_data->macro_index++;

			rc = holtek8_set_feature_report(device, &report);
			if (rc < 0)
				return rc;

			rc = holtek8_write_chunked(device, (uint8_t*) &macro_data, HOLTEK8_MACRO_DATA_SIZE);
			if (rc < 0)
				return rc;

			data_i = 0;
			memset(&macro_data.event, 0, sizeof(macro_data.event));
		}
		else {
			*ev = macro_events[events_i];
			events_i++; data_i++;
		}
	}

	assert(drv_data->macro_index <= max_macro_index && drv_data->macro_index > 0);
	report.arg[0] = drv_data->macro_index++;

	rc = holtek8_set_feature_report(device, &report);
	if (rc < 0)
		return rc;

	rc = holtek8_write_chunked(device, (uint8_t*) &macro_data, HOLTEK8_MACRO_DATA_SIZE);
	if (rc < 0)
		return rc;

	return first_page;
}

/*
 * Converts at most two key codes and modifiers into a simple ratbag macro and writes to a button
 */
static void
holtek8_button_macro_new_from_keycodes(struct ratbag_button *button, unsigned int key1, unsigned int key2, unsigned int modifiers)
{
	const struct holtek8_modifier_mapping *mapping;
	struct ratbag_button_macro *macro = ratbag_button_macro_new("keys");
	int i = 0;

	ARRAY_FOR_EACH(holtek8_modifier_map, mapping) {
		if (modifiers & mapping->modifiers) {
			ratbag_button_macro_set_event(macro,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_PRESSED,
						      mapping->key);
		}
	}

	if (key1 != 0)
		ratbag_button_macro_set_event(macro,
					i++,
					RATBAG_MACRO_EVENT_KEY_PRESSED,
					key1);

	if (key2 != 0) {
		ratbag_button_macro_set_event(macro,
					i++,
					RATBAG_MACRO_EVENT_KEY_PRESSED,
					key2);
		ratbag_button_macro_set_event(macro,
					i++,
					RATBAG_MACRO_EVENT_KEY_RELEASED,
					key2);
	}

	if (key1 != 0)
		ratbag_button_macro_set_event(macro,
					i++,
					RATBAG_MACRO_EVENT_KEY_RELEASED,
					key1);

	ARRAY_FOR_EACH(holtek8_modifier_map, mapping) {
		if (modifiers & mapping->modifiers) {
			ratbag_button_macro_set_event(macro,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_RELEASED,
						      mapping->key);
		}
	}

	ratbag_button_copy_macro(button, macro);
	ratbag_button_macro_unref(macro);
}

/*
 * Converts a simple ratbag macro to a set of at most two key codes and modifiers
 *
 * @return Number of key codes on success or a negative errno, -EPROTO if ratbag macro is too complex
 */
static int
holtek8_keycodes_from_ratbag_macro(const struct ratbag_button_action *action, unsigned int *key1_out, unsigned int *key2_out, unsigned int *modifiers_out)
{
	const struct ratbag_macro *macro = action->macro;
	const unsigned int num_keys = (unsigned int)ratbag_action_macro_num_keys(action);
	unsigned int key1 = KEY_RESERVED;
	unsigned int key2 = KEY_RESERVED;
	unsigned int modifiers = 0;
	unsigned int i;
	unsigned int keys_pressed = 0;
	unsigned int mods_pressed = 0;
	unsigned int num_mods = 0;

	if (!macro || action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return -EINVAL;

	if (num_keys > 2)
		return -EPROTO;

	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		const struct ratbag_macro_event *event = &macro->events[i];
		if (event->type == RATBAG_MACRO_EVENT_NONE ||
		    event->type == RATBAG_MACRO_EVENT_INVALID) {
			break;
		}
		if (ratbag_key_is_modifier(event->event.key) && event->type == RATBAG_MACRO_EVENT_KEY_PRESSED) {
			num_mods += 1;
		}
	}

	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		const struct ratbag_macro_event *event = &macro->events[i];

		switch (event->type) {
			case RATBAG_MACRO_EVENT_INVALID:
				return -EINVAL;
			case RATBAG_MACRO_EVENT_KEY_PRESSED:
				switch (event->event.key) {
				case KEY_LEFTCTRL: modifiers |= HOLTEK8_MODIFIER_LEFTCTRL; mods_pressed++; break;
				case KEY_LEFTSHIFT: modifiers |= HOLTEK8_MODIFIER_LEFTSHIFT; mods_pressed++; break;
				case KEY_LEFTALT: modifiers |= HOLTEK8_MODIFIER_LEFTALT; mods_pressed++; break;
				case KEY_LEFTMETA: modifiers |= HOLTEK8_MODIFIER_LEFTMETA; mods_pressed++; break;
				case KEY_RIGHTCTRL: modifiers |= HOLTEK8_MODIFIER_RIGHTCTRL; mods_pressed++; break;
				case KEY_RIGHTSHIFT: modifiers |= HOLTEK8_MODIFIER_RIGHTSHIFT; mods_pressed++; break;
				case KEY_RIGHTALT: modifiers |= HOLTEK8_MODIFIER_RIGHTALT; mods_pressed++; break;
				case KEY_RIGHTMETA: modifiers |= HOLTEK8_MODIFIER_RIGHTMETA; mods_pressed++; break;
				default:
					if (key1 == KEY_RESERVED) {
						key1 = event->event.key;
					} else if (key2 == KEY_RESERVED) {
						key2 = event->event.key;
					} else {
						return -EPROTO;
					}
					keys_pressed++;
				}
				break;
			case RATBAG_MACRO_EVENT_NONE:
			case RATBAG_MACRO_EVENT_KEY_RELEASED:
				if (keys_pressed == num_keys && mods_pressed == num_mods) {
					*modifiers_out = modifiers;
					*key1_out = key1;
					*key2_out = key2;
					return (int)keys_pressed;
				}
				return -EPROTO;
			case RATBAG_MACRO_EVENT_WAIT:
				return -EPROTO;
			default:
				return -EINVAL;
		}
	}

	return -EINVAL;
}

/*
 * Converts raw macro events to ratbag macro and writes to a button
 *
 * @param macro_events An array of macro events
 * @param macro_events_size Array length of macro_events
 * @return 0 on success or a negative errno.
 */
static int
holtek8_macro_from_events(struct ratbag_button *button, const union holtek8_macro_event *macro_events, size_t macro_events_size)
{
	struct ratbag_device *device = button->profile->device;
	struct holtek8_data *drv_data = device->drv_data;
	struct ratbag_button_macro *macro = ratbag_button_macro_new("macro");
	int rc;
	size_t i, macro_i = 0;
	unsigned int delay = 0;
	unsigned int delay_base_ms;
	unsigned int key;

	switch (drv_data->api_version) {
		case HOLTEK8_API_A:
			delay_base_ms = HOLTEK8A_MACRO_DELAY_MS;
			break;
		case HOLTEK8_API_B:
			delay_base_ms = HOLTEK8B_MACRO_DELAY_MS;
			break;
	}

	button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;

	for (i = 0; i < macro_events_size; i++) {
		const union holtek8_macro_event *event = &macro_events[i];

		if (event->command == 0 && event->argument == 0)
			break;

		if (macro_i >= MAX_MACRO_EVENTS)
			goto overflow;

		if (event->command == HOLTEK8_MACRO_CMD_WAIT) {
			if (event->argument != 0) {
				rc = -EINVAL;
				goto err;
			}
			if (++i >= macro_events_size) {
				rc = -ENODATA;
				goto err;
			}

			delay += get_unaligned_be_u16(macro_events[i].data) * 2;
			continue;
		}
		if (event->command == HOLTEK8_MACRO_CMD_MOUSE) {
			//no support in ratbag for mouse movements in macros
			i += 1;
			continue;
		}

		if (delay > 1) {
			ratbag_button_macro_set_event(macro, macro_i++, RATBAG_MACRO_EVENT_WAIT, delay * delay_base_ms);
			if (macro_i >= MAX_MACRO_EVENTS)
				goto overflow;
		}
		delay = event->delay;

		switch (event->key) {
			case HOLTEK8_BUTTON_MOUSE_LEFT:   key = BTN_LEFT; break;
			case HOLTEK8_BUTTON_MOUSE_RIGHT:  key = BTN_RIGHT; break;
			case HOLTEK8_BUTTON_MOUSE_MIDDLE: key = BTN_MIDDLE; break;
			case HOLTEK8_BUTTON_MOUSE_MB4:    key = BTN_SIDE; break;
			case HOLTEK8_BUTTON_MOUSE_MB5:    key = BTN_EXTRA; break;
			default:
				key = ratbag_hidraw_get_keycode_from_keyboard_usage(device, event->key);
		}

		ratbag_button_macro_set_event(macro, macro_i++, event->release ? RATBAG_MACRO_EVENT_KEY_RELEASED : RATBAG_MACRO_EVENT_KEY_PRESSED, key);
	}

	ratbag_button_copy_macro(button, macro);
	ratbag_button_macro_unref(macro);
	return 0;

overflow:
	log_error(device->ratbag, "Can't fit device macro for button %u in ratbag macro\n", button->index);
	rc = -EOVERFLOW;
	ratbag_button_copy_macro(button, macro);
err:
	ratbag_button_macro_unref(macro);
	return rc;
}

/*
 * Reads ratbag macro from a button and converts to raw macro events
 *
 * @param macro_events An array of macro events
 * @param macro_events_size Array length of macro_events
 * @return Number of events on success or a negative errno.
 */
static int
holtek8_macro_to_events(const struct ratbag_button *button, union holtek8_macro_event *macro_events, size_t macro_events_size)
{
	const struct ratbag_device *device = button->profile->device;
	const struct holtek8_data *drv_data = device->drv_data;
	const struct ratbag_macro *macro = button->action.macro;
	size_t i, event_i = 0;
	unsigned int delay = 0;
	unsigned int delay_base_ms;
	unsigned int raw_delay;
	uint8_t key;

	switch (drv_data->api_version) {
		case HOLTEK8_API_A:
			delay_base_ms = HOLTEK8A_MACRO_DELAY_MS;
			break;
		case HOLTEK8_API_B:
			delay_base_ms = HOLTEK8B_MACRO_DELAY_MS;
			break;
	}

	if (!macro || button->action.type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return -EINVAL;

	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		const struct ratbag_macro_event *ratbag_ev = &macro->events[i];

		if (event_i+2 >= macro_events_size)
			return -EOVERFLOW;

		switch (ratbag_ev->type) {
			case RATBAG_MACRO_EVENT_INVALID:
				return -EINVAL;
			case RATBAG_MACRO_EVENT_KEY_PRESSED:
			case RATBAG_MACRO_EVENT_KEY_RELEASED:
				if (delay != 0 && event_i == 0) {
					raw_delay = delay / (2 * delay_base_ms);
					if (raw_delay == 0)
						raw_delay = 1;

					macro_events[event_i].command = HOLTEK8_MACRO_CMD_WAIT;
					macro_events[event_i++].argument = 0;
					set_unaligned_be_u16(macro_events[event_i++].data, raw_delay);
					delay = 0;
				}
				else if (delay != 0) {
					if (delay / delay_base_ms < 128) {
						raw_delay = delay / delay_base_ms;
						if (raw_delay == 0)
							raw_delay = 1;

						macro_events[event_i-1].delay = raw_delay;
					}
					else {
						raw_delay = delay / (2 * delay_base_ms);
						macro_events[event_i].command = HOLTEK8_MACRO_CMD_WAIT;
						macro_events[event_i++].argument = 0;
						set_unaligned_be_u16(macro_events[event_i++].data, raw_delay);
					}
					delay = 0;
				}

				macro_events[event_i].release = ratbag_ev->type == RATBAG_MACRO_EVENT_KEY_RELEASED;
				macro_events[event_i].delay = 1;

				switch (ratbag_ev->event.key) {
					case BTN_LEFT:   key = HOLTEK8_BUTTON_MOUSE_LEFT; break;
					case BTN_RIGHT:  key = HOLTEK8_BUTTON_MOUSE_RIGHT; break;
					case BTN_MIDDLE: key = HOLTEK8_BUTTON_MOUSE_MIDDLE; break;
					case BTN_SIDE:   key = HOLTEK8_BUTTON_MOUSE_MB4; break;
					case BTN_EXTRA:  key = HOLTEK8_BUTTON_MOUSE_MB5; break;
					default:
						key = ratbag_hidraw_get_keyboard_usage_from_keycode(device, ratbag_ev->event.key);
				}
				if (key == 0)
					return -EINVAL;

				macro_events[event_i].key = key;
				event_i += 1;
				break;
			case RATBAG_MACRO_EVENT_WAIT:
				delay += ratbag_ev->event.timeout;
				break;
			case RATBAG_MACRO_EVENT_NONE:
				return (int)event_i;
			default:
				return -EINVAL;
		}
	}

	return (int)event_i;
}

/*
 * Converts raw device button data and writes to ratbag button.
 * If the data->type is macro, reads a decoded macro from the device
 * memory and writes to ratbag button.
 *
 * @return 0 on success or a negative errno.
 */
int
holtek8_button_from_data(struct ratbag_button *button, const struct holtek8_button_data *data)
{
	struct ratbag_device *device = button->profile->device;
	const struct ratbag_button_action *action;
	int rc;

	action = holtek8_button_map_raw_to_action(data);

	if (action != NULL) {
		button->action = *action;
		return 0;
	}

	button->action.type = RATBAG_BUTTON_ACTION_TYPE_UNKNOWN;

	switch (data->type) {
		case HOLTEK8_BUTTON_TYPE_KEYBOARD: {
			const unsigned int modifiers = data->keyboard.modifiers;
			const unsigned int key1 = ratbag_hidraw_get_keycode_from_keyboard_usage(device, data->keyboard.hid_key);
			const unsigned int key2 = ratbag_hidraw_get_keycode_from_keyboard_usage(device, data->keyboard.hid_key2);

			holtek8_button_macro_new_from_keycodes(button, key1, key2, modifiers);
			break;
		}
		case HOLTEK8_BUTTON_TYPE_MEDIA: {
			const uint16_t hid_code_cc = get_unaligned_le_u16(data->media.hid_key);
			const unsigned int key = ratbag_hidraw_get_keycode_from_consumer_usage(device, hid_code_cc);

			holtek8_button_macro_new_from_keycodes(button, key, 0, 0);
			break;
		}
		case HOLTEK8_BUTTON_TYPE_MACRO: {
			union holtek8_macro_event macro_events[HOLTEK8_MAX_MACRO_EVENTS] = {0};

			rc = holtek8_read_macro_data(device, macro_events, HOLTEK8_MAX_MACRO_EVENTS, data->macro.index);
			if (rc == -EOVERFLOW)
				return 0;
			if (rc < 0)
				return rc;

			holtek8_macro_from_events(button, macro_events, HOLTEK8_MAX_MACRO_EVENTS);
			break;
		}
		default:
			log_debug(device->ratbag, "Button %u unsupported: %#x %#x %#x %#x\n", button->index, data->type, data->data[0], data->data[1], data->data[2]);
			break;
	}


	return 0;
}

/*
 * Converts ratbag button to raw device button data.
 * If the ratbag's button action is macro, writes an encoded macro
 * to the device memory and sets a data already pointing to the
 * just written macro.
 *
 * @param[out] data Resulting raw button data, zeroed on error
 * @return 0 on success or a negative errno.
 */
int
holtek8_button_to_data(const struct ratbag_button *button, struct holtek8_button_data *data)
{
	struct ratbag_device *device = button->profile->device;
	int rc;

	rc = holtek8_button_map_action_to_raw(&button->action, data);
	if (rc)
		return 0;

	memset(data, 0, sizeof(*data));

	switch (button->action.type) {
		case RATBAG_BUTTON_ACTION_TYPE_KEY: {
			const uint8_t hid_code = ratbag_hidraw_get_keyboard_usage_from_keycode(device, button->action.action.key.key);
			const uint16_t hid_code_cc = ratbag_hidraw_get_consumer_usage_from_keycode(device, button->action.action.key.key);
			data->type = HOLTEK8_BUTTON_TYPE_KEYBOARD;

			if (hid_code > 0) {
				data->keyboard.hid_key = hid_code;
			}
			else if (hid_code_cc > 0) {
				data->type = HOLTEK8_BUTTON_TYPE_MEDIA;
				set_unaligned_le_u16(data->media.hid_key, hid_code_cc);
			}

			break;
		}
		case RATBAG_BUTTON_ACTION_TYPE_MACRO: {
			unsigned int modifiers;
			unsigned int key1;
			unsigned int key2;

			rc = holtek8_keycodes_from_ratbag_macro(&button->action, &key1, &key2, &modifiers);
			if (rc == -EPROTO) {
				union holtek8_macro_event macro_events[HOLTEK8_MAX_MACRO_EVENTS] = {0};

				rc = holtek8_macro_to_events(button, macro_events, HOLTEK8_MAX_MACRO_EVENTS);
				if (rc < 0)
					return rc;

				rc = holtek8_write_macro_data(device, macro_events, HOLTEK8_MAX_MACRO_EVENTS);
				if (rc < 0)
					return rc;

				data->type = HOLTEK8_BUTTON_TYPE_MACRO;
				data->macro.mode = HOLTEK8_BUTTON_MACRO_REPEAT_COUNT;
				data->macro.index = rc;

				return 0;
			}
			if (rc < 0) {
				return rc;
			}

			data->type = HOLTEK8_BUTTON_TYPE_KEYBOARD;
			data->keyboard.modifiers = modifiers;
			data->keyboard.hid_key = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key1);
			data->keyboard.hid_key2 = ratbag_hidraw_get_keyboard_usage_from_keycode(device, key2);

			if (rc == 1 && modifiers == 0 && data->keyboard.hid_key == 0) {
				uint16_t hid_code_cc = ratbag_hidraw_get_consumer_usage_from_keycode(device, key1);
				if (hid_code_cc == 0)
					break;

				data->type = HOLTEK8_BUTTON_TYPE_MEDIA;
				set_unaligned_le_u16(data->media.hid_key, hid_code_cc);
			}

			break;
		}
		default:
			log_error(device->ratbag, "Button %u action type unsupported: %u\n", button->index, button->action.type);
			return -EINVAL;
	}

	return 0;
}

const struct holtek8_sensor_config *
holtek8_get_sensor_config(enum holtek8_sensor sensor)
{
	const struct holtek8_sensor_config *cfg = NULL;

	ARRAY_FOR_EACH(holtek8_sensor_configurations, cfg) {
		if (cfg->sensor != sensor) {
			continue;
		}
		return cfg;
	}

	abort();
}

enum holtek8_sensor
holtek8_get_sensor_from_name(const char *name)
{
	const struct holtek8_sensor_config *cfg = NULL;

	ARRAY_FOR_EACH(holtek8_sensor_configurations, cfg) {
		if (!streq(cfg->name, name)) {
			continue;
		}
		return cfg->sensor;
	}

	return HOLTEK8_SENSOR_UNKNOWN;
}

/*
 * The device is sensitive to unsynchronized writes.
 * This functions asks the device if it really expects the amount
 * of bytes to write we think.
 *
 * @return 0 on success or a negative errno.
 */
static int
holtek8_poll_write_ready(struct ratbag_device *device, uint8_t bytes_left)
{
	struct holtek8_data *drv_data = device->drv_data;
	struct holtek8_feature_report report;
	uint8_t bytes_left_dev, bytes_left_pos;
	int i, rc;

	switch (drv_data->api_version) {
		case HOLTEK8_API_A:
			bytes_left_pos = 3;
			break;
		case HOLTEK8_API_B:
			bytes_left_pos = 1;
			break;
	}

	for (i = 0; i < HOLTEK8_POLL_RETRY_LIMIT; i++) {
		rc = holtek8_get_feature_report(device, &report);
		if (rc < 0)
			return rc;

		bytes_left_dev = report.arg[bytes_left_pos];

		if (bytes_left == bytes_left_dev)
			return 0;

		msleep(HOLTEK8_POLL_TIME_MS);
	}

	return -EIO;
}

/*
 * Clears hidraw's read buffer.
 * Prevents reading incorrect data if there was another
 * configuration program before us.
 *
 * @return Number of chunks of data cleared or a negative errno.
 */
static int
holtek8_clear_read_buffer(struct ratbag_device *device)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t chunk_size = drv_data->chunk_size;
	int rc, nfds;
	struct pollfd fds;
	uint8_t tmp_buf[HOLTEK8_MAX_CHUNK_SIZE + 1];
	int chunks_cleared = 0;

	assert(HOLTEK8_MAX_CHUNK_SIZE >= chunk_size);

	fds.fd = device->hidraw[0].fd;
	fds.events = POLLIN;

	while (1) {
		nfds = poll(&fds, 1, 0);

		if (nfds < 0)
			return -errno;
		if (nfds > 0) {
			rc = (int)read(device->hidraw[0].fd, tmp_buf, chunk_size);

			if (rc < 0)
				return -errno;

			chunks_cleared++;
		}
		else {
			return chunks_cleared;
		}
	}
}

/*
 * Read `len` bytes in chunks from device.
 *
 * These devices don't use numbered reports for configuration
 * interface, instead expect us to to read raw data in constant sized
 * chunks. The read is initiated with SET report (by the caller)
 * with a command and parameters, and a GET report afterwards (by
 * this function).
 *
 * @param response A response for a GET report, discarded if NULL
 * @return 0 on success or a negative errno.
 */
int
holtek8_read_chunked(struct ratbag_device *device, uint8_t *buf, uint8_t len, struct holtek8_feature_report *response)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t chunk_size = drv_data->chunk_size;
	struct holtek8_feature_report tmp;
	int i, rc;

	assert(len % chunk_size == 0);
	assert(chunk_size <= HOLTEK8_MAX_CHUNK_SIZE);

	if (response == NULL)
		response = &tmp;

	rc = holtek8_clear_read_buffer(device);
	if (rc < 0)
		return rc;

	rc = holtek8_get_feature_report(device, response);
	if (rc < 0)
		return rc;

	for (i = 0; i < len/chunk_size; i++) {
		rc = ratbag_hidraw_read_input_report(device, buf + (size_t)i*chunk_size, chunk_size, NULL);

		if (rc < 0)
			return rc;

		if (rc != chunk_size)
			return -EIO;
	}

	return 0;
}

/*
 * Write `len` bytes in chunks to device.
 *
 * The write is initiated with SET report with a command
 * and parameters (by the caller). Polls before each write.
 *
 * @return 0 on success or a negative errno.
 */
int
holtek8_write_chunked(struct ratbag_device *device, const uint8_t *buf, uint8_t len)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t chunk_size = drv_data->chunk_size;
	uint8_t tmp_buf[HOLTEK8_MAX_CHUNK_SIZE + 1];
	uint8_t bytes_left = len;
	int i, rc;

	assert(len % chunk_size == 0);
	assert(chunk_size <= HOLTEK8_MAX_CHUNK_SIZE);

	for (i = 0; i < len/chunk_size; i++) {
		tmp_buf[0] = 0;
		memcpy(tmp_buf + 1, buf + (size_t)i*chunk_size, chunk_size);

		rc = holtek8_poll_write_ready(device, bytes_left);
		if (rc < 0)
			return rc;

		rc = ratbag_hidraw_output_report(device, tmp_buf, chunk_size + 1);
		if (rc < 0)
			return rc;

		bytes_left -= chunk_size;
	}

	rc = holtek8_poll_write_ready(device, 0);
	if (rc < 0)
		return rc;

	return 0;
}

/*
 * Read a padded chunk with `len` bytes of data from device.
 *
 * The device expects us to read a full chunk size of data, even
 * when we request less.
 * The read is initiated with SET report (by the caller) with a
 * command and parameters, and a GET report afterwards (by this function).
 *
 * @param response A response for a GET report, discarded if NULL
 * @return 0 on success or a negative errno.
 */
int
holtek8_read_padded(struct ratbag_device *device, uint8_t *buf, uint8_t len, struct holtek8_feature_report *response)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t chunk_size = drv_data->chunk_size;
	struct holtek8_feature_report tmp;
	uint8_t tmp_buf[HOLTEK8_MAX_CHUNK_SIZE];
	int rc;

	assert(len <= chunk_size);
	assert(chunk_size <= HOLTEK8_MAX_CHUNK_SIZE);

	if (response == NULL)
		response = &tmp;

	rc = holtek8_clear_read_buffer(device);
	if (rc < 0)
		return rc;

	rc = holtek8_get_feature_report(device, response);
	if (rc < 0)
		return rc;

	rc = ratbag_hidraw_read_input_report(device, tmp_buf, chunk_size, NULL);

	if (rc < 0)
		return rc;

	if (rc != chunk_size)
		return -EIO;

	memcpy(buf, tmp_buf, len);

	return 0;
}

/*
 * Write a padded chunk with `len` bytes of data to device.
 *
 * The write is initiated with SET report with a command
 * and parameters (by the caller). Polls before a write.
 *
 * @return 0 on success or a negative errno.
 */
int
holtek8_write_padded(struct ratbag_device *device, const uint8_t *buf, uint8_t len)
{
	struct holtek8_data *drv_data = device->drv_data;
	uint8_t chunk_size = drv_data->chunk_size;
	uint8_t tmp_buf[HOLTEK8_MAX_CHUNK_SIZE + 1] = {0};
	int rc;

	assert(len <= chunk_size);
	assert(chunk_size <= HOLTEK8_MAX_CHUNK_SIZE);

	tmp_buf[0] = 0;
	memcpy(tmp_buf + 1, buf, len);

	rc = holtek8_poll_write_ready(device, len);
	if (rc < 0)
		return rc;

	rc = ratbag_hidraw_output_report(device, tmp_buf, chunk_size + 1);
	if (rc < 0)
		return rc;

	return 0;
}

void
holtek8_calculate_checksum(struct holtek8_feature_report *report)
{
	int i;

	report->checksum = 0xff;
	report->checksum -= report->command;
	for (i = 0; i<6; i++) {
		report->checksum -= report->arg[i];
	}
}

/*
 * Ask device to reply with given 4 bytes using HOLTEK8_CMD_ECHO
 * to check if the device responds and password is set correctly.
 *
 * @return true if reply match, false otherwise or on error
 */
bool
holtek8_test_echo(struct ratbag_device *device)
{
	int rc;
	struct holtek8_feature_report report = {0, HOLTEK8_CMD_ECHO, {'R', 'A', 'T', 'B', 0, 0}, 0};

	rc = holtek8_set_feature_report(device, &report);
	if (rc < 0)
		return false;

	rc = holtek8_get_feature_report(device, &report);
	if (rc < 0)
		return false;

	return report.arg[0] == 'R' && report.arg[1] == 'A' && report.arg[2] == 'T' && report.arg[3] == 'B';

}

static const struct holtek8_device_data *
holtek8_find_device_data(const struct ratbag_device *device, const char *fw_version)
{
	const struct ratbag_device_data *data = device->data;

	const struct list *supported_devices = ratbag_device_data_holtek8_get_supported_devices(data);

	struct holtek8_device_data *device_data = NULL;
	list_for_each(device_data, supported_devices, link) {
		if (device_data->device_name == NULL) {
			log_error(device->ratbag, "Skipping invalid device data\n");
			continue;
		}

		if (!strneq(fw_version, device_data->fw_version, HOLTEK8_FW_VERSION_LEN))
			continue;

		return device_data;
	}

	return NULL;
}

int
holtek8_load_device_data(struct ratbag_device *device)
{
	const char *fw_version;
	struct holtek8_data *drv_data = device->drv_data;
	const struct holtek8_device_data *device_data;

	fw_version = udev_prop_value(device->udev_device, "ID_USB_REVISION");
	if (!fw_version)
		return -ENODEV;

	ratbag_device_set_firmware_version(device, fw_version);

	device_data = holtek8_find_device_data(device, fw_version);
	if (!device_data) {
		log_info(device->ratbag,
			  "Device with firmware version `%s` is not supported; "
			  "Perhaps the device file is missing a section for this device?\n",
			  fw_version
		);
		return -ENODEV;
	}

	drv_data->sensor_cfg = holtek8_get_sensor_config(device_data->sensor);
	if (drv_data->sensor_cfg->sensor == HOLTEK8_SENSOR_UNKNOWN)
		log_error(device->ratbag, "Unknown sensor type, using fallback values\n");

	if (device_data->button_count < 0 || device_data->button_count > 16) {
		log_error(device->ratbag, "Couldn't load button count\n");
		return -EINVAL;
	}
	drv_data->button_count = device_data->button_count;

	switch (drv_data->api_version) {
		case HOLTEK8_API_A:
			assert(sizeof(drv_data->api_a.password) == 6 && sizeof(device_data->password) == 6);
			memcpy(drv_data->api_a.password, device_data->password, sizeof(drv_data->api_a.password));
			break;
		case HOLTEK8_API_B:
			break;
	}

	log_info(device->ratbag, "Found device %s fw_ver %s, %d buttons, sensor %s\n", device_data->device_name, fw_version, device_data->button_count, drv_data->sensor_cfg->name);
	return 0;
}

#define HID_REPORT_COUNT	0b10010100
#define HID_INPUT		0b10000000

/*
 * Gets a chunk size and input capability from a report descriptor
 * to check driver's assumption that all devices of the same api
 * should have the same chunk size. If a mismatch occurs, don't try
 * to load chunk size at runtime and allow us to investigate first.
 *
 * @return 0 on success or a negative errno.
 */
int
holtek8_test_report_descriptor(struct ratbag_device *device)
{
	int rc, desc_size = 0;
	const struct holtek8_data *drv_data = device->drv_data;
	const struct ratbag_hidraw *hidraw = &device->hidraw[0];
	struct hidraw_report_descriptor report_desc = {0};
	unsigned int i, j;
	unsigned int desc_chunk;
	bool desc_input;

	rc = ioctl(hidraw->fd, HIDIOCGRDESCSIZE, &desc_size);
	if (rc < 0)
		return rc;

	report_desc.size = desc_size;
	rc = ioctl(hidraw->fd, HIDIOCGRDESC, &report_desc);
	if (rc < 0)
		return rc;


	i = 0;
	desc_chunk = 0;
	desc_input = false;
	while (i < report_desc.size) {
		uint8_t value = report_desc.value[i];
		uint8_t hid = value & 0xfc;
		uint8_t size = value & 0x3;
		unsigned content = 0;

		if (size == 3)
			size = 4;

		if (i + size >= report_desc.size)
			return -EPROTO;

		for (j = 0; j < size; j++)
			content |= report_desc.value[i + j + 1] << (j * 8);

		switch (hid) {
		case HID_REPORT_COUNT:
			if (!desc_chunk)
				desc_chunk = content;
			break;
		case HID_INPUT:
			desc_input = true;
			break;
		}

		i += 1 + size;
	}

	if (drv_data->chunk_size != desc_chunk) {
		log_error(device->ratbag, "Driver's chunk size does not match device's, please report this bug\n");
		log_buf_error(device->ratbag, "Report descriptor: ", report_desc.value, report_desc.size);
		return -ENODEV;
	}

	if (!desc_input) {
		log_error(device->ratbag, "Device claims having no input capability, please report this bug\n");
		log_buf_error(device->ratbag, "Report descriptor: ", report_desc.value, report_desc.size);
		return -ENODEV;
	}

	return 0;
}

