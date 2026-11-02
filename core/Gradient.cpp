#include "core/Gradient.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Oklab.h"
#include "core/Paint.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace david {

static constexpr int TABLE = 4096;
static constexpr double PI = 3.14159265358979323846;

#pragma region [ Dither patterns ]
float bayer4(int x, int y) {
  static const int m[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
  return (m[y & 3][x & 3] + 0.5f) / 16;
}

const std::vector<float>& blueNoise() {
  static const std::vector<float> table = [] {
    constexpr int N = 64, SIZE = N * N;
    // Gaussian energy on a torus, so the pattern tiles
    std::vector<float> kernel(SIZE);
    for(int y = 0; y < N; y++)
      for(int x = 0; x < N; x++) {
        int dx = std::min(x, N - x), dy = std::min(y, N - y);
        kernel[y * N + x] = std::exp(-(dx * dx + dy * dy) / (2 * 1.5f * 1.5f));
      }
    std::vector<uint8_t> on(SIZE, 0);
    std::vector<float> energy(SIZE, 0);
    auto put = [&](std::vector<uint8_t>& p, std::vector<float>& e, int i, bool v) {
      p[i] = v;
      int px = i % N, py = i / N;
      float s = v ? 1.f : -1.f;
      for(int y = 0; y < N; y++)
        for(int x = 0; x < N; x++) e[y * N + x] += s * kernel[((y - py + N) % N) * N + (x - px + N) % N];
    };
    auto pick = [&](const std::vector<uint8_t>& p, const std::vector<float>& e, bool ones) {
      int best = -1;
      for(int i = 0; i < SIZE; i++) {
        if(bool(p[i]) != ones) continue;
        if(best < 0 || (ones ? e[i] > e[best] : e[i] < e[best])) best = i;
      }
      return best;
    };

    // Initial pattern: random points, then moved from the tightest cluster to the largest void until stable
    std::mt19937 rng(1);
    int count = SIZE / 10;
    for(int placed = 0; placed < count;) {
      int i = int(rng() % SIZE);
      if(!on[i]) { put(on, energy, i, true); placed++; }
    }
    for(int guard = 0; guard < SIZE; guard++) {
      int c = pick(on, energy, true);
      put(on, energy, c, false);
      int v = pick(on, energy, false);
      put(on, energy, v, true);
      if(v == c) break;
    }

    std::vector<int> rank(SIZE);
    // Ranks below the initial count: remove clusters one by one
    auto p = on;
    auto e = energy;
    for(int r = count - 1; r >= 0; r--) {
      int c = pick(p, e, true);
      put(p, e, c, false);
      rank[c] = r;
    }
    // The rest: fill voids one by one
    for(int r = count; r < SIZE; r++) {
      int v = pick(on, energy, false);
      put(on, energy, v, true);
      rank[v] = r;
    }
    std::vector<float> out(SIZE);
    for(int i = 0; i < SIZE; i++) out[i] = (rank[i] + 0.5f) / SIZE;
    return out;
  }();
  return table;
}
#pragma endregion

#pragma region [ Color table ]
namespace {
  struct Vec3 { float x, y, z; };

  Vec3 toSpace(uint32_t c, EGradSpace s) {
    uint8_t r = uint8_t(c), g = uint8_t(c >> 8), b = uint8_t(c >> 16);
    if(s == EGradSpace::Srgb) return { r / 255.f, g / 255.f, b / 255.f };
    if(s == EGradSpace::Linear) return { srgbToLinear(r), srgbToLinear(g), srgbToLinear(b) };
    Lab l = toOklab(c);
    if(s == EGradSpace::Oklab) return { l.L, l.a, l.b };
    return { l.L, std::sqrt(l.a * l.a + l.b * l.b), std::atan2(l.b, l.a) };
  }

  // Back to sRGB 0-255, clipped to the gamut
  Vec3 fromSpace(Vec3 v, EGradSpace s) {
    float r, g, b;
    if(s == EGradSpace::Srgb) { r = v.x; g = v.y; b = v.z; }
    else {
      if(s == EGradSpace::Linear) { r = v.x; g = v.y; b = v.z; }
      else {
        Lab l = s == EGradSpace::Oklab ? Lab{ v.x, v.y, v.z } : Lab{ v.x, v.y * std::cos(v.z), v.y * std::sin(v.z) };
        oklabToLinear(l, r, g, b);
      }
      r = linearToSrgb(r); g = linearToSrgb(g); b = linearToSrgb(b);
    }
    return { std::clamp(r, 0.f, 1.f) * 255, std::clamp(g, 0.f, 1.f) * 255, std::clamp(b, 0.f, 1.f) * 255 };
  }
}

std::vector<float> gradientTable(const GradientSpec& g) {
  std::vector<float> out(TABLE * 4);
  if(g.stops.empty()) return out;
  for(int i = 0; i < TABLE; i++) {
    float t = i / float(TABLE - 1);
    size_t k = 0;
    while(k + 1 < g.stops.size() && g.stops[k + 1].pos < t) k++;
    const GradStop& s0 = g.stops[k];
    const GradStop& s1 = g.stops[std::min(k + 1, g.stops.size() - 1)];
    float f = s1.pos > s0.pos ? std::clamp((t - s0.pos) / (s1.pos - s0.pos), 0.f, 1.f) : (t >= s1.pos ? 1.f : 0.f);
    float a0 = (s0.color >> 24) / 255.f, a1 = (s1.color >> 24) / 255.f;
    // Colors mix weighted by alpha (premultiplied), so a fade to transparent keeps its hue
    float w0 = (1 - f) * a0, w1 = f * a1;
    if(w0 + w1 <= 0) { w0 = 1 - f; w1 = f; }
    float k1 = w1 / (w0 + w1);
    Vec3 c0 = toSpace(s0.color, g.space), c1 = toSpace(s1.color, g.space), m;
    if(g.space == EGradSpace::OklchShort || g.space == EGradSpace::OklchLong) {
      // A gray stop has no hue; it takes the other stop's hue
      if(c0.y < 0.002f) c0.z = c1.z;
      if(c1.y < 0.002f) c1.z = c0.z;
      float dh = c1.z - c0.z;
      while(dh > PI) dh -= float(2 * PI);
      while(dh < -PI) dh += float(2 * PI);
      if(g.space == EGradSpace::OklchLong && dh != 0) dh -= float(dh > 0 ? 2 * PI : -2 * PI);
      m = { c0.x + (c1.x - c0.x) * k1, c0.y + (c1.y - c0.y) * k1, c0.z + dh * k1 };
    } else {
      m = { c0.x + (c1.x - c0.x) * k1, c0.y + (c1.y - c0.y) * k1, c0.z + (c1.z - c0.z) * k1 };
    }
    Vec3 rgb = fromSpace(m, g.space);
    float* o = &out[i * 4];
    o[0] = rgb.x; o[1] = rgb.y; o[2] = rgb.z; o[3] = (a0 + (a1 - a0) * f) * 255;
  }
  return out;
}
#pragma endregion

#pragma region [ Render ]
float gradientPos(const GradientSpec& g, double x, double y) {
  double dx = g.b.x - g.a.x, dy = g.b.y - g.a.y, len2 = dx * dx + dy * dy;
  if(len2 < 1e-9) return 0;
  double px = x - g.a.x, py = y - g.a.y;
  double u = (px * dx + py * dy) / len2, v = (-px * dy + py * dx) / len2;
  auto frac = [](double t) { return t - std::floor(t); };
  switch(g.type) {
    case EGradType::Linear: return float(std::clamp(u, 0.0, 1.0));
    case EGradType::Reflected: return float(std::clamp(std::abs(u), 0.0, 1.0));
    case EGradType::Diamond: return float(std::clamp(std::abs(u) + std::abs(v), 0.0, 1.0));
    case EGradType::Radial: return float(std::clamp(std::sqrt(u * u + v * v), 0.0, 1.0));
    case EGradType::Conical: return float(std::abs(std::atan2(v, u)) / PI);
    case EGradType::SpiralCW: return float(frac(std::sqrt(u * u + v * v) + std::atan2(v, u) / (2 * PI)));
    case EGradType::SpiralCCW: return float(frac(std::sqrt(u * u + v * v) - std::atan2(v, u) / (2 * PI)));
    default: return 0;
  }
}

void renderGradient(Document& doc, TileRecorder& rec, const GradientSpec& g) {
  if(g.stops.size() < 2) return;
  std::vector<float> table = gradientTable(g);
  const std::vector<float>* noise = g.dither == EDither::BlueNoise ? &blueNoise() : nullptr;
  IRect all{ 0, 0, doc.width() - 1, doc.height() - 1 };
  paintRegion(doc, rec, all, [&](int x0, int y, int n, const uint32_t* before, uint32_t* out) {
    for(int i = 0; i < n; i++) {
      int x = x0 + i;
      double px = x + 0.5, py = y + 0.5;
      if(g.mirrorX && (px < g.axisX) != (g.a.x < g.axisX)) px = 2 * g.axisX - px;
      if(g.mirrorY && (py < g.axisY) != (g.a.y < g.axisY)) py = 2 * g.axisY - py;
      float t = gradientPos(g, px, py);
      uint32_t c;
      if(g.pixelArt) {
        // Only stop colors: the pixel takes the next stop when its position passes the Bayer threshold
        size_t k = 0;
        while(k + 2 < g.stops.size() && g.stops[k + 1].pos < t) k++;
        const GradStop &s0 = g.stops[k], &s1 = g.stops[k + 1];
        float f = s1.pos > s0.pos ? std::clamp((t - s0.pos) / (s1.pos - s0.pos), 0.f, 1.f) : 0.f;
        c = f > bayer4(x, y) ? s1.color : s0.color;
      } else {
        const float* v = &table[std::min(TABLE - 1, int(t * (TABLE - 1) + 0.5f)) * 4];
        float th = g.dither == EDither::Off ? 0.5f : noise ? (*noise)[(y & 63) * 64 + (x & 63)] : bayer4(x, y);
        c = 0;
        for(int k = 0; k < 4; k++) c |= uint32_t(std::clamp(int(std::floor(v[k] + th)), 0, 255)) << (k * 8);
      }
      if(g.alphaMode) {
        uint32_t a = (before[i] >> 24) * (c >> 24) / 255;
        out[i] = a ? (before[i] & 0x00FFFFFF) | (a << 24) : 0;
      } else {
        out[i] = blendOver(before[i], c);
      }
    }
  });
}
#pragma endregion

}
