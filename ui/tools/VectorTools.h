#pragma once
#include "core/Vector.h"
#include "ui/tools/Tool.h"
#include <memory>
#include <string>

namespace david {

// A vector object a drawing tool is placing on a vector layer: added on the first update, replaced on each
// later one, and one History step on commit
class LiveObject {
public:
  bool active() const { return bool(_change); }
  uint64_t id() const { return _id; }
  void set(ToolHost& h, VecObject o);
  void commit(ToolHost& h, const char* name, const char* icon);
  void cancel(ToolHost& h);
private:
  std::unique_ptr<VectorChange> _change;
  uint64_t _id = 0;
};

// Smart guides for vector edits: moving points line up, horizontally or vertically, with the box edges,
// centers and nodes of the other objects (and any extra points). Alt turns it off; grid snap goes first.
class Snapper {
public:
  // Targets from the objects (world coordinates), skipping the ids being moved
  void begin(const std::vector<VecObject>& objects, const std::vector<uint64_t>& skip, const std::vector<Point>& extra = {});
  // The offset that lines one of the moving points up with a target on each axis, within tol image pixels
  // (0 on an axis that found none); remembers the guide lines to draw
  Point snap(const std::vector<Point>& moving, double tol, bool snapX = true, bool snapY = true);
  // Off for this frame (Alt held, grid snap): no offset and no lines
  void idle() { _guides.clear(); }
  void clear() { _targets.clear(); _guides.clear(); }
  void draw(ToolHost& h, ImDrawList* dl) const;
  // Whether this gesture should snap: on a vector layer, grid snap off, Alt up
  static bool wanted(ToolHost& h, const ToolInput& in);
private:
  struct Guide { Point a, b; };
  std::vector<Point> _targets;
  std::vector<Guide> _guides;
};

// The line a drawing tool gives a new object: the color and width, inside the shape like the raster tools
VecStroke toolStroke(uint32_t color, double width, EStrokeAlign align);

// Actions on the picked objects of the active vector layer (the vector tool row, the Object window, keys)
enum class EObjOp : uint8_t {
  Delete, Duplicate, Forward, Backward, Group, Ungroup, FlipH, FlipV, RotateLeft, RotateRight, ToPath,
  Union, Subtract, Intersect, Exclude, COUNT
};
struct ObjOpInfo { const char* icon; const char* name; size_t minPicked; };
const ObjOpInfo& objectOpInfo(EObjOp op);
// The command id ("object.union", ...)
std::string objectOpId(EObjOp op);
// Enough objects are picked for it
bool objectOpReady(ToolHost& h, EObjOp op);
// One History step; nothing when the op changes nothing
void runObjectOp(ToolHost& h, EObjOp op);

}
