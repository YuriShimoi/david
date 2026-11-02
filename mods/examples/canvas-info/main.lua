-- Canvas Info: a tool window that docks like Layers, and a dialog it opens.
-- Windows are redrawn every frame; their values live in the mod's own variables.

local count = 0

david.window{
  id = "canvas-info",
  title = "Canvas Info",
  draw = function(ui, doc)
    ui:header("Image")
    ui:text(doc.width .. " x " .. doc.height .. " px")
    ui:text(doc:layerCount() .. " layers, " .. doc:frameCount() .. " frames at " .. string.format("%g", doc.fps) .. " fps")
    local x, y, w, h = doc:selectionBounds()
    if x then ui:text("Selection: " .. w .. " x " .. h .. " at " .. x .. ", " .. y)
    else ui:muted("No selection") end

    ui:header("Active layer")
    local layer = doc:layer()
    ui:text(layer.name)
    if ui:button("Count opaque pixels", -1) then
      count = 0
      for py = 0, doc.height - 1 do
        for px = 0, doc.width - 1 do
          local _, _, _, a = david.channels(layer:get(px, py))
          if a > 0 then count = count + 1 end
        end
      end
    end
    ui:muted(count .. " opaque pixels at the last count")

    ui:spacing()
    if ui:button("Rename Layers...", -1) then david.openWindow("rename") end
  end,
}

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
    -- Everything changed in this call is one undo step, named after the window
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
