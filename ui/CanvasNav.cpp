#include "ui/CanvasNav.h"
#include <algorithm>
#include <cmath>

namespace david {

// Paint.NET-like zoom presets
static const double ZOOMS[] = {
  0.01, 0.02, 0.03, 0.05, 0.08, 0.12, 0.16, 0.25, 0.33, 0.5, 0.66, 1, 1.5, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 24, 32, 48, 64,
};

void CanvasNav::setViewport(int x, int y, int w, int h, int fbHeight) {
  bool first = view.vpW <= 1;
  // The image point at the middle of the view stays there when the view changes size (panels docking, resizing)
  double cx = toImageX(view.vpX + view.vpW / 2.0), cy = toImageY(view.vpY + view.vpH / 2.0);
  bool moved = w != view.vpW || h != view.vpH || x != view.vpX || y != view.vpY;
  view.vpX = x; view.vpY = y; view.vpW = std::max(w, 1); view.vpH = std::max(h, 1); view.fbHeight = fbHeight;
  if(first) fit();
  else if(moved) centerOn(cx, cy);
}

void CanvasNav::fit() {
  double z = std::min(view.vpW / double(_imgW), view.vpH / double(_imgH));
  view.zoom = std::clamp(z, ZOOMS[0], 1.0);
  centerOn(_imgW / 2.0, _imgH / 2.0);
}

void CanvasNav::actualSize() {
  zoomAt(1, view.vpX + view.vpW / 2.0, view.vpY + view.vpH / 2.0);
}

void CanvasNav::zoomAt(double zoom, double sx, double sy) {
  zoom = std::clamp(zoom, ZOOMS[0], ZOOMS[std::size(ZOOMS) - 1]);
  double ix = toImageX(sx), iy = toImageY(sy);
  view.zoom = zoom;
  view.originX = ix - (sx - view.vpX) / zoom;
  view.originY = iy - (sy - view.vpY) / zoom;
  clamp();
}

void CanvasNav::zoomStep(int dir, double sx, double sy) {
  double next = view.zoom;
  if(dir > 0) {
    for(double z : ZOOMS) if(z > view.zoom * 1.001) { next = z; break; }
  } else {
    for(double z : ZOOMS) if(z < view.zoom * 0.999) next = z;
  }
  zoomAt(next, sx, sy);
}

void CanvasNav::panBy(double dx, double dy) {
  view.originX -= dx / view.zoom;
  view.originY -= dy / view.zoom;
  clamp();
}

void CanvasNav::centerOn(double ix, double iy) {
  view.originX = ix - view.vpW / 2.0 / view.zoom;
  view.originY = iy - view.vpH / 2.0 / view.zoom;
  clamp();
}

bool CanvasNav::inViewport(double sx, double sy) const {
  return sx >= view.vpX && sy >= view.vpY && sx < view.vpX + view.vpW && sy < view.vpY + view.vpH;
}

// The image moves freely, as long as a strip of it (48 screen px, or all of it when smaller) stays in view
void CanvasNav::clamp() {
  double viewW = view.vpW / view.zoom, viewH = view.vpH / view.zoom;
  auto axis = [&](double& origin, double img, double vis) {
    // Never more than the view itself, so the bounds stay ordered (lo <= 0 <= hi)
    double keep = std::min({ img, vis, 48 / view.zoom });
    origin = std::clamp(origin, keep - vis, img - keep);
  };
  axis(view.originX, _imgW, viewW);
  axis(view.originY, _imgH, viewH);
  // Snap to whole screen pixels so nearest-filtered pixels don't shimmer while panning
  view.originX = std::round(view.originX * view.zoom) / view.zoom;
  view.originY = std::round(view.originY * view.zoom) / view.zoom;
}

}
