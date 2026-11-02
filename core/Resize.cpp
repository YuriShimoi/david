#include "core/Resize.h"
#include "core/Vector.h"
#include "core/Layer.h"
#include "core/Document.h"
#include "core/Oklab.h"
#include "core/Text.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <thread>

namespace david {

#pragma region [ Resampling ]
namespace {
  struct Kernel {
    double support;
    double (*f)(double);
  };

  double box(double x) { return std::abs(x) <= 0.5 ? 1 : 0; }
  double triangle(double x) { x = std::abs(x); return x < 1 ? 1 - x : 0; }
  // Catmull-Rom: sharp, the usual "bicubic"
  double catmullRom(double x) {
    x = std::abs(x);
    if(x < 1) return 1.5 * x * x * x - 2.5 * x * x + 1;
    if(x < 2) return -0.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
    return 0;
  }
  // Mitchell-Netravali (B = C = 1/3): less ringing, used for "best quality"
  double mitchell(double x) {
    x = std::abs(x);
    const double B = 1.0 / 3, C = 1.0 / 3;
    if(x < 1) return ((12 - 9 * B - 6 * C) * x * x * x + (-18 + 12 * B + 6 * C) * x * x + (6 - 2 * B)) / 6;
    if(x < 2) return ((-B - 6 * C) * x * x * x + (6 * B + 30 * C) * x * x + (-12 * B - 48 * C) * x + (8 * B + 24 * C)) / 6;
    return 0;
  }

  Kernel kernelFor(EResample m) {
    switch(m) {
      case EResample::Bilinear: return { 1, triangle };
      case EResample::Bicubic: return { 2, catmullRom };
      case EResample::Supersample: return { 0.5, box };
      default: return { 2, mitchell };
    }
  }

  // Source taps and weights for every destination column (or row)
  struct Taps {
    std::vector<int> start, count;
    std::vector<float> weights;  // count[i] weights from offset[i]
    std::vector<size_t> offset;
  };

  Taps makeTaps(int srcN, int dstN, Kernel k) {
    Taps t;
    double scale = double(dstN) / srcN, widen = std::max(1.0, 1 / scale), support = k.support * widen;
    for(int i = 0; i < dstN; i++) {
      double center = (i + 0.5) / scale;
      int a = std::max(0, int(std::floor(center - support))), b = std::min(srcN - 1, int(std::ceil(center + support)));
      t.offset.push_back(t.weights.size());
      double sum = 0;
      size_t first = t.weights.size();
      for(int s = a; s <= b; s++) {
        double w = k.f((s + 0.5 - center) / widen);
        t.weights.push_back(float(w));
        sum += w;
      }
      if(sum != 0) for(size_t j = first; j < t.weights.size(); j++) t.weights[j] = float(t.weights[j] / sum);
      t.start.push_back(a);
      t.count.push_back(b - a + 1);
    }
    return t;
  }

  // Premultiplied float pixel from straight RGBA8
  void load(uint32_t c, bool gamma, float* out) {
    float a = (c >> 24) / 255.f;
    for(int k = 0; k < 3; k++) {
      uint8_t v = uint8_t(c >> (k * 8));
      out[k] = (gamma ? srgbToLinear(v) : v / 255.f) * a;
    }
    out[3] = a;
  }

  uint32_t store(const float* p, bool gamma) {
    float a = std::clamp(p[3], 0.f, 1.f);
    if(a <= 0.5f / 255) return 0;
    uint32_t out = uint32_t(std::round(a * 255)) << 24;
    for(int k = 0; k < 3; k++) {
      float v = std::clamp(p[k] / a, 0.f, 1.f);
      if(gamma) v = linearToSrgb(v);
      out |= uint32_t(std::round(v * 255)) << (k * 8);
    }
    return out;
  }

  // Tiles are allocated up front, so workers write pixels without touching the layer's structure
  void allocateAll(Layer& l) {
    for(int ty = 0; ty < l.tilesY(); ty++)
      for(int tx = 0; tx < l.tilesX(); tx++) l.ensureTile(tx, ty);
  }

  void dropEmptyTiles(Layer& l) {
    for(int ty = 0; ty < l.tilesY(); ty++)
      for(int tx = 0; tx < l.tilesX(); tx++)
        if(Tile* t = l.tile(tx, ty)) { if(t->isEmpty()) l.putTile(tx, ty, nullptr); else t->touch(); }
  }

  void writePixel(Layer& l, int x, int y, uint32_t v) {
    l.tile(x / Tile::SIZE, y / Tile::SIZE)->data()[(y % Tile::SIZE) * Tile::SIZE + x % Tile::SIZE] = v;
  }
}

// A layer of w × h with the same frames as src (empty and linked cels kept), each frame made by make(frame).
// A vector layer's objects go through vec instead, and its pixels are rendered again from them.
static std::unique_ptr<Layer> perFrame(const Layer& src, int w, int h, const std::function<void(int, Layer&)>& make, const Affine& vec) {
  auto out = std::make_unique<Layer>(w, h, src.name);
  out->kind = src.kind;
  out->visible = src.visible;
  out->opacity = src.opacity;
  out->blend = src.blend;
  out->fitFrames(src.frames(), 0);
  for(int f = 0; f < src.frames(); f++) {
    int g = 0;
    while(g < f && !src.linked(f, g)) g++;
    if(g < f) { out->setCel(f, out->cel(g)); continue; }
    if(!src.cel(f)) continue;
    out->setFrame(f);
    if(const VecTree* t = src.kind == ELayerKind::Vector ? vectorTreeOf(src, f) : nullptr) {
      VecTree& dst = vectorTree(*out, f);
      dst = *t;
      for(auto& o : dst.objects) o.xf = vec * o.xf;
      dst.touch();
      rasterizeVectorCel(*out, f);
      continue;
    }
    make(f, *out);
  }
  out->setFrame(src.frame());
  return out;
}

static void resampleFrame(const Layer& src, int f, Layer& dst, int w, int h, EResample mode, bool gamma) {
  Layer* out = &dst;
  int sw = src.width(), sh = src.height();
  allocateAll(*out);
  if(mode == EResample::Nearest) {
    for(int y = 0; y < h; y++) {
      int sy = std::min(sh - 1, int((y + 0.5) * sh / h));
      for(int x = 0; x < w; x++) writePixel(*out, x, y, src.getAt(f, std::min(sw - 1, int((x + 0.5) * sw / w)), sy));
    }
    dropEmptyTiles(*out);
    return;
  }

  Kernel k = kernelFor(mode);
  // Best quality: Mitchell up, area average down (no ringing, no skipped pixels)
  if(mode == EResample::Best && (w < sw || h < sh)) k = { 0.5, box };
  Taps tx = makeTaps(sw, w, k), ty = makeTaps(sh, h, k);

  // Destination rows in bands per thread; each band keeps the horizontally resampled source rows it needs
  int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
  int band = std::max(16, (h + threads - 1) / threads);
  std::atomic<int> next{ 0 };
  auto work = [&] {
    std::vector<float> srcRow(size_t(sw) * 4), acc(size_t(w) * 4);
    std::map<int, std::vector<float>> rows;
    auto hrow = [&](int sy) -> const std::vector<float>& {
      auto& r = rows[sy];
      if(!r.empty()) return r;
      for(int x = 0; x < sw; x++) load(src.getAt(f, x, sy), gamma, &srcRow[size_t(x) * 4]);
      r.assign(size_t(w) * 4, 0.f);
      for(int x = 0; x < w; x++) {
        float* o = &r[size_t(x) * 4];
        const float* wt = &tx.weights[tx.offset[x]];
        for(int j = 0; j < tx.count[x]; j++) {
          const float* p = &srcRow[size_t(tx.start[x] + j) * 4];
          for(int c = 0; c < 4; c++) o[c] += p[c] * wt[j];
        }
      }
      return r;
    };
    for(int b; (b = next++) * band < h;) {
      int y0 = b * band, y1 = std::min(h, y0 + band);
      rows.clear();
      for(int y = y0; y < y1; y++) {
        // Rows the filter no longer reaches are dropped
        while(!rows.empty() && rows.begin()->first < ty.start[y]) rows.erase(rows.begin());
        std::fill(acc.begin(), acc.end(), 0.f);
        const float* wt = &ty.weights[ty.offset[y]];
        for(int j = 0; j < ty.count[y]; j++) {
          const std::vector<float>& r = hrow(ty.start[y] + j);
          for(size_t i = 0; i < acc.size(); i++) acc[i] += r[i] * wt[j];
        }
        for(int x = 0; x < w; x++) writePixel(*out, x, y, store(&acc[size_t(x) * 4], gamma));
      }
    }
  };
  std::vector<std::thread> pool;
  for(int i = 1; i < threads; i++) pool.emplace_back(work);
  work();
  for(auto& t : pool) t.join();
  dropEmptyTiles(*out);
}

std::unique_ptr<Layer> resampleLayer(const Layer& src, int w, int h, EResample mode, bool gamma) {
  return perFrame(src, w, h, [&](int f, Layer& out) { resampleFrame(src, f, out, w, h, mode, gamma); },
                  Affine::scaling(double(w) / src.width(), double(h) / src.height()));
}

std::unique_ptr<Layer> reframeLayer(const Layer& src, int w, int h, int dx, int dy, uint32_t fill) {
  return perFrame(src, w, h, [&](int f, Layer& out) {
    if(fill) out.fill(fill);
    for(int y = std::max(0, dy); y < std::min(h, src.height() + dy); y++)
      for(int x = std::max(0, dx); x < std::min(w, src.width() + dx); x++) {
        uint32_t v = src.getAt(f, x - dx, y - dy);
        if(v || fill) out.set(x, y, v);
      }
  }, Affine::translate(dx, dy));
}
#pragma endregion

#pragma region [ Document ]
size_t DocumentReplace::bytes() const {
  size_t n = 0;
  for(auto* s : { &before, &after })
    for(auto& l : s->layers) n += l->allocatedBytes();
  return n;
}

void DocumentReplace::apply(Document& doc, State& s) {
  // Swap in this state's layers (copies, so the step can be applied again)
  std::vector<std::unique_ptr<Layer>> layers;
  for(auto& l : s.layers) layers.push_back(l->clone(true));
  doc.replaceImage(s.width, s.height, std::move(layers), s.selection->clone());
  doc.dpi = s.dpi;
}

static void pushReplace(Document& doc, std::vector<std::unique_ptr<Layer>> layers, int w, int h, double dpi, std::string name, const char* icon) {
  auto step = std::make_unique<DocumentReplace>();
  step->name = std::move(name);
  step->icon = icon;
  step->before = { doc.width(), doc.height(), doc.dpi, {}, doc.selection->clone() };
  for(auto& l : doc.layers) step->before.layers.push_back(l->clone(true));
  step->after = { w, h, dpi, std::move(layers), std::make_unique<Mask>(w, h) };
  step->redo(doc);
  doc.history.push(doc, std::move(step));
}

void resizeImage(Document& doc, int w, int h, double dpi, EResample mode, bool gamma, const char* icon) {
  std::vector<std::unique_ptr<Layer>> layers;
  for(auto& l : doc.layers) layers.push_back(resampleLayer(*l, w, h, mode, gamma));
  pushReplace(doc, std::move(layers), w, h, dpi, N_("Resize Image"), icon);
}

void resizeCanvas(Document& doc, int w, int h, int anchorX, int anchorY, uint32_t fill, const char* icon) {
  int dx = (w - doc.width()) * anchorX / 2, dy = (h - doc.height()) * anchorY / 2;
  std::vector<std::unique_ptr<Layer>> layers;
  for(size_t i = 0; i < doc.layers.size(); i++) layers.push_back(reframeLayer(*doc.layers[i], w, h, dx, dy, i == 0 ? fill : 0));
  pushReplace(doc, std::move(layers), w, h, doc.dpi, N_("Canvas Size"), icon);
}
#pragma endregion

}
