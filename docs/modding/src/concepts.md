# Concepts

## Pixels and coordinates

- Coordinates are image pixels from the top-left corner, starting at 0. `x` grows to the right, `y` down.
- A color is one 32-bit integer: straight (not premultiplied) RGBA, red in the low byte. In Lua use `david.rgba` and `david.channels` instead of doing the bit math.
- Layers are numbered from 1 (the bottom layer) in Lua, the usual Lua way.

## Tiles

An image is stored in tiles of 256 × 256 pixels, and an empty tile takes no memory. That's why a fresh 8000 × 8000 layer costs nothing until you paint on it. For mods it is invisible: `get` and `set` work anywhere in the image.

## The selection

The selection is a mask with a value from 0 to 255 per pixel, not a shape: soft edges (from the magic wand or a feathered lasso) are partly selected pixels.

- Effects only change pixels inside the selection, blended by the mask: a pixel selected at 128 gets half the effect. You don't do anything for that.
- An effect's image is the selection's bounding box plus the effect's margin. Outside the bounding box nothing changes.
- Commands see the mask through `doc:selected(x, y)` and `doc:selectionBounds()`.

## Undo transactions

Each effect run, command run or tool stroke is one History step. David records the tiles a step touches (before and after), so undo memory stays small even on big images.
Inside a Lua command every change goes into the same step, in order, and a Lua error undoes everything the command did.

## Color spaces

Colors are 8-bit sRGB. A few things work in other spaces:

- **Linear light** for resampling (Image > Resize, with its gamma option on).
- **OKLab**, a perceptual space, for the magic wand's and bucket's tolerance, gradients, palette matching in Dither, and Hue / Saturation. Mods can do their own conversions; the formulas are in [Björn Ottosson's OKLab article](https://bottosson.github.io/posts/oklab/).

## The sandbox

Lua mods run with only `math`, `string`, `table`, `utf8` and the safe parts of the base library. There is no `os`, `io`, `package`, `require`, `load`, `loadfile` or `dofile`, and compiled bytecode can't be loaded.
A Lua mod can only change the image it is given. Each call has a 10-second limit.

Shader mods only see the area they run on. Native plugins are not sandboxed (see the [native tutorial](tutorial-native.html)).
