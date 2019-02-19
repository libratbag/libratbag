********
DBus API
********

Note: **the DBus interface is subject to change**

Interfaces:

*  :ref:`manager`
*  :ref:`device`
*  :ref:`profile`
*  :ref:`resolution`
*  :ref:`button`
*  :ref:`led`

Changing settings on a device is a three-step process:

#. Change the various properties to the desired value
#. Invoke :func:`org.freedesktop.ratbag1.Device.Commit() <Commit>`
#. Optional: if an error occured writing the new data to the device,
   a :func:`org.freedesktop.ratbag1.Resync <Resync>` signal is emitted on the device and
   all properties are updated accordingly.

The time it takes to write settings to a device varies greatly, any caller
must be prepared to receive a :func:`org.freedesktop.ratbag1.Resync
<Resync>` signal several seconds after
:func:`org.freedesktop.ratbag1.Device.Commit() <Commit>`.

Notes on the DBus API
---------------------
Values listed as enums are defined in `libratbag-enums.h
<https://github.com/libratbag/libratbag/blob/master/src/libratbag-enums.h>`_

For easier debugging, objects paths are constructed from the device. e.g.
``/org/freedesktop/ratbag/button/event5/p0/b10`` is the button interface for
button 10 on profile 0 on event5. The naming is subject to change. Do not
rely on a constructed object path in your application.

Types
.....

Types used by these interfaces:

+----------+-----------------------------------+
| Type     | Description                       |
+==========+===================================+
| ``b``    | Bool                              |
+----------+-----------------------------------+
| ``o``    | Object path                       |
+----------+-----------------------------------+
| ``s``    | String                            |
+----------+-----------------------------------+
| ``u``    | Unsigned 32-bit integer           |
+----------+-----------------------------------+
|``(uuu)`` | A triplet of 32-bit integers      |
+----------+-----------------------------------+
| ``ao``   | Array of object paths             |
+----------+-----------------------------------+
| ``as``   | Array of strings                  |
+----------+-----------------------------------+
| ``au``   | Array of 32-bit integers          |
+----------+-----------------------------------+
| ``a(uu)``| Array of 2 32-bit integer tuples  |
+----------+-----------------------------------+

For details on each type, see the `DBus Specification
<https://dbus.freedesktop.org/doc/dbus-specification.html>`_.

Flags
.....

Properties marked as **constant** do not change for the lifetime of the
object. Properties marked as **mutable** may change, and a
``org.freedesktop.DBus.Properties.PropertyChanged`` signal is sent for those
unless otherwise specified.

.. _manager:

org.freedesktop.ratbag1.Manager
-------------------------------

The **org.freedesktop.ratbag1.Manager** interface is the entry point to
interact with ratbagd.

.. attribute:: Devices

	:type: ao
	:flags: read-only, mutable

	An array of read-only object paths referencing the available
	devices. The devices implement the :ref:`device` interface.

.. attribute:: Themes

        :type: as
        :flags: read-only, constant

	The list of available theme names. This list is guaranteed to have
	at least one theme available ('default'). Other themes are
	implementation-defined. A theme listed here is only a guarantee
	that the theme is known to libratbag and that SVGs *may* exist, it
	is not a guarantee that the SVG for any specific device exists. In
	other words, a device may not have an SVG for a specific theme.

.. _device:

org.freedesktop.ratbag1.Device
-------------------------------

The **org.freedesktop.ratbag1.Device** interface describes a single device
known to ratbagd.

.. attribute:: Id

        :type: as
        :flags: read-only, constant

        A unique ID describing this device. This ID should not be used for presentation to
	the user. This ID may be recycled when the device is removed. The
	content of the ID is undefined, the client should treat it as an
	opaque string.

.. attribute:: Name

        :type: s
        :flags: read-only, constant

        The device's name, suitable for presentation to the user.

.. attribute:: Capabilities

        :type: a(us)
        :flags: read-only, constant

	The capabilities supported by this device. see
	:cpp:enum:`ratbag_device_capability` in libratbag-enums.h for the
	list of permissible capabilities.

	The content of this property is a list of tuples with the
	capability as first entry and a string specifying the modes as second
	entry. Characters allowed in this string are 'r' for read-only, 'w'
	for write-only.

	Future versions of libratbag may add additional modes,
	unknown characters must be ignored by the caller.

.. attribute:: Profiles

        :type: ao
        :flags: read-only, mutable

        This property is mutable if the device supports adding and removing
        profiles.

        Provides the list of profile paths for all profiles on this device, see
	:ref:`profile`

.. function:: Commit() → ()

        Commits the changes to the device. This call always succeeds,
	the data is written to the device asynchronously. Where an error
	occurs, the :func:`Resync` signal is emitted and all properties are
	updated to the current state.

.. function:: GetSvgFd(s) → (h)

        :param s: the theme name
        :returns: An open file descriptor to the SVG for the given theme

        Returns an open file descriptor to the SVG for the given theme or an
        errno on error. The theme must be one of
	:func:`org.freedesktop.ratbag1.Manager.Themes <Themes>`.

        The theme **'default'** is guaranteed to be available.

        ratbagd may return ``ENOENT`` if a file doesn't exist.
        This is the case if the device has SVGs available but not
        for the given theme.

.. function:: Resync()

        :type: Signal

        Emitted when an internal error occurs, usually on writing values to
        the device after :func:`Commit()`. Upon receiving this
        signal, clients are expected to resync their property values with
        ratbagd.


.. _profile:

org.freedesktop.ratbag1.Profile
-------------------------------

.. attribute:: Index

        :type: u
        :flags: read-only, constant

        The zero-based index of this profile

.. attribute:: Name

        :type: s
        :flags: read-write, mutable

        The name of this profile.

.. attribute:: Enabled

        :type: b
        :flags: read-write, mutable

        True if this is the profile is enabled, false otherwise.

        Note that a disabled profile might not have correct bindings, so it's
	a good thing to rebind everything before calling
	:func:`Commit`.

.. attribute:: Resolutions

        :type: ao
        :flags: read-only, mutable

        This property is mutable if the device supports adding and removing
        resolutions.

        Provides the object paths of all resolutions in this profile, see
	:ref:`resolution`.

.. attribute:: Buttons

        :type: ao
        :flags: read-only, constant

        Provides the object paths of all buttons in this profile, see
	:ref:`button`.

.. attribute:: Leds

        :type: ao
        :flags: read-only, constant

        Provides the object paths of all LEDs in this profile, see
	:ref:`led`.

.. attribute:: IsActive

        :type: b
        :flags: read-only, mutable

        True if this is the currently active profile, false otherwise.

        Profiles can only be set to active, but never to not active - at least one
        profile must be active at all times. This property is read-only, use the
        :func:`SetActive` method to activate a profile.

.. function:: SetActive() → ()

        Set this profile to be the active profile

.. _resolution:

org.freedesktop.ratbag1.Resolution
----------------------------------

.. attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the resolution

.. attribute:: Capabilities

        :type: au
        :flags: read-only, constant

	Array of uints from the :cpp:enum:`ratbag_resolution_capability`
	from libratbag.h.

.. attribute:: IsActive

        :type: b
        :flags: read-only, mutable

        True if this is the currently active resolution, false otherwise.

        Resolutions can only be set to active, but never to not
        active - at least one resoultion must be active at all
        times. This property is read-only, use the
        :func:`SetActive` method to set a resolution as the
        active resolution.

.. attribute:: IsDefault

        :type: b
        :flags: read-only, mutable

        True if this is the currently default resolution, false
        otherwise. If the device does not have the default
        resolution capability, this property is always false.

        Resolutions can only be set to default, but never to not
        default - at least one resolution must be default at all
        times. This property is read-only, use the
        :func:`SetDefault` method to set a resolution as
        the default resolution.

.. attribute:: Resolution

        :type: uu
        :flags: read-write, mutable

        uint for the x and y resolution assigned to this entry,
        respectively.  The value for the resolution must be equal to
        one of the values in :attr:`Resolutions`.

        If the resolution does not support separate x/y resolutions,
        x and y must be the same value.

.. attribute:: Resolutions

        :type: au
        :flags: read-only, constant

        A list of permitted resolutions. Values in this list may be used in
        the :attr:`Resolution` property. This list is always sorted
        ascending, the lowest resolution is the first item in the list.

        This list may be empty if the device does not support reading and/or
        writing to resolutions.

.. attribute:: ReportRate

        :type: u
        :flags: read-write, mutable

        uint for the report rate in Hz assigned to this entry

        If the resolution does not have the individual report rate
        capability, changing the report rate on one resolution will
        change the report rate on all resolutions.

.. attribute:: ReportRates

        :type: au
        :flags: read-write, constant

        A list of permitted report rates. Values in this list may be used
        in the :attr:`ReportRate` property. This list is always sorted
        ascending, the lowest report rate is the first item in the list.

        This list may be empty if the device does not support reading and/or
        writing to resolutions.

.. function:: SetDefault() → ()

        Set this resolution to be the default

.. function:: SetActive() → ()

        Set this resolution to be the active one

.. _button:

org.freedesktop.ratbag1.Button
------------------------------

.. attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the button

.. attribute:: Type

        :type: u
        :flags: read-only, constant

        Enum describing the button physical type, see
        :cpp:enum:`ratbag_button_type`. This type is unrelated to the
        logical button mapping and serves to easily identify the button on
        the device.

.. attribute:: ButtonMapping

        :type: u
        :flags: read-write, mutable

        uint of the current button mapping (if mapping to button)

.. attribute:: SpecialMapping

        :type: u
        :flags: read-write, mutable

        Enum describing the current special mapping (if mapped to special)

.. attribute:: Macro

        :type: a(uu)
        :flags: read-write, mutable

        Array of (type, keycode), where type may be one of
        :cpp:enumerator:`RATBAG_MACRO_EVENT_KEY_PRESSED` or
        :cpp:enumerator:`RATBAG_MACRO_EVENT_KEY_RELEASED`.

.. attribute:: ActionType

        :type: u
        :flags: read-only, mutable

        An enum describing the action type of the button, see
        :cpp:enum:`ratbag_button_action_type` for the list of enums.
        This decides which one of :attr:`ButtonMapping`,
	:attr:`SpecialMapping` and :attr:`Macro` has a value.

.. attribute:: ActionTypes

        :type: au
        :flags: read-only, constant

        Array of :cpp:enum:`ratbag_button_action_type`, possible values
        for ActionType on the current device

.. function:: Disable() → ()

        Disable this button

.. _led:

org.freedesktop.ratbag1.Led
---------------------------

.. attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the LED

.. attribute:: Mode

        :type: u
        :flags: read-write, mutable

        Enum describing the current mode, see
        :cpp:enum:`ratbag_led_mode`.

.. attribute:: Modes

        :type: au
        :flags: read-only, constant

        A list of modes supported by this LED, see
        :cpp:enum:`ratbag_led_mode`.

.. attribute:: Type

        :type: u
        :flags: read-only, mutable

        The LED type, see :cpp:enum:`ratbag_led_type`.

.. attribute:: Color

        :type: (uuu)
        :flags: read-write, mutable

        uint triplet (RGB) of the LED's color

.. attribute:: ColorDepth

        :type: u
        :flags: read-only, constant

        The color depth of this LED, see :cpp:enum:`ratbag_led_colordepth`.

.. attribute:: EffectDuration

        :type: u
        :flags: read-write, mutable

        The effect duration in ms, possible values are in the range 0 - 10000

.. attribute:: Brightness

        :type: u
        :flags: read-write, mutable

        The brightness of the LED, possible values are in the range 0 - 255

