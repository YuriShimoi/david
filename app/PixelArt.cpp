#include "core/Text.h"
#include "app/App.h"
#include "core/Autotile.h"
#include "core/Resize.h"
#include "core/Stroke.h"
#include "core/io/Png.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace david {

static ImVec4 toVec(uint32_t c) {
  return { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f };
}

#pragma region [ Tool bar ]
// A tool bar button that opens a small menu of choices; lit while the value isn't 0
static void choiceButton(const char* id, const char* icon, int& value, const char* tip, std::initializer_list<const char*> names) {
  bool on = value != 0;
  if(ui::iconToggle(id, icon, on, tr(tip))) ImGui::OpenPopup(id);
  if(ImGui::BeginPopup(id)) {
    int i = 0;
    for(const char* n : names) {
      if(ImGui::MenuItem(tr(n), nullptr, value == i)) value = i;
      i++;
    }
    ImGui::EndPopup();
  }
}

void App::pixelArtBar() {
  auto& t = _cfg.tools;
  ImGui::SameLine();
  choiceButton("##symmetry", ICON_TB_FLIP_VERTICAL, t.symmetry, "Symmetry", { N_("Off"), N_("Left and right"), N_("Top and bottom"), N_("Both") });
  ImGui::SameLine();
  choiceButton("##tiled", ICON_TB_GRID_4X4, t.tiled, N_("Tiled mode (strokes wrap around the edges)"), { N_("Off"), N_("Wrap X"), N_("Wrap Y"), N_("Wrap both") });
  ImGui::SameLine();
  ui::iconToggle("##grid", ICON_TB_GRID_3X3, t.grid, tr("Grid (right click: size)"));
  if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) _popup = "###grid";
  if(t.grid) {
    ImGui::SameLine();
    ui::iconToggle("##snap", ICON_PH_MAGNET, t.gridSnap, tr("Snap to grid"));
  }
}

void App::drawGridDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(ICON_FA_TABLE_CELLS "  ") + tr("Grid") + "###grid";
  if(!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
  auto& t = _cfg.tools;
  float w = 90 * _dpi;
  ImGui::SetNextItemWidth(w); ImGui::InputInt(ICON_FA_ARROWS_LEFT_RIGHT "##gw", &t.gridW, 0);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(w); ImGui::InputInt(ICON_FA_ARROWS_UP_DOWN "##gh", &t.gridH, 0);
  ImGui::SetNextItemWidth(w); ImGui::InputInt("X##gx", &t.gridX, 0);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(w); ImGui::InputInt("Y##gy", &t.gridY, 0);
  t.gridW = std::clamp(t.gridW, 1, 4096); t.gridH = std::clamp(t.gridH, 1, 4096);
  ImGui::Checkbox(tr("Show"), &t.grid);
  ImGui::SameLine();
  ImGui::Checkbox(tr("Snap"), &t.gridSnap);
  ImGui::Spacing();
  if(ui::dialogButton(tr("OK"), true, 100 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}
#pragma endregion

#pragma region [ Symmetry axis ]
// Nub positions of the axes: the vertical axis has its nub above the image, the horizontal one left of it
static constexpr float AXIS_GAP = 14;
static constexpr float AXIS_SNAP = 8;  // screen px around the image center where a dragged axis snaps to it

void App::drawSymmetry(ImDrawList* dl) {
  auto& t = _cfg.tools;
  if(!t.symmetry) return;
  StrokeSpace sp = strokeSpace(t, *_doc);
  ImU32 col = ImGui::GetColorU32(_theme.accent);
  if(sp.mirrorX) {
    ImVec2 a = toScreen(sp.axisX, 0.0), b = toScreen(sp.axisX, _doc->height());
    dl->AddLine(a, b, col, 1.5f * _dpi);
    drawNub(dl, { a.x, a.y - AXIS_GAP * _dpi }, _dpi, _axisDrag == 1);
  }
  if(sp.mirrorY) {
    ImVec2 a = toScreen(0.0, sp.axisY), b = toScreen(_doc->width(), sp.axisY);
    dl->AddLine(a, b, col, 1.5f * _dpi);
    drawNub(dl, { a.x - AXIS_GAP * _dpi, a.y }, _dpi, _axisDrag == 2);
  }
}

int App::axisAt(const ToolInput& in) {
  auto& t = _cfg.tools;
  if(!t.symmetry) return 0;
  StrokeSpace sp = strokeSpace(t, *_doc);
  if(sp.mirrorX) { ImVec2 a = toScreen(sp.axisX, 0.0); if(overNub(in, { a.x, a.y - AXIS_GAP * _dpi }, _dpi)) return 1; }
  if(sp.mirrorY) { ImVec2 a = toScreen(0.0, sp.axisY); if(overNub(in, { a.x - AXIS_GAP * _dpi, a.y }, _dpi)) return 2; }
  return 0;
}

void App::dragAxis(const ToolInput& in) {
  // Half-pixel steps: the axis sits on a pixel edge or a pixel center. Near the image center it snaps there
  // (-1 keeps it centered when the canvas is resized); Alt drags freely
  auto& t = _cfg.tools;
  float snap = in.alt ? 0 : AXIS_SNAP * _dpi;
  if(_axisDrag == 1) {
    double cx = _doc->width() / 2.0;
    t.symX = std::abs(toScreen(in.x, 0.0).x - toScreen(cx, 0.0).x) < snap ? -1 : std::clamp(std::round(in.x * 2) / 2, 0.0, double(_doc->width()));
  }
  if(_axisDrag == 2) {
    double cy = _doc->height() / 2.0;
    t.symY = std::abs(toScreen(0.0, in.y).y - toScreen(0.0, cy).y) < snap ? -1 : std::clamp(std::round(in.y * 2) / 2, 0.0, double(_doc->height()));
  }
}
#pragma endregion

#pragma region [ Tile preview ]
void App::drawTilePreview() {
  if(!_cfg.showTilePreview) return;
  if(beginPanel(9, ICON_FA_TABLE_CELLS_LARGE, N_("Tile Preview"), "tilepreview", &_cfg.showTilePreview)) {
    _preview.update(*_doc, 1024, SDL_GetTicks());
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // 3 × 3 copies; the wheel zooms in steps, double-click fits
    float fit = std::min(avail.x / (_preview.width() * 3.f), avail.y / (_preview.height() * 3.f));
    if(_previewZoom <= 0) _previewZoom = fit;
    float z = _previewZoom;
    ImVec2 size = { _preview.width() * 3 * z, _preview.height() * 3 * z };
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tiles", { std::max(avail.x, 1.f), std::max(avail.y, 1.f) });
    if(ImGui::IsItemHovered()) {
      float wheel = ImGui::GetIO().MouseWheel;
      if(wheel != 0) _previewZoom = std::clamp(z * (wheel > 0 ? 1.25f : 0.8f), 0.05f, 64.f);
      if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) _previewZoom = fit;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 c = { p.x + std::max(0.f, (avail.x - size.x) / 2), p.y + std::max(0.f, (avail.y - size.y) / 2) };
    dl->PushClipRect(p, { p.x + avail.x, p.y + avail.y }, true);
    dl->AddRectFilled(c, { c.x + size.x, c.y + size.y }, ImGui::GetColorU32(_theme.checkerA));
    dl->AddImage(ImTextureRef(ImTextureID(_preview.tex())), c, { c.x + size.x, c.y + size.y }, { 0, 0 }, { 3, 3 });
    dl->PopClipRect();
  }
  ImGui::End();
}
#pragma endregion

#pragma region [ Palette ]
void App::drawPaletteWindow() {
  if(!_cfg.showPalette) return;
  if(beginPanel(10, ICON_FA_SWATCHBOOK, N_("Palette"), "palette", &_cfg.showPalette)) {
    Palette& pal = _cfg.tools.palette;
    float btn = ImGui::GetFrameHeight();
    auto bar = [&](const char* id, const char* icon, const char* tip) { bool r = ui::iconButton(id, icon, { btn, btn }, false, tr(tip)); ImGui::SameLine(0, 2 * _dpi); return r; };
    if(bar("##add", ICON_FA_PLUS, N_("Add the primary color"))) { pal.colors.push_back(_primary | 0xFF000000u); _palSel = pal.size() - 1; }
    ImGui::BeginDisabled(_palSel < 0 || _palSel >= pal.size());
    if(bar("##del", ICON_FA_MINUS, N_("Remove the selected swatch"))) { pal.colors.erase(pal.colors.begin() + _palSel); _palSel = std::min(_palSel, pal.size() - 1); }
    ImGui::EndDisabled();
    if(bar("##hue", ICON_FA_RAINBOW, N_("Sort by hue"))) pal.sortByHue();
    if(bar("##light", ICON_FA_ARROW_DOWN_WIDE_SHORT, N_("Sort by lightness"))) pal.sortByLightness();
    if(bar("##fromimg", ICON_FA_WAND_MAGIC, N_("Palette from image"))) ImGui::OpenPopup("##fromimage");
    if(bar("##load", ICON_FA_FOLDER_OPEN, N_("Load palette (GPL, PAL, HEX)"))) showPaletteDialog(false);
    if(bar("##save", ICON_FA_FLOPPY_DISK, N_("Save palette"))) showPaletteDialog(true);
    if(bar("##reset", ICON_FA_ROTATE_LEFT, N_("Default palette"))) { pal = Palette::defaults(); _palSel = -1; }
    ImGui::NewLine();
    if(ImGui::BeginPopup("##fromimage")) {
      ImGui::SetNextItemWidth(80 * _dpi);
      ImGui::InputInt(tr("colors"), &_palFromImage, 0);
      _palFromImage = std::clamp(_palFromImage, 1, 256);
      if(ui::dialogButton(tr("OK"), true, 80 * _dpi)) {
        Palette p = Palette::fromImage(*_doc, _palFromImage);
        if(!p.empty()) { pal = std::move(p); _palSel = -1; }
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Swatches: click = primary, right click = secondary, double-click edits, drag to reorder
    float cell = 18 * _dpi, step = cell + 2 * _dpi;
    float avail = ImGui::GetContentRegionAvail().x;
    int perRow = std::max(1, int((avail + 2 * _dpi) / step));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2 * _dpi, 2 * _dpi });
    int moveFrom = -1, moveTo = -1;
    for(int i = 0; i < pal.size(); i++) {
      if(i % perRow) ImGui::SameLine();
      ImGui::PushID(i);
      ImVec2 a = ImGui::GetCursorScreenPos();
      if(ImGui::ColorButton("##s", toVec(pal.colors[i]), ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoDragDrop, { cell, cell })) { _primary = pal.colors[i]; _palSel = i; }
      if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) { _secondary = pal.colors[i]; _palSel = i; }
      if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) { _palEdit = i; ImGui::OpenPopup("##edit"); }
      if(ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("david.swatch", &i, sizeof i);
        ImGui::ColorButton("##drag", toVec(pal.colors[i]), ImGuiColorEditFlags_NoAlpha, { cell, cell });
        ImGui::EndDragDropSource();
      }
      if(ImGui::BeginDragDropTarget()) {
        if(const ImGuiPayload* p = ImGui::AcceptDragDropPayload("david.swatch")) { moveFrom = *static_cast<const int*>(p->Data); moveTo = i; }
        ImGui::EndDragDropTarget();
      }
      if(i == _palSel) ImGui::GetWindowDrawList()->AddRect({ a.x - 1, a.y - 1 }, { a.x + cell + 1, a.y + cell + 1 }, ImGui::GetColorU32(_theme.accent), 0, 0, 2 * _dpi);
      if(ImGui::BeginPopup("##edit")) {
        if(_palEdit >= 0 && _palEdit < pal.size()) {
          ImVec4 v = toVec(pal.colors[_palEdit]);
          float c[3] = { v.x, v.y, v.z };
          if(ImGui::ColorPicker3("##pick", c, ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoSidePreview))
            pal.colors[_palEdit] = uint32_t(c[0] * 255 + 0.5f) | uint32_t(c[1] * 255 + 0.5f) << 8 | uint32_t(c[2] * 255 + 0.5f) << 16 | 0xFF000000u;
        }
        ImGui::EndPopup();
      }
      ImGui::PopID();
    }
    ImGui::PopStyleVar();
    if(moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo) {
      uint32_t c = pal.colors[moveFrom];
      pal.colors.erase(pal.colors.begin() + moveFrom);
      pal.colors.insert(pal.colors.begin() + moveTo, c);
      _palSel = moveTo;
    }
  }
  ImGui::End();
}

void App::paletteFileChosen(const std::string& path, bool save) {
  std::string err;
  Palette& pal = _cfg.tools.palette;
  if(save) {
    std::string p = path;
    if(!pathFromUtf8(p).has_extension()) p += ".gpl";
    if(!pal.save(p, err)) { _error = err; _popup = "error"; }
  } else {
    Palette next;
    if(next.load(path, err)) { pal = std::move(next); _palSel = -1; }
    else { _error = err; _popup = "error"; }
  }
}
#pragma endregion

#pragma region [ Autotile ]
void App::drawAutotileDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(ICON_FA_PUZZLE_PIECE "  ") + tr("Autotile") + "###autotile";
  if(!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
  auto& a = _autotile;
  // The source: border tile then floor tile, at the selection's top-left (or the image's)
  IRect src = _doc->hasSelection() ? _doc->selection->bounds() : IRect{ 0, 0, _doc->width() - 1, _doc->height() - 1 };
  ImGui::TextDisabled("%s %d, %d", tr("Source at"), src.x0, src.y0);
  float w = 90 * _dpi;
  ImGui::SetNextItemWidth(w); ImGui::InputInt(tr("Tile size"), &a.grid, 0);
  ImGui::SetNextItemWidth(w); ImGui::InputInt(tr("Border"), &a.border, 0);
  ImGui::SetNextItemWidth(w); ImGui::InputInt(tr("Corner"), &a.corner, 0);
  a.grid = std::clamp(a.grid, 2, 512); a.border = std::clamp(a.border, 0, a.grid); a.corner = std::clamp(a.corner, 0, a.grid);
  ImGui::Spacing();
  bool ok = ui::dialogButton(tr("OK"), true, 100 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Enter);
  ImGui::SameLine();
  bool cancel = ui::dialogButton(tr("Cancel"), false, 100 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Escape);
  if(ok) { runAutotile(); ImGui::CloseCurrentPopup(); }
  else if(cancel) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void App::runAutotile() {
  Document& doc = *_doc;
  const Layer& layer = doc.active();
  IRect src = doc.hasSelection() ? doc.selection->bounds() : IRect{ 0, 0, doc.width() - 1, doc.height() - 1 };
  int g = _autotile.grid;
  Image in(g * 2, g);
  for(int y = 0; y < g; y++)
    for(int x = 0; x < g * 2; x++)
      if(layer.contains(src.x0 + x, src.y0 + y)) in.at(x, y) = layer.get(src.x0 + x, src.y0 + y);
  Image sheet = autotile(in, g, _autotile.border, _autotile.corner);
  // The canvas grows first when the sheet doesn't fit
  if(sheet.w > doc.width() || sheet.h > doc.height())
    resizeCanvas(doc, std::max(sheet.w, doc.width()), std::max(sheet.h, doc.height()), 0, 0, CLEAR, ICON_FA_EXPAND);
  auto out = std::make_unique<Layer>(doc.width(), doc.height(), tr("Autotile"));
  for(int y = 0; y < sheet.h; y++)
    for(int x = 0; x < sheet.w; x++)
      if(uint32_t c = sheet.at(x, y)) out->set(x, y, c);
  addLayer(doc, std::move(out), doc.activeLayer + 1, N_("Autotile"), ICON_FA_PUZZLE_PIECE);
}
#pragma endregion

}
