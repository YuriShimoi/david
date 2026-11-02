#pragma once
#include "core/Mask.h"
#include "core/TileEdit.h"
#include <functional>

namespace david {

class Document;
class Layer;

struct Palette;

// Paint blends over the pixel; Replace swaps it for the color, alpha included (mixed only at soft edges)
enum class EInk : uint8_t { Paint, Erase, Shade, Custom, Replace };

// A custom ink (Lua tools): the new pixel from its position, its value before the stroke and the color
using InkFn = std::function<uint32_t(int x, int y, uint32_t before, uint32_t color)>;

// Where a stroke's pixels land besides where they are drawn: mirrored copies and wrapping (tiled mode)
struct StrokeSpace {
  bool wrapX = false, wrapY = false;
  bool mirrorX = false, mirrorY = false;  // mirrorX: left-right across the vertical axis at axisX
  double axisX = 0, axisY = 0;
  bool wraps() const { return wrapX || wrapY; }
};

uint32_t applyInk(EInk ink, uint32_t before, uint32_t color, int coverage);

// One edit with an ink. Each pixel keeps the highest coverage it got, and is always recomputed from its
// pre-stroke value, so overlapping dabs inside one stroke never build up opacity.
class Stroke {
public:
  Stroke(Document& doc, Layer& layer, EInk ink, uint32_t color);

  // Round point shape centered at (cx, cy) in image pixels; opacity scales the coverage
  void dab(double cx, double cy, double radius, float opacity, bool antialias);
  // Dabs from the last point to (x, y), spaced along the path; radius and opacity are interpolated
  void lineTo(double x, double y, double radius, float opacity, bool antialias);
  void moveTo(double x, double y, double radius, float opacity) { _x = x; _y = y; _r = radius; _o = opacity; _carry = 0; }
  // Coverage 0-255 for one pixel, clipped by the selection; mirrored and wrapped by the stroke space
  void cover(int x, int y, int coverage);
  // Puts a pixel (and its mirrors) back to its pre-stroke value (pixel-perfect cleanup)
  void uncover(int x, int y);
  void setSpace(const StrokeSpace& s) { _space = s; }
  // Shade ink: the palette ramp and the step (+1 next swatch, -1 previous)
  void setShade(const Palette* palette, int step) { _palette = palette; _step = step; }
  // Custom ink: the result is mixed over the old pixel by the coverage
  void setInk(InkFn fn) { _custom = std::move(fn); }

  std::unique_ptr<TileEdit> finish(std::string name, const char* icon) { return _rec.finish(std::move(name), icon); }
  TileRecorder& recorder() { return _rec; }

private:
  Document& _doc;
  Layer& _layer;
  TileRecorder _rec;
  Mask _coverage;
  EInk _ink;
  uint32_t _color;
  StrokeSpace _space;
  const Palette* _palette = nullptr;
  InkFn _custom;
  int _step = 1;
  double _x = 0, _y = 0, _r = 1, _carry = 0;

  // Calls fn for the pixel and each of its mirrors, wrapped into the image; skips pixels outside it
  template<class F> void each(int x, int y, F fn);
  void coverOne(int x, int y, int coverage);
  float _o = 1;
};

}
