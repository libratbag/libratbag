#include "marsgaming-buttons.h"

struct marsgaming_button_action_mapping {
	enum marsgaming_button_action marsgaming_action_id;
	struct ratbag_button_action ratbag_action;
};

/**
 * This array contains only the actions that can be statically generated.
 * This array is used in the marsgaming_button_action_lookup function.
 * For any action that cannot be statically generated (has variables in it)
 * we use the rest of the marsgaming_button_action_* functions
 */
static const struct marsgaming_button_action_mapping marsgaming_mm4_button_action_mapping[] = {
	{ MARSGAMING_MM4_ACTION_LEFT_CLICK, BUTTON_ACTION_BUTTON(1) },
	{ MARSGAMING_MM4_ACTION_RIGHT_CLICK, BUTTON_ACTION_BUTTON(2) },
	{ MARSGAMING_MM4_ACTION_MIDDLE_CLICK, BUTTON_ACTION_BUTTON(3) },
	{ MARSGAMING_MM4_ACTION_BACKWARD, BUTTON_ACTION_BUTTON(4) },
	{ MARSGAMING_MM4_ACTION_FORWARD, BUTTON_ACTION_BUTTON(5) },
	{ MARSGAMING_MM4_ACTION_DPI_SWITCH, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP) },
	{ MARSGAMING_MM4_ACTION_DPI_MINUS, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN) },
	{ MARSGAMING_MM4_ACTION_DPI_PLUS, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP) },
	{ MARSGAMING_MM4_ACTION_PROFILE_SWITCH, BUTTON_ACTION_SPECIAL(RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP) }
};

static struct ratbag_button_action
marsgaming_button_action_lookup(struct ratbag_button *button,
				const struct marsgaming_button_info *button_info)
{
	const struct marsgaming_button_action_mapping *button_mapping;
	ARRAY_FOR_EACH(marsgaming_mm4_button_action_mapping, button_mapping) {
		if (button_mapping->marsgaming_action_id == button_info->action)
			return button_mapping->ratbag_action;
	}
	return (struct ratbag_button_action)BUTTON_ACTION_UNKNOWN;
}

static struct ratbag_button_action
marsgaming_button_action_media(struct ratbag_button *button,
			       const struct marsgaming_button_info *button_info)
{
	// TODO: Convert from marsgaming media to ratbag media key codes
	return (struct ratbag_button_action)BUTTON_ACTION_UNKNOWN;
}

static int
marsgaming_ratbag_button_macro_from_combo_keycode(struct ratbag_button *button, unsigned int key0, unsigned int key1, unsigned int modifiers);

static struct ratbag_button_action
marsgaming_button_action_key(struct ratbag_button *button,
			     const struct marsgaming_button_info *button_info)
{
	// Single and combo keys share some structure, so we will treat them all like combo keys
	const uint8_t mods = button_info->action_info.combo_key.modifiers;
	const uint8_t key0 = button_info->action_info.combo_key.keys[0];
	const uint8_t key1 = button_info->action_info.combo_key.keys[1];
	const unsigned int event_key0 = ratbag_hidraw_get_keycode_from_keyboard_usage(button->profile->device, key0);
	if (key1 == 0) {
		ratbag_button_macro_new_from_keycode(button, event_key0, mods);
		return button->action;
	}
	const unsigned int event_key1 = ratbag_hidraw_get_keycode_from_keyboard_usage(button->profile->device, key1);
	marsgaming_ratbag_button_macro_from_combo_keycode(button, event_key0, event_key1, mods);
	return button->action;
}

enum marsgaming_modifier_mask_offset {
	MARSGAMING_MODIFIER_LEFTCTRL = 1 << 0,
	MARSGAMING_MODIFIER_LEFTSHIFT = 1 << 1,
	MARSGAMING_MODIFIER_LEFTALT = 1 << 2,
	MARSGAMING_MODIFIER_LEFTMETA = 1 << 3,
	MARSGAMING_MODIFIER_RIGHTCTRL = 1 << 4,
	MARSGAMING_MODIFIER_RIGHTSHIFT = 1 << 5,
	MARSGAMING_MODIFIER_RIGHTALT = 1 << 6,
	MARSGAMING_MODIFIER_RIGHTMETA = 1 << 7,
};

struct marsgaming_modifier_mapping {
	unsigned int modifier_mask;
	unsigned int key;
};

static const struct marsgaming_modifier_mapping marsgaming_modifier_mapping[] = {
	{ MARSGAMING_MODIFIER_LEFTCTRL, KEY_LEFTCTRL },
	{ MARSGAMING_MODIFIER_LEFTSHIFT, KEY_LEFTSHIFT },
	{ MARSGAMING_MODIFIER_LEFTALT, KEY_LEFTALT },
	{ MARSGAMING_MODIFIER_LEFTMETA, KEY_LEFTMETA },
	{ MARSGAMING_MODIFIER_RIGHTCTRL, KEY_RIGHTCTRL },
	{ MARSGAMING_MODIFIER_RIGHTSHIFT, KEY_RIGHTSHIFT },
	{ MARSGAMING_MODIFIER_RIGHTALT, KEY_RIGHTALT },
	{ MARSGAMING_MODIFIER_RIGHTMETA, KEY_RIGHTMETA },
};

static int
marsgaming_ratbag_button_macro_from_combo_keycode(struct ratbag_button *button, unsigned int key0, unsigned int key1, unsigned int modifiers)
{
	const struct marsgaming_modifier_mapping *mapping;
	struct ratbag_button_macro *macro = ratbag_button_macro_new("combo-key");
	int i = 0;

	ARRAY_FOR_EACH(marsgaming_modifier_mapping, mapping) {
		if (modifiers & mapping->modifier_mask) {
			ratbag_button_macro_set_event(macro,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_PRESSED,
						      mapping->key);
		}
	}

	ratbag_button_macro_set_event(macro,
				      i++,
				      RATBAG_MACRO_EVENT_KEY_PRESSED,
				      key0);
	ratbag_button_macro_set_event(macro,
				      i++,
				      RATBAG_MACRO_EVENT_KEY_PRESSED,
				      key1);
	ratbag_button_macro_set_event(macro,
				      i++,
				      RATBAG_MACRO_EVENT_KEY_RELEASED,
				      key1);
	ratbag_button_macro_set_event(macro,
				      i++,
				      RATBAG_MACRO_EVENT_KEY_RELEASED,
				      key0);

	ARRAY_FOR_EACH(marsgaming_modifier_mapping, mapping) {
		if (modifiers & mapping->modifier_mask) {
			ratbag_button_macro_set_event(macro,
						      i++,
						      RATBAG_MACRO_EVENT_KEY_RELEASED,
						      mapping->key);
		}
	}

	ratbag_button_copy_macro(button, macro);
	ratbag_button_macro_unref(macro);

	return 0;
}

static struct ratbag_button_action
marsgaming_button_action_macro(struct ratbag_button *button,
			       const struct marsgaming_button_info *button_info)
{
	// TODO: Create a ratbag macro from the marsgaming macro
	return (struct ratbag_button_action)BUTTON_ACTION_UNKNOWN;
}

static struct ratbag_button_action
marsgaming_button_action_fire(struct ratbag_button *button,
			      const struct marsgaming_button_info *button_info)
{
	// There's no way to convert this to ratbag structs, so we'll return unknown
	return (struct ratbag_button_action)BUTTON_ACTION_UNKNOWN;
}

#define MARSGAMING_BUTTON_ACTION_NONE                                                   \
	{                                                                               \
		.action = MARSGAMING_MM4_ACTION_MEDIA, .action_info.media.media_key = 0 \
	}

static struct marsgaming_button_info
marsgaming_from_ratbag_to_action_none(struct ratbag_button *button)
{
	return (struct marsgaming_button_info)MARSGAMING_BUTTON_ACTION_NONE;
}

struct marsgaming_from_ratbag_to_button_map {
	uint8_t button_id;
	struct marsgaming_button_info button_info;
};

static const struct marsgaming_from_ratbag_to_button_map marsgaming_from_ratbag_to_button_maps[] = {
	{ 1, { .action = MARSGAMING_MM4_ACTION_LEFT_CLICK } },
	{ 2, { .action = MARSGAMING_MM4_ACTION_RIGHT_CLICK } },
	{ 3, { .action = MARSGAMING_MM4_ACTION_MIDDLE_CLICK } },
	{ 4, { .action = MARSGAMING_MM4_ACTION_BACKWARD } },
	{ 5, { .action = MARSGAMING_MM4_ACTION_FORWARD } },
};

static struct marsgaming_button_info
marsgaming_from_ratbag_to_action_button(struct ratbag_button *button)
{
	const unsigned int button_id = button->action.action.button;
	const struct marsgaming_from_ratbag_to_button_map *map;
	ARRAY_FOR_EACH(marsgaming_from_ratbag_to_button_maps, map) {
		if (button_id == map->button_id)
			return map->button_info;
	}
	return (struct marsgaming_button_info)MARSGAMING_BUTTON_ACTION_NONE;
}

struct marsgaming_from_ratbag_to_special_map {
	enum ratbag_button_action_special special_id;
	struct marsgaming_button_info button_info;
};

static const struct marsgaming_from_ratbag_to_special_map marsgaming_from_ratbag_to_special_maps[] = {
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_CYCLE_UP, { .action = MARSGAMING_MM4_ACTION_DPI_SWITCH } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_DOWN, { .action = MARSGAMING_MM4_ACTION_DPI_MINUS } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_RESOLUTION_UP, { .action = MARSGAMING_MM4_ACTION_DPI_PLUS } },
	{ RATBAG_BUTTON_ACTION_SPECIAL_PROFILE_CYCLE_UP, { .action = MARSGAMING_MM4_ACTION_PROFILE_SWITCH } },
};

static struct marsgaming_button_info
marsgaming_from_ratbag_to_action_special(struct ratbag_button *button)
{
	const enum ratbag_button_action_special special_type = button->action.action.special;
	const struct marsgaming_from_ratbag_to_special_map *map;
	ARRAY_FOR_EACH(marsgaming_from_ratbag_to_special_maps, map) {
		if (special_type == map->special_id)
			return map->button_info;
	}
	return (struct marsgaming_button_info)MARSGAMING_BUTTON_ACTION_NONE;
}

static int
marsgaming_keycodes_from_ratbag_macro(struct ratbag_button_action *action, unsigned int *key0_out, unsigned int *key1_out, unsigned int *mods_out);

static struct marsgaming_button_info
marsgaming_from_ratbag_to_action_macro(struct ratbag_button *button)
{
	unsigned int key0;
	unsigned int key1;
	unsigned int mods;
	int e = marsgaming_keycodes_from_ratbag_macro(&button->action, &key0, &key1, &mods);
	if (e == 1 || e == 2) { // We can convert this to 2-key combo
		return (struct marsgaming_button_info){
			.action = MARSGAMING_MM4_ACTION_COMBO_KEY,
			.action_info.combo_key = {
				.modifiers = mods,
				.keys[0] = ratbag_hidraw_get_keyboard_usage_from_keycode(button->profile->device, key0),
				.keys[1] = ratbag_hidraw_get_keyboard_usage_from_keycode(button->profile->device, key1),
			}

		};
	}
	// TODO: Implement macros
	return (struct marsgaming_button_info)MARSGAMING_BUTTON_ACTION_NONE;
}

static int
marsgaming_keycodes_from_ratbag_macro(struct ratbag_button_action *action, unsigned int *key0_out, unsigned int *key1_out, unsigned int *modifiers_out)
{
	struct ratbag_macro *macro = action->macro;
	unsigned int key0 = KEY_RESERVED;
	unsigned int key1 = KEY_RESERVED;
	unsigned int modifiers = 0;
	unsigned int i;
	unsigned int keys_pressed = 0;

	if (!macro || action->type != RATBAG_BUTTON_ACTION_TYPE_MACRO)
		return -EINVAL;

	if (macro->events[0].type == RATBAG_MACRO_EVENT_NONE)
		return -EINVAL;

	{
		unsigned int num_keys = ratbag_action_macro_num_keys(action);
		if (num_keys == 0 || num_keys > 2)
			return -EINVAL;
	}

	for (i = 0; i < MAX_MACRO_EVENTS; i++) {
		struct ratbag_macro_event event;

		event = macro->events[i];
		switch (event.type) {
		case RATBAG_MACRO_EVENT_INVALID:
			return -EINVAL;
		case RATBAG_MACRO_EVENT_NONE:
			return 0;
		case RATBAG_MACRO_EVENT_KEY_PRESSED:
			switch (event.event.key) {
			case KEY_LEFTCTRL: modifiers |= MARSGAMING_MODIFIER_LEFTCTRL; break;
			case KEY_LEFTSHIFT: modifiers |= MARSGAMING_MODIFIER_LEFTSHIFT; break;
			case KEY_LEFTALT: modifiers |= MARSGAMING_MODIFIER_LEFTALT; break;
			case KEY_LEFTMETA: modifiers |= MARSGAMING_MODIFIER_LEFTMETA; break;
			case KEY_RIGHTCTRL: modifiers |= MARSGAMING_MODIFIER_RIGHTCTRL; break;
			case KEY_RIGHTSHIFT: modifiers |= MARSGAMING_MODIFIER_RIGHTSHIFT; break;
			case KEY_RIGHTALT: modifiers |= MARSGAMING_MODIFIER_RIGHTALT; break;
			case KEY_RIGHTMETA: modifiers |= MARSGAMING_MODIFIER_RIGHTMETA; break;
			default:
				if (key0 == KEY_RESERVED) {
					key0 = event.event.key;
				} else if (key1 == KEY_RESERVED) {
					key1 = event.event.key;
				} else {
					return -EINVAL;
				}
				++keys_pressed;
			}
			break;
		case RATBAG_MACRO_EVENT_KEY_RELEASED:
			switch (event.event.key) {
			case KEY_LEFTCTRL: modifiers &= ~MARSGAMING_MODIFIER_LEFTCTRL; break;
			case KEY_LEFTSHIFT: modifiers &= ~MARSGAMING_MODIFIER_LEFTSHIFT; break;
			case KEY_LEFTALT: modifiers &= ~MARSGAMING_MODIFIER_LEFTALT; break;
			case KEY_LEFTMETA: modifiers &= ~MARSGAMING_MODIFIER_LEFTMETA; break;
			case KEY_RIGHTCTRL: modifiers &= ~MARSGAMING_MODIFIER_RIGHTCTRL; break;
			case KEY_RIGHTSHIFT: modifiers &= ~MARSGAMING_MODIFIER_RIGHTSHIFT; break;
			case KEY_RIGHTALT: modifiers &= ~MARSGAMING_MODIFIER_RIGHTALT; break;
			case KEY_RIGHTMETA: modifiers &= ~MARSGAMING_MODIFIER_RIGHTMETA; break;
			default:
				// As soon as we release a key that we pressed, return what we have processed so far
				if (event.event.key == key0 || event.event.key == key1) {
					*key0_out = key0;
					*key1_out = key1;
					*modifiers_out = modifiers;
					return (int)keys_pressed;
				}

				return -EINVAL;
			}
		case RATBAG_MACRO_EVENT_WAIT:
			break;
		default:
			return -EINVAL;
		}
	}

	return -EINVAL;
}

struct marsgaming_button_action_to_ratbag_parser {
	enum marsgaming_button_action marsgaming_action_id;
	struct ratbag_button_action (*parse_action)(struct ratbag_button *button,
						    const struct marsgaming_button_info *button_info);
};

static const struct marsgaming_button_action_to_ratbag_parser marsgaming_button_action_to_ratbag_parsers[] = {
	{ MARSGAMING_MM4_ACTION_LEFT_CLICK, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_RIGHT_CLICK, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_MIDDLE_CLICK, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_BACKWARD, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_FORWARD, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_DPI_SWITCH, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_DPI_MINUS, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_DPI_PLUS, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_PROFILE_SWITCH, marsgaming_button_action_lookup },
	{ MARSGAMING_MM4_ACTION_MEDIA, marsgaming_button_action_media },
	{ MARSGAMING_MM4_ACTION_COMBO_KEY, marsgaming_button_action_key },
	{ MARSGAMING_MM4_ACTION_SINGLE_KEY, marsgaming_button_action_key },
	{ MARSGAMING_MM4_ACTION_MACRO, marsgaming_button_action_macro },
	{ MARSGAMING_MM4_ACTION_FIRE, marsgaming_button_action_fire },
};

struct ratbag_button_action
marsgaming_parse_button_to_action(struct ratbag_button *button,
				  const struct marsgaming_button_info *button_info)
{
	const struct marsgaming_button_action_to_ratbag_parser *parser;
	ARRAY_FOR_EACH(marsgaming_button_action_to_ratbag_parsers, parser) {
		if (button_info->action == parser->marsgaming_action_id)
			return parser->parse_action(button, button_info);
	}
	// If no action matches, set it to unknown
	return (struct ratbag_button_action)BUTTON_ACTION_UNKNOWN;
}

struct marsgaming_from_ratbag_button_action_to_parser {
	enum ratbag_button_action_type ratbag_action_type;
	struct marsgaming_button_info (*parse_action)(struct ratbag_button *button);
};

static const struct marsgaming_from_ratbag_button_action_to_parser marsgaming_button_action_to_marsgaming_parsers[] = {
	{ RATBAG_BUTTON_ACTION_TYPE_NONE, marsgaming_from_ratbag_to_action_none },
	{ RATBAG_BUTTON_ACTION_TYPE_BUTTON, marsgaming_from_ratbag_to_action_button },
	{ RATBAG_BUTTON_ACTION_TYPE_SPECIAL, marsgaming_from_ratbag_to_action_special },
	{ RATBAG_BUTTON_ACTION_TYPE_MACRO, marsgaming_from_ratbag_to_action_macro },
	{ RATBAG_BUTTON_ACTION_TYPE_UNKNOWN, marsgaming_from_ratbag_to_action_none },
};

struct marsgaming_optional_button_info
marsgaming_button_of_type(struct ratbag_button *button)
{
	const struct marsgaming_from_ratbag_button_action_to_parser *parser;
	ARRAY_FOR_EACH(marsgaming_button_action_to_marsgaming_parsers, parser) {
		if (button->action.type != parser->ratbag_action_type)
			continue;
		return (struct marsgaming_optional_button_info){
			.is_present = 1,
			.button_info = parser->parse_action(button)
		};
	}
	return (struct marsgaming_optional_button_info){
		.is_present = 0,
		.none = 1
	};
}
