#include "app/App.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace david {

static ImVec4 toVec(uint32_t c) {
  return { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f };
}

static uint32_t fromVec(const float* v) {
  auto b = [](float f) { return uint32_t(std::clamp(f, 0.f, 1.f) * 255 + 0.5f); };
  return b(v[0]) | (b(v[1]) << 8) | (b(v[2]) << 16) | (b(v[3]) << 24);
}

#pragma region [ Panels ]
static const char* panelKey(int which) {
  switch(which) {
    case 6: return "history"; case 7: return "layers"; case 9: return "tilepreview"; case 10: return "palette";
    case 11: return "timeline"; case 12: return "animpreview"; default: return "colors";
  }
}

// Where each panel floats in floating mode: its saved spot, or Paint.NET's corner of the work area
void App::floatingPlace(int which) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  auto saved = _cfg.floatRects.find(panelKey(which));
  if(saved != _cfg.floatRects.end() && saved->second.size() == 4) {
    const auto& r = saved->second;
    ImVec2 p = vp->WorkPos, s = vp->WorkSize;
    float w = std::min(r[2], s.x), h = std::min(r[3], s.y);
    ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
    ImGui::SetNextWindowPos({ std::clamp(r[0], p.x, p.x + s.x - w), std::clamp(r[1], p.y, p.y + s.y - h) }, ImGuiCond_Always);
    ImGui::SetNextWindowSize({ w, h }, ImGuiCond_Always);
    return;
  }
  ImVec2 p = vp->WorkPos, s = vp->WorkSize;
  float m = 12 * _dpi, w = 220 * _dpi;
  ImVec2 pos, size;
  // Colors top right; Layers and History below it, side by side
  float colorsH = 470 * _dpi, lowH = std::max(200 * _dpi, s.y - colorsH - m * 3);
  switch(which) {
    case 8: pos = { p.x + s.x - w - m, p.y + m }; size = { w, colorsH }; break;                                        // Colors
    case 7: pos = { p.x + s.x - w - m, p.y + s.y - lowH - m }; size = { w, lowH }; break;                              // Layers
    case 9: case 10: pos = { p.x + s.x - (w + m) * 2, p.y + m + (which - 9) * (w + m) }; size = { w, w }; break;       // Tile preview, palette
    case 12: pos = { p.x + s.x - (w + m) * 3, p.y + m }; size = { w, w }; break;                                       // Animation preview
    case 11: {                                                                                                          // Timeline, along the bottom
      float th = 190 * _dpi, left = 64 * _dpi;
      pos = { p.x + left, p.y + s.y - th - m }; size = { std::max(300 * _dpi, s.x - left - (w + m) * 2 - m), th };
      break;
    }
    default: pos = { p.x + s.x - (w + m) * 2, p.y + s.y - lowH - m }; size = { w, lowH }; break;                       // History
  }
  ImGui::SetNextWindowDockID(0, ImGuiCond_Always);
  ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
}

// The title shows the name next to the icon when the panel was wide enough last frame
bool App::beginPanel(int which, const char* icon, const char* name, const char* id, bool* open) {
  bool reset = _resetWindow == which || _floatReset;
  if(_cfg.floatingWindows) {
    // The pixel-art windows have no spot until they first open
    if(reset || (which >= 9 && !ImGui::FindWindowByName((std::string("###") + id).c_str()))) floatingPlace(which);
  } else if(which == 11 && (reset || (!ImGui::FindWindowByName("###timeline") && !ImGui::FindWindowSettingsByID(ImHashStr("###timeline"))))) {
    // The Timeline docks along the bottom of the canvas: a node split off the central one the first time
    ImGuiDockNode* node = _nodeTimeline ? ImGui::DockBuilderGetNode(_nodeTimeline) : nullptr;
    if(!node || !node->IsLeafNode()) {
      if(ImGuiDockNode* central = _dockId ? ImGui::DockBuilderGetCentralNode(_dockId) : nullptr) {
        ImGuiID rest = 0;
        _nodeTimeline = ImGui::DockBuilderSplitNode(central->ID, ImGuiDir_Down, 0.26f, nullptr, &rest);
        ImGui::DockBuilderFinish(_dockId);
      }
    }
    if(_nodeTimeline) ImGui::SetNextWindowDockID(_nodeTimeline, ImGuiCond_Always);
  } else if(reset) {
    ImGuiID nodes[] = { _nodeHistory, _nodeLayers, _nodeColors, _nodeLayers, _nodeLayers, 0, _nodeLayers };
    if(ImGuiID node = nodes[which - 6]) ImGui::SetNextWindowDockID(node, ImGuiCond_Always);
  } else if(which >= 9 && which != 11 && _nodeLayers) {
    // The pixel-art windows first open as tabs next to Layers
    ImGui::SetNextWindowDockID(_nodeLayers, ImGuiCond_FirstUseEver);
  }
  ImGuiWindowClass wc;
  wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
  ImGui::SetNextWindowClass(&wc);

  std::string full = std::string(icon) + "  " + tr(name);
  float need = ImGui::CalcTextSize(full.c_str()).x + ImGui::GetFrameHeight() * 2.5f;
  std::string title = (_panelWidth[which] >= need ? full : std::string(icon)) + "###" + id;
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | (_cfg.floatingWindows ? ImGuiWindowFlags_NoDocking : 0);
  // Floating Layers fits its button row
  if(_cfg.floatingWindows && which == 7) {
    const ImGuiStyle& st = ImGui::GetStyle();
    float fh = ImGui::GetFrameHeight();
    float inner = fh * 1.2f * 8 + 2 * _dpi * 7;
    float w = inner + st.WindowPadding.x * 2 + st.ScrollbarSize;
    ImGui::SetNextWindowSizeConstraints({ w, fh * 4 }, { w, FLT_MAX });
  }
  bool visible = ImGui::Begin(title.c_str(), open, flags);
  _panelWidth[which] = ImGui::GetWindowWidth();
  if(_cfg.floatingWindows) snapFloating(which);
  return visible;
}

// While a floating panel is dragged, its sides stick to the other panels' sides and the work area's edges
// when they come within a few pixels. Its rect is kept in the settings, so it reopens in the same place.
void App::snapFloating(int which) {
  ImGuiWindow* win = ImGui::GetCurrentWindow();
  ImVec2 pos = win->Pos, size = win->Size;
  if(GImGui->MovingWindow && GImGui->MovingWindow->RootWindow == win) {
    float t = 10 * _dpi;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    std::vector<float> xs = { vp->WorkPos.x, vp->WorkPos.x + vp->WorkSize.x }, ys = { vp->WorkPos.y, vp->WorkPos.y + vp->WorkSize.y };
    for(auto& [other, r] : _panelRect) {
      if(other == which) continue;
      xs.push_back(r.x); xs.push_back(r.x + r.z);
      ys.push_back(r.y); ys.push_back(r.y + r.w);
    }
    // Best snap per axis: either edge of this panel to any candidate line
    auto snap = [&](float start, float len, const std::vector<float>& lines) {
      float best = t + 1, out = start;
      for(float l : lines) {
        for(float edge : { start, start + len }) {
          float d = std::abs(edge - l);
          if(d < best) { best = d; out = start + (l - edge); }
        }
      }
      return out;
    };
    ImVec2 snapped = { snap(pos.x, size.x, xs), snap(pos.y, size.y, ys) };
    if(snapped.x != pos.x || snapped.y != pos.y) { ImGui::SetWindowPos(snapped); pos = snapped; }
  }
  _panelRect[which] = { pos.x, pos.y, size.x, size.y };
  _cfg.floatRects[panelKey(which)] = { pos.x, pos.y, size.x, size.y };
}

// Colors on the top right, Layers and History as tabs below it, canvas everywhere else (the tools are a strip)
void App::buildDefaultLayout(ImGuiID dock) {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dock);
  ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
  ImGuiID center = dock;
  // About 280 px wide (more on very wide windows), like the reference layout
  float ratio = std::clamp(280 * _dpi / std::max(vp->WorkSize.x, 1.f), 0.15f, 0.35f);
  ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, ratio, nullptr, &center);
  ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.36f, nullptr, &right);
  _nodeColors = right; _nodeHistory = rightBottom; _nodeLayers = rightBottom;
  if(!_cfg.floatingWindows) {
    ImGui::DockBuilderDockWindow("###colors", right);
    ImGui::DockBuilderDockWindow("###layers", rightBottom);
    ImGui::DockBuilderDockWindow("###history", rightBottom);
    // Layers is the open tab (focused once its window exists)
    _focusLayers = true;
  }
  ImGui::DockBuilderFinish(dock);
}
#pragma endregion

#pragma region [ Tool bar ]
void App::drawToolBar() {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 8 * _dpi, 4 * _dpi });
  ImGui::PushStyleColor(ImGuiCol_WindowBg, _theme.bg);
  float h = ImGui::GetFrameHeight() + 8 * _dpi;
  if(ImGui::BeginViewportSideBar("##toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, h, flags)) {
    tool().options(*this);
    // Options every tool shares, after a divider
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1);
    ImGui::SameLine();
    ui::iconToggle(ICON_TB_WAVE_SINE, ICON_TB_WAVE_SINE, _cfg.tools.antialias, tr("Antialiasing"));
    ImGui::SameLine();
    ui::iconToggle(ICON_PH_CHECKERBOARD, ICON_PH_CHECKERBOARD, _cfg.tools.pixelPerfect, tr("Pixel perfect (1-px lines without doubled corners)"));
    ImGui::SameLine();
    ui::iconToggle(ICON_TB_REPLACE, ICON_TB_REPLACE, _cfg.tools.replaceAlpha, tr("Replace: the pencil and brushes put the color's alpha in place of the pixel's, instead of blending over it"));
    pixelArtBar();
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}
#pragma endregion

#pragma region [ Tool strip ]
// The tools as a fixed strip on the left: one column, a second one only when the window is too short
void App::drawToolStrip() {
  ImGuiViewport* vp = ImGui::GetMainViewport();
  float s = ImGui::GetFrameHeight() * 1.5f, gap = 4 * _dpi, pad = 6 * _dpi;
  int perCol = std::max(1, int((vp->WorkSize.y - pad * 2 + gap) / (s + gap)));
  // Hidden tools stay out (the current one shows while it's selected by its shortcut)
  std::vector<int> order = stripOrder(true);
  int cols = std::max(1, (int(order.size()) + perCol - 1) / perCol);
  float w = cols * s + (cols - 1) * gap + pad * 2;
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { pad, pad });
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { gap, gap });
  ImGui::PushStyleColor(ImGuiCol_WindowBg, _theme.bg);
  if(ImGui::BeginViewportSideBar("##tools", vp, ImGuiDir_Left, w, flags)) {
    int rows = std::max(1, (int(order.size()) + cols - 1) / cols);
    for(int k = 0; k < int(order.size()); k++) {
      int i = order[k];
      // Column-major: the strip reads top to bottom
      int col = k / rows, row = k % rows;
      ImGui::SetCursorPos({ pad + col * (s + gap), pad + row * (s + gap) });
      Tool& t = *_tools[i];
      Command* c = _commands.find(std::string("tool.") + ImGui::GetKeyName(t.key));
      std::string tip = tr(t.name);
      if(c && !c->chords.empty()) tip += "  (" + Commands::chordName(c->chords[0]) + ")";
      ImGui::PushID(i);
      if(ui::iconButton("##t", t.icon, { s, s }, i == _tool, tip.c_str())) selectTool(i);
      ImGui::PopID();
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
}
#pragma endregion

#pragma region [ History window ]
void App::drawHistoryWindow() {
  if(!_cfg.showHistory) return;
  if(beginPanel(6, ICON_FA_CLOCK_ROTATE_LEFT, N_("History"), "history", &_cfg.showHistory)) {
    History& hist = _doc->history;
    float buttons = ImGui::GetFrameHeightWithSpacing();
    ImGui::BeginChild("##steps", { 0, -buttons });
    bool scroll = _historySeen != hist.index();
    _historySeen = hist.index();
    int jump = -1;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2 * _dpi });
    for(int i = 0; i <= hist.count(); i++) {
      ImGui::PushID(i);
      ImVec2 a, b;
      if(ui::listRow("##step", i == hist.index(), ImGui::GetFrameHeight(), a, b)) jump = i;
      if(scroll && i == hist.index()) ImGui::SetScrollHereY();
      const char* icon = i == 0 ? ICON_FA_IMAGE : hist.at(i - 1).icon;
      const char* name = i == 0 ? tr(_doc->path.empty() ? "New Image" : "Open Image") : tr(hist.at(i - 1).name.c_str());
      ui::rowLabel(icon, name, a, b, ImGui::GetColorU32(i > hist.index() ? _theme.textMuted : _theme.text));
      ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndChild();
    if(jump >= 0) { commitTool(); hist.goTo(*_doc, jump); }
    commandButton("edit.undo");
    ImGui::SameLine(0, 2 * _dpi);
    commandButton("edit.redo");
  }
  ImGui::End();
}

// A flat icon button for a command, disabled when the command is, with its name and key as the tooltip
void App::commandButton(const char* id) {
  Command* c = _commands.find(id);
  if(!c) return;
  bool on = !c->enabled || c->enabled();
  std::string tip = tr(c->label.c_str());
  if(!c->chords.empty()) tip += "  (" + Commands::chordName(c->chords[0]) + ")";
  ImGui::BeginDisabled(!on);
  if(ui::iconButton(id, c->icon, { ImGui::GetFrameHeight() * 1.2f, 0 })) _commands.run(id);
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("%s", tip.c_str());
}
#pragma endregion

#pragma region [ Layers window ]
void App::drawLayersWindow() {
  if(!_cfg.showLayers) return;
  if(_focusLayers) { ImGui::SetNextWindowFocus(); _focusLayers = false; }
  if(beginPanel(7, ICON_FA_LAYER_GROUP, N_("Layers"), "layers", &_cfg.showLayers)) {
    float buttons = ImGui::GetFrameHeightWithSpacing();
    float thumb = 36 * _dpi;
    ImGui::BeginChild("##list", { 0, -buttons });
    int select = -1, toggle = -1, props = -1;
    float rowH = thumb + 6 * _dpi;
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2 * _dpi });
    for(int i = int(_doc->layers.size()) - 1; i >= 0; i--) {
      Layer& l = *_doc->layers[i];
      ImGui::PushID(int(l.id()));
      ImVec2 ra, rb;
      if(ui::listRow("##row", i == _doc->activeLayer, rowH, ra, rb, true)) select = i;
      if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) props = i;
      ImVec2 after = ImGui::GetCursorScreenPos();
      ImVec2 rowPos = { ra.x + ImGui::GetStyle().FramePadding.x * 0.5f, ra.y + (rowH - thumb) / 2 };

      // Eye, thumbnail over a checkerboard, then the name
      float s = ImGui::GetFrameHeight();
      ImGui::SetCursorScreenPos({ rowPos.x, rowPos.y + (thumb - s) / 2 });
      if(ui::iconButton("##eye", l.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, { s, s }, false, tr(l.visible ? "Hide" : "Show"))) toggle = i;
      ImGui::SameLine();
      Thumbnails::Thumb t = _thumbs.get(l, int(thumb));
      ImVec2 p = { ImGui::GetCursorScreenPos().x, rowPos.y };
      // Downscaled thumbnails draw 1:1 on whole pixels; a small image is scaled up (nearest)
      float fit = std::max(t.w, t.h) < int(thumb) ? thumb / std::max(t.w, t.h) : 1.f;
      float tw = std::round(t.w * fit), th = std::round(t.h * fit);
      ImVec2 tp = { std::round(p.x + (thumb - tw) / 2), std::round(p.y + (thumb - th) / 2) };
      ImDrawList* dl = ImGui::GetWindowDrawList();
      dl->AddRectFilled(tp, { tp.x + tw, tp.y + th }, ImGui::GetColorU32(_theme.checkerA));
      float cell = 4 * _dpi;
      for(float y = 0; y < th; y += cell)
        for(float x = int((y / cell)) % 2 ? cell : 0; x < tw; x += cell * 2)
          dl->AddRectFilled({ tp.x + x, tp.y + y }, { tp.x + std::min(x + cell, tw), tp.y + std::min(y + cell, th) }, ImGui::GetColorU32(_theme.checkerB));
      dl->AddImage(ImTextureRef(ImTextureID(t.tex)), tp, { tp.x + tw, tp.y + th });
      float ty = ImTrunc((ra.y + rb.y - ImGui::GetTextLineHeight()) / 2);
      dl->PushClipRect(ra, rb, true);
      dl->AddText({ p.x + thumb + ImGui::GetStyle().ItemSpacing.x, ty }, ImGui::GetColorU32(l.visible ? _theme.text : _theme.textMuted), l.name.c_str());
      dl->PopClipRect();
      ImGui::SetCursorScreenPos(after);
      ImGui::PopID();
    }
    ImGui::Dummy({ 0, 0 });  // ImGui wants an item after the last cursor move
    ImGui::PopStyleVar();
    ImGui::EndChild();

    if(select >= 0 && select != _doc->activeLayer) { commitTool(); _doc->activeLayer = select; _doc->onChanged(); }
    if(toggle >= 0) {
      commitTool();
      LayerProps p = LayerProps::of(*_doc->layers[toggle]);
      p.visible = !p.visible;
      setLayerProps(*_doc, toggle, p, p.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH);
    }
    if(props >= 0) { _doc->activeLayer = props; _commands.run("layer.props"); }

    for(auto id : { "layer.add", "layer.delete", "layer.duplicate", "layer.merge", "layer.up", "layer.down", "layer.props" }) {
      commandButton(id);
      ImGui::SameLine(0, 2 * _dpi);
    }
    std::string tip = std::string(tr("Layer onion: ghost the layers next to this one")) + "  (Alt+L)  · " + tr("right click: options");
    ui::iconToggle("##layeronion", ICON_TB_GHOST_2, _cfg.anim.layerOnion, tip.c_str());
    if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##layeronionopts");
    if(ImGui::BeginPopup("##layeronionopts")) { layerOnionOptions(); ImGui::EndPopup(); }
    ImGui::NewLine();
  }
  ImGui::End();
}
#pragma endregion

#pragma region [ Colors window ]
// One Paint.NET-style band: a gradient bar with a marker, dragged to set the value, plus a number field.
// colorAt(t) gives the bar color at t in 0..1.
static bool band(const char* id, const char* letter, float& value, float max, float dpi, const std::function<ImVec4(float)>& colorAt) {
  ImGui::PushID(id);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", letter);
  ImGui::SameLine(18 * dpi);
  bool changed = ui::band("##band", value, 0, max, 1, dpi, colorAt);
  ImGui::PopID();
  return changed;
}

void App::drawColorsWindow() {
  if(!_cfg.showColors) return;
  if(beginPanel(8, ICON_FA_PALETTE, N_("Colors"), "colors", &_cfg.showColors)) {
    float s = ImGui::GetFrameHeight() * 1.4f;
    ImGuiColorEditFlags swatch = ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoTooltip;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    auto swatchButton = [&](const char* id, uint32_t c, bool editing, const char* tip) {
      bool r = ImGui::ColorButton(id, toVec(c), swatch, { s, s });
      if(editing) dl->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(_theme.accent), 0, 0, 2 * _dpi);
      ImGui::SetItemTooltip("%s", tip);
      return r;
    };
    ImVec2 rowTop = ImGui::GetCursorScreenPos();
    if(swatchButton("##primary", _primary, _editPrimary, tr("Primary"))) _editPrimary = true;
    ImGui::SameLine();
    if(swatchButton("##secondary", _secondary, !_editPrimary, tr("Secondary"))) _editPrimary = false;
    ImGui::SameLine();
    std::string swapTip = std::string(tr("Swap")) + "  (X)";
    if(ui::iconButton("##swap", ICON_FA_RIGHT_LEFT, { s * 0.8f, s }, false, swapTip.c_str())) std::swap(_primary, _secondary);
    ImGui::SameLine(0, 0);
    if(ui::iconButton("##bw", ICON_FA_CIRCLE_HALF_STROKE, { s * 0.8f, s }, false, tr("Black and white"))) { _primary = BLACK; _secondary = WHITE; }
    // Picker shape: a plain icon of the current shape in the top-right corner; click to switch
    ImVec2 next = ImGui::GetCursorScreenPos();
    float ib = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos({ ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - ib, rowTop.y });
    if(ui::iconButton("##shape", _cfg.colorSquare ? ICON_FA_SQUARE : ICON_FA_CIRCLE, { ib, ib }, false, tr(_cfg.colorSquare ? "Hue wheel" : "Square picker")))
      _cfg.colorSquare = !_cfg.colorSquare;
    ImGui::SetCursorScreenPos(next);

    uint32_t& target = _editPrimary ? _primary : _secondary;
    ImVec4 v = toVec(target);
    float col[4] = { v.x, v.y, v.z, v.w };
    ImGuiColorEditFlags pf = (_cfg.colorSquare ? ImGuiColorEditFlags_PickerHueBar : ImGuiColorEditFlags_PickerHueWheel)
                           | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel
                           | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_NoInputs;
    // The palette goes to the right of the picker when there's room for six cells there, else below it
    // Widths always leave room for the scrollbar: the picker's height follows its width, so letting the
    // scrollbar change the width made the window flip between two sizes
    const ImGuiStyle& st = ImGui::GetStyle();
    float cell = 16 * _dpi, step = cell + 2 * _dpi;
    float avail = ImGui::GetWindowWidth() - st.WindowPadding.x * 2 - st.ScrollbarSize;
    float pickerW = std::min(avail, 260 * _dpi);
    bool beside = avail - pickerW - ImGui::GetStyle().ItemSpacing.x >= step * 6;
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(pickerW);
    if(ImGui::ColorPicker4("##picker", col, pf)) target = fromVec(col);

    // Hex right under the picker, always
    char hex[16];
    std::snprintf(hex, sizeof hex, "%02X%02X%02X%02X", target & 0xFF, (target >> 8) & 0xFF, (target >> 16) & 0xFF, target >> 24);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("#");
    ImGui::SameLine(0, 4 * _dpi);
    ImGui::SetNextItemWidth(pickerW - ImGui::CalcTextSize("#").x - 4 * _dpi);
    if(ImGui::InputText("##hex", hex, sizeof hex, ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
      unsigned r = 0, g = 0, b = 0, a = 255;
      if(std::strlen(hex) >= 6 && std::sscanf(hex, "%02x%02x%02x", &r, &g, &b) == 3) {
        if(std::strlen(hex) >= 8) std::sscanf(hex + 6, "%02x", &a);
        target = r | (g << 8) | (b << 16) | (a << 24);
      }
    }
    ImGui::EndGroup();

    // Palette: left click sets the primary color, right click the secondary
    if(beside) ImGui::SameLine();
    else ImGui::Spacing();
    ImGui::BeginGroup();
    float palW = beside ? avail - pickerW - st.ItemSpacing.x : avail;
    int perRow = std::max(1, int((palW + 2 * _dpi) / step));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2 * _dpi, 2 * _dpi });
    // The current palette (the Palette window edits it)
    const auto& pal = _cfg.tools.palette.colors;
    for(int i = 0; i < int(pal.size()); i++) {
      if(i % perRow) ImGui::SameLine();
      ImGui::PushID(i);
      // Left click sets the color being edited (primary or secondary), right click the other one
      if(ImGui::ColorButton("##p", toVec(pal[i]), swatch, { cell, cell })) (_editPrimary ? _primary : _secondary) = pal[i];
      if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) (_editPrimary ? _secondary : _primary) = pal[i];
      ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::EndGroup();

    // A quiet hint that opens the RGB/HSV/alpha bands under the palette
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, _theme.textMuted);
    ImGui::PushFont(nullptr, _theme.fontSize * 0.85f);
    std::string hint = std::string(_cfg.colorAdvanced ? ICON_FA_CHEVRON_DOWN : ICON_FA_CHEVRON_RIGHT) + "  RGB · HSV · A##bands";
    if(ImGui::Selectable(hint.c_str(), false, 0, { ImGui::CalcTextSize("RGB · HSV · A").x + ImGui::GetFontSize() * 2, 0 })) _cfg.colorAdvanced = !_cfg.colorAdvanced;
    ImGui::PopFont();
    ImGui::PopStyleColor();

    if(_cfg.colorAdvanced) {
      // RGB, HSV and alpha bands; the hue is kept while saturation or value is 0
      float rgba[4] = { float(target & 0xFF), float((target >> 8) & 0xFF), float((target >> 16) & 0xFF), float(target >> 24) };
      float h, sat, val;
      ImGui::ColorConvertRGBtoHSV(rgba[0] / 255, rgba[1] / 255, rgba[2] / 255, h, sat, val);
      if((sat == 0 || val == 0) && _hsvTarget == target) h = _hue;
      float hsv[3] = { std::round(h * 360), std::round(sat * 100), std::round(val * 100) };
      bool rgbChanged = false, hsvChanged = false;
      auto rgbAt = [&](int ch) {
        return [&rgba, ch](float t) { ImVec4 c = { rgba[0] / 255, rgba[1] / 255, rgba[2] / 255, 1 }; (&c.x)[ch] = t; return c; };
      };
      rgbChanged |= band("r", "R", rgba[0], 255, _dpi, rgbAt(0));
      rgbChanged |= band("g", "G", rgba[1], 255, _dpi, rgbAt(1));
      rgbChanged |= band("b", "B", rgba[2], 255, _dpi, rgbAt(2));
      auto hsvAt = [&](int ch) {
        return [&hsv, ch](float t) {
          float k[3] = { hsv[0] / 360, hsv[1] / 100, hsv[2] / 100 };
          k[ch] = t;
          ImVec4 c{ 0, 0, 0, 1 };
          ImGui::ColorConvertHSVtoRGB(std::min(k[0], 0.9999f), k[1], k[2], c.x, c.y, c.z);
          return c;
        };
      };
      hsvChanged |= band("h", "H", hsv[0], 360, _dpi, hsvAt(0));
      hsvChanged |= band("s", "S", hsv[1], 100, _dpi, hsvAt(1));
      hsvChanged |= band("v", "V", hsv[2], 100, _dpi, hsvAt(2));
      bool aChanged = band("a", "A", rgba[3], 255, _dpi, [&rgba](float t) { return ImVec4(rgba[0] / 255, rgba[1] / 255, rgba[2] / 255, t); });
      if(hsvChanged) {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(std::min(hsv[0] / 360, 0.9999f), hsv[1] / 100, hsv[2] / 100, r, g, b);
        float c[4] = { r, g, b, rgba[3] / 255 };
        target = fromVec(c);
        _hue = hsv[0] / 360;
      } else if(rgbChanged || aChanged) {
        float c[4] = { rgba[0] / 255, rgba[1] / 255, rgba[2] / 255, rgba[3] / 255 };
        target = fromVec(c);
      } else {
        _hue = h;
      }
      _hsvTarget = target;
    }
  }
  ImGui::End();
}
#pragma endregion

#pragma region [ Pen probe ]
void App::drawPenProbe() {
  if(!_cfg.showProbe) return;
  ImGui::SetNextWindowSize({ 240 * _dpi, 0 }, ImGuiCond_FirstUseEver);
  std::string title = std::string(ICON_FA_PEN_NIB "  ") + tr("Pen Probe") + "###probe";
  if(ImGui::Begin(title.c_str(), &_cfg.showProbe, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextDisabled(ICON_FA_GAUGE);
    ImGui::SetItemTooltip("%s", tr("Pressure"));
    ImGui::SameLine();
    ImGui::ProgressBar(_probe.pressure, { 160 * _dpi, 0 });
    ImGui::TextDisabled(ICON_FA_ARROWS_LEFT_RIGHT);
    ImGui::SameLine();
    ImGui::Text("%+.0f°", _probe.tiltX);
    ImGui::SameLine(0, 16 * _dpi);
    ImGui::TextDisabled(ICON_FA_ARROWS_UP_DOWN);
    ImGui::SameLine();
    ImGui::Text("%+.0f°", _probe.tiltY);
    ImGui::TextColored(_probe.down ? _theme.good : _theme.textMuted, ICON_FA_CIRCLE_DOT);
    ImGui::SetItemTooltip("%s", tr("Touching"));
    ImGui::SameLine();
    ImGui::TextColored(_probe.eraser ? _theme.warn : _theme.textMuted, ICON_FA_ERASER);
    ImGui::SetItemTooltip("%s", tr("Eraser end"));
    ImGui::SameLine(0, 16 * _dpi);
    ImGui::TextDisabled("#%u  %d / %d", unsigned(_probe.id), _probe.events, _probe.axisEvents);
    ImGui::SetItemTooltip("%s", tr("Pen id, pen events / pressure and tilt events"));
  }
  ImGui::End();
}
#pragma endregion

}
