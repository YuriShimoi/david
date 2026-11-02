#pragma once
#include <cstdint>

namespace david {

// Paint.NET's layer blend modes, in its menu order
enum class EBlend : uint8_t {
  Normal, Multiply, Additive, ColorBurn, ColorDodge, Reflect, Glow, Overlay,
  Difference, Negation, Lighten, Darken, Screen, Xor, COUNT
};

const char* blendName(EBlend mode);
// The id used in files and the CLI ("multiply"), and back; Normal when unknown
const char* blendId(EBlend mode);
EBlend blendFromId(const char* id);

// One channel of the blend function f(backdrop, source), 0-255
int blendChannel(EBlend mode, int b, int s);
// src over dst with the mode: a = Sa + Ba(1 - Sa), C = (Sa(1 - Ba)S + SaBa f(B, S) + (1 - Sa)Ba B) / a.
// Normal is plain "over", the same as blendOver.
uint32_t blendPixel(uint32_t dst, uint32_t src, uint8_t opacity, EBlend mode);

}
