#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace david::ui {

static ImVec4 s_accent{ 0.55f, 0.91f, 0.99f, 1 }, s_accentHover{ 0.65f, 0.93f, 0.99f, 1 }, s_onAccent{ 0.1f, 0.1f, 0.1f, 1 };

void setAccent(ImVec4 accent, ImVec4 accentHover, ImVec4 onAccent) {
  s_accent = accent; s_accentHover = accentHover; s_onAccent = onAccent;
}

bool band(const char* id, float& value, float min, float max, float step, float dpi, const std::function<ImVec4(float)>& colorAt, float width) {
  bool changed = false;
  ImGui::PushID(id);
  float numW = 46 * dpi, m = 4 * dpi;  // m keeps the marker clear of the label and the number
  if(width <= 0) width = ImGui::GetContentRegionAvail().x;
  float full = width - numW - ImGui::GetStyle().ItemSpacing.x, h = ImGui::GetFrameHeight();
  auto snap = [&](float v) { return std::clamp(step > 0 ? std::round(v / step) * step : v, min, max); };
  ImVec2 o = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##bar", { full, h });
  float w = full - m * 2;
  ImVec2 a = { o.x + m, o.y }, b = { a.x + w, a.y + h };
  if(ImGui::IsItemActive()) {
    float v = snap(min + std::clamp((ImGui::GetIO().MousePos.x - a.x) / w, 0.f, 1.f) * (max - min));
    if(v != value) { value = v; changed = true; }
  }
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Whole pixels: on fractional edges the checkerboard showed as dashed lines above and below the gradient
  ImVec2 ba = { std::round(a.x), std::round(a.y + h * 0.22f) }, bb = { std::round(b.x), std::round(b.y - h * 0.22f) };
  w = bb.x - ba.x;
  float t = max > min ? std::clamp((value - min) / (max - min), 0.f, 1.f) : 0;
  if(colorAt) {
    // Checkerboard under the bar, only for a band that isn't opaque (alpha), kept a pixel inside the gradient
    if(colorAt(0).w < 1 || colorAt(1).w < 1) {
      ImVec2 ca = { ba.x, ba.y + 1 }, cb = { bb.x, bb.y - 1 };
      float cw = 6 * dpi, cm = std::round((ca.y + cb.y) / 2);
      dl->AddRectFilled(ca, cb, IM_COL32(204, 204, 204, 255));
      for(float x = ca.x; x < cb.x; x += cw)
        for(int r = 0; r < 2; r++)
          if((int((x - ca.x) / cw) + r) % 2) dl->AddRectFilled({ x, r ? cm : ca.y }, { std::min(x + cw, cb.x), r ? cb.y : cm }, IM_COL32(255, 255, 255, 255));
    }
    const int SEG = 24;
    for(int i = 0; i < SEG; i++) {
      float t0 = float(i) / SEG, t1 = float(i + 1) / SEG;
      ImU32 c0 = ImGui::GetColorU32(colorAt(t0)), c1 = ImGui::GetColorU32(colorAt(t1));
      dl->AddRectFilledMultiColor({ ba.x + w * t0, ba.y }, { ba.x + w * t1, bb.y }, c0, c1, c1, c0);
    }
  }
  else {
    // A plain track, filled from the zero point (or the start) to the value
    float z = min < 0 && max > 0 ? -min / (max - min) : 0;
    ImVec4 track = ImGui::GetStyleColorVec4(ImGuiCol_Border);
    track.w = 1;
    dl->AddRectFilled(ba, bb, ImGui::GetColorU32(track));
    float x0 = ba.x + w * std::min(z, t), x1 = ba.x + w * std::max(z, t);
    if(x1 > x0) dl->AddRectFilled({ std::round(x0), ba.y }, { std::round(x1), bb.y }, ImGui::GetColorU32(s_accent));
  }
  // Small pointers above and below the bar
  float mx = ba.x + w * t, k = 3 * dpi;
  ImU32 mc = ImGui::GetColorU32(ImGuiCol_Text);
  dl->AddTriangleFilled({ mx - k, ba.y - k }, { mx + k, ba.y - k }, { mx, ba.y }, mc);
  dl->AddTriangleFilled({ mx - k, bb.y + k }, { mx + k, bb.y + k }, { mx, bb.y }, mc);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(numW);
  if(step >= 1) {
    int iv = int(std::round(value));
    if(ImGui::DragInt("##n", &iv, 0.5f, int(min), int(max))) { value = snap(float(iv)); changed = true; }
  }
  else {
    float fv = value;
    if(ImGui::DragFloat("##n", &fv, step > 0 ? step : 0.01f, min, max, "%.2f")) { value = snap(fv); changed = true; }
  }
  ImGui::PopID();
  return changed;
}

bool dialogButton(const char* label, bool primary, float width) {
  ImVec4 base = primary ? s_accent : ImGui::GetStyleColorVec4(ImGuiCol_Border);
  ImVec4 hover = primary ? s_accentHover : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered);
  base.w = hover.w = 1;
  ImGui::PushStyleColor(ImGuiCol_Button, base);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, hover);
  if(primary) ImGui::PushStyleColor(ImGuiCol_Text, s_onAccent);
  bool r = ImGui::Button(label, { width, 0 });
  ImGui::PopStyleColor(primary ? 4 : 3);
  return r;
}

void centeredIcon(ImDrawList* dl, const char* icon, ImVec2 min, ImVec2 max, ImU32 color) {
  ImFont* font = ImGui::GetFont();
  float size = ImGui::GetFontSize();
  unsigned int c = 0;
  ImTextCharFromUtf8(&c, icon, nullptr);
  ImFontGlyph* g = c ? ImGui::GetFontBaked()->FindGlyph(ImWchar(c)) : nullptr;
  if(!g || icon[ImTextCountUtf8BytesFromChar(icon, nullptr)] != '\0') {
    // Not a single glyph: center the text box instead
    ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, icon);
    dl->AddText(font, size, { ImTrunc((min.x + max.x - ts.x) / 2), ImTrunc((min.y + max.y - ts.y) / 2) }, color, icon);
    return;
  }
  ImVec2 c2 = { (min.x + max.x) / 2 - (g->X0 + g->X1) / 2, (min.y + max.y) / 2 - (g->Y0 + g->Y1) / 2 };
  dl->AddText(font, size, { ImTrunc(c2.x + 0.5f), ImTrunc(c2.y + 0.5f) }, color, icon);
}

void centeredText(ImDrawList* dl, ImFont* font, float size, const char* text, ImVec2 min, ImVec2 max, ImU32 color) {
  if(!font) font = ImGui::GetFont();
  ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0, text);
  dl->AddText(font, size, { ImTrunc((min.x + max.x - ts.x) / 2), ImTrunc((min.y + max.y - ts.y) / 2) }, color, text);
}

static bool flatFrame(const char* id, ImVec2 size, bool on, ImU32& textColor, ImVec2& min, ImVec2& max) {
  bool pressed = ImGui::InvisibleButton(id, size);
  bool hovered = ImGui::IsItemHovered(), held = ImGui::IsItemActive();
  bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
  min = ImGui::GetItemRectMin(); max = ImGui::GetItemRectMax();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float r = ImGui::GetStyle().FrameRounding;
  if(on) dl->AddRectFilled(min, max, ImGui::GetColorU32(hovered ? s_accentHover : s_accent), r);
  else if(held && !disabled) dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_ButtonActive), r);
  else if(hovered && !disabled) dl->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_ButtonHovered), r);
  textColor = on ? ImGui::GetColorU32(s_onAccent) : ImGui::GetColorU32(ImGuiCol_Text);
  return pressed;
}

bool iconButton(const char* id, const char* icon, ImVec2 size, bool on, const char* tip) {
  if(size.x <= 0) size.x = ImGui::GetFrameHeight();
  if(size.y <= 0) size.y = ImGui::GetFrameHeight();
  ImU32 col; ImVec2 a, b;
  bool r = flatFrame(id, size, on, col, a, b);
  centeredIcon(ImGui::GetWindowDrawList(), icon, a, b, col);
  if(tip && *tip) ImGui::SetItemTooltip("%s", tip);
  return r;
}

bool labelButton(const char* id, const char* icon, const char* label, ImVec2 size, bool on, const char* tip) {
  if(size.y <= 0) size.y = ImGui::GetFrameHeight();
  ImU32 col; ImVec2 a, b;
  bool r = flatFrame(id, size, on, col, a, b);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float iconBox = size.y;
  // Icon and label share one text line (the icon font is offset to the text's cap height), so they line up
  float y = ImTrunc((a.y + b.y - ImGui::GetTextLineHeight()) / 2);
  float iw = ImGui::CalcTextSize(icon).x;
  dl->AddText({ ImTrunc(a.x + (iconBox - iw) / 2), y }, col, icon);
  dl->PushClipRect(a, b, true);
  dl->AddText({ a.x + iconBox + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f, y }, col, label);
  dl->PopClipRect();
  if(tip && *tip) ImGui::SetItemTooltip("%s", tip);
  return r;
}

bool listRow(const char* id, bool selected, float height, ImVec2& min, ImVec2& max, bool overlap) {
  if(overlap) ImGui::SetNextItemAllowOverlap();
  bool pressed = ImGui::InvisibleButton(id, { std::max(ImGui::GetContentRegionAvail().x, 1.f), height });
  min = ImGui::GetItemRectMin(); max = ImGui::GetItemRectMax();
  bool hovered = ImGui::IsItemHovered();
  float r = ImGui::GetStyle().FrameRounding;
  if(selected) ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_Header), r);
  else if(hovered) ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_HeaderHovered), r);
  return pressed;
}

void rowLabel(const char* icon, const char* label, ImVec2 min, ImVec2 max, ImU32 color) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  float y = ImTrunc((min.y + max.y - ImGui::GetTextLineHeight()) / 2);
  float x = min.x + ImGui::GetStyle().FramePadding.x;
  float box = ImGui::GetFontSize() * 1.3f;
  if(icon && *icon) {
    dl->AddText({ ImTrunc(x + (box - ImGui::CalcTextSize(icon).x) / 2), y }, color, icon);
    x += box + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
  }
  dl->PushClipRect(min, max, true);
  dl->AddText({ x, y }, color, label);
  dl->PopClipRect();
}

bool iconToggle(const char* id, const char* icon, bool& v, const char* tip) {
  ImU32 col; ImVec2 a, b;
  ImVec2 size = { ImGui::GetFrameHeight() * 1.3f, ImGui::GetFrameHeight() };
  bool r = flatFrame(id, size, false, col, a, b);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  // Selected state: a quiet fill, so toggles read differently from the accent "current tool"
  if(v) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_Header), ImGui::GetStyle().FrameRounding);
  centeredIcon(dl, icon, a, b, v ? ImGui::GetColorU32(s_accent) : col);
  if(r) v = !v;
  if(tip && *tip) ImGui::SetItemTooltip("%s", tip);
  return r;
}

}
