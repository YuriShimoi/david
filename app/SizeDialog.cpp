#include "app/App.h"
#include "core/Resize.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include <algorithm>
#include <cmath>

namespace david {

// Fields sit on the dialog's raised color, so they take the darker background to stay visible
void App::pushFieldColors() {
  ImGui::PushStyleColor(ImGuiCol_FrameBg, _theme.bg);
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, _theme.line);
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, _theme.line);
}

// Resize Image and Canvas Size share Paint.NET's layout: by percentage or by absolute size, pixel size with
// resolution, print size, then resampling (resize) or anchor and fill (canvas)
void App::openSizeDialog(bool canvas) {
  _size = {};
  _size.canvas = canvas;
  _size.keep = !canvas;
  _size.w = _doc->width();
  _size.h = _doc->height();
  _size.dpi = _doc->dpi;
  _popup = "###size";
}

void App::drawSizeDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
  std::string title = std::string(_size.canvas ? tr("Canvas Size") : tr("Resize Image")) + "###size";
  bool open = true;
  if(!ImGui::BeginPopupModal(title.c_str(), &open, flags)) return;
  SizeDialog& s = _size;
  pushFieldColors();
  float dpi = _dpi, labelW = 120 * dpi, fieldW = 110 * dpi, indent = 16 * dpi;
  int ow = _doc->width(), oh = _doc->height();
  double inchesPer = s.printUnit == 1 ? 2.54 : 1;  // print values shown in inches or centimeters
  double resShown = s.resUnit == 1 ? s.dpi / 2.54 : s.dpi;

  // Keeps the other side in proportion to the original image
  auto fromW = [&] { if(s.keep) s.h = std::max(1, int(std::lround(double(s.w) * oh / ow))); };
  auto fromH = [&] { if(s.keep) s.w = std::max(1, int(std::lround(double(s.h) * ow / oh))); };
  auto row = [&](const char* label) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(labelW + indent);
    ImGui::SetNextItemWidth(fieldW);
  };

  double mb = double(s.w) * s.h * 4 * _doc->layers.size() / (1024.0 * 1024.0);
  ImGui::TextDisabled("%s %.1f MB", tr("New size:"), mb);
  ImGui::Separator();

  // The radio dot in the accent color (checkboxes keep their own look)
  ImGui::PushStyleColor(ImGuiCol_CheckMark, _theme.accent);
  if(ImGui::RadioButton(tr("By percentage:"), s.percent)) s.percent = true;
  ImGui::PopStyleColor();
  ImGui::SameLine(labelW + indent);
  ImGui::BeginDisabled(!s.percent);
  ImGui::SetNextItemWidth(fieldW);
  if(ImGui::InputFloat("%##pct", &s.pct, 1, 10, "%.2f")) {
    s.pct = std::clamp(s.pct, 0.1f, 10000.f);
    s.w = std::max(1, int(std::lround(ow * s.pct / 100)));
    s.h = std::max(1, int(std::lround(oh * s.pct / 100)));
  }
  ImGui::EndDisabled();
  ImGui::PushStyleColor(ImGuiCol_CheckMark, _theme.accent);
  if(ImGui::RadioButton(tr("By absolute size:"), !s.percent)) s.percent = false;
  ImGui::PopStyleColor();

  ImGui::BeginDisabled(s.percent);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
  if(ImGui::Checkbox(tr("Maintain aspect ratio"), &s.keep) && s.keep) fromW();

  ImGui::SeparatorText(tr("Pixel size"));
  row(tr("Width:"));
  if(ImGui::InputInt("##w", &s.w, 0)) { s.w = std::clamp(s.w, 1, 65535); fromW(); }
  ImGui::SameLine(); ImGui::TextUnformatted(tr("pixels"));
  row(tr("Height:"));
  if(ImGui::InputInt("##h", &s.h, 0)) { s.h = std::clamp(s.h, 1, 65535); fromH(); }
  ImGui::SameLine(); ImGui::TextUnformatted(tr("pixels"));
  row(tr("Resolution:"));
  // A new resolution keeps the pixels and changes the print size
  if(ImGui::InputDouble("##res", &resShown, 0, 0, "%.2f")) s.dpi = std::clamp(s.resUnit == 1 ? resShown * 2.54 : resShown, 1.0, 100000.0);
  ImGui::SameLine();
  const char* resUnits[] = { tr("pixels/inch"), tr("pixels/cm") };
  ImGui::SetNextItemWidth(130 * dpi);
  ImGui::Combo("##resUnit", &s.resUnit, resUnits, 2);

  ImGui::SeparatorText(tr("Print size"));
  double pw = s.w / s.dpi * inchesPer, ph = s.h / s.dpi * inchesPer;
  row(tr("Width:"));
  if(ImGui::InputDouble("##pw", &pw, 0, 0, "%.2f")) { s.w = std::clamp(int(std::lround(pw / inchesPer * s.dpi)), 1, 65535); fromW(); }
  ImGui::SameLine();
  const char* printUnits[] = { tr("inches"), tr("centimeters") };
  ImGui::SetNextItemWidth(130 * dpi);
  ImGui::Combo("##printUnit", &s.printUnit, printUnits, 2);
  row(tr("Height:"));
  if(ImGui::InputDouble("##ph", &ph, 0, 0, "%.2f")) { s.h = std::clamp(int(std::lround(ph / inchesPer * s.dpi)), 1, 65535); fromH(); }
  ImGui::SameLine(); ImGui::TextUnformatted(printUnits[s.printUnit]);
  ImGui::EndDisabled();

  ImGui::SeparatorText(tr("Options"));
  if(!s.canvas) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Resampling:"));
    ImGui::SameLine(labelW + indent);
    const char* modes[] = { tr("Best quality"), tr("Bicubic"), tr("Bilinear"), tr("Nearest neighbor"), tr("Supersampling") };
    ImGui::SetNextItemWidth(fieldW + 130 * dpi + ImGui::GetStyle().ItemSpacing.x);
    ImGui::Combo("##resample", &s.resample, modes, int(EResample::COUNT));
    ImGui::BeginDisabled(s.resample == int(EResample::Nearest));
    ImGui::Checkbox(tr("Use gamma correction"), &s.gamma);
    ImGui::EndDisabled();
  } else {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Anchor:"));
    // 3×3 grid: the image at the anchor, arrows pointing where the canvas grows
    float cell = ImGui::GetFrameHeight() * 1.3f;
    ImVec2 origin = { ImGui::GetCursorScreenPos().x + labelW + indent, ImGui::GetCursorScreenPos().y };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int ax = s.anchor % 3, ay = s.anchor / 3;
    for(int i = 0; i < 9; i++) {
      int cx = i % 3, cy = i / 3;
      ImGui::SetCursorScreenPos({ origin.x + cx * (cell + 2), origin.y + cy * (cell + 2) });
      ImGui::PushID(i);
      bool here = i == s.anchor;
      // Every spot gets a fill, so the grid reads as nine cells (flat buttons alone are invisible until hovered)
      if(!here) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec4 fill = ImGui::GetStyleColorVec4(ImGuiCol_Border);
        fill.w = 1;
        dl->AddRectFilled(p, { p.x + cell, p.y + cell }, ImGui::GetColorU32(fill), ImGui::GetStyle().FrameRounding);
      }
      if(ui::iconButton("##a", here ? ICON_FA_IMAGE : "", { cell, cell }, here)) s.anchor = i;
      ImGui::PopID();
      int dx = cx - ax, dy = cy - ay;
      if(here || std::abs(dx) > 1 || std::abs(dy) > 1) continue;
      ImVec2 c = { origin.x + cx * (cell + 2) + cell / 2, origin.y + cy * (cell + 2) + cell / 2 };
      float len = cell * 0.28f, nx = dx / std::hypot(float(dx), float(dy)), ny = dy / std::hypot(float(dx), float(dy));
      ImVec2 a = { c.x - nx * len, c.y - ny * len }, b = { c.x + nx * len, c.y + ny * len };
      ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
      dl->AddLine(a, b, col, 1.5f * dpi);
      float hl = len * 0.7f;
      dl->AddLine(b, { b.x - (nx * 0.7f - ny * 0.7f) * hl, b.y - (ny * 0.7f + nx * 0.7f) * hl }, col, 1.5f * dpi);
      dl->AddLine(b, { b.x - (nx * 0.7f + ny * 0.7f) * hl, b.y - (ny * 0.7f - nx * 0.7f) * hl }, col, 1.5f * dpi);
    }
    ImGui::SetCursorScreenPos({ ImGui::GetCursorScreenPos().x, origin.y + 3 * (cell + 2) });
    ImGui::Dummy({ 0, 0 });
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Fill:"));
    ImGui::SameLine(labelW + indent);
    const char* fills[] = { tr("Transparent"), tr("Primary color"), tr("Secondary color") };
    ImGui::SetNextItemWidth(fieldW + 130 * dpi + ImGui::GetStyle().ItemSpacing.x);
    ImGui::Combo("##fill", &s.fill, fills, 3);
  }

  ImGui::Spacing();
  float bw = 90 * dpi;
  ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x);
  bool ok = ui::dialogButton(tr("OK"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  ImGui::SameLine();
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !open;
  if(ok) {
    commitTool();
    if(s.canvas) {
      uint32_t fill = s.fill == 1 ? _primary : s.fill == 2 ? _secondary : 0;
      if(s.w != ow || s.h != oh) resizeCanvas(*_doc, s.w, s.h, s.anchor % 3, s.anchor / 3, fill, ICON_FA_EXPAND);
    } else if(s.w != ow || s.h != oh || s.dpi != _doc->dpi) {
      resizeImage(*_doc, s.w, s.h, s.dpi, EResample(s.resample), s.gamma, ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER);
    }
    _nav.setImage(_doc->width(), _doc->height());
    _nav.fit();
    ImGui::CloseCurrentPopup();
  } else if(cancel) {
    ImGui::CloseCurrentPopup();
  }
  ImGui::PopStyleColor(3);
  ImGui::EndPopup();
}

}
