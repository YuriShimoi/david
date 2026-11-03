#include "core/VectorOps.h"
#include <clipper2/clipper.h>
#include <algorithm>
#include <cmath>

namespace david {

static Point operator+(Point a, Point b) { return { a.x + b.x, a.y + b.y }; }
static Point operator-(Point a, Point b) { return { a.x - b.x, a.y - b.y }; }
static Point operator*(Point a, double k) { return { a.x * k, a.y * k }; }
static double dot(Point a, Point b) { return a.x * b.x + a.y * b.y; }
static double dist(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }
static Point unit(Point a) {
  double l = std::hypot(a.x, a.y);
  return l > 1e-12 ? a * (1 / l) : Point{ 0, 0 };
}

static bool picked(const std::vector<uint64_t>& sel, uint64_t id) { return std::find(sel.begin(), sel.end(), id) != sel.end(); }

#pragma region [ Object edits ]
bool removeObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel) {
  size_t n = objs.size();
  std::erase_if(objs, [&](const VecObject& o) { return picked(sel, o.id); });
  sel.clear();
  return objs.size() != n;
}

bool duplicateObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel) {
  std::vector<uint64_t> copies;
  size_t n = objs.size();
  for(size_t i = 0; i < n; i++) {
    if(!picked(sel, objs[i].id)) continue;
    VecObject c = objs[i];
    renumber(c);
    c.xf = Affine::translate(10, 10) * c.xf;
    copies.push_back(c.id);
    objs.push_back(std::move(c));
  }
  if(copies.empty()) return false;
  sel = copies;
  return true;
}

bool orderObjects(std::vector<VecObject>& objs, const std::vector<uint64_t>& sel, int dir) {
  bool moved = false;
  auto on = [&](size_t i) { return picked(sel, objs[i].id); };
  if(dir > 0) { for(int i = int(objs.size()) - 2; i >= 0; i--) if(on(i) && !on(i + 1)) { std::swap(objs[i], objs[i + 1]); moved = true; } }
  else { for(size_t i = 1; i < objs.size(); i++) if(on(i) && !on(i - 1)) { std::swap(objs[i], objs[i - 1]); moved = true; } }
  return moved;
}

bool groupObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel) {
  if(sel.size() < 2) return false;
  VecObject g;
  g.id = newObjectId();
  g.kind = EVecKind::Group;
  g.name = "Group";
  size_t at = objs.size();
  for(size_t i = 0; i < objs.size();) {
    if(picked(sel, objs[i].id)) {
      at = std::min(at, i);
      g.children.push_back(std::move(objs[i]));
      objs.erase(objs.begin() + i);
    } else i++;
  }
  if(g.children.size() < 2) return false;
  at = std::min(at, objs.size());
  objs.insert(objs.begin() + at, std::move(g));
  sel = { objs[at].id };
  return true;
}

bool ungroupObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel) {
  std::vector<uint64_t> out;
  for(size_t i = 0; i < objs.size();) {
    if(objs[i].kind == EVecKind::Group && picked(sel, objs[i].id)) {
      VecObject g = std::move(objs[i]);
      objs.erase(objs.begin() + i);
      for(auto& c : g.children) {
        c.xf = g.xf * c.xf;
        c.opacity *= g.opacity;
        out.push_back(c.id);
        objs.insert(objs.begin() + i++, std::move(c));
      }
    } else i++;
  }
  if(out.empty()) return false;
  sel = out;
  return true;
}

bool transformObjects(std::vector<VecObject>& objs, const std::vector<uint64_t>& sel, const Affine& m) {
  DRect box;
  for(auto& o : objs) if(picked(sel, o.id)) box.add(bounds(o, true));
  if(box.empty()) return false;
  Affine around = Affine::around({ (box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2 }, m);
  for(auto& o : objs) if(picked(sel, o.id)) o.xf = around * o.xf;
  return true;
}

bool objectsToPath(std::vector<VecObject>& objs, const std::vector<uint64_t>& sel) {
  bool any = false;
  for(auto& o : objs) {
    if(!picked(sel, o.id) || o.kind == EVecKind::Path || o.kind == EVecKind::Group) continue;
    // Text too: its glyph outlines become the path
    o.paths = outline(o);
    o.kind = EVecKind::Path;
    o.text.clear();
    any = true;
  }
  return any;
}
#pragma endregion

#pragma region [ Boolean operations ]
namespace {
using namespace Clipper2Lib;

// The fill area of an object (a group: its children's) as polygons in image coordinates
void fillArea(const VecObject& o, const Affine& parent, PathsD& out) {
  if(!o.visible) return;
  Affine m = parent * o.xf;
  if(o.kind == EVecKind::Group) { for(auto& c : o.children) fillArea(c, m, out); return; }
  double tol = 0.02 / std::max(m.scale(), 1e-6);
  for(auto& s : outline(o)) {
    std::vector<Point> pts = flatten(s, tol);
    if(pts.size() < 3) continue;
    PathD path;
    path.reserve(pts.size());
    for(Point p : pts) { Point w = m.apply(p); path.push_back({ w.x, w.y }); }
    out.push_back(std::move(path));
  }
}

// The object whose look the result takes: the bottom one, or a group's bottom child, with its full transform
const VecObject& styleSource(const VecObject& o, Affine& m) {
  m = m * o.xf;
  if(o.kind == EVecKind::Group && !o.children.empty()) return styleSource(o.children[0], m);
  return o;
}

// A paint's gradient ends from the source object's coordinates into image coordinates
VecPaint worldPaint(VecPaint p, const Affine& m) {
  p.a = m.apply(p.a);
  p.b = m.apply(p.b);
  return p;
}
}

bool booleanObjects(std::vector<VecObject>& objs, std::vector<uint64_t>& sel, EBoolOp op) {
  std::vector<size_t> at;
  for(size_t i = 0; i < objs.size(); i++) if(picked(sel, objs[i].id)) at.push_back(i);
  if(at.size() < 2) return false;
  // Each object first on its own (nonzero), so overlaps inside one object or opposite windings don't cancel
  PathsD acc, rest;
  for(size_t k = 0; k < at.size(); k++) {
    PathsD area;
    fillArea(objs[at[k]], {}, area);
    area = Union(area, FillRule::NonZero, 4);
    if(k == 0) { acc = std::move(area); continue; }
    switch(op) {
      case EBoolOp::Union: acc = Union(acc, area, FillRule::NonZero, 4); break;
      case EBoolOp::Subtract: rest = Union(rest, area, FillRule::NonZero, 4); break;
      case EBoolOp::Intersect: acc = Intersect(acc, area, FillRule::NonZero, 4); break;
      case EBoolOp::Exclude: acc = Xor(acc, area, FillRule::NonZero, 4); break;
    }
  }
  if(op == EBoolOp::Subtract) acc = Difference(acc, rest, FillRule::NonZero, 4);
  acc = SimplifyPaths(acc, 0.01);
  std::erase_if(acc, [](const PathD& p) { return p.size() < 3 || std::abs(Area(p)) < 1e-4; });
  if(acc.empty()) return false;

  const VecObject& bottom = objs[at[0]];
  Affine m;
  const VecObject& src = styleSource(bottom, m);
  VecObject r;
  r.id = newObjectId();
  r.kind = EVecKind::Path;
  static const char* names[] = { "Union", "Subtract", "Intersect", "Exclude" };
  r.name = bottom.name.empty() || bottom.kind == EVecKind::Group ? names[int(op)] : bottom.name;
  r.opacity = bottom.opacity;
  r.fill = worldPaint(src.fill, m);
  r.stroke = src.stroke;
  r.stroke.paint = worldPaint(src.stroke.paint, m);
  r.stroke.width *= m.scale();
  for(auto& poly : acc) {
    std::vector<Point> pts;
    pts.reserve(poly.size());
    for(auto& q : poly) pts.push_back({ q.x, q.y });
    r.paths.push_back(fitCurve(std::move(pts), true, 0.05));
  }
  // The result takes the bottom object's place
  for(size_t k = at.size(); k-- > 1;) objs.erase(objs.begin() + at[k]);
  objs[at[0]] = std::move(r);
  sel = { objs[at[0]].id };
  return true;
}
#pragma endregion

#pragma region [ Curve fitting ]
namespace {
struct Bez { Point p0, p1, p2, p3; };

Point bezAt(const Bez& b, double t) {
  double u = 1 - t;
  return b.p0 * (u * u * u) + b.p1 * (3 * u * u * t) + b.p2 * (3 * u * t * t) + b.p3 * (t * t * t);
}

// One Newton-Raphson step for the curve parameter closest to p
double newton(const Bez& b, Point p, double t) {
  Point d1[3] = { (b.p1 - b.p0) * 3, (b.p2 - b.p1) * 3, (b.p3 - b.p2) * 3 };
  Point d2[2] = { (d1[1] - d1[0]) * 2, (d1[2] - d1[1]) * 2 };
  double u = 1 - t;
  Point q = bezAt(b, t) - p;
  Point q1 = d1[0] * (u * u) + d1[1] * (2 * u * t) + d1[2] * (t * t);
  Point q2 = d2[0] * u + d2[1] * t;
  double den = dot(q1, q1) + dot(q, q2);
  return std::abs(den) < 1e-12 ? t : t - dot(q, q1) / den;
}

// Least-squares handle lengths along the end tangents for the points at parameters u
Bez generate(const std::vector<Point>& d, size_t first, size_t last, const std::vector<double>& u, Point t1, Point t2) {
  double c00 = 0, c01 = 0, c11 = 0, x0 = 0, x1 = 0;
  Point p0 = d[first], p3 = d[last];
  for(size_t i = 0; i < u.size(); i++) {
    double t = u[i], s = 1 - t;
    double b0 = s * s * s, b1 = 3 * s * s * t, b2 = 3 * s * t * t, b3 = t * t * t;
    Point a0 = t1 * b1, a1 = t2 * b2;
    c00 += dot(a0, a0); c01 += dot(a0, a1); c11 += dot(a1, a1);
    Point tmp = d[first + i] - (p0 * (b0 + b1) + p3 * (b2 + b3));
    x0 += dot(a0, tmp); x1 += dot(a1, tmp);
  }
  double det = c00 * c11 - c01 * c01;
  double al = std::abs(det) < 1e-12 ? 0 : (x0 * c11 - x1 * c01) / det;
  double ar = std::abs(det) < 1e-12 ? 0 : (c00 * x1 - c01 * x0) / det;
  double seg = dist(p0, p3), eps = 1e-6 * seg;
  // A degenerate fit falls back to a third of the chord (Wu/Barsky)
  if(al < eps || ar < eps) al = ar = seg / 3;
  return { p0, p0 + t1 * al, p3 + t2 * ar, p3 };
}

double maxError(const std::vector<Point>& d, size_t first, const Bez& b, const std::vector<double>& u, size_t& split) {
  double worst = 0;
  split = first + u.size() / 2;
  for(size_t i = 1; i + 1 < u.size(); i++) {
    double e = dist(bezAt(b, u[i]), d[first + i]);
    if(e >= worst) { worst = e; split = first + i; }
  }
  return worst;
}

void fitCubic(const std::vector<Point>& d, size_t first, size_t last, Point t1, Point t2, double error, int depth, std::vector<Bez>& out) {
  if(last - first == 1 || depth > 24) {
    double k = dist(d[first], d[last]) / 3;
    out.push_back({ d[first], d[first] + t1 * k, d[last] + t2 * k, d[last] });
    return;
  }
  // Chord-length parameters
  std::vector<double> u(last - first + 1, 0);
  for(size_t i = first + 1; i <= last; i++) u[i - first] = u[i - first - 1] + dist(d[i], d[i - 1]);
  if(u.back() > 0) for(double& v : u) v /= u.back();
  Bez b = generate(d, first, last, u, t1, t2);
  size_t split;
  double err = maxError(d, first, b, u, split);
  if(err < error) { out.push_back(b); return; }
  // Close: refine the parameters a few times before splitting
  if(err < error * 4) {
    for(int it = 0; it < 12; it++) {
      for(size_t i = 0; i < u.size(); i++) u[i] = std::clamp(newton(b, d[first + i], u[i]), 0.0, 1.0);
      b = generate(d, first, last, u, t1, t2);
      err = maxError(d, first, b, u, split);
      if(err < error) { out.push_back(b); return; }
    }
  }
  Point center = unit(d[split - 1] - d[split + 1]);
  if(center.x == 0 && center.y == 0) center = unit(d[split - 1] - d[split]);
  fitCubic(d, first, split, t1, center, error, depth + 1, out);
  fitCubic(d, split, last, center * -1, t2, error, depth + 1, out);
}

// Every point stays within error of the chord a-b
bool straight(const std::vector<Point>& d, size_t a, size_t b, double error) {
  // A loop back to its start is never one straight segment
  if(dist(d[a], d[b]) <= error) return false;
  Point dir = unit(d[b] - d[a]);
  for(size_t i = a + 1; i < b; i++) {
    Point v = d[i] - d[a];
    double along = dot(v, dir);
    if(along < -error || along > dist(d[a], d[b]) + error) return false;
    if(std::abs(v.x * dir.y - v.y * dir.x) > error) return false;
  }
  return true;
}

// The direction leaving point a toward b, from the first point at least span away (smooths noisy input)
Point tangent(const std::vector<Point>& d, size_t a, size_t b, double span) {
  int step = b > a ? 1 : -1;
  size_t i = a;
  while(i != b) {
    i += step;
    if(dist(d[i], d[a]) >= span) break;
  }
  return unit(d[i] - d[a]);
}

// Points where the line turns by more than minTurn, measured between points span away on each side; the
// sharpest point of each bend only
std::vector<size_t> findCorners(const std::vector<Point>& p, bool closed, double span, double minTurn) {
  size_t n = p.size();
  std::vector<double> turn(n, 0);
  std::vector<size_t> reachBack(n, 0), reachAhead(n, 0);
  auto walk = [&](size_t i, int dir, size_t& steps) {
    size_t j = i;
    steps = 0;
    while(steps + 1 < n) {
      if(!closed && ((dir < 0 && j == 0) || (dir > 0 && j == n - 1))) break;
      j = (j + n + dir) % n;
      steps++;
      if(dist(p[j], p[i]) >= span) break;
    }
    return j;
  };
  for(size_t i = 0; i < n; i++) {
    if(!closed && (i == 0 || i == n - 1)) continue;
    size_t j = walk(i, -1, reachBack[i]), k = walk(i, 1, reachAhead[i]);
    Point a = unit(p[i] - p[j]), b = unit(p[k] - p[i]);
    turn[i] = std::acos(std::clamp(dot(a, b), -1.0, 1.0));
  }
  std::vector<size_t> out;
  for(size_t i = 0; i < n; i++) {
    if(turn[i] < minTurn) continue;
    bool best = true;
    for(int dir : { -1, 1 }) {
      size_t steps = dir < 0 ? reachBack[i] : reachAhead[i];
      for(size_t s = 1, j = i; s <= steps && best; s++) {
        j = (j + n + dir) % n;
        // Only points of the same bend (closer than span) compete
        if(dist(p[j], p[i]) >= span) break;
        // Ties go to the earlier point
        if(turn[j] > turn[i] || (turn[j] == turn[i] && j < i)) best = false;
      }
    }
    if(best) out.push_back(i);
  }
  return out;
}
}

Subpath fitCurve(std::vector<Point> pts, bool closed, double error) {
  Subpath out;
  out.closed = closed;
  // No repeated points (a closed polyline doesn't repeat its start either)
  std::vector<Point> d;
  for(Point q : pts) if(d.empty() || dist(q, d.back()) > 1e-6) d.push_back(q);
  if(closed && d.size() > 1 && dist(d.front(), d.back()) <= 1e-6) d.pop_back();
  if(d.size() < (closed ? 3u : 2u) ) {
    for(Point q : d) out.nodes.push_back(PathNode::corner(q));
    return out;
  }
  double span = error * 3;
  std::vector<size_t> corners = findCorners(d, closed, span, 0.6);
  // Pieces between corners (and the ends); a closed loop starts on a corner and comes back to it
  std::vector<size_t> breaks;
  if(closed) {
    if(!corners.empty()) {
      std::rotate(d.begin(), d.begin() + corners[0], d.end());
      size_t shift = corners[0];
      for(size_t& c : corners) c = (c + d.size() - shift) % d.size();
      std::sort(corners.begin(), corners.end());
    }
    d.push_back(d[0]);
    breaks = corners.empty() ? std::vector<size_t>{ 0 } : corners;
    breaks.push_back(d.size() - 1);
  } else {
    breaks.push_back(0);
    for(size_t c : corners) breaks.push_back(c);
    breaks.push_back(d.size() - 1);
  }

  std::vector<Bez> segs;
  std::vector<bool> cornerAt;  // per segment: it starts on a corner
  for(size_t k = 0; k + 1 < breaks.size(); k++) {
    size_t a = breaks[k], b = breaks[k + 1];
    if(b <= a) continue;
    size_t before = segs.size();
    if(straight(d, a, b, error)) {
      segs.push_back({ d[a], d[a], d[b], d[b] });
    } else if(closed && corners.empty()) {
      // A smooth loop: the seam's tangent runs through its two neighbors
      Point t = unit(d[1] - d[d.size() - 2]);
      fitCubic(d, a, b, t, t * -1, error, 0, segs);
    } else {
      fitCubic(d, a, b, tangent(d, a, b, span), tangent(d, b, a, span), error, 0, segs);
    }
    for(size_t s = before; s < segs.size(); s++) cornerAt.push_back(s == before && !(closed && corners.empty()));
  }

  for(size_t s = 0; s < segs.size(); s++) {
    const Bez& b = segs[s];
    if(s == 0) out.nodes.push_back(PathNode::corner(b.p0));
    PathNode& from = out.nodes.back();
    from.out = b.p1;
    if(!cornerAt[s] && s > 0) from.type = ENodeType::Smooth;
    PathNode to = PathNode::corner(b.p3);
    to.in = b.p2;
    out.nodes.push_back(to);
  }
  if(closed && out.nodes.size() > 1) {
    // The last node is the first one again
    out.nodes.front().in = out.nodes.back().in;
    out.nodes.pop_back();
    if(corners.empty()) out.nodes.front().type = ENodeType::Smooth;
  }
  return out;
}
#pragma endregion

}
