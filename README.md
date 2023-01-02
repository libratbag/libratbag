libratbag
=========

<img src="https://libratbag.github.io/_images/logo.svg" alt="" width="30%" align="right">

libratbag provides **ratbagd**, a DBus daemon to configure input devices,
mainly gaming mice. The daemon provides a generic way to access the various
features exposed by these mice and abstracts away hardware-specific and
kernel-specific quirks.

libratbag currently supports devices from Logitech, Etekcity, GSkill,
Roccat, Steelseries. See [the device
files](https://github.com/libratbag/libratbag/tree/master/data/devices) for
a complete list of supported devices.

Users interact through a GUI like
[Piper](https://github.com/libratbag/piper/). For developers, the
`ratbagctl` tool is the prime tool for debugging.

Installing libratbag from system packages
-----------------------------------------

libratbag is packaged for some distributions, you can use your system's
package manager to install it. See [the
wiki](https://github.com/libratbag/libratbag/wiki/Installation) for details.

Compiling libratbag
-------------------

libratbag uses the [meson build system](http://mesonbuild.com) which in
turn uses ninja to invoke the compiler. Run the following commands to clone
libratbag and initialize the build:

    git clone https://github.com/libratbag/libratbag.git
    cd libratbag
    meson builddir
    ninja -C builddir
    sudo ninja -C builddir install

The default prefix is `/usr/local`, i.e. it will not overwrite the system
installation. For more information, see [the
wiki](https://github.com/libratbag/libratbag/wiki/Installation).

And to build or re-build after code-changes, run:

    ninja -C builddir
    sudo ninja -C builddir install
    
To remove/uninstall simply run:

    sudo ninja -C builddir uninstall

Note: `builddir` is the build output directory and can be changed to any
other directory name. To set configure-time options, use e.g.

    meson configure builddir -Ddocumentation=false

Run `meson configure builddir` to list the options.

Running ratbagd as DBus-activated systemd service
-------------------------------------------------

To run ratbagd, simply run it as root `sudo ratbagd`. However,
ratbagd is intended to run as dbus-activated systemd service and installs
the following files:

    /usr/share/dbus-1/system.d/org.freedesktop.ratbag1.conf
    /usr/share/dbus-1/system-services/org.freedesktop.ratbag1.conf
    /usr/share/systemd/system/ratbagd.service

These files are installed into the prefix by `ninja install`, see also the
configure-time options `-Dsystemd-unit-dir` and `-Ddbus-root-dir`.
Developers are encouraged to simply symlink to the files in the git
repository.

For the files to take effect, you should run

    sudo systemctl daemon-reload
    sudo systemctl reload dbus.service

And finally, to enable the service:

    sudo systemctl enable ratbagd.service

This places the required symlink into the systemd directory so that dbus
activation is possible.

The DBus Interface
-------------------

Full documentation of the DBus interface to interact with devices is
available here: [ratbagd DBus Interface description](https://libratbag.github.io/).

libratbag Internal Architecture
-------------------------------

libratbag has two main components, libratbag and ratbagd. Applications like
Piper talk over DBus to ratbagd. ratbagd uses libratbag to access the actual
devices.

    +-------+    +------+    +---------+    +-----------+
    | Piper | -> | DBus | -> | ratbagd | -> | libratbag | -> device
    +-------+    +------+    +---------+    +-----------+


Inside libratbag, we have the general frontend and API. Each device is
handled by a HW-specific backend.  That HW backend is responsible for the
device-specific communication (usually some vendor-specific HID protocol).

    +---------+    +-----+    +------------+    +----------+
    | ratbagd | -> | API | -> | hw backend | -> | protocol | -> device
    +---------+    +-----+    +------------+    +----------+

The API layer is HW agnostic. Depend on the HW, the protocol may be part of
the driver implementation (e.g. etekcity) or a separate set of files
(HID++). Where the protocol is separate, the whole known protocol should be
implemented. The HW driver then only accesses the bits required for
libratbag. This allows us to optionally export the protocol as separate
library in the future, if other projects require it.

Adding Devices to libratbag
---------------------------

libratbag relies on a device database to match a device with the drivers.
See the [data/devices/](https://github.com/libratbag/libratbag/tree/master/data/devices)
directory for the set of known devices. These files
are usually installed into `$prefix/$datadir` (e.g. `/usr/share/libratbag/`).

Adding a new device can be as simple as adding a new `.device` file. This is
the case for many devices with a shared protocol (e.g. Logitech's HID++).
See the
[data/devices/device.example](https://github.com/libratbag/libratbag/tree/master/data/devices/device.example)
file for guidance on what information must be set. Look for existing devices
from the same vendor as guidance too.

If the device has a different protocol and doesn't work after adding the
device file, you'll have to start reverse-engineering the device-specific
protocol. Good luck :)

Source
------

    git clone https://github.com/libratbag/libratbag.git

Bugs
----

Bugs can be reported in [our issue tracker](https://github.com/libratbag/libratbag/issues)

Mailing list
------------

libratbag discussions happen on the [input-tools mailing
list](http://lists.freedesktop.org/archives/input-tools/) hosted on
freedesktop.org

Device-specific notes
---------------------

A number of device-specific notes and observations can be found in our
wiki: https://github.com/libratbag/libratbag/wiki/Devices

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
