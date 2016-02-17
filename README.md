ratbagd
=======

ratbagd is a system daemon to introspect and modify configurable mice.

Running ratbagd
---------------

To be able to run ratbagd, you need to add a policy file to your dbus
environment:

    sudo cp dbus/org.freedesktop.ratbag1.conf /etc/dbus-1/system.d/org.freedesktop.ratbag1.conf

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

libratbag is licensed under the MIT license.

> Permission is hereby granted, free of charge, to any person obtaining a
> copy of this software and associated documentation files (the "Software"),
> to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense,
> and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions: [...]
