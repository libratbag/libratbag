# vim: set expandtab shiftwidth=4 tabstop=4:
#
# This file is part of ratbagd.
#
# Copyright 2016 Red Hat, Inc.
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

from gi.repository import GLib
from gi.repository import Gio

class RatbagdDBusUnavailable(BaseException):
    """
    Signals DBus is unavailable or the ratbagd daemon is not available.
    """
    pass

class _RatbagdDBus(object):
    def __init__(self, interface, object_path):
        self._dbus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
        if self._dbus is None:
            raise RatbagdDBusUnavailable()

        try:
            self._proxy = Gio.DBusProxy.new_sync(self._dbus,
                                                 Gio.DBusProxyFlags.NONE,
                                                 None,
                                                 'org.freedesktop.ratbag1',
                                                 object_path,
                                                 'org.freedesktop.ratbag1.{}'.format(interface),
                                                 None)
        except GLib.GError:
            raise RatbagdDBusUnavailable()

        if self._proxy.get_name_owner() is None:
            raise RatbagdDBusUnavailable()

    def dbus_property(self, property):
        p = self._proxy.get_cached_property(property)
        if p != None:
            return p.unpack()
        return p

    def dbus_call(self, method, type, *value):
        val = GLib.Variant("({})".format(type), value )
        self._proxy.call_sync(method, val, Gio.DBusCallFlags.NO_AUTO_START, 500, None)

class Ratbagd(_RatbagdDBus):
    """
    The ratbagd top-level object. Provides a list of devices available
    through ratbagd, actual interaction with the devices is via the
    RatbagdDevice, RatbagdProfile and RatbagdResolution objects.

    Throws RatbagdDBusUnavailable when the DBus service is not available.
    """
    def __init__(self):
        _RatbagdDBus.__init__(self, "Manager", '/org/freedesktop/ratbag1')
        self._devices = []
        result = self.dbus_property("Devices")
        if result != None:
            self._devices = [RatbagdDevice(objpath) for objpath in result]

    @property
    def devices(self):
        """
        A list of RatbagdDevice objects supported by ratbagd.
        """
        return self._devices

class RatbagdDevice(_RatbagdDBus):
    CAP_SWITCHABLE_RESOLUTION = 1
    CAP_SWITCHABLE_PROFILE = 2
    CAP_BUTTON_KEY = 3
    CAP_LED = 4
    CAP_BUTTON_MACROS = 5
    CAP_DEFAULT_PROFILE = 6
    CAP_QUERY_CONFIGURATION = 7
    CAP_DISABLE_PROFILE = 8

    """
    Represents a ratbagd device.
    """
    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Device", object_path)
        self._devnode = self.dbus_property("Id")
        self._name = self.dbus_property("Name")
        self._svg = self.dbus_property("Svg")
        self._svg_path = self.dbus_property("SvgPath")

        self._profiles = []
        self._active_profile = -1
        result = self.dbus_property("Profiles")
        if result != None:
            self._profiles = [RatbagdProfile(objpath) for objpath in result]
            self._active_profile = self.dbus_property("ActiveProfile")

        self._caps = self.dbus_property("Capabilities")

    @property
    def profiles(self):
        """
        A list of RatbagdProfile objects provided by this device.
        """
        return self._profiles

    @property
    def name(self):
        """
        The device name, usually provided by the kernel.
        """
        return self._name

    @property
    def svg(self):
        """
        The SVG file name. This function returns the file name only, not the
        absolute path to the file.
        """
        return self._svg

    @property
    def svg_path(self):
        """
        The absolute SVG path. This function returns the full path to the
        svg file.
        """
        return self._svg_path

    @property
    def id(self):
        """
        A unique identifier for this device.
        """
        return self._devnode

    @property
    def active_profile(self):
        """
        The currently active profile. This function returns a RatbagdProfile
        or None if no active profile was found.
        """
        if self._active_profile == -1:
            return None
        return self._profiles[self._active_profile]

    @property
    def capabilities(self):
        """
        Return the capabilities of this device as an array.
        Capabilities not present on the device are not in the list. Thus use
        e.g.
            if RatbagdDevice.CAP_SWITCHABLE_RESOLUTION is in device.caps:
                 do something
        """
        return self._caps

    def __eq__(self, other):
        return other and self._objpath == other._objpath

class RatbagdProfile(_RatbagdDBus):
    """
    Represents a ratbagd profile
    """
    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Profile", object_path)
        self._objpath = object_path
        self._index = self.dbus_property("Index")

        self._resolutions = []
        self._active_resolution_idx = -1
        self._default_resolution_idx = -1
        self._buttons = []

        result = self.dbus_property("Resolutions")
        if result != None:
            self._resolutions = [RatbagdResolution(objpath) for objpath in result]
            self._active_resolution_idx = self.dbus_property("ActiveResolution")
            self._default_resolution_idx = self.dbus_property("DefaultResolution")

        result = self.dbus_property("Buttons")
        if result != None:
            self._buttons = [RatbagdButton(objpath) for objpath in result]

    @property
    def index(self):
        return self._index

    @property
    def resolutions(self):
        """
        A list of RatbagdResolution objects with this profile's resolutions.
        """
        return self._resolutions

    @property
    def active_resolution(self):
        """
        The currently active resolution. This function returns a
        RatbagdResolution object or None.
        """
        if self._active_resolution_idx == -1:
            return None
        return self._resolutions[self._active_resolution_idx]

    @property
    def default_resolution(self):
        """
        The default resolution. This function returns a RatbagdResolution
        object or None.
        """
        if self._default_resolution_idx == -1:
            return None
        return self._resolutions[self._default_resolution_idx]

    @property
    def buttons(self):
        """
        A list of RatbagdButton objects with this profile's button
        mappings. Note that the list of buttons differs between profiles but
        the number of buttons is identical across profiles.
        """
        return self._buttons

    def __eq__(self, other):
        return self._objpath == other._objpath

class RatbagdResolution(_RatbagdDBus):
    CAP_INDIVIDUAL_REPORT_RATE = 1
    CAP_SEPARATE_XY_RESOLUTION = 2
    """
    Represents a ratbagd resolution.
    """
    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Resolution", object_path)
        self._index = self.dbus_property("Index")
        self._xres = self.dbus_property("XResolution")
        self._yres = self.dbus_property("YResolution")
        self._rate = self.dbus_property("ReportRate")
        self._objpath = object_path

        self._caps = self.dbus_property("Capabilities")

    @property
    def resolution(self):
        """Returns the tuple (xres, yres) with each resolution in DPI"""
        return (self._xres, self._yres)

    @resolution.setter
    def resolution(self, res):
        return self.dbus_call("SetResolution", "uu", *res)

    @property
    def report_rate(self):
        """
        Returns the report rate in Hz.
        """
        return self._rate

    @report_rate.setter
    def report_rate(self, rate):
        return self.dbus_call("SetReportRate", "u", rate)

    @property
    def capabilities(self):
        """
        Return the capabilities of this device as a list.
        Capabilities not present on the device are not in the list. Thus use
        e.g.
            if RatbagdResolution.CAP_SEPARATE_XY_RESOLUTION is in resolution.caps:
                 do something
        """
        return self._caps

    def __eq__(self, other):
        return self._objpath == other._objpath

class RatbagdButton(_RatbagdDBus):
    """
    Represents a ratbagd button.
    """
    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Button", object_path)
        self._index = self.dbus_property("Index")
        self._button = self.dbus_property("ButtonMapping")

    @property
    def index(self):
        return self._index

    @property
    def button_type(self):
        return self.dbus_property("Type")

    @property
    def action_type(self):
        return self.dbus_property("ActionType")

    @property
    def special(self):
        self._special = self.dbus_property("SpecialMapping")
        return self._special

    @special.setter
    def special(self, special):
        return self.dbus_call("SetSpecialMapping", "s", special)

    @property
    def key(self):
        self._key = self.dbus_property("KeyMapping")
        return self._key

    @key.setter
    def key(self, key, modifiers):
        return self.dbus_call("SetKeyMapping", "au", [key].append(modifiers))

    @property
    def button(self):
        self._button = self.dbus_property("ButtonMapping")
        return self._button

    @button.setter
    def button(self, button):
        return self.dbus_call("SetButtonMapping", "u", button)

    def disable(self):
        return self.dbus_call("Disable", "")
