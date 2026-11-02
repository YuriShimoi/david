# Tutorial: Palette Swap (Lua command)

A command that replaces one color with another in the active layer, inside the selection. It asks for the two colors and a tolerance first, and the whole change is one undo step.
The finished mod is `mods/examples/palette-swap`.

## 1. mod.json

```json
{
  "name": "Palette Swap",
  "version": "1.0.0",
  "api": 1,
  "kind": "lua",
  "entry": "main.lua"
}
```

## 2. Registering the command

`main.lua` runs once when the mod loads. It registers things through the `david` table:

```lua
david.command{
  id = "palette-swap",
  name = "Palette Swap",
  menu = "Mods/Color",
  params = {
    { id = "from", label = "Replace", type = "color" },
    { id = "to", label = "With", type = "color" },
    { id = "tolerance", label = "Tolerance", type = "int", min = 0, max = 255, default = 0 },
  },
  run = function(doc, p)
    -- the edit goes here
  end,
}
```

`menu = "Mods/Color"` puts it under **Mods > Color**. Because it has `params`, David asks for them in a dialog first (color params start at the primary color), then calls `run(doc, p)` with the values by id.

## 3. Editing pixels

`doc:layer()` is the active layer. Colors are plain integers: `david.channels(c)` splits one into r, g, b, a (0–255), and `david.rgba(r, g, b, a)` packs them.

```lua
run = function(doc, p)
  local layer = doc:layer()
  local fr, fg, fb = david.channels(p.from)
  local tr, tg, tb = david.channels(p.to)
  for y = 0, doc.height - 1 do
    for x = 0, doc.width - 1 do
      local r, g, b, a = david.channels(layer:get(x, y))
      local close = math.abs(r - fr) <= p.tolerance and math.abs(g - fg) <= p.tolerance and math.abs(b - fb) <= p.tolerance
      if a > 0 and close and doc:selected(x, y) > 0 then
        layer:set(x, y, david.rgba(tr, tg, tb, a))
      end
    end
  end
end,
```

`layer:set` writes regardless of the selection, so the command checks `doc:selected(x, y)` itself (it is 255 everywhere when nothing is selected).

## 4. Undo

You don't do anything for undo: every pixel and layer change inside `run` becomes one History step named after the command. If `run` raises an error, everything it changed is put back and the message shows in a dialog.

## Notes

- A call that runs longer than 10 seconds is stopped, so a mistake can't freeze David.
- Big images: reading and writing pixels one by one from Lua is slow past a few megapixels. An effect (`david.effect`) runs only over the selection's bounds; prefer it for per-pixel work.
- The same command logic can run from the CLI as a script: see the [cookbook](cookbook.html).
