# Tutorial: Canvas Info (Lua windows)

A tool window that docks next to Layers and shows the image's size, layers and frames, plus a dialog it opens to rename the layers. The finished mod is `mods/examples/canvas-info`.

## Two kinds of window

- **Tool window** (`kind = "tool"`, the default): docks like Colors, Layers and History. It first opens as a tab next to Layers; drag its tab to dock it anywhere else. With Settings > Appearance > floating windows on, it floats like the other windows.
- **Dialog** (`kind = "dialog"`): floats over the canvas like Settings and never docks. Esc closes it.

Both are listed in the Mods menu (checked while open), and David remembers which ones were open for the next session.

## The tool window

`mod.json` is the same as in the [command tutorial](tutorial-lua-command.html), with `"entry": "main.lua"`. Then:

```lua
local count = 0

david.window{
  id = "canvas-info",
  title = "Canvas Info",
  draw = function(ui, doc)
    ui:header("Image")
    ui:text(doc.width .. " x " .. doc.height .. " px")
    ui:text(doc:layerCount() .. " layers, " .. doc:frameCount() .. " frames")

    ui:header("Active layer")
    local layer = doc:layer()
    ui:text(layer.name)
    if ui:button("Count opaque pixels", -1) then
      count = 0
      for y = 0, doc.height - 1 do
        for x = 0, doc.width - 1 do
          local _, _, _, a = david.channels(layer:get(x, y))
          if a > 0 then count = count + 1 end
        end
      end
    end
    ui:muted(count .. " opaque pixels at the last count")

    if ui:button("Rename Layers...", -1) then david.openWindow("rename") end
  end,
}
```

`draw(ui, doc)` runs **every frame** while the window is open (immediate mode): it rebuilds the contents each time from the document and the mod's own variables. Keep it light; do heavy work only when a button is pressed, as the pixel count does here.

Widgets are laid out top to bottom. A button returns `true` on the frame it was clicked; `-1` as its width fills the row.

## The dialog

```lua
local prefix, numbered, start = "Layer", true, 1

david.window{
  id = "rename",
  title = "Rename Layers",
  kind = "dialog",
  width = 360,
  height = 200,
  draw = function(ui, doc)
    prefix = ui:input("Prefix", prefix)
    numbered = ui:checkbox("Number them", numbered)
    if numbered then start = ui:slider("Start at", start, 0, 100) end
    ui:separator()
    if ui:button("Apply", 100) then
      for i = 1, doc:layerCount() do
        doc:layer(i).name = numbered and (prefix .. " " .. (start + i - 1)) or prefix
      end
      ui:close()
    end
    ui:sameLine()
    if ui:button("Cancel", 100) then ui:close() end
  end,
}
```

Value widgets take the current value and return the new one (plus `true` as a second result when it changed), so the pattern is always `value = ui:widget("Label", value)`. Their labels sit on the left, like the effect dialogs; the slider is the same bar with a number field.

`width` and `height` are the size the window first opens at; after that David keeps the size the user gave it.

## Undo

Everything a `draw` call changes in the document is **one undo step**, named after the window (here "Rename Layers"). So change the document on a button press, not while a slider moves, or every frame of the drag becomes its own step.

## When it goes wrong

If `draw` raises an error, the window shows the error instead of its contents and the error goes to the mod's log (Settings > Mods). Fix the file and save it: the mod reloads and the window draws again.

## Widgets

| Call | Returns |
|---|---|
| `ui:text(s)`, `ui:muted(s)`, `ui:header(s)` | |
| `ui:separator()`, `ui:spacing()`, `ui:sameLine()` | |
| `ui:button(label [, width])` | pressed |
| `ui:checkbox(label, value)` | value, changed |
| `ui:slider(label, value, min, max [, step])` | value, changed |
| `ui:choice(label, index, { names })` | index (from 1), changed |
| `ui:color(label, color)` | color, changed |
| `ui:input(label, text)` | text, changed |
| `ui:width()` | the width left on the row |
| `ui:close()` | |

Two widgets with the same label are fine; text after `##` in a label is not shown. The [API reference](reference.html#lua-ui) has the details.
