// @name Scanlines
// @id scanlines
// @category Effects/Stylize
// @param spacing int 2..16 = 2 "Spacing"
// @param thickness int 1..8 = 1 "Thickness"
// @param darkness float 0..1 = 0.5 "Darkness"
// @param tint color = #000000FF "Line color"

// pos is the pixel's position in the image; pixel(pos) reads the image there
vec4 effect(vec2 pos) {
  vec4 c = pixel(pos);
  // The first `thickness` rows of every `spacing` rows are lines
  bool line = mod(pos.y, float(spacing)) < float(thickness);
  if(line) c.rgb = mix(c.rgb, tint.rgb, darkness * tint.a);
  return c;
}
