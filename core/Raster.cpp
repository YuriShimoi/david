#include "core/Raster.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Stroke.h"
#include "core/TileEdit.h"
#include "core/Oklab.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>
#include <unordered_map>

namespace david {

#pragma region [ Selection shapes ]
std::unique_ptr<Mask> rectMask(int w, int h, double x0, double y0, double x1, double y1) {
  auto m = std::make_unique<Mask>(w, h);
  int ix0 = int(std::floor(std::min(x0, x1))), ix1 = int(std::ceil(std::max(x0, x1))) - 1;
  int iy0 = int(std::floor(std::min(y0, y1))), iy1 = int(std::ceil(std::max(y0, y1))) - 1;
  for(int y = std::max(iy0, 0); y <= std::min(iy1, h - 1); y++) m->fillSpan(y, ix0, ix1, 255);
  return m;
}

// Antialiased by the approximate signed distance to the ellipse edge
std::unique_ptr<Mask> ellipseMask(int w, int h, double x0, double y0, double x1, double y1) {
  auto m = std::make_unique<Mask>(w, h);
  double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, rx = std::abs(x1 - x0) / 2, ry = std::abs(y1 - y0) / 2;
  if(rx < 0.5 || ry < 0.5) return m;
  int ya = std::max(0, int(std::floor(cy - ry))), yb = std::min(h - 1, int(std::ceil(cy + ry)));
  for(int y = ya; y <= yb; y++) {
    double v = (y + 0.5 - cy) / ry;
    double inner = 1 - v * v;
    // Pixels well inside the edge are filled as one span
    double half = inner > 0 ? rx * std::sqrt(inner) : 0;
    int fa = int(std::ceil(cx - half + 1.5)), fb = int(std::floor(cx + half - 2.5));
    if(fa <= fb) m->fillSpan(y, fa, fb, 255);
    int xa = std::max(0, int(std::floor(cx - rx - 1))), xb = std::min(w - 1, int(std::ceil(cx + rx + 1)));
    for(int x = xa; x <= xb; x++) {
      if(x >= fa && x <= fb) continue;
      double u = (x + 0.5 - cx) / rx;
      double f = u * u + v * v - 1;
      double gx = 2 * u / rx, gy = 2 * v / ry;
      double g = std::sqrt(gx * gx + gy * gy);
      double d = g > 0 ? f / g : -1;
      double c = std::clamp(0.5 - d, 0.0, 1.0);
      if(c > 0) m->set(x, y, uint8_t(c * 255 + 0.5));
    }
  }
  return m;
}

std::unique_ptr<Mask> polygonMask(int w, int h, const std::vector<Point>& pts) {
  auto m = std::make_unique<Mask>(w, h);
  if(pts.size() < 3) return m;
  double minY = pts[0].y, maxY = pts[0].y;
  for(auto& p : pts) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
  std::vector<double> xs;
  for(int y = std::max(0, int(std::floor(minY))); y <= std::min(h - 1, int(std::ceil(maxY))); y++) {
    double yc = y + 0.5;
    xs.clear();
    for(size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
      const Point &a = pts[i], &b = pts[j];
      if((a.y > yc) != (b.y > yc)) xs.push_back(a.x + (yc - a.y) * (b.x - a.x) / (b.y - a.y));
    }
    std::sort(xs.begin(), xs.end());
    for(size_t k = 0; k + 1 < xs.size(); k += 2) {
      int xa = int(std::ceil(xs[k] - 0.5)), xb = int(std::ceil(xs[k + 1] - 0.5)) - 1;
      if(xa <= xb) m->fillSpan(y, xa, xb, 255);
    }
  }
  return m;
}
#pragma endregion

#pragma region [ Outline ]
std::vector<Segment> maskOutline(const Mask& mask) {
  std::vector<Segment> out;
  IRect b = mask.bounds();
  if(b.empty()) return out;
  int x0 = b.x0 - 1, x1 = b.x1 + 1, n = x1 - x0 + 1;
  std::vector<uint8_t> prev(n, 0), cur(n);
  std::vector<int> openY(n, -1);
  for(int y = b.y0; y <= b.y1 + 1; y++) {
    if(y <= b.y1) mask.readRow(y, x0, x1, cur.data());
    else std::fill(cur.begin(), cur.end(), 0);
    for(auto& v : cur) v = v >= 128;

    // Horizontal edges between row y-1 and y, merged into runs
    for(int i = 0; i < n;) {
      if(prev[i] == cur[i]) { i++; continue; }
      int j = i;
      while(j < n && prev[j] != cur[j]) j++;
      out.push_back({ x0 + i, y, x0 + j, y });
      i = j;
    }
    // Vertical edges inside row y, extended while the same x keeps an edge
    for(int i = 1; i < n; i++) {
      bool edge = y <= b.y1 && cur[i - 1] != cur[i];
      if(edge && openY[i] < 0) openY[i] = y;
      if(!edge && openY[i] >= 0) { out.push_back({ x0 + i, openY[i], x0 + i, y }); openY[i] = -1; }
    }
    std::swap(prev, cur);
  }
  return out;
}
#pragma endregion

#pragma region [ Flood fill ]
namespace {
  // Reads layer pixels, or the flattened image with rows built on first use
  struct Sampler {
    const Document& doc;
    const Layer* layer;
    std::vector<std::vector<uint32_t>> rows;

    uint32_t get(int x, int y) {
      if(layer) return layer->get(x, y);
      if(rows.empty()) rows.resize(doc.height());
      auto& r = rows[y];
      if(r.empty()) { r.resize(doc.width()); flattenRow(doc, y, r.data()); }
      return r[x];
    }
  };

  float distance(const FloodOptions& o, uint32_t a, uint32_t b) { return o.classicRgb ? rgbDistance(a, b) : colorDistance(a, b); }

  bool isOutline(const FloodOptions& o, uint32_t c) {
    // The tolerance widens what counts as outline
    return o.tolerance <= 0 ? c == o.outline : distance(o, c, o.outline) <= o.tolerance;
  }

  // Coverage of one color, with a direct-mapped cache because images repeat colors a lot
  struct Matcher {
    const FloodOptions& o;
    uint32_t target;
    struct Slot { uint32_t c = 0; uint8_t v = 0; bool used = false; };
    std::vector<Slot> cache = std::vector<Slot>(4096);

    uint8_t coverage(uint32_t c) {
      Slot& s = cache[((c * 2654435761u) >> 20) & 4095];
      if(s.used && s.c == c) return s.v;
      s = { c, compute(c), true };
      return s.v;
    }

    uint8_t compute(uint32_t c) const {
      if(o.toOutline) return isOutline(o, c) ? 0 : 255;
      if(o.tolerance <= 0) return c == target ? 255 : 0;
      float d = distance(o, c, target);
      if(d > o.tolerance) return 0;
      if(!o.soft) return 255;
      // Full up to half the tolerance, smooth falloff to 0 at the tolerance
      float t = std::clamp((o.tolerance - d) / (o.tolerance * 0.5f), 0.f, 1.f);
      return uint8_t(std::max(1.f, t * t * (3 - 2 * t) * 255 + 0.5f));
    }
  };

  // OKLab lightness in lazily filled 256×256 tiles, for the edge-stop Sobel
  struct Lightness {
    Sampler& s;
    int w, h;
    std::unordered_map<int, std::unique_ptr<float[]>> tiles;

    float get(int x, int y) {
      x = std::clamp(x, 0, w - 1); y = std::clamp(y, 0, h - 1);
      int tx = x >> 8, ty = y >> 8;
      auto& t = tiles[ty * 65536 + tx];
      if(!t) {
        t = std::make_unique<float[]>(256 * 256);
        for(int yy = 0; yy < 256; yy++)
          for(int xx = 0; xx < 256; xx++) {
            uint32_t c = s.get(std::min(tx * 256 + xx, w - 1), std::min(ty * 256 + yy, h - 1));
            // Scaled by alpha, so a hard alpha edge also stops the fill
            t[yy * 256 + xx] = toOklab(c).L * ((c >> 24) / 255.f);
          }
      }
      return t[(y & 255) * 256 + (x & 255)];
    }

    // Sobel magnitude scaled so a hard step of ΔL reads as ΔL
    float edge(int x, int y) {
      float a = get(x - 1, y - 1), b = get(x, y - 1), c = get(x + 1, y - 1);
      float d = get(x - 1, y), f = get(x + 1, y);
      float g = get(x - 1, y + 1), hh = get(x, y + 1), i = get(x + 1, y + 1);
      float gx = (c + 2 * f + i) - (a + 2 * d + g), gy = (g + 2 * hh + i) - (a + 2 * b + c);
      return std::sqrt(gx * gx + gy * gy) / 4;
    }
  };
}

std::unique_ptr<Mask> floodMask(const Document& doc, const Layer* layer, int sx, int sy, const FloodOptions& o) {
  int w = doc.width(), h = doc.height();
  auto m = std::make_unique<Mask>(w, h);
  if(sx < 0 || sy < 0 || sx >= w || sy >= h) return m;
  Sampler s{ doc, layer, {} };
  uint32_t target = s.get(sx, sy);
  if(o.toOutline && isOutline(o, target)) return m;

  if(!o.contiguous) {
    // Parallel bands of one tile row; each thread has its own row buffer and cache, the mask is written afterwards
    int bands = (h + 255) / 256;
    std::vector<std::vector<uint8_t>> out(bands);
    std::atomic<int> next{ 0 };
    auto work = [&] {
      Matcher mt{ o, target };
      std::vector<uint32_t> row(w);
      for(int b; (b = next++) < bands;) {
        int y0 = b * 256, y1 = std::min(h, y0 + 256);
        out[b].resize(size_t(w) * (y1 - y0));
        for(int y = y0; y < y1; y++) {
          if(layer) for(int x = 0; x < w; x++) row[x] = layer->get(x, y);
          else flattenRow(doc, y, row.data());
          uint8_t* dst = &out[b][size_t(y - y0) * w];
          for(int x = 0; x < w; x++) dst[x] = mt.coverage(row[x]);
        }
      }
    };
    int n = std::clamp(int(std::thread::hardware_concurrency()), 1, std::min(bands, 16));
    std::vector<std::thread> pool;
    for(int i = 1; i < n; i++) pool.emplace_back(work);
    work();
    for(auto& t : pool) t.join();
    for(int b = 0; b < bands; b++) {
      int y0 = b * 256;
      for(int y = y0; y < std::min(h, y0 + 256); y++) {
        const uint8_t* r = &out[b][size_t(y - y0) * w];
        for(int x = 0; x < w;) {
          int e = x;
          while(e + 1 < w && r[e + 1] == r[x]) e++;
          if(r[x]) m->fillSpan(y, x, e, r[x]);
          x = e + 1;
        }
      }
    }
    return m;
  }

  // Scanline fill with a span stack. Visited pixels have coverage ≥ 1, so the mask doubles as the visited set.
  Matcher mt{ o, target };
  Lightness lum{ s, w, h, {} };
  bool edges = o.edgeStop > 0;
  auto cov = [&](int x, int y) -> uint8_t {
    if(m->get(x, y)) return 0;
    uint8_t v = mt.coverage(s.get(x, y));
    if(v && edges && !(x == sx && y == sy) && lum.edge(x, y) > o.edgeStop) return 0;
    return v;
  };
  std::vector<std::pair<int, int>> stack{ { sx, sy } };
  while(!stack.empty()) {
    auto [x, y] = stack.back();
    stack.pop_back();
    uint8_t v = cov(x, y);
    if(!v) continue;
    m->set(x, y, v);
    int l = x, r = x;
    while(l > 0 && (v = cov(l - 1, y))) m->set(--l, y, v);
    while(r < w - 1 && (v = cov(r + 1, y))) m->set(++r, y, v);
    for(int ny : { y - 1, y + 1 }) {
      if(ny < 0 || ny >= h) continue;
      bool inRun = false;
      for(int i = l; i <= r; i++) {
        bool ok = cov(i, ny) != 0;
        if(ok && !inRun) stack.push_back({ i, ny });
        inRun = ok;
      }
    }
  }
  return m;
}
#pragma endregion

#pragma region [ Transform ]
Point Xform::apply(Point p) const {
  double c = std::cos(angle), s = std::sin(angle);
  double x = (p.x - cx) * sx, y = (p.y - cy) * sy;
  return { cx + x * c - y * s + dx, cy + x * s + y * c + dy };
}

Point Xform::inverse(Point p) const {
  double c = std::cos(angle), s = std::sin(angle);
  double x = p.x - dx - cx, y = p.y - dy - cy;
  return { cx + (x * c + y * s) / sx, cy + (-x * s + y * c) / sy };
}

Lifted lift(const Layer& layer, const Mask& sel) {
  Lifted l;
  l.bounds = sel.bounds();
  if(l.bounds.empty()) return l;
  l.px.resize(size_t(l.bounds.width()) * l.bounds.height());
  size_t i = 0;
  for(int y = l.bounds.y0; y <= l.bounds.y1; y++) {
    for(int x = l.bounds.x0; x <= l.bounds.x1; x++, i++) {
      uint32_t c = layer.get(x, y), cov = sel.get(x, y);
      uint32_t a = (c >> 24) * cov / 255;
      l.px[i] = a ? (c & 0x00FFFFFF) | (a << 24) : 0;
    }
  }
  return l;
}

// The transformed pixel-edge rect of the source bounds, as an integer box
static IRect transformedBox(const IRect& b, const Xform& xf, int w, int h) {
  Point c[4] = { xf.apply({ double(b.x0), double(b.y0) }), xf.apply({ double(b.x1 + 1), double(b.y0) }),
                 xf.apply({ double(b.x0), double(b.y1 + 1) }), xf.apply({ double(b.x1 + 1), double(b.y1 + 1) }) };
  double minX = c[0].x, maxX = c[0].x, minY = c[0].y, maxY = c[0].y;
  for(auto& p : c) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
  return { std::max(0, int(std::floor(minX)) - 1), std::max(0, int(std::floor(minY)) - 1),
           std::min(w - 1, int(std::ceil(maxX))), std::min(h - 1, int(std::ceil(maxY))) };
}

// Samples a w×h grid at a continuous position; nearest when not rotated or scaled, bilinear (premultiplied) otherwise
template<class Get>
static uint32_t sampleRgba(Get get, int w, int h, Point p, bool bilinear) {
  if(!bilinear) {
    int x = int(std::floor(p.x)), y = int(std::floor(p.y));
    return x >= 0 && y >= 0 && x < w && y < h ? get(x, y) : 0;
  }
  double fx = p.x - 0.5, fy = p.y - 0.5;
  int x = int(std::floor(fx)), y = int(std::floor(fy));
  double tx = fx - x, ty = fy - y;
  double acc[4] = { 0, 0, 0, 0 };
  for(int k = 0; k < 4; k++) {
    int sx = x + (k & 1), sy = y + (k >> 1);
    if(sx < 0 || sy < 0 || sx >= w || sy >= h) continue;
    double wgt = ((k & 1) ? tx : 1 - tx) * ((k >> 1) ? ty : 1 - ty);
    uint32_t c = get(sx, sy);
    double a = (c >> 24) / 255.0 * wgt;
    acc[0] += (c & 0xFF) * a; acc[1] += ((c >> 8) & 0xFF) * a; acc[2] += ((c >> 16) & 0xFF) * a; acc[3] += a;
  }
  if(acc[3] <= 0.002) return 0;
  auto ch = [&](int i) { return uint32_t(std::clamp(acc[i] / acc[3] + 0.5, 0.0, 255.0)); };
  return ch(0) | (ch(1) << 8) | (ch(2) << 16) | (uint32_t(std::clamp(acc[3] * 255 + 0.5, 0.0, 255.0)) << 24);
}

void stampLifted(Layer& layer, TileRecorder& rec, const Mask& sel, const Lifted& lifted, const Xform& xf, bool copy) {
  const IRect& b = lifted.bounds;
  if(b.empty()) return;
  if(!copy) {
    for(int y = b.y0; y <= b.y1; y++)
      for(int x = b.x0; x <= b.x1; x++)
        if(int cov = sel.get(x, y)) {
          rec.touchPixel(x, y);
          layer.set(x, y, applyInk(EInk::Erase, layer.get(x, y), 0, cov));
        }
  }
  int lw = b.width(), lh = b.height();
  auto get = [&](int x, int y) { return lifted.px[size_t(y) * lw + x]; };
  // Mirroring (a -1 scale) still maps pixel to pixel
  bool bilinear = xf.angle != 0 || std::abs(xf.sx) != 1 || std::abs(xf.sy) != 1;
  IRect d = transformedBox(b, xf, layer.width(), layer.height());
  for(int y = d.y0; y <= d.y1; y++) {
    for(int x = d.x0; x <= d.x1; x++) {
      Point s = xf.inverse({ x + 0.5, y + 0.5 });
      uint32_t c = sampleRgba(get, lw, lh, { s.x - b.x0, s.y - b.y0 }, bilinear);
      if(!(c >> 24)) continue;
      rec.touchPixel(x, y);
      layer.set(x, y, blendOver(layer.get(x, y), c));
    }
  }
}

std::unique_ptr<Mask> transformMask(const Mask& mask, const Xform& xf) {
  auto out = std::make_unique<Mask>(mask.width(), mask.height());
  IRect b = mask.bounds();
  if(b.empty()) return out;
  if(xf.angle == 0 && !xf.scaled() && std::floor(xf.dx) == xf.dx && std::floor(xf.dy) == xf.dy) {
    // Whole-pixel move: copy rows
    std::vector<uint8_t> row(b.width());
    for(int y = b.y0; y <= b.y1; y++) {
      mask.readRow(y, b.x0, b.x1, row.data());
      for(int i = 0; i < b.width(); i++)
        if(row[i]) out->set(b.x0 + i + int(xf.dx), y + int(xf.dy), row[i]);
    }
    return out;
  }
  IRect d = transformedBox(b, xf, mask.width(), mask.height());
  for(int y = d.y0; y <= d.y1; y++) {
    for(int x = d.x0; x <= d.x1; x++) {
      Point s = xf.inverse({ x + 0.5, y + 0.5 });
      // Unrotated (scaled): nearest, so the edges stay on whole pixels
      if(xf.angle == 0) {
        if(uint8_t v = mask.get(int(std::floor(s.x)), int(std::floor(s.y)))) out->set(x, y, v);
        continue;
      }
      double fx = s.x - 0.5, fy = s.y - 0.5;
      int ix = int(std::floor(fx)), iy = int(std::floor(fy));
      double tx = fx - ix, ty = fy - iy;
      double v = mask.get(ix, iy) * (1 - tx) * (1 - ty) + mask.get(ix + 1, iy) * tx * (1 - ty)
               + mask.get(ix, iy + 1) * (1 - tx) * ty + mask.get(ix + 1, iy + 1) * tx * ty;
      if(v >= 0.5) out->set(x, y, uint8_t(std::min(255.0, v + 0.5)));
    }
  }
  return out;
}
#pragma endregion

}
