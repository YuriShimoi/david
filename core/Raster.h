#pragma once
#include "core/Mask.h"
#include <vector>

namespace david {

class Document;
class Layer;
class TileRecorder;

struct Point { double x, y; };

#pragma region [ Selection shapes ]
std::unique_ptr<Mask> rectMask(int w, int h, double x0, double y0, double x1, double y1);
std::unique_ptr<Mask> ellipseMask(int w, int h, double x0, double y0, double x1, double y1);
// Even-odd fill sampled at pixel centers
std::unique_ptr<Mask> polygonMask(int w, int h, const std::vector<Point>& pts);
#pragma endregion

#pragma region [ Outline ]
// Pixel-edge outline of the mask at 50% coverage, as merged horizontal and vertical runs
struct Segment { int x0, y0, x1, y1; };
std::vector<Segment> maskOutline(const Mask& mask);
#pragma endregion

#pragma region [ Flood fill ]
struct FloodOptions {
  float tolerance = 0.5f;   // 0 = byte-exact match, 1 = everything
  bool contiguous = true;
  bool classicRgb = false;  // Paint.NET's RGBA distance instead of OKLab
  bool soft = false;        // antialiased coverage near the tolerance
  float edgeStop = 0;       // > 0: the contiguous fill never enters pixels whose Sobel lightness edge is above this
  bool toOutline = false;   // fill everything that is not the outline color
  uint32_t outline = 0;
};
// layer = null samples the image
std::unique_ptr<Mask> floodMask(const Document& doc, const Layer* layer, int x, int y, const FloodOptions& o);
#pragma endregion

#pragma region [ Transform ]
// Scale and rotate around (cx, cy), then translate by (dx, dy)
struct Xform {
  double dx = 0, dy = 0, angle = 0, cx = 0, cy = 0, sx = 1, sy = 1;
  bool identity() const { return dx == 0 && dy == 0 && angle == 0 && sx == 1 && sy == 1; }
  bool scaled() const { return sx != 1 || sy != 1; }
  Point apply(Point p) const;
  Point inverse(Point p) const;
};

// Pixels lifted out of a layer under a selection
struct Lifted {
  IRect bounds;
  std::vector<uint32_t> px;  // alpha already scaled by the selection coverage
};

Lifted lift(const Layer& layer, const Mask& sel);
// Erases the source (unless copying) and draws the lifted pixels transformed, recording touched tiles
void stampLifted(Layer& layer, TileRecorder& rec, const Mask& sel, const Lifted& lifted, const Xform& xf, bool copy);
std::unique_ptr<Mask> transformMask(const Mask& mask, const Xform& xf);
#pragma endregion

}
