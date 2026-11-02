#include "core/Effect.h"
#include "core/Oklab.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace david {

namespace {
inline int R(uint32_t c) { return c & 0xFF; }
inline int G(uint32_t c) { return (c >> 8) & 0xFF; }
inline int B(uint32_t c) { return (c >> 16) & 0xFF; }
inline uint32_t pack(int r, int g, int b, uint32_t a) {
  return uint32_t(std::clamp(r, 0, 255)) | uint32_t(std::clamp(g, 0, 255)) << 8 | uint32_t(std::clamp(b, 0, 255)) << 16 | (a & 0xFF000000u);
}
// Paint.NET's intensity
inline int intensity(uint32_t c) { return (7471 * B(c) + 38470 * G(c) + 19595 * R(c)) >> 16; }

// Every pixel through fn, in parallel
template<class F> void mapPixels(const Image& src, Image& dst, F fn) {
  parallelRows(src.h, [&](int y0, int y1) {
    for(int y = y0; y < y1; y++)
      for(int x = 0; x < src.w; x++) dst.at(x, y) = fn(src.at(x, y));
  });
}

// Three per-channel lookups (alpha kept)
void mapLuts(const Image& src, Image& dst, const uint8_t* r, const uint8_t* g, const uint8_t* b) {
  mapPixels(src, dst, [&](uint32_t c) { return pack(r[R(c)], g[G(c)], b[B(c)], c); });
}

Effect make(const char* id, const char* name, const char* chord, std::vector<EffectParam> params) {
  Effect e;
  e.id = id; e.name = name; e.category = "Adjustments"; e.chord = chord; e.params = std::move(params);
  return e;
}
}

void registerAdjustments() {
  #pragma region [ No params ]
  Effect e = make("invert", "Invert Colors", "Ctrl+Shift+I", {});
  e.run = [](const Image& s, Image& d, const ParamValues&, const EffectEnv&) {
    mapPixels(s, d, [](uint32_t c) { return (c ^ 0x00FFFFFFu); });
  };
  registerEffect(e);

  e = make("invert-alpha", "Invert Alpha", "Ctrl+Alt+I", {});
  e.run = [](const Image& s, Image& d, const ParamValues&, const EffectEnv&) {
    mapPixels(s, d, [](uint32_t c) { return c ^ 0xFF000000u; });
  };
  registerEffect(e);

  e = make("black-and-white", "Black and White", "Ctrl+Shift+G", {});
  e.run = [](const Image& s, Image& d, const ParamValues&, const EffectEnv&) {
    mapPixels(s, d, [](uint32_t c) { int i = intensity(c); return pack(i, i, i, c); });
  };
  registerEffect(e);

  e = make("sepia", "Sepia", "Ctrl+Shift+E", {});
  e.run = [](const Image& s, Image& d, const ParamValues&, const EffectEnv&) {
    // Gray first, then a warm tone that keeps black and white
    mapPixels(s, d, [](uint32_t c) {
      double i = intensity(c) / 255.0, bump = 4 * i * (1 - i);  // 0 at black and white, 1 in the middle
      return pack(int(std::lround(255 * (i + 0.10 * bump))), int(std::lround(255 * (i + 0.01 * bump))), int(std::lround(255 * (i - 0.13 * bump))), c);
    });
  };
  registerEffect(e);

  e = make("auto-level", "Auto-Level", "Ctrl+Shift+L", {});
  e.run = [](const Image& s, Image& d, const ParamValues&, const EffectEnv&) {
    // Each channel stretched so its 0.5% and 99.5% points reach 0 and 255 (opaque-ish pixels only)
    long hist[3][256] = {};
    long n = 0;
    for(uint32_t c : s.px) if((c >> 24) >= 128) { hist[0][R(c)]++; hist[1][G(c)]++; hist[2][B(c)]++; n++; }
    uint8_t lut[3][256];
    for(int ch = 0; ch < 3; ch++) {
      int lo = 0, hi = 255;
      long acc = 0, cut = n / 200;
      for(lo = 0; lo < 255; lo++) { acc += hist[ch][lo]; if(acc > cut) break; }
      acc = 0;
      for(hi = 255; hi > 0; hi--) { acc += hist[ch][hi]; if(acc > cut) break; }
      for(int v = 0; v < 256; v++) lut[ch][v] = hi > lo ? uint8_t(std::clamp((v - lo) * 255 / (hi - lo), 0, 255)) : uint8_t(v);
    }
    mapLuts(s, d, lut[0], lut[1], lut[2]);
  };
  registerEffect(e);
  #pragma endregion

  #pragma region [ With params ]
  e = make("brightness-contrast", "Brightness / Contrast", "Ctrl+Shift+T", {
    { "brightness", "Brightness", EParam::Int, -100, 100, 0 },
    { "contrast", "Contrast", EParam::Int, -100, 100, 0 },
  });
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    double b = v.get("brightness") * 2.55, c = v.get("contrast");
    double k = c >= 0 ? 100 / std::max(1.0, 100 - c * 0.99) : (100 + c) / 100;
    uint8_t lut[256];
    for(int i = 0; i < 256; i++) lut[i] = uint8_t(std::clamp(int(std::lround((i - 127.5) * k + 127.5 + b)), 0, 255));
    mapLuts(s, d, lut, lut, lut);
  };
  registerEffect(e);

  e = make("hue-saturation", "Hue / Saturation", "Ctrl+Shift+U", {
    { "hue", "Hue", EParam::Int, -180, 180, 0 },
    { "saturation", "Saturation", EParam::Int, 0, 200, 100 },
    { "lightness", "Lightness", EParam::Int, -100, 100, 0 },
  });
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    // In OKLCh, so a hue turn keeps the perceived lightness
    double turn = v.get("hue") * 3.14159265 / 180, sat = v.get("saturation", 100) / 100, light = v.get("lightness") / 100;
    double cs = std::cos(turn), sn = std::sin(turn);
    mapPixels(s, d, [&](uint32_t c) {
      Lab l = toOklab(c);
      double a = (l.a * cs - l.b * sn) * sat, b = (l.a * sn + l.b * cs) * sat, L = l.L;
      // Lightness mixes toward white or black, like Paint.NET
      if(light > 0) { L += (1 - L) * light; a *= 1 - light; b *= 1 - light; }
      else if(light < 0) { L *= 1 + light; a *= 1 + light; b *= 1 + light; }
      float r, g, bl;
      oklabToLinear({ float(L), float(a), float(b) }, r, g, bl);
      auto to8 = [](float x) { return int(std::lround(linearToSrgb(std::clamp(x, 0.f, 1.f)) * 255)); };
      return pack(to8(r), to8(g), to8(bl), c);
    });
  };
  registerEffect(e);

  e = make("posterize", "Posterize", "Ctrl+Shift+P", {
    { "levels", "Levels", EParam::Int, 2, 64, 16 },
  });
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    int n = std::clamp(v.geti("levels", 16), 2, 256);
    uint8_t lut[256];
    for(int i = 0; i < 256; i++) lut[i] = uint8_t(std::lround(std::round(i * (n - 1) / 255.0) * 255.0 / (n - 1)));
    mapLuts(s, d, lut, lut, lut);
  };
  registerEffect(e);

  e = make("levels", "Levels", "Ctrl+L", {
    { "channel", "Channel", EParam::Choice, 0, 3, 0, { "RGB", "Red", "Green", "Blue" } },
    { "inBlack", "Input black", EParam::Int, 0, 255, 0 },
    { "inWhite", "Input white", EParam::Int, 0, 255, 255 },
    { "gamma", "Gamma", EParam::Float, 0.1, 10, 1 },
    { "outBlack", "Output black", EParam::Int, 0, 255, 0 },
    { "outWhite", "Output white", EParam::Int, 0, 255, 255 },
  });
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    int ib = v.geti("inBlack"), iw = std::max(ib + 1, v.geti("inWhite", 255)), ob = v.geti("outBlack"), ow = v.geti("outWhite", 255);
    double gm = std::max(0.01, v.get("gamma", 1));
    uint8_t lut[256], id[256];
    for(int i = 0; i < 256; i++) {
      double t = std::clamp((i - ib) / double(iw - ib), 0.0, 1.0);
      lut[i] = uint8_t(std::clamp(int(std::lround(ob + std::pow(t, 1 / gm) * (ow - ob))), 0, 255));
      id[i] = uint8_t(i);
    }
    int ch = v.geti("channel");
    mapLuts(s, d, ch == 0 || ch == 1 ? lut : id, ch == 0 || ch == 2 ? lut : id, ch == 0 || ch == 3 ? lut : id);
  };
  registerEffect(e);

  e = make("curves", "Curves", "Ctrl+Shift+M", {
    { "curve", "Curve", EParam::Curve, 0, 0, 0, {}, "0,0 255,255|0,0 255,255|0,0 255,255|0,0 255,255" },
  });
  e.run = [](const Image& s, Image& d, const ParamValues& v, const EffectEnv&) {
    // Channels: all, red, green, blue; each channel's curve, then the "all" curve
    std::vector<std::string> parts;
    std::stringstream ss(v.str("curve"));
    std::string part;
    while(std::getline(ss, part, '|')) parts.push_back(part);
    while(parts.size() < 4) parts.push_back("0,0 255,255");
    auto all = curveTable(parts[0]);
    std::vector<uint8_t> lut[3];
    for(int c = 0; c < 3; c++) {
      auto own = curveTable(parts[c + 1]);
      lut[c].resize(256);
      for(int i = 0; i < 256; i++) lut[c][i] = all[own[i]];
    }
    mapLuts(s, d, lut[0].data(), lut[1].data(), lut[2].data());
  };
  registerEffect(e);
  #pragma endregion
}

}
