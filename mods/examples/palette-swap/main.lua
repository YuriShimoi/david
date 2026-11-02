-- Palette Swap: a command with a dialog. Everything it changes is one undo step.
david.command{
  id = "palette-swap",
  name = "Palette Swap",
  menu = "Mods/Color",
  params = {
    { id = "from", label = "Replace", type = "color", default = 0 },
    { id = "to", label = "With", type = "color", default = 0 },
    { id = "tolerance", label = "Tolerance", type = "int", min = 0, max = 255, default = 0 },
  },
  run = function(doc, p)
    local layer = doc:layer()
    local fr, fg, fb = david.channels(p.from)
    local tr, tg, tb = david.channels(p.to)
    local changed = 0
    for y = 0, doc.height - 1 do
      for x = 0, doc.width - 1 do
        local c = layer:get(x, y)
        local r, g, b, a = david.channels(c)
        local close = math.abs(r - fr) <= p.tolerance and math.abs(g - fg) <= p.tolerance and math.abs(b - fb) <= p.tolerance
        if a > 0 and close and doc:selected(x, y) > 0 then
          layer:set(x, y, david.rgba(tr, tg, tb, a))
          changed = changed + 1
        end
      end
    end
    david.log("palette swap: " .. changed .. " pixels")
  end,
}
