The ratbagd DBus-Interface
--------------------------

Note: **the DBus interface is subject to change**

Interfaces:
-  org.freedesktop.ratbag1.Manager
-  org.freedesktop.ratbag1.Device
-  org.freedesktop.ratbag1.Profile
-  org.freedesktop.ratbag1.Resolution
-  org.freedesktop.ratbag1.Button
-  org.freedesktop.ratbag1.Led

For a list of dbus types as used below, see https://dbus.freedesktop.org/doc/dbus-specification.html

Properties marked as 'constant' do not change for the lifetime of the
object. Properties marked as 'mutable' may change, and a
`org.freedesktop.DBus.Properties.PropertyChanged` signal is sent for those
unless otherwise specified.

org.freedesktop.ratbag1.Manager
===============================

The **org.freedesktop.ratbag1.Manager** interface is the entry point to
interact with ratbagd.

### Properties

#### `Devices`
- type `ao`, read-only, mutable

Provides the object paths of all current devices, see **org.freedesktop.ratbag1.Device**

#### `Themes`
- type `as`, read-only, constant

Provides the list of available theme names. This list is guaranteed to have
one theme available ('default'). Other themes are implementation defined.
A theme listed here is only a guarantee that the theme is known to libratbag
and that SVGs *may* exist, it is not a guarantee that the SVG for any
specific device exists. In other words, a device may not have an SVG for a
specific theme.

This list is static for the lifetime of ratbagd.

org.freedesktop.ratbag1.Device
==============================

The **org.freedesktop.ratbag1.Device** interface describes a single device
known to ratbagd.

### Properties

#### `Id`
- type: `s`, read-only, constant

An ID describing this device. This ID should not be used for presentation to
the user.

#### `Description`
- type: `s`, read-only, constant

The device's name, suitable for presentation to the user.

#### `Capabilities`
- type: `au`, read-only, constant

The capabilities supported by this device. see `enum
ratbag_device_capability` in libratbag.h for the list of permissible
capabilities.


#### `Svg`
- type: `s`, read-only, constant

The device's SVG file name, without path.

#### `Profiles`
- type: `ao`, read-only, mutable

This property is mutable if the device supports adding and removing
profiles.

Provides the list of profile paths for all profiles on this device, see
**org.freedesktop.ratbag1.Profile**.

#### `Commit() → ()`

Commits the changes to the device. Changes to the device are batched; they
are not written to the hardware until `Commit()` is invoked.

#### `GetSvg(s) → (s)`

Returns the full path to the SVG for the given theme or an empty string if
none is available.  The theme must be one of
**org.freedesktop.ratbag1.Manager.Themes**. The theme 'default' is
guaranteed to be available. ratbagd may return the path to a file that
doesn't exist. This is the case if the device has SVGs available but not for
the given theme.

org.freedesktop.ratbag1.Profile
===============================

### Properties

#### `Index`
- type: `u`, read-only, constant

The index of this profile

#### `Enabled`
- type: `b`, read-write, mutable

True if this is the profile is enabled, false otherwise.

Note that a disabled profile might not have correct bindings, so it's
a good thing to rebind everything before calling `Commit()` on the
**org.freedesktop.ratbag1.Device**.

#### `Resolutions`
- type: `ao`, read-only, mutable

This property is mutable if the device supports adding and removing
resolutions.

Provides the object paths of all resolutions in this profile, see
**org.freedesktop.ratbag1.Resolution**

#### `Buttons`
- type: `ao`, read-only, constant

Provides the object paths of all buttons in this profile, see
**org.freedesktop.ratbag1.Button**

#### `Leds`
- type: `ao`, read-only, constant

Provides the object paths of all LEDs in this profile, see
**org.freedesktop.ratbag1.Led**

### `IsActive`
- type: `b`, read-only, mutable

True if this is the currently active profile, false otherwise.

Profiles can only be set to active, but never to not active - at least one
profile must be active at all times. This property is read-only, use the
`SetActive()` method to activate a profile.

### Methods

#### `SetActive() → ()`
Set this profile to be the active profile

org.freedesktop.ratbag1.Resolution
==================================

### Properties:
#### `Index`
- type: `u`, read-only, constant

Index of the resolution
#### `Capabilities`
- type: `au`, read-only, constant

Array of uints with the capabilities enum from libratbag

#### `IsActive`
- type: `b`, read-only, mutable

True if this is the currently active resolution, false otherwise.

Resolutions can only be set to active, but never to not active - at least
one resoultion must be active at all times. This property is read-only, use the
`SetActive()` method to set a resolution as the active resolution.

#### `IsDefault`
- type: `b`, read-only, mutable

True if this is the currently default resolution, false otherwise. If the
device does not have the default resolution capability, this property is
always false.

Resolutions can only be set to default, but never to not default - at least
one resolution must be default at all times. This property is read-only, use the
`SetDefault()` method to set a resolution as the default resolution.

#### `Resolution`
- type: `uu`, read-write, mutable

uint for the x and y resolution assigned to this entry, respectively

#### `ReportRate`
- type: `u`, read-write, mutable

uint for the report rate in Hz assigned to this entry

#### `Maximum`
- type: `u`, read-only, constant

uint for the maximum possible resolution
#### `Minimum`
- type: `u`, read-only, constant

uint for the minimum possible resolution

### Methods:

#### `SetDefault() → () `

Set this resolution to be the default

org.freedesktop.ratbag1.Button
==============================

### Properties:
#### `Index`
- type: `u`, read-only, constant

Index of the button
#### `Type`
- type: `s`, read-only, constant

String describing the button type
#### `ButtonMapping`
- type: `u`, read-only, mutable

uint of the current button mapping (if mapping to button)
#### `SpecialMapping`
- type: `s`, read-only, mutable

String of the current special mapping (if mapped to special)
#### `KeyMapping`
- type: `au`, read-only, mutable

Array of uints, first entry is the keycode, other entries, if any, are
modifiers (if mapped to key)
#### `ActionType`
- type: `u`, read-only, mutable

An enum describing the action type of the button, see
ratbag\_button\_get\_action\_type for the list of enums.
This decides which Mapping  property has a value.
#### `ActionTypes`
- type: `au`, read-only, constant
Array of enum ratbag\_button\_action\_type, possible values for ActionType

### Methods:
#### `SetButtonMapping(u) → ()`
Set the button mapping to the given button
#### `SetSpecialMapping(s) → ()`
Set the button mapping to the given special entry
#### `SetKeyMapping(au) → ()`
Set the key mapping, first entry is the keycode, other entries, if any, are
modifier keycodes
#### `Disable() → ()`
Disable this button

org.freedesktop.ratbag1.Led
===========================

### Properties:
#### `Index`
- type: `u`, read-only, constant

Index of the LED

#### `Mode`
- type: `u`, read-write, mutable

uint mapping to the mode enum from libratbag

#### `Type`
- type: `s`, read-only, mutable

String describing the LED type

#### `Color`
- type: `(uuu)`, read-write, mutable
uint triplet (RGB) of the LED's color

#### `EffectRate`
- type: `u`, read-write, mutable

The effect rate in Hz, possible values are in the range 100 - 20000

#### `Brightness`
- type: `u`, read-write, mutable

The brightness of the LED, possible values are in the range 0 - 255

For easier debugging, objects paths are constructed from the device. e.g.
`/org/freedesktop/ratbag/button/event5/p0/b10` is the button interface for
button 10 on profile 0 on event5. The naming is subject to change. Do not
rely on a constructed object path in your application.


