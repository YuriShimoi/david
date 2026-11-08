#pragma once
#include "core/Blend.h"
#include "core/Gradient.h"
#include "core/History.h"
#include "core/Raster.h"
#include <memory>
#include <string>
#include <vector>

namespace david {

class Document;
class Layer;

#pragma region [ Geometry ]
// 2×3 affine in SVG order: x' = a x + c y + e, y' = b x + d y + f
struct Affine {
  double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
  Point apply(Point p) const { return { a * p.x + c * p.y + e, b * p.x + d * p.y + f }; }
  // Directions only (no translation)
  Point applyVector(Point p) const { return { a * p.x + c * p.y, b * p.x + d * p.y }; }
  // this after o
  Affine operator*(const Affine& o) const;
  Affine inverse() const;
  bool identity() const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
  // The average scale (stroke widths through the transform)
  double scale() const;
  static Affine translate(double x, double y) { return { 1, 0, 0, 1, x, y }; }
  static Affine scaling(double sx, double sy) { return { sx, 0, 0, sy, 0, 0 }; }
  static Affine rotation(double radians);
  // Scale and rotation around a point
  static Affine around(Point c, const Affine& m) { return translate(c.x, c.y) * m * translate(-c.x, -c.y); }
};

struct DRect {
  double x0 = 1, y0 = 1, x1 = 0, y1 = 0;
  bool empty() const { return x1 < x0 || y1 < y0; }
  double width() const { return x1 - x0; }
  double height() const { return y1 - y0; }
  void add(Point p);
  void add(const DRect& r) { if(!r.empty()) { add(Point{ r.x0, r.y0 }); add(Point{ r.x1, r.y1 }); } }
};
#pragma endregion

#pragma region [ Objects ]
enum class EVecKind : uint8_t { Path, Rect, Ellipse, Polygon, Text, Group };
enum class ENodeType : uint8_t { Corner, Smooth, Symmetric };
enum class EPaintType : uint8_t { None, Solid, Linear, Radial };
enum class EStrokeAlign : uint8_t { Center, Inside, Outside };
enum class ECap : uint8_t { Butt, Round, Square };
enum class EJoin : uint8_t { Miter, Round, Bevel };

// A path node: the point and its two Bezier handles (equal to the point when that side is straight).
// radius rounds a corner node in the outline; the node itself stays where it is.
struct PathNode {
  Point p{}, in{}, out{};
  ENodeType type = ENodeType::Corner;
  double radius = 0;
  static PathNode corner(Point p) { return { p, p, p, ENodeType::Corner }; }
};
struct Subpath {
  std::vector<PathNode> nodes;
  bool closed = false;
};

// Fill or stroke paint. Gradient ends are in the object's own coordinates; the colors between stops follow
// the gradient tool's interpolation spaces.
struct VecPaint {
  EPaintType type = EPaintType::None;
  uint32_t color = 0xFF000000u;
  std::vector<GradStop> stops;
  EGradSpace space = EGradSpace::Oklab;
  Point a{}, b{};
  static VecPaint solid(uint32_t c) { VecPaint p; p.type = EPaintType::Solid; p.color = c; return p; }
  bool visible() const { return type != EPaintType::None; }
};

struct VecStroke {
  VecPaint paint;
  double width = 1;
  EStrokeAlign align = EStrokeAlign::Center;
  ECap cap = ECap::Butt;
  EJoin join = EJoin::Miter;
  double miter = 4;
  std::vector<double> dash;  // on, off, ... in stroke widths
};

struct VecObject {
  uint64_t id = 0;
  EVecKind kind = EVecKind::Path;
  std::string name;
  bool visible = true, locked = false;
  double opacity = 1;
  Affine xf;
  // Path
  std::vector<Subpath> paths;
  // Rect, Ellipse, Polygon: the box in local coordinates
  double x = 0, y = 0, w = 0, h = 0;
  double radius = 0;   // Rect corners
  int sides = 5;       // Polygon
  double inner = 1;    // Polygon: inner radius ratio, below 1 makes a star
  // Text: (x, y) is the top-left of the first line
  std::string text, font;
  int fontIndex = 0;
  double size = 32;
  bool bold = false, italic = false;
  int align = 0;
  VecPaint fill;
  VecStroke stroke;
  std::vector<VecObject> children;  // Group, bottom to top
};

// One cel's objects, bottom to top. revision changes with every edit so renders know to redo.
struct VecTree {
  std::vector<VecObject> objects;
  uint64_t revision = 0;
  void touch();
};

uint64_t newObjectId();
// Fresh ids for an object and its children (pasted or duplicated copies)
void renumber(VecObject& o);
// Objects as paths in local coordinates: shapes become their outline, text its glyph outlines, rounded
// corner nodes their arcs
std::vector<Subpath> outline(const VecObject& o);
// The paths with each corner node's radius turned into an arc
std::vector<Subpath> roundCorners(const std::vector<Subpath>& paths);
bool hasRoundCorners(const std::vector<Subpath>& paths);
// Bounds of the outline (and half the stroke when withStroke), in local or world coordinates
DRect bounds(const VecObject& o, bool world, bool withStroke = false);
DRect bounds(const std::vector<VecObject>& objects, bool withStroke = false);
// By id, anywhere in the tree (groups too); null when gone
VecObject* findObject(std::vector<VecObject>& objects, uint64_t id);
const VecObject* findObject(const std::vector<VecObject>& objects, uint64_t id);
// The topmost top-level object under p (image coordinates): inside its fill, or within tol of its stroke
VecObject* hitTest(std::vector<VecObject>& objects, Point p, double tol);
// Flattens a subpath to a polyline (closed ones repeat no point)
std::vector<Point> flatten(const Subpath& s, double tolerance = 0.25);
// The shape's outline folded into a Path with its transform kept; text and groups stay as they are
void toPath(VecObject& o);
// Text outlines through FreeType and HarfBuzz (core/VectorText.cpp), in local coordinates
std::vector<Subpath> textOutline(const VecObject& o);
#pragma endregion

#pragma region [ Rendering ]
// Renders the objects through view (image -> target pixels) into a straight RGBA buffer of w × h (ThorVG)
void renderVector(const VecTree& tree, const Affine& view, int w, int h, std::vector<uint32_t>& out);
// Rebuilds a vector layer's tiles for one frame from its tree, at document resolution
void rasterizeVectorCel(Layer& layer, int frame);
// The frame's tree of a vector layer, made empty when it has none
VecTree& vectorTree(Layer& layer, int frame);
const VecTree* vectorTreeOf(const Layer& layer, int frame);
#pragma endregion

#pragma region [ History ]
// One edit of a vector layer's objects: the whole tree before and after (trees are small)
class VectorEdit : public Action {
public:
  uint64_t layerId = 0;
  int celFrame = 0;
  VecTree before, after;
  void undo(Document& doc) override;
  void redo(Document& doc) override;
  size_t bytes() const override;
};
// Records an edit of the active vector layer: snapshot first, then change the tree, then commit
class VectorChange {
public:
  explicit VectorChange(Document& doc);
  VecTree& tree() { return *_tree; }
  // Pushes the step when the tree changed (and rebuilds the layer's pixels); false when nothing did
  bool commit(std::string name, const char* icon);
  // Puts the tree back as it was
  void cancel();
private:
  Document& _doc;
  Layer* _layer;
  int _frame;
  VecTree* _tree;
  VecTree _before;
};
// The tree changed in place (live tool edits): bumps its revision and, unless pixels is false (the screen
// draws from the objects meanwhile), rebuilds the layer's pixels
void vectorChanged(Document& doc, Layer& layer, bool pixels = true);
#pragma endregion

}
