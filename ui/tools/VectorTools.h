#pragma once
#include "core/Vector.h"
#include "ui/tools/Tool.h"
#include <memory>

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

// The line a drawing tool gives a new object: the color and width, inside the shape like the raster tools
VecStroke toolStroke(uint32_t color, double width, EStrokeAlign align);

}
