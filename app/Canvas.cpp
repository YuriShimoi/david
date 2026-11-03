#include "app/App.h"
#include "core/Vector.h"
#include "core/Layer.h"
#include "core/Stroke.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace david {

#pragma region [ Input ]
ToolInput App::makeInput(float wx, float wy, int button, bool pen) const {
  ToolInput in;
  float d = density();
  in.sx = wx;
  in.sy = wy;
  in.x = _nav.toImageX(wx * d);
  in.y = _nav.toImageY(wy * d);
  SDL_Keymod mod = SDL_GetModState();
  in.ctrl = mod & SDL_KMOD_CTRL;
  in.shift = mod & SDL_KMOD_SHIFT;
  in.alt = mod & SDL_KMOD_ALT;
  in.button = button;
  in.pen = pen;
  in.pressure = pen ? _pressure : 1;
  return in;
}

ImVec2 App::toScreen(double ix, double iy) {
  float d = ImGui::GetIO().DisplayFramebufferScale.x;
  auto& v = _nav.view;
  return { float((v.vpX + (ix - v.originX) * v.zoom) / d), float((v.vpY + (iy - v.originY) * v.zoom) / d) };
}

// phase 0 = press, 1 = move, 2 = release
void App::pointer(int phase, float wx, float wy, int button, bool pen, int clicks) {
  ToolInput in = makeInput(wx, wy, phase == 0 ? button : _gestureButton, pen);
  in.doubleClick = clicks >= 2;
  // The symmetry axis nubs come before the tool
  if(phase == 0 && button == SDL_BUTTON_LEFT) {
    if(int a = axisAt(in)) {
      _axisDrag = a;
      _gesture = EGesture::Axis;
      _gestureButton = button;
      SDL_CaptureMouse(true);
      _hover = in;
      return;
    }
  }
  if(_gesture == EGesture::Axis) {
    dragAxis(in);
    if(phase == 2) { _gesture = EGesture::None; _axisDrag = 0; SDL_CaptureMouse(false); }
    _hover = in;
    return;
  }
  // Grid snap for tools that place points (selection shapes, line, curve, shapes, gradient)
  ETool id = tool().id;
  bool snaps = (id == ETool::RectSelect && _cfg.tools.selectShape < 2) || id == ETool::Line || id == ETool::Curve || id == ETool::Shapes || id == ETool::Gradient;
  if(snaps && !in.doubleClick) snapToGrid(_cfg.tools, in.x, in.y);
  if(phase == 0) {
    // Tools that paint pixels ask before touching a vector layer; vector-aware ones make objects there
    static const ETool safe[] = { ETool::Mouse, ETool::Zoom, ETool::Pan, ETool::Picker, ETool::RectSelect, ETool::Lasso, ETool::EllipseSelect,
                                  ETool::PolyLasso, ETool::MagicWand, ETool::MoveSelection, ETool::Node };
    static const ETool drawing[] = { ETool::Shapes, ETool::Line, ETool::Curve, ETool::Text, ETool::Pencil };
    bool harmless = std::find(std::begin(safe), std::end(safe), id) != std::end(safe);
    bool vectorTool = vectorMode() && std::find(std::begin(drawing), std::end(drawing), id) != std::end(drawing);
    if(_doc->active().kind == ELayerKind::Vector && !harmless && !vectorTool && !rasterGuard(nullptr)) { _hover = in; return; }
    _gesture = EGesture::Tool;
    _gestureButton = button;
    SDL_CaptureMouse(true);
    tool().down(*this, in);
  } else if(phase == 1 && _gesture == EGesture::Tool) {
    tool().drag(*this, in);
  } else if(phase == 2 && _gesture == EGesture::Tool && button == _gestureButton) {
    tool().up(*this, in);
    _gesture = EGesture::None;
    SDL_CaptureMouse(false);
  }
  _hover = in;
}

void App::canvasEvent(const SDL_Event& e, bool uiHasMouse) {
  float d = density();
  switch(e.type) {
    case SDL_EVENT_MOUSE_WHEEL: {
      float sx = e.wheel.mouse_x * d, sy = e.wheel.mouse_y * d;
      if(uiHasMouse || !_nav.inViewport(sx, sy)) break;
      SDL_Keymod mod = SDL_GetModState();
      float y = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.y : e.wheel.y;
      float x = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e.wheel.x : e.wheel.x;
      // Paint.NET: Ctrl+wheel zooms; the "wheel zooms" setting swaps that
      if(bool(mod & SDL_KMOD_CTRL) != _cfg.wheelZooms) {
        if(y != 0) _nav.zoomStep(y > 0 ? 1 : -1, sx, sy);
      } else {
        float step = 64 * _dpi;
        if(mod & SDL_KMOD_SHIFT) _nav.panBy(y * step, 0);
        else _nav.panBy(-x * step, y * step);
      }
      break;
    }

    // The pen drives tools directly (with pressure); SDL's mouse events synthesized from it are skipped
    case SDL_EVENT_PEN_AXIS:
      _probe.axisEvents++;
      _probe.id = e.paxis.which;
      if(e.paxis.axis == SDL_PEN_AXIS_PRESSURE) _pressure = _probe.pressure = e.paxis.value;
      if(e.paxis.axis == SDL_PEN_AXIS_XTILT) _probe.tiltX = e.paxis.value;
      if(e.paxis.axis == SDL_PEN_AXIS_YTILT) _probe.tiltY = e.paxis.value;
      break;
    case SDL_EVENT_PEN_DOWN:
      _probe.events++;
      _probe.down = true;
      _probe.eraser = e.ptouch.eraser;
      if(uiHasMouse || !_nav.inViewport(e.ptouch.x * d, e.ptouch.y * d) || _gesture != EGesture::None) break;
      _penDown = true;
      pointer(0, e.ptouch.x, e.ptouch.y, SDL_BUTTON_LEFT, true, 1);
      break;
    case SDL_EVENT_PEN_MOTION:
      _probe.events++;
      _cursorIn = !uiHasMouse && _nav.inViewport(e.pmotion.x * d, e.pmotion.y * d);
      if(_penDown) pointer(1, e.pmotion.x, e.pmotion.y, SDL_BUTTON_LEFT, true, 0);
      else _hover = makeInput(e.pmotion.x, e.pmotion.y, SDL_BUTTON_LEFT, true);
      break;
    case SDL_EVENT_PEN_UP:
      _probe.events++;
      _probe.down = false;
      if(_penDown) pointer(2, e.ptouch.x, e.ptouch.y, SDL_BUTTON_LEFT, true, 0);
      _penDown = false;
      break;

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
      if(e.button.which == SDL_PEN_MOUSEID) break;
      float sx = e.button.x * d, sy = e.button.y * d;
      if(uiHasMouse || !_nav.inViewport(sx, sy) || _gesture != EGesture::None) break;
      if(e.button.button == SDL_BUTTON_MIDDLE || (e.button.button == SDL_BUTTON_LEFT && _spaceDown)) {
        _gesture = EGesture::Pan;
        _gestureButton = e.button.button;
        _panX = sx; _panY = sy;
        SDL_CaptureMouse(true);
      } else if(e.button.button == SDL_BUTTON_LEFT || e.button.button == SDL_BUTTON_RIGHT) {
        pointer(0, e.button.x, e.button.y, e.button.button, false, e.button.clicks);
      }
      break;
    }
    case SDL_EVENT_MOUSE_MOTION: {
      if(e.motion.which == SDL_PEN_MOUSEID) break;
      float sx = e.motion.x * d, sy = e.motion.y * d;
      _cursorIn = !uiHasMouse && _nav.inViewport(sx, sy);
      if(_gesture == EGesture::Pan) {
        _nav.panBy(sx - _panX, sy - _panY);
        _panX = sx; _panY = sy;
      } else if(_gesture == EGesture::Tool || _gesture == EGesture::Axis) {
        pointer(1, e.motion.x, e.motion.y, _gestureButton, false, 0);
      } else {
        _hover = makeInput(e.motion.x, e.motion.y, SDL_BUTTON_LEFT, false);
      }
      break;
    }
    case SDL_EVENT_MOUSE_BUTTON_UP:
      if(e.button.which == SDL_PEN_MOUSEID) break;
      if(_gesture == EGesture::Pan && e.button.button == _gestureButton) {
        _gesture = EGesture::None;
        SDL_CaptureMouse(false);
      } else if(_gesture == EGesture::Tool || _gesture == EGesture::Axis) {
        pointer(2, e.button.x, e.button.y, e.button.button, false, 0);
      }
      break;
  }
}
#pragma endregion

#pragma region [ Tools ]
std::vector<int> App::stripOrder(bool shownOnly) const {
  std::vector<int> out;
  auto add = [&](int i) {
    if(std::find(out.begin(), out.end(), i) != out.end()) return;
    out.push_back(i);
  };
  for(auto& name : _cfg.toolOrder)
    for(int i = 0; i < int(_tools.size()); i++) if(name == _tools[i]->name) add(i);
  for(int i = 0; i < int(_tools.size()); i++) add(i);
  if(shownOnly) std::erase_if(out, [&](int i) { return toolHidden(*_tools[i]) && i != _tool; });
  return out;
}

bool App::toolHidden(const Tool& t) const {
  if(t.id == ETool::Node && !_cfg.betaVector) return true;
  return std::find(_cfg.hiddenTools.begin(), _cfg.hiddenTools.end(), t.name) != _cfg.hiddenTools.end();
}

void App::selectTool(int index) {
  if(index == _tool || index < 0 || index >= int(_tools.size())) return;
  commitTool();
  _previousTool = _tool;
  _tool = index;
  // The Node tool works on a vector layer: the nearest one above the active layer, else the topmost below
  // it, else a new one
  if(_tools[index]->id == ETool::Node && _cfg.betaVector && _doc->active().kind != ELayerKind::Vector) {
    int n = int(_doc->layers.size()), found = -1;
    for(int i = _doc->activeLayer + 1; i < n && found < 0; i++) if(_doc->layers[i]->kind == ELayerKind::Vector) found = i;
    for(int i = n - 1; i >= 0 && found < 0; i--) if(_doc->layers[i]->kind == ELayerKind::Vector) found = i;
    if(found >= 0) { _doc->activeLayer = found; _doc->onChanged(); }
    else _commands.run("layer.addVector");
  }
}

bool App::vectorMode() { return _cfg.betaVector && _doc->active().kind == ELayerKind::Vector; }

void App::useTool(ETool id) {
  for(int i = 0; i < int(_tools.size()); i++) if(_tools[i]->id == id) { selectTool(i); return; }
}

// Paint.NET: pressing the key of the current tool moves to the next tool sharing that key
void App::toolKey(ImGuiKey key) {
  std::vector<int> group;
  for(int i = 0; i < int(_tools.size()); i++) if(_tools[i]->key == key && !(_tools[i]->id == ETool::Node && !_cfg.betaVector)) group.push_back(i);
  if(group.empty()) return;
  auto it = std::find(group.begin(), group.end(), _tool);
  if(it != group.end() && tool().cycle(*this)) return;
  selectTool(it == group.end() ? group[0] : group[(it - group.begin() + 1) % group.size()]);
}

void App::commitTool() {
  if(_tools.empty() || !_doc) return;
  tool().commit(*this);
  if(_gesture == EGesture::Tool) { _gesture = EGesture::None; SDL_CaptureMouse(false); }
}
#pragma endregion

#pragma region [ Overlays ]
void App::drawOverlays() {
  ImDrawList* dl = ImGui::GetBackgroundDrawList();
  float d = ImGui::GetIO().DisplayFramebufferScale.x;
  auto& v = _nav.view;
  ImVec2 clipMin{ v.vpX / d, v.vpY / d }, clipMax{ (v.vpX + v.vpW) / d, (v.vpY + v.vpH) / d };
  dl->PushClipRect(clipMin, clipMax, true);

  // Marching ants from the selection's pixel-edge outline, rebuilt only when the selection changes
  if(_doc->hasSelection()) {
    if(_outlineSerial != _doc->selection->serial()) {
      _outline = maskOutline(*_doc->selection);
      _outlineSerial = _doc->selection->serial();
    }
    float phase = float(std::fmod(ImGui::GetTime() * 10, 1000.0));
    if(_outline.size() > 200000) {
      IRect b = _doc->selection->bounds();
      _outline = { { b.x0, b.y0, b.x1 + 1, b.y0 }, { b.x1 + 1, b.y0, b.x1 + 1, b.y1 + 1 }, { b.x0, b.y1 + 1, b.x1 + 1, b.y1 + 1 }, { b.x0, b.y0, b.x0, b.y1 + 1 } };
    }
    for(auto& s : _outline) {
      ImVec2 a = toScreen(s.x0, s.y0), b = toScreen(s.x1, s.y1);
      if(std::max(a.x, b.x) < clipMin.x || std::min(a.x, b.x) > clipMax.x || std::max(a.y, b.y) < clipMin.y || std::min(a.y, b.y) > clipMax.y) continue;
      antsLine(dl, a, b, phase, _dpi);
    }
  } else if(!_outline.empty()) {
    _outline.clear();
    _outlineSerial = 0;
  }

  drawSymmetry(dl);
  if(_cursorIn || tool().busy()) tool().overlay(*this, dl, _hover);
  dl->PopClipRect();
}
#pragma endregion

#pragma region [ Selection and layer commands ]
void App::selectionOp(const char* what) {
  Document& doc = *_doc;
  int w = doc.width(), h = doc.height();
  // On a vector layer Select All and Deselect pick the objects (shown by the Mouse tool)
  if(vectorMode() && (!std::strcmp(what, "all") || !std::strcmp(what, "none"))) {
    doc.selectedObjects.clear();
    if(!std::strcmp(what, "all")) {
      for(auto& o : vectorTree(doc.active(), doc.active().frame()).objects)
        if(o.visible && !o.locked) doc.selectedObjects.push_back(o.id);
      if(tool().id != ETool::Mouse && tool().id != ETool::Node) useTool(ETool::Mouse);
      return;
    }
    if(!doc.hasSelection()) return;
  }
  if(!std::strcmp(what, "all")) {
    auto m = std::make_unique<Mask>(w, h);
    m->fillAll();
    pushSelection(doc, std::move(m), N_("Select All"), ICON_FA_OBJECT_UNGROUP);
  } else if(!std::strcmp(what, "none")) {
    pushSelection(doc, std::make_unique<Mask>(w, h), N_("Deselect"), ICON_FA_XMARK);
  } else if(!std::strcmp(what, "invert")) {
    auto m = doc.selection->clone();
    m->invert();
    pushSelection(doc, std::move(m), N_("Invert Selection"), ICON_FA_CIRCLE_HALF_STROKE);
  } else if(doc.hasSelection()) {
    if(!rasterGuard([this, w = std::string(what)] { selectionOp(w.c_str()); })) return;
    bool erase = !std::strcmp(what, "erase");
    Stroke st(doc, doc.active(), erase ? EInk::Erase : EInk::Paint, _primary);
    IRect b = doc.selection->bounds();
    std::vector<uint8_t> row(b.width());
    for(int y = b.y0; y <= b.y1; y++) {
      doc.selection->readRow(y, b.x0, b.x1, row.data());
      for(int i = 0; i < b.width(); i++) if(row[i]) st.cover(b.x0 + i, y, 255);
    }
    if(auto edit = st.finish(erase ? N_("Erase Selection") : N_("Fill Selection"), erase ? ICON_FA_ERASER : ICON_FA_FILL_DRIP))
      doc.history.push(doc, std::move(edit));
  }
}

void App::layerOp(const char* what) {
  Document& doc = *_doc;
  int a = doc.activeLayer;
  if(!std::strcmp(what, "addVector")) {
    auto l = std::make_unique<Layer>(doc.width(), doc.height(), std::string(tr("Vector")) + " " + std::to_string(doc.layers.size() + 1));
    l->kind = ELayerKind::Vector;
    vectorTree(*l, 0).touch();
    addLayer(doc, std::move(l), a + 1, N_("Add New Vector Layer"), ICON_FA_BEZIER_CURVE);
  } else if(!std::strcmp(what, "add")) {
    // Paint.NET names new layers by count
    addLayer(doc, std::make_unique<Layer>(doc.width(), doc.height(), std::string(tr("Layer")) + " " + std::to_string(doc.layers.size() + 1)), a + 1, N_("Add New Layer"), ICON_FA_SQUARE_PLUS);
  } else if(!std::strcmp(what, "delete")) {
    deleteLayer(doc, a, ICON_FA_TRASH);
  } else if(!std::strcmp(what, "duplicate")) {
    auto copy = doc.active().clone();
    copy->name += std::string(" ") + tr("copy");
    addLayer(doc, std::move(copy), a + 1, N_("Duplicate Layer"), ICON_FA_CLONE);
  } else if(!std::strcmp(what, "merge")) {
    // Merging into a vector layer turns it into pixels first
    if(a > 0 && doc.layers[a - 1]->kind == ELayerKind::Vector) rasterizeLayer(a - 1);
    mergeDown(doc, a, ICON_FA_ANGLES_DOWN);
  } else if(!std::strcmp(what, "up")) {
    moveLayer(doc, a, a + 1, ICON_FA_ARROW_UP);
  } else if(!std::strcmp(what, "down")) {
    moveLayer(doc, a, a - 1, ICON_FA_ARROW_DOWN);
  } else if(!std::strcmp(what, "props")) {
    _propsBefore = LayerProps::of(doc.active());
    std::snprintf(_propsName, sizeof _propsName, "%s", doc.active().name.c_str());
    _popup = "layerProps";
  }
}
#pragma endregion

}
