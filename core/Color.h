#pragma once
#include <cstdint>

namespace david {

// Straight (non-premultiplied) RGBA8. Packed as r | g<<8 | b<<16 | a<<24, the same byte order GL_RGBA reads.
struct Color {
  uint8_t r = 0, g = 0, b = 0, a = 255;

  constexpr uint32_t pack() const { return r | (g << 8) | (b << 16) | (uint32_t(a) << 24); }
  static constexpr Color unpack(uint32_t v) { return { uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24) }; }
  bool operator==(const Color&) const = default;
};

constexpr uint32_t BLACK = Color{ 0, 0, 0, 255 }.pack();
constexpr uint32_t WHITE = Color{ 255, 255, 255, 255 }.pack();
constexpr uint32_t CLEAR = 0;

}
