#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace david {

enum class ECombine : uint8_t { Replace, Union, Exclude, Intersect, Xor };

struct IRect {
  int x0 = 0, y0 = 0, x1 = -1, y1 = -1; // inclusive
  bool empty() const { return x1 < x0 || y1 < y0; }
  int width() const { return x1 - x0 + 1; }
  int height() const { return y1 - y0 + 1; }
};

// 8-bit coverage in 256×256 tiles. A missing tile is all 0, a "full" tile is all 255 with no storage.
class Mask {
public:
  static constexpr int SIZE = 256;

  Mask(int width, int height);

  int width() const { return _width; }
  int height() const { return _height; }
  bool empty() const { return _empty; }
  // Changes whenever the mask is created or changed as a whole, so caches (outline) can tell
  uint64_t serial() const { return _serial; }

  uint8_t get(int x, int y) const;
  void set(int x, int y, uint8_t v);
  void fillSpan(int y, int x0, int x1, uint8_t v); // inclusive, clipped
  void readRow(int y, int x0, int x1, uint8_t* out) const; // inclusive, 0 outside
  void clear();
  void fillAll();
  void invert();
  void combine(const Mask& shape, ECombine mode);
  IRect bounds() const;
  bool equals(const Mask& other) const;
  std::unique_ptr<Mask> clone() const;

  // Row access for fast loops; returns null for an all-zero tile row and sets full for an all-255 tile
  const uint8_t* row(int tx, int ty, int ry, bool& full) const;

private:
  struct Tile {
    bool full = false;
    std::unique_ptr<uint8_t[]> px;
  };
  int _width, _height, _tilesX, _tilesY;
  bool _empty = true;
  uint64_t _serial;
  std::vector<Tile> _tiles;
  static inline uint64_t s_serial = 0;

  Tile& tileAt(int tx, int ty) { return _tiles[ty * _tilesX + tx]; }
  const Tile& tileAt(int tx, int ty) const { return _tiles[ty * _tilesX + tx]; }
  void expand(Tile& t);
  void recomputeEmpty();
};

}
