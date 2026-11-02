#include "core/Vector.h"
#include "core/Document.h"
#include "core/Layer.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>

namespace david {

static constexpr double PI = 3.14159265358979323846;
// Cubic handle length for a quarter circle
static constexpr double KAPPA = 0.5522847498;

#pragma region [ Geometry ]
Affine Affine::operator*(const Affine& o) const {
  return { a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d, a * o.e + c * o.f + e, b * o.e + d * o.f + f };
}

Affine Affine::inverse() const {
  double det = a * d - b * c;
  if(std::abs(det) < 1e-12) return {};
  double ia = d / det, ib = -b / det, ic = -c / det, id = a / det;
  return { ia, ib, ic, id, -(ia * e + ic * f), -(ib * e + id * f) };
}

double Affine::scale() const { return std::sqrt(std::abs(a * d - b * c)); }

Affine Affine::rotation(double r) {
  double cs = std::cos(r), sn = std::sin(r);
  return { cs, sn, -sn, cs, 0, 0 };
}

void DRect::add(Point p) {
  if(empty()) { x0 = x1 = p.x; y0 = y1 = p.y; return; }
  x0 = std::min(x0, p.x); y0 = std::min(y0, p.y);
  x1 = std::max(x1, p.x); y1 = std::max(y1, p.y);
}
#pragma endregion

#pragma region [ Objects ]
static std::atomic<uint64_t> s_nextObjectId = 1;

uint64_t newObjectId() { return s_nextObjectId++; }

void renumber(VecObject& o) {
  o.id = newObjectId();
  for(auto& c : o.children) renumber(c);
}

void VecTree::touch() {
  static std::atomic<uint64_t> s_revision = 1;
  revision = s_revision++;
}

static Subpath polygonPath(const std::vector<Point>& pts) {
  Subpath s;
  s.closed = true;
  for(auto& p : pts) s.nodes.push_back(PathNode::corner(p));
  return s;
}

std::vector<Subpath> outline(const VecObject& o) {
  double l = std::min(o.x, o.x + o.w), t = std::min(o.y, o.y + o.h), r = std::max(o.x, o.x + o.w), b = std::max(o.y, o.y + o.h);
  double rx = (r - l) / 2, ry = (b - t) / 2, cx = l + rx, cy = t + ry;
  switch(o.kind) {
    case EVecKind::Path: return o.paths;
    case EVecKind::Rect: {
      double rr = std::clamp(o.radius, 0.0, std::min(rx, ry));
      if(rr <= 0) return { polygonPath({ { l, t }, { r, t }, { r, b }, { l, b } }) };
      // Each corner is a quarter circle: straight sides between the arcs
      double k = rr * (1 - KAPPA);
      Subpath s;
      s.closed = true;
      auto node = [&](Point p, Point in, Point out) { s.nodes.push_back({ p, in, out, ENodeType::Smooth }); };
      node({ l + rr, t }, { l + k, t }, { l + rr, t });
      node({ r - rr, t }, { r - rr, t }, { r - k, t });
      node({ r, t + rr }, { r, t + k }, { r, t + rr });
      node({ r, b - rr }, { r, b - rr }, { r, b - k });
      node({ r - rr, b }, { r - k, b }, { r - rr, b });
      node({ l + rr, b }, { l + rr, b }, { l + k, b });
      node({ l, b - rr }, { l, b - k }, { l, b - rr });
      node({ l, t + rr }, { l, t + rr }, { l, t + k });
      return { s };
    }
    case EVecKind::Ellipse: {
      Subpath s;
      s.closed = true;
      double kx = rx * KAPPA, ky = ry * KAPPA;
      s.nodes.push_back({ { cx, t }, { cx - kx, t }, { cx + kx, t }, ENodeType::Symmetric });
      s.nodes.push_back({ { r, cy }, { r, cy - ky }, { r, cy + ky }, ENodeType::Symmetric });
      s.nodes.push_back({ { cx, b }, { cx + kx, b }, { cx - kx, b }, ENodeType::Symmetric });
      s.nodes.push_back({ { l, cy }, { l, cy + ky }, { l, cy - ky }, ENodeType::Symmetric });
      return { s };
    }
    case EVecKind::Polygon: {
      // Regular polygon (or star) in the box's ellipse, first corner up
      int n = std::clamp(o.sides, 3, 64);
      bool star = o.inner < 0.999;
      std::vector<Point> pts;
      for(int i = 0; i < n * (star ? 2 : 1); i++) {
        double a = -PI / 2 + PI * i / (star ? n : n / 2.0);
        double k = star && (i & 1) ? o.inner : 1;
        pts.push_back({ cx + rx * k * std::cos(a), cy + ry * k * std::sin(a) });
      }
      return { polygonPath(pts) };
    }
    case EVecKind::Text: return textOutline(o);
    default: return {};
  }
}

std::vector<Point> flatten(const Subpath& s, double tol) {
  std::vector<Point> out;
  size_t n = s.nodes.size();
  if(!n) return out;
  out.push_back(s.nodes[0].p);
  size_t segs = s.closed ? n : n - 1;
  for(size_t i = 0; i < segs; i++) {
    const PathNode &p0 = s.nodes[i], &p1 = s.nodes[(i + 1) % n];
    bool straight = p0.out.x == p0.p.x && p0.out.y == p0.p.y && p1.in.x == p1.p.x && p1.in.y == p1.p.y;
    if(straight) {
      out.push_back(p1.p);
    } else {
      // Enough steps that each chord stays within the tolerance of the curve
      double len = std::hypot(p0.out.x - p0.p.x, p0.out.y - p0.p.y) + std::hypot(p1.in.x - p0.out.x, p1.in.y - p0.out.y) + std::hypot(p1.p.x - p1.in.x, p1.p.y - p1.in.y);
      int steps = std::clamp(int(std::sqrt(len / std::max(tol, 0.01)) * 1.5), 2, 512);
      for(int k = 1; k <= steps; k++) {
        double t = double(k) / steps, u = 1 - t;
        double w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
        out.push_back({ w0 * p0.p.x + w1 * p0.out.x + w2 * p1.in.x + w3 * p1.p.x, w0 * p0.p.y + w1 * p0.out.y + w2 * p1.in.y + w3 * p1.p.y });
      }
    }
  }
  // A closed path ends where it started: that point isn't repeated
  if(s.closed && out.size() > 1 && std::abs(out.back().x - out[0].x) < 1e-9 && std::abs(out.back().y - out[0].y) < 1e-9) out.pop_back();
  return out;
}

DRect bounds(const VecObject& o, bool world, bool withStroke) {
  DRect r;
  if(o.kind == EVecKind::Group) {
    for(auto& c : o.children) {
      if(!world) { r.add(bounds(c, false, withStroke)); continue; }
      // Children are placed by the group's transform too
      VecObject placed = c;
      placed.xf = o.xf * c.xf;
      r.add(bounds(placed, true, withStroke));
    }
    return r;
  }
  for(auto& s : outline(o))
    for(auto& p : flatten(s, 0.5)) r.add(world ? o.xf.apply(p) : p);
  if(withStroke && o.stroke.paint.visible() && !r.empty()) {
    double hw = o.stroke.width / 2 * (world ? o.xf.scale() : 1);
    if(o.stroke.align == EStrokeAlign::Outside) hw *= 2;
    else if(o.stroke.align == EStrokeAlign::Inside) hw = 0;
    if(o.stroke.join == EJoin::Miter) hw *= std::max(1.0, o.stroke.miter / 2);
    r.x0 -= hw; r.y0 -= hw; r.x1 += hw; r.y1 += hw;
  }
  return r;
}

DRect bounds(const std::vector<VecObject>& objects, bool withStroke) {
  DRect r;
  for(auto& o : objects) r.add(bounds(o, true, withStroke));
  return r;
}

template<class V, class O> static O* findIn(V& objects, uint64_t id) {
  for(auto& o : objects) {
    if(o.id == id) return &o;
    if(auto* c = findIn<V, O>(o.children, id)) return c;
  }
  return nullptr;
}

VecObject* findObject(std::vector<VecObject>& objects, uint64_t id) { return findIn<std::vector<VecObject>, VecObject>(objects, id); }
const VecObject* findObject(const std::vector<VecObject>& objects, uint64_t id) { return findIn<const std::vector<VecObject>, const VecObject>(objects, id); }

static double segDistance(Point p, Point a, Point b) {
  double dx = b.x - a.x, dy = b.y - a.y, len = dx * dx + dy * dy;
  double t = len > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len, 0.0, 1.0) : 0;
  return std::hypot(p.x - a.x - t * dx, p.y - a.y - t * dy);
}

static bool hits(const VecObject& o, const Affine& parent, Point p, double tol) {
  if(!o.visible) return false;
  Affine xf = parent * o.xf;
  if(o.kind == EVecKind::Group) {
    for(auto& c : o.children) if(hits(c, xf, p, tol)) return true;
    return false;
  }
  Point q = xf.inverse().apply(p);
  double ltol = tol / std::max(xf.scale(), 1e-6);
  double sw = o.stroke.paint.visible() ? o.stroke.width / 2 : 0;
  int winding = 0;
  bool near = false;
  for(auto& s : outline(o)) {
    auto pts = flatten(s, 0.5);
    size_t n = pts.size();
    for(size_t i = 0; i + 1 < n || (s.closed && i < n); i++) {
      Point a = pts[i], b = pts[(i + 1) % n];
      if(segDistance(q, a, b) <= ltol + sw) near = true;
      // Nonzero winding; open paths count as closed for their fill, as SVG does
      if(a.y <= q.y) { if(b.y > q.y && (b.x - a.x) * (q.y - a.y) - (q.x - a.x) * (b.y - a.y) > 0) winding++; }
      else if(b.y <= q.y && (b.x - a.x) * (q.y - a.y) - (q.x - a.x) * (b.y - a.y) < 0) winding--;
    }
    if(!s.closed && n > 2) {
      Point a = pts[n - 1], b = pts[0];
      if(a.y <= q.y) { if(b.y > q.y && (b.x - a.x) * (q.y - a.y) - (q.x - a.x) * (b.y - a.y) > 0) winding++; }
      else if(b.y <= q.y && (b.x - a.x) * (q.y - a.y) - (q.x - a.x) * (b.y - a.y) < 0) winding--;
    }
  }
  // Text is picked by its whole box, so the gaps between letters count
  if(o.kind == EVecKind::Text) {
    DRect r = bounds(o, false);
    return !r.empty() && q.x >= r.x0 - ltol && q.x <= r.x1 + ltol && q.y >= r.y0 - ltol && q.y <= r.y1 + ltol;
  }
  return near || (winding != 0 && o.fill.visible());
}

VecObject* hitTest(std::vector<VecObject>& objects, Point p, double tol) {
  for(int i = int(objects.size()) - 1; i >= 0; i--)
    if(!objects[i].locked && hits(objects[i], {}, p, tol)) return &objects[i];
  return nullptr;
}

void toPath(VecObject& o) {
  if(o.kind == EVecKind::Path || o.kind == EVecKind::Group) return;
  o.paths = outline(o);
  o.kind = EVecKind::Path;
}
#pragma endregion

#pragma region [ Layer trees ]
VecTree& vectorTree(Layer& layer, int frame) {
  Cel& c = layer.ensureCel(frame);
  if(!c.vec) c.vec = std::make_shared<VecTree>();
  return *c.vec;
}

const VecTree* vectorTreeOf(const Layer& layer, int frame) {
  if(frame < 0 || frame >= layer.frames()) return nullptr;
  const auto& c = layer.cel(frame);
  return c ? c->vec.get() : nullptr;
}
#pragma endregion

#pragma region [ History ]
static void putTree(Document& doc, uint64_t layerId, int frame, const VecTree& t) {
  Layer* l = doc.findLayer(layerId);
  if(!l) return;
  VecTree& tree = vectorTree(*l, frame);
  tree = t;
  tree.touch();
  rasterizeVectorCel(*l, frame);
  doc.onChanged();
}

void VectorEdit::undo(Document& doc) { putTree(doc, layerId, celFrame, before); }
void VectorEdit::redo(Document& doc) { putTree(doc, layerId, celFrame, after); }

size_t VectorEdit::bytes() const {
  // A rough size: what the objects hold
  std::function<size_t(const std::vector<VecObject>&)> size = [&](const std::vector<VecObject>& v) {
    size_t n = 0;
    for(auto& o : v) {
      n += sizeof(VecObject) + o.text.size();
      for(auto& s : o.paths) n += s.nodes.size() * sizeof(PathNode);
      n += size(o.children);
    }
    return n;
  };
  return size(before.objects) + size(after.objects) + sizeof(*this);
}

VectorChange::VectorChange(Document& doc) : _doc(doc), _layer(&doc.active()), _frame(doc.active().frame()) {
  _tree = &vectorTree(*_layer, _frame);
  _before = *_tree;
}

bool VectorChange::commit(std::string name, const char* icon) {
  if(_tree->revision == _before.revision) return false;
  auto edit = std::make_unique<VectorEdit>();
  edit->name = std::move(name);
  edit->icon = icon;
  edit->layerId = _layer->id();
  edit->celFrame = _frame;
  edit->before = _before;
  edit->after = *_tree;
  rasterizeVectorCel(*_layer, _frame);
  _doc.history.push(_doc, std::move(edit));
  _before = *_tree;
  _doc.onChanged();
  return true;
}

void VectorChange::cancel() {
  *_tree = _before;
  _tree->touch();
  rasterizeVectorCel(*_layer, _frame);
  _before = *_tree;
  _doc.onChanged();
}

void vectorChanged(Document& doc, Layer& layer, bool pixels) {
  vectorTree(layer, layer.frame()).touch();
  if(pixels) rasterizeVectorCel(layer, layer.frame());
  doc.onChanged();
}
#pragma endregion

}
