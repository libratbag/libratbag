ratbagd
=======

ratbagd is a system daemon to introspect and modify configurable mice.

Running ratbagd
---------------

To be able to run ratbagd, you need to add a policy file to your dbus
environment:

    sudo cp dbus/org.freedesktop.ratbag1.conf /etc/dbus-1/system.d/org.freedesktop.ratbag1.conf

License
-------

libratbag is licensed under the MIT license.

> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the "Software"),
> to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense,
> and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions: [...]
