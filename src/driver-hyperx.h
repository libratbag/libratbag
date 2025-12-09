#pragma once

#include "libratbag-data.h"


#define HYPERX_USAGE_PAGE 0xff00
#define HYPERX_PACKET_SIZE 64

#define HYPERX_LED_PACKET_COUNT 6
#define	HYPERX_DPI_STEP 100
#define HYPERX_DPI_COUNT 5

// Magic numbers, no clue what these mean
#define HYPERX_LED_MODE_VALUE_BEFORE 0x55
#define HYPERX_LED_MODE_VALUE_AFTER 0x23

#define HYPERX_ACTION_DPI_TOGGLE 8

#define HYPERX_MAX_MACRO_EVENTS 80
#define HYPERX_MAX_MACRO_PACKETS 14
#define HYPERX_MACRO_EVENT_MAX_KEYS 6
#define HYPERX_MACRO_EVENT_DEFAULT_DELAY htole16(20)

// Max number of events in a macro data packet
#define HYPERX_MACRO_DATA_MAX_EVENTS 6

#define hyperx_is_dpi_profile_enabled(profile_bitmask, n) ((profile_bitmask) & (1 << (n)))
#define hyperx_brightness_value(x) ((int) ((x / 255.0) * 100))

/**
 * Every macro data packet seems to have a sum byte? after the button byte that alternates between adding 1 and 2 each packet.
 * Another way of thinking about it is half of the numbers in the sum are 1 and half are 2.
 *
 * Thus, we get the following formula: (1x / 2) + (2x / 2). Which is simplified to 3x / 2. Note that this is int division, so there would be no fractional part.
 *
 * For example, the sum bytes for 6 packets would be: 0x00, 0x01, 0x03, 0x04, 0x06, 0x07
 */
#define HYPERX_MACRO_PACKET_SUM(x) ((3*(x)) / 2)

/**
 * The event count byte in odd indexed macro data packets have 0x80 added to it.
 * For example, a packet with 2 events would be 0x02 if it was even, and 0x82 if it was odd.
 */
#define HYPERX_MACRO_PACKET_EVENT_COUNT(x) (((x) % 2) * 0x80)

#define	HYPERX_BYTES_AFTER_MACRO_ASSIGNMENT (5)
#define	HYPERX_BYTES_AFTER_LED_MODE (3)

#define _Static_assert_bytes_after(expr) _Static_assert(expr, "Incorrect value for 'bytes_after'")
#define _Static_assert_enum_size(enum_name) _Static_assert(sizeof(enum enum_name) == 1, \
		"Incorrect size for '" #enum_name "''")

enum hyperx_config_value {
	HYPERX_CONFIG_POLLING_RATE              = 0xd0,
	HYPERX_CONFIG_LED_EFFECT                = 0xda,
	HYPERX_CONFIG_LED_MODE                  = 0xd9,
	HYPERX_CONFIG_DPI                       = 0xd3,
	HYPERX_CONFIG_BUTTON_ASSIGNMENT         = 0xd4,
	HYPERX_CONFIG_MACRO_ASSIGNMENT          = 0xd5,
	HYPERX_CONFIG_MACRO_DATA                = 0xd6,

	HYPERX_CONFIG_SAVE_SETTINGS             = 0xde
} __attribute((packed));

_Static_assert_enum_size(hyperx_config_value);

enum hyperx_report_value {
	HYPERX_REPORT_DEVICE_INFO = 0x50,
	HYPERX_RPEORT_DPI_SETTINGS = 0x53
} __attribute((packed));

_Static_assert_enum_size(hyperx_report_value);

enum hyperx_report_device_info_type {
	HYPERX_REPORT_DEVICE_INFO_HARDWARE = 0x00,
	HYPERX_REPORT_DEVICE_INFO_SETTINGS = 0x03
} __attribute((packed));

_Static_assert_enum_size(hyperx_report_device_info_type);

enum hyperx_save_type {
	HYPERX_SAVE_TYPE_ALL                    = 0xff,
	HYPERX_SAVE_TYPE_DPI_PROFILES           = 0x03
} __attribute((packed));

_Static_assert_enum_size(hyperx_save_type);

enum hyperx_dpi_config {
	HYPERX_DPI_CONFIG_SELECTED_PROFILE	= 0x00,
	HYPERX_DPI_CONFIG_ENABLED_PROFILES	= 0x01,
	HYPERX_DPI_CONFIG_DPI_VALUE         = 0x02,
} __attribute((packed));

_Static_assert_enum_size(hyperx_dpi_config);

enum hyperx_led_mode {
	HYPERX_LED_MODE_SOLID = 0x01
} __attribute((packed));

_Static_assert_enum_size(hyperx_led_mode);

enum hyperx_action_type {
	HYPERX_ACTION_TYPE_DISABLED,
	HYPERX_ACTION_TYPE_MOUSE,
	HYPERX_ACTION_TYPE_KEY,
	HYPERX_ACTION_TYPE_MEDIA,
	HYPERX_ACTION_TYPE_MACRO,
	HYPERX_ACTION_TYPE_SHORTCUT,
	HYPERX_ACTION_TYPE_DPI_TOGGLE = 0x07,
	HYPERX_ACTION_TYPE_INVALID
} __attribute((packed));

_Static_assert_enum_size(hyperx_action_type);

enum hyperx_macro_event_type {
	HYPERX_MACRO_EVENT_TYPE_KEY = 0x1a
} __attribute((packed));

_Static_assert_enum_size(hyperx_macro_event_type);

union hyperx_polling_rate_packet {
	struct {
		enum hyperx_config_value polling_rate_cmd;
		uint8_t _padding[2];
		uint8_t bytes_after;
		uint8_t rate_index;
	} __attribute((packed));

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_color {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
} __attribute((packed));

union hyperx_led_packet {
	struct {
		enum hyperx_config_value led_cmd;
		uint8_t led_mode;
		uint8_t packet_number;
		uint8_t bytes_after;
		struct hyperx_color colors[20];
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

union hyperx_led_mode_packet {
	struct {
		enum hyperx_config_value led_mode_cmd;
		uint8_t _padding[2];
		uint8_t bytes_after;
		uint8_t led_mode_value_before;
		enum hyperx_led_mode led_mode;
		uint8_t led_mode_value_after;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
} __attribute((packed));

union hyperx_dpi_profile_packet {
	struct {
		enum hyperx_config_value dpi_cmd;
		enum hyperx_dpi_config value_type;
		uint8_t dpi_profile_index;
		uint8_t bytes_after;
		uint16_t dpi_step_value;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
} __attribute((packed));

union hyperx_dpi_config_packet {
	struct {
		enum hyperx_config_value dpi_cmd;
		enum hyperx_dpi_config config_type;
		uint8_t _padding[1];
		uint8_t bytes_after;
		union {
			uint8_t enabled_dpi_profiles;
			uint8_t selected_profile;
		};
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_action {
	enum hyperx_action_type type;
	uint8_t action;
	uint8_t button_index;
	struct ratbag_macro *macro;
};

union hyperx_button_packet {
	struct {
		enum hyperx_config_value button_cmd;
		uint8_t button;
		uint8_t action_type;
		uint8_t bytes_after;
		uint8_t action;
		uint8_t unknown;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_macro_event {
	enum hyperx_macro_event_type event_type;
	uint8_t modifier;
	uint8_t keys[HYPERX_MACRO_EVENT_MAX_KEYS];
	uint16_t delay_next_event;
} __attribute__((packed));

union hyperx_macro_data_packet {
	struct {
		enum hyperx_config_value macro_data_cmd;
		uint8_t button_index;
		uint8_t sum_value;
		uint8_t event_count;
		struct hyperx_macro_event events[HYPERX_MACRO_DATA_MAX_EVENTS];
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_macro {
	int event_count;
	union hyperx_macro_data_packet macro_packets[HYPERX_MAX_MACRO_PACKETS];
};

union hyperx_macro_assigment_packet {
	struct {
		enum hyperx_config_value macro_assign_cmd;
		uint8_t button;
		uint8_t _padding[1];
		uint8_t bytes_after;
		uint8_t event_count;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

union hyperx_save_settings_packet {
	struct {
		enum hyperx_config_value save_settings_cmd;
		enum hyperx_save_type save_type;
	};

	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_report_button_action {
	enum hyperx_action_type type;
	uint8_t action;
	uint8_t _padding[1];
};

struct hyperx_input_report {
	// Should be set to data[0]
	uint8_t report_value;
	uint8_t data[HYPERX_PACKET_SIZE];
};

struct hyperx_device_settings_report {
	uint8_t _report_value;

	union {
		struct {
			enum hyperx_report_value info_report_value;
			enum hyperx_report_device_info_type info_type;
			uint8_t _padding[8];
			uint8_t settings_data[HYPERX_PACKET_SIZE - 10];
		};

		uint8_t data[HYPERX_PACKET_SIZE];
	};

	// Each member should be assigned to offsets in settings_data
	// corresponding to their order and size
	struct {
		uint16_t *dpi_step_values; // Array length = dpi count
		struct hyperx_color *dpi_indicator_colors; // Array length = dpi count
		struct hyperx_report_button_action *button_actions; // Array length = button count
		uint8_t *polling_rate_index;
	};
};

struct hyperx_dpi_settings_report {
	uint8_t _report_value;

	union {
		struct {
			enum hyperx_report_value dpi_settings_report_value;
			uint8_t _padding[3];
			uint8_t selected_dpi_profile_index;
			uint8_t enabled_dpi_profiles;
		};

		uint8_t data[HYPERX_PACKET_SIZE];
	};
};

struct hyperx_drv_data {
	uint8_t enabled_dpi_profiles; // A 5-bit little-endian number, where the nth bit corresponds to profile n
	uint8_t selected_dpi_profile_index;
	struct hyperx_device_settings_report device_settings;
};

struct data_hyperx {
	int profile_count;
	int button_count;
	int led_count;
	size_t nrates;
	unsigned int *rates;
	int dpi_count;
	int is_wireless;
	struct dpi_range *dpi_range;
};

const struct data_hyperx *
ratbag_device_data_hyperx_get_data(const struct ratbag_device_data *data);

