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
from evdev import ecodes
from gettext import gettext as _
from gi.repository import Gio, GLib, GObject


# Deferred translations, see https://docs.python.org/3/library/gettext.html#deferred-translations
def N_(x):
    return x


class RatbagErrorCode(IntEnum):
    SUCCESS = 0

    """An error occured on the device. Either the device is not a libratbag
    device or communication with the device failed."""
    DEVICE = -1000

    """Insufficient capabilities. This error occurs when a requested change is
    beyond the device's capabilities."""
    CAPABILITY = -1001

    """Invalid value or value range. The provided value or value range is
    outside of the legal or supported range."""
    VALUE = -1002

    """A low-level system error has occured, e.g. a failure to access files
    that should be there. This error is usually unrecoverable and libratbag will
    print a log message with details about the error."""
    SYSTEM = -1003

    """Implementation bug, either in libratbag or in the caller. This error is
    usually unrecoverable and libratbag will print a log message with details
    about the error."""
    IMPLEMENTATION = -1004


class RatbagdUnavailable(Exception):
    """Signals DBus is unavailable or the ratbagd daemon is not available."""
    pass


class RatbagdDBusTimeout(Exception):
    """Signals that a timeout occurred during a DBus method call."""
    pass


class RatbagError(Exception):
    """A common base exception to catch any ratbag exception."""
    pass


class RatbagErrorDevice(RatbagError):
    """An exception corresponding to RatbagErrorCode.DEVICE."""
    pass


class RatbagErrorCapability(RatbagError):
    """An exception corresponding to RatbagErrorCode.CAPABILITY."""
    pass


class RatbagErrorValue(RatbagError):
    """An exception corresponding to RatbagErrorCode.VALUE."""
    pass


class RatbagErrorSystem(RatbagError):
    """An exception corresponding to RatbagErrorCode.SYSTEM."""
    pass


class RatbagErrorImplementation(RatbagError):
    """An exception corresponding to RatbagErrorCode.IMPLEMENTATION."""
    pass


"""A table mapping RatbagErrorCode values to RatbagError* exceptions."""
EXCEPTION_TABLE = {
    RatbagErrorCode.DEVICE: RatbagErrorDevice,
    RatbagErrorCode.CAPABILITY: RatbagErrorCapability,
    RatbagErrorCode.VALUE: RatbagErrorValue,
    RatbagErrorCode.SYSTEM: RatbagErrorSystem,
    RatbagErrorCode.IMPLEMENTATION: RatbagErrorImplementation
}


class _RatbagdDBus(GObject.GObject):
    _dbus = None

    def __init__(self, interface, object_path):
        GObject.GObject.__init__(self)

        if _RatbagdDBus._dbus is None:
            try:
                _RatbagdDBus._dbus = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
            except GLib.Error as e:
                raise RatbagdUnavailable(e.message)

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
        except GLib.Error as e:
            raise RatbagdUnavailable(e.message)

        if self._proxy.get_name_owner() is None:
            raise RatbagdUnavailable("No one currently owns {}".format(ratbag1))

        self._proxy.connect("g-properties-changed", self._on_properties_changed)
        self._proxy.connect("g-signal", self._on_signal_received)

    def _on_properties_changed(self, proxy, changed_props, invalidated_props):
        # Implement this in derived classes to respond to property changes.
        pass

    def _on_signal_received(self, proxy, sender_name, signal_name, parameters):
        # Implement this in derived classes to respond to signals.
        pass

    def _find_object_with_path(self, iterable, object_path):
        # Find the index of an object in an iterable that whose object path
        # matches the given object path.
        for index, obj in enumerate(iterable):
            if obj._object_path == object_path:
                return index
        return -1

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
                                  2000, None)

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
                                        2000, None)
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

    def _dbus_call_return_one_fd(self, method, type, *value):
        # Calls a method synchronously on the bus, using the given method name,
        # type signature and values.
        #
        # It the result is valid, it is returned. Invalid results raise the
        # appropriate RatbagError* or RatbagdDBus* exception, or GLib.Error if
        # it is an unexpected exception that probably shouldn't be passed up to
        # the UI.
        val = GLib.Variant("({})".format(type), value)
        try:
            res = self._proxy.call_with_unix_fd_list_sync(method, val,
                                                          Gio.DBusCallFlags.NO_AUTO_START,
                                                          2000, None, None)
            if res in EXCEPTION_TABLE:
                raise EXCEPTION_TABLE[res]
            # Result is a tuple of (GLib.Variant('(h)', (0,)), fd_list),
            # we return the first file descriptor in that list
            return res[1].get(0)
        except GLib.Error as e:
            if e.code == Gio.DBusError.FILE_NOT_FOUND:
                raise FileNotFoundError('Failed to open SVG file')
            elif e.code == Gio.IOErrorEnum.TIMED_OUT:
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

    Throws RatbagdUnavailable when the DBus service is not available.
    """

    __gsignals__ = {
        "device-added":
            (GObject.SignalFlags.RUN_FIRST, None, (GObject.TYPE_PYOBJECT,)),
        "device-removed":
            (GObject.SignalFlags.RUN_FIRST, None, (GObject.TYPE_PYOBJECT,)),
        "daemon-disappeared":
            (GObject.SignalFlags.RUN_FIRST, None, ()),
    }

    def __init__(self):
        _RatbagdDBus.__init__(self, "Manager", None)
        result = self._get_dbus_property("Devices") or []
        self._devices = [RatbagdDevice(objpath) for objpath in result]
        self._proxy.connect("notify::g-name-owner", self._on_name_owner_changed)

    def _on_name_owner_changed(self, *kwargs):
        self.emit("daemon-disappeared")

    def _on_properties_changed(self, proxy, changed_props, invalidated_props):
        if "Devices" in changed_props.keys():
            object_paths = [d._object_path for d in self._devices]
            for object_path in changed_props["Devices"]:
                if object_path not in object_paths:
                    device = RatbagdDevice(object_path)
                    self._devices.append(device)
                    self.emit("device-added", device)
            for device in self.devices:
                if device._object_path not in changed_props["Devices"]:
                    self._devices.remove(device)
                    self.emit("device-removed", device)
            self.notify("devices")

    @GObject.Property
    def devices(self):
        """A list of RatbagdDevice objects supported by ratbagd."""
        return self._devices

    def __getitem__(self, id):
        """Returns the requested device, or None."""
        for d in self.devices:
            if d.id == id:
                return d
        return None

    @GObject.Property
    def themes(self):
        """A list of theme names. The theme 'default' is guaranteed to be
        available."""
        return self._get_dbus_property("Themes") or []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        pass


class RatbagdDevice(_RatbagdDBus):
    """Represents a ratbagd device."""

    __gsignals__ = {
        "active-profile-changed":
            (GObject.SignalFlags.RUN_FIRST, None, (GObject.TYPE_PYOBJECT,)),
        "resync":
            (GObject.SignalFlags.RUN_FIRST, None, ()),
    }

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Device", object_path)

        # FIXME: if we start adding and removing objects from this list,
        # things will break!
        result = self._get_dbus_property("Profiles") or []
        self._profiles = [RatbagdProfile(objpath) for objpath in result]
        for profile in self._profiles:
            profile.connect("notify::is-active", self._on_active_profile_changed)

    def _on_signal_received(self, proxy, sender_name, signal_name, parameters):
        if signal_name == "Resync":
            self.emit("resync")

    def _on_active_profile_changed(self, profile, pspec):
        if profile.is_active:
            self.emit("active-profile-changed", self._profiles[profile.index])

    @GObject.Property
    def id(self):
        """The unique identifier of this device."""
        return self._get_dbus_property("Id")

    @GObject.Property
    def name(self):
        """The device name, usually provided by the kernel."""
        return self._get_dbus_property("Name")

    @GObject.Property
    def profiles(self):
        """A list of RatbagdProfile objects provided by this device."""
        return self._profiles

    @GObject.Property
    def active_profile(self):
        """The currently active profile. This is a non-DBus property computed
        over the cached list of profiles. In the unlikely case that your device
        driver is misconfigured and there is no active profile, this returns
        the first profile."""
        for profile in self._profiles:
            if profile.is_active:
                return profile
        print("No active profile. Please report this bug to the libratbag developers", file=sys.stderr)
        return self._profiles[0]

    def get_svg_fd(self, theme):
        """Returns a File-like object to the SVG for the given theme, or
        raises FileNotFoundError if the file is not available.

        The theme must be one of org.freedesktop.ratbag1.Manager.Themes. The
        theme 'default' is guaranteed to be available (but the file itself
        may not be).

        @param theme The theme from which to retrieve the SVG, as str
        """
        fd = self._dbus_call_return_one_fd("GetSvgFd", "s", theme)
        return os.fdopen(fd)

    def commit(self):
        """Commits all changes made to the device.

        This is implemented asynchronously inside ratbagd. Hence, we just call
        this method and always succeed.  Any failure is handled inside ratbagd
        by emitting the Resync signal, which automatically resynchronizes the
        device. No further interaction is required by the client.
        """
        self._dbus_call("Commit", "")
        for profile in self._profiles:
            if profile.dirty:
                profile._dirty = False
                profile.notify("dirty")


class RatbagdProfile(_RatbagdDBus):
    """Represents a ratbagd profile."""

    CAP_WRITABLE_NAME = 100
    CAP_SET_DEFAULT = 101
    CAP_DISABLE = 102
    CAP_WRITE_ONLY = 103

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Profile", object_path)
        self._dirty = False
        self._active = self._get_dbus_property("IsActive")

        # FIXME: if we start adding and removing objects from any of these
        # lists, things will break!
        result = self._get_dbus_property("Resolutions") or []
        self._resolutions = [RatbagdResolution(objpath) for objpath in result]
        self._subscribe_dirty(self._resolutions)

        result = self._get_dbus_property("Buttons") or []
        self._buttons = [RatbagdButton(objpath) for objpath in result]
        self._subscribe_dirty(self._buttons)

        result = self._get_dbus_property("Leds") or []
        self._leds = [RatbagdLed(objpath) for objpath in result]
        self._subscribe_dirty(self._leds)

    def _subscribe_dirty(self, objects):
        for obj in objects:
            obj.connect("notify", self._on_obj_notify)

    def _on_obj_notify(self, obj, pspec):
        if not self._dirty:
            self._dirty = True
            self.notify("dirty")

    def _on_properties_changed(self, proxy, changed_props, invalidated_props):
        if "IsActive" in changed_props.keys():
            active = changed_props["IsActive"]
            if active != self._active:
                self._active = active
                self.notify("is-active")
                self._on_obj_notify(None, None)

    @GObject.Property
    def capabilities(self):
        """The capabilities of this profile as an array. Capabilities not
        present on the profile are not in the list. Thus use e.g.

        if RatbagdProfile.CAP_WRITABLE_NAME is in profile.capabilities:
            do something
        """
        return self._get_dbus_property("Capabilities") or []

    @GObject.Property
    def name(self):
        """The name of the profile"""
        return self._get_dbus_property("Name")

    @name.setter
    def name(self, name):
        """Set the name of this profile.

        @param name The new name, as str"""
        self._set_dbus_property("Name", "s", name)

    @GObject.Property
    def index(self):
        """The index of this profile."""
        return self._get_dbus_property("Index")

    @GObject.Property
    def dirty(self):
        """Whether this profile is dirty."""
        return self._dirty

    @GObject.Property
    def enabled(self):
        """tells if the profile is enabled."""
        return self._get_dbus_property("Enabled")

    @enabled.setter
    def enabled(self, enabled):
        """Enable/Disable this profile.

        @param enabled The new state, as boolean"""
        self._set_dbus_property("Enabled", "b", enabled)

    @GObject.Property
    def resolutions(self):
        """A list of RatbagdResolution objects with this profile's resolutions.
        Note that the list of resolutions differs between profiles but the number
        of resolutions is identical across profiles."""
        return self._resolutions

    @GObject.Property
    def active_resolution(self):
        """The currently active resolution of this profile. This is a non-DBus
        property computed over the cached list of resolutions. In the unlikely
        case that your device driver is misconfigured and there is no active
        resolution, this returns the first resolution."""
        for resolution in self._resolutions:
            if resolution.is_active:
                return resolution
        print("No active resolution. Please report this bug to the libratbag developers", file=sys.stderr)
        return self._resolutions[0]

    @GObject.Property
    def buttons(self):
        """A list of RatbagdButton objects with this profile's button mappings.
        Note that the list of buttons differs between profiles but the number
        of buttons is identical across profiles."""
        return self._buttons

    @GObject.Property
    def leds(self):
        """A list of RatbagdLed objects with this profile's leds. Note that the
        list of leds differs between profiles but the number of leds is
        identical across profiles."""
        return self._leds

    @GObject.Property
    def is_active(self):
        """Returns True if the profile is currenly active, false otherwise."""
        return self._active

    def set_active(self):
        """Set this profile to be the active profile."""
        ret = self._dbus_call("SetActive", "")
        self._set_dbus_property("IsActive", "b", True, readwrite=False)
        return ret


class RatbagdResolution(_RatbagdDBus):
    """Represents a ratbagd resolution."""

    CAP_INDIVIDUAL_REPORT_RATE = 1
    CAP_SEPARATE_XY_RESOLUTION = 2

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Resolution", object_path)
        self._active = self._get_dbus_property("IsActive")
        self._default = self._get_dbus_property("IsDefault")

    def _on_properties_changed(self, proxy, changed_props, invalidated_props):
        if "IsActive" in changed_props.keys():
            active = changed_props["IsActive"]
            if active != self._active:
                self._active = active
                self.notify("is-active")
        elif "IsDefault" in changed_props.keys():
            default = changed_props["IsDefault"]
            if default != self._default:
                self._default = default
                self.notify("is-default")

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
        return self._get_dbus_property("Capabilities") or []

    @GObject.Property
    def resolution(self):
        """The tuple (xres, yres) with each resolution in DPI."""
        return self._get_dbus_property("Resolution")

    @resolution.setter
    def resolution(self, res):
        """Set the x- and y-resolution using the given (xres, yres) tuple.

        @param res The new resolution, as (int, int)
        """
        self._set_dbus_property("Resolution", "(uu)", res)

    @GObject.Property
    def report_rate(self):
        """The report rate in Hz."""
        return self._get_dbus_property("ReportRate")

    @report_rate.setter
    def report_rate(self, rate):
        """Set the report rate in Hz.

        @param rate The new report rate, as int
        """
        self._set_dbus_property("ReportRate", "u", rate)

    @GObject.Property
    def resolutions(self):
        """The list of supported DPI values"""
        return self._get_dbus_property("Resolutions") or []

    @GObject.Property
    def report_rates(self):
        """The list of supported report rates"""
        return self._get_dbus_property("ReportRates") or []

    @GObject.Property
    def is_active(self):
        """True if this is the currently active resolution, False
        otherwise"""
        return self._active

    @GObject.Property
    def is_default(self):
        """True if this is the currently default resolution, False
        otherwise"""
        return self._default

    def set_default(self):
        """Set this resolution to be the default."""
        ret = self._dbus_call("SetDefault", "")
        self._set_dbus_property("IsDefault", "b", True, readwrite=False)
        return ret

    def set_active(self):
        """Set this resolution to be the active one."""
        ret = self._dbus_call("SetActive", "")
        self._set_dbus_property("IsActive", "b", True, readwrite=False)
        return ret


class RatbagdButton(_RatbagdDBus):
    """Represents a ratbagd button."""

    ACTION_TYPE_NONE = 0
    ACTION_TYPE_BUTTON = 1
    ACTION_TYPE_SPECIAL = 2
    ACTION_TYPE_MACRO = 4

    ACTION_SPECIAL_INVALID = -1
    ACTION_SPECIAL_UNKNOWN = (1 << 30)
    ACTION_SPECIAL_DOUBLECLICK = (1 << 30) + 1
    ACTION_SPECIAL_WHEEL_LEFT = (1 << 30) + 2
    ACTION_SPECIAL_WHEEL_RIGHT = (1 << 30) + 3
    ACTION_SPECIAL_WHEEL_UP = (1 << 30) + 4
    ACTION_SPECIAL_WHEEL_DOWN = (1 << 30) + 5
    ACTION_SPECIAL_RATCHET_MODE_SWITCH = (1 << 30) + 6
    ACTION_SPECIAL_RESOLUTION_CYCLE_UP = (1 << 30) + 7
    ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN = (1 << 30) + 8
    ACTION_SPECIAL_RESOLUTION_UP = (1 << 30) + 9
    ACTION_SPECIAL_RESOLUTION_DOWN = (1 << 30) + 10
    ACTION_SPECIAL_RESOLUTION_ALTERNATE = (1 << 30) + 11
    ACTION_SPECIAL_RESOLUTION_DEFAULT = (1 << 30) + 12
    ACTION_SPECIAL_PROFILE_CYCLE_UP = (1 << 30) + 13
    ACTION_SPECIAL_PROFILE_CYCLE_DOWN = (1 << 30) + 14
    ACTION_SPECIAL_PROFILE_UP = (1 << 30) + 15
    ACTION_SPECIAL_PROFILE_DOWN = (1 << 30) + 16
    ACTION_SPECIAL_SECOND_MODE = (1 << 30) + 17
    ACTION_SPECIAL_BATTERY_LEVEL = (1 << 30) + 18

    MACRO_KEY_PRESS = 1
    MACRO_KEY_RELEASE = 2
    MACRO_WAIT = 3

    """A table mapping a button's index to its usual function as defined by X
    and the common desktop environments."""
    BUTTON_DESCRIPTION = {
        0: N_("Left mouse button click"),
        1: N_("Right mouse button click"),
        2: N_("Middle mouse button click"),
        3: N_("Backward"),
        4: N_("Forward"),
    }

    """A table mapping a special function to its human-readable description."""
    SPECIAL_DESCRIPTION = {
        ACTION_SPECIAL_INVALID: N_("Invalid"),
        ACTION_SPECIAL_UNKNOWN: N_("Unknown"),
        ACTION_SPECIAL_DOUBLECLICK: N_("Doubleclick"),
        ACTION_SPECIAL_WHEEL_LEFT: N_("Wheel Left"),
        ACTION_SPECIAL_WHEEL_RIGHT: N_("Wheel Right"),
        ACTION_SPECIAL_WHEEL_UP: N_("Wheel Up"),
        ACTION_SPECIAL_WHEEL_DOWN: N_("Wheel Down"),
        ACTION_SPECIAL_RATCHET_MODE_SWITCH: N_("Ratchet Mode"),
        ACTION_SPECIAL_RESOLUTION_CYCLE_UP: N_("Cycle Resolution Up"),
        ACTION_SPECIAL_RESOLUTION_CYCLE_DOWN: N_("Cycle Resolution Down"),
        ACTION_SPECIAL_RESOLUTION_UP: N_("Resolution Up"),
        ACTION_SPECIAL_RESOLUTION_DOWN: N_("Resolution Down"),
        ACTION_SPECIAL_RESOLUTION_ALTERNATE: N_("Resolution Switch"),
        ACTION_SPECIAL_RESOLUTION_DEFAULT: N_("Default Resolution"),
        ACTION_SPECIAL_PROFILE_CYCLE_UP: N_("Cycle Profile Up"),
        ACTION_SPECIAL_PROFILE_CYCLE_DOWN: N_("Cycle Profile Down"),
        ACTION_SPECIAL_PROFILE_UP: N_("Profile Up"),
        ACTION_SPECIAL_PROFILE_DOWN: N_("Profile Down"),
        ACTION_SPECIAL_SECOND_MODE: N_("Second Mode"),
        ACTION_SPECIAL_BATTERY_LEVEL: N_("Battery Level"),
    }

    def __init__(self, object_path):
        _RatbagdDBus.__init__(self, "Button", object_path)

    def _on_properties_changed(self, proxy, changed_props, invalidated_props):
        if "ActionType" in changed_props.keys():
            self.notify("action-type")

    @GObject.Property
    def index(self):
        """The index of this button."""
        return self._get_dbus_property("Index")

    @GObject.Property
    def mapping(self):
        """An integer of the current button mapping, if mapping to a button."""
        return self._get_dbus_property("ButtonMapping")

    @mapping.setter
    def mapping(self, button):
        """Set the button mapping to the given button.

        @param button The button to map to, as int
        """
        self._set_dbus_property("ButtonMapping", "u", button)

    @GObject.Property
    def macro(self):
        """A RatbagdMacro object representing the currently set macro."""
        return RatbagdMacro.from_ratbag(self._get_dbus_property("Macro"))

    @macro.setter
    def macro(self, macro):
        """Set the macro to the macro represented by the given RatbagdMacro
        object.

        @param macro A RatbagdMacro object representing the macro to apply to
                     the button, as RatbagdMacro.
        """
        self._set_dbus_property("Macro", "a(uu)", macro.keys)

    @GObject.Property
    def special(self):
        """An enum describing the current special mapping, if mapped to special."""
        return self._get_dbus_property("SpecialMapping")

    @special.setter
    def special(self, special):
        """Set the button mapping to the given special entry.

        @param special The special entry, as one of RatbagdButton.ACTION_SPECIAL_*
        """
        self._set_dbus_property("SpecialMapping", "u", special)

    @GObject.Property
    def action_type(self):
        """An enum describing the action type of the button. One of
        ACTION_TYPE_NONE, ACTION_TYPE_BUTTON, ACTION_TYPE_SPECIAL,
        ACTION_TYPE_MACRO. This decides which
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


class RatbagdMacro(GObject.Object):
    """Represents a button macro. Note that it uses keycodes as defined by
    linux/input.h and not those used by X.Org or any other higher layer such as
    Gdk."""

    # All keys from ecodes.KEY have a KEY_ prefix. We strip it.
    _PREFIX_LEN = len("KEY_")

    # Both a key press and release.
    _MACRO_KEY = 1000

    _MACRO_DESCRIPTION = {
        RatbagdButton.MACRO_KEY_PRESS: lambda key:
            "↓{}".format(ecodes.KEY[key][RatbagdMacro._PREFIX_LEN:]),
        RatbagdButton.MACRO_KEY_RELEASE: lambda key:
            "↑{}".format(ecodes.KEY[key][RatbagdMacro._PREFIX_LEN:]),
        RatbagdButton.MACRO_WAIT: lambda val:
            "{}ms".format(val),
        _MACRO_KEY: lambda key:
            "↕{}".format(ecodes.KEY[key][RatbagdMacro._PREFIX_LEN:]),
    }

    __gsignals__ = {
        'macro-set': (GObject.SignalFlags.RUN_FIRST, None, ()),
    }

    def __init__(self, **kwargs):
        GObject.Object.__init__(self, **kwargs)
        self._macro = []

    def __str__(self):
        if not self._macro:
            # Translators: this is used when there is no macro to preview.
            return _("None")

        keys = []
        idx = 0
        while idx < len(self._macro):
            t, v = self._macro[idx]
            try:
                if t == RatbagdButton.MACRO_KEY_PRESS:
                    # Check for a paired press/release event
                    t2, v2 = self._macro[idx + 1]
                    if t2 == RatbagdButton.MACRO_KEY_RELEASE and v == v2:
                        t = self._MACRO_KEY
                        idx += 1
            except IndexError:
                pass
            keys.append(self._MACRO_DESCRIPTION[t](v))
            idx += 1
        return " ".join(keys)

    @GObject.Property
    def keys(self):
        """A list of (RatbagdButton.MACRO_*, value) tuples representing the
        current macro."""
        return self._macro

    @staticmethod
    def from_ratbag(macro):
        """Instantiates a new RatbagdMacro instance from the given macro in
        libratbag format.

        @param macro The macro in libratbag format, as
                     [(RatbagdButton.MACRO_*, value)].
        """
        ratbagd_macro = RatbagdMacro()

        # Do not emit notify::keys for every key that we add.
        with ratbagd_macro.freeze_notify():
            for (type, value) in macro:
                ratbagd_macro.append(type, value)
        return ratbagd_macro

    def accept(self):
        """Applies the currently cached macro."""
        self.emit("macro-set")

    def append(self, type, value):
        """Appends the given event to the current macro.

        @param type The type of event, as one of RatbagdButton.MACRO_*.
        @param value If the type denotes a key event, the X.Org or Gdk keycode
                     of the event, as int. Otherwise, the value of the timeout
                     in milliseconds, as int.
        """
        # Only append if the entry isn't identical to the last one, as we cannot
        # e.g. have two identical key presses in a row.
        if len(self._macro) == 0 or (type, value) != self._macro[-1]:
            self._macro.append((type, value))
            self.notify("keys")


class RatbagdLed(_RatbagdDBus):
    """Represents a ratbagd led."""

    TYPE_LOGO = 1
    TYPE_SIDE = 2
    TYPE_BATTERY = 3
    TYPE_DPI = 4
    TYPE_WHEEL = 5

    MODE_OFF = 0
    MODE_ON = 1
    MODE_CYCLE = 2
    MODE_BREATHING = 3

    COLORDEPTH_MONOCHROME = 400
    COLORDEPTH_RGB_888 = 401
    COLORDEPTH_RGB_111 = 402

    LED_DESCRIPTION = {
        # Translators: the LED is off.
        MODE_OFF: N_("Off"),
        # Translators: the LED has a single, solid color.
        MODE_ON: N_("Solid"),
        # Translators: the LED is cycling between red, green and blue.
        MODE_CYCLE: N_("Cycle"),
        # Translators: the LED's is pulsating a single color on different
        # brightnesses.
        MODE_BREATHING: N_("Breathing"),
    }

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
        self._set_dbus_property("Mode", "u", mode)

    @GObject.Property
    def modes(self):
        """The supported modes as a list"""
        return self._get_dbus_property("Modes")

    @GObject.Property
    def type(self):
        """An enum describing this led's type,
        RatbagdLed.TYPE_LOGO or RatbagdLed.TYPE_SIDE."""
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
        self._set_dbus_property("Color", "(uuu)", color)

    @GObject.Property
    def colordepth(self):
        """An enum describing this led's colordepth, one of
        RatbagdLed.COLORDEPTH_MONOCHROME, RatbagdLed.COLORDEPTH_RGB"""
        return self._get_dbus_property("ColorDepth")

    @GObject.Property
    def effect_duration(self):
        """The LED's effect duration in ms, values range from 0 to 10000."""
        return self._get_dbus_property("EffectDuration")

    @effect_duration.setter
    def effect_duration(self, effect_duration):
        """Set the effect duration in ms. Allowed values range from 0 to 10000.

        @param effect_duration The new effect duration, as int
        """
        self._set_dbus_property("EffectDuration", "u", effect_duration)

    @GObject.Property
    def brightness(self):
        """The LED's brightness, values range from 0 to 255."""
        return self._get_dbus_property("Brightness")

    @brightness.setter
    def brightness(self, brightness):
        """Set the brightness. Allowed values range from 0 to 255.

        @param brightness The new brightness, as int
        """
        self._set_dbus_property("Brightness", "u", brightness)
