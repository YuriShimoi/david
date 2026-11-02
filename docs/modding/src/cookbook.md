# Cookbook

## Read every pixel in the selection (Lua command)

```lua
run = function(doc)
  local x0, y0, w, h = doc:selectionBounds()
  if not x0 then x0, y0, w, h = 0, 0, doc.width, doc.height end
  local layer = doc:layer()
  for y = y0, y0 + h - 1 do
    for x = x0, x0 + w - 1 do
      if doc:selected(x, y) > 0 then
        local r, g, b, a = david.channels(layer:get(x, y))
        -- ...
      end
    end
  end
end
```

## An effect with a margin (Lua)

An effect that reads neighbors needs pixels around the selection. Give it a margin, and `img:get` can read that far out:

```lua
david.effect{
  id = "box-blur-3", name = "Box Blur 3×3", margin = 1,
  run = function(img, p)
    for y = 0, img.height - 1 do
      for x = 0, img.width - 1 do
        local sr, sg, sb, sa = 0, 0, 0, 0
        for dy = -1, 1 do for dx = -1, 1 do
          local r, g, b, a = david.channels(img:get(x + dx, y + dy))
          sr, sg, sb, sa = sr + r, sg + g, sb + b, sa + a
        end end
        img:set(x, y, david.rgba(sr // 9, sg // 9, sb // 9, sa // 9))
      end
    end
  end,
}
```

## Add a layer and fill it

```lua
david.command{ id = "red-layer", name = "Red Layer", run = function(doc)
  local layer = doc:addLayer("Red")
  layer.blend = "multiply"
  layer.opacity = 128
  for y = 0, doc.height - 1 do for x = 0, doc.width - 1 do layer:set(x, y, david.rgba(255, 0, 0)) end end
end }
```

## Run a built-in effect from a command

```lua
run = function(doc)
  doc:applyEffect("gaussian-blur", { radius = 4 })
  doc:applyEffect("posterize", { levels = 6 })
end
```

## Do something on every frame (Lua command)

A document can be an animation. Layers read and write the active frame, so a command loops over the frames by moving `doc.activeFrame`; it's still one undo step.

```lua
david.command{ id = "flash", name = "Flash Every Other Frame", run = function(doc)
  local layer = doc:layer()
  for f = 2, doc:frameCount(), 2 do
    doc.activeFrame = f
    for y = 0, doc.height - 1 do
      for x = 0, doc.width - 1 do
        local r, g, b, a = david.channels(layer:get(x, y))
        if a > 0 then layer:set(x, y, david.rgba(255, 255, 255, a)) end
      end
    end
  end
end }
```

From the CLI, `--all-frames` runs effects on every frame, and `--slice 16x16` turns a sprite sheet into frames:

```
david-cli walk-sheet.png --slice 16x16 --fps 10 -o walk.gif
david-cli walk.gif --effect outline:color=#000 --all-frames --sheet cols=8,json -o walk-outlined.png
```

## Batch-process a folder from the CLI

```
david-cli --batch "sprites/*.png" --resize 300% --filter nearest --effect outline:color=#000 --out-dir big/
```

Every match goes to `big/` with the same name. `--format webp` changes the format.

## A script on one image

`--script` runs a Lua file on the image, with `david.image` as the document and `--arg` values in `david.args`:

```lua
-- tint.lua: david-cli in.png --script tint.lua --arg amount=40 -o out.png
local doc = david.image
local amount = tonumber(david.args.amount or "20")
local layer = doc:layer()
for y = 0, doc.height - 1 do
  for x = 0, doc.width - 1 do
    local r, g, b, a = david.channels(layer:get(x, y))
    layer:set(x, y, david.rgba(math.min(255, r + amount), g, b, a))
  end
end
```

## Pipe images

`-` reads stdin or writes stdout (PNG unless `--format` says otherwise):

```
cat in.png | david-cli - --effect invert -o - > out.png
```

## Find an effect's parameters

```
david-cli --list-effects
david-cli --list-effects --json
```
