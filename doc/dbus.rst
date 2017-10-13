********
DBus API
********

Note: **the DBus interface is subject to change**

Interfaces:

*  org.freedesktop.ratbag1.Manager
*  org.freedesktop.ratbag1.Device
*  org.freedesktop.ratbag1.Profile
*  org.freedesktop.ratbag1.Resolution
*  org.freedesktop.ratbag1.Button
*  org.freedesktop.ratbag1.Led

For a list of dbus types as used below, see https://dbus.freedesktop.org/doc/dbus-specification.html

Properties marked as **constant** do not change for the lifetime of the
object. Properties marked as **mutable** may change, and a
``org.freedesktop.DBus.Properties.PropertyChanged`` signal is sent for those
unless otherwise specified.

All setters (whether implicit for read-write properties or explicit) return
success instead of the internal return value because DBus does not like
non-standard error values. Upon a ratbag error, the `Resync` signal is emitted
on the device, indicating that clients should resync their values.

Values listed as enums are defined in `libratbag-enums.h <https://github.com/libratbag/libratbag/blob/master/src/libratbag-enums.h>`_

org.freedesktop.ratbag1.Manager
-------------------------------

.. js:class:: Manager

    The **org.freedesktop.ratbag1.Manager** interface is the entry point to
    interact with ratbagd.

    .. js:attribute:: Devices

        :type: ao

        An array of read-only **object paths** to :js:class:`Device` objects

    .. js:attribute:: Themes

        :type: as

        :flags: read-only, constant

        Provides the list of available theme names. This list is guaranteed to have
        one theme available ('default'). Other themes are implementation defined.
        A theme listed here is only a guarantee that the theme is known to libratbag
        and that SVGs *may* exist, it is not a guarantee that the SVG for any
        specific device exists. In other words, a device may not have an SVG for a
        specific theme.

        This list is static for the lifetime of ratbagd.

org.freedesktop.ratbag1.Device
-------------------------------

.. js:class:: Device

    The **org.freedesktop.ratbag1.Device** interface describes a single device
    known to ratbagd.

    .. js:attribute:: Id

        :type: as

        :flags: read-only, constant

        An ID describing this device. This ID should not be used for presentation to
        the user. This ID is unique for the device for the lifetime of the device
        but may be recycled when the device is removed. No guarantee is given for
        the content of the ID, the client should treat it as an opaque string.

    .. js:attribute:: Description

        :type: s
        :flags: read-only, constant

        The device's name, suitable for presentation to the user.

    .. js:attribute:: Capabilities

        :type: au
        :flags: read-only, constant

        The capabilities supported by this device. see `enum
        ratbag_device_capability` in libratbag-enums.h for the list of permissible
        capabilities.


    .. js:attribute:: Svg

        :type: s
        :flags: read-only, constant

        The device's SVG file name, without path.

    .. js:attribute:: Profiles

        :type: ao
        :flags: read-only, mutable

        This property is mutable if the device supports adding and removing
        profiles.

        Provides the list of profile paths for all profiles on this device, see
        :js:class:`Profile`.

    .. js:function:: Commit() → ()

        Commits the changes to the device. Changes to the device are batched; they
        are not written to the hardware until :js:func:`Commit()` is invoked.

    .. js:function:: GetSvg(s) → (s)

        :param s: the theme name
        :returns: A full path to the SVG for the given theme

        Returns the full path to the SVG for the given theme or an
        empty string if none is available.  The theme must be one of
        :js:attr:`Themes`.

        The theme **'default'** is guaranteed to be available.
        ratbagd may return the path to a file that doesn't exist.
        This is the case if the device has SVGs available but not
        for the given theme.

    .. js:function:: Resync()

        :type: Signal

        Emitted when an internal error occurs. Upon receiving this
        signal, clients are expected to resync their values with
        ratbagd.


org.freedesktop.ratbag1.Profile
-------------------------------

.. js:class:: Profile

    .. js:attribute:: Index

        :type: u
        :flags: read-only, constant

        The index of this profile

    .. js:attribute:: Name

        :type: s
        :flags: read-write, mutable

        The name of this profile.

    .. js:attribute:: Enabled

        :type: b
        :flags: read-write, mutable

        True if this is the profile is enabled, false otherwise.

        Note that a disabled profile might not have correct bindings, so it's
        a good thing to rebind everything before calling :js:func:`Commit()` on the
        :js:class:`Device`.

    .. js:attribute:: Resolutions

        :type: ao
        :flags: read-only, mutable

        This property is mutable if the device supports adding and removing
        resolutions.

        Provides the object paths of all resolutions in this profile, see
        :js:class:`Resolution`

    .. js:attribute:: Buttons

        :type: ao
        :flags: read-only, constant

        Provides the object paths of all buttons in this profile, see
        :js:class:`Button`

    .. js:attribute:: Leds

        :type: ao
        :flags: read-only, constant

        Provides the object paths of all LEDs in this profile, see
        :js:class:`Led`

    .. js:attribute:: IsActive

        :type: b
        :flags: read-only, mutable

        True if this is the currently active profile, false otherwise.

        Profiles can only be set to active, but never to not active - at least one
        profile must be active at all times. This property is read-only, use the
        :js:func:`SetActive()` method to activate a profile.

    .. js:function:: SetActive() → ()

        Set this profile to be the active profile

org.freedesktop.ratbag1.Resolution
----------------------------------

.. js:class:: Resolution

    .. js:attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the resolution

    .. js:attribute:: Capabilities

        :type: au
        :flags: read-only, constant

        Array of uints from the ratbag\_resolution\_capability from libratbag.h.

    .. js:attribute:: IsActive

        :type: b
        :flags: read-only, mutable

        True if this is the currently active resolution, false otherwise.

        Resolutions can only be set to active, but never to not
        active - at least one resoultion must be active at all
        times. This property is read-only, use the
        :js:func:`SetActive()` method to set a resolution as the
        active resolution.

    .. js:attribute:: IsDefault

        :type: b
        :flags: read-only, mutable

        True if this is the currently default resolution, false
        otherwise. If the device does not have the default
        resolution capability, this property is always false.

        Resolutions can only be set to default, but never to not
        default - at least one resolution must be default at all
        times. This property is read-only, use the
        :js:func:`SetDefault()` method to set a resolution as
        the default resolution.

    .. js:attribute:: Resolution

        :type: uu
        :flags: read-write, mutable

        uint for the x and y resolution assigned to this entry,
        respectively.  The value for the resolution must be equal to
        one of the values in :js:attr:`Resolutions`.

        If the resolution does not support separate x/y resolutions,
        x and y must be the same value.

    .. js:attribute:: Resolutions

        :type: au
        :flags: read-only, constant

        A list of permitted resolutions. Values in this list may be used in
        the :js:attr:`Resolution` property. This list is always sorted
        ascending, the lowest resolution is the first item in the list.

    .. js:attribute:: ReportRate

        :type: u
        :flags: read-write, mutable

        uint for the report rate in Hz assigned to this entry

        If the resolution does not have the individual report rate
        capability, changing the report rate on one resolution will
        change the report rate on all resolutions.

    .. js:function:: SetDefault() → ()

        Set this resolution to be the default

    .. js:function:: SetActive() → ()

        Set this resolution to be the active one

org.freedesktop.ratbag1.Button
------------------------------

.. js:class:: Button

    .. js:attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the button

    .. js:attribute:: Type

        :type: u
        :flags: read-only, constant

        Enum describing the button physical type, see
        :cpp:enum:`ratbag_button_type`. This type is unrelated to the
        logical button mapping and serves to easily identify the button on
        the device.

    .. js:attribute:: ButtonMapping

        :type: u
        :flags: read-write, mutable

        uint of the current button mapping (if mapping to button)

    .. js:attribute:: SpecialMapping

        :type: u
        :flags: read-write, mutable

        Enum describing the current special mapping (if mapped to special)

    .. js:attribute:: Macro

        :type: a(uu)
        :flags: read-write, mutable

        Array of (type, keycode), where type may be one of
        :cpp:enumerator:`RATBAG_MACRO_EVENT_KEY_PRESSED` or
        :cpp:enumerator:`RATBAG_MACRO_EVENT_KEY_RELEASED`.

    .. js:attribute:: ActionType

        :type: u
        :flags: read-only, mutable

        An enum describing the action type of the button, see
        :cpp:enum:`ratbag_button_action_type` for the list of enums.
        This decides which one of :js:attr:`ButtonMapping`,
	:js:attr:`SpecialMapping` and :js:attr:`Macro` has a value.

    .. js:attribute:: ActionTypes

        :type: au
        :flags: read-only, constant

        Array of :cpp:enum:`ratbag_button_action_type`, possible values
        for ActionType on the current device

    .. js:function:: Disable() → ()

        Disable this button

org.freedesktop.ratbag1.Led
---------------------------

.. js:class:: Led

    .. js:attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the LED

    .. js:attribute:: Mode

        :type: u
        :flags: read-write, mutable

        uint mapping to the mode enum from libratbag

    .. js:attribute:: Type

        :type: u
        :flags: read-only, mutable

        enum describing the LED type

    .. js:attribute:: Color

        :type: (uuu)
        :flags: read-write, mutable

        uint triplet (RGB) of the LED's color

    .. js:attribute:: ColorDepth

        :type: u
        :flags: read-only, constant

        The color depth of this LED as one of the constants in libratbag-enums.h

    .. js:attribute:: EffectRate

        :type: u
        :flags: read-write, mutable

        The effect rate in Hz, possible values are in the range 100 - 20000

    .. js:attribute:: Brightness

        :type: u
        :flags: read-write, mutable

        The brightness of the LED, possible values are in the range 0 - 255

For easier debugging, objects paths are constructed from the device. e.g.
`/org/freedesktop/ratbag/button/event5/p0/b10` is the button interface for
button 10 on profile 0 on event5. The naming is subject to change. Do not
rely on a constructed object path in your application.
