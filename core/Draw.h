#pragma once
#include "core/Raster.h"
#include <vector>

namespace david {

using Polyline = std::vector<Point>;

#pragma region [ Coverage ]
// Even-odd fill of all contours together (a ring is an outer and an inner contour). Antialiased with 16 sub-rows
// and exact horizontal coverage; aliased samples pixel centers.
std::unique_ptr<Mask> fillContours(int w, int h, const std::vector<Polyline>& contours, bool antialias);
// Round-capped, round-joined line of the given width; each pixel keeps its highest coverage
void strokePolyline(Mask& m, const Polyline& pts, double width, bool antialias);
// 1-px Bresenham path through the points, with pixel-perfect cleanup (no L-shaped corners)
void pixelPolyline(Mask& m, const Polyline& pts, bool pixelPerfect);
#pragma endregion

#pragma region [ Paths ]
// Splits a line into dashes: on and off lengths in pixels
std::vector<Polyline> dashed(const Polyline& pts, double on, double off);
// Two wings for an arrow head at pts.back() (or pts.front() when start), sized by the line width
Polyline arrowHead(const Polyline& pts, double width, bool start);
void cubicTo(Polyline& out, Point p0, Point c0, Point c1, Point p1);
#pragma endregion

#pragma region [ Shapes ]
enum class EShapeKind : uint8_t { Rect, RoundRect, Ellipse, Polygon, COUNT };
// A closed contour inside the box (x0, y0)-(x1, y1), inset by `inset` pixels on every side
Polyline shapeContour(EShapeKind kind, double x0, double y0, double x1, double y1, double inset, double radius, int sides);
#pragma endregion

}
