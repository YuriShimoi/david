#include "core/Tile.h"
#include <algorithm>
#include <cstring>

namespace david {

Tile::Tile() : _px(std::make_unique<uint32_t[]>(PIXELS)) {
  touch();
}

Tile::Tile(uint32_t fill) : _px(std::make_unique_for_overwrite<uint32_t[]>(PIXELS)) {
  std::fill_n(_px.get(), PIXELS, fill);
  touch();
}

std::unique_ptr<Tile> Tile::clone() const {
  auto t = std::make_unique<Tile>();
  std::memcpy(t->_px.get(), _px.get(), bytes());
  t->format = format;
  t->touch();
  return t;
}

bool Tile::equals(const Tile& other) const {
  return format == other.format && std::memcmp(_px.get(), other._px.get(), bytes()) == 0;
}

bool Tile::isEmpty() const {
  return std::all_of(_px.get(), _px.get() + PIXELS, [](uint32_t v) { return (v >> 24) == 0; });
}

}
