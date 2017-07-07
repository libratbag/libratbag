libratbag gnome theme
====================

Requirements
------------

See the Logitech G403 for an example of a device with a small amount of buttons,
and the Logitech G700 for an example of a device with a large amount of buttons.

- Canvas size should be between 400x400 and 500x500 pixels.
- Three layers in the final SVG:
  1. a lower layer named "Device" with the device itself. Each button on the
  device should have an id `buttonX`, with `X` being the number of the button
  (so for button 0 the id would be `button0`). Similarly, each LED on the device
  should have an id `ledX`.
  2. a middle layer named "Buttons" with the button leaders (see below for
  leaders).
  3. an upper layer named "LEDs" with the LED leaders.
- A leader line is a path that extends from the button or LED to the left or the
  right of the device (see below). Each leader line requires the following:
  - It should start with a 7x7 square placed on or close to the button or LED
    that it maps with.
  - From this square, a path should extend left or right (see below).
  - Each path should end with a 1x1 pixel with identifier `buttonX-leader` (or
    `ledX-leader`), where `X` is the number of the button (or LED) with
    which the leader maps. For button 0, this would be `button0-leader`.
  - All these elements should be grouped and given the identifier `buttonX-path`
    (or `ledX-path` for LEDs)
- Leader lines should have a vertical spacing of at least 40 pixels. When there
  are several leader lines above and below each other, make the spacing between
  them equal.
- If the device's scroll wheel supports horizontal tilting, add two small arrows
  left and right of the scroll wheel with the respective button identifiers (see
  the Logitech G700 for an example). Do not cut the scroll wheel in half
  vertically to map these buttons.
- If there aren't too many buttons, preferably make the leaders point to the
  right with the device itself placed on the left. If the buttons would extend
  below or above the device, make some point to the left instead with the device
  itself centered in the middle. In this case, half of the leaders should extend
  to the left and the other half to the right.
- When a leader points to the right, its 1x1 pixel should have a style property
  `text-align:start`. When a leader points to the left, its 1x1 pixel should
  have a style property `text-align:end`.
- The canvas should be resized so that there is a 20px gap between the device
  and the edge of the canvas and no gap between the 1x1 pixels and the canvas.

Technique
---------

The simplest approach is to find a photo of the device and import it into
inkscape. Put it on the lowest layer, create a new layer "Device" above it
and start tracing the outlines and edges of the device. Fill in the shapes
and your device should resemble the underlying photo. Delete the photo
layer, add leaders in their respective layers and you're done.

Make sure the image looks ''toned-down'' and not realistic. Do not use dark or
bright colors.

