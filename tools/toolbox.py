# vim: set expandtab shiftwidth=4 tabstop=4:
#
# This file is part of libratbag.
#
# Copyright 2017 Red Hat, Inc.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.

import imp
import os
import shutil
import subprocess
import sys

from gi.repository import GLib

# various constants
RATBAGCTL_NAME = 'ratbagctl'
RATBAGCTL_PATH = os.path.join('@MESON_BUILD_ROOT@', RATBAGCTL_NAME)
RATBAGCTL_DEVEL_NAME = 'ratbagctl.devel'
RATBAGCTL_DEVEL_PATH = os.path.join('@MESON_BUILD_ROOT@', RATBAGCTL_DEVEL_NAME)
DBUS_CONF_DIR = '/etc/dbus-1/system.d'
DBUS_CONF_NAME = 'org.freedesktop.ratbag_devel1.conf'
DBUS_CONF_PATH = os.path.join(DBUS_CONF_DIR, DBUS_CONF_NAME)


def import_non_standard_path(name, path):
    # Fast path: see if the module has already been imported.
    try:
        return sys.modules[name]
    except KeyError:
        pass

    # If any of the following calls raises an exception,
    # there's a problem we can't handle -- let the caller handle it.

    with open(path, 'rb') as fp:
        module = imp.load_module(name, fp, os.path.basename(path), ('.py', 'rb', imp.PY_SOURCE))

    return module


def start_ratbagd():
    from gi.repository import Gio
    import time

    # first copy the policy for the ratbagd daemon to be allowed to run
    shutil.copy(os.path.join('@MESON_BUILD_ROOT@',
                DBUS_CONF_NAME),
                DBUS_CONF_DIR)

    # FIXME: kill any running ratbagd.devel

    ratbagd_process = subprocess.Popen([os.path.join('@MESON_BUILD_ROOT@', "ratbagd.devel")], shell=False)

    dbus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)

    name_owner = None
    start_time = time.clock()
    while name_owner is None and time.clock() - start_time < 30:
        proxy = Gio.DBusProxy.new_sync(dbus,
                                       Gio.DBusProxyFlags.NONE,
                                       None,
                                       "org.freedesktop.ratbag_devel1_@ratbagd_sha@",
                                       "/org/freedesktop/ratbag_devel1_@ratbagd_sha@",
                                       "org.freedesktop.ratbag_devel1_@ratbagd_sha@.Manager",
                                       None)
        name_owner = proxy.get_name_owner()
        if name_owner is None:
            time.sleep(0.2)

    os.environ['RATBAGCTL_DEVEL'] = "org.freedesktop.ratbag_devel1_@ratbagd_sha@"

    if name_owner is None or ratbagd_process.poll() is not None:
        return None

    return ratbagd_process


def terminate_ratbagd(ratbagd):
    try:
        ratbagd.terminate()
        ratbagd.wait(5)
    except subprocess.TimeoutExpired:
        ratbagd.kill()
    os.unlink(DBUS_CONF_PATH)


def sync_dbus():
    main_context = GLib.MainContext.default()
    while main_context.pending():
        main_context.iteration(False)


ratbagctl = import_non_standard_path(RATBAGCTL_NAME, RATBAGCTL_PATH)

from ratbagctl import open_ratbagd, get_parser, RatbagError, RatbagErrorCapability

__all__ = [
    RATBAGCTL_NAME,
    RATBAGCTL_PATH,
    DBUS_CONF_DIR,
    DBUS_CONF_NAME,
    DBUS_CONF_PATH,
    start_ratbagd,
    terminate_ratbagd,
    open_ratbagd,
    get_parser,
    RatbagError,
    RatbagErrorCapability,
]
