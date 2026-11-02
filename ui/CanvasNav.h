#pragma once
#include "render/CanvasRenderer.h"

namespace david {

// Zoom and pan of the canvas view. Screen coordinates are framebuffer pixels, y down.
class CanvasNav {
public:
  CanvasView view;

  void setViewport(int x, int y, int w, int h, int fbHeight);
  void setImage(int w, int h) { _imgW = w; _imgH = h; clamp(); }

  void fit();
  void actualSize();
  void zoomAt(double zoom, double sx, double sy);
  void zoomStep(int dir, double sx, double sy);
  void zoomStepCenter(int dir) { zoomStep(dir, view.vpX + view.vpW / 2.0, view.vpY + view.vpH / 2.0); }
  void panBy(double dx, double dy);
  void centerOn(double ix, double iy);

  double toImageX(double sx) const { return view.originX + (sx - view.vpX) / view.zoom; }
  double toImageY(double sy) const { return view.originY + (sy - view.vpY) / view.zoom; }
  bool inViewport(double sx, double sy) const;

private:
  int _imgW = 1, _imgH = 1;
  void clamp();
};

}
