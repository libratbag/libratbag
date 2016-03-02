libratbag data files
====================

These SVG files should approximate the device to make it immediately
recognizable. Detail work not required, the graphic is an illustration only.

Requirements
------------

- Canvas size: 750x750 pixels
- Two layers in the final SVG: a lower layer named "Device" with the device
  itself, "Leaders" with the buttons and leader-lines
- The device should be approximately centered on the canvas and occupy a
  meaningful portion of the canvas without making it look cramped
- Button labels must be in 12pt Sans, starting with Button0
- Wheel buttons may be labeled separately if required (look at the Etekcity
  for an example)

Technique
---------

The simplest approach is to find a photo of the device and import it into
inkscape. Put it on the lowest layer, create a new layer "Device" above it
and start tracing the outlines and edges of the device. Fill in the shapes
and your device should resemble the underlying photo. Delete the photo
layer, add leaders and button labels and you're done.
