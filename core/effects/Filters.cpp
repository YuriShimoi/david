#include "core/Effect.h"
#include "core/Composite.h"
#include <algorithm>
#include <cmath>

namespace david {

namespace {
// Premultiplied float copy, for filters that average pixels (no dark fringes at alpha edges)
struct Premul {
  int w, h;
  std::vector<float> px;  // r, g, b, a premultiplied, 0-255
  explicit Premul(const Image& img) : w(img.w), h(img.h), px(size_t(img.w) * img.h * 4) {
    for(size_t i = 0; i < img.px.size(); i++) {
      uint32_t c = img.px[i];
      float a = float(c >> 24);
      px[i * 4 + 0] = (c & 0xFF) * a / 255; px[i * 4 + 1] = ((c >> 8) & 0xFF) * a / 255;
      px[i * 4 + 2] = ((c >> 16) & 0xFF) * a / 255; px[i * 4 + 3] = a;
    }
  }
  uint32_t straight(size_t i) const {
    float a = px[i * 4 + 3];
    if(a < 0.5f) return 0;
    uint32_t out = uint32_t(std::clamp(std::lround(a), 0L, 255L)) << 24;
    for(int k = 0; k < 3; k++) out |= uint32_t(std::clamp(std::lround(px[i * 4 + k] * 255 / a), 0L, 255L)) << (k * 8);
    return out;
  }
};

// Separable Gaussian on premultiplied pixels, edges clamped
void gaussian(Premul& p, double sigma) {
  if(sigma < 0.05) return;
  int r = int(std::ceil(sigma * 3));
  std::vector<float> k(r * 2 + 1);
  float sum = 0;
  for(int i = -r; i <= r; i++) sum += k[i + r] = float(std::exp(-i * i / (2 * sigma * sigma)));
  for(auto& v : k) v /= sum;
  std::vector<float> tmp(p.px.size());
  parallelRows(p.h, [&](int y0, int y1) {
    for(int y = y0; y < y1; y++)
      for(int x = 0; x < p.w; x++) {
        float acc[4] = {};
        for(int i = -r; i <= r; i++) {
          const float* s = &p.px[(size_t(y) * p.w + std::clamp(x + i, 0, p.w - 1)) * 4];
          for(int c = 0; c < 4; c++) acc[c] += s[c] * k[i + r];
        }
        std::copy_n(acc, 4, &tmp[(size_t(y) * p.w + x) * 4]);
      }
  });
  parallelRows(p.h, [&](int y0, int y1) {
    for(int y = y0; y < y1; y++)
      for(int x = 0; x < p.w; x++) {
        float acc[4] = {};
        for(int i = -r; i <= r; i++) {
          const float* s = &tmp[(size_t(std::clamp(y + i, 0, p.h - 1)) * p.w + x) * 4];
          for(int c = 0; c < 4; c++) acc[c] += s[c] * k[i + r];
        }
        std::copy_n(acc, 4, &p.px[(size_t(y) * p.w + x) * 4]);
      }
  });
}

uint32_t hash(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
  return x;
}
}

void registerFilters() {
  Effect e;
  e.id = "gaussian-blur"; e.name = "Gaussian Blur"; e.category = "Effects/Blurs";
  e.params = { { "radius", "Radius", EParam::Float, 0, 200, 2 } };
  e.margin = [](const ParamValues& v) { return int(std::ceil(v.get("radius", 2))) + 1; };
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    Premul p(s);
    gaussian(p, v.get("radius", 2) / 3);
    for(size_t i = 0; i < d.px.size(); i++) d.px[i] = p.straight(i);
  };
  registerEffect(e);

  e = {};
  e.id = "sharpen"; e.name = "Sharpen"; e.category = "Effects/Photo";
  e.params = { { "amount", "Amount", EParam::Int, 1, 20, 10 } };
  e.margin = [](const ParamValues&) { return 4; };
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    // Unsharp mask: the difference from a small blur, scaled
    Premul p(s);
    gaussian(p, 1);
    float k = float(v.get("amount", 10) / 10);
    for(size_t i = 0; i < d.px.size(); i++) {
      uint32_t c = s.px[i];
      uint32_t bl = p.straight(i);
      uint32_t out = c & 0xFF000000u;
      for(int sh = 0; sh < 24; sh += 8) {
        float x = float((c >> sh) & 0xFF), b = float((bl >> sh) & 0xFF);
        out |= uint32_t(std::clamp(std::lround(x + (x - b) * k), 0L, 255L)) << sh;
      }
      d.px[i] = out;
    }
  };
  registerEffect(e);

  e = {};
  e.id = "add-noise"; e.name = "Add Noise"; e.category = "Effects/Noise";
  e.params = {
    { "intensity", "Intensity", EParam::Int, 0, 100, 64 },
    { "saturation", "Color saturation", EParam::Int, 0, 400, 100 },
    { "coverage", "Coverage", EParam::Float, 0, 100, 100 },
    { "seed", "Seed", EParam::Int, 0, 9999, 0 },
  };
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv& env) {
    // Noise is a function of the image position, so a preview and the final result match
    double amp = v.get("intensity", 64) * 1.28, sat = v.get("saturation", 100) / 100, cover = v.get("coverage", 100) / 100;
    uint32_t seed = uint32_t(v.geti("seed")) * 0x9E3779B9u;
    parallelRows(s.h, [&](int y0, int y1) {
      for(int y = y0; y < y1; y++)
        for(int x = 0; x < s.w; x++) {
          uint32_t hsh = hash(uint32_t(x + env.originX) * 73856093u ^ uint32_t(y + env.originY) * 19349663u ^ seed);
          uint32_t c = s.at(x, y);
          if((hash(hsh) & 0xFFFF) / 65536.0 >= cover) { d.at(x, y) = c; continue; }
          // Roughly normal: the sum of uniforms
          auto gauss = [&](uint32_t h) { return ((h & 0x3FF) + ((h >> 10) & 0x3FF) + ((h >> 20) & 0x3FF)) / 1534.5 - 1.0; };
          double lum = gauss(hsh) * amp;
          double n[3] = { gauss(hash(hsh + 1)) * amp * sat, gauss(hash(hsh + 2)) * amp * sat, gauss(hash(hsh + 3)) * amp * sat };
          uint32_t out = c & 0xFF000000u;
          for(int k = 0; k < 3; k++) out |= uint32_t(std::clamp(std::lround(((c >> (k * 8)) & 0xFF) + lum + n[k] - (n[0] + n[1] + n[2]) / 3), 0L, 255L)) << (k * 8);
          d.at(x, y) = out;
        }
    });
  };
  registerEffect(e);

  e = {};
  e.id = "pixelate"; e.name = "Pixelate"; e.category = "Effects/Distort";
  e.params = { { "size", "Cell size", EParam::Int, 1, 100, 4 } };
  e.margin = [](const ParamValues& v) { return v.geti("size", 4); };
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv& env) {
    // Cells line up with the image's own grid, not the selection's corner
    int n = std::max(1, v.geti("size", 4));
    Premul p(s);
    auto cell = [&](int i) { return int(std::floor(double(i) / n)); };
    for(int cy = cell(env.originY); cy <= cell(env.originY + s.h - 1); cy++)
      for(int cx = cell(env.originX); cx <= cell(env.originX + s.w - 1); cx++) {
        int x0 = std::max(0, cx * n - env.originX), x1 = std::min(s.w, cx * n + n - env.originX);
        int y0 = std::max(0, cy * n - env.originY), y1 = std::min(s.h, cy * n + n - env.originY);
        double acc[4] = {};
        for(int y = y0; y < y1; y++) for(int x = x0; x < x1; x++) for(int k = 0; k < 4; k++) acc[k] += p.px[(size_t(y) * s.w + x) * 4 + k];
        double cnt = double(x1 - x0) * (y1 - y0);
        uint32_t out = 0;
        if(acc[3] > 0) {
          out = uint32_t(std::lround(acc[3] / cnt)) << 24;
          for(int k = 0; k < 3; k++) out |= uint32_t(std::clamp(std::lround(acc[k] * 255 / acc[3]), 0L, 255L)) << (k * 8);
        }
        for(int y = y0; y < y1; y++) for(int x = x0; x < x1; x++) d.at(x, y) = out;
      }
  };
  registerEffect(e);

  e = {};
  e.id = "drop-shadow"; e.name = "Drop Shadow"; e.category = "Effects/Object";
  e.params = {
    { "x", "Offset X", EParam::Int, -100, 100, 2 },
    { "y", "Offset Y", EParam::Int, -100, 100, 2 },
    { "color", "Color", EParam::Color, 0, 0, double(0xFF000000u) },
    { "opacity", "Opacity", EParam::Int, 0, 100, 50 },
  };
  e.margin = [](const ParamValues& v) { return std::max(std::abs(v.geti("x", 2)), std::abs(v.geti("y", 2))); };
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    // A hard copy of the shape's alpha, offset and tinted, behind the pixels
    int ox = v.geti("x", 2), oy = v.geti("y", 2);
    uint32_t col = v.color("color");
    double op = v.get("opacity", 50) / 100.0;
    for(int y = 0; y < s.h; y++)
      for(int x = 0; x < s.w; x++) {
        int sx = x - ox, sy = y - oy;
        uint32_t a = s.contains(sx, sy) ? s.at(sx, sy) >> 24 : 0;
        uint32_t shadow = (col & 0xFFFFFF) | uint32_t(std::lround(a * ((col >> 24) / 255.0) * op)) << 24;
        d.at(x, y) = blendOver(shadow, s.at(x, y));
      }
  };
  registerEffect(e);

  e = {};
  e.id = "rotate-zoom"; e.name = "Rotate / Zoom"; e.category = "Layers";
  e.params = {
    { "angle", "Angle", EParam::Float, -180, 180, 0 },
    { "zoom", "Zoom", EParam::Float, 0.1, 10, 1 },
    { "panX", "Pan X", EParam::Int, -2000, 2000, 0 },
    { "panY", "Pan Y", EParam::Int, -2000, 2000, 0 },
  };
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    // Each output pixel samples the source back through the rotation (bilinear, premultiplied)
    double ang = v.get("angle") * 3.14159265 / 180, z = std::max(0.01, v.get("zoom", 1));
    double cs = std::cos(-ang) / z, sn = std::sin(-ang) / z;
    double cx = s.w / 2.0, cy = s.h / 2.0, px = v.get("panX"), py = v.get("panY");
    Premul p(s);
    auto at = [&](int x, int y, int k) { return s.contains(x, y) ? p.px[(size_t(y) * s.w + x) * 4 + k] : 0.f; };
    parallelRows(s.h, [&](int y0, int y1) {
      for(int y = y0; y < y1; y++)
        for(int x = 0; x < s.w; x++) {
          double dx = x + 0.5 - cx - px, dy = y + 0.5 - cy - py;
          double sx = cx + dx * cs - dy * sn - 0.5, sy = cy + dx * sn + dy * cs - 0.5;
          int ix = int(std::floor(sx)), iy = int(std::floor(sy));
          float fx = float(sx - ix), fy = float(sy - iy), c[4];
          for(int k = 0; k < 4; k++)
            c[k] = (at(ix, iy, k) * (1 - fx) + at(ix + 1, iy, k) * fx) * (1 - fy) + (at(ix, iy + 1, k) * (1 - fx) + at(ix + 1, iy + 1, k) * fx) * fy;
          uint32_t out = 0;
          if(c[3] >= 0.5f) {
            out = uint32_t(std::lround(c[3])) << 24;
            for(int k = 0; k < 3; k++) out |= uint32_t(std::clamp(std::lround(c[k] * 255 / c[3]), 0L, 255L)) << (k * 8);
          }
          d.at(x, y) = out;
        }
    });
  };
  registerEffect(e);
}

}
