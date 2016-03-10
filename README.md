ratbagd
=======

ratbagd is a system daemon to introspect and modify configurable mice.

ratbagd uses libratbag to access mice and exports the features over a DBus
API to unprivileged proceses. ratbagd needs permissions to access device
nodes (primarily /dev/hidraw nodes) and usually needs to run as root.

ratbagd is a relatively thin wrapper arond libratbag. If a device is not
detected or does not function as expected, the issue is usually in
libratbag. libratbag can be found at
   https://github.com/libratbag/libratbag


Running ratbagd
---------------

ratbagd is intended to run as dbus-activated systemd service. This requires
installation of the following files:

    sudo cp dbus/org.freedesktop.ratbag1.conf /etc/dbus-1/system.d/org.freedesktop.ratbag1.conf
    sudo cp dbus/org.freedesktop.ratbag1.service /etc/dbus-1/systemd-services/org.freedesktop.ratbag1.conf
    sudo cp ratbagd.service /etc/systemd/system/ratbagd.service

The files are installed into the prefix by make install, see also the
configure switches ---with-systemd-unit-dir and --with-dbus-root-dir.
Developers are encouraged to simply symlink to the files in the git
repository.

For the files to take effect, you should run
    sudo systemctl daemon-reload
    sudo systemctl reload dbus.service

And finally, to enable the service:
    sudo systemctl enable ratbagd.service

This places the required symlink into the systemd directory so that dbus
activation is possible.

Compiling ratbagd
-----------------

ratbagd needs systemd >= 227. If you are running Fedora 23, you might find the
following instruction valuable:

Download systemd v229 (master caused troubles with gcrypt and gpg-error last
time I tried).

    $ git clone --branch v229 https://github.com/systemd/systemd


Configure it with:

    $ ./autogen.sh && ./configure --prefix=/opt/systemd \
                                  --libdir=/opt/systemd/lib \
                                  --disable-gcrypt \
                                  --without-bashcompletiondir \
                                  --with-rootprefix=/opt/systemd \
                                  --with-sysvinit-path=/opt/systemd/etc \
                                  --with-sysvrcnd-path=/opt/systemd/etc/rc.d

Then run make and make install.

In ratbagd, you will need to add the newly installed libsystemd in the
pkg_config path:

    $ PKG_CONFIG_PATH=/opt/systemd/lib/pkgconfig ./configure

License
-------

ratbagd is licensed under the MIT license.

> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the "Software"),
> to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense,
> and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions: [...]
