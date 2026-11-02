#include "core/Oklab.h"
#include <algorithm>
#include <array>

namespace david {

static const std::array<float, 256> s_toLinear = [] {
  std::array<float, 256> t{};
  for(int i = 0; i < 256; i++) {
    float v = i / 255.f;
    t[i] = v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
  }
  return t;
}();

float srgbToLinear(uint8_t v) { return s_toLinear[v]; }

float linearToSrgb(float v) {
  v = std::clamp(v, 0.f, 1.f);
  return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
}

Lab linearToOklab(float r, float g, float b) {
  float l = std::cbrt(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
  float m = std::cbrt(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
  float s = std::cbrt(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);
  return {
    0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
    1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
    0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
  };
}

void oklabToLinear(const Lab& c, float& r, float& g, float& b) {
  float l = c.L + 0.3963377774f * c.a + 0.2158037573f * c.b;
  float m = c.L - 0.1055613458f * c.a - 0.0638541728f * c.b;
  float s = c.L - 0.0894841775f * c.a - 1.2914855480f * c.b;
  l = l * l * l; m = m * m * m; s = s * s * s;
  r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
  g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
  b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

float colorDistance(uint32_t a, uint32_t b) {
  if(a == b) return 0;
  float aa = (a >> 24) / 255.f, ab = (b >> 24) / 255.f;
  Lab x = toOklab(a), y = toOklab(b);
  float dl = x.L - y.L, da = x.a - y.a, db = x.b - y.b;
  float de = std::sqrt(dl * dl + da * da + db * db) * std::min(aa, ab), dA = aa - ab;
  return std::min(1.f, std::sqrt(de * de + dA * dA));
}

float rgbDistance(uint32_t a, uint32_t b) {
  float d = 0;
  for(int k = 0; k < 32; k += 8) {
    float v = float((a >> k) & 0xFF) - float((b >> k) & 0xFF);
    d += v * v;
  }
  return std::sqrt(d) / 510;
}

}
