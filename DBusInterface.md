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

The **org.freedesktop.ratbag1.Manager** interface provides:
- Properties:
  - Devices -> array of object paths with interface Device
  - Themes -> array of strings with theme names. The theme 'default' is
              guaranteed to be available.
- Signals:
  - DeviceNew -> new device available, carries object path
  - DeviceRemoved -> device removed, carries object path

The **org.freedesktop.ratbag1.Device** interface provides:
- Properties:
  - Id -> unique ID of this device
  - Capabilities -> array of uints with the capabilities enum from libratbag
  - Description -> device name
  - Svg -> device SVG name (file only)
  - SvgPath -> device SVG name (full absolute path)
  - Profiles -> array of object paths with interface Profile
  - ActiveProfile -> index of the currently active profile in Profiles
- Methods:
  - GetProfileByIndex(uint) -> returns the object path for the given index
  - Commit() -> commit the changes to the device
  - GetSvg(string) -> returns the full path to the SVG for the given theme
       or an empty string if none is available.  The theme must be one of
       org.freedesktop.ratbag1.Manager.Themes. The theme 'default' is
       guaranteed to be available. ratbagd may return the path to a
       file that doesn't exist. This is the case if the device has SVGs
       available but not for the given theme.

The **org.freedesktop.ratbag1.Profile** interface provides:
- Properties:
  - Index -> index of the profile
  - Resolutions -> array of object paths with interface Resolution
  - Buttons -> array of object paths with interface Button
  - Leds -> array of object paths with interface Led
  - ActiveResolution -> index of the currently active resolution in Resolutions
  - DefaultResolution -> index of the default resolution in Resolutions
- Methods:
  - SetActive(void) -> set this profile to be the active profile
  - GetResolutionByIndex(uint) -> returns the object path for the given index
- Signals:
  - ActiveProfileChanged -> active profile changed, carries index of the new active profile

The **org.freedesktop.ratbag1.Resolution** interface provides:
- Properties:
  - Index -> index of the resolution
  - Capabilities -> array of uints with the capabilities enum from libratbag
  - XResolution -> uint for the x resolution assigned to this entry
  - YResolution -> uint for the y resolution assigned to this entry
  - ReportRate -> uint for the report rate in Hz assigned to this entry
  - Maximum -> uint for the maximum possible resolution
  - Minimum -> uint for the minimum possible resolution
- Methods:
  - SetResolution(uint, uint) -> x/y resolution to assign
  - SetReportRate(uint) -> uint for the report rate to assign
  - SetDefault -> set this resolution to be the default
- Signals:
  - ActiveResolutionChanged -> active resolution changed, carries index of the new active resolution
  - DefaultResolutionChanged -> default resolution changed, carries index of the new default resolution

The **org.freedesktop.ratbag1.Button** interface provides:
- Properties:
  - Index -> index of the button
  - Type -> string describing the button type
  - ButtonMapping -> uint of the current button mapping (if mapping to button)
  - SpecialMapping -> string of the current special mapping (if mapped to special)
  - KeyMapping -> array of uints, first entry is the keycode, other entries, if any, are modifiers (if mapped to key)
  - ActionType -> string describing the action type of the button ("none", "button", "key", "special", "macro", "unknown"). This decides which \*Mapping  property has a value
  - ActionTypes -> array of strings, possible values for ActionType
- Methods:
  - SetButtonMapping(uint) -> set the button mapping to the given button
  - SetSpecialMapping(string) -> set the button mapping to the given special entry
  - SetKeyMapping(uint[]) -> set the key mapping, first entry is the keycode, other entries, if any, are modifier keycodes
  - Disable(void) -> disable this button

The **org.freedesktop.ratbag1.Led** interface provides:
- Properties:
  - Index -> index of the LED
  - Mode -> uint mapping to the mode enum from libratbag
  - Type -> string describing the LED type
  - Color -> uint triplet (RGB) of the LED's color
  - EffectRate -> the effect rate in Hz, possible values are in the range 100 - 20000
  - Brightness -> the brightness of the LED, possible values are in the range 0 - 255
- Methods:
  - SetMode(uint) -> set the mode to the given mode enum value from libratbag
  - SetColor((uuu)) -> set the color to the given uint triplet (RGB)
  - SetEffectRate(uint) -> set the effect rate in Hz, possible values are in the range 100 - 20000
  - SetBrightness(int) -> set the brightness, possible values are in the range 0 - 255

For easier debugging, objects paths are constructed from the device. e.g.
`/org/freedesktop/ratbag/button/event5/p0/b10` is the button interface for
button 10 on profile 0 on event5. The naming is subject to change. Do not
rely on a constructed object path in your application.


