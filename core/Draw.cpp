#include "core/Draw.h"
#include <algorithm>
#include <cmath>

namespace david {

static constexpr double PI = 3.14159265358979323846;

#pragma region [ Coverage ]
std::unique_ptr<Mask> fillContours(int w, int h, const std::vector<Polyline>& contours, bool antialias) {
  auto m = std::make_unique<Mask>(w, h);
  double minY = 1e18, maxY = -1e18, minX = 1e18, maxX = -1e18;
  for(auto& c : contours)
    for(auto& p : c) { minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); }
  if(minY > maxY) return m;
  int xa = std::max(0, int(std::floor(minX))), xb = std::min(w - 1, int(std::ceil(maxX)));
  if(xa > xb) return m;
  int subs = antialias ? 16 : 1;
  std::vector<float> acc(xb - xa + 1);
  std::vector<double> xs;
  for(int y = std::max(0, int(std::floor(minY))); y <= std::min(h - 1, int(std::ceil(maxY))); y++) {
    std::fill(acc.begin(), acc.end(), 0.f);
    bool any = false;
    for(int s = 0; s < subs; s++) {
      double yc = y + (s + 0.5) / subs;
      xs.clear();
      for(auto& c : contours)
        for(size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
          const Point &a = c[i], &b = c[j];
          if((a.y > yc) != (b.y > yc)) xs.push_back(a.x + (yc - a.y) * (b.x - a.x) / (b.y - a.y));
        }
      std::sort(xs.begin(), xs.end());
      for(size_t k = 0; k + 1 < xs.size(); k += 2) {
        double l = std::max(xs[k], double(xa)), r = std::min(xs[k + 1], double(xb + 1));
        if(r <= l) continue;
        any = true;
        if(!antialias) {
          // Pixel centers inside the span
          int pa = int(std::ceil(l - 0.5)), pb = int(std::ceil(r - 0.5)) - 1;
          for(int x = std::max(pa, xa); x <= std::min(pb, xb); x++) acc[x - xa] = 1;
          continue;
        }
        int pa = int(std::floor(l)), pb = int(std::floor(r));
        float wgt = 1.f / subs;
        if(pa == pb) { acc[pa - xa] += float(r - l) * wgt; continue; }
        acc[pa - xa] += float(pa + 1 - l) * wgt;
        for(int x = pa + 1; x < pb; x++) acc[x - xa] += wgt;
        if(pb <= xb) acc[pb - xa] += float(r - pb) * wgt;
      }
    }
    if(!any) continue;
    for(int x = xa; x <= xb;) {
      uint8_t v = uint8_t(std::clamp(acc[x - xa], 0.f, 1.f) * 255 + 0.5f);
      int e = x;
      while(e < xb && uint8_t(std::clamp(acc[e + 1 - xa], 0.f, 1.f) * 255 + 0.5f) == v) e++;
      if(v) m->fillSpan(y, x, e, v);
      x = e + 1;
    }
  }
  return m;
}

static void maxSet(Mask& m, int x, int y, uint8_t v) {
  if(v > m.get(x, y)) m.set(x, y, v);
}

void strokePolyline(Mask& m, const Polyline& pts, double width, bool antialias) {
  if(pts.empty()) return;
  double r = std::max(width, 1.0) / 2;
  auto capsule = [&](Point a, Point b) {
    int x0 = std::max(0, int(std::floor(std::min(a.x, b.x) - r - 1))), x1 = std::min(m.width() - 1, int(std::ceil(std::max(a.x, b.x) + r + 1)));
    int y0 = std::max(0, int(std::floor(std::min(a.y, b.y) - r - 1))), y1 = std::min(m.height() - 1, int(std::ceil(std::max(a.y, b.y) + r + 1)));
    double dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy;
    for(int y = y0; y <= y1; y++)
      for(int x = x0; x <= x1; x++) {
        double px = x + 0.5 - a.x, py = y + 0.5 - a.y;
        double t = len2 > 0 ? std::clamp((px * dx + py * dy) / len2, 0.0, 1.0) : 0;
        double ex = px - dx * t, ey = py - dy * t, d = std::sqrt(ex * ex + ey * ey);
        double c = antialias ? std::clamp(r - d + 0.5, 0.0, 1.0) : (d <= r ? 1.0 : 0.0);
        if(c > 0) maxSet(m, x, y, uint8_t(c * 255 + 0.5));
      }
  };
  if(pts.size() == 1) capsule(pts[0], pts[0]);
  for(size_t i = 0; i + 1 < pts.size(); i++) capsule(pts[i], pts[i + 1]);
}

void pixelPolyline(Mask& m, const Polyline& pts, bool pixelPerfect) {
  std::vector<std::pair<int, int>> path;
  auto add = [&](int x, int y) { if(path.empty() || path.back() != std::pair{ x, y }) path.push_back({ x, y }); };
  for(size_t i = 0; i < pts.size(); i++) {
    int x1 = int(std::floor(pts[i].x)), y1 = int(std::floor(pts[i].y));
    if(i == 0) { add(x1, y1); continue; }
    int x0 = path.back().first, y0 = path.back().second;
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while(x0 != x1 || y0 != y1) {
      int e2 = 2 * err;
      if(e2 >= dy) { err += dy; x0 += sx; }
      if(e2 <= dx) { err += dx; y0 += sy; }
      add(x0, y0);
    }
  }
  // Drops the middle pixel of each L corner, so diagonal runs stay one pixel thick
  if(pixelPerfect && path.size() > 2) {
    std::vector<std::pair<int, int>> out{ path[0] };
    for(size_t i = 1; i + 1 < path.size(); i++) {
      auto [ax, ay] = out.back();
      auto [cx, cy] = path[i + 1];
      if(std::abs(ax - cx) == 1 && std::abs(ay - cy) == 1) continue;
      out.push_back(path[i]);
    }
    out.push_back(path.back());
    path = std::move(out);
  }
  for(auto [x, y] : path)
    if(x >= 0 && y >= 0 && x < m.width() && y < m.height()) m.set(x, y, 255);
}
#pragma endregion

#pragma region [ Paths ]
std::vector<Polyline> dashed(const Polyline& pts, double on, double off) {
  std::vector<Polyline> out;
  if(pts.size() < 2) return { pts };
  bool drawing = true;
  double left = on;
  Polyline cur{ pts[0] };
  for(size_t i = 0; i + 1 < pts.size(); i++) {
    Point a = pts[i], b = pts[i + 1];
    double len = std::hypot(b.x - a.x, b.y - a.y), t = 0;
    while(len - t > left) {
      t += left;
      Point p{ a.x + (b.x - a.x) * t / len, a.y + (b.y - a.y) * t / len };
      if(drawing) { cur.push_back(p); out.push_back(std::move(cur)); cur.clear(); }
      else cur = { p };
      drawing = !drawing;
      left = drawing ? on : off;
    }
    left -= len - t;
    if(drawing) cur.push_back(b);
  }
  if(drawing && cur.size() > 1) out.push_back(std::move(cur));
  return out;
}

Polyline arrowHead(const Polyline& pts, double width, bool start) {
  if(pts.size() < 2) return {};
  Point tip = start ? pts.front() : pts.back();
  // The direction from a point a few pixels back, so a curve's last tiny segment doesn't skew the head
  double len = 4 * width + 6, need = len, back = 0;
  Point from = tip;
  for(size_t k = 1; k < pts.size() && back < need; k++) {
    from = start ? pts[k] : pts[pts.size() - 1 - k];
    back = std::hypot(from.x - tip.x, from.y - tip.y);
  }
  double ang = std::atan2(tip.y - from.y, tip.x - from.x), wing = PI / 6;
  return { { tip.x - len * std::cos(ang - wing), tip.y - len * std::sin(ang - wing) }, tip,
           { tip.x - len * std::cos(ang + wing), tip.y - len * std::sin(ang + wing) } };
}

void cubicTo(Polyline& out, Point p0, Point c0, Point c1, Point p1) {
  double est = std::hypot(c0.x - p0.x, c0.y - p0.y) + std::hypot(c1.x - c0.x, c1.y - c0.y) + std::hypot(p1.x - c1.x, p1.y - c1.y);
  int n = std::clamp(int(est / 2), 4, 512);
  for(int i = 1; i <= n; i++) {
    double t = double(i) / n, u = 1 - t;
    double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    out.push_back({ a * p0.x + b * c0.x + c * c1.x + d * p1.x, a * p0.y + b * c0.y + c * c1.y + d * p1.y });
  }
}
#pragma endregion

#pragma region [ Shapes ]
Polyline shapeContour(EShapeKind kind, double x0, double y0, double x1, double y1, double inset, double radius, int sides) {
  double l = std::min(x0, x1) + inset, r = std::max(x0, x1) - inset, t = std::min(y0, y1) + inset, b = std::max(y0, y1) - inset;
  if(r - l < 0.01 || b - t < 0.01) return {};
  double cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l) / 2, ry = (b - t) / 2;
  Polyline out;
  auto arc = [&](double ax, double ay, double arx, double ary, double a0, double a1) {
    int n = std::clamp(int(std::max(arx, ary) * std::abs(a1 - a0) / 2), 4, 720);
    for(int i = 0; i <= n; i++) {
      double a = a0 + (a1 - a0) * i / n;
      out.push_back({ ax + arx * std::cos(a), ay + ary * std::sin(a) });
    }
  };
  switch(kind) {
    case EShapeKind::Rect:
      return { { l, t }, { r, t }, { r, b }, { l, b } };
    case EShapeKind::RoundRect: {
      double rr = std::clamp(radius - inset, 0.0, std::min(rx, ry));
      if(rr < 0.01) return { { l, t }, { r, t }, { r, b }, { l, b } };
      arc(r - rr, t + rr, rr, rr, -PI / 2, 0);
      arc(r - rr, b - rr, rr, rr, 0, PI / 2);
      arc(l + rr, b - rr, rr, rr, PI / 2, PI);
      arc(l + rr, t + rr, rr, rr, PI, PI * 1.5);
      return out;
    }
    case EShapeKind::Ellipse:
      arc(cx, cy, rx, ry, 0, 2 * PI);
      out.pop_back();
      return out;
    case EShapeKind::Polygon: {
      // Regular polygon in the box's ellipse, first corner up; the inset is applied to the apothem
      sides = std::clamp(sides, 3, 64);
      if(inset > 0) {
        double orx = rx + inset, ory = ry + inset, apothem = std::min(orx, ory) * std::cos(PI / sides);
        double s = std::max(0.0, (apothem - inset) / apothem);
        rx = orx * s; ry = ory * s;
      }
      for(int i = 0; i < sides; i++) {
        double a = -PI / 2 + 2 * PI * i / sides;
        out.push_back({ cx + rx * std::cos(a), cy + ry * std::sin(a) });
      }
      return out;
    }
    default: return out;
  }
}
#pragma endregion

}
