#include "core/Vector.h"
#include "core/Layer.h"
#include <thorvg.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <thread>

namespace david {

#pragma region [ ThorVG scene ]
static void ensureEngine() {
  static std::once_flag once;
  std::call_once(once, [] { tvg::Initializer::init(std::max(1u, std::min(4u, std::thread::hardware_concurrency()))); });
}

static tvg::Matrix matrix(const Affine& m) {
  return { float(m.a), float(m.c), float(m.e), float(m.b), float(m.d), float(m.f), 0, 0, 1 };
}

static void appendPath(tvg::Shape* s, const std::vector<Subpath>& paths) {
  for(auto& sp : paths) {
    size_t n = sp.nodes.size();
    if(!n) continue;
    s->moveTo(float(sp.nodes[0].p.x), float(sp.nodes[0].p.y));
    size_t segs = sp.closed ? n : n - 1;
    for(size_t i = 0; i < segs; i++) {
      const PathNode &a = sp.nodes[i], &b = sp.nodes[(i + 1) % n];
      s->cubicTo(float(a.out.x), float(a.out.y), float(b.in.x), float(b.in.y), float(b.p.x), float(b.p.y));
    }
    if(sp.closed) s->close();
  }
}

// Gradient stops baked from the gradient engine, so OKLab and OKLCH spaces look as in the gradient tool
static std::vector<tvg::Fill::ColorStop> bakedStops(const VecPaint& p) {
  GradientSpec g;
  g.space = p.space;
  g.stops = p.stops;
  if(g.stops.size() < 2) g.stops = { { 0, p.color }, { 1, p.color } };
  std::stable_sort(g.stops.begin(), g.stops.end(), [](const GradStop& a, const GradStop& b) { return a.pos < b.pos; });
  bool plain = p.space == EGradSpace::Srgb;
  std::vector<tvg::Fill::ColorStop> out;
  if(plain) {
    for(auto& s : g.stops) out.push_back({ s.pos, uint8_t(s.color), uint8_t(s.color >> 8), uint8_t(s.color >> 16), uint8_t(s.color >> 24) });
    return out;
  }
  std::vector<float> table = gradientTable(g);
  int n = int(table.size() / 4);
  for(int i = 0; i <= 32; i++) {
    const float* v = &table[size_t(std::min(n - 1, i * (n - 1) / 32)) * 4];
    auto ch = [&](int k) { return uint8_t(std::clamp(std::lround(v[k]), 0L, 255L)); };
    out.push_back({ i / 32.f, ch(0), ch(1), ch(2), ch(3) });
  }
  return out;
}

static tvg::Fill* makeFill(const VecPaint& p) {
  auto stops = bakedStops(p);
  if(p.type == EPaintType::Linear) {
    auto* f = tvg::LinearGradient::gen();
    f->linear(float(p.a.x), float(p.a.y), float(p.b.x), float(p.b.y));
    f->colorStops(stops.data(), uint32_t(stops.size()));
    return f;
  }
  auto* f = tvg::RadialGradient::gen();
  float r = float(std::hypot(p.b.x - p.a.x, p.b.y - p.a.y));
  f->radial(float(p.a.x), float(p.a.y), r, float(p.a.x), float(p.a.y), 0);
  f->colorStops(stops.data(), uint32_t(stops.size()));
  return f;
}

static void setFill(tvg::Shape* s, const VecPaint& p) {
  if(p.type == EPaintType::Solid) s->fill(uint8_t(p.color), uint8_t(p.color >> 8), uint8_t(p.color >> 16), uint8_t(p.color >> 24));
  else if(p.type != EPaintType::None) s->fill(makeFill(p));
}

static void setStroke(tvg::Shape* s, const VecStroke& st, double width) {
  s->strokeWidth(float(width));
  const VecPaint& p = st.paint;
  if(p.type == EPaintType::Solid) s->strokeFill(uint8_t(p.color), uint8_t(p.color >> 8), uint8_t(p.color >> 16), uint8_t(p.color >> 24));
  else s->strokeFill(makeFill(p));
  s->strokeCap(st.cap == ECap::Round ? tvg::StrokeCap::Round : st.cap == ECap::Square ? tvg::StrokeCap::Square : tvg::StrokeCap::Butt);
  s->strokeJoin(st.join == EJoin::Round ? tvg::StrokeJoin::Round : st.join == EJoin::Bevel ? tvg::StrokeJoin::Bevel : tvg::StrokeJoin::Miter);
  s->strokeMiterlimit(float(std::max(1.0, st.miter)));
  if(!st.dash.empty()) {
    // Dashes are kept in stroke widths, so they scale with the line
    std::vector<float> d;
    for(double v : st.dash) d.push_back(float(std::max(0.01, v * st.width)));
    if(d.size() % 2) d.insert(d.end(), d.begin(), d.end());
    s->strokeDash(d.data(), uint32_t(d.size()));
  }
}

static tvg::Paint* build(const VecObject& o, const Affine& parent) {
  if(!o.visible || o.opacity <= 0) return nullptr;
  Affine m = parent * o.xf;
  uint8_t alpha = uint8_t(std::clamp(std::lround(o.opacity * 255), 0L, 255L));
  if(o.kind == EVecKind::Group) {
    auto* scene = tvg::Scene::gen();
    for(auto& c : o.children)
      if(tvg::Paint* p = build(c, m)) scene->add(p);
    scene->opacity(alpha);
    return scene;
  }
  std::vector<Subpath> paths = outline(o);
  if(paths.empty()) return nullptr;
  auto shapeOf = [&] {
    auto* s = tvg::Shape::gen();
    appendPath(s, paths);
    s->transform(matrix(m));
    return s;
  };
  bool stroked = o.stroke.paint.visible() && o.stroke.width > 0;
  auto* body = shapeOf();
  setFill(body, o.fill);
  if(!stroked) { body->opacity(alpha); return body; }
  if(o.stroke.align == EStrokeAlign::Center) {
    setStroke(body, o.stroke, o.stroke.width);
    body->opacity(alpha);
    return body;
  }
  // Inside and outside strokes: a stroke twice as wide, clipped to the shape or masked by it
  auto* scene = tvg::Scene::gen();
  scene->add(body);
  auto* line = shapeOf();
  setStroke(line, o.stroke, o.stroke.width * 2);
  if(o.stroke.align == EStrokeAlign::Inside) {
    auto* clip = shapeOf();
    clip->fill(255, 255, 255, 255);
    line->clip(clip);
  } else {
    auto* mask = shapeOf();
    mask->fill(255, 255, 255, 255);
    line->mask(mask, tvg::MaskMethod::InvAlpha);
  }
  scene->add(line);
  scene->opacity(alpha);
  return scene;
}
#pragma endregion

void renderVector(const VecTree& tree, const Affine& view, int w, int h, std::vector<uint32_t>& out) {
  out.assign(size_t(std::max(w, 0)) * std::max(h, 0), 0);
  if(w <= 0 || h <= 0 || tree.objects.empty()) return;
  ensureEngine();
  tvg::SwCanvas* canvas = tvg::SwCanvas::gen();
  if(!canvas) return;
  // Straight RGBA in memory order, the same layout as David's pixels
  canvas->target(out.data(), uint32_t(w), uint32_t(w), uint32_t(h), tvg::ColorSpace::ABGR8888S);
  auto* root = tvg::Scene::gen();
  for(auto& o : tree.objects)
    if(tvg::Paint* p = build(o, view)) root->add(p);
  canvas->add(root);
  canvas->draw(true);
  canvas->sync();
  delete canvas;
}

void rasterizeVectorCel(Layer& layer, int frame) {
  const VecTree* tree = vectorTreeOf(layer, frame);
  if(!tree) return;
  int w = layer.width(), h = layer.height();
  std::vector<uint32_t> px;
  renderVector(*tree, {}, w, h, px);
  for(int ty = 0; ty < layer.tilesY(); ty++)
    for(int tx = 0; tx < layer.tilesX(); tx++) {
      int x0 = tx * Tile::SIZE, y0 = ty * Tile::SIZE;
      int cw = std::min(Tile::SIZE, w - x0), ch = std::min(Tile::SIZE, h - y0);
      bool any = false;
      for(int y = 0; y < ch && !any; y++)
        for(int x = 0; x < cw; x++) if(px[size_t(y0 + y) * w + x0 + x]) { any = true; break; }
      if(!any) { layer.putTileAt(frame, tx, ty, nullptr); continue; }
      auto t = std::make_unique<Tile>();
      for(int y = 0; y < ch; y++) std::copy_n(&px[size_t(y0 + y) * w + x0], cw, t->data() + y * Tile::SIZE);
      t->touch();
      layer.putTileAt(frame, tx, ty, std::move(t));
    }
}

}
