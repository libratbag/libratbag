/*
 * Copyright © 2015 Red Hat, Inc.
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

#ifndef LIBRATBAG_H
#define LIBRATBAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdarg.h>
#include <libudev.h>

#define LIBRATBAG_ATTRIBUTE_PRINTF(_format, _args) \
	__attribute__ ((format (printf, _format, _args)))
#define LIBRATBAG_ATTRIBUTE_DEPRECATED __attribute__ ((deprecated))

/**
 * @ingroup base
 *
 * Log priority for internal logging messages.
 */
enum ratbag_log_priority {
	RATBAG_LOG_PRIORITY_DEBUG = 10,
	RATBAG_LOG_PRIORITY_INFO = 20,
	RATBAG_LOG_PRIORITY_ERROR = 30,
};

/**
 * @ingroup base
 * @struct ratbag
 *
 * A handle for accessing ratbag contexts. This struct is refcounted, use
 * ratbag_ref() and ratbag_unref().
 */
struct ratbag;

/**
 * @ingroup base
 *
 * Log handler type for custom logging.
 *
 * @param ratbag The ratbag context
 * @param priority The priority of the current message
 * @param format Message format in printf-style
 * @param args Message arguments
 *
 * @see ratbag_log_set_priority
 * @see ratbag_log_get_priority
 * @see ratbag_log_set_handler
 */
typedef void (*ratbag_log_handler)(struct ratbag *ratbag,
				      enum ratbag_log_priority priority,
				      const char *format, va_list args)
	   LIBRATBAG_ATTRIBUTE_PRINTF(3, 0);


/**
 * @defgroup base Initialization and manipulation of ratbag contexts
 * @defgroup device Querying and manipulating devices
 *
 * Device configuration is managed by "profiles" (see @ref ratbag_profile).
 * In the simplest case, a device has a single profile that can be fetched,
 * queried and manipulated and then re-applied to the device. Other devices
 * may have multiple profiles, each of which can be queried and managed
 * independently.
 *
 * FIXME: Logitech G500s software on the Mac: profiles are DPI+report rate,
 * but buttons are independent of the profiles. There are buttons to switch
 * the DPI/report rate but not the buttons at the same time.
 * Could be handled in software by updating all profiles when a button is
 * reassigned but that potentially changes the data from underneath the
 * client that already has a profile handle.
 * Possible solution: see ratbag_profile_get_button_by_index() vs
 * ratbag_get_button_by_index()
 */


/**
 * @ingroup base
 * @struct ratbag_device
 *
 * A ratbag context represents one single device. This struct is
 * refcounted, use ratbag_device_ref() and ratbag_device_unref().
 *
 * FIXME: missing: set/get_userdata
 */
struct ratbag_device;

/**
 * @ingroup device
 * @struct ratbag_profile
 *
 * A handle to a profile context on devices with the @ref
 * RATBAG_CAP_SWITCHABLE_PROFILE capability.
 * This struct is refcounted, use ratbag_profile_ref() and
 * ratbag_profile_unref().
 *
 * FIXME: missing: set/get_userdata
 */
struct ratbag_profile;

/**
 * @ingroup device
 * @struct ratbag_button
 *
 * Represents a button on the device.
 *
 * This struct is refcounted, use ratbag_button_ref() and
 * ratbag_button_unref().
 *
 * FIXME: missing: set/get_userdata
 */
struct ratbag_button;

/**
 * @ingroup base
 * @struct ratbag_interface
 *
 * libratbag does not open file descriptors to devices directly, instead
 * open_restricted() and close_restricted() are called for each path that
 * must be opened.
 *
 * @see ratbag_create_context
 */
struct ratbag_interface {
	/**
	 * Open the device at the given path with the flags provided and
	 * return the fd.
	 *
	 * @param path The device path to open
	 * @param flags Flags as defined by open(2)
	 * @param user_data The user_data provided in
	 * ratbag_create_context()
	 *
	 * @return The file descriptor, or a negative errno on failure.
	 */
	int (*open_restricted)(const char *path, int flags, void *user_data);
	/**
	 * Close the file descriptor.
	 *
	 * @param fd The file descriptor to close
	 * @param user_data The user_data provided in
	 * ratbag_create_context()
	 */
	void (*close_restricted)(int fd, void *user_data);
};

/**
 * @ingroup base
 *
 * Create a new ratbag context.
 *
 * @return An initialized ratbag context or NULL on error
 */
struct ratbag *
ratbag_create_context(const struct ratbag_interface *interface,
			 void *userdata);

/**
 * @ingroup base
 *
 * Add a reference to the context. A context is destroyed whenever the
 * reference count reaches 0. See @ref ratbag_unref.
 *
 * @param ratbag A previously initialized valid ratbag context
 * @return The passed ratbag context
 */
struct ratbag *
ratbag_ref(struct ratbag *ratbag);

/**
 * @ingroup base
 *
 * Dereference the ratbag context. After this, the context may have been
 * destroyed, if the last reference was dereferenced. If so, the context is
 * invalid and may not be interacted with.
 *
 * @param ratbag A previously initialized ratbag context
 * @return NULL if context was destroyed otherwise the passed context
 */
struct ratbag *
ratbag_unref(struct ratbag *ratbag);

/**
 * @ingroup base
 *
 * Create a new ratbag context from the given udev device.
 */
struct ratbag_device*
ratbag_device_new_from_udev_device(struct ratbag *ratbag,
				   struct udev_device *device);

/**
 * @ingroup base
 *
 * @param ratbag A previously initialized ratbag context
 * @return The name of the device associated with the given ratbag.
 */
const char *
ratbag_device_get_name(const struct ratbag_device* ratbag);

/**
 * @ingroup device
 */
enum ratbag_capability {
	RATBAG_CAP_NONE = 0,
	/**
	 * The device can change resolution, either software-controlled or
	 * by a hardware button.
	 *
	 * FIXME: what about devices that only have hw buttons? can we
	 * query that, even if we can't switch it ourselves? Maybe better to
	 * have a separate cap for that then.
	 */
	RATBAG_CAP_SWITCHABLE_RESOLUTION,
	/**
	 * The device can switch between hardware profiles.
	 * A device with this capability can store multiple profiles in the
	 * hardware and provides the ability to switch between the profiles,
	 * possibly with a button.
	 * Devices without this capability will only have a single profile.
	 */
	RATBAG_CAP_SWITCHABLE_PROFILE,

	/**
	 * The device supports assigning button numbers, key events or key +
	 * modifier combinations.
	 */
	RATBAG_CAP_BUTTON_KEY,

	/**
	 * The device supports user-defined key or button sequences.
	 */
	RATBAG_CAP_BUTTON_MACROS,
};

/**
 * @ingroup device
 *
 * @retval 1 The context has the capability
 * @retval 0 The context does not have the capability
 */
int
ratbag_device_has_capability(const struct ratbag_device *ratbag, enum ratbag_capability cap);

/**
 * @ingroup device
 *
 * Return the number of profiles supported by this device.
 *
 * Note that the number of profiles available may be different to the number
 * of profiles currently active. This function returns the maximum number of
 * profiles available and is static for the lifetime of the device.
 *
 * A device that does not support profiles in hardware provides a single
 * profile that reflects the current settings of the device.
 *
 * @param ratbag A previously initialized ratbag context
 * @return The number of profiles available on this device.
 */
unsigned int
ratbag_device_get_num_profiles(struct ratbag_device *ratbag);

/**
 * @ingroup device
 *
 * Return the number of buttons available on this device.
 *
 * @param ratbag A previously initialized ratbag context
 * @return The number of buttons available on this device.
 */
unsigned int
ratbag_device_get_num_buttons(struct ratbag_device *ratbag);

/**
 * @ingroup device
 *
 * This function creates if necessary and returns a profile for the given
 * index. The index must be less than the number returned by
 * ratbag_get_num_profiles().
 *
 * The profile is refcounted with an initial value of at least 1.
 * Use ratbag_profile_unref() to release the profile.
 *
 * @param ratbag A previously initialized ratbag context
 * @param index The index of the profile
 *
 * @return The profile at the given index, or NULL if the profile does not
 * exist.
 *
 * @see ratbag_get_num_profiles
 */
struct ratbag_profile *
ratbag_device_get_profile_by_index(struct ratbag_device *ratbag, unsigned int index);

/**
 * @ingroup device
 *
 * This function returns the currently active profile. Note that some
 * devices allow switching profiles with hardware buttons thus making the
 * use of this function racy.
 *
 * The profile is refcounted with an initial value of at least 1.
 * Use ratbag_profile_unref() to release the profile.
 *
 * @param ratbag A previously initialized ratbag context
 *
 * @return The profile currently active on the device.
 */
struct ratbag_profile *
ratbag_device_get_active_profile(struct ratbag_device *ratbag);

/**
 * @ingroup device
 *
 * This function sets the currently active profile to the one provided.
 *
 * @param profile The profile to make the active profile.
 *
 * @return 0 on success or nonzero otherwise.
 */
int
ratbag_device_set_active_profile(struct ratbag_profile *profile);

/**
 * @ingroup device
 *
 * @param profile A previously initialized ratbag profile
 * @return The current resolution in dots-per-inch.
 * @retval 0 The resolution is unknown
 */
int
ratbag_profile_get_resolution_dpi(const struct ratbag_profile *profile);

/**
 * @ingroup device
 *
 * Sets the current resolution to the given dpi.
 *
 * @note If the profile is the currently active profile, this may change the
 * devices' behavior immediately.
 *
 * @param profile A previously initialized ratbag profile
 * @param dpi The new resolution in dots-per-inch
 *
 * @return 0 on success or -1 on failure
 */
int
ratbag_profile_set_resolution_dpi(struct ratbag_profile *profile, int dpi);

/**
 * @ingroup device
 *
 * @param profile A previously initialized ratbag profile
 * @return The current report rate in Hz
 * @retval 0 The report rate is unknown
 */
int
ratbag_profile_get_report_rate_hz(const struct ratbag_profile *profile);

/**
 * @ingroup device
 *
 * Sets the current report rate to the given frequency in Hz.
 *
 * @note If the profile is the currently active profile, this may change the
 * devices' behavior immediately.
 *
 * @param profile A previously initialized ratbag profile
 * @param hz The new report rate in Hz
 *
 * @return 0 on success or -1 on failure
 */
int
ratbag_profile_set_report_rate_hz(struct ratbag_profile *profile, int hz);

/**
 * @ingroup device
 *
 * Return a reference to the button given by the index. The order of the
 * buttons is device-specific though indices 0, 1 and 2 should always refer
 * to left, middle, right buttons.
 *
 * The button is refcounted with an initial value of at least 1.
 * Use ratbag_button_unref() to release the button.
 *
 * @param profile A previously initialized ratbag profile
 * @param index The index of the button
 *
 * @return A button context, or NULL if the button does not exist.
 *
 * @see ratbag_get_num_buttons
 */
struct ratbag_button*
ratbag_profile_get_button_by_index(struct ratbag_profile *profile,
				   unsigned int index);

enum ratbag_button_type {
	RATBAG_BUTTON_TYPE_UNKNOWN = 0,

	/* mouse buttons */
	RATBAG_BUTTON_TYPE_LEFT,
	RATBAG_BUTTON_TYPE_MIDDLE,
	RATBAG_BUTTON_TYPE_RIGHT,
	RATBAG_BUTTON_TYPE_THUMB,
	RATBAG_BUTTON_TYPE_THUMB2,
	RATBAG_BUTTON_TYPE_THUMB3,
	RATBAG_BUTTON_TYPE_THUMB4,
	RATBAG_BUTTON_TYPE_WHEEL_LEFT,
	RATBAG_BUTTON_TYPE_WHEEL_RIGHT,
	RATBAG_BUTTON_TYPE_WHEEL_CLICK, /* FIXME: same as middle click? */
	RATBAG_BUTTON_TYPE_WHEEL_UP,
	RATBAG_BUTTON_TYPE_WHEEL_DOWN,
	RATBAG_BUTTON_TYPE_EXTRA,
	RATBAG_BUTTON_TYPE_SIDE,

	/* DPI switch */
	RATBAG_BUTTON_TYPE_RESOLUTION_CYCLE_UP,
	RATBAG_BUTTON_TYPE_RESOLUTION_UP,
	RATBAG_BUTTON_TYPE_RESOLUTION_DOWN,

	/* Profile */
	RATBAG_BUTTON_TYPE_PROFILE_CYCLE_UP,
	RATBAG_BUTTON_TYPE_PROFILE_UP,
	RATBAG_BUTTON_TYPE_PROFILE_DOWN,

	/* Macro */
	RATBAG_BUTTON_TYPE_MACRO,

	/* multimedia */
	RATBAG_BUTTON_TYPE_KEY_CONFIG,
	RATBAG_BUTTON_TYPE_KEY_PREVIOUSSONG,
	RATBAG_BUTTON_TYPE_KEY_NEXTSONG,
	RATBAG_BUTTON_TYPE_KEY_PLAYPAUSE,
	RATBAG_BUTTON_TYPE_KEY_STOPCD,
	RATBAG_BUTTON_TYPE_KEY_MUTE,
	RATBAG_BUTTON_TYPE_KEY_VOLUMEUP,
	RATBAG_BUTTON_TYPE_KEY_VOLUMEDOWN,

	/* desktop */
	RATBAG_BUTTON_TYPE_KEY_CALC,
	RATBAG_BUTTON_TYPE_KEY_MAIL,
	RATBAG_BUTTON_TYPE_KEY_BOOKMARKS,
	RATBAG_BUTTON_TYPE_KEY_FORWARD,
	RATBAG_BUTTON_TYPE_KEY_BACK,
	RATBAG_BUTTON_TYPE_KEY_STOP,
	RATBAG_BUTTON_TYPE_KEY_FILE,
	RATBAG_BUTTON_TYPE_KEY_REFRESH,
	RATBAG_BUTTON_TYPE_KEY_HOMEPAGE,
	RATBAG_BUTTON_TYPE_KEY_SEARCH,

	/* disabled button */
	RATBAG_BUTTON_TYPE_NONE,
};

/**
 * @ingroup device
 *
 * Return the type of the physical button. This function is intended to be
 * used by configuration tools to provide a generic list of button names or
 * handles to configure devices. The type describes the physical location of
 * the button and remains constant for the lifetime of the device.
 *
 * For the button currently mapped to this physical button, see
 * ratbag_button_get_button()
 *
 * @return The type of the button
 */
enum ratbag_button_type
ratbag_button_get_type(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * Change the type of the physical button. This function is intended to be
 * used by configuration tools to configure devices.
 *
 * FIXME: if we do this with linux/input.h instead, we need to add things like
 * BTN_RESOLUTION_UP/DOWN there. But this is supposed to describe the
 * physical location of the button, so input.h is not a good match.
 *
 * @return The error code or 0 on success.
 */
int
ratbag_button_set_type(struct ratbag_button *button, enum ratbag_button_type type);

/**
 * @ingroup device
 *
 * The type assigned to a button.
 */
enum ratbag_button_action_type {
	/**
	 * Button action is unknown
	 */
	RATBAG_BUTTON_ACTION_TYPE_UNKNOWN = -1,
	/**
	 * Button is disabled
	 */
	RATBAG_BUTTON_ACTION_TYPE_NONE = 0,
	/**
	 * Button sends numeric button events
	 */
	RATBAG_BUTTON_ACTION_TYPE_BUTTON,
	/**
	 * Button sends a key or key + modifier combination
	 */
	RATBAG_BUTTON_ACTION_TYPE_KEY,
	/**
	 * Button sends a user-defined key or button sequence
	 */
	RATBAG_BUTTON_ACTION_TYPE_MACRO,
};

/**
 * @ingroup device
 *
 * @return The type of the action currently configured for this button
 */
enum ratbag_button_action_type
ratbag_button_get_action_type(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * If the button's action type is not @ref RATBAG_BUTTON_ACTION_TYPE_BUTTON,
 * this function returns 0.
 *
 * @return The button number this button sends, in one of BTN_*, as defined
 * in linux/input.h.
 * @retval 0 This button is disabled or its action type is not @ref
 * RATBAG_BUTTON_ACTION_TYPE_BUTTON.
 */
unsigned int
ratbag_button_get_button(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * @param button A previously initialized ratbag button
 * @param btn The button number to assign to this button, one of BTN_* as
 * defined in linux/input.h
 * @return 0 on success or nonzero otherwise. On success, the button's
 * action is set to @ref RATBAG_BUTTON_ACTION_TYPE_BUTTON.
 */
int
ratbag_button_set_button(struct ratbag_button *button,
			 unsigned int btn);

/**
 * @ingroup device
 *
 * Return the key or button configured for this button.
 *
 * If the button's action type is not @ref RATBAG_BUTTON_ACTION_TYPE_KEY,
 * this function returns 0 and leaves modifiers and sz untouched.
 *
 * @param button A previously initialized ratbag button
 * @param[out] modifiers Will be filled with the modifiers required for this
 * action. The modifiers are as defined in linux/input.h.
 * @param[in,out] sz Takes the size of the modifiers array and returns the
 * number of modifiers filled in. sz may be 0 if no modifiers are required.
 *
 * @note The caller must ensure that modifiers is large enough to accomodate
 * for the key combination.
 *
 * @return The button number
 */
unsigned int
ratbag_button_get_key(struct ratbag_button *button,
		      unsigned int *modifiers,
		      size_t *sz);

/**
 * @ingroup device
 *
 * @param button A previously initialized ratbag button
 * @param btn The button number to assign to this button, one of BTN_* as
 * defined in linux/input.h
 * @param modifiers The modifiers required for this action. The
 * modifiers are as defined in linux/input.h, in the order they should be
 * pressed.
 * @param sz The size of the modifiers array. sz may be 0 if no modifiers
 * are required.
 *
 * @return 0 on success or nonzero otherwise. On success, the button's
 * action is set to @ref RATBAG_BUTTON_ACTION_TYPE_KEY.
 */
int
ratbag_button_set_key(struct ratbag_button *button,
		      unsigned int btn,
		      unsigned int *modifiers,
		      size_t sz);

/**
 * @ingroup device
 *
 * FIXME: no idea at this point
 */
unsigned int
ratbag_button_get_macro(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * FIXME: no idea at this point
 */
unsigned int
ratbag_button_set_macro(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * Add a reference to the button. A button is destroyed whenever the
 * reference count reaches 0. See @ref ratbag_button_unref.
 *
 * @param button A previously initialized valid ratbag button
 * @return The passed ratbag button
 */
struct ratbag_button *
ratbag_button_ref(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * Dereference the ratbag button. After this, the button may have been
 * destroyed, if the last reference was dereferenced. If so, the button is
 * invalid and may not be interacted with.
 *
 * @param button A previously initialized ratbag button
 * @return NULL if context was destroyed otherwise the passed button
 */
struct ratbag_button *
ratbag_button_unref(struct ratbag_button *button);

/**
 * @ingroup device
 *
 * Add a reference to the profile. A profile is destroyed whenever the
 * reference count reaches 0. See @ref ratbag_profile_unref.
 *
 * @param profile A previously initialized valid ratbag profile
 * @return The passed ratbag profile
 */
struct ratbag_profile *
ratbag_profile_ref(struct ratbag_profile *profile);

/**
 * @ingroup device
 *
 * Dereference the ratbag profile. After this, the profile may have been
 * destroyed, if the last reference was dereferenced. If so, the profile is
 * invalid and may not be interacted with.
 *
 * @param profile A previously initialized ratbag profile
 * @return NULL if context was destroyed otherwise the passed profile
 */
struct ratbag_profile *
ratbag_profile_unref(struct ratbag_profile *profile);

/**
 * @ingroup base
 *
 * Add a reference to the context. A context is destroyed whenever the
 * reference count reaches 0. See @ref ratbag_device_unref.
 *
 * @param ratbag A previously initialized valid ratbag context
 * @return The passed ratbag context
 */
struct ratbag_device *
ratbag_device_ref(struct ratbag_device *ratbag);

/**
 * @ingroup base
 *
 * Dereference the ratbag context. After this, the context may have been
 * destroyed, if the last reference was dereferenced. If so, the context is
 * invalid and may not be interacted with.
 *
 * @param ratbag A previously initialized ratbag context
 * @return NULL if context was destroyed otherwise the passed context
 */
struct ratbag_device *
ratbag_device_unref(struct ratbag_device *ratbag);

/**
 * @ingroup base
 *
 * Set the log priority for the ratbag context. Messages with priorities
 * equal to or higher than the argument will be printed to the context's
 * log handler.
 *
 * The default log priority is @ref RATBAG_LOG_PRIORITY_ERROR.
 *
 * @param ratbag A previously initialized ratbag context
 * @param priority The minimum priority of log messages to print.
 *
 * @see ratbag_log_set_handler
 * @see ratbag_log_get_priority
 */
void
ratbag_log_set_priority(struct ratbag *ratbag,
			enum ratbag_log_priority priority);

/**
 * @ingroup base
 *
 * Get the context's log priority. Messages with priorities equal to or
 * higher than the argument will be printed to the current log handler.
 *
 * The default log priority is @ref RATBAG_LOG_PRIORITY_ERROR.
 *
 * @param ratbag A previously initialized ratbag context
 * @return The minimum priority of log messages to print.
 *
 * @see ratbag_log_set_handler
 * @see ratbag_log_set_priority
 */
enum ratbag_log_priority
ratbag_log_get_priority(const struct ratbag *ratbag);

/**
 * @ingroup base
 *
 * Set the context's log handler. Messages with priorities equal to or
 * higher than the context's log priority will be passed to the given
 * log handler.
 *
 * The default log handler prints to stderr.
 *
 * @param ratbag A previously initialized ratbag context
 * @param log_handler The log handler for library messages.
 *
 * @see ratbag_log_set_priority
 * @see ratbag_log_get_priority
 */
void
ratbag_log_set_handler(struct ratbag *ratbag,
		       ratbag_log_handler log_handler);

#ifdef __cplusplus
}
#endif
#endif /* LIBRATBAG_H */
