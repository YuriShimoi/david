#include "ui/tools/Tool.h"
#include "core/Actions.h"
#include "core/Document.h"
#include "core/Raster.h"
#include "core/TileEdit.h"
#include "ui/CanvasNav.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>

namespace david {

static ECombine modeFor(const ToolSettings& s, const ToolInput& in) {
  bool right = in.button == SDL_BUTTON_RIGHT;
  if(in.ctrl) return right ? ECombine::Xor : ECombine::Union;
  if(in.alt) return right ? ECombine::Intersect : ECombine::Exclude;
  return s.selectMode;
}

static void applySelection(Document& doc, std::unique_ptr<Mask> shape, ECombine mode, const char* name, const char* icon) {
  if(mode != ECombine::Replace) {
    auto next = doc.selection->clone();
    next->combine(*shape, mode);
    shape = std::move(next);
  }
  pushSelection(doc, std::move(shape), name, icon);
}

// A finished selection hands over to Move Selected Pixels, unless Ctrl/Alt (combining) was held
static void selected(ToolHost& h, const ToolInput* in) {
  if(!h.settings().selectThenMove || !h.doc().hasSelection()) return;
  if(in && (in->ctrl || in->alt)) return;
  h.useTool(ETool::MovePixels);
}

static void selectModeOptions(ToolHost& h) {
  struct Mode { ECombine m; const char* icon; const char* tip; };
  static const Mode modes[] = {
    { ECombine::Replace, ICON_PH_SQUARE, N_("Replace") },
    { ECombine::Union, ICON_PH_UNITE_SQUARE, N_("Union (Ctrl)") },
    { ECombine::Exclude, ICON_PH_SUBTRACT_SQUARE, N_("Exclude (Alt)") },
    { ECombine::Intersect, ICON_PH_INTERSECT_SQUARE, N_("Intersect (Alt + right click)") },
    { ECombine::Xor, ICON_PH_EXCLUDE_SQUARE, N_("Xor (Ctrl + right click)") },
  };
  for(auto& m : modes) {
    bool on = h.settings().selectMode == m.m;
    if(ui::iconToggle(m.icon, m.icon, on, tr(m.tip))) h.settings().selectMode = m.m;
    ImGui::SameLine(0, 2);
  }
}

static void antsPolyline(ToolHost& h, ImDrawList* dl, const std::vector<Point>& pts, bool closed) {
  float phase = float(ImGui::GetTime() * 8);
  for(size_t i = 0; i + 1 < pts.size() + (closed ? 1 : 0); i++) {
    const Point& a = pts[i];
    const Point& b = pts[(i + 1) % pts.size()];
    antsLine(dl, h.toScreen(a.x, a.y), h.toScreen(b.x, b.y), phase, h.dpi());
  }
}

#pragma region [ Rectangle, ellipse, lasso ]
enum class EShape { Rect, Ellipse, Lasso };

class ShapeSelectTool : public Tool {
public:
  ShapeSelectTool(ETool id, const char* icon, const char* name, EShape shape) : Tool(id, icon, name, ImGuiKey_S), _shape(shape) {}

  void down(ToolHost& h, const ToolInput& in) override {
    _active = true;
    _mode = modeFor(h.settings(), in);
    _start = _cur = { in.x, in.y };
    _shift = in.shift;
    _pts = { _start };
    _moved = false;
    // A double-click with the grid on selects the grid cell under the pointer
    auto& st = h.settings();
    _cell = in.doubleClick && st.grid && _shape == EShape::Rect;
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(!_active) return;
    _cur = { in.x, in.y };
    _shift = in.shift;
    double z = h.nav().view.zoom;
    if(std::abs(_cur.x - _start.x) * z > 2 || std::abs(_cur.y - _start.y) * z > 2) _moved = true;
    if(_shape == EShape::Lasso) {
      const Point& last = _pts.back();
      if(std::abs(last.x - _cur.x) * z >= 1 || std::abs(last.y - _cur.y) * z >= 1) _pts.push_back(_cur);
    }
  }

  void up(ToolHost& h, const ToolInput& in) override {
    if(!_active) return;
    _active = false;
    Document& doc = h.doc();
    if(!_moved && _cell) {
      auto& st = h.settings();
      double cx = std::floor((in.x - st.gridX) / st.gridW) * st.gridW + st.gridX, cy = std::floor((in.y - st.gridY) / st.gridH) * st.gridH + st.gridY;
      applySelection(doc, rectMask(doc.width(), doc.height(), cx, cy, cx + st.gridW, cy + st.gridH), _mode, name, icon);
      selected(h, &in);
      return;
    }
    // A click without a drag deselects, like Paint.NET
    if(!_moved) {
      if(_mode == ECombine::Replace) pushSelection(doc, std::make_unique<Mask>(doc.width(), doc.height()), N_("Deselect"), ICON_FA_XMARK);
      return;
    }
    std::unique_ptr<Mask> m;
    if(_shape == EShape::Lasso) m = polygonMask(doc.width(), doc.height(), _pts);
    else {
      Point b = constrained();
      double x0 = _start.x, y0 = _start.y, x1 = b.x, y1 = b.y;
      if(_shape == EShape::Rect) {
        // Snap to pixel edges, so a rectangle selection is always crisp
        x0 = std::round(x0); y0 = std::round(y0); x1 = std::round(x1); y1 = std::round(y1);
        m = rectMask(doc.width(), doc.height(), x0, y0, x1, y1);
      } else {
        m = ellipseMask(doc.width(), doc.height(), x0, y0, x1, y1);
      }
    }
    applySelection(doc, std::move(m), _mode, name, icon);
    selected(h, &in);
  }

  bool busy() const override { return _active; }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput&) override {
    if(!_active || !_moved) return;
    if(_shape == EShape::Lasso) { antsPolyline(h, dl, _pts, true); return; }
    Point b = constrained();
    std::vector<Point> pts;
    if(_shape == EShape::Rect) {
      double x0 = std::round(_start.x), y0 = std::round(_start.y), x1 = std::round(b.x), y1 = std::round(b.y);
      pts = { { x0, y0 }, { x1, y0 }, { x1, y1 }, { x0, y1 } };
    } else {
      double cx = (_start.x + b.x) / 2, cy = (_start.y + b.y) / 2, rx = std::abs(b.x - _start.x) / 2, ry = std::abs(b.y - _start.y) / 2;
      for(int i = 0; i < 72; i++) pts.push_back({ cx + rx * std::cos(i * 6.2831853 / 72), cy + ry * std::sin(i * 6.2831853 / 72) });
    }
    antsPolyline(h, dl, pts, true);
  }

  void options(ToolHost& h) override { selectModeOptions(h); }

private:
  EShape _shape;
  bool _cell = false;
  bool _active = false, _moved = false, _shift = false;
  ECombine _mode = ECombine::Replace;
  Point _start{}, _cur{};
  std::vector<Point> _pts;

  // Shift keeps a square or circle
  Point constrained() const {
    if(!_shift) return _cur;
    double dx = _cur.x - _start.x, dy = _cur.y - _start.y, s = std::max(std::abs(dx), std::abs(dy));
    return { _start.x + (dx < 0 ? -s : s), _start.y + (dy < 0 ? -s : s) };
  }
};
#pragma endregion

#pragma region [ Magic wand ]
// Selects by color with the OKLab flood engine. Shift = global; Ctrl/Alt combine like the other selection tools.
class WandTool : public Tool {
public:
  WandTool() : Tool(ETool::MagicWand, ICON_TB_WAND, N_("Magic Wand"), ImGuiKey_S) {}

  void down(ToolHost& h, const ToolInput& in) override {
    Document& doc = h.doc();
    auto& s = h.settings();
    int x = int(std::floor(in.x)), y = int(std::floor(in.y));
    if(x < 0 || y < 0 || x >= doc.width() || y >= doc.height()) return;
    FloodOptions o = floodOptions(s, s.wandGlobal != in.shift, 0);
    o.toOutline = false;
    auto m = floodMask(doc, s.sampleImage ? nullptr : &doc.active(), x, y, o);
    applySelection(doc, std::move(m), modeFor(s, in), name, icon);
    selected(h, &in);
  }

  void options(ToolHost& h) override {
    selectModeOptions(h);
    ImGui::SameLine();
    floodOptionsBar(h, h.settings().wandGlobal, false);
  }

  ECursor cursor(ToolHost&, const ToolInput&) override { return ECursor::Cross; }
};
#pragma endregion

#pragma region [ Polygonal lasso ]
// Click to add corners; click the first point, double-click or Enter to close; Backspace removes the last one
class PolyLassoTool : public Tool {
public:
  PolyLassoTool() : Tool(ETool::PolyLasso, ICON_TB_LASSO_POLYGON, N_("Polygon Lasso"), ImGuiKey_S) {}

  void down(ToolHost& h, const ToolInput& in) override {
    Point p{ in.x, in.y };
    if(_pts.empty()) {
      _mode = modeFor(h.settings(), in);
      _pts.push_back(p);
      return;
    }
    ImVec2 first = h.toScreen(_pts[0].x, _pts[0].y);
    float dx = first.x - in.sx, dy = first.y - in.sy;
    if(in.doubleClick || (_pts.size() >= 3 && dx * dx + dy * dy < 64 * h.dpi() * h.dpi())) { finish(h, &in); return; }
    _pts.push_back(p);
  }

  bool busy() const override { return !_pts.empty(); }

  void commit(ToolHost& h) override {
    if(_pts.size() >= 3) {
      Document& doc = h.doc();
      applySelection(doc, polygonMask(doc.width(), doc.height(), _pts), _mode, name, icon);
    }
    _pts.clear();
  }

  void cancel(ToolHost&) override { _pts.clear(); }

  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(_pts.empty()) return false;
    if(k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter) { finish(h, nullptr); return true; }
    if(k == ImGuiKey_Escape) { cancel(h); return true; }
    if(k == ImGuiKey_Backspace) { _pts.pop_back(); return true; }
    return false;
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    if(_pts.empty()) return;
    auto pts = _pts;
    pts.push_back({ at.x, at.y });
    antsPolyline(h, dl, pts, false);
    ImVec2 first = h.toScreen(_pts[0].x, _pts[0].y);
    dl->AddCircleFilled(first, 4 * h.dpi(), ImGui::GetColorU32(ImGuiCol_SliderGrab));
  }

  void options(ToolHost& h) override { selectModeOptions(h); }

private:
  std::vector<Point> _pts;
  ECombine _mode = ECombine::Replace;

  // Closed by the user (not by a tool switch, which also commits)
  void finish(ToolHost& h, const ToolInput* in) {
    bool closed = _pts.size() >= 3;
    commit(h);
    if(closed) selected(h, in);
  }
};
#pragma endregion

#pragma region [ Move ]
// Move Selected Pixels (pixels = true) or Move Selection. Left drag moves; the round handle above the box (or a
// right drag) rotates, Shift snaps to 15°; the corner nubs resize, Shift keeps the proportions. Angle and scale
// can also be typed on the tool bar. Ctrl at the start of a session leaves a copy. The session stays open across
// drags until committed.
class MoveTool : public Tool {
public:
  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(_dragging) return _rotating || _corner >= 0 ? ECursor::Pointer : ECursor::Move;
    return overHandle(h, at) || cornerAt(h, at) >= 0 ? ECursor::Pointer : ECursor::Move;
  }
  MoveTool(bool pixels)
    : Tool(pixels ? ETool::MovePixels : ETool::MoveSelection, pixels ? ICON_TB_ARROWS_MOVE : ICON_TB_DRAG_DROP,
           pixels ? N_("Move Selected Pixels") : N_("Move Selection"), ImGuiKey_M), _pixels(pixels) {}

  void down(ToolHost& h, const ToolInput& in) override {
    bool handle = overHandle(h, in);
    int corner = handle || in.button == SDL_BUTTON_RIGHT ? -1 : cornerAt(h, in);
    if(!_session && !begin(h, in.ctrl)) return;
    _dragging = true;
    _corner = corner;
    _rotating = corner < 0 && (handle || in.button == SDL_BUTTON_RIGHT);
    _startXf = _xf;
    _sx = in.x; _sy = in.y;
    Point c = center();
    _startAngle = std::atan2(in.y - c.y, in.x - c.x);
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(!_dragging) return;
    if(_corner >= 0) {
      // Grid snap: the dragged corner lands on the grid
      ToolInput at = in;
      snapToGrid(h.settings(), at.x, at.y);
      resize(at);
    } else if(_rotating) {
      Point c = center();
      double a = _startXf.angle + std::atan2(in.y - c.y, in.x - c.x) - _startAngle;
      if(in.shift) { double step = 3.14159265358979 / 12; a = std::round(a / step) * step; }
      _xf.angle = a;
    } else {
      // Whole-pixel steps keep unrotated pixels crisp
      double mx = std::round(in.x - _sx), my = std::round(in.y - _sy);
      if(in.shift) { if(std::abs(mx) > std::abs(my)) my = 0; else mx = 0; }
      // Grid snap: the box's top-left corner lands on the grid
      if(h.settings().grid && h.settings().gridSnap) {
        Point c[4];
        Xform keep = _xf;
        _xf = _startXf;
        bool ok = box(h, c);
        _xf = keep;
        if(ok) {
          double x0 = std::min({ c[0].x, c[1].x, c[2].x, c[3].x }) + mx, y0 = std::min({ c[0].y, c[1].y, c[2].y, c[3].y }) + my;
          double sx = x0, sy = y0;
          snapToGrid(h.settings(), sx, sy);
          if(!in.shift || mx != 0) mx += sx - x0;
          if(!in.shift || my != 0) my += sy - y0;
        }
      }
      _xf.dx = _startXf.dx + mx;
      _xf.dy = _startXf.dy + my;
    }
    apply(h);
  }

  void up(ToolHost&, const ToolInput&) override { _dragging = false; _corner = -1; }

  bool busy() const override { return _session; }

  void commit(ToolHost& h) override {
    if(!_session) return;
    Document& doc = h.doc();
    auto step = std::make_unique<CompoundAction>();
    step->name = _selBefore ? N_("Paste") : name;
    step->icon = icon;
    if(_rec) if(auto edit = _rec->finish("", "")) step->parts.push_back(std::move(edit));
    // A paste replaced the selection with its box; the step goes back to the one before
    const Mask& before = _selBefore ? *_selBefore : *_base;
    if(_hadSelection && !doc.selection->equals(before)) {
      auto sel = std::make_unique<SelectionEdit>();
      sel->before = before.clone();
      sel->after = doc.selection->clone();
      step->parts.push_back(std::move(sel));
    }
    if(!step->parts.empty()) doc.history.push(doc, std::move(step));
    end();
  }

  void cancel(ToolHost& h) override {
    if(!_session) return;
    if(_rec) _rec->restore();
    if(_hadSelection) h.doc().selection = (_selBefore ? _selBefore : _base)->clone();
    h.doc().onChanged();
    end();
  }

  bool paste(ToolHost& h, Lifted&& px) override {
    if(!_pixels) return false;
    commit(h);
    Document& doc = h.doc();
    _selBefore = doc.selection->clone();
    _hadSelection = true;
    _bounds = px.bounds;
    _base = std::make_unique<Mask>(doc.width(), doc.height());
    for(int y = std::max(0, _bounds.y0); y <= std::min(doc.height() - 1, _bounds.y1); y++) _base->fillSpan(y, _bounds.x0, _bounds.x1, 255);
    _lifted = std::move(px);
    Layer& layer = doc.active();
    _rec = std::make_unique<TileRecorder>(layer);
    _layerId = layer.id();
    _xf = {};
    _xf.cx = (_bounds.x0 + _bounds.x1 + 1) / 2.0;
    _xf.cy = (_bounds.y0 + _bounds.y1 + 1) / 2.0;
    // Nothing under the pasted pixels is lifted, so nothing is erased
    _copy = true;
    _session = true;
    apply(h);
    return true;
  }

  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(!_session) return false;
    if(k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter) { commit(h); return true; }
    if(k == ImGuiKey_Escape) { cancel(h); return true; }
    return false;
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    Point c[4];
    if(!box(h, c)) return;
    ImU32 col = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    for(int i = 0; i < 4; i++) dl->AddLine(h.toScreen(c[i].x, c[i].y), h.toScreen(c[(i + 1) % 4].x, c[(i + 1) % 4].y), col, 1);
    Point m = { (c[0].x + c[2].x) / 2, (c[0].y + c[2].y) / 2 };
    dl->AddCircle(h.toScreen(m.x, m.y), 4 * h.dpi(), col, 0, 1.5f);
    // Rotate handle on a short stem above the top edge
    ImVec2 top = h.toScreen((c[0].x + c[1].x) / 2, (c[0].y + c[1].y) / 2), hd = handlePos(h, c);
    dl->AddLine(top, hd, col, 1);
    drawNub(dl, hd, h.dpi(), (_dragging && _rotating) || overHandle(h, at));
    int hot = _dragging ? _corner : cornerAt(h, at);
    for(int i = 0; i < 4; i++) drawNub(dl, h.toScreen(c[i].x, c[i].y), h.dpi(), hot == i);
  }

  void options(ToolHost& h) override {
    // The angle, typed or nudged; ±90° buttons for quarter turns
    float deg = float(_xf.angle * 180 / 3.14159265358979);
    if(!_session) deg = 0;
    ImGui::TextDisabled(ICON_TB_ROTATE_CLOCKWISE);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * h.dpi());
    bool changed = ImGui::DragFloat("##angle", &deg, 0.5f, -360, 360, "%.1f°");
    ImGui::SetItemTooltip("%s", tr("Rotation angle (drag the round handle, or right-drag; Shift snaps to 15°)"));
    ImGui::SameLine();
    if(ui::iconButton("##ccw", ICON_TB_ROTATE_2, { 0, 0 }, false, tr("Rotate 90° left"))) { deg -= 90; changed = true; }
    ImGui::SameLine(0, 2);
    if(ui::iconButton("##cw", ICON_TB_ROTATE_CLOCKWISE_2, { 0, 0 }, false, tr("Rotate 90° right"))) { deg += 90; changed = true; }
    if(changed && (_session || begin(h, false))) {
      _xf.angle = deg * 3.14159265358979 / 180;
      apply(h);
    }

    // Width and height in percent, around the center
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_TB_ARROWS_DIAGONAL);
    ImGui::SameLine();
    float pct[2] = { float(_session ? _xf.sx * 100 : 100), float(_session ? _xf.sy * 100 : 100) };
    ImGui::SetNextItemWidth(150 * h.dpi());
    if(ImGui::DragFloat2("##scale", pct, 0.5f, -1000, 1000, "%.1f%%") && (_session || begin(h, false))) {
      _xf.sx = std::abs(pct[0]) < 1 ? (pct[0] < 0 ? -0.01 : 0.01) : pct[0] / 100;
      _xf.sy = std::abs(pct[1]) < 1 ? (pct[1] < 0 ? -0.01 : 0.01) : pct[1] / 100;
      apply(h);
    }
    ImGui::SetItemTooltip("%s", tr("Width and height (drag a corner; Shift keeps the proportions)"));
  }

private:
  bool _pixels;
  bool _session = false, _dragging = false, _hadSelection = false, _copy = false;
  std::unique_ptr<Mask> _base, _selBefore;  // _selBefore: the selection before a paste
  IRect _bounds;
  Lifted _lifted;
  std::unique_ptr<TileRecorder> _rec;
  uint64_t _layerId = 0;
  Xform _xf, _startXf;
  bool _rotating = false;
  int _corner = -1;  // the corner nub being dragged
  double _sx = 0, _sy = 0, _startAngle = 0;

  Point center() const { return { _xf.cx + _xf.dx, _xf.cy + _xf.dy }; }

  // The four corners of what would move: the session's transformed box, or the selection (or the whole layer
  // for Move Selected Pixels) before a session starts
  bool box(ToolHost& h, Point c[4]) const {
    IRect b;
    Xform xf;
    if(_session) { b = _bounds; xf = _xf; }
    else {
      Document& doc = h.doc();
      if(doc.hasSelection()) b = doc.selection->bounds();
      else if(_pixels) b = { 0, 0, doc.width() - 1, doc.height() - 1 };
      if(b.empty()) return false;
    }
    c[0] = xf.apply({ double(b.x0), double(b.y0) });
    c[1] = xf.apply({ double(b.x1 + 1), double(b.y0) });
    c[2] = xf.apply({ double(b.x1 + 1), double(b.y1 + 1) });
    c[3] = xf.apply({ double(b.x0), double(b.y1 + 1) });
    return true;
  }

  // 24 screen px out from the top edge's middle, along the box's own "up"
  ImVec2 handlePos(ToolHost& h, const Point c[4]) const {
    ImVec2 top = h.toScreen((c[0].x + c[1].x) / 2, (c[0].y + c[1].y) / 2);
    ImVec2 mid = h.toScreen((c[0].x + c[2].x) / 2, (c[0].y + c[2].y) / 2);
    float dx = top.x - mid.x, dy = top.y - mid.y, len = std::sqrt(dx * dx + dy * dy);
    if(len < 0.001f) { dx = 0; dy = -1; len = 1; }
    float out = 24 * h.dpi();
    return { top.x + dx / len * out, top.y + dy / len * out };
  }

  bool overHandle(ToolHost& h, const ToolInput& in) const {
    Point c[4];
    return box(h, c) && overNub(in, handlePos(h, c), h.dpi());
  }

  // 0-3 clockwise from the top-left of the source box, -1 none
  int cornerAt(ToolHost& h, const ToolInput& in) const {
    Point c[4];
    if(!box(h, c)) return -1;
    for(int i = 0; i < 4; i++) if(overNub(in, h.toScreen(c[i].x, c[i].y), h.dpi())) return i;
    return -1;
  }

  // The dragged corner follows the pointer and the opposite corner stays, in the box's own rotated frame
  void resize(const ToolInput& in) {
    double w = _bounds.width(), hgt = _bounds.height();
    double gx = _corner == 1 || _corner == 2 ? 1 : -1, gy = _corner >= 2 ? 1 : -1;
    Point fixed = _startXf.apply({ _xf.cx - gx * w / 2, _xf.cy - gy * hgt / 2 });
    double cs = std::cos(_xf.angle), sn = std::sin(_xf.angle);
    double px = in.x - fixed.x, py = in.y - fixed.y;
    double vx = px * cs + py * sn, vy = -px * sn + py * cs;
    double sx = vx / (gx * w), sy = vy / (gy * hgt);
    if(in.shift) {
      // Keep the proportions the box had at the press
      double k = std::max(std::abs(sx / _startXf.sx), std::abs(sy / _startXf.sy));
      sx = (sx < 0 ? -k : k) * std::abs(_startXf.sx);
      sy = (sy < 0 ? -k : k) * std::abs(_startXf.sy);
    }
    // Whole-pixel sizes while unrotated, never thinner than a pixel
    auto fit = [&](double s, double len) {
      double px = _xf.angle == 0 ? std::round(s * len) : s * len;
      if(std::abs(px) < 1) px = s < 0 ? -1 : 1;
      return px / len;
    };
    _xf.sx = fit(sx, w);
    _xf.sy = fit(sy, hgt);
    // The center sits halfway between the fixed corner and the dragged one
    double ox = _xf.sx * gx * w / 2, oy = _xf.sy * gy * hgt / 2;
    _xf.dx = fixed.x + ox * cs - oy * sn - _xf.cx;
    _xf.dy = fixed.y + ox * sn + oy * cs - _xf.cy;
  }

  bool begin(ToolHost& h, bool copy) {
    Document& doc = h.doc();
    _hadSelection = doc.hasSelection();
    if(!_hadSelection && !_pixels) return false;
    // With no selection, Move Selected Pixels moves the whole layer
    _base = doc.selection->clone();
    if(!_hadSelection) _base->fillAll();
    _bounds = _base->bounds();
    if(_bounds.empty()) return false;
    if(_pixels) {
      Layer& layer = doc.active();
      _lifted = lift(layer, *_base);
      _rec = std::make_unique<TileRecorder>(layer);
      _layerId = layer.id();
    }
    _xf = {};
    _xf.cx = (_bounds.x0 + _bounds.x1 + 1) / 2.0;
    _xf.cy = (_bounds.y0 + _bounds.y1 + 1) / 2.0;
    _copy = copy;
    _session = true;
    return true;
  }

  void apply(ToolHost& h) {
    Document& doc = h.doc();
    if(_pixels) {
      Layer* layer = doc.findLayer(_layerId);
      if(!layer) return;
      _rec->restore();
      stampLifted(*layer, *_rec, *_base, _lifted, _xf, _copy);
    }
    if(_hadSelection) doc.selection = transformMask(*_base, _xf);
    doc.onChanged();
  }

  void end() {
    _session = _dragging = _rotating = false;
    _corner = -1;
    _base.reset();
    _selBefore.reset();
    _rec.reset();
    _lifted = {};
  }
};
#pragma endregion

#pragma region [ Select ]
// One tool for the four selection shapes; the shape is a tool bar option, and S cycles it before moving on
class SelectTool : public Tool {
public:
  SelectTool() : Tool(ETool::RectSelect, ICON_TB_MARQUEE_2, N_("Rectangle Select"), ImGuiKey_S) {
    _shapes.push_back(std::make_unique<ShapeSelectTool>(ETool::RectSelect, ICON_TB_MARQUEE_2, N_("Rectangle Select"), EShape::Rect));
    _shapes.push_back(std::make_unique<ShapeSelectTool>(ETool::EllipseSelect, ICON_TB_CIRCLE_DASHED, N_("Ellipse Select"), EShape::Ellipse));
    _shapes.push_back(std::make_unique<ShapeSelectTool>(ETool::Lasso, ICON_TB_LASSO, N_("Lasso Select"), EShape::Lasso));
    _shapes.push_back(std::make_unique<PolyLassoTool>());
  }

  void down(ToolHost& h, const ToolInput& in) override { cur(h).down(h, in); }
  void drag(ToolHost& h, const ToolInput& in) override { cur(h).drag(h, in); }
  void up(ToolHost& h, const ToolInput& in) override { cur(h).up(h, in); }
  bool busy() const override { for(auto& t : _shapes) if(t->busy()) return true; return false; }
  void commit(ToolHost& h) override { for(auto& t : _shapes) t->commit(h); }
  void cancel(ToolHost& h) override { for(auto& t : _shapes) t->cancel(h); }
  bool onKey(ToolHost& h, ImGuiKey k) override { return cur(h).onKey(h, k); }
  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override { cur(h).overlay(h, dl, at); }
  ECursor cursor(ToolHost&, const ToolInput&) override { return ECursor::Cross; }

  bool cycle(ToolHost& h) override {
    int& s = h.settings().selectShape;
    if(s + 1 >= int(_shapes.size())) { setShape(h, 0); return false; }
    setShape(h, s + 1);
    return true;
  }

  void options(ToolHost& h) override {
    for(int i = 0; i < int(_shapes.size()); i++) {
      bool on = h.settings().selectShape == i;
      if(ui::iconToggle(_shapes[i]->icon, _shapes[i]->icon, on, tr(_shapes[i]->name))) setShape(h, i);
      ImGui::SameLine(0, 2);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    selectModeOptions(h);
  }

private:
  std::vector<std::unique_ptr<Tool>> _shapes;

  Tool& cur(ToolHost& h) {
    int& s = h.settings().selectShape;
    s = std::clamp(s, 0, int(_shapes.size()) - 1);
    icon = _shapes[s]->icon;
    name = _shapes[s]->name;
    return *_shapes[s];
  }

  void setShape(ToolHost& h, int i) {
    if(i == h.settings().selectShape) return;
    commit(h);
    h.settings().selectShape = i;
    cur(h);
  }
};
#pragma endregion

std::unique_ptr<Tool> makeSelect() { return std::make_unique<SelectTool>(); }
std::unique_ptr<Tool> makeRectSelect() { return std::make_unique<ShapeSelectTool>(ETool::RectSelect, ICON_TB_MARQUEE_2, N_("Rectangle Select"), EShape::Rect); }
std::unique_ptr<Tool> makeEllipseSelect() { return std::make_unique<ShapeSelectTool>(ETool::EllipseSelect, ICON_TB_CIRCLE_DASHED, N_("Ellipse Select"), EShape::Ellipse); }
std::unique_ptr<Tool> makeLasso() { return std::make_unique<ShapeSelectTool>(ETool::Lasso, ICON_TB_LASSO, N_("Lasso Select"), EShape::Lasso); }
std::unique_ptr<Tool> makePolyLasso() { return std::make_unique<PolyLassoTool>(); }
std::unique_ptr<Tool> makeWand() { return std::make_unique<WandTool>(); }
std::unique_ptr<Tool> makeMovePixels() { return std::make_unique<MoveTool>(true); }
std::unique_ptr<Tool> makeMoveSelection() { return std::make_unique<MoveTool>(false); }

std::unique_ptr<Tool> makeMouse(), makeBrush(), makeEraser(), makePencil(), makeBucket(), makePicker(), makeZoom(), makePan(), makeSpray();
std::unique_ptr<Tool> makeGradient(), makeLine(), makeCurve(), makeShapes(), makeText(), makeNode();

std::vector<std::unique_ptr<Tool>> createTools() {
  // Paint.NET's Tools window order, two per row
  std::vector<std::unique_ptr<Tool>> list;
  list.push_back(makeMouse());
  list.push_back(makeSelect());
  list.push_back(makeMovePixels());
  list.push_back(makeMoveSelection());
  list.push_back(makeZoom());
  list.push_back(makePan());
  list.push_back(makeWand());
  list.push_back(makeBucket());
  list.push_back(makeGradient());
  list.push_back(makePicker());
  list.push_back(makeBrush());
  list.push_back(makeEraser());
  list.push_back(makePencil());
  list.push_back(makeSpray());
  list.push_back(makeText());
  list.push_back(makeLine());
  list.push_back(makeCurve());
  list.push_back(makeShapes());
  // Vector layers (hidden unless Beta tools > Vector support is on)
  list.push_back(makeNode());
  return list;
}

}
