# Vector objects: Star Generator

Vector layers hold objects instead of pixels: rectangles, ellipses, polygons and stars, paths and text, each with a fill, a line and a transform. They stay sharp at any zoom and stay editable with the Mouse and Node tools. Vector support is a beta feature: turn it on in **Settings > Beta tools > Vector support**.

This tutorial builds a command that adds a row of stars to a vector layer. The finished mod is `mods/examples/star-generator`.

## 1. Objects from Lua

A vector layer's objects are plain tables. `layer:objects()` lists them (bottom to top), `layer:addObject(fields)` adds one on top and returns its id, `layer:setObject(id, fields)` changes some fields and `layer:removeObject(id)` deletes it.

| Kind | Fields |
|---|---|
| `rect` | `x`, `y`, `w`, `h`, `radius` (rounded corners) |
| `ellipse` | `x`, `y`, `w`, `h` (the box around it) |
| `polygon` | `x`, `y`, `w`, `h`, `sides`, `inner` (below 1 makes a star) |
| `path` | `d`: SVG path data, like `"M0 0 L10 0 L10 10 Z"` |
| `text` | `text`, `font` (a font file), `size`, `x`, `y` |

Every kind also takes `name`, `fill` and `stroke` (colors from `david.rgba`, or `false` for none), `width` (the line width), `opacity` (0 to 1) and `transform` (an SVG transform list, like `"rotate(45 50 50)"`).

## 2. mod.json

```json
{
  "name": "Star Generator",
  "version": "1.0.0",
  "api": 1,
  "kind": "lua",
  "entry": "main.lua"
}
```

## 3. The command

```lua
david.command{
  id = "star-generator",
  name = "Star Generator",
  menu = "Mods/Vector",
  params = {
    { id = "count", label = "Stars", type = "int", min = 1, max = 20, default = 3 },
    { id = "points", label = "Points", type = "int", min = 3, max = 24, default = 5 },
    { id = "inner", label = "Inner radius %", type = "int", min = 10, max = 100, default = 45 },
    { id = "color", label = "Fill", type = "color", default = 0 },
  },
  run = function(doc, p)
    -- Draw on the active layer when it is a vector layer, else on a new one
    local layer = doc:layer()
    if not layer.isVector then layer = doc:addVectorLayer("Stars") end
    -- One row across the middle of the image
    local cell = doc.width / p.count
    local size = math.min(cell, doc.height) * 0.8
    for i = 0, p.count - 1 do
      layer:addObject({
        kind = "polygon",
        name = "Star",
        x = cell * i + (cell - size) / 2, y = (doc.height - size) / 2, w = size, h = size,
        sides = p.points, inner = p.inner / 100,
        fill = p.color,
        stroke = david.rgba(0, 0, 0), width = math.max(1, size / 40),
      })
    end
    david.log("star generator: " .. #layer:objects() .. " objects on " .. layer.name)
  end,
}
```

`layer.isVector` tells a vector layer from a raster one, and `doc:addVectorLayer(name)` adds one above the active layer. Everything the command adds (the layer and the stars) is one undo step, like any other command.

## 4. SVG in and out

`layer:svg()` gives a vector layer's objects as an SVG document, and `layer:addSvg(text)` adds the objects of an SVG document on top. That is the easy way to bring in shapes made elsewhere:

```lua
layer:addSvg('<svg xmlns="http://www.w3.org/2000/svg"><circle cx="20" cy="20" r="10" fill="red"/></svg>')
```

From the command line, `david-cli` reads and writes SVG too: `david-cli icon.svg --resize 256x256 -o icon.png` renders an SVG sharp at any size, and `david-cli sprite.png --trace pixel -o sprite.svg` turns pixel art into one shape per color region.

## 5. Try it

Turn on vector support, open any image and run **Mods > Vector > Star Generator**. Pick the stars with the Mouse tool to move, scale, rotate or recolor them, or bend them with the Node tool (N).
