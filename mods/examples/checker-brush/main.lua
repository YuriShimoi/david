-- Checker Brush: a tool. David moves the round brush along the stroke; the ink decides each pixel.
david.tool{
  id = "checker-brush",
  name = "Checker Brush",
  ink = function(x, y, before, color)
    if (x + y) % 2 == 0 then
      return color
    end
    -- nil leaves the pixel as it was
  end,
}
