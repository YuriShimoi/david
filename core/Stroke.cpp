#include "core/Stroke.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Paint.h"
#include "core/Palette.h"
#include <algorithm>
#include <cmath>

namespace david {

uint32_t applyInk(EInk ink, uint32_t before, uint32_t color, int coverage) {
  if(ink == EInk::Erase) {
    uint32_t a = (before >> 24) * (255 - coverage) / 255;
    return a ? (before & 0x00FFFFFF) | (a << 24) : 0;
  }
  if(ink == EInk::Replace) return mixPixel(before, color, coverage);
  return blendOver(before, color, uint8_t(coverage));
}

Stroke::Stroke(Document& doc, Layer& layer, EInk ink, uint32_t color)
  : _doc(doc), _layer(layer), _rec(layer), _coverage(layer.width(), layer.height()), _ink(ink), _color(color) {}

template<class F> void Stroke::each(int x, int y, F fn) {
  int w = _layer.width(), h = _layer.height();
  auto wrap = [&](int px, int py) {
    if(_space.wrapX) px = ((px % w) + w) % w;
    if(_space.wrapY) py = ((py % h) + h) % h;
    if(_layer.contains(px, py)) fn(px, py);
  };
  // Mirror pixel centers across the axis: x + 0.5 -> 2a - x - 0.5
  int mx = int(std::floor(2 * _space.axisX - x - 0.5)), my = int(std::floor(2 * _space.axisY - y - 0.5));
  wrap(x, y);
  if(_space.mirrorX && mx != x) wrap(mx, y);
  if(_space.mirrorY && my != y) wrap(x, my);
  if(_space.mirrorX && _space.mirrorY && mx != x && my != y) wrap(mx, my);
}

void Stroke::cover(int x, int y, int coverage) {
  if(coverage <= 0) return;
  each(x, y, [&](int px, int py) { coverOne(px, py, coverage); });
}

void Stroke::coverOne(int x, int y, int coverage) {
  coverage = coverage * _doc.clip(x, y) / 255;
  if(coverage <= _coverage.get(x, y)) return;
  _coverage.set(x, y, uint8_t(coverage));
  _rec.touchPixel(x, y);
  uint32_t before = _rec.beforePixel(x, y);
  if(_ink == EInk::Shade) {
    // Only pixels whose color is a swatch move, and only once per stroke
    int i = _palette && (before >> 24) && coverage >= 128 ? _palette->find(before) : -1;
    if(i < 0) return;
    i = std::clamp(i + _step, 0, _palette->size() - 1);
    _layer.set(x, y, (_palette->colors[i] & 0xFFFFFF) | (before & 0xFF000000u));
    return;
  }
  if(_ink == EInk::Custom && _custom) {
    _layer.set(x, y, mixPixel(before, _custom(x, y, before, _color), coverage));
    return;
  }
  _layer.set(x, y, applyInk(_ink, before, _color, coverage));
}

void Stroke::uncover(int x, int y) {
  each(x, y, [&](int px, int py) {
    if(!_coverage.get(px, py)) return;
    _coverage.set(px, py, 0);
    _layer.set(px, py, _rec.beforePixel(px, py));
  });
}

void Stroke::dab(double cx, double cy, double radius, float opacity, bool antialias) {
  radius = std::max(radius, 0.5);
  int x0 = int(std::floor(cx - radius - 1)), x1 = int(std::ceil(cx + radius + 1));
  int y0 = int(std::floor(cy - radius - 1)), y1 = int(std::ceil(cy + radius + 1));
  // Wrapped strokes keep the part past the edge; it lands on the other side
  if(!_space.wrapX) { x0 = std::max(x0, 0); x1 = std::min(x1, _layer.width() - 1); }
  if(!_space.wrapY) { y0 = std::max(y0, 0); y1 = std::min(y1, _layer.height() - 1); }
  for(int y = y0; y <= y1; y++) {
    double dy = y + 0.5 - cy;
    for(int x = x0; x <= x1; x++) {
      double dx = x + 0.5 - cx;
      double d = std::sqrt(dx * dx + dy * dy);
      double c = antialias ? std::clamp(radius - d + 0.5, 0.0, 1.0) : (d <= radius ? 1.0 : 0.0);
      if(c > 0) cover(x, y, int(c * opacity * 255 + 0.5));
    }
  }
}

void Stroke::lineTo(double x, double y, double radius, float opacity, bool antialias) {
  double dx = x - _x, dy = y - _y;
  double len = std::sqrt(dx * dx + dy * dy);
  // Dense spacing keeps the max-coverage union smooth along the path
  double spacing = std::max(0.5, std::min(_r, radius) * 0.15);
  double t = spacing - _carry;
  while(t <= len) {
    double k = len > 0 ? t / len : 1;
    dab(_x + dx * k, _y + dy * k, _r + (radius - _r) * k, _o + float((opacity - _o) * k), antialias);
    t += spacing;
  }
  _carry = len - (t - spacing);
  _x = x; _y = y; _r = radius; _o = opacity;
}

}
