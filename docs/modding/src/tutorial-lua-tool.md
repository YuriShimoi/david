# Tutorial: Checker Brush (Lua tool)

A painting tool that only paints every other pixel, in a checkerboard. The finished mod is `mods/examples/checker-brush`.

## How tools are built

A David tool has three parts: the **controller** (how pointer input becomes positions: freehand here), the **point shape** (the round brush, sized by the tool bar's width) and the **ink** (what a covered pixel becomes).
A Lua tool keeps the Paintbrush's controller and point shape and gives its own ink.

## The code

`mod.json` is the same as in the [command tutorial](tutorial-lua-command.html), with `"entry": "main.lua"`. Then:

```lua
david.tool{
  id = "checker-brush",
  name = "Checker Brush",
  ink = function(x, y, before, color)
    if (x + y) % 2 == 0 then
      return color
    end
    -- returning nothing leaves the pixel as it was
  end,
}
```

`ink(x, y, before, color)` gets the pixel's position, its color before the stroke and the current color (primary for the left button, secondary for the right). It returns the new color, or nothing to leave the pixel.
David mixes your result in by the brush's coverage, so antialiased edges stay smooth, and each pixel is computed once per stroke from its `before` value, so going over it again in the same stroke doesn't build up.

Restart David: the tool appears at the end of the tool strip with a puzzle icon. Add `key = "Q"` to give it a one-letter shortcut.

![The Checker Brush on the canvas](img/checker.png)

## Ideas

- Shading: return a lighter or darker version of `before`.
- Patterns: use `x % 4` and `y % 4` to stamp a texture.
- Symmetry and tiled mode work with mod tools too: they happen before the ink is called.
