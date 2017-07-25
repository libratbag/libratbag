# vim: set expandtab shiftwidth=4 tabstop=4:
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

import os
import sys

from enum import IntEnum
from gi.repository import Gio, GLib, GObject


class RatbagErrorCode(IntEnum):
    RATBAG_SUCCESS = 0

    """An error occured on the device. Either the device is not a libratbag
    device or communication with the device failed."""
    RATBAG_ERROR_DEVICE = -1000

    """Insufficient capabilities. This error occurs when a requested change is
    beyond the device's capabilities."""
    RATBAG_ERROR_CAPABILITY = -1001

    """Invalid value or value range. The provided value or value range is
    outside of the legal or supported range."""
    RATBAG_ERROR_VALUE = -1002

    """A low-level system error has occured, e.g. a failure to access files
    that should be there. This error is usually unrecoverable and libratbag will
    print a log message with details about the error."""
    RATBAG_ERROR_SYSTEM = -1003

    """Implementation bug, either in libratbag or in the caller. This error is
    usually unrecoverable and libratbag will print a log message with details
    about the error."""
    RATBAG_ERROR_IMPLEMENTATION = -1004


class RatbagdDBusUnavailable(BaseException):
    """Signals DBus is unavailable or the ratbagd daemon is not available."""
    pass


class RatbagdDBusTimeout(BaseException):
    """Signals that a timeout occurred during a DBus method call."""
    pass


class RatbagError(BaseException):
    """A common base exception to catch any ratbag exception."""
    pass


class RatbagErrorDevice(RatbagError):
    """An exception corresponding to RatbagErrorCode.RATBAG_ERROR_DEVICE."""
    pass


class RatbagErrorCapability(RatbagError):
    """An exception corresponding to RatbagErrorCode.RATBAG_ERROR_CAPABILITY."""
    pass


class RatbagErrorValue(RatbagError):
    """An exception corresponding to RatbagErrorCode.RATBAG_ERROR_Value."""
    pass


class RatbagErrorSystem(RatbagError):
    """An exception corresponding to RatbagErrorCode.RATBAG_ERROR_System."""
    pass


class RatbagErrorImplementation(RatbagError):
    """An exception corresponding to RatbagErrorCode.RATBAG_ERROR_IMPLEMENTATION."""
    pass


"""A table mapping RatbagErrorCode values to RatbagError* exceptions."""
EXCEPTION_TABLE = {
    RatbagErrorCode.RATBAG_ERROR_DEVICE: RatbagErrorDevice,
    RatbagErrorCode.RATBAG_ERROR_CAPABILITY: RatbagErrorCapability,
    RatbagErrorCode.RATBAG_ERROR_VALUE: RatbagErrorValue,
    RatbagErrorCode.RATBAG_ERROR_SYSTEM: RatbagErrorSystem,
    RatbagErrorCode.RATBAG_ERROR_IMPLEMENTATION: RatbagErrorImplementation
}


class _RatbagdDBus(GObject.GObject):
    _dbus = None

    def __init__(self, interface, object_path):
        GObject.GObject.__init__(self)

        if _RatbagdDBus._dbus is None:
            try:
                _RatbagdDBus._dbus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
            except GLib.Error:
                raise RatbagdDBusUnavailable()

        ratbag1 = "org.freedesktop.ratbag1"
        if os.environ.get('RATBAGCTL_DEVEL'):
            ratbag1 = os.environ['RATBAGCTL_DEVEL']

        if object_path is None:
            object_path = "/" + ratbag1.replace('.', '/')

        self._object_path = object_path
        self._interface = "{}.{}".format(ratbag1, interface)

        try:
            self._proxy = Gio.DBusProxy.new_sync(_RatbagdDBus._dbus,
                                                 Gio.DBusProxyFlags.NONE,
                                                 None,
                                                 ratbag1,
                                                 object_path,
                                                 self._interface,
                                                 None)
        except GLib.Error:
            raise RatbagdDBusUnavailable()

        if self._proxy.get_name_owner() is None:
            raise RatbagdDBusUnavailable()

    def _get_dbus_property(self, property):
        # Retrieves a cached property from the bus, or None.
        p = self._proxy.get_cached_property(property)
        if p is not None:
            return p.unpack()
        return p

    def _set_dbus_property(self, property, type, value, readwrite=True):
        # Sets a cached property on the bus.

        # Take our real value and wrap it into a variant. To call
        # org.freedesktop.DBus.Properties.Set we need to wrap that again
        # into a (ssv), where v is our value's variant.
        # args to .Set are "interface name", "function name",  value-variant
        val = GLib.Variant("{}".format(type), value)
        if readwrite:
            pval = GLib.Variant("(ssv)".format(type), (self._interface, property, val))
            self._proxy.call_sync("org.freedesktop.DBus.Properties.Set",
                                  pval, Gio.DBusCallFlags.NO_AUTO_START,
                                  500, None)

        # This is our local copy, so we don't have to wait for the async
        # update
        self._proxy.set_cached_property(property, val)

    def _dbus_call(self, method, type, *value):
        # Calls a method synchronously on the bus, using the given method name,
        # type signature and values.
        #
        # It the result is valid, it is returned. Invalid results raise the
        # appropriate RatbagError* or RatbagdDBus* exception, or GLib.Error if
        # it is an unexpected exception that probably shouldn't be passed up to
        # the UI.
        val = GLib.Variant("({})".format(type), value)
        try:
            res = self._proxy.call_sync(method, val,
                                        Gio.DBusCallFlags.NO_AUTO_START,
                                        500, None)
            global EXCEPTION_TABLE
            if res in EXCEPTION_TABLE:
                raise EXCEPTION_TABLE[res]
            return res.unpack()[0]  # Result is always a tuple
        except GLib.Error as e:
            if e.code == Gio.IOErrorEnum.TIMED_OUT:
                raise RatbagdDBusTimeout(e.message)
            else:
                # Unrecognized error code; print the message to stderr and raise
                # the GLib.Error.
                print(e.message, file=sys.stderr)
                raise

    def __eq__(self, other):
        return other and self._object_path == other._object_path


class Ratbagd(_RatbagdDBus):
    """The ratbagd top-level object. Provides a list of devices available
    through ratbagd; actual interaction with the devices is via the
    RatbagdDevice, RatbagdProfile, RatbagdResolution and RatbagdButton objects.

    Throws RatbagdDBusUnavailable when the DBus service is not available.
    """

    def __init__(self):
        _RatbagdDBus.__init__(self, "Manager", None)

    @GObject.Property
    def devices(self):
        """A list of RatbagdDevice objects supported by ratbagd."""
        devices = []
        result = self._get_dbus_property("Devices")
        if result is not None:
            devices = [RatbagdDevice(objpath) for objpath in result]
        return devices

    @GObject.Property
    def themes(self):
        """A list of theme names. The theme 'default' is guaranteed to be
        available."""
        return self._get_dbus_property("Themes")


class RatbagdDevice(_RatbagdDBus):
    """Represents a ratbagd device."""

    CAP_NONE = 0
    CAP_QUERY_CONFIGURATION = 1
    CAP_RESOLUTION = 100
    CAP_SWITCHABLE_RESOLUTION = 101
    CAP_PROFILE = 200
    CAP_SWITCHABLE_PROFILE = 201
    CAP_DISABLE_PROFILE = 202
    CAP_DEFAULT_PROFILE = 203
    CAP_BUTTON = 300
    CAP_BUTTON_KEY = 301
    CAP_BUTTON_MACROS = 302
    CAP_LED = 400

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Device", object_path)

    @GObject.Property
    def id(self):
        """The unique identifier of this device."""
        return self._get_dbus_property("Id")

    @GObject.Property
    def capabilities(self):
        """The capabilities of this device as an array. Capabilities not
        present on the device are not in the list. Thus use e.g.

        if RatbagdDevice.CAP_SWITCHABLE_RESOLUTION is in device.capabilities:
            do something
        """
        return self._get_dbus_property("Capabilities")

    @GObject.Property
    def name(self):
        """The device name, usually provided by the kernel."""
        return self._get_dbus_property("Name")

    @GObject.Property
    def profiles(self):
        """A list of RatbagdProfile objects provided by this device."""
        profiles = []
        result = self._get_dbus_property("Profiles")
        if result is not None:
            profiles = [RatbagdProfile(objpath) for objpath in result]
        return profiles

    def get_svg(self, theme):
        """Gets the full path to the SVG for the given theme, or the empty
        string if none is available.

        The theme must be one of org.freedesktop.ratbag1.Manager.Themes. The
        theme 'default' is guaranteed to be available.

        @param theme The theme from which to retrieve the SVG, as str
        """
        return self._dbus_call("GetSvg", "s", theme)

    def commit(self):
        """Commits all changes made to the device."""
        return self._dbus_call("Commit", "")


class RatbagdProfile(_RatbagdDBus):
    """Represents a ratbagd profile."""

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Profile", object_path)

    @GObject.Property
    def index(self):
        """The index of this profile."""
        return self._get_dbus_property("Index")

    @GObject.Property
    def enabled(self):
        """tells if the profile is enabled."""
        return self._get_dbus_property("Enabled")

    @enabled.setter
    def enabled(self, enabled):
        """Enable/Disable this profile.

        @param enabled The new state, as boolean"""
        return self._set_dbus_property("Enabled", "b", enabled)

    @GObject.Property
    def resolutions(self):
        """A list of RatbagdResolution objects with this profile's resolutions.
        """
        resolutions = []
        result = self._get_dbus_property("Resolutions")
        if result is not None:
            resolutions = [RatbagdResolution(objpath) for objpath in result]
        return resolutions

    @GObject.Property
    def buttons(self):
        """A list of RatbagdButton objects with this profile's button mappings.
        Note that the list of buttons differs between profiles but the number
        of buttons is identical across profiles."""
        buttons = []
        result = self._get_dbus_property("Buttons")
        if result is not None:
            buttons = [RatbagdButton(objpath) for objpath in result]
        return buttons

    @GObject.Property
    def leds(self):
        """A list of RatbagdLed objects with this profile's leds."""
        leds = []
        result = self._get_dbus_property("Leds")
        if result is not None:
            leds = [RatbagdLed(objpath) for objpath in result]
        return leds

    @GObject.Property
    def is_active(self):
        """Returns True if the profile is currenly active, false otherwise."""
        return self._get_dbus_property("IsActive")

    def set_active(self):
        """Set this profile to be the active profile."""
        return self._dbus_call("SetActive", "")


class RatbagdResolution(_RatbagdDBus):
    """Represents a ratbagd resolution."""

    CAP_INDIVIDUAL_REPORT_RATE = 1
    CAP_SEPARATE_XY_RESOLUTION = 2

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Resolution", object_path)

    @GObject.Property
    def index(self):
        """The index of this resolution."""
        return self._get_dbus_property("Index")

    @GObject.Property
    def capabilities(self):
        """The capabilities of this resolution as a list. Capabilities not
        present on the resolution are not in the list. Thus use e.g.

        if RatbagdResolution.CAP_SEPARATE_XY_RESOLUTION is in resolution.capabilities:
            do something
        """
        return self._get_dbus_property("Capabilities")

    @GObject.Property
    def resolution(self):
        """The tuple (xres, yres) with each resolution in DPI."""
        return self._get_dbus_property("Resolution")

    @resolution.setter
    def resolution(self, res):
        """Set the x- and y-resolution using the given (xres, yres) tuple.

        @param res The new resolution, as (int, int)
        """
        ret = self._set_dbus_property("Resolution", "(uu)", res)
        return ret

    @GObject.Property
    def report_rate(self):
        """The report rate in Hz."""
        return self._get_dbus_property("ReportRate")

    @GObject.Property
    def maximum(self):
        """The maximum possible resolution."""
        return self._get_dbus_property("Maximum")

    @GObject.Property
    def minimum(self):
        """The minimum possible resolution."""
        return self._get_dbus_property("Minimum")

    @report_rate.setter
    def report_rate(self, rate):
        """Set the report rate in Hz.

        @param rate The new report rate, as int
        """
        return self._set_dbus_property("ReportRate", "u", rate)

    @GObject.Property
    def is_active(self):
        """True if this is the currently active resolution, False
        otherwise"""
        return self._get_dbus_property("IsActive")

    @GObject.Property
    def is_default(self):
        """True if this is the currently default resolution, False
        otherwise"""
        return self._get_dbus_property("IsDefault")

    def set_default(self):
        """Set this resolution to be the default."""
        return self._dbus_call("SetDefault", "")


class RatbagdButton(_RatbagdDBus):
    """Represents a ratbagd button."""

    ACTION_TYPE_NONE = 0
    ACTION_TYPE_BUTTON = 1
    ACTION_TYPE_SPECIAL = 2
    ACTION_TYPE_KEY = 3
    ACTION_TYPE_MACRO = 4

    MACRO_KEY_PRESS = 1
    MACRO_KEY_RELEASE = 2
    MACRO_WAIT = 3

    TYPE_UNKNOWN = 0
    TYPE_LEFT = 1
    TYPE_MIDDLE = 2
    TYPE_RIGHT= 3
    TYPE_THUMB = 4
    TYPE_THUMB2 = 5
    TYPE_THUMB3 = 6
    TYPE_THUMB4 = 7
    TYPE_WHEEL_LEFT = 8
    TYPE_WHEEL_RIGHT = 9
    TYPE_WHEEL_CLICK = 10
    TYPE_WHEEL_UP = 11
    TYPE_WHEEL_DOWN = 12
    TYPE_WHEEL_RATCHET_MODE_SHIFT = 13
    TYPE_EXTRA = 14
    TYPE_SIDE = 15
    TYPE_PINKIE = 16
    TYPE_PINKIE2 = 17
    TYPE_RESOLUTION_CYCLE_UP = 18
    TYPE_RESOLUTION_UP = 19
    TYPE_RESOLUTION_DOWN = 20
    TYPE_PROFILE_CYCLE_UP = 21
    TYPE_PROFILE_UP = 22
    TYPE_PROFILE_DOWN = 23


    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Button", object_path)

    @GObject.Property
    def index(self):
        """The index of this button."""
        return self._get_dbus_property("Index")

    @GObject.Property
    def type(self):
        """An enum describing this button's type."""
        return self._get_dbus_property("Type")

    @GObject.Property
    def mapping(self):
        """An integer of the current button mapping, if mapping to a button."""
        return self._get_dbus_property("ButtonMapping")

    @mapping.setter
    def mapping(self, button):
        """Set the button mapping to the given button.

        @param button The button to map to, as int
        """
        ret = self._dbus_call("SetButtonMapping", "u", button)
        self._set_dbus_property("ButtonMapping", "u", button)
        return ret

    @GObject.Property
    def macro(self):
        """A list of (type, value) tuples that form the currently set macro,
        where type is either RatbagdButton.KEY_PRESS, RatbagdButton.KEY_RELEASE
        or RatbagdButton.MACRO_WAIT and the value is the keycode as specified
        in linux/input.h for key related types, or the timeout in milliseconds."""
        return self._get_dbus_property("Macro")

    @macro.setter
    def macro(self, macro):
        """Set the macro to the given macro. Note that the type must be one of
        RatbagdButton.KEY_PRESS, RatbagdButton.KEY_RELEASE or
        RatbagdButton.MACRO_WAIT and the keycodes must be as specified in
        linux/input.h, and the timeout must be in milliseconds.

        @param macro A list of (type, value) tuples to form the new macro.
        """
        ret = self._dbus_call("SetMacro", "a(uu)", macro)
        self._set_dbus_property("Macro", "a(uu)", macro, readwrite=False)
        return ret

    @GObject.Property
    def special(self):
        """A string of the current special mapping, if mapped to special."""
        return self._get_dbus_property("SpecialMapping")

    @special.setter
    def special(self, special):
        """Set the button mapping to the given special entry.

        @param special The special entry, as str
        """
        ret = self._dbus_call("SetSpecialMapping", "s", special)
        self._set_dbus_property("SpecialMapping", "s", special)
        return ret

    @GObject.Property
    def key(self):
        """A list of integers, the first being the keycode and the other
        entries, if any, are modifiers (if mapped to key)."""
        return self._get_dbus_property("KeyMapping")

    @key.setter
    def key(self, keys):
        """Set the key mapping.

        @param keys A list of integers, the first being the keycode and the rest
                    modifiers.
        """
        ret = self._dbus_call("SetKeyMapping", "au", keys)
        self._set_dbus_property("KeyMapping", "au", keys)
        return ret

    @GObject.Property
    def action_type(self):
        """An enum describing the action type of the button. One of
        ACTION_TYPE_NONE, ACTION_TYPE_BUTTON, ACTION_TYPE_SPECIAL,
        ACTION_TYPE_KEY, ACTION_TYPE_MACRO. This decides which
        *Mapping property has a value.
        """
        return self._get_dbus_property("ActionType")

    @GObject.Property
    def action_types(self):
        """An array of possible values for ActionType."""
        return self._get_dbus_property("ActionTypes")

    def disable(self):
        """Disables this button."""
        return self._dbus_call("Disable", "")


class RatbagdLed(_RatbagdDBus):
    """Represents a ratbagd led."""

    MODE_OFF = 0
    MODE_ON = 1
    MODE_CYCLE = 2
    MODE_BREATHING = 3

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Led", object_path)

    @GObject.Property
    def index(self):
        """The index of this led."""
        return self._get_dbus_property("Index")

    @GObject.Property
    def mode(self):
        """This led's mode, one of MODE_OFF, MODE_ON, MODE_CYCLE and
        MODE_BREATHING."""
        return self._get_dbus_property("Mode")

    @mode.setter
    def mode(self, mode):
        """Set the led's mode to the given mode.

        @param mode The new mode, as one of MODE_OFF, MODE_ON, MODE_CYCLE and
                    MODE_BREATHING.
        """
        return self._set_dbus_property("Mode", "u", mode)

    @GObject.Property
    def type(self):
        """A string describing this led's type."""
        return self._get_dbus_property("Type")

    @GObject.Property
    def color(self):
        """An integer triple of the current LED color."""
        return self._get_dbus_property("Color")

    @color.setter
    def color(self, color):
        """Set the led color to the given color.

        @param color An RGB color, as an integer triplet with values 0-255.
        """
        return self._set_dbus_property("Color", "(uuu)", color)

    @GObject.Property
    def effect_rate(self):
        """The LED's effect rate in Hz, values range from 100 to 20000."""
        return self._get_dbus_property("EffectRate")

    @effect_rate.setter
    def effect_rate(self, effect_rate):
        """Set the effect rate in Hz. Allowed values range from 100 to 20000.

        @param effect_rate The new effect rate, as int
        """
        return self._set_dbus_property("EffectRate", "u", effect_rate)

    @GObject.Property
    def brightness(self):
        """The LED's brightness, values range from 0 to 255."""
        return self._get_dbus_property("Brightness")

    @brightness.setter
    def brightness(self, brightness):
        """Set the brightness. Allowed values range from 0 to 255.

        @param brightness The new brightness, as int
        """
        return self._set_dbus_property("Brightness", "u", brightness)
