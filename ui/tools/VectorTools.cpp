#include "ui/tools/VectorTools.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/Text.h"
#include "core/VectorOps.h"
#include "ui/CanvasNav.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace david {

static constexpr double PI = 3.14159265358979323846;

#pragma region [ Live objects ]
void LiveObject::set(ToolHost& h, VecObject o) {
  Document& doc = h.doc();
  if(!_change) {
    _change = std::make_unique<VectorChange>(doc);
    _id = newObjectId();
    o.id = _id;
    _change->tree().objects.push_back(std::move(o));
  } else if(VecObject* cur = findObject(_change->tree().objects, _id)) {
    o.id = _id;
    *cur = std::move(o);
  }
  // Live: the screen draws from the objects; the layer's pixels are rebuilt on commit
  vectorChanged(doc, doc.active(), false);
}

void LiveObject::commit(ToolHost& h, const char* name, const char* icon) {
  if(!_change) return;
  _change->commit(name, icon);
  h.doc().selectedObjects = { _id };
  _change.reset();
}

void LiveObject::cancel(ToolHost& h) {
  if(!_change) return;
  _change->cancel();
  _change.reset();
  h.doc().onChanged();
}

VecStroke toolStroke(uint32_t color, double width, EStrokeAlign align) {
  VecStroke s;
  s.paint = VecPaint::solid(color);
  s.width = width;
  s.align = align;
  return s;
}
#pragma endregion

namespace {

std::vector<VecObject>& objectsOf(ToolHost& h) { return vectorTree(h.doc().active(), h.doc().active().frame()).objects; }

}

#pragma region [ Object actions ]
const ObjOpInfo& objectOpInfo(EObjOp op) {
  static const ObjOpInfo infos[] = {
    { ICON_FA_TRASH, N_("Delete Objects"), 1 },
    { ICON_FA_CLONE, N_("Duplicate Objects"), 1 },
    { ICON_FA_ARROW_UP, N_("Bring Forward"), 1 },
    { ICON_FA_ARROW_DOWN, N_("Send Backward"), 1 },
    { ICON_FA_OBJECT_GROUP, N_("Group"), 2 },
    { ICON_FA_OBJECT_UNGROUP, N_("Ungroup"), 1 },
    { ICON_TB_FLIP_HORIZONTAL, N_("Flip Horizontal"), 1 },
    { ICON_TB_FLIP_VERTICAL, N_("Flip Vertical"), 1 },
    { ICON_TB_ROTATE_2, N_("Rotate 90° Left"), 1 },
    { ICON_TB_ROTATE_CLOCKWISE_2, N_("Rotate 90° Right"), 1 },
    { ICON_TB_VECTOR_SPLINE, N_("Object to Path"), 1 },
    { ICON_PH_UNITE_SQUARE, N_("Union"), 2 },
    { ICON_PH_SUBTRACT_SQUARE, N_("Subtract"), 2 },
    { ICON_PH_INTERSECT_SQUARE, N_("Intersect"), 2 },
    { ICON_PH_EXCLUDE_SQUARE, N_("Exclude"), 2 },
  };
  return infos[std::min(int(op), int(EObjOp::COUNT) - 1)];
}

std::string objectOpId(EObjOp op) {
  static const char* ids[] = { "delete", "duplicate", "forward", "backward", "group", "ungroup", "flipH", "flipV", "rotateLeft",
                               "rotateRight", "toPath", "union", "subtract", "intersect", "exclude" };
  return std::string("object.") + ids[std::min(int(op), int(EObjOp::COUNT) - 1)];
}

bool objectOpReady(ToolHost& h, EObjOp op) {
  if(!h.vectorMode()) return false;
  auto& objs = objectsOf(h);
  size_t n = 0;
  for(uint64_t id : h.doc().selectedObjects)
    for(auto& o : objs) if(o.id == id) { n++; break; }
  return n >= objectOpInfo(op).minPicked;
}

void runObjectOp(ToolHost& h, EObjOp op) {
  if(!objectOpReady(h, op)) return;
  Document& doc = h.doc();
  VectorChange ch(doc);
  auto& objs = ch.tree().objects;
  auto& sel = doc.selectedObjects;
  bool changed = false;
  switch(op) {
    case EObjOp::Delete: changed = removeObjects(objs, sel); break;
    case EObjOp::Duplicate: changed = duplicateObjects(objs, sel); break;
    case EObjOp::Forward: changed = orderObjects(objs, sel, 1); break;
    case EObjOp::Backward: changed = orderObjects(objs, sel, -1); break;
    case EObjOp::Group: changed = groupObjects(objs, sel); break;
    case EObjOp::Ungroup: changed = ungroupObjects(objs, sel); break;
    case EObjOp::FlipH: changed = transformObjects(objs, sel, Affine::scaling(-1, 1)); break;
    case EObjOp::FlipV: changed = transformObjects(objs, sel, Affine::scaling(1, -1)); break;
    case EObjOp::RotateLeft: changed = transformObjects(objs, sel, Affine::rotation(-PI / 2)); break;
    case EObjOp::RotateRight: changed = transformObjects(objs, sel, Affine::rotation(PI / 2)); break;
    case EObjOp::ToPath: changed = objectsToPath(objs, sel); break;
    case EObjOp::Union: changed = booleanObjects(objs, sel, EBoolOp::Union); break;
    case EObjOp::Subtract: changed = booleanObjects(objs, sel, EBoolOp::Subtract); break;
    case EObjOp::Intersect: changed = booleanObjects(objs, sel, EBoolOp::Intersect); break;
    case EObjOp::Exclude: changed = booleanObjects(objs, sel, EBoolOp::Exclude); break;
    case EObjOp::COUNT: break;
  }
  if(!changed) { ch.cancel(); return; }
  ch.tree().touch();
  const ObjOpInfo& info = objectOpInfo(op);
  ch.commit(info.name, info.icon);
}
#pragma endregion

namespace {

// The four corners of an object's local bounds through its transform
void corners(const VecObject& o, Point c[4]) {
  DRect r = bounds(o, false);
  if(o.kind == EVecKind::Group) r = bounds(o.children, false);
  Point local[4] = { { r.x0, r.y0 }, { r.x1, r.y0 }, { r.x1, r.y1 }, { r.x0, r.y1 } };
  for(int i = 0; i < 4; i++) c[i] = o.xf.apply(local[i]);
}

void quad(ImDrawList* dl, ToolHost& h, const Point c[4], ImU32 col, float thick) {
  for(int i = 0; i < 4; i++) dl->AddLine(h.toScreen(c[i].x, c[i].y), h.toScreen(c[(i + 1) % 4].x, c[(i + 1) % 4].y), col, thick);
}

std::string utf8Name(const VecObject& o) {
  if(!o.name.empty()) return tr(o.name.c_str());
  switch(o.kind) {
    case EVecKind::Rect: return tr("Rectangle");
    case EVecKind::Ellipse: return tr("Ellipse");
    case EVecKind::Polygon: return tr("Polygon");
    case EVecKind::Text: return tr("Text");
    case EVecKind::Group: return tr("Group");
    default: return tr("Path");
  }
}

// RGBA packed <-> ImGui floats
ImVec4 toVec(uint32_t c) { return { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f }; }
uint32_t fromVec(const float v[4]) {
  uint32_t c = 0;
  for(int k = 0; k < 4; k++) c |= uint32_t(std::clamp(std::lround(v[k] * 255), 0L, 255L)) << (k * 8);
  return c;
}
}

#pragma region [ Object tool ]
// The Mouse tool. On a vector layer (with vector support on) it picks objects: click, Shift-click and a
// marquee select; dragging moves, the corner nubs scale (Shift keeps the ratio) and the round nub rotates
// (Shift snaps to 15°). Arrow keys nudge, Del deletes; the tool bar edits fill, line and order.
class MouseTool : public Tool {
public:
  MouseTool() : Tool(ETool::Mouse, ICON_TB_POINTER, N_("Mouse"), ImGuiKey_V) {}

  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(!h.vectorMode()) return ECursor::Arrow;
    if(_mode == EMode::Move) return ECursor::Move;
    if(handleAt(h, at) >= 0) return ECursor::Pointer;
    return hitTest(objectsOf(h), { at.x, at.y }, 4 / h.nav().view.zoom) ? ECursor::Move : ECursor::Arrow;
  }

  void down(ToolHost& h, const ToolInput& in) override {
    if(!h.vectorMode()) return;
    auto& sel = h.doc().selectedObjects;
    Point p{ in.x, in.y };
    _start = p;
    if(int hd = handleAt(h, in); hd >= 0) { begin(h, hd == 4 ? EMode::Rotate : EMode::Scale, hd); return; }
    VecObject* hit = hitTest(objectsOf(h), p, 4 / h.nav().view.zoom);
    // Double-click (Gravit): straight into editing the object's nodes
    if(hit && in.doubleClick && hit->kind != EVecKind::Group && hit->kind != EVecKind::Text) {
      sel = { hit->id };
      h.useTool(ETool::Node);
      return;
    }
    if(hit) {
      bool picked = std::find(sel.begin(), sel.end(), hit->id) != sel.end();
      if(in.shift) {
        if(picked) { std::erase(sel, hit->id); return; }
        sel.push_back(hit->id);
      } else if(!picked) sel = { hit->id };
      begin(h, EMode::Move, -1);
      return;
    }
    if(!in.shift) sel.clear();
    _mode = EMode::Marquee;
    _end = p;
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    Point p{ in.x, in.y };
    if(_mode == EMode::Marquee) { _end = p; return; }
    if(!_change) return;
    Affine m;
    if(_mode == EMode::Move) {
      double dx = p.x - _start.x, dy = p.y - _start.y;
      if(in.shift) { if(std::abs(dx) > std::abs(dy)) dy = 0; else dx = 0; }
      // Grid snap: the selection box's top-left corner lands on the grid
      if(h.settings().grid && h.settings().gridSnap && !_box.empty()) {
        double x0 = _box.x0 + dx, y0 = _box.y0 + dy, sx = x0, sy = y0;
        snapToGrid(h.settings(), sx, sy);
        if(!in.shift || dx != 0) dx += sx - x0;
        if(!in.shift || dy != 0) dy += sy - y0;
      }
      m = Affine::translate(dx, dy);
    } else if(_mode == EMode::Rotate) {
      Point c{ (_box.x0 + _box.x1) / 2, (_box.y0 + _box.y1) / 2 };
      double a = std::atan2(p.y - c.y, p.x - c.x) - std::atan2(_start.y - c.y, _start.x - c.x);
      if(in.shift) a = std::round(a / (PI / 12)) * (PI / 12);
      m = Affine::around(c, Affine::rotation(a));
    } else {
      // The opposite corner stays; the dragged one follows the pointer (onto the grid when snap is on)
      snapToGrid(h.settings(), p.x, p.y);
      Point fixed{ _handle == 1 || _handle == 2 ? _box.x0 : _box.x1, _handle >= 2 ? _box.y0 : _box.y1 };
      Point from{ _handle == 1 || _handle == 2 ? _box.x1 : _box.x0, _handle >= 2 ? _box.y1 : _box.y0 };
      double sx = std::abs(from.x - fixed.x) > 1e-9 ? (p.x - fixed.x) / (from.x - fixed.x) : 1;
      double sy = std::abs(from.y - fixed.y) > 1e-9 ? (p.y - fixed.y) / (from.y - fixed.y) : 1;
      if(in.shift) { double k = std::max(std::abs(sx), std::abs(sy)); sx = sx < 0 ? -k : k; sy = sy < 0 ? -k : k; }
      if(std::abs(sx) < 1e-3) sx = 1e-3;
      if(std::abs(sy) < 1e-3) sy = 1e-3;
      m = Affine::around(fixed, Affine::scaling(sx, sy));
    }
    for(auto& [id, xf] : _startXf)
      if(VecObject* o = findObject(_change->tree().objects, id)) o->xf = m * xf;
    vectorChanged(h.doc(), h.doc().active(), false);
  }

  void up(ToolHost& h, const ToolInput&) override {
    if(_mode == EMode::Marquee) {
      DRect r;
      r.add(_start); r.add(_end);
      auto& sel = h.doc().selectedObjects;
      for(auto& o : objectsOf(h)) {
        DRect b = bounds(o, true, true);
        bool inside = !b.empty() && b.x0 >= r.x0 && b.x1 <= r.x1 && b.y0 >= r.y0 && b.y1 <= r.y1;
        if(inside && !o.locked && std::find(sel.begin(), sel.end(), o.id) == sel.end()) sel.push_back(o.id);
      }
    } else if(_change) {
      const char* names[] = { "", N_("Move Objects"), N_("Scale Objects"), N_("Rotate Objects") };
      _change->commit(names[int(_mode)], icon);
      _change.reset();
    }
    _mode = EMode::None;
  }

  bool busy() const override { return _mode != EMode::None; }
  void cancel(ToolHost&) override {
    if(_change) _change->cancel();
    _change.reset();
    _mode = EMode::None;
  }

  void keys(ToolHost& h) override {
    if(!h.vectorMode() || h.doc().selectedObjects.empty()) return;
    auto& io = ImGui::GetIO();
    if(io.KeyCtrl || io.KeyAlt) return;
    double step = io.KeyShift ? 10 : 1;
    Point d{};
    if(ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) d.x = -step;
    if(ImGui::IsKeyPressed(ImGuiKey_RightArrow)) d.x = step;
    if(ImGui::IsKeyPressed(ImGuiKey_UpArrow)) d.y = -step;
    if(ImGui::IsKeyPressed(ImGuiKey_DownArrow)) d.y = step;
    if(d.x || d.y) edit(h, N_("Move Objects"), [&](std::vector<VecObject>& objs, VecObject& o) { o.xf = Affine::translate(d.x, d.y) * o.xf; (void)objs; });
    // Del with no pixel selection deletes the objects (with one, Erase Selection asks first)
    if(ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !h.doc().hasSelection()) runObjectOp(h, EObjOp::Delete);
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    if(!h.vectorMode()) return;
    float dpi = h.dpi();
    auto& objs = objectsOf(h);
    ImU32 accent = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    // The object under the pointer, outlined
    if(_mode == EMode::None)
      if(VecObject* o = hitTest(objs, { at.x, at.y }, 4 / h.nav().view.zoom)) {
        Point c[4];
        corners(*o, c);
        quad(dl, h, c, IM_COL32(120, 200, 255, 160), 1);
      }
    for(uint64_t id : h.doc().selectedObjects)
      if(const VecObject* o = findObject(objs, id)) {
        Point c[4];
        corners(*o, c);
        quad(dl, h, c, accent, 1);
      }
    DRect b = selectionBox(h);
    if(!b.empty() && _mode != EMode::Marquee) {
      Point c[4] = { { b.x0, b.y0 }, { b.x1, b.y0 }, { b.x1, b.y1 }, { b.x0, b.y1 } };
      for(int i = 0; i < 4; i++) antsLine(dl, h.toScreen(c[i].x, c[i].y), h.toScreen(c[(i + 1) % 4].x, c[(i + 1) % 4].y), 0, dpi);
      int hot = handleAt(h, at);
      for(int i = 0; i < 4; i++) drawNub(dl, h.toScreen(c[i].x, c[i].y), dpi, hot == i || (_mode == EMode::Scale && _handle == i));
      ImVec2 top = h.toScreen((b.x0 + b.x1) / 2, b.y0), r = rotatePos(h, b);
      dl->AddLine(top, r, accent, 1);
      drawNub(dl, r, dpi, hot == 4 || _mode == EMode::Rotate);
    }
    if(_mode == EMode::Marquee) {
      ImVec2 a = h.toScreen(_start.x, _start.y), e = h.toScreen(_end.x, _end.y);
      dl->AddRectFilled({ std::min(a.x, e.x), std::min(a.y, e.y) }, { std::max(a.x, e.x), std::max(a.y, e.y) }, IM_COL32(120, 200, 255, 40));
      dl->AddRect({ std::min(a.x, e.x), std::min(a.y, e.y) }, { std::max(a.x, e.x), std::max(a.y, e.y) }, IM_COL32(120, 200, 255, 200));
    }
  }

  // The style lives in the Object window; the bar names what is picked
  void options(ToolHost& h) override {
    if(!h.vectorMode()) return;
    auto& objs = objectsOf(h);
    auto& sel = h.doc().selectedObjects;
    std::erase_if(sel, [&](uint64_t id) { return !findObject(objs, id); });
    if(sel.empty()) ImGui::TextDisabled("%s", tr("Click objects to select them (Shift adds); drag on empty space for a marquee"));
    else ImGui::TextUnformatted(sel.size() == 1 ? utf8Name(*findObject(objs, sel[0])).c_str() : (std::to_string(sel.size()) + " " + tr("objects")).c_str());
  }

private:
  enum class EMode : uint8_t { None, Move, Scale, Rotate, Marquee };
  EMode _mode = EMode::None;
  int _handle = -1;
  Point _start{}, _end{};
  DRect _box;
  std::unique_ptr<VectorChange> _change;
  std::map<uint64_t, Affine> _startXf;

  DRect selectionBox(ToolHost& h) {
    DRect b;
    auto& objs = objectsOf(h);
    for(uint64_t id : h.doc().selectedObjects)
      if(const VecObject* o = findObject(objs, id)) b.add(bounds(*o, true));
    return b;
  }

  ImVec2 rotatePos(ToolHost& h, const DRect& b) {
    ImVec2 top = h.toScreen((b.x0 + b.x1) / 2, b.y0);
    return { top.x, top.y - 24 * h.dpi() };
  }

  // 0-3 corners clockwise from the top-left, 4 the rotate nub, -1 none
  int handleAt(ToolHost& h, const ToolInput& in) {
    if(!h.vectorMode()) return -1;
    DRect b = selectionBox(h);
    if(b.empty()) return -1;
    float dpi = h.dpi();
    if(overNub(in, rotatePos(h, b), dpi)) return 4;
    Point c[4] = { { b.x0, b.y0 }, { b.x1, b.y0 }, { b.x1, b.y1 }, { b.x0, b.y1 } };
    for(int i = 0; i < 4; i++) if(overNub(in, h.toScreen(c[i].x, c[i].y), dpi)) return i;
    return -1;
  }

  void begin(ToolHost& h, EMode mode, int handle) {
    _mode = mode;
    _handle = handle;
    _box = selectionBox(h);
    _change = std::make_unique<VectorChange>(h.doc());
    _startXf.clear();
    for(uint64_t id : h.doc().selectedObjects)
      if(const VecObject* o = findObject(_change->tree().objects, id)) _startXf[id] = o->xf;
  }

  // One step that changes each selected top-level object
  template<class F> void edit(ToolHost& h, const char* name, F fn) {
    VectorChange ch(h.doc());
    auto& objs = ch.tree().objects;
    for(uint64_t id : h.doc().selectedObjects)
      for(auto& o : objs) if(o.id == id) fn(objs, o);
    ch.tree().touch();
    ch.commit(name, icon);
  }
};
#pragma endregion

#pragma region [ Node tool ]
// Edits the nodes of a vector object (Inkscape's node tool): click picks an object or a node (Shift adds),
// a drag on empty space picks nodes in a marquee; dragging moves nodes and handles (smooth and symmetric nodes
// keep their handles in line), dragging a segment bends it, double-clicking a segment adds a node and Del
// removes the picked ones. A shape becomes a path on its first edit.
class NodeTool : public Tool {
public:
  NodeTool() : Tool(ETool::Node, ICON_TB_VECTOR, N_("Node"), ImGuiKey_N) {}

  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(!h.vectorMode()) return ECursor::Arrow;
    Hit t = hitAt(h, at);
    return t.kind != EHit::None ? ECursor::Pointer : ECursor::Arrow;
  }

  void down(ToolHost& h, const ToolInput& in) override {
    if(!h.vectorMode()) return;
    Point p{ in.x, in.y };
    _start = _end = p;
    Hit t = hitAt(h, in);
    auto& sel = h.doc().selectedObjects;
    if(t.kind == EHit::Node) {
      Key k{ t.sub, t.node };
      bool picked = std::find(_nodes.begin(), _nodes.end(), k) != _nodes.end();
      if(in.shift) { if(picked) std::erase(_nodes, k); else _nodes.push_back(k); return; }
      if(!picked) _nodes = { k };
      begin(h, EDrag::Nodes, t);
      return;
    }
    if(t.kind == EHit::HandleIn || t.kind == EHit::HandleOut) { begin(h, EDrag::Handle, t); return; }
    if(t.kind == EHit::Segment) {
      if(in.doubleClick) { insertNode(h, t); return; }
      begin(h, EDrag::Segment, t);
      return;
    }
    // Another object: it becomes the one being edited
    if(VecObject* o = hitTest(objectsOf(h), p, 4 / h.nav().view.zoom)) {
      if(sel.empty() || sel[0] != o->id) { sel = { o->id }; _nodes.clear(); }
      return;
    }
    // Double-click on empty space goes back to the Mouse tool, the object still picked
    if(in.doubleClick) { _nodes.clear(); h.useTool(ETool::Mouse); return; }
    if(!in.shift) _nodes.clear();
    _drag = EDrag::Marquee;
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    _end = { in.x, in.y };
    if(_drag == EDrag::None || _drag == EDrag::Marquee || !_change) return;
    VecObject* o = target(_change->tree().objects, h);
    if(!o) return;
    // Grid snap: the grabbed node or handle lands on the grid
    const ToolSettings& st = h.settings();
    if(st.grid && st.gridSnap && (_drag == EDrag::Nodes || _drag == EDrag::Handle) && _hit.sub < _startPaths.size() && _hit.node < _startPaths[_hit.sub].nodes.size()) {
      const PathNode& n = _startPaths[_hit.sub].nodes[_hit.node];
      Point w0 = o->xf.apply(_drag == EDrag::Nodes ? n.p : _hit.kind == EHit::HandleIn ? n.in : n.out);
      Point w{ w0.x + _end.x - _start.x, w0.y + _end.y - _start.y };
      snapToGrid(st, w.x, w.y);
      _end = { _start.x + w.x - w0.x, _start.y + w.y - w0.y };
    }
    Affine inv = o->xf.inverse();
    Point a = inv.apply(_start), b = inv.apply(_end);
    Point d{ b.x - a.x, b.y - a.y };
    o->paths = _startPaths;
    if(_drag == EDrag::Nodes) {
      for(auto& k : _nodes) {
        if(k.sub >= o->paths.size() || k.node >= o->paths[k.sub].nodes.size()) continue;
        PathNode& n = o->paths[k.sub].nodes[k.node];
        for(Point* q : { &n.p, &n.in, &n.out }) { q->x += d.x; q->y += d.y; }
      }
    } else if(_drag == EDrag::Handle) {
      PathNode& n = o->paths[_hit.sub].nodes[_hit.node];
      Point& hd = _hit.kind == EHit::HandleIn ? n.in : n.out;
      Point& other = _hit.kind == EHit::HandleIn ? n.out : n.in;
      hd = { hd.x + d.x, hd.y + d.y };
      // Smooth: the other handle turns to stay in line; symmetric: it mirrors exactly. Alt breaks the link.
      if(!in.alt && n.type != ENodeType::Corner) {
        double ox = hd.x - n.p.x, oy = hd.y - n.p.y, len = std::hypot(ox, oy);
        double keep = n.type == ENodeType::Symmetric ? len : std::hypot(other.x - n.p.x, other.y - n.p.y);
        if(len > 1e-9) other = { n.p.x - ox / len * keep, n.p.y - oy / len * keep };
      } else if(in.alt) n.type = ENodeType::Corner;
    } else if(_drag == EDrag::Segment) {
      // Bend: both handles of the segment move with the pointer, more where the press was near them
      Subpath& s = o->paths[_hit.sub];
      PathNode& n0 = s.nodes[_hit.node];
      PathNode& n1 = s.nodes[(_hit.node + 1) % s.nodes.size()];
      double t = _hit.t, w0 = (1 - t) * 4 / 3, w1 = t * 4 / 3;
      n0.out = { n0.out.x + d.x * w0, n0.out.y + d.y * w0 };
      n1.in = { n1.in.x + d.x * w1, n1.in.y + d.y * w1 };
      if(n0.type != ENodeType::Corner) n0.type = ENodeType::Corner;
      if(n1.type != ENodeType::Corner) n1.type = ENodeType::Corner;
    }
    vectorChanged(h.doc(), h.doc().active(), false);
  }

  void up(ToolHost& h, const ToolInput&) override {
    if(_drag == EDrag::Marquee) {
      DRect r;
      r.add(_start); r.add(_end);
      if(const VecObject* o = target(objectsOf(h), h))
        for(size_t si = 0; si < o->paths.size(); si++)
          for(size_t ni = 0; ni < o->paths[si].nodes.size(); ni++) {
            Point p = o->xf.apply(o->paths[si].nodes[ni].p);
            Key k{ si, ni };
            if(p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1 && std::find(_nodes.begin(), _nodes.end(), k) == _nodes.end()) _nodes.push_back(k);
          }
    } else if(_change) {
      if(_start.x != _end.x || _start.y != _end.y) _change->commit(N_("Edit Nodes"), icon);
      else _change->cancel();
      _change.reset();
    }
    _drag = EDrag::None;
  }

  bool busy() const override { return _drag != EDrag::None; }
  // Esc lets go of the picked nodes, then goes back to the Mouse tool
  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(k != ImGuiKey_Escape || !h.vectorMode() || _drag != EDrag::None) return false;
    if(!_nodes.empty()) _nodes.clear();
    else h.useTool(ETool::Mouse);
    return true;
  }
  void cancel(ToolHost&) override {
    if(_change) _change->cancel();
    _change.reset();
    _drag = EDrag::None;
  }

  void keys(ToolHost& h) override {
    if(!h.vectorMode() || _nodes.empty()) return;
    auto& io = ImGui::GetIO();
    if(io.KeyCtrl || io.KeyAlt) return;
    if(ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !h.doc().hasSelection()) removeNodes(h);
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    if(!h.vectorMode()) return;
    float dpi = h.dpi();
    const VecObject* o = target(objectsOf(h), h);
    if(!o) {
      if(VecObject* hv = hitTest(objectsOf(h), { at.x, at.y }, 4 / h.nav().view.zoom)) {
        for(auto& s : outline(*hv)) {
          auto pts = flatten(s, 0.5);
          for(size_t i = 0; i + 1 < pts.size() || (s.closed && i < pts.size()); i++) {
            Point a = hv->xf.apply(pts[i]), b = hv->xf.apply(pts[(i + 1) % pts.size()]);
            dl->AddLine(h.toScreen(a.x, a.y), h.toScreen(b.x, b.y), IM_COL32(120, 200, 255, 200), 1);
          }
        }
      }
      return;
    }
    std::vector<Subpath> paths = o->kind == EVecKind::Path ? o->paths : outline(*o);
    ImU32 line = IM_COL32(120, 200, 255, 220);
    for(auto& s : paths) {
      auto pts = flatten(s, 0.5);
      for(size_t i = 0; i + 1 < pts.size() || (s.closed && i < pts.size() && pts.size() > 1); i++) {
        Point a = o->xf.apply(pts[i]), b = o->xf.apply(pts[(i + 1) % pts.size()]);
        dl->AddLine(h.toScreen(a.x, a.y), h.toScreen(b.x, b.y), line, 1);
      }
    }
    Hit hot = hitAt(h, at);
    for(size_t si = 0; si < paths.size(); si++)
      for(size_t ni = 0; ni < paths[si].nodes.size(); ni++) {
        const PathNode& n = paths[si].nodes[ni];
        ImVec2 p = h.toScreen(o->xf.apply(n.p).x, o->xf.apply(n.p).y);
        bool picked = std::find(_nodes.begin(), _nodes.end(), Key{ si, ni }) != _nodes.end();
        if(picked) {
          for(const Point* hd : { &n.in, &n.out }) {
            if(hd->x == n.p.x && hd->y == n.p.y) continue;
            Point w = o->xf.apply(*hd);
            ImVec2 q = h.toScreen(w.x, w.y);
            dl->AddLine(p, q, IM_COL32(255, 255, 255, 200), 1);
            dl->AddCircleFilled(q, 3.5f * dpi, IM_COL32(255, 255, 255, 230));
            dl->AddCircle(q, 3.5f * dpi, IM_COL32(0, 0, 0, 200));
          }
        }
        // Corners are squares, smooth nodes round; picked nodes are filled with the accent
        float r = 4 * dpi;
        bool hotNode = hot.kind == EHit::Node && hot.sub == si && hot.node == ni;
        ImU32 fill = picked ? ImGui::GetColorU32(ImGuiCol_SliderGrab) : hotNode ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 230, 255);
        if(n.type == ENodeType::Corner) {
          dl->AddRectFilled({ p.x - r, p.y - r }, { p.x + r, p.y + r }, IM_COL32(0, 0, 0, 200));
          dl->AddRectFilled({ p.x - r + 1.5f, p.y - r + 1.5f }, { p.x + r - 1.5f, p.y + r - 1.5f }, fill);
        } else {
          dl->AddCircleFilled(p, r + 0.5f, IM_COL32(0, 0, 0, 200));
          dl->AddCircleFilled(p, r - 1, fill);
        }
      }
    if(_drag == EDrag::Marquee) {
      ImVec2 a = h.toScreen(_start.x, _start.y), e = h.toScreen(_end.x, _end.y);
      dl->AddRect({ std::min(a.x, e.x), std::min(a.y, e.y) }, { std::max(a.x, e.x), std::max(a.y, e.y) }, IM_COL32(120, 200, 255, 200));
    }
  }

  void options(ToolHost& h) override {
    if(!h.vectorMode()) return;
    const VecObject* o = target(objectsOf(h), h);
    if(!o) { ImGui::TextDisabled("%s", tr("Click a vector object to edit its nodes")); return; }
    float dpi = h.dpi();
    auto button = [&](const char* id, const char* ic, const char* tip, bool on = false) {
      bool r = ui::iconButton(id, ic, { 0, 0 }, on, tr(tip));
      ImGui::SameLine(0, 2 * dpi);
      return r;
    };
    ENodeType cur = ENodeType::Corner;
    if(!_nodes.empty() && o->kind == EVecKind::Path && _nodes[0].sub < o->paths.size() && _nodes[0].node < o->paths[_nodes[0].sub].nodes.size())
      cur = o->paths[_nodes[0].sub].nodes[_nodes[0].node].type;
    if(button("##corner", ICON_TB_ANGLE, N_("Corner node"), cur == ENodeType::Corner)) setType(h, ENodeType::Corner);
    if(button("##smooth", ICON_TB_WAVE_SINE, N_("Smooth node"), cur == ENodeType::Smooth)) setType(h, ENodeType::Smooth);
    if(button("##sym", ICON_TB_VECTOR_BEZIER_2, N_("Symmetric node"), cur == ENodeType::Symmetric)) setType(h, ENodeType::Symmetric);
    if(button("##delnode", ICON_FA_MINUS, N_("Delete nodes (Del)"))) removeNodes(h);
    bool closed = o->kind == EVecKind::Path && !o->paths.empty() && o->paths[0].closed;
    if(button("##close", closed ? ICON_FA_LINK_SLASH : ICON_FA_LINK, closed ? N_("Open path") : N_("Close path")))
      change(h, closed ? N_("Open Path") : N_("Close Path"), [&](VecObject& t) { for(auto& s : t.paths) s.closed = !closed; });
    ImGui::NewLine();
  }

private:
  enum class EHit : uint8_t { None, Node, HandleIn, HandleOut, Segment };
  enum class EDrag : uint8_t { None, Nodes, Handle, Segment, Marquee };
  struct Key {
    size_t sub, node;
    bool operator==(const Key&) const = default;
  };
  struct Hit { EHit kind = EHit::None; size_t sub = 0, node = 0; double t = 0; };

  std::vector<Key> _nodes;
  EDrag _drag = EDrag::None;
  Hit _hit;
  Point _start{}, _end{};
  std::unique_ptr<VectorChange> _change;
  std::vector<Subpath> _startPaths;

  static std::vector<VecObject>& objectsOf(ToolHost& h) { return vectorTree(h.doc().active(), h.doc().active().frame()).objects; }

  // The object being edited: the first selected one, if it isn't a group
  template<class V> static auto target(V& objs, ToolHost& h) -> decltype(&objs[0]) {
    auto& sel = h.doc().selectedObjects;
    if(sel.empty()) return nullptr;
    for(auto& o : objs) if(o.id == sel[0] && o.kind != EVecKind::Group) return &o;
    return nullptr;
  }

  Hit hitAt(ToolHost& h, const ToolInput& in) {
    Hit out;
    const VecObject* o = target(objectsOf(h), h);
    if(!o) return out;
    float dpi = h.dpi();
    std::vector<Subpath> paths = o->kind == EVecKind::Path ? o->paths : outline(*o);
    auto screen = [&](Point p) { Point w = o->xf.apply(p); return h.toScreen(w.x, w.y); };
    // Handles of the picked nodes first, then nodes, then segments
    for(auto& k : _nodes) {
      if(k.sub >= paths.size() || k.node >= paths[k.sub].nodes.size()) continue;
      const PathNode& n = paths[k.sub].nodes[k.node];
      if((n.out.x != n.p.x || n.out.y != n.p.y) && overNub(in, screen(n.out), dpi)) return { EHit::HandleOut, k.sub, k.node };
      if((n.in.x != n.p.x || n.in.y != n.p.y) && overNub(in, screen(n.in), dpi)) return { EHit::HandleIn, k.sub, k.node };
    }
    for(size_t si = 0; si < paths.size(); si++)
      for(size_t ni = 0; ni < paths[si].nodes.size(); ni++)
        if(overNub(in, screen(paths[si].nodes[ni].p), dpi)) return { EHit::Node, si, ni };
    double tol = 5 * dpi;
    for(size_t si = 0; si < paths.size(); si++) {
      const Subpath& s = paths[si];
      size_t n = s.nodes.size(), segs = s.closed ? n : n - 1;
      for(size_t i = 0; i < segs && n > 1; i++) {
        const PathNode &a = s.nodes[i], &b = s.nodes[(i + 1) % n];
        for(int k = 0; k <= 24; k++) {
          double t = k / 24.0, u = 1 - t;
          Point q{ u * u * u * a.p.x + 3 * u * u * t * a.out.x + 3 * u * t * t * b.in.x + t * t * t * b.p.x,
                   u * u * u * a.p.y + 3 * u * u * t * a.out.y + 3 * u * t * t * b.in.y + t * t * t * b.p.y };
          ImVec2 sq = screen(q);
          if(std::hypot(sq.x - in.sx, sq.y - in.sy) <= tol) return { EHit::Segment, si, i, t };
        }
      }
    }
    return out;
  }

  // Starts a drag on the edited object; a shape is turned into a path first
  void begin(ToolHost& h, EDrag drag, const Hit& hit) {
    _change = std::make_unique<VectorChange>(h.doc());
    VecObject* o = target(_change->tree().objects, h);
    if(!o) { _change.reset(); return; }
    toPath(*o);
    _startPaths = o->paths;
    _drag = drag;
    _hit = hit;
  }

  template<class F> void change(ToolHost& h, const char* name, F fn) {
    VectorChange ch(h.doc());
    VecObject* o = target(ch.tree().objects, h);
    if(!o) return;
    toPath(*o);
    fn(*o);
    ch.tree().touch();
    ch.commit(name, icon);
  }

  void setType(ToolHost& h, ENodeType type) {
    change(h, N_("Node Type"), [&](VecObject& o) {
      for(auto& k : _nodes) {
        if(k.sub >= o.paths.size() || k.node >= o.paths[k.sub].nodes.size()) continue;
        PathNode& n = o.paths[k.sub].nodes[k.node];
        n.type = type;
        if(type == ENodeType::Corner) continue;
        // Line up the handles through the node (straight nodes get handles a third of the way to their neighbors)
        const Subpath& s = o.paths[k.sub];
        size_t cnt = s.nodes.size();
        Point prev = s.nodes[(k.node + cnt - 1) % cnt].p, next = s.nodes[(k.node + 1) % cnt].p;
        double dx = next.x - prev.x, dy = next.y - prev.y, len = std::hypot(dx, dy);
        if(len < 1e-9) continue;
        double li = std::hypot(n.in.x - n.p.x, n.in.y - n.p.y), lo = std::hypot(n.out.x - n.p.x, n.out.y - n.p.y);
        if(li < 1e-9) li = std::hypot(n.p.x - prev.x, n.p.y - prev.y) / 3;
        if(lo < 1e-9) lo = std::hypot(next.x - n.p.x, next.y - n.p.y) / 3;
        if(type == ENodeType::Symmetric) li = lo = (li + lo) / 2;
        n.in = { n.p.x - dx / len * li, n.p.y - dy / len * li };
        n.out = { n.p.x + dx / len * lo, n.p.y + dy / len * lo };
      }
    });
  }

  void removeNodes(ToolHost& h) {
    change(h, N_("Delete Nodes"), [&](VecObject& o) {
      std::vector<Key> keys = _nodes;
      std::sort(keys.begin(), keys.end(), [](const Key& a, const Key& b) { return a.sub != b.sub ? a.sub > b.sub : a.node > b.node; });
      for(auto& k : keys)
        if(k.sub < o.paths.size() && k.node < o.paths[k.sub].nodes.size()) o.paths[k.sub].nodes.erase(o.paths[k.sub].nodes.begin() + k.node);
      std::erase_if(o.paths, [](const Subpath& s) { return s.nodes.size() < 2; });
    });
    _nodes.clear();
    // A path with no segments left goes away
    VectorChange ch(h.doc());
    auto& objs = ch.tree().objects;
    if(std::erase_if(objs, [](const VecObject& o) { return o.kind == EVecKind::Path && o.paths.empty(); })) {
      ch.tree().touch();
      ch.commit(N_("Delete Objects"), ICON_FA_TRASH);
      h.doc().selectedObjects.clear();
    }
  }

  // Splits the segment at t (de Casteljau), so the curve keeps its shape
  void insertNode(ToolHost& h, const Hit& hit) {
    change(h, N_("Add Node"), [&](VecObject& o) {
      Subpath& s = o.paths[hit.sub];
      size_t n = s.nodes.size();
      PathNode& a = s.nodes[hit.node];
      PathNode& b = s.nodes[(hit.node + 1) % n];
      double t = hit.t;
      auto lerp = [](Point p, Point q, double t) { return Point{ p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t }; };
      Point p01 = lerp(a.p, a.out, t), p12 = lerp(a.out, b.in, t), p23 = lerp(b.in, b.p, t);
      Point p012 = lerp(p01, p12, t), p123 = lerp(p12, p23, t), m = lerp(p012, p123, t);
      bool straight = a.out.x == a.p.x && a.out.y == a.p.y && b.in.x == b.p.x && b.in.y == b.p.y;
      PathNode mid = straight ? PathNode::corner(m) : PathNode{ m, p012, p123, ENodeType::Smooth };
      if(!straight) { a.out = p01; b.in = p23; }
      s.nodes.insert(s.nodes.begin() + hit.node + 1, mid);
    });
    _nodes = { { hit.sub, hit.node + 1 } };
  }
};
#pragma endregion

std::unique_ptr<Tool> makeMouse() { return std::make_unique<MouseTool>(); }
std::unique_ptr<Tool> makeNode() { return std::make_unique<NodeTool>(); }

}
