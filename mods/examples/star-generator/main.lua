-- Star Generator: vector objects from a command. The stars stay editable (Mouse and Node tools), and the
-- whole change is one undo step.
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
