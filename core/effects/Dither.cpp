#include "core/Effect.h"
#include "core/Oklab.h"
#include "core/Palette.h"
#include "core/Pattern.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace david {

namespace {
enum EMethod { Bayer2, Bayer4, Bayer8, BlueNoise, Custom, FloydSteinberg, Atkinson, SierraLite, Jjn, METHODS };

struct V3 { float x, y, z; };
inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Colors in the matching space: OKLab, or sRGB scaled to 0-1
struct Space {
  bool srgb;
  V3 of(uint32_t c) const {
    if(srgb) return { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f };
    Lab l = toOklab(c);
    return { l.L, l.a, l.b };
  }
};

// The nearest palette color, the second nearest, and how far (0-0.5) the pixel sits toward the second
struct Match { int a = 0, b = 0; float t = 0; };

Match match(const std::vector<V3>& pal, V3 c) {
  Match m;
  float da = 1e30f, db = 1e30f;
  for(int i = 0; i < int(pal.size()); i++) {
    V3 d = c - pal[i];
    float dd = dot(d, d);
    if(dd < da) { db = da; m.b = m.a; da = dd; m.a = i; }
    else if(dd < db) { db = dd; m.b = i; }
  }
  if(pal.size() < 2) return m;
  V3 seg = pal[m.b] - pal[m.a];
  float len = dot(seg, seg);
  m.t = len > 0 ? std::clamp(dot(c - pal[m.a], seg) / len, 0.f, 0.5f) : 0;
  return m;
}

// Band and amount: below the band the pixel stays flat; above it t is stretched back to 0-0.5, then scaled
float shape(float t, float band, float amount) {
  float lo = (1 - band) * 0.5f;
  if(t <= lo || lo >= 0.5f) return 0;
  return (t - lo) / (0.5f - lo) * 0.5f * amount;
}

struct Tap { int dx, dy; float w; };
const std::vector<Tap>& kernel(int method) {
  static const std::vector<Tap> fs = { { 1, 0, 7 / 16.f }, { -1, 1, 3 / 16.f }, { 0, 1, 5 / 16.f }, { 1, 1, 1 / 16.f } };
  static const std::vector<Tap> atk = { { 1, 0, 1 / 8.f }, { 2, 0, 1 / 8.f }, { -1, 1, 1 / 8.f }, { 0, 1, 1 / 8.f }, { 1, 1, 1 / 8.f }, { 0, 2, 1 / 8.f } };
  static const std::vector<Tap> sierra = { { 1, 0, 2 / 4.f }, { -1, 1, 1 / 4.f }, { 0, 1, 1 / 4.f } };
  static const std::vector<Tap> jjn = {
    { 1, 0, 7 / 48.f }, { 2, 0, 5 / 48.f },
    { -2, 1, 3 / 48.f }, { -1, 1, 5 / 48.f }, { 0, 1, 7 / 48.f }, { 1, 1, 5 / 48.f }, { 2, 1, 3 / 48.f },
    { -2, 2, 1 / 48.f }, { -1, 2, 3 / 48.f }, { 0, 2, 5 / 48.f }, { 1, 2, 3 / 48.f }, { 2, 2, 1 / 48.f } };
  return method == Atkinson ? atk : method == SierraLite ? sierra : method == Jjn ? jjn : fs;
}

Palette paletteFor(const Image& src, const ParamValues& v, const EffectEnv& env) {
  Palette p;
  switch(v.geti("palette")) {
    case 0: if(env.palette) p = *env.palette; break;
    case 1: {
      std::string path = v.str("file"), err;
      auto fp = std::filesystem::path(path);
      if(!path.empty() && fp.is_relative() && !env.baseDir.empty()) path = env.baseDir + path;
      p.load(path, err);
      break;
    }
    case 2: p = Palette::fromPixels(src.px, std::clamp(v.geti("colors", 16), 2, 256)); break;
    default: p.colors = { env.primary | 0xFF000000u, env.secondary | 0xFF000000u }; break;
  }
  return p;
}
}

void registerDither() {
  Effect e;
  e.id = "dither"; e.name = "Dither"; e.category = "Effects/Distort";
  e.params = {
    { "palette", "Palette", EParam::Choice, 0, 3, 0, { "Current palette", "Palette file", "From image", "1-bit (primary and secondary)" } },
    { "file", "Palette file", EParam::Text, 0, 0, 0, {}, "", "palette=1" },
    { "colors", "Colors", EParam::Int, 2, 256, 16, {}, "", "palette=2" },
    { "method", "Method", EParam::Choice, 0, METHODS - 1, Bayer4, { "Bayer 2×2", "Bayer 4×4", "Bayer 8×8", "Blue noise", "Pattern", "Floyd–Steinberg", "Atkinson", "Sierra Lite", "Jarvis–Judice–Ninke" } },
    { "pattern", "Pattern", EParam::Text, 0, 0, 0, {}, "", "method=4" },
    { "amount", "Amount", EParam::Int, 0, 100, 100 },
    { "band", "Dither band", EParam::Int, 0, 100, 100 },
    { "space", "Match in", EParam::Choice, 0, 1, 0, { "OKLab", "sRGB" } },
    { "alpha", "Alpha", EParam::Choice, 0, 2, 0, { "Keep", "1-bit", "1-bit dithered" } },
    { "scale", "Pattern scale", EParam::Int, 1, 8, 1, {}, "", "method=0|1|2|3|4" },
    { "offsetX", "Pattern offset X", EParam::Int, 0, 64, 0, {}, "", "method=0|1|2|3|4" },
    { "offsetY", "Pattern offset Y", EParam::Int, 0, 64, 0, {}, "", "method=0|1|2|3|4" },
    { "serpentine", "Serpentine", EParam::Bool, 0, 1, 1, {}, "", "method=5|6|7|8" },
  };
  e.run = [](const Image& src, Image& dst, const ParamValues& v, const EffectEnv& env) {
    Palette pal = paletteFor(src, v, env);
    if(pal.empty()) return;
    Space sp{ v.geti("space") == 1 };
    std::vector<V3> pv;
    for(uint32_t c : pal.colors) pv.push_back(sp.of(c));
    int method = std::clamp(v.geti("method", Bayer4), 0, METHODS - 1);
    float amount = float(v.get("amount", 100) / 100), band = float(v.get("band", 100) / 100);
    int alphaMode = v.geti("alpha"), scale = std::max(1, v.geti("scale", 1));
    int offX = v.geti("offsetX"), offY = v.geti("offsetY");
    static const char* builtin[] = { "bayer2", "bayer4", "bayer8", "bluenoise" };
    const Pattern* pat = method < Custom ? findPattern(builtin[method]) : method == Custom ? findPattern(v.str("pattern")) : nullptr;
    if(method == Custom && !pat) pat = findPattern("bayer4");
    // The threshold at a pixel, lined up with the image (not the selection) and scaled into chunky cells
    auto threshold = [&](int x, int y) {
      const Pattern* p = pat ? pat : findPattern("bayer4");
      int px = int(std::floor(double(x + env.originX + offX) / scale)), py = int(std::floor(double(y + env.originY + offY) / scale));
      return p->at(px, py);
    };
    auto outAlpha = [&](uint32_t c, int x, int y) -> uint32_t {
      uint32_t a = c >> 24;
      if(alphaMode == 1) return a >= 128 ? 255 : 0;
      if(alphaMode == 2) return a / 255.f > threshold(x, y) ? 255 : 0;
      return a;
    };

    if(method <= Custom) {
      parallelRows(src.h, [&](int y0, int y1) {
        std::unordered_map<uint32_t, Match> cache;
        for(int y = y0; y < y1; y++)
          for(int x = 0; x < src.w; x++) {
            uint32_t c = src.at(x, y);
            if(!(c >> 24)) { dst.at(x, y) = c; continue; }
            auto it = cache.find(c & 0xFFFFFF);
            if(it == cache.end()) it = cache.emplace(c & 0xFFFFFF, match(pv, sp.of(c))).first;
            const Match& m = it->second;
            float t = shape(m.t, band, amount);
            int pick = t > threshold(x, y) ? m.b : m.a;
            dst.at(x, y) = (pal.colors[pick] & 0xFFFFFF) | outAlpha(c, x, y) << 24;
          }
      });
      return;
    }

    // Error diffusion: one pass in scan order (serpentine flips every other row), error scaled by the amount
    std::vector<V3> buf(src.px.size());
    for(size_t i = 0; i < src.px.size(); i++) buf[i] = sp.of(src.px[i]);
    const auto& taps = kernel(method);
    bool serp = v.geti("serpentine", 1) != 0;
    for(int y = 0; y < src.h; y++) {
      bool rev = serp && (y & 1);
      for(int k = 0; k < src.w; k++) {
        int x = rev ? src.w - 1 - k : k;
        uint32_t c = src.at(x, y);
        if(!(c >> 24)) { dst.at(x, y) = c; continue; }
        V3 cur = buf[size_t(y) * src.w + x];
        Match m = match(pv, cur);
        // Inside the band the error is passed on; below it the pixel snaps clean and its error is dropped
        bool spread = shape(m.t, band, 1) > 0 || band >= 1;
        int pick = m.a;
        dst.at(x, y) = (pal.colors[pick] & 0xFFFFFF) | outAlpha(c, x, y) << 24;
        if(!spread) continue;
        V3 err = cur - pv[pick];
        err = { err.x * amount, err.y * amount, err.z * amount };
        for(auto& tp : taps) {
          int nx = x + (rev ? -tp.dx : tp.dx), ny = y + tp.dy;
          if(nx < 0 || ny >= src.h || nx >= src.w) continue;
          V3& n = buf[size_t(ny) * src.w + nx];
          n = { n.x + err.x * tp.w, n.y + err.y * tp.w, n.z + err.z * tp.w };
        }
      }
    }
  };
  registerEffect(e);

  // Two-Color Dither: every pixel becomes a mix of two colors, placed by how far it sits from the first to the
  // second (along the line between them in OKLab), or only by its lightness (dark to the darker color). The
  // range can stretch to the image's own spread, so the whole image uses the full mix.
  e = {};
  e.id = "two-color-dither"; e.name = "Two-Color Dither"; e.category = "Effects/Distort";
  e.params = {
    { "color1", "Color 1", EParam::Color, 0, 0, double(0xFF000000u) },
    { "color2", "Color 2", EParam::Color, 0, 0, double(0xFFFFFFFFu) },
    { "mode", "Match by", EParam::Choice, 0, 1, 0, { "Nearest color", "Lightness" } },
    { "range", "Range", EParam::Choice, 0, 1, 0, { "The two colors", "The image" } },
    { "method", "Method", EParam::Choice, 0, METHODS - 1, Bayer4, { "Bayer 2×2", "Bayer 4×4", "Bayer 8×8", "Blue noise", "Pattern", "Floyd–Steinberg", "Atkinson", "Sierra Lite", "Jarvis–Judice–Ninke" } },
    { "pattern", "Pattern", EParam::Text, 0, 0, 0, {}, "", "method=4" },
    { "scale", "Pattern scale", EParam::Int, 1, 8, 1, {}, "", "method=0|1|2|3|4" },
    { "serpentine", "Serpentine", EParam::Bool, 0, 1, 1, {}, "", "method=5|6|7|8" },
  };
  e.run = [](const Image& src, Image& dst, const ParamValues& v, const EffectEnv& env) {
    uint32_t c1 = v.color("color1") | 0xFF000000u, c2 = v.color("color2") | 0xFF000000u;
    Space sp{ false };
    V3 a = sp.of(c1), b = sp.of(c2), seg = b - a;
    float len = dot(seg, seg);
    bool light = v.geti("mode") == 1;
    // t: 0 at color 1, 1 at color 2 (lightness: 0 at the darker one)
    bool firstDark = a.x <= b.x;
    float lo = std::min(a.x, b.x), hi = std::max(a.x, b.x);
    auto position = [&](uint32_t c) {
      V3 p = sp.of(c);
      if(light) {
        float t = hi - lo > 1e-4f ? (p.x - lo) / (hi - lo) : p.x;
        return firstDark ? t : 1 - t;
      }
      return len > 1e-12f ? dot(p - a, seg) / len : 0.f;
    };
    std::vector<float> t(src.px.size(), 0);
    float tmin = 1e30f, tmax = -1e30f;
    for(size_t i = 0; i < src.px.size(); i++) {
      if(!(src.px[i] >> 24)) continue;
      t[i] = position(src.px[i]);
      tmin = std::min(tmin, t[i]); tmax = std::max(tmax, t[i]);
    }
    bool stretch = v.geti("range") == 1 && tmax - tmin > 1e-6f;
    for(size_t i = 0; i < t.size(); i++) t[i] = std::clamp(stretch ? (t[i] - tmin) / (tmax - tmin) : t[i], 0.f, 1.f);
    auto put = [&](int x, int y, bool second) { uint32_t c = src.at(x, y); dst.at(x, y) = ((second ? c2 : c1) & 0xFFFFFF) | (c & 0xFF000000u); };

    int method = std::clamp(v.geti("method", Bayer4), 0, METHODS - 1), scale = std::max(1, v.geti("scale", 1));
    if(method <= Custom) {
      static const char* builtin[] = { "bayer2", "bayer4", "bayer8", "bluenoise" };
      const Pattern* pat = method < Custom ? findPattern(builtin[method]) : findPattern(v.str("pattern"));
      if(!pat) pat = findPattern("bayer4");
      parallelRows(src.h, [&](int y0, int y1) {
        for(int y = y0; y < y1; y++)
          for(int x = 0; x < src.w; x++) {
            if(!(src.at(x, y) >> 24)) { dst.at(x, y) = src.at(x, y); continue; }
            int px = int(std::floor(double(x + env.originX) / scale)), py = int(std::floor(double(y + env.originY) / scale));
            put(x, y, t[size_t(y) * src.w + x] > pat->at(px, py));
          }
      });
      return;
    }
    // Error diffusion on t alone
    const auto& taps = kernel(method);
    bool serp = v.geti("serpentine", 1) != 0;
    for(int y = 0; y < src.h; y++) {
      bool rev = serp && (y & 1);
      for(int k = 0; k < src.w; k++) {
        int x = rev ? src.w - 1 - k : k;
        if(!(src.at(x, y) >> 24)) { dst.at(x, y) = src.at(x, y); continue; }
        float cur = t[size_t(y) * src.w + x];
        bool second = cur >= 0.5f;
        put(x, y, second);
        float err = cur - (second ? 1.f : 0.f);
        for(auto& tp : taps) {
          int nx = x + (rev ? -tp.dx : tp.dx), ny = y + tp.dy;
          if(nx < 0 || ny >= src.h || nx >= src.w) continue;
          t[size_t(ny) * src.w + nx] += err * tp.w;
        }
      }
    }
  };
  registerEffect(e);

  // Dither Blur: a Gaussian blur put back onto the colors the image already uses (or the palette) with an
  // ordered pattern, so pixel art gets soft transitions without new colors; soft alpha edges dither too
  e = {};
  e.id = "dither-blur"; e.name = "Dither Blur"; e.category = "Effects/Blurs";
  e.params = {
    { "radius", "Radius", EParam::Float, 0, 50, 2 },
    { "colors", "Colors", EParam::Choice, 0, 1, 0, { "Image colors", "Current palette" } },
    { "method", "Pattern", EParam::Choice, 0, BlueNoise, Bayer4, { "Bayer 2×2", "Bayer 4×4", "Bayer 8×8", "Blue noise" } },
    { "alpha", "Alpha", EParam::Choice, 0, 1, 0, { "Dithered", "Soft" } },
  };
  e.margin = [](const ParamValues& v) { return int(std::ceil(v.get("radius", 2))) + 1; };
  e.run = [](const Image& src, Image& dst, const ParamValues& v, const EffectEnv& env) {
    Image blur = src;
    if(const Effect* g = findEffect("gaussian-blur")) {
      ParamValues gv = defaultValues(*g);
      gv.num["radius"] = v.get("radius", 2);
      g->run(src, blur, gv, env);
    }
    Palette pal;
    if(v.geti("colors") == 1 && env.palette) pal = *env.palette;
    else {
      // The image's own opaque colors; past 256 of them, a median-cut palette of the image
      std::unordered_map<uint32_t, bool> seen;
      for(uint32_t c : src.px)
        if(c >> 24 && seen.emplace(c & 0xFFFFFF, true).second && seen.size() > 256) break;
      if(seen.size() > 256) pal = Palette::fromPixels(src.px, 256);
      else for(auto& [c, _] : seen) pal.colors.push_back(c | 0xFF000000u);
    }
    if(pal.empty()) return;
    Space sp{ false };
    std::vector<V3> pv;
    for(uint32_t c : pal.colors) pv.push_back(sp.of(c));
    static const char* builtin[] = { "bayer2", "bayer4", "bayer8", "bluenoise" };
    const Pattern* pat = findPattern(builtin[std::clamp(v.geti("method", Bayer4), 0, int(BlueNoise))]);
    if(!pat) pat = findPattern("bayer4");
    bool softAlpha = v.geti("alpha") == 1;
    parallelRows(src.h, [&](int y0, int y1) {
      std::unordered_map<uint32_t, Match> cache;
      for(int y = y0; y < y1; y++)
        for(int x = 0; x < src.w; x++) {
          uint32_t c = blur.at(x, y), a = c >> 24;
          float th = pat->at(x + env.originX, y + env.originY);
          if(!softAlpha) a = a / 255.f > th ? 255 : 0;
          if(!a) { dst.at(x, y) = 0; continue; }
          auto it = cache.find(c & 0xFFFFFF);
          if(it == cache.end()) it = cache.emplace(c & 0xFFFFFF, match(pv, sp.of(c))).first;
          const Match& m = it->second;
          // t runs 0-0.5 toward the second nearest color, as in Dither
          dst.at(x, y) = (pal.colors[m.t > th ? m.b : m.a] & 0xFFFFFF) | a << 24;
        }
    });
  };
  registerEffect(e);
}

}
