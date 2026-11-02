#pragma once
#include <cmath>
#include <cstdint>

namespace david {

// Björn Ottosson's OKLab, from and to straight sRGB8
struct Lab { float L, a, b; };

float srgbToLinear(uint8_t v);  // table lookup
float linearToSrgb(float v);    // 0-1 in, 0-1 out
Lab linearToOklab(float r, float g, float b);
void oklabToLinear(const Lab& c, float& r, float& g, float& b);

inline Lab toOklab(uint32_t c) {
  return linearToOklab(srgbToLinear(uint8_t(c)), srgbToLinear(uint8_t(c >> 8)), srgbToLinear(uint8_t(c >> 16)));
}

// OKLab distance (black to white is 1) weighted by the smaller alpha, combined with the alpha difference
float colorDistance(uint32_t a, uint32_t b);
// Paint.NET's RGBA distance, 0-1
float rgbDistance(uint32_t a, uint32_t b);

}
