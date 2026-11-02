#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace david {

enum class EPixelFormat : uint8_t { Rgba8 };

class Tile {
public:
  static constexpr int SIZE = 256;
  static constexpr int PIXELS = SIZE * SIZE;

  EPixelFormat format = EPixelFormat::Rgba8;

  Tile();
  explicit Tile(uint32_t fill);

  std::unique_ptr<Tile> clone() const;
  bool equals(const Tile& other) const;
  bool isEmpty() const;

  uint32_t* data() { return _px.get(); }
  const uint32_t* data() const { return _px.get(); }
  uint32_t get(int x, int y) const { return _px[y * SIZE + x]; }
  void set(int x, int y, uint32_t v) { _px[y * SIZE + x] = v; touch(); }

  // Every write gets a new global revision, so caches (GPU textures, thumbnails) can tell a tile changed
  // even when an undo swaps in a different Tile object.
  uint64_t revision() const { return _revision; }
  void touch() { _revision = ++s_revision; }
  static size_t bytes() { return PIXELS * 4; }

private:
  std::unique_ptr<uint32_t[]> _px;
  uint64_t _revision = 0;
  static inline std::atomic<uint64_t> s_revision = 0;
};

}
