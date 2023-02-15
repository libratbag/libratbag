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
#. Optional: if an error occurred writing the new data to the device,
   a :func:`org.freedesktop.ratbag1.Resync <Resync>` signal is emitted on the device and
   all properties are updated accordingly.

The time it takes to write settings to a device varies greatly, any caller
must be prepared to receive a :func:`org.freedesktop.ratbag1.Device.Resync
<Resync>` signal several seconds after
:func:`org.freedesktop.ratbag1.Device.Commit() <Commit>`.

Notes on the DBus API
---------------------

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

.. attribute:: APIVersion

        :type: i
        :flags: read-only,constant

        The DBus API version. This is a stopgap feature while the DBus API
        is still in flux. The version has no backwards or
        forward-compatibility guarantees - your client must understand
        **exactly** the same version, it is not enough to support an older
        or a newer version.

        This API will be removed once the DBus API is declared stable.

.. attribute:: Devices

        :type: ao
        :flags: read-only, mutable

        An array of read-only object paths referencing the available
        devices. The devices implement the :ref:`device` interface.

.. _device:

org.freedesktop.ratbag1.Device
-------------------------------

The **org.freedesktop.ratbag1.Device** interface describes a single device
known to ratbagd.

.. attribute:: Model

        :type: s
        :flags: read-only, constant

        An ID identifying the physical device model. This string is
        guaranteed to be unique for a specific model and always identical
        for devices of that model.

        This is a string of one of the following formats:

        - ``usb:1234:abcd:0``
        - ``bluetooth:5678:ef01:0``
        - ``unknown``

        In the future, other formats may get added. Clients must ignore
        unknown string formats.

        For a string starting with ``usb:``, the format is the bus type (USB)
        followed by a 4-digit lowercase hex USB vendor ID, followed by a
        4-digit lowercase hex USB product ID, followed by an decimal version
        number of unspecified length. These four elements are separated by a
        colon (``:``).

        For a string starting with ``bluetooth:``, the format is the bus type
        (Bluetooth) followed by a 4-digit lowercase hex Bluetooth vendor ID,
        followed by a 4-digit lowercase hex Bluetooth product ID, followed
        by an decimal version number of unspecified length. These four
        elements are separated by a colon (``:``).

        For the string ``unknown``, the model of the device cannot be
        determined. This is usually a bug in libratbag.

        For a ``Model`` of type ``usb`` and ``bluetooth``, the version
        number is reserved for use by libratbag. Device with identical
        vendor and product IDs but different versions must be considered
        different devices. For example, the version may increase when a
        manufacturer re-uses USB Ids.

        Vendor or product IDs of 0 are valid IDs (e.g. used used by test
        devices).

.. attribute:: Name

        :type: s
        :flags: read-only, constant

        The device's name, suitable for presentation to the user.

.. attribute:: FirmwareVersion

        :type: s
        :flags: read-only, constant

        A device-specific string with the firmware version, or the empty
        string. For devices with a major/minor or purely numeric firmware
        version, the conversion into a string is implementation-defined.

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

        The name of this profile. If the name is the empty string, the
        profile name cannot be changed.

.. attribute:: Disabled

        :type: b
        :flags: read-write, mutable

        True if this is the profile is disabled, false otherwise.

        Note that a disabled profile might not have correct bindings, so it's
        a good thing to rebind everything before calling
        :func:`Commit`.

.. attribute:: IsActive

        :type: b
        :flags: read-only, mutable

        True if this is the currently active profile, false otherwise.

        Profiles can only be set to active, but never to not active - at least one
        profile must be active at all times. This property is read-only, use the
        :func:`SetActive` method to activate a profile.

.. attribute:: IsDirty

        :type: b
        :flags: read-only, mutable

        True if this profile is dirty, false otherwise.

        Dirty, that is, the device needs committing after being configured.
        This property becomes `true` whenever any other property of the
        profile is updated.

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

.. attribute:: AngleSnapping

        :type: i
        :flags: read-write, mutable

        Sensor angle snapping boolean value as an int (1 or 0), or `-1` to
        indicate that the device doesn't support reading and/or writing this
        value.

.. attribute:: Debounce

        :type: i
        :flags: read-write, mutable

        Int for the button debounce time in milliseconds assigned to this
        profile. This time must be one of those listed in
        :attr:`Debounces`, or `-1` to indicate that changing debounce time
        for this device is not allowed.

.. attribute:: Debounces

        :type: au
        :flags: read-write, constant

        A list of permitted button debounce times. Values in this list may
        be used in the :attr:`Debounce` property. This list is always sorted
        ascending, the lowest debounce time is the first item in the list.

        This list may be empty if the device does not support reading and/or
        writing the debounce time.

.. attribute:: ReportRate

        :type: u
        :flags: read-write, mutable

        uint for the report rate in Hz assigned to this profile. This rate
        must be one of those listed in :attr:`ReportRates`.

.. attribute:: ReportRates

        :type: au
        :flags: read-write, constant

        A list of permitted report rates. Values in this list may be used
        in the :attr:`ReportRate` property. This list is always sorted
        ascending, the lowest report rate is the first item in the list.

        This list may be empty if the device does not support reading and/or
        writing the report rate.

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

        Bit-mask of capabilities.

        +-------+-----------------------------------------------------+
        | Value | Definition                                          |
        +=======+=====================================================+
        |   1   | Resolution can be set for x and y axes separately.  |
        +-------+-----------------------------------------------------+
        |   2   | Resolution can be disabled and enabled.             |
        |       | Note that this capability only notes the general    |
        |       | capability. A specific resolution may still fail to |
        |       | be disabled, e.g. when it is the active resolution  |
        |       | on the device.                                      |
        +-------+-----------------------------------------------------+

        (Source: :cpp:enum:`ratbag_resolution_capability` in `libratbag.h`).

        In the future, extra values may get added. Clients must ignore
        unknown Capabilities.

.. attribute:: IsActive

        :type: b
        :flags: read-only, mutable

        True if this is the currently active resolution, false otherwise.

        Resolutions can only be set to active, but never to not
        active - at least one resolution must be active at all
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

.. attribute:: IsDisabled

        :type: b
        :flags: read-write, mutable

        True if this resolution is disabled, false otherwise.
        If the device does not have the disabled resolution
        capability, this property is always false.

.. attribute:: Resolution

        :type: v
        :flags: read-write, mutable

        The resolution for this entry in dpi.

        If the variant is a single unsigned integer (``u``), the value is
        the resolution for both the x- and the y- axis.

        If the variant is a unsigned integer tuple (``(uu)``), the value is
        the resolution for the x- and y- axis separately.

        A client must leave the type intact, assigning a single ``u`` to a
        resolution object previously exporting ``(uu)`` is invalid.

        The value for the resolution must be equal to one of the values in
        :attr:`Resolutions`.

.. attribute:: Resolutions

        :type: au
        :flags: read-only, constant

        A list of permitted resolutions. Values in this list may be used in
        the :attr:`Resolution` property. This list is always sorted
        ascending, the lowest resolution is the first item in the list.

        This list may be empty if the device does not support reading and/or
        writing to resolutions.

.. function:: SetActive() → ()

        Set this resolution to be the active one

.. function:: SetDefault() → ()

        Set this resolution to be the default

.. _button:

org.freedesktop.ratbag1.Button
------------------------------

.. attribute:: Index

        :type: u
        :flags: read-only, constant

        Index of the button

.. attribute:: Mapping

        :type: (uv)
        :flags: read-write, mutable

        The current button mapping. The first element is the ``ActionType``
        for this button and must be one of those in :attr:`ActionTypes`.

        If the ActionType is *Button*, the variant is an unsigned integer
        (``u``) denoting the button number to map to.

        If the ActionType is *Special*, the variant is an unsigned integer
        (``u``) denoting the special value to map to.

        If the ActionType is *Macro*, the variant is an array of integer
        tuples (``a(uu)``) where each tuple is ``(type, keycode)`` and the
        type is one of the following:

        +---------+--------------------------------------+
        | Value   | Description                          |
        +=========+======================================+
        |   0     | Key release event                    |
        +---------+--------------------------------------+
        |   1     | Key press event                      |
        +---------+--------------------------------------+

        If the ActionType is *None*, the variant is an unsigned integer
        (``u``) of value 0.

        If the ActionType is *Unknown*, the variant is an unsigned integer
        (``u``) of value 0.

.. attribute:: ActionTypes

        :type: au
        :flags: read-only, constant

        +---------+---------+--------------------------------------+
        | Value   | Name    | Description                          |
        +=========+=========+======================================+
        |   0     | None    | No mapping configured                |
        +---------+---------+--------------------------------------+
        |   1     | Button  | Mapping to a logical button number   |
        +---------+---------+--------------------------------------+
        |   2     | Special | Mapping to a special function        |
        +---------+---------+--------------------------------------+
        |   3     | Key     | Mapping to a simple key action       |
        +---------+---------+--------------------------------------+
        |   4     | Macro   | Mapping to a macro key sequence      |
        +---------+---------+--------------------------------------+
        | 1000    | Unknown | An unknown or unreadable mapping type|
        +---------+---------+--------------------------------------+

        See :ref:`button_special` for a list of supported special
        functions.

        Clients must ignore :attr:`ActionTypes` unknown to them.

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

        Enum describing the current mode, see :attr:`Modes`.

.. attribute:: Modes

        :type: au
        :flags: read-only, constant

        A list of modes supported by this LED.

        +-------+-------------------------------------+
        | Value | Definition                          |
        +=======+=====================================+
        |   0   | LED is off                          |
        +-------+-------------------------------------+
        |   1   | LED is on with constant brightness  |
        +-------+-------------------------------------+
        |   2   | LED cycles through a set of colors. |
        |       | This mode ignores the :attr:`Color` |
        |       | values.                             |
        +-------+-------------------------------------+
        |   3   | LED uses a breathing-style animation|
        +-------+-------------------------------------+

        In the future, extra values may get added. Clients must ignore
        unknown Modes.

.. attribute:: Color

        :type: (uuu)
        :flags: read-write, mutable

        32-bit unsigned int triplet (RGB) of the LED's color. Only the least
        significant bits are valid, the :attr:`ColorDepth` property defines
        the number of bits for each color. When writing to this property,
        all bits outside the color depth must be 0.

.. attribute:: ColorDepth

        :type: u
        :flags: read-only, constant

        An enum specifying the color depth of this LED. Permitted values are:

        +-------+-------------------------------+
        | Value | Definition                    |
        +=======+===============================+
        |   0   | 0 bits per color (monochrome) |
        +-------+-------------------------------+
        |   1   | 8 bits per color              |
        +-------+-------------------------------+
        |   2   | 1 bit per color               |
        +-------+-------------------------------+

        In the future, extra values may get added. Clients must ignore
        unknown ``ColorDepths`` and not manipulate the LED color where
        the ``ColorDepth`` is unknown.

.. attribute:: EffectDuration

        :type: u
        :flags: read-write, mutable

        The effect duration in ms, possible values are in the range 0 - 10000

.. attribute:: Brightness

        :type: u
        :flags: read-write, mutable

        The brightness of the LED, normalized to the range 0-255, inclusive.
        Where the LED supports less than 8-bit of brightness, libratbag maps
        the value to a device-supported value in an implementation-defined
        manner.


.. _button_special:

Special button functions
------------------------

        All special button function values are based on the value
        ``0x40000000`` (``1 << 30``).

        +------------+------------+---------------------------------------------------+
        | Offset     | Value      |Description                                        |
        +============+============+===================================================+
        |  0         | 0x40000000 | Unknown                                           |
        +------------+------------+---------------------------------------------------+
        |  1         | 0x40000001 | Doublelick                                        |
        +------------+------------+---------------------------------------------------+
        |  2         | 0x40000002 | Wheel left                                        |
        +------------+------------+---------------------------------------------------+
        |  3         | 0x40000003 | Wheel right                                       |
        +------------+------------+---------------------------------------------------+
        |  4         | 0x40000004 | Wheel up                                          |
        +------------+------------+---------------------------------------------------+
        |  5         | 0x40000005 | Wheel down                                        |
        +------------+------------+---------------------------------------------------+
        |  6         | 0x40000006 | Ratchet mode switch                               |
        +------------+------------+---------------------------------------------------+
        |  7         | 0x40000007 | Resolution cycle up                               |
        +------------+------------+---------------------------------------------------+
        |  8         | 0x40000008 | Resolution cycle down                             |
        +------------+------------+---------------------------------------------------+
        |  9         | 0x40000009 | Resolution up                                     |
        +------------+------------+---------------------------------------------------+
        |  10        | 0x4000000a | Resolution down                                   |
        +------------+------------+---------------------------------------------------+
        |  11        | 0x4000000b | Resolution alternate                              |
        +------------+------------+---------------------------------------------------+
        |  12        | 0x4000000c | Resolution default                                |
        +------------+------------+---------------------------------------------------+
        |  13        | 0x4000000d | Profile cycle up                                  |
        +------------+------------+---------------------------------------------------+
        |  14        | 0x4000000e | Profile cycle down                                |
        +------------+------------+---------------------------------------------------+
        |  15        | 0x4000000f | Profile up                                        |
        +------------+------------+---------------------------------------------------+
        |  16        | 0x40000010 | Profile down                                      |
        +------------+------------+---------------------------------------------------+
        |  17        | 0x40000011 | Second mode                                       |
        +------------+------------+---------------------------------------------------+
        |  18        | 0x40000012 | Battery level                                     |
        +------------+------------+---------------------------------------------------+
