#include "ui/tools/Tool.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Draw.h"
#include "core/Raster.h"
#include "core/Stroke.h"
#include "ui/CanvasNav.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include "scripting/Mods.h"
#include <cctype>
#include "ui/Theme.h"
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <cmath>

namespace david {

void antsLine(ImDrawList* dl, ImVec2 a, ImVec2 b, float phase, float dpi) {
  float dash = 4 * dpi;
  dl->AddLine(a, b, IM_COL32(255, 255, 255, 255), 1);
  float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
  if(len < 0.5f) return;
  // Dashes are anchored to screen space, so joined segments line up
  float start = std::fmod(a.x + a.y + phase, dash * 2);
  for(float t = -start; t < len; t += dash * 2) {
    float t0 = std::max(t, 0.f), t1 = std::min(t + dash, len);
    if(t1 <= t0) continue;
    dl->AddLine({ a.x + dx * t0 / len, a.y + dy * t0 / len }, { a.x + dx * t1 / len, a.y + dy * t1 / len }, IM_COL32(0, 0, 0, 255), 1);
  }
}

StrokeSpace strokeSpace(const ToolSettings& s, const Document& doc) {
  StrokeSpace sp;
  sp.wrapX = s.tiled & 1; sp.wrapY = s.tiled & 2;
  sp.mirrorX = s.symmetry & 1; sp.mirrorY = s.symmetry & 2;
  sp.axisX = s.symX < 0 ? doc.width() / 2.0 : s.symX;
  sp.axisY = s.symY < 0 ? doc.height() / 2.0 : s.symY;
  return sp;
}

void snapToGrid(const ToolSettings& s, double& x, double& y) {
  if(!s.grid || !s.gridSnap) return;
  x = std::round((x - s.gridX) / s.gridW) * s.gridW + s.gridX;
  y = std::round((y - s.gridY) / s.gridH) * s.gridH + s.gridY;
}

// The stroke for a freehand tool: the stroke space, and the shading ink when that's on
static std::unique_ptr<Stroke> makeStroke(ToolHost& h, EInk ink, const ToolInput& in, bool canShade) {
  auto& s = h.settings();
  bool primary = in.button != SDL_BUTTON_RIGHT;
  bool shade = canShade && s.shading && ink == EInk::Paint;
  if(shade) ink = EInk::Shade;
  else if(ink == EInk::Paint && s.replaceAlpha) ink = EInk::Replace;
  auto st = std::make_unique<Stroke>(h.doc(), h.doc().active(), ink, h.color(primary));
  st->setSpace(strokeSpace(s, h.doc()));
  if(shade) st->setShade(&s.palette, primary ? 1 : -1);
  return st;
}

static bool toggle(const char* icon, bool& v, const char* tip) {
  return ui::iconToggle(icon, icon, v, tr(tip));
}

void drawNub(ImDrawList* dl, ImVec2 p, float dpi, bool hot) {
  float r = (hot ? 6.f : 5.f) * dpi;
  dl->AddCircleFilled(p, r + 1.5f * dpi, IM_COL32(0, 0, 0, 200));
  dl->AddCircleFilled(p, r, hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(230, 230, 230, 255));
}

bool overNub(const ToolInput& in, ImVec2 p, float dpi) {
  float dx = in.sx - p.x, dy = in.sy - p.y, r = 9 * dpi;
  return dx * dx + dy * dy <= r * r;
}

FloodOptions floodOptions(const ToolSettings& s, bool global, uint32_t outline) {
  FloodOptions o;
  o.tolerance = s.tolerance;
  o.contiguous = !global;
  o.classicRgb = s.classicRgb;
  o.soft = s.softEdges;
  o.edgeStop = s.edgeStop ? std::max(s.edgeThreshold, 0.01f) : 0;
  o.toOutline = s.fillOutline;
  o.outline = outline;
  return o;
}

void floodOptionsBar(ToolHost& h, bool& global, bool bucket) {
  auto& s = h.settings();
  bool contiguous = !global;
  if(toggle(ICON_PH_POLYGON, contiguous, N_("Contiguous"))) global = false;
  ImGui::SameLine();
  toggle(ICON_TB_WORLD, global, N_("Global (Shift)"));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(120 * h.dpi());
  float pct = s.tolerance * 100;
  if(ImGui::SliderFloat("##tol", &pct, 0, 100, "%.0f%%")) s.tolerance = pct / 100;
  ImGui::SetItemTooltip("%s", tr("Tolerance (0% = exact color)"));
  ImGui::SameLine();
  // Lit = Paint.NET's RGB distance, off = perceptual OKLab
  toggle(ICON_TB_MATH_FUNCTION, s.classicRgb, N_("Classic RGB tolerance"));
  ImGui::SameLine();
  toggle(ICON_PH_FEATHER, s.softEdges, N_("Soft edges"));
  ImGui::SameLine();
  toggle(ICON_TB_BORDER_OUTER, s.edgeStop, N_("Stop at edges"));
  if(s.edgeStop) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * h.dpi());
    float e = s.edgeThreshold * 100;
    if(ImGui::SliderFloat("##edge", &e, 1, 100, "%.0f%%")) s.edgeThreshold = e / 100;
    ImGui::SetItemTooltip("%s", tr("Edge strength that stops the fill"));
  }
  if(bucket) {
    ImGui::SameLine();
    toggle(ICON_TB_SHAPE_2, s.fillOutline, N_("Fill to outline (secondary color)"));
  }
  ImGui::SameLine();
  toggle(ICON_TB_STACK_2, s.sampleImage, "Sample image");
}

static void brushSizeOption(ToolHost& h) {
  ImGui::TextDisabled(ICON_TB_CIRCLE);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(140 * h.dpi());
  ImGui::SliderInt("##size", &h.settings().brushSize, 1, 500, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
  ImGui::SetItemTooltip("%s  [ ]", tr("Brush width"));
}

#pragma region [ Brush and eraser ]
class BrushTool : public Tool {
public:
  BrushTool(ETool id, const char* icon, const char* name, ImGuiKey key, EInk ink) : Tool(id, icon, name, key), _ink(ink) {}

  void down(ToolHost& h, const ToolInput& in) override {
    auto& s = h.settings();
    _stroke = makeStroke(h, _ink, in, true);
    prepare(*_stroke);
    float r = radius(s, in), o = opacity(s, in);
    _stroke->moveTo(in.x, in.y, r, o);
    _stroke->dab(in.x, in.y, r, o, s.antialias);
    h.doc().onChanged();
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(!_stroke) return;
    auto& s = h.settings();
    _stroke->lineTo(in.x, in.y, radius(s, in), opacity(s, in), s.antialias);
    h.doc().onChanged();
  }

  void up(ToolHost& h, const ToolInput&) override { commit(h); }

  void commit(ToolHost& h) override {
    if(!_stroke) return;
    if(auto edit = _stroke->finish(name, icon)) h.doc().history.push(h.doc(), std::move(edit));
    _stroke.reset();
  }

  bool busy() const override { return bool(_stroke); }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    float r = float(h.settings().brushSize / 2.0 * h.nav().view.zoom / ImGui::GetIO().DisplayFramebufferScale.x);
    ImVec2 c = { at.sx, at.sy };
    dl->AddCircle(c, std::max(r, 2.f), IM_COL32(0, 0, 0, 200), 0, 2.5f);
    dl->AddCircle(c, std::max(r, 2.f), IM_COL32(255, 255, 255, 230), 0, 1);
  }

  void options(ToolHost& h) override {
    auto& s = h.settings();
    brushSizeOption(h);
    ImGui::SameLine();
    toggle(ICON_PH_PEN_NIB, s.pressureSize, N_("Pressure: size"));
    ImGui::SameLine();
    toggle(ICON_TB_DROPLET, s.pressureOpacity, N_("Pressure: opacity"));
    if(_ink == EInk::Paint) {
      ImGui::SameLine();
      toggle(ICON_TB_STAIRS_UP, s.shading, N_("Shading ink (steps along the palette; right click steps back)"));
    }
  }

protected:
  // Sets up the stroke before its first dab (mod tools give it their ink)
  virtual void prepare(Stroke&) {}

private:
  EInk _ink;
  std::unique_ptr<Stroke> _stroke;

  static float radius(const ToolSettings& s, const ToolInput& in) {
    return s.brushSize / 2.f * (in.pen && s.pressureSize ? std::max(in.pressure, 0.05f) : 1.f);
  }
  static float opacity(const ToolSettings& s, const ToolInput& in) {
    return in.pen && s.pressureOpacity ? in.pressure : 1.f;
  }
};
#pragma endregion

#pragma region [ Pencil ]
// One aliased pixel, Bresenham between events
class PencilTool : public Tool {
public:
  PencilTool() : Tool(ETool::Pencil, ICON_TB_PENCIL, N_("Pencil"), ImGuiKey_P) {}

  void down(ToolHost& h, const ToolInput& in) override {
    _stroke = makeStroke(h, EInk::Paint, in, true);
    _x = int(std::floor(in.x)); _y = int(std::floor(in.y));
    _path.clear();
    line(h, _x, _y);
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(!_stroke) return;
    line(h, int(std::floor(in.x)), int(std::floor(in.y)));
  }

  void up(ToolHost& h, const ToolInput&) override { commit(h); }

  void commit(ToolHost& h) override {
    if(!_stroke) return;
    if(auto edit = _stroke->finish(name, icon)) h.doc().history.push(h.doc(), std::move(edit));
    _stroke.reset();
  }

  bool busy() const override { return bool(_stroke); }

  void options(ToolHost& h) override {
    toggle(ICON_TB_STAIRS_UP, h.settings().shading, N_("Shading ink (steps along the palette; right click steps back)"));
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    double x = std::floor(at.x), y = std::floor(at.y);
    ImVec2 a = h.toScreen(x, y), b = h.toScreen(x + 1, y + 1);
    if(b.x - a.x >= 4) {
      dl->AddRect(a, b, IM_COL32(0, 0, 0, 200), 0, 0, 2.5f);
      dl->AddRect(a, b, IM_COL32(255, 255, 255, 230));
    }
    float s = 6 * h.dpi();
    dl->AddLine({ at.sx - s, at.sy }, { at.sx + s, at.sy }, IM_COL32(255, 255, 255, 230));
    dl->AddLine({ at.sx, at.sy - s }, { at.sx, at.sy + s }, IM_COL32(255, 255, 255, 230));
  }

private:
  std::unique_ptr<Stroke> _stroke;
  int _x = 0, _y = 0;
  std::vector<std::pair<int, int>> _path;  // the last pixels, for the pixel-perfect rule

  void plot(ToolHost& h, int x, int y) {
    if(!_path.empty() && _path.back() == std::pair(x, y)) return;
    _stroke->cover(x, y, 255);
    _path.push_back({ x, y });
    // Pixel-perfect (Aseprite's rule): the middle of three pixels that make an L corner goes back
    if(h.settings().pixelPerfect && _path.size() >= 3) {
      auto [ax, ay] = _path[_path.size() - 3];
      auto [bx, by] = _path[_path.size() - 2];
      auto [cx, cy] = _path.back();
      if((ax == bx || ay == by) && (cx == bx || cy == by) && ax != cx && ay != cy) {
        _stroke->uncover(bx, by);
        _path.erase(_path.end() - 2);
      }
    }
    if(_path.size() > 8) _path.erase(_path.begin(), _path.end() - 3);
  }

  void line(ToolHost& h, int x1, int y1) {
    int x0 = _x, y0 = _y;
    int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
    while(true) {
      plot(h, x0, y0);
      if(x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if(e2 >= dy) { err += dy; x0 += sx; }
      if(e2 <= dx) { err += dx; y0 += sy; }
    }
    _x = x1; _y = y1;
    h.doc().onChanged();
  }
};
#pragma endregion

#pragma region [ Spray ]
// Random dots inside the brush circle, more while the button is held (LibreSprite's spray)
class SprayTool : public Tool {
public:
  SprayTool() : Tool(ETool::Spray, ICON_TB_SPRAY, N_("Spray"), ImGuiKey_U) {}

  void down(ToolHost& h, const ToolInput& in) override {
    _stroke = makeStroke(h, EInk::Paint, in, false);
    _x = in.x; _y = in.y;
    burst(h);
  }
  void drag(ToolHost& h, const ToolInput& in) override {
    if(!_stroke) return;
    _x = in.x; _y = in.y;
    burst(h);
  }
  void tick(ToolHost& h) override { if(_stroke) burst(h); }
  void up(ToolHost& h, const ToolInput&) override { commit(h); }
  void commit(ToolHost& h) override {
    if(!_stroke) return;
    if(auto edit = _stroke->finish(name, icon)) h.doc().history.push(h.doc(), std::move(edit));
    _stroke.reset();
  }
  bool busy() const override { return bool(_stroke); }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    float r = float(h.settings().brushSize / 2.0 * h.nav().view.zoom / ImGui::GetIO().DisplayFramebufferScale.x);
    dl->AddCircle({ at.sx, at.sy }, std::max(r, 2.f), IM_COL32(0, 0, 0, 200), 0, 2.5f);
    dl->AddCircle({ at.sx, at.sy }, std::max(r, 2.f), IM_COL32(255, 255, 255, 230), 0, 1);
  }

  void options(ToolHost& h) override {
    brushSizeOption(h);
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_TB_GRAIN);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100 * h.dpi());
    ImGui::SliderInt("##density", &h.settings().sprayDensity, 1, 500, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("%s", tr("Dots per tick"));
  }

private:
  std::unique_ptr<Stroke> _stroke;
  double _x = 0, _y = 0;
  uint32_t _seed = 0x9E3779B9u;

  double rnd() {
    _seed ^= _seed << 13; _seed ^= _seed >> 17; _seed ^= _seed << 5;
    return (_seed & 0xFFFFFF) / double(0x1000000);
  }
  void burst(ToolHost& h) {
    double r = std::max(0.5, h.settings().brushSize / 2.0);
    for(int i = 0; i < h.settings().sprayDensity; i++) {
      // Uniform over the disc
      double a = rnd() * 6.2831853, d = std::sqrt(rnd()) * r;
      _stroke->cover(int(std::floor(_x + std::cos(a) * d)), int(std::floor(_y + std::sin(a) * d)), 255);
    }
    h.doc().onChanged();
  }
};
#pragma endregion

#pragma region [ Paint bucket ]
class BucketTool : public Tool {
public:
  BucketTool() : Tool(ETool::Bucket, ICON_TB_BUCKET_DROPLET, N_("Paint Bucket"), ImGuiKey_F) {}

  void down(ToolHost& h, const ToolInput& in) override {
    auto& s = h.settings();
    Document& doc = h.doc();
    int x = int(std::floor(in.x)), y = int(std::floor(in.y));
    if(x < 0 || y < 0 || x >= doc.width() || y >= doc.height()) return;
    // Shift flips contiguous/global for one click
    bool global = s.fillGlobal != in.shift;
    bool primary = in.button != SDL_BUTTON_RIGHT;
    // Fill to outline stops at the other color
    auto area = floodMask(doc, s.sampleImage ? nullptr : &doc.active(), x, y, floodOptions(s, global, h.color(!primary)));
    Stroke st(doc, doc.active(), EInk::Paint, h.color(primary));
    IRect b = area->bounds();
    std::vector<uint8_t> row(std::max(b.width(), 0));
    for(int yy = b.y0; yy <= b.y1; yy++) {
      area->readRow(yy, b.x0, b.x1, row.data());
      for(int i = 0; i < b.width(); i++) if(row[i]) st.cover(b.x0 + i, yy, row[i]);
    }
    if(auto edit = st.finish(name, icon)) doc.history.push(doc, std::move(edit));
  }

  void options(ToolHost& h) override { floodOptionsBar(h, h.settings().fillGlobal, true); }
};
#pragma endregion

#pragma region [ Color picker ]
class PickerTool : public Tool {
public:
  PickerTool() : Tool(ETool::Picker, ICON_PH_EYEDROPPER, N_("Color Picker"), ImGuiKey_K) {}

  void down(ToolHost& h, const ToolInput& in) override { pick(h, in); }
  void drag(ToolHost& h, const ToolInput& in) override { pick(h, in); }
  void up(ToolHost& h, const ToolInput&) override { if(h.settings().pickerSwitchBack) h.switchBack(); }

  void options(ToolHost& h) override {
    auto& s = h.settings();
    const char* sizes[] = { "1 × 1", "3 × 3", "5 × 5", "9 × 9" };
    int sizeVals[] = { 1, 3, 5, 9 };
    int cur = 0;
    for(int i = 0; i < 4; i++) if(sizeVals[i] == s.pickerSize) cur = i;
    ImGui::SetNextItemWidth(90 * h.dpi());
    if(ImGui::Combo("##sample", &cur, sizes, 4)) s.pickerSize = sizeVals[cur];
    ImGui::SetItemTooltip("%s", tr("Sampling"));
    ImGui::SameLine();
    toggle(ICON_TB_STACK_2, s.pickerImage, N_("Sample image (Ctrl)"));
    ImGui::SameLine();
    toggle(ICON_TB_ARROW_BACK_UP, s.pickerSwitchBack, N_("Switch to previous tool"));
  }

private:
  void pick(ToolHost& h, const ToolInput& in) {
    Document& doc = h.doc();
    auto& s = h.settings();
    bool image = s.pickerImage != in.ctrl;
    int cx = int(std::floor(in.x)), cy = int(std::floor(in.y)), r = s.pickerSize / 2;
    double acc[4] = { 0, 0, 0, 0 };
    int n = 0;
    std::vector<uint32_t> row(doc.width());
    for(int y = cy - r; y <= cy + r; y++) {
      if(y < 0 || y >= doc.height()) continue;
      if(image) flattenRow(doc, y, row.data());
      for(int x = cx - r; x <= cx + r; x++) {
        if(x < 0 || x >= doc.width()) continue;
        uint32_t c = image ? row[x] : doc.active().get(x, y);
        double a = (c >> 24) / 255.0;
        acc[0] += (c & 0xFF) * a; acc[1] += ((c >> 8) & 0xFF) * a; acc[2] += ((c >> 16) & 0xFF) * a; acc[3] += a;
        n++;
      }
    }
    if(!n) return;
    uint32_t out = 0;
    if(acc[3] > 0)
      for(int k = 0; k < 3; k++) out |= uint32_t(acc[k] / acc[3] + 0.5) << (k * 8);
    out |= uint32_t(acc[3] / n * 255 + 0.5) << 24;
    h.color(in.button != SDL_BUTTON_RIGHT) = out;
  }
};
#pragma endregion

#pragma region [ Zoom and pan ]
class ZoomTool : public Tool {
public:
  ZoomTool() : Tool(ETool::Zoom, ICON_TB_ZOOM, N_("Zoom"), ImGuiKey_Z) {}

  void down(ToolHost&, const ToolInput& in) override { _start = in; _dragging = true; }
  void drag(ToolHost&, const ToolInput& in) override { _cur = in; }
  void up(ToolHost& h, const ToolInput& in) override {
    _dragging = false;
    CanvasNav& nav = h.nav();
    float d = ImGui::GetIO().DisplayFramebufferScale.x;
    double x0, y0, x1, y1;
    rect(h, _start, in, x0, y0, x1, y1);
    double w = x1 - x0, hh = y1 - y0;
    if(in.button == SDL_BUTTON_LEFT && w * nav.view.zoom > 8 && hh * nav.view.zoom > 8) {
      // Zoom to the dragged rectangle
      double z = std::min(nav.view.vpW / w, nav.view.vpH / hh);
      double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
      nav.zoomAt(z, nav.view.vpX + nav.view.vpW / 2.0, nav.view.vpY + nav.view.vpH / 2.0);
      nav.centerOn(cx, cy);
      return;
    }
    nav.zoomStep(in.button == SDL_BUTTON_RIGHT ? -1 : 1, in.sx * d, in.sy * d);
  }
  bool busy() const override { return _dragging; }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput&) override {
    if(!_dragging) return;
    double x0, y0, x1, y1;
    rect(h, _start, _cur, x0, y0, x1, y1);
    ImVec2 a = h.toScreen(x0, y0), b = h.toScreen(x1, y1);
    float p = float(ImGui::GetTime() * 8);
    antsLine(dl, a, { b.x, a.y }, p, h.dpi()); antsLine(dl, { b.x, a.y }, b, p, h.dpi());
    antsLine(dl, b, { a.x, b.y }, p, h.dpi()); antsLine(dl, { a.x, b.y }, a, p, h.dpi());
  }

private:
  ToolInput _start, _cur;
  bool _dragging = false;

  // The dragged rectangle, kept inside the canvas: a drag that starts outside zooms to the image's edges
  static void rect(ToolHost& h, const ToolInput& a, const ToolInput& b, double& x0, double& y0, double& x1, double& y1) {
    double w = h.doc().width(), hh = h.doc().height();
    x0 = std::clamp(std::min(a.x, b.x), 0.0, w); x1 = std::clamp(std::max(a.x, b.x), 0.0, w);
    y0 = std::clamp(std::min(a.y, b.y), 0.0, hh); y1 = std::clamp(std::max(a.y, b.y), 0.0, hh);
  }
};

class PanTool : public Tool {
public:
  PanTool() : Tool(ETool::Pan, ICON_TB_HAND_STOP, N_("Pan"), ImGuiKey_H) {}
  void down(ToolHost&, const ToolInput& in) override { _x = in.sx; _y = in.sy; _dragging = true; }
  void drag(ToolHost& h, const ToolInput& in) override {
    float d = ImGui::GetIO().DisplayFramebufferScale.x;
    h.nav().panBy((in.sx - _x) * d, (in.sy - _y) * d);
    _x = in.sx; _y = in.sy;
  }
  void up(ToolHost&, const ToolInput&) override { _dragging = false; }
  bool busy() const override { return _dragging; }
  ECursor cursor(ToolHost&, const ToolInput&) override { return ECursor::Move; }
private:
  float _x = 0, _y = 0;
  bool _dragging = false;
};
#pragma endregion

std::unique_ptr<Tool> makeBrush() { return std::make_unique<BrushTool>(ETool::Brush, ICON_TB_BRUSH, N_("Paintbrush"), ImGuiKey_B, EInk::Paint); }
std::unique_ptr<Tool> makeEraser() { return std::make_unique<BrushTool>(ETool::Eraser, ICON_TB_ERASER, N_("Eraser"), ImGuiKey_E, EInk::Erase); }
std::unique_ptr<Tool> makePencil() { return std::make_unique<PencilTool>(); }
std::unique_ptr<Tool> makeBucket() { return std::make_unique<BucketTool>(); }
std::unique_ptr<Tool> makePicker() { return std::make_unique<PickerTool>(); }
std::unique_ptr<Tool> makeZoom() { return std::make_unique<ZoomTool>(); }
std::unique_ptr<Tool> makePan() { return std::make_unique<PanTool>(); }
std::unique_ptr<Tool> makeSpray() { return std::make_unique<SprayTool>(); }

#pragma region [ Mod tools ]
// The Paintbrush with a Lua ink: the ink gives each covered pixel's new color, mixed in by the coverage
class ModBrushTool : public BrushTool {
public:
  explicit ModBrushTool(const ModTool& def) : BrushTool(ETool::Mod, ICON_FA_PUZZLE_PIECE, "", keyFor(def.key), EInk::Custom), _def(def) {
    name = _def.name.c_str();
  }
protected:
  void prepare(Stroke& st) override {
    auto ink = _def.ink;
    st.setInk([ink](int x, int y, uint32_t before, uint32_t color) { return ink(x, y, before, color); });
  }
private:
  ModTool _def;
  static ImGuiKey keyFor(const std::string& k) {
    if(k.size() == 1 && std::isalpha(uint8_t(k[0]))) return ImGuiKey(ImGuiKey_A + (std::toupper(uint8_t(k[0])) - 'A'));
    return ImGuiKey_None;
  }
};
#pragma endregion

std::unique_ptr<Tool> makeModTool(const ModTool& def) { return std::make_unique<ModBrushTool>(def); }

}
