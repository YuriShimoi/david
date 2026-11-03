#pragma once
#include <imgui.h>
#include <string>
#include <vector>

namespace david {

// A flat list of tokens, loaded from themes/<name>.theme.json and mapped onto ImGui's style
struct Theme {
  struct ColorToken { const char* key; ImVec4 Theme::* member; };
  struct ShapeToken { const char* key; float Theme::* member; float min, max; };

  std::string name = "Dev";
  ImVec4 bg, surface, raised, line, hover, pressed, text, textMuted;
  ImVec4 accent, accentHover, onAccent, good, warn, bad;
  ImVec4 canvasBg, checkerA, checkerB, selectionAnts, nub, grid;
  float radius = 0, borderWidth = 0, padding = 6, spacing = 6, fontSize = 15;
  std::string font;

  static const std::vector<ColorToken>& colorTokens();
  static const std::vector<ShapeToken>& shapeTokens();
  static Theme dev();
  static Theme classic();
  // The border width of dialogs and floating windows (Settings): at least 1 px
  float dialogBorder() const { return borderWidth > 1 ? borderWidth : 1.f; }

  bool load(const std::string& path, std::string& error);
  bool save(const std::string& path, std::string& error) const;
  void apply(ImGuiStyle& style, float dpiScale) const;
};

ImVec4 hexToColor(const std::string& hex);
std::string colorToHex(const ImVec4& c);
// WCAG 2 contrast ratio (1-21) of fg over bg; fg's alpha is blended onto bg first
float contrastRatio(const ImVec4& fg, const ImVec4& bg);

}
