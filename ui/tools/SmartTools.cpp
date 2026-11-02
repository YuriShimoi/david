#include "ui/tools/Tool.h"
#include "core/Document.h"
#include "core/Draw.h"
#include "core/Gradient.h"
#include "core/Paint.h"
#include "core/Stroke.h"
#include "ui/tools/VectorTools.h"
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

static constexpr double PI = 3.14159265358979323846;

static bool toggle(const char* icon, bool& v, const char* tip) {
  return ui::iconToggle(icon, icon, v, tr(tip));
}

// Shift: the angle from origin snaps to 15° steps
static Point snap15(Point origin, Point p) {
  double dx = p.x - origin.x, dy = p.y - origin.y, len = std::hypot(dx, dy);
  double a = std::round(std::atan2(dy, dx) / (PI / 12)) * (PI / 12);
  return { origin.x + len * std::cos(a), origin.y + len * std::sin(a) };
}

static bool combo(const char* id, int& v, std::initializer_list<const char*> items, float width, const char* tip) {
  std::vector<const char*> names;
  for(const char* s : items) names.push_back(tr(s));
  ImGui::SetNextItemWidth(width);
  bool changed = ImGui::Combo(id, &v, names.data(), int(names.size()));
  ImGui::SetItemTooltip("%s", tr(tip));
  return changed;
}

static void brushWidth(ToolHost& h, const char* tip) {
  ImGui::TextDisabled(ICON_TB_GRIP_HORIZONTAL);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(110 * h.dpi());
  ImGui::SliderInt("##width", &h.settings().brushSize, 1, 200, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetItemTooltip("%s  [ ]", tr(tip));
}

#pragma region [ Gradient ]
// Drag from the start to the end color; the two nubs and the stop bar stay editable until Enter or another edit
class GradientTool : public Tool {
public:
  GradientTool() : Tool(ETool::Gradient, ICON_PH_GRADIENT, N_("Gradient"), ImGuiKey_G) {
    _stops = { { 0, 0 }, { 1, 0 } };
  }

  void down(ToolHost& h, const ToolInput& in) override {
    if(_edit.active()) {
      for(int i = 0; i < 2; i++)
        if(overNub(in, h.toScreen(end(i).x, end(i).y), h.dpi())) { _drag = i; return; }
      commit(h);
    }
    syncEnds(h, in.button == SDL_BUTTON_RIGHT);
    _a = _b = { in.x, in.y };
    _drag = 1;
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(_drag < 0) return;
    Point p{ in.x, in.y };
    if(in.shift) p = snap15(end(1 - _drag), p);
    (_drag == 0 ? _a : _b) = p;
    render(h);
  }

  void up(ToolHost& h, const ToolInput&) override {
    _drag = -1;
    if(_edit.active() && std::hypot(_b.x - _a.x, _b.y - _a.y) * h.nav().view.zoom < 2) cancel(h);
  }

  bool busy() const override { return _edit.active() || _drag >= 0; }
  void commit(ToolHost& h) override { _edit.commit(h.doc(), name, icon); _drag = -1; }

  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(_drag >= 0) return ECursor::Move;
    if(_edit.active())
      for(int i = 0; i < 2; i++) if(overNub(at, h.toScreen(end(i).x, end(i).y), h.dpi())) return ECursor::Move;
    return ECursor::Cross;
  }
  void cancel(ToolHost& h) override { _edit.cancel(); _drag = -1; h.doc().onChanged(); }

  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(!_edit.active()) return false;
    if(k == ImGuiKey_Escape) cancel(h);
    else if(k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter) commit(h);
    else return false;
    return true;
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    if(!_edit.active()) return;
    ImVec2 a = h.toScreen(_a.x, _a.y), b = h.toScreen(_b.x, _b.y);
    antsLine(dl, a, b, 0, h.dpi());
    drawNub(dl, a, h.dpi(), overNub(at, a, h.dpi()));
    drawNub(dl, b, h.dpi(), overNub(at, b, h.dpi()));
  }

  void options(ToolHost& h) override {
    auto& s = h.settings();
    float dpi = h.dpi();
    bool changed = false;
    changed |= combo("##type", s.gradType, { N_("Linear"), N_("Linear reflected"), N_("Diamond"), N_("Radial"), N_("Conical"),
                                             N_("Spiral clockwise"), N_("Spiral counterclockwise") }, 150 * dpi, N_("Gradient type"));
    ImGui::SameLine();
    changed |= combo("##space", s.gradSpace, { N_("sRGB"), N_("Linear RGB"), N_("OKLab"), N_("OKLCH shorter hue"), N_("OKLCH longer hue") },
                     140 * dpi, N_("Interpolation"));
    ImGui::SameLine();
    changed |= toggle(ICON_TB_DROPLET_OFF, s.gradAlpha, N_("Transparency mode"));
    ImGui::SameLine();
    changed |= toggle(ICON_TB_GRID_DOTS, s.gradPixel, N_("Pixel art (stop colors only)"));
    ImGui::SameLine();
    if(!_edit.active() && _drag < 0) syncEnds(h, _reversed);
    changed |= stopBar(h);
    if(changed && _edit.active()) render(h);
  }

private:
  LiveEdit _edit;
  Point _a{}, _b{};
  int _drag = -1, _sel = -1;
  bool _reversed = false;
  std::vector<GradStop> _stops;

  Point end(int i) const { return i == 0 ? _a : _b; }

  // The end stops follow the primary and secondary colors (opaque to clear in transparency mode)
  void syncEnds(ToolHost& h, bool reversed) {
    _reversed = reversed;
    uint32_t c0 = h.color(true), c1 = h.color(false);
    if(h.settings().gradAlpha) { c0 = 0xFFFFFFFF; c1 = 0x00FFFFFF; }
    if(reversed) std::swap(c0, c1);
    _stops.front().color = c0;
    _stops.back().color = c1;
  }

  GradientSpec spec(ToolHost& h) const {
    auto& s = h.settings();
    GradientSpec g;
    g.type = EGradType(std::clamp(s.gradType, 0, int(EGradType::COUNT) - 1));
    g.space = EGradSpace(std::clamp(s.gradSpace, 0, int(EGradSpace::COUNT) - 1));
    g.alphaMode = s.gradAlpha;
    StrokeSpace sp = strokeSpace(s, h.doc());
    g.mirrorX = sp.mirrorX; g.mirrorY = sp.mirrorY; g.axisX = sp.axisX; g.axisY = sp.axisY;
    g.pixelArt = s.gradPixel;
    g.a = _a; g.b = _b;
    g.stops = _stops;
    std::stable_sort(g.stops.begin(), g.stops.end(), [](const GradStop& x, const GradStop& y) { return x.pos < y.pos; });
    return g;
  }

  void render(ToolHost& h) {
    Document& doc = h.doc();
    TileRecorder& rec = _edit.redraw(doc);
    if(std::hypot(_b.x - _a.x, _b.y - _a.y) >= 0.5) renderGradient(doc, rec, spec(h));
    doc.onChanged();
  }

  // The gradient preview with a nub per stop: click adds a stop, drag moves one, drag off removes one,
  // double-click edits its color (the end stops edit the primary and secondary colors)
  bool stopBar(ToolHost& h) {
    float dpi = h.dpi(), w = 180 * dpi, bh = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##stops", { w, bh });
    ImGui::SetItemTooltip("%s", tr("Click to add a stop, drag it off to remove, double-click to change its color"));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    GradientSpec g = spec(h);
    g.space = EGradSpace(std::clamp(h.settings().gradSpace, 0, int(EGradSpace::COUNT) - 1));
    std::vector<float> table = gradientTable(g);
    float barH = bh * 0.6f;
    // Checker under the colors, so transparency shows
    for(int i = 0; i * 6 * dpi < w; i++)
      for(int j = 0; j * 6 * dpi < barH; j++)
        dl->AddRectFilled({ p.x + i * 6 * dpi, p.y + j * 6 * dpi }, { std::min(p.x + (i + 1) * 6 * dpi, p.x + w), std::min(p.y + (j + 1) * 6 * dpi, p.y + barH) },
                          (i + j) % 2 ? IM_COL32(200, 200, 200, 255) : IM_COL32(255, 255, 255, 255));
    int steps = 64;
    for(int i = 0; i < steps; i++) {
      const float* v = &table[(i * 4095 / (steps - 1)) * 4];
      dl->AddRectFilled({ p.x + w * i / steps, p.y }, { p.x + w * (i + 1) / steps + 1, p.y + barH }, IM_COL32(int(v[0]), int(v[1]), int(v[2]), int(v[3])));
    }
    auto nubX = [&](const GradStop& s) { return p.x + s.pos * w; };
    for(int i = 0; i < int(_stops.size()); i++) {
      float x = nubX(_stops[i]);
      ImVec2 a{ x, p.y + barH }, b{ x - 5 * dpi, p.y + bh }, c{ x + 5 * dpi, p.y + bh };
      uint32_t col = _stops[i].color | 0xFF000000;
      dl->AddTriangleFilled(a, b, c, IM_COL32((col) & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, 255));
      dl->AddTriangle(a, b, c, i == _sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 200), i == _sel ? 2.f : 1.f);
    }

    bool changed = false;
    ImVec2 m = ImGui::GetIO().MousePos;
    if(ImGui::IsItemActivated()) {
      _sel = -1;
      for(int i = 0; i < int(_stops.size()); i++)
        if(std::abs(m.x - nubX(_stops[i])) <= 6 * dpi) _sel = i;
      if(_sel < 0) {
        float pos = std::clamp((m.x - p.x) / w, 0.f, 1.f);
        const float* v = &table[int(pos * 4095) * 4];
        uint32_t c = uint32_t(v[0]) | uint32_t(v[1]) << 8 | uint32_t(v[2]) << 16 | uint32_t(v[3]) << 24;
        _stops.insert(_stops.end() - 1, { pos, c });
        _sel = int(_stops.size()) - 2;
        changed = true;
      }
      if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && _sel >= 0) ImGui::OpenPopup("##stopColor");
    }
    bool middle = _sel > 0 && _sel < int(_stops.size()) - 1;
    if(ImGui::IsItemActive() && middle && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      if(m.y > p.y + bh + 24 * dpi) {
        _stops.erase(_stops.begin() + _sel);
        _sel = -1;
      } else {
        _stops[_sel].pos = std::clamp((m.x - p.x) / w, 0.f, 1.f);
      }
      changed = true;
    }
    if(ImGui::BeginPopup("##stopColor")) {
      if(_sel >= 0 && _sel < int(_stops.size())) {
        uint32_t& c = _stops[_sel].color;
        float f[4] = { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f };
        if(ImGui::ColorPicker4("##pick", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview)) {
          c = 0;
          for(int k = 0; k < 4; k++) c |= uint32_t(f[k] * 255 + 0.5f) << (k * 8);
          // The end stops are the primary and secondary colors
          if(!h.settings().gradAlpha) {
            if(_sel == 0) h.color(!_reversed) = c;
            if(_sel == int(_stops.size()) - 1) h.color(_reversed) = c;
          }
          changed = true;
        }
      }
      ImGui::EndPopup();
    }
    return changed;
  }
};
#pragma endregion

#pragma region [ Line and curve ]
// Line: drag; Shift snaps to 15°, Alt draws from the center. In curved mode a middle nub bends it.
// Curve: the first drag places two corner nodes like a line; then a click adds a corner, a click-drag a smooth node
// (Alt-drag breaks the handle symmetry), Backspace removes the last node, double-click or Enter finishes and
// clicking the first node closes the path. Both stay editable (drag the nubs) until Enter, a click away or another edit.
class PathTool : public Tool {
public:
  explicit PathTool(bool curve)
    : Tool(curve ? ETool::Curve : ETool::Line, curve ? ICON_TB_VECTOR_BEZIER_2 : ICON_TB_LINE, curve ? N_("Curve") : N_("Line"), ImGuiKey_O), _curve(curve) {}

  void down(ToolHost& h, const ToolInput& in) override {
    Point p{ in.x, in.y };
    if(!_nodes.empty() && hit(h, in)) return;
    if(_curve && !_nodes.empty()) {
      if(in.doubleClick) { commit(h); return; }
      _nodes.push_back({ p, {}, {} });
      _dragNode = int(_nodes.size()) - 1; _dragPart = 3;
      render(h);
      return;
    }
    commit(h);
    _color = h.color(in.button != SDL_BUTTON_RIGHT);
    _origin = p;
    _midSet = false;
    _nodes = { { p, {}, {} }, { p, {}, {} } };
    _dragNode = 1; _dragPart = 4;
    render(h);
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(_dragNode < 0) return;
    Point p{ in.x, in.y };
    Node& n = _nodes[_dragNode];
    switch(_dragPart) {
      case 0: {
        // Move the node; Shift snaps it against its neighbor
        if(in.shift && _nodes.size() > 1) p = snap15(_nodes[_dragNode == 0 ? 1 : _dragNode - 1].p, p);
        n.p = p;
        break;
      }
      case 1: case 2: {
        Point& hd = _dragPart == 1 ? n.in : n.out;
        Point& other = _dragPart == 1 ? n.out : n.in;
        hd = { p.x - n.p.x, p.y - n.p.y };
        if(!in.alt) other = { -hd.x, -hd.y };
        break;
      }
      case 3: {
        // A new curve node being dragged out: symmetric handles unless Alt
        Point d{ p.x - n.p.x, p.y - n.p.y };
        if(std::hypot(d.x, d.y) * h.nav().view.zoom < 3) d = {};
        n.out = d;
        if(!in.alt) n.in = { -d.x, -d.y };
        break;
      }
      case 4: {
        // The first drag: a straight segment
        if(in.shift) p = snap15(_origin, p);
        _nodes[1].p = p;
        _nodes[0].p = in.alt ? Point{ 2 * _origin.x - p.x, 2 * _origin.y - p.y } : _origin;
        break;
      }
      case 5: {
        // The line's middle point: a corner, or a point the curve passes through
        _mid = p;
        _midSet = true;
        break;
      }
      case 6: {
        // Closing a curve: dragging from the first node shapes it like a new smooth node
        Point d{ p.x - n.p.x, p.y - n.p.y };
        if(std::hypot(d.x, d.y) * h.nav().view.zoom < 3) d = {};
        n.out = d;
        if(!in.alt) n.in = { -d.x, -d.y };
        break;
      }
    }
    render(h);
  }

  void up(ToolHost& h, const ToolInput&) override {
    bool first = _dragPart == 4;
    bool closing = _dragNode >= 0 && _dragPart == 6;
    _dragNode = -1;
    if(closing) { commit(h); return; }
    if(!first || _nodes.size() < 2) return;
    if(std::hypot(_nodes[1].p.x - _nodes[0].p.x, _nodes[1].p.y - _nodes[0].p.y) * h.nav().view.zoom >= 2) return;
    // A click without a drag: a line is dropped, a curve keeps its first node and goes on with clicks
    if(_curve) { _nodes.pop_back(); render(h); }
    else cancel(h);
  }

  bool busy() const override { return !_nodes.empty(); }

  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(_dragNode >= 0 && _dragPart != 3 && _dragPart != 4) return ECursor::Move;
    if(!_nodes.empty()) {
      int node, part;
      if(nubAt(h, at, node, part)) return node == 0 && part == 0 && _curve && _nodes.size() >= 2 ? ECursor::Pointer : ECursor::Move;
    }
    return ECursor::Cross;
  }

  void commit(ToolHost& h) override {
    if(_nodes.empty()) return;
    _edit.commit(h.doc(), name, icon);
    if(_live.active()) _live.commit(h, name, icon);
    _nodes.clear();
    _dragNode = -1;
    _closed = false;
  }

  void cancel(ToolHost& h) override {
    _edit.cancel();
    _live.cancel(h);
    _nodes.clear();
    _dragNode = -1;
    _closed = false;
    h.doc().onChanged();
  }

  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(_nodes.empty()) return false;
    if(k == ImGuiKey_Escape) cancel(h);
    else if(k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter) commit(h);
    else if(k == ImGuiKey_Backspace && _curve) {
      _nodes.pop_back();
      if(_nodes.empty()) cancel(h);
      else render(h);
    }
    else return false;
    return true;
  }

  // Paint.NET's line keys: , and . toggle the arrow heads, / cycles the dash style
  void keys(ToolHost& h) override {
    auto& s = h.settings();
    if(ImGui::GetIO().KeyCtrl) return;
    bool changed = false;
    if(ImGui::IsKeyPressed(ImGuiKey_Comma, false)) { s.arrowStart = !s.arrowStart; changed = true; }
    if(ImGui::IsKeyPressed(ImGuiKey_Period, false)) { s.arrowEnd = !s.arrowEnd; changed = true; }
    if(ImGui::IsKeyPressed(ImGuiKey_Slash, false)) { s.dash = (s.dash + 1) % 3; changed = true; }
    if(changed && !_nodes.empty()) render(h);
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    float dpi = h.dpi();
    int hotNode = -1, hotPart = -1;
    if(!_nodes.empty()) nubAt(h, at, hotNode, hotPart);
    for(int i = 0; i < int(_nodes.size()); i++) {
      const Node& n = _nodes[i];
      ImVec2 p = h.toScreen(n.p.x, n.p.y);
      if(_curve) {
        for(Point hd : { n.in, n.out }) {
          if(hd.x == 0 && hd.y == 0) continue;
          ImVec2 q = h.toScreen(n.p.x + hd.x, n.p.y + hd.y);
          dl->AddLine(p, q, IM_COL32(255, 255, 255, 200), 1);
          dl->AddCircleFilled(q, 3.5f * dpi, IM_COL32(255, 255, 255, 230));
          dl->AddCircle(q, 3.5f * dpi, IM_COL32(0, 0, 0, 200));
        }
      }
      drawNub(dl, p, dpi, hotNode == i && hotPart == 0);
    }
    if(!_curve && _nodes.size() == 2) {
      Point m = mid();
      ImVec2 q = h.toScreen(m.x, m.y);
      // The middle nub is smaller and tinted, so it reads as "shape", not "end"
      dl->AddCircleFilled(q, 5.5f * dpi, IM_COL32(0, 0, 0, 200));
      dl->AddCircleFilled(q, 4 * dpi, hotPart == 5 ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 200, 255, 255));
    }
  }

  void options(ToolHost& h) override {
    auto& s = h.settings();
    bool changed = false;
    if(!_curve) {
      // Off: the line curves through its middle point; on: a sharp corner there
      changed |= toggle(ICON_TB_ANGLE, s.lineSharp, N_("Sharp corner at the middle point"));
      ImGui::SameLine();
    }
    int width = s.brushSize;
    brushWidth(h, N_("Line width"));
    changed |= width != s.brushSize;
    ImGui::SameLine();
    changed |= combo("##dash", s.dash, { N_("Solid"), N_("Dashed"), N_("Dotted") }, 90 * h.dpi(), N_("Dash style ( / )"));
    ImGui::SameLine();
    changed |= toggle(ICON_TB_ARROW_NARROW_LEFT, s.arrowStart, N_("Arrow at start ( , )"));
    ImGui::SameLine();
    changed |= toggle(ICON_TB_ARROW_NARROW_RIGHT, s.arrowEnd, N_("Arrow at end ( . )"));
    changed |= globalsChanged(h);
    if(changed && !_nodes.empty()) render(h);
  }

private:
  // Handles are offsets from the node; zero means a corner
  struct Node { Point p, in, out; };
  const bool _curve;
  LiveEdit _edit;
  LiveObject _live;
  std::vector<Node> _nodes;
  Point _origin{}, _mid{};
  bool _midSet = false;  // the middle point follows the ends until it is dragged
  uint32_t _color = 0;
  bool _closed = false;
  int _dragNode = -1, _dragPart = 0;  // 0 node, 1/2 handles, 3 new curve node, 4 first drag, 5 line middle, 6 closing drag
  std::pair<bool, bool> _globals{};

  Point mid() const { return _midSet ? _mid : Point{ (_nodes[0].p.x + _nodes[1].p.x) / 2, (_nodes[0].p.y + _nodes[1].p.y) / 2 }; }

  // Antialiasing and pixel-perfect live on the global part of the tool bar; a change there redraws too
  bool globalsChanged(ToolHost& h) {
    std::pair<bool, bool> now{ h.settings().antialias, h.settings().pixelPerfect };
    bool changed = now != _globals;
    _globals = now;
    return changed;
  }

  // The nub under the pointer: part 0 node, 1/2 handles, 5 the line's bend nub
  bool nubAt(ToolHost& h, const ToolInput& in, int& node, int& part) {
    float dpi = h.dpi();
    if(!_curve && _nodes.size() == 2) {
      Point m = mid();
      if(overNub(in, h.toScreen(m.x, m.y), dpi)) { node = 0; part = 5; return true; }
    }
    for(int i = int(_nodes.size()) - 1; i >= 0; i--) {
      Node& n = _nodes[i];
      if(_curve) {
        if((n.out.x || n.out.y) && overNub(in, h.toScreen(n.p.x + n.out.x, n.p.y + n.out.y), dpi)) { node = i; part = 2; return true; }
        if((n.in.x || n.in.y) && overNub(in, h.toScreen(n.p.x + n.in.x, n.p.y + n.in.y), dpi)) { node = i; part = 1; return true; }
      }
      if(overNub(in, h.toScreen(n.p.x, n.p.y), dpi)) { node = i; part = 0; return true; }
    }
    return false;
  }

  bool hit(ToolHost& h, const ToolInput& in) {
    int node, part;
    if(!nubAt(h, in, node, part)) return false;
    // Clicking a curve's first node closes the path, like the polygon lasso; a drag from there shapes the
    // closing node, and the release finishes it
    if(_curve && node == 0 && part == 0 && _nodes.size() >= 2) {
      _closed = true;
      _dragNode = 0; _dragPart = 6;
      render(h);
      return true;
    }
    // Double-clicking the last node of a curve finishes it
    if(_curve && part == 0 && in.doubleClick) { commit(h); return true; }
    _dragNode = node; _dragPart = part;
    return true;
  }

  Polyline path(ToolHost& h) const {
    Polyline out;
    if(_nodes.empty()) return out;
    out.push_back(_nodes[0].p);
    if(!_curve) {
      if(_nodes.size() < 2) return out;
      const Point &a = _nodes[0].p, &b = _nodes[1].p;
      Point m = mid();
      if(!h.settings().lineSharp) {
        // A quadratic through the middle point at t = 0.5, written as a cubic
        Point c{ 2 * m.x - (a.x + b.x) / 2, 2 * m.y - (a.y + b.y) / 2 };
        cubicTo(out, a, { a.x + (c.x - a.x) * 2 / 3, a.y + (c.y - a.y) * 2 / 3 }, { b.x + (c.x - b.x) * 2 / 3, b.y + (c.y - b.y) * 2 / 3 }, b);
      } else {
        out.push_back(m);
        out.push_back(b);
      }
      return out;
    }
    auto segment = [&](const Node& a, const Node& b) {
      if(!a.out.x && !a.out.y && !b.in.x && !b.in.y) { out.push_back(b.p); return; }
      cubicTo(out, a.p, { a.p.x + a.out.x, a.p.y + a.out.y }, { b.p.x + b.in.x, b.p.y + b.in.y }, b.p);
    };
    for(size_t i = 0; i + 1 < _nodes.size(); i++) segment(_nodes[i], _nodes[i + 1]);
    if(_closed) segment(_nodes.back(), _nodes.front());
    return out;
  }

  // On a vector layer: the line or curve as a path object with round ends, dashes and arrow heads
  void renderObject(ToolHost& h) {
    auto& s = h.settings();
    VecObject o;
    o.kind = EVecKind::Path;
    o.name = _curve ? N_("Curve") : N_("Line");
    Subpath sp;
    if(!_curve && _nodes.size() == 2) {
      const Point &a = _nodes[0].p, &b = _nodes[1].p;
      Point m = mid();
      if(s.lineSharp) sp.nodes = { PathNode::corner(a), PathNode::corner(m), PathNode::corner(b) };
      else {
        Point c{ 2 * m.x - (a.x + b.x) / 2, 2 * m.y - (a.y + b.y) / 2 };
        sp.nodes = { { a, a, { a.x + (c.x - a.x) * 2 / 3, a.y + (c.y - a.y) * 2 / 3 }, ENodeType::Corner },
                     { b, { b.x + (c.x - b.x) * 2 / 3, b.y + (c.y - b.y) * 2 / 3 }, b, ENodeType::Corner } };
      }
    } else {
      for(auto& n : _nodes) {
        bool smooth = n.in.x || n.in.y || n.out.x || n.out.y;
        sp.nodes.push_back({ n.p, { n.p.x + n.in.x, n.p.y + n.in.y }, { n.p.x + n.out.x, n.p.y + n.out.y }, smooth ? ENodeType::Symmetric : ENodeType::Corner });
      }
      sp.closed = _closed;
    }
    o.paths = { sp };
    double w = std::max(1, s.brushSize);
    Polyline pts = path(h);
    for(bool start : { true, false })
      if((start ? s.arrowStart : s.arrowEnd) && !_closed && pts.size() >= 2) {
        Subpath wing;
        for(auto& q : arrowHead(pts, w, start)) wing.nodes.push_back(PathNode::corner(q));
        o.paths.push_back(wing);
      }
    o.stroke = toolStroke(_color, w, EStrokeAlign::Center);
    o.stroke.cap = ECap::Round;
    o.stroke.join = EJoin::Round;
    if(s.dash == 1) o.stroke.dash = { 4, 2 };
    else if(s.dash == 2) o.stroke.dash = { 0.01, 2 };
    _live.set(h, std::move(o));
  }

  void render(ToolHost& h) {
    if(h.vectorMode()) { if(_nodes.size() >= 2 || _live.active()) renderObject(h); return; }
    Document& doc = h.doc();
    auto& s = h.settings();
    TileRecorder& rec = _edit.redraw(doc);
    Polyline pts = path(h);
    if(pts.size() >= 2) {
      Mask m(doc.width(), doc.height());
      double w = s.pixelPerfect ? 1 : std::max(1, s.brushSize);
      std::vector<Polyline> parts;
      if(s.dash == 1) parts = dashed(pts, 4 * w, 2 * w);
      else if(s.dash == 2) parts = dashed(pts, s.pixelPerfect ? 1 : 0.01, 2 * w);
      else parts = { pts };
      if(s.arrowStart && !_closed) parts.push_back(arrowHead(pts, w, true));
      if(s.arrowEnd && !_closed) parts.push_back(arrowHead(pts, w, false));
      for(auto& part : parts) {
        if(s.pixelPerfect) pixelPolyline(m, part, true);
        else strokePolyline(m, part, w, s.antialias);
      }
      StrokeSpace sp = strokeSpace(s, doc);
      paintCoverage(doc, rec, { { &m, _color } }, &sp);
    }
    doc.onChanged();
  }
};
#pragma endregion

#pragma region [ Shapes ]
// Drag a box; Shift keeps it square. The corners and the body stay draggable until Enter or a click away.
// Outline uses the primary color, fill the secondary one when both are drawn (Paint.NET).
// Free polygon: the outline is drawn freehand, then gets the same box handles.
class ShapesTool : public Tool {
public:
  ShapesTool() : Tool(ETool::Shapes, ICON_TB_TRIANGLE_SQUARE_CIRCLE, N_("Shapes"), ImGuiKey_O) {}

  void down(ToolHost& h, const ToolInput& in) override {
    Point p{ in.x, in.y };
    if(editing()) {
      // The box is kept top-left to bottom-right, so corner and edge numbers stay put while dragging
      normalize();
      if(int n = nubAt(h, in); n >= 0) {
        _mode = n;
        if(n < 4) _fixed = corner((n + 2) % 4);
        return;
      }
      if(p.x >= _a.x && p.x <= _b.x && p.y >= _a.y && p.y <= _b.y) {
        _mode = 4; _grab = p;
        return;
      }
      commit(h);
    }
    bool right = in.button == SDL_BUTTON_RIGHT;
    _primary = h.color(!right); _secondary = h.color(right);
    _a = _b = _fixed = p;
    _mode = 5;
    _drawing = kind(h) == EShapeKind::Free;
    _free.clear();
    if(_drawing) _free.push_back(p);
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(_mode < 0) return;
    Point p{ in.x, in.y };
    if(_drawing) {
      double z = h.nav().view.zoom;
      const Point& last = _free.back();
      if(std::abs(last.x - p.x) * z >= 1 || std::abs(last.y - p.y) * z >= 1) _free.push_back(p);
      render(h);
      return;
    }
    if(_mode == 4) {
      double dx = p.x - _grab.x, dy = p.y - _grab.y;
      _a.x += dx; _a.y += dy; _b.x += dx; _b.y += dy;
      _grab = p;
    } else if(_mode >= 6) {
      // An edge nub moves only its side
      switch(_mode) {
        case 6: _a.y = p.y; break;
        case 7: _b.x = p.x; break;
        case 8: _b.y = p.y; break;
        default: _a.x = p.x; break;
      }
    } else {
      // The dragged corner follows the pointer; the opposite corner, taken at the press, stays
      if(in.shift) {
        double dx = p.x - _fixed.x, dy = p.y - _fixed.y, s = std::max(std::abs(dx), std::abs(dy));
        p = { _fixed.x + (dx < 0 ? -s : s), _fixed.y + (dy < 0 ? -s : s) };
      }
      _a = _fixed; _b = p;
    }
    render(h);
  }

  void up(ToolHost& h, const ToolInput&) override {
    bool fresh = _mode == 5;
    _mode = -1;
    double z = h.nav().view.zoom;
    if(_drawing) {
      _drawing = false;
      if(!fitFree(z)) cancel(h);
      return;
    }
    if(fresh && (std::abs(_b.x - _a.x) * z < 2 || std::abs(_b.y - _a.y) * z < 2)) cancel(h);
  }

  bool busy() const override { return editing() || _mode >= 0; }

  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(_drawing) return ECursor::Cross;
    if(_mode >= 0 && _mode != 5) return ECursor::Move;
    if(!editing()) return ECursor::Cross;
    if(nubAt(h, at) >= 0) return ECursor::Move;
    bool inside = at.x >= std::min(_a.x, _b.x) && at.x <= std::max(_a.x, _b.x) && at.y >= std::min(_a.y, _b.y) && at.y <= std::max(_a.y, _b.y);
    return inside ? ECursor::Move : ECursor::Cross;
  }
  void commit(ToolHost& h) override { _edit.commit(h.doc(), name, icon); if(_live.active()) _live.commit(h, name, icon); _mode = -1; }
  void cancel(ToolHost& h) override { _edit.cancel(); _live.cancel(h); _mode = -1; _drawing = false; _free.clear(); h.doc().onChanged(); }

  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(!editing()) return false;
    if(k == ImGuiKey_Escape) cancel(h);
    else if(k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter) commit(h);
    else return false;
    return true;
  }

  // A / Shift+A cycles the shape
  void keys(ToolHost& h) override {
    auto& io = ImGui::GetIO();
    if(io.KeyCtrl || io.KeyAlt || !ImGui::IsKeyPressed(ImGuiKey_A, false)) return;
    int n = int(EShapeKind::COUNT);
    h.settings().shapeKind = (h.settings().shapeKind + (io.KeyShift ? n - 1 : 1)) % n;
    if(editing()) render(h);
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    if(!editing()) return;
    float dpi = h.dpi();
    if(_drawing) {
      float phase = float(ImGui::GetTime() * 8);
      for(size_t i = 0; i < _free.size(); i++)
        antsLine(dl, h.toScreen(_free[i].x, _free[i].y), h.toScreen(_free[(i + 1) % _free.size()].x, _free[(i + 1) % _free.size()].y), phase, dpi);
      return;
    }
    ImVec2 c[4];
    for(int i = 0; i < 4; i++) c[i] = h.toScreen(corner(i).x, corner(i).y);
    for(int i = 0; i < 4; i++) antsLine(dl, c[i], c[(i + 1) % 4], 0, dpi);
    int hot = nubAt(h, at);
    for(int i = 0; i < 4; i++) drawNub(dl, c[i], dpi, hot == i);
    // Edge nubs: smaller, they resize one side
    for(int i = 0; i < 4; i++) {
      Point e = edge(i);
      ImVec2 q = h.toScreen(e.x, e.y);
      dl->AddCircleFilled(q, 5 * dpi, IM_COL32(0, 0, 0, 200));
      dl->AddCircleFilled(q, 3.5f * dpi, hot == 6 + i ? IM_COL32(255, 255, 255, 255) : IM_COL32(220, 220, 220, 255));
    }
  }

  void options(ToolHost& h) override {
    auto& s = h.settings();
    bool changed = false;
    struct Kind { const char* icon; const char* tip; };
    static const Kind kinds[] = { { ICON_TB_SQUARE, N_("Rectangle (A)") }, { ICON_TB_SQUARE_ROUNDED, N_("Rounded rectangle (A)") },
                                  { ICON_TB_CIRCLE, N_("Ellipse (A)") }, { ICON_TB_PENTAGON, N_("Polygon (A)") },
                                  { ICON_TB_SCRIBBLE, N_("Free polygon (A)") } };
    for(int i = 0; i < int(EShapeKind::COUNT); i++) {
      bool on = s.shapeKind == i;
      if(toggle(kinds[i].icon, on, kinds[i].tip)) { s.shapeKind = i; changed = true; }
      ImGui::SameLine(0, 2);
    }
    ImGui::SameLine();
    changed |= combo("##draw", s.shapeDraw, { N_("Outline"), N_("Fill"), N_("Outline and fill") }, 120 * h.dpi(), N_("Draw mode"));
    ImGui::SameLine();
    int width = s.brushSize;
    brushWidth(h, N_("Outline width"));
    changed |= width != s.brushSize;
    if(s.shapeKind == int(EShapeKind::RoundRect)) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80 * h.dpi());
      changed |= ImGui::SliderFloat("##radius", &s.cornerRadius, 0, 200, "%.0f", ImGuiSliderFlags_Logarithmic);
      ImGui::SetItemTooltip("%s", tr("Corner radius"));
    }
    if(s.shapeKind == int(EShapeKind::Polygon)) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80 * h.dpi());
      changed |= ImGui::SliderInt("##sides", &s.polySides, 3, 24);
      ImGui::SetItemTooltip("%s", tr("Sides"));
    }
    std::pair<bool, bool> g{ s.antialias, s.pixelPerfect };
    changed |= g != _globals;
    _globals = g;
    if(changed && editing()) render(h);
  }

private:
  LiveEdit _edit;
  LiveObject _live;  // the same shape as an object, on vector layers
  Point _a{}, _b{}, _grab{};
  uint32_t _primary = 0, _secondary = 0;
  int _mode = -1;  // 0-3 corner, 4 move, 5 new, 6-9 edge
  std::pair<bool, bool> _globals{};
  Polyline _free;  // Free polygon points, 0-1 in the box (absolute while drawing)
  bool _drawing = false;

  Point _fixed{};

  static EShapeKind kind(ToolHost& h) { return EShapeKind(std::clamp(h.settings().shapeKind, 0, int(EShapeKind::COUNT) - 1)); }
  bool editing() const { return _edit.active() || _live.active(); }

  // On a vector layer: the shape as an object (the outline inside it, like the pixels)
  void renderObject(ToolHost& h) {
    auto& s = h.settings();
    EShapeKind k = kind(h);
    VecObject o;
    o.x = _a.x; o.y = _a.y; o.w = _b.x - _a.x; o.h = _b.y - _a.y;
    switch(k) {
      case EShapeKind::Rect: o.kind = EVecKind::Rect; break;
      case EShapeKind::RoundRect: o.kind = EVecKind::Rect; o.radius = s.cornerRadius; break;
      case EShapeKind::Ellipse: o.kind = EVecKind::Ellipse; break;
      case EShapeKind::Polygon: o.kind = EVecKind::Polygon; o.sides = s.polySides; break;
      default: {
        o.kind = EVecKind::Path;
        Subpath sp;
        sp.closed = true;
        for(auto& q : freeContour()) sp.nodes.push_back(PathNode::corner(q));
        o.paths = { sp };
        break;
      }
    }
    o.name = k == EShapeKind::Free ? N_("Free polygon") : k == EShapeKind::Ellipse ? N_("Ellipse") : k == EShapeKind::Polygon ? N_("Polygon") : N_("Rectangle");
    double w = std::max(1, s.brushSize);
    if(s.shapeDraw == 0) o.stroke = toolStroke(_primary, w, EStrokeAlign::Inside);
    else if(s.shapeDraw == 1) o.fill = VecPaint::solid(_primary);
    else { o.fill = VecPaint::solid(_secondary); o.stroke = toolStroke(_primary, w, EStrokeAlign::Inside); }
    _live.set(h, std::move(o));
  }

  // Fits the box to the drawn points and makes them relative to it; false when too small to keep
  bool fitFree(double zoom) {
    if(_free.size() < 3) return false;
    Point lo = _free[0], hi = _free[0];
    for(auto& q : _free) {
      lo = { std::min(lo.x, q.x), std::min(lo.y, q.y) };
      hi = { std::max(hi.x, q.x), std::max(hi.y, q.y) };
    }
    if((hi.x - lo.x) * zoom < 2 || (hi.y - lo.y) * zoom < 2) return false;
    for(auto& q : _free) q = { (q.x - lo.x) / (hi.x - lo.x), (q.y - lo.y) / (hi.y - lo.y) };
    _a = lo; _b = hi;
    return true;
  }

  // The box itself when the shape was switched to free after drawing another kind
  Polyline freeContour() const {
    if(_drawing) return _free;
    static const Polyline box{ { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    Polyline out;
    for(auto& q : _free.empty() ? box : _free) out.push_back({ _a.x + (_b.x - _a.x) * q.x, _a.y + (_b.y - _a.y) * q.y });
    return out;
  }

  void normalize() {
    // A mirrored box flips the free points, so the shape stays where it is
    for(auto& q : _free) {
      if(_a.x > _b.x) q.x = 1 - q.x;
      if(_a.y > _b.y) q.y = 1 - q.y;
    }
    Point a{ std::min(_a.x, _b.x), std::min(_a.y, _b.y) }, b{ std::max(_a.x, _b.x), std::max(_a.y, _b.y) };
    _a = a; _b = b;
  }

  // Top, right, bottom, left midpoints
  Point edge(int i) const {
    double cx = (_a.x + _b.x) / 2, cy = (_a.y + _b.y) / 2;
    switch(i) {
      case 0: return { cx, _a.y };
      case 1: return { _b.x, cy };
      case 2: return { cx, _b.y };
      default: return { _a.x, cy };
    }
  }

  // 0-3 corner, 6-9 edge, -1 none; corners win where they overlap a small box's edge nubs
  int nubAt(ToolHost& h, const ToolInput& in) const {
    for(int i = 0; i < 4; i++) if(overNub(in, h.toScreen(corner(i).x, corner(i).y), h.dpi())) return i;
    for(int i = 0; i < 4; i++) if(overNub(in, h.toScreen(edge(i).x, edge(i).y), h.dpi())) return 6 + i;
    return -1;
  }

  // Clockwise from _a
  Point corner(int i) const {
    switch(i) {
      case 0: return _a;
      case 1: return { _b.x, _a.y };
      case 2: return _b;
      default: return { _a.x, _b.y };
    }
  }

  void render(ToolHost& h) {
    if(h.vectorMode()) { renderObject(h); return; }
    Document& doc = h.doc();
    auto& s = h.settings();
    TileRecorder& rec = _edit.redraw(doc);
    EShapeKind k = kind(h);
    double w = std::max(1, s.brushSize);
    bool isFree = k == EShapeKind::Free;
    Polyline outer = isFree ? freeContour() : shapeContour(k, _a.x, _a.y, _b.x, _b.y, 0, s.cornerRadius, s.polySides);
    if(outer.size() >= 3) {
      auto fill = fillContours(doc.width(), doc.height(), { outer }, s.antialias);
      std::unique_ptr<Mask> line;
      if(s.shapeDraw != 1 && isFree) {
        // A free outline has no inset contour: a stroke twice as wide, kept inside the fill
        line = std::make_unique<Mask>(doc.width(), doc.height());
        Polyline loop = outer;
        loop.push_back(outer.front());
        strokePolyline(*line, loop, w * 2, s.antialias);
        line->combine(*fill, ECombine::Intersect);
      } else if(s.shapeDraw != 1) {
        Polyline inner = shapeContour(k, _a.x, _a.y, _b.x, _b.y, w, s.cornerRadius, s.polySides);
        line = inner.size() >= 3 ? fillContours(doc.width(), doc.height(), { outer, inner }, s.antialias) : fill->clone();
      }
      StrokeSpace sp = strokeSpace(s, doc);
      if(s.shapeDraw == 0) paintCoverage(doc, rec, { { line.get(), _primary } }, &sp);
      else if(s.shapeDraw == 1) paintCoverage(doc, rec, { { fill.get(), _primary } }, &sp);
      else paintCoverage(doc, rec, { { fill.get(), _secondary }, { line.get(), _primary } }, &sp);
    }
    doc.onChanged();
  }
};
#pragma endregion

std::unique_ptr<Tool> makeGradient() { return std::make_unique<GradientTool>(); }
std::unique_ptr<Tool> makeLine() { return std::make_unique<PathTool>(false); }
std::unique_ptr<Tool> makeCurve() { return std::make_unique<PathTool>(true); }
std::unique_ptr<Tool> makeShapes() { return std::make_unique<ShapesTool>(); }

}
