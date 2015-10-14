/*!@mainpage

libratbag
=========

libratbag is a configuration library for gaming mice. It provides a generic
way to access the various features exposed by these mice and abstracts away
hardware-specific and kernel-specific quirks.

Architecture
------------

libratbag has two main components, the front-end API and wrapper code, and
the back-end HW-specific drivers:

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
"devices-notes" repository: http://libratbag.github.io/devices-notes/

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

*/
