#include "ui/Theme.h"
#include "core/io/Png.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

namespace david {

#pragma region [ Hex ]
ImVec4 hexToColor(const std::string& hex) {
  unsigned r = 0, g = 0, b = 0, a = 255;
  const char* s = hex.c_str() + (hex.starts_with('#') ? 1 : 0);
  size_t n = std::strlen(s);
  if(n == 6) std::sscanf(s, "%02x%02x%02x", &r, &g, &b);
  else if(n == 8) std::sscanf(s, "%02x%02x%02x%02x", &r, &g, &b, &a);
  return { r / 255.f, g / 255.f, b / 255.f, a / 255.f };
}

std::string colorToHex(const ImVec4& c) {
  auto b = [](float v) { return unsigned(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
  char buf[16];
  if(b(c.w) == 255) std::snprintf(buf, sizeof buf, "#%02X%02X%02X", b(c.x), b(c.y), b(c.z));
  else std::snprintf(buf, sizeof buf, "#%02X%02X%02X%02X", b(c.x), b(c.y), b(c.z), b(c.w));
  return buf;
}
static float luminance(const ImVec4& c) {
  auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
  return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
}

float contrastRatio(const ImVec4& fg, const ImVec4& bg) {
  ImVec4 f = { fg.x * fg.w + bg.x * (1 - fg.w), fg.y * fg.w + bg.y * (1 - fg.w), fg.z * fg.w + bg.z * (1 - fg.w), 1 };
  float a = luminance(f), b = luminance(bg);
  return (std::max(a, b) + 0.05f) / (std::min(a, b) + 0.05f);
}
#pragma endregion

#pragma region [ Tokens ]
const std::vector<Theme::ColorToken>& Theme::colorTokens() {
  static const std::vector<ColorToken> list = {
    { "bg", &Theme::bg }, { "surface", &Theme::surface }, { "raised", &Theme::raised }, { "line", &Theme::line },
    { "hover", &Theme::hover }, { "pressed", &Theme::pressed }, { "text", &Theme::text }, { "textMuted", &Theme::textMuted },
    { "accent", &Theme::accent }, { "accentHover", &Theme::accentHover }, { "onAccent", &Theme::onAccent },
    { "good", &Theme::good }, { "warn", &Theme::warn }, { "bad", &Theme::bad },
    { "canvasBg", &Theme::canvasBg }, { "checkerA", &Theme::checkerA }, { "checkerB", &Theme::checkerB },
    { "selectionAnts", &Theme::selectionAnts }, { "nub", &Theme::nub },
  };
  return list;
}

const std::vector<Theme::ShapeToken>& Theme::shapeTokens() {
  static const std::vector<ShapeToken> list = {
    { "radius", &Theme::radius, 0, 12 }, { "borderWidth", &Theme::borderWidth, 0, 2 },
    { "spacing", &Theme::spacing, 2, 16 }, { "fontSize", &Theme::fontSize, 10, 24 },
  };
  return list;
}

Theme Theme::dev() {
  Theme t;
  t.name = "Dev";
  t.bg = hexToColor("#1A1A1A"); t.surface = hexToColor("#212121"); t.raised = hexToColor("#2A2A2A");
  t.line = hexToColor("#44475A"); t.hover = hexToColor("#525568"); t.pressed = hexToColor("#5E6178");
  t.text = hexToColor("#FFFFFF"); t.textMuted = hexToColor("#A3A3A3");
  t.accent = hexToColor("#8BE9FD"); t.accentHover = hexToColor("#A6EEFD"); t.onAccent = hexToColor("#1A1A1A");
  t.good = hexToColor("#50FA7B"); t.warn = hexToColor("#FFB86C"); t.bad = hexToColor("#FF5555");
  t.canvasBg = hexToColor("#141414"); t.checkerA = hexToColor("#3C3C3C"); t.checkerB = hexToColor("#2E2E2E");
  t.selectionAnts = hexToColor("#FFFFFF"); t.nub = hexToColor("#8BE9FD");
  t.radius = 4; t.borderWidth = 0; t.padding = 6; t.spacing = 6; t.fontSize = 15;
  return t;
}

Theme Theme::classic() {
  Theme t;
  t.name = "Classic";
  t.bg = hexToColor("#F6F6F6"); t.surface = hexToColor("#EDEDED"); t.raised = hexToColor("#D8D7D7");
  t.line = hexToColor("#D1D1D1"); t.hover = hexToColor("#CBDEED"); t.pressed = hexToColor("#CCE4F7");
  t.text = hexToColor("#1B1B1B"); t.textMuted = hexToColor("#5D5D5D");
  t.accent = hexToColor("#0078D4"); t.accentHover = hexToColor("#1A86D9"); t.onAccent = hexToColor("#FFFFFF");
  t.good = hexToColor("#107C10"); t.warn = hexToColor("#C27C0E"); t.bad = hexToColor("#C42B1C");
  t.canvasBg = hexToColor("#E6E6E6"); t.checkerA = hexToColor("#FFFFFF"); t.checkerB = hexToColor("#CCCCCC");
  t.selectionAnts = hexToColor("#000000"); t.nub = hexToColor("#0078D4");
  t.radius = 4; t.borderWidth = 1; t.padding = 6; t.spacing = 6; t.fontSize = 15;
  return t;
}
#pragma endregion

#pragma region [ File ]
bool Theme::load(const std::string& path, std::string& error) {
  std::ifstream in(pathFromUtf8(path));
  if(!in) { error = "can't open " + path; return false; }
  json j = json::parse(in, nullptr, false);
  if(j.is_discarded() || !j.is_object()) { error = "bad JSON in " + path; return false; }

  // Missing tokens keep the Dev values, so a theme file only needs the tokens it changes
  *this = dev();
  name = j.value("name", name);
  if(auto c = j.find("colors"); c != j.end() && c->is_object())
    for(auto& t : colorTokens())
      if(auto v = c->find(t.key); v != c->end() && v->is_string()) this->*t.member = hexToColor(v->get<std::string>());
  if(auto s = j.find("shape"); s != j.end() && s->is_object()) {
    for(auto& t : shapeTokens())
      if(auto v = s->find(t.key); v != s->end() && v->is_number()) this->*t.member = std::clamp(v->get<float>(), t.min, t.max);
    font = s->value("font", font);
  }
  return true;
}

bool Theme::save(const std::string& path, std::string& error) const {
  json j;
  j["name"] = name;
  for(auto& t : colorTokens()) j["colors"][t.key] = colorToHex(this->*t.member);
  for(auto& t : shapeTokens()) j["shape"][t.key] = this->*t.member;
  j["shape"]["font"] = font;
  std::string text = j.dump(2) + '\n';
  return writeFileAtomic(path, { text.begin(), text.end() }, error);
}
#pragma endregion

#pragma region [ ImGui ]
static ImVec4 alpha(ImVec4 c, float a) { return { c.x, c.y, c.z, a }; }

void Theme::apply(ImGuiStyle& style, float dpiScale) const {
  style = ImGuiStyle();
  style.WindowRounding = style.ChildRounding = style.FrameRounding = style.PopupRounding = radius;
  style.ScrollbarRounding = style.GrabRounding = style.TabRounding = radius;
  style.WindowBorderSize = style.ChildBorderSize = borderWidth;
  // Dialogs and menus always get at least a 1 px line border: without the dim behind effect dialogs they need
  // it to stand apart from the canvas. Floating windows like Settings push the same (dialogBorder).
  style.PopupBorderSize = std::max(borderWidth, 1.f);
  // Buttons are flat (no fill, no border until hovered); structure comes from fills
  style.FrameBorderSize = 0;
  style.TabBorderSize = 0;
  style.TabBarBorderSize = 0;
  style.TabBarOverlineSize = 0;
  style.WindowPadding = { padding + 2, padding + 2 };
  style.FramePadding = { padding, padding * 0.6f };
  style.ItemSpacing = { spacing, spacing };
  style.ItemInnerSpacing = { spacing * 0.75f, spacing * 0.75f };
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.DockingSeparatorSize = 2;
  style.ScrollbarSize = 12;
  style.GrabMinSize = 10;
  style.SeparatorTextBorderSize = 1;
  style.FontSizeBase = fontSize;
  // ImGui resyncs FontSizeBase inside a frame; this carries a change made mid-frame (Settings) to the next one
  style._NextFrameFontSizeBase = fontSize;
  style.ScaleAllSizes(dpiScale);
  style.FontScaleDpi = dpiScale;

  ImVec4* c = style.Colors;
  c[ImGuiCol_Text] = text;
  c[ImGuiCol_TextDisabled] = textMuted;
  c[ImGuiCol_WindowBg] = surface;
  c[ImGuiCol_ChildBg] = surface;
  c[ImGuiCol_PopupBg] = raised;
  c[ImGuiCol_Border] = line;
  c[ImGuiCol_BorderShadow] = { 0, 0, 0, 0 };
  c[ImGuiCol_FrameBg] = raised;
  c[ImGuiCol_FrameBgHovered] = hover;
  c[ImGuiCol_FrameBgActive] = pressed;
  c[ImGuiCol_TitleBg] = bg;
  c[ImGuiCol_TitleBgActive] = bg;
  c[ImGuiCol_TitleBgCollapsed] = bg;
  c[ImGuiCol_MenuBarBg] = bg;
  c[ImGuiCol_ScrollbarBg] = surface;
  c[ImGuiCol_ScrollbarGrab] = line;
  c[ImGuiCol_ScrollbarGrabHovered] = hover;
  c[ImGuiCol_ScrollbarGrabActive] = accent;
  c[ImGuiCol_CheckMark] = onAccent;
  c[ImGuiCol_CheckboxSelectedBg] = accent;
  c[ImGuiCol_SliderGrab] = accent;
  c[ImGuiCol_SliderGrabActive] = accentHover;
  c[ImGuiCol_Button] = { 0, 0, 0, 0 };
  c[ImGuiCol_ButtonHovered] = hover;
  c[ImGuiCol_ButtonActive] = pressed;
  c[ImGuiCol_Header] = line;
  c[ImGuiCol_HeaderHovered] = hover;
  c[ImGuiCol_HeaderActive] = pressed;
  c[ImGuiCol_Separator] = line;
  c[ImGuiCol_SeparatorHovered] = hover;
  c[ImGuiCol_SeparatorActive] = accent;
  c[ImGuiCol_ResizeGrip] = { 0, 0, 0, 0 };
  c[ImGuiCol_ResizeGripHovered] = hover;
  c[ImGuiCol_ResizeGripActive] = accent;
  c[ImGuiCol_InputTextCursor] = text;
  c[ImGuiCol_Tab] = bg;
  c[ImGuiCol_TabHovered] = hover;
  c[ImGuiCol_TabSelected] = surface;
  c[ImGuiCol_TabSelectedOverline] = accent;
  c[ImGuiCol_TabDimmed] = bg;
  c[ImGuiCol_TabDimmedSelected] = surface;
  c[ImGuiCol_TabDimmedSelectedOverline] = line;
  c[ImGuiCol_DockingPreview] = alpha(accent, 0.4f);
  c[ImGuiCol_DockingEmptyBg] = canvasBg;
  c[ImGuiCol_PlotLines] = accent;
  c[ImGuiCol_PlotLinesHovered] = accentHover;
  c[ImGuiCol_PlotHistogram] = accent;
  c[ImGuiCol_PlotHistogramHovered] = accentHover;
  c[ImGuiCol_TableHeaderBg] = raised;
  c[ImGuiCol_TableBorderStrong] = line;
  c[ImGuiCol_TableBorderLight] = line;
  c[ImGuiCol_TableRowBg] = { 0, 0, 0, 0 };
  c[ImGuiCol_TableRowBgAlt] = alpha(raised, 0.5f);
  c[ImGuiCol_TextLink] = accent;
  c[ImGuiCol_TextSelectedBg] = alpha(accent, 0.35f);
  c[ImGuiCol_TreeLines] = line;
  c[ImGuiCol_DragDropTarget] = accent;
  c[ImGuiCol_DragDropTargetBg] = alpha(accent, 0.15f);
  c[ImGuiCol_UnsavedMarker] = warn;
  c[ImGuiCol_NavCursor] = accent;
  c[ImGuiCol_NavWindowingHighlight] = accent;
  c[ImGuiCol_NavWindowingDimBg] = { 0, 0, 0, 0.5f };
  c[ImGuiCol_ModalWindowDimBg] = { 0, 0, 0, 0.5f };
}
#pragma endregion

}
