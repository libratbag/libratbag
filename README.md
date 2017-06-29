libratbag
=========

libratbag is a configuration library for gaming mice. It provides a generic
way to access the various features exposed by these mice and abstracts away
hardware-specific and kernel-specific quirks.

libratbag exports the devices over a DBus daemon called ratbagd. ratbagd
needs permissions to access device nodes (primarily /dev/hidraw nodes) and
usually needs to run as root.

Running ratbagd
---------------

ratbagd is intended to run as dbus-activated systemd service. This requires
installation of the following files:

    sudo cp dbus/org.freedesktop.ratbag1.conf /etc/dbus-1/system.d/org.freedesktop.ratbag1.conf
    sudo cp builddir/org.freedesktop.ratbag1.service /etc/dbus-1/system-services/org.freedesktop.ratbag1.conf
    sudo cp builddir/ratbagd.service /etc/systemd/system/ratbagd.service

The files are installed into the prefix by `ninja install`, see also the
configure-time options `-Dsystemd-unit-dir` and `-Ddbus-root-dir` (see
"Compiling ratbagd" below). Developers are encouraged to simply symlink to the
files in the git repository. If you used any other build directory than
`builddir`, adjust accordingly.

For the files to take effect, you should run

    sudo systemctl daemon-reload
    sudo systemctl reload dbus.service

And finally, to enable the service:

    sudo systemctl enable ratbagd.service

This places the required symlink into the systemd directory so that dbus
activation is possible.

Compiling ratbagd
-----------------

ratbagd uses the meson build system (see http://mesonbuild.com) which in
turn uses ninja to invoke the compiler (`ninja` may be `ninja-build` on your
distribution). From a fresh git checkout, run the following commands to init
the repository:

    meson builddir --prefix=/usr/

And to build or re-build after code-changes, run:

    ninja -C builddir
    sudo ninja -C builddir install

Note: `builddir` is the build output directory and can be changed to any
other directory name. To set configure-time options, use e.g.

    mesonconf builddir -Denable-documentation=false

Run `mesonconf builddir` to list the options.

The DBus-Interface
------------------

Note: **the DBus interface is subject to change**

Interfaces:
-  org.freedesktop.ratbag1.Manager
-  org.freedesktop.ratbag1.Device
-  org.freedesktop.ratbag1.Profile
-  org.freedesktop.ratbag1.Resolution
-  org.freedesktop.ratbag1.Button
-  org.freedesktop.ratbag1.Led

The **org.freedesktop.ratbag1.Manager** interface provides:
- Properties:
  - Devices -> array of object paths with interface Device
- Signals:
  - DeviceNew -> new device available, carries object path
  - DeviceRemoved -> device removed, carries object path

The **org.freedesktop.ratbag1.Device** interface provides:
- Properties:
  - Id -> unique ID of this device
  - Capabilities -> array of uints with the capabilities enum from libratbag
  - Description -> device name
  - Svg -> device SVG name (file only)
  - SvgPath -> device SVG name (full absolute path)
  - Profiles -> array of object paths with interface Profile
  - ActiveProfile -> index of the currently active profile in Profiles
- Methods:
  - GetProfileByIndex(uint) -> returns the object path for the given index

The **org.freedesktop.ratbag1.Profile** interface provides:
- Properties:
  - Index -> index of the profile
  - Resolutions -> array of object paths with interface Resolution
  - Buttons -> array of object paths with interface Button
  - Leds -> array of object paths with interface Led
  - ActiveResolution -> index of the currently active resolution in Resolutions
  - DefaultResolution -> index of the default resolution in Resolutions
- Methods:
  - SetActive(void) -> set this profile to be the active profile
  - GetResolutionByIndex(uint) -> returns the object path for the given index
- Signals:
  - ActiveProfileChanged -> active profile changed, carries index of the new active profile

The **org.freedesktop.ratbag1.Resolution** interface provides:
- Properties:
  - Index -> index of the resolution
  - Capabilities -> array of uints with the capabilities enum from libratbag
  - XResolution -> uint for the x resolution assigned to this entry
  - YResolution -> uint for the y resolution assigned to this entry
  - ReportRate -> uint for the report rate in Hz assigned to this entry
- Methods:
  - SetResolution(uint, uint) -> x/y resolution to assign
  - SetReportRate(uint) -> uint for the report rate to assign
  - SetDefault -> set this resolution to be the default
- Signals:
  - ActiveResolutionChanged -> active resolution changed, carries index of the new active resolution
  - DefaultResolutionChanged -> default resolution changed, carries index of the new default resolution

The **org.freedesktop.ratbag1.Button** interface provides:
- Properties:
  - Index -> index of the button
  - Type -> string describing the button type
  - ButtonMapping -> uint of the current button mapping (if mapping to button)
  - SpecialMapping -> string of the current special mapping (if mapped to special)
  - KeyMapping -> array of uints, first entry is the keycode, other entries, if any, are modifiers (if mapped to key)
  - ActionType -> string describing the action type of the button ("none", "button", "key", "special", "macro", "unknown"). This decides which \*Mapping  property has a value
  - ActionTypes -> array of strings, possible values for ActionType
- Methods:
  - SetButtonMapping(uint) -> set the button mapping to the given button
  - SetSpecialMapping(string) -> set the button mapping to the given special entry
  - SetKeyMapping(uint[]) -> set the key mapping, first entry is the keycode, other entries, if any, are modifier keycodes
  - Disable(void) -> disable this button

The **org.freedesktop.ratbag1.Led** interface provides:
- Properties:
  - Index -> index of the LED
  - Mode -> uint mapping to the mode enum from libratbag
  - Type -> string describing the LED type
  - Color -> uint triplet (RGB) of the LED's color
  - EffectRate -> the effect rate in Hz, possible values are in the range 100 - 20000
  - Brightness -> the brightness of the LED, possible values are in the range 0 - 255
- Methods:
  - SetMode(uint) -> set the mode to the given mode enum value from libratbag
  - SetColor((uuu)) -> set the color to the given uint triplet (RGB)
  - SetEffectRate(uint) -> set the effect rate in Hz, possible values are in the range 100 - 20000
  - SetBrightness(int) -> set the brightness, possible values are in the range 0 - 255

For easier debugging, objects paths are constructed from the device. e.g.
`/org/freedesktop/ratbag/button/event5/p0/b10` is the button interface for
button 10 on profile 0 on event5. The naming is subject to change. Do not
rely on a constructed object path in your application.

Architecture
------------

libratbag has two main components, libratbag and ratbagd.
The former is a library to access the devices. It consists of
the front-end API and wrapper code, and the back-end HW-specific drivers:

    +-----+    +-----+    +-----------+    +----------+
    | app | -> | API | -> | hw-driver | -> | protocol | -> device
    +-----+    +-----+    +-----------+    +----------+

The API layer is HW agnostic. Depend on the HW, the protocol may be part of
the driver implementation (e.g. etekcity) or a separate set of files
(HID++). Where the protocol is separate, the whole known protocol should be
implemented. The HW driver then only accesses the bits required for
libratbag. This allows us to optionally export the protocol as separate
library in the future, if other projects require it.

Adding Devices
--------------

As of commit 3605bede4 libratbag now uses a hwdb entry to match the device
with the drivers. To access a device through libratbag, the udev property
RATBAG_DRIVER must be set for a device's event node. Check with

     sudo udevadm info /sys/class/input/eventX | grep RATBAG_DRIVER

If your device is not yet assigned the property, it is not in the hwdb. It
may however be supported by one of our existing drivers. Try enabling it by
adding the device vendor-id/product-id to the hwdb file in
`hwdb/70-libratbag-mouse.hwdb`. For example, for a HID++ 1.0 device, edit
the 70-libratbag-mouse.hwdb file and add an entry with `RATBAG_DRIVER=hidpp10`.
For the other drivers, look for the id of the driver in driver-{drivername}.c
file and do the same.

Once your device is added to the hwdb, install libratbag and trigger a hwdb
update:

    sudo udevadm hwdb --update
    sudo udevadm control --reload

Then unplug/replug your mouse. `RATBAG_DRIVER` should appear in the udev
properties of your device with the value you previously set. If the property
is not assigned, the hwdb entry does not correctly match your device or the
installed udev rules/hwdb entries are not picked up by udev.

If the device doesn't work, you'll have to start reverse-engineering the
device-specific protocol. Good luck :)

Source
------

    git clone https://github.com/libratbag/libratbag.git

Building
--------

libratbag uses the meson build system, see http://mesonbuild.com which in
turn uses ninja to invoke the compiler (`ninja` may be `ninja-build` on your
distribution). From a fresh git checkout, run the
following commands to init the repository:

    meson builddir --prefix=/usr/

And to build or re-build after code-changes, run:

    ninja -C builddir
    sudo ninja -C builddir install

Note: 'builddir' is the build output directory and can be changed to any
other directory name. To set configure-time options, use e.g.

    mesonconf builddir -Denable-documentation=false

Run 'mesonconf builddir' to list the options.

Bugs
----

Bugs can be reported in the issue tracker on our github repo:
https://github.com/libratbag/libratbag/issues

Mailing list
------------

libratbag discussions happen on the input-tools mailing list hosted on
freedesktop.org: http://lists.freedesktop.org/archives/input-tools/

Device-specific notes
---------------------

A number of device-specific notes and observations can be found in our
"device-notes" repository: http://libratbag.github.io/device-notes/

License
-------

libratbag is licensed under the MIT license.

> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the "Software"),
> to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense,
> and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions: [...]

See the COPYING file for the full license information.

[![Build Status](https://semaphoreci.com/api/v1/projects/7905244a-c0b5-468b-9071-d846de3ce9f1/545192/badge.svg)](https://semaphoreci.com/libratbag/libratbag)

[![Build Status](https://circleci.com/gh/libratbag/libratbag.svg?style=shield&circle-token=d7c782e10d2d934b176da754f11b5105ea074f4a)](https://circleci.com/gh/bentiss/libratbag)
