#include "core/Mask.h"
#include <algorithm>
#include <cstring>

namespace david {

Mask::Mask(int width, int height)
  : _width(width), _height(height), _tilesX((width + SIZE - 1) / SIZE), _tilesY((height + SIZE - 1) / SIZE), _serial(++s_serial) {
  _tiles.resize(size_t(_tilesX) * _tilesY);
}

uint8_t Mask::get(int x, int y) const {
  if(x < 0 || y < 0 || x >= _width || y >= _height) return 0;
  const Tile& t = tileAt(x / SIZE, y / SIZE);
  if(t.full) return 255;
  return t.px ? t.px[(y % SIZE) * SIZE + x % SIZE] : 0;
}

void Mask::expand(Tile& t) {
  if(t.px) return;
  t.px = std::make_unique<uint8_t[]>(SIZE * SIZE);
  if(t.full) std::memset(t.px.get(), 255, SIZE * SIZE);
  t.full = false;
}

void Mask::set(int x, int y, uint8_t v) {
  if(x < 0 || y < 0 || x >= _width || y >= _height) return;
  Tile& t = tileAt(x / SIZE, y / SIZE);
  if(!t.px) {
    if((t.full && v == 255) || (!t.full && v == 0)) return;
    expand(t);
  }
  t.px[(y % SIZE) * SIZE + x % SIZE] = v;
  if(v) _empty = false;
}

void Mask::fillSpan(int y, int x0, int x1, uint8_t v) {
  if(y < 0 || y >= _height) return;
  x0 = std::max(x0, 0); x1 = std::min(x1, _width - 1);
  int ty = y / SIZE, ry = y % SIZE;
  for(int tx = x0 / SIZE; tx <= x1 / SIZE && x0 <= x1; tx++) {
    int a = std::max(x0, tx * SIZE) - tx * SIZE, b = std::min(x1, tx * SIZE + SIZE - 1) - tx * SIZE;
    Tile& t = tileAt(tx, ty);
    if(!t.px && ((t.full && v == 255) || (!t.full && v == 0))) continue;
    expand(t);
    std::memset(t.px.get() + ry * SIZE + a, v, b - a + 1);
  }
  if(v && x0 <= x1) _empty = false;
}

void Mask::readRow(int y, int x0, int x1, uint8_t* out) const {
  for(int x = x0; x <= x1; ) {
    if(y < 0 || y >= _height || x < 0 || x >= _width) { *out++ = 0; x++; continue; }
    int tx = x / SIZE, end = std::min(x1, std::min(tx * SIZE + SIZE - 1, _width - 1));
    const Tile& t = tileAt(tx, y / SIZE);
    int n = end - x + 1;
    if(t.full) std::memset(out, 255, n);
    else if(t.px) std::memcpy(out, t.px.get() + (y % SIZE) * SIZE + x % SIZE, n);
    else std::memset(out, 0, n);
    out += n;
    x = end + 1;
  }
}

void Mask::clear() {
  _serial = ++s_serial;
  for(auto& t : _tiles) { t.px.reset(); t.full = false; }
  _empty = true;
}

void Mask::fillAll() {
  _serial = ++s_serial;
  for(int ty = 0; ty < _tilesY; ty++) {
    for(int tx = 0; tx < _tilesX; tx++) {
      Tile& t = tileAt(tx, ty);
      int w = std::min(SIZE, _width - tx * SIZE), h = std::min(SIZE, _height - ty * SIZE);
      t.px.reset();
      t.full = w == SIZE && h == SIZE;
      if(!t.full) {
        // Edge tiles keep 0 past the image bounds
        t.px = std::make_unique<uint8_t[]>(SIZE * SIZE);
        for(int y = 0; y < h; y++) std::memset(t.px.get() + y * SIZE, 255, w);
      }
    }
  }
  _empty = _width == 0 || _height == 0;
}

void Mask::invert() {
  _serial = ++s_serial;
  for(int ty = 0; ty < _tilesY; ty++) {
    for(int tx = 0; tx < _tilesX; tx++) {
      Tile& t = tileAt(tx, ty);
      int w = std::min(SIZE, _width - tx * SIZE), h = std::min(SIZE, _height - ty * SIZE);
      if(w == SIZE && h == SIZE && !t.px) { t.full = !t.full; continue; }
      expand(t);
      for(int y = 0; y < h; y++)
        for(int x = 0; x < w; x++) t.px[y * SIZE + x] = 255 - t.px[y * SIZE + x];
    }
  }
  recomputeEmpty();
}

void Mask::combine(const Mask& shape, ECombine mode) {
  if(mode == ECombine::Replace) { *this = std::move(*shape.clone()); return; }
  _serial = ++s_serial;
  for(int ty = 0; ty < _tilesY; ty++) {
    for(int tx = 0; tx < _tilesX; tx++) {
      Tile& a = tileAt(tx, ty);
      const Tile& b = shape.tileAt(tx, ty);
      bool bEmpty = !b.full && !b.px;
      if(bEmpty && mode != ECombine::Intersect) continue;
      if(bEmpty) { a.px.reset(); a.full = false; continue; }
      if(b.full && mode == ECombine::Union) { a.px.reset(); a.full = true; continue; }
      if(b.full && mode == ECombine::Exclude) { a.px.reset(); a.full = false; continue; }
      if(b.full && mode == ECombine::Intersect) continue;
      if(!a.full && !a.px && mode != ECombine::Union && mode != ECombine::Xor) continue;
      expand(a);
      for(int i = 0; i < SIZE * SIZE; i++) {
        int va = a.px[i], vb = b.full ? 255 : b.px[i];
        switch(mode) {
          case ECombine::Union: va = std::max(va, vb); break;
          case ECombine::Exclude: va = va * (255 - vb) / 255; break;
          case ECombine::Intersect: va = std::min(va, vb); break;
          case ECombine::Xor: va = std::abs(va - vb); break;
          default: break;
        }
        a.px[i] = uint8_t(va);
      }
    }
  }
  recomputeEmpty();
}

IRect Mask::bounds() const {
  IRect r{ _width, _height, -1, -1 };
  for(int ty = 0; ty < _tilesY; ty++) {
    for(int tx = 0; tx < _tilesX; tx++) {
      const Tile& t = tileAt(tx, ty);
      if(!t.full && !t.px) continue;
      int w = std::min(SIZE, _width - tx * SIZE), h = std::min(SIZE, _height - ty * SIZE);
      if(t.full) {
        r.x0 = std::min(r.x0, tx * SIZE); r.y0 = std::min(r.y0, ty * SIZE);
        r.x1 = std::max(r.x1, tx * SIZE + w - 1); r.y1 = std::max(r.y1, ty * SIZE + h - 1);
        continue;
      }
      for(int y = 0; y < h; y++) {
        for(int x = 0; x < w; x++) {
          if(!t.px[y * SIZE + x]) continue;
          int gx = tx * SIZE + x, gy = ty * SIZE + y;
          r.x0 = std::min(r.x0, gx); r.y0 = std::min(r.y0, gy);
          r.x1 = std::max(r.x1, gx); r.y1 = std::max(r.y1, gy);
        }
      }
    }
  }
  return r;
}

bool Mask::equals(const Mask& other) const {
  if(_width != other._width || _height != other._height) return false;
  for(int ty = 0; ty < _tilesY; ty++)
    for(int tx = 0; tx < _tilesX; tx++)
      for(int y = 0; y < SIZE; y++) {
        bool fa, fb;
        const uint8_t* a = row(tx, ty, y, fa);
        const uint8_t* b = other.row(tx, ty, y, fb);
        if(fa != fb || bool(a) != bool(b)) {
          for(int x = 0; x < SIZE; x++)
            if((fa ? 255 : a ? a[x] : 0) != (fb ? 255 : b ? b[x] : 0)) return false;
        } else if(a && b && std::memcmp(a, b, SIZE)) return false;
      }
  return true;
}

std::unique_ptr<Mask> Mask::clone() const {
  auto m = std::make_unique<Mask>(_width, _height);
  for(size_t i = 0; i < _tiles.size(); i++) {
    m->_tiles[i].full = _tiles[i].full;
    if(_tiles[i].px) {
      m->_tiles[i].px = std::make_unique<uint8_t[]>(SIZE * SIZE);
      std::memcpy(m->_tiles[i].px.get(), _tiles[i].px.get(), SIZE * SIZE);
    }
  }
  m->_empty = _empty;
  return m;
}

const uint8_t* Mask::row(int tx, int ty, int ry, bool& full) const {
  const Tile& t = tileAt(tx, ty);
  full = t.full;
  return t.px ? t.px.get() + ry * SIZE : nullptr;
}

void Mask::recomputeEmpty() {
  _empty = true;
  for(auto& t : _tiles) {
    if(t.full) { _empty = false; return; }
    if(t.px && std::any_of(t.px.get(), t.px.get() + SIZE * SIZE, [](uint8_t v) { return v != 0; })) { _empty = false; return; }
  }
}

}
