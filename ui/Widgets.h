#pragma once
#include <imgui.h>
#include <functional>

namespace david {

struct Theme;

namespace ui {
  // Flat button: no fill or border until hovered; `on` fills it with the accent. The icon is centered on its
  // drawn glyph box, not its advance, so every icon sits in the middle of the button.
  bool iconButton(const char* id, const char* icon, ImVec2 size, bool on = false, const char* tip = nullptr);
  // Icon plus a label, left aligned, for wide panels
  bool labelButton(const char* id, const char* icon, const char* label, ImVec2 size, bool on = false, const char* tip = nullptr);
  // Draws an icon centered in a rect
  void centeredIcon(ImDrawList* dl, const char* icon, ImVec2 min, ImVec2 max, ImU32 color);
  // Text centered in a rect at a given font size
  void centeredText(ImDrawList* dl, ImFont* font, float size, const char* text, ImVec2 min, ImVec2 max, ImU32 color);
  // Toggle shown as a flat icon button
  bool iconToggle(const char* id, const char* icon, bool& v, const char* tip);

  // A list row: full-width, exact highlight (selected / hovered) with the frame rounding. Returns the click;
  // min/max give the row rect for its content. overlap lets buttons drawn on top of the row take clicks.
  bool listRow(const char* id, bool selected, float height, ImVec2& min, ImVec2& max, bool overlap = false);
  // Icon + label inside a row, with the frame's left padding, vertically centered on one text line
  void rowLabel(const char* icon, const char* label, ImVec2 min, ImVec2 max, ImU32 color);

  // The accent colors used by `on` buttons, set from the theme
  void setAccent(ImVec4 accent, ImVec4 accentHover, ImVec4 onAccent);
  // A dialog's text button with a filled background: accent for the main action, the line color otherwise
  bool dialogButton(const char* label, bool primary, float width);

  // A bar slider with a number field after it (the Colors window's RGB / HSV rows), `width` wide in all (0 = the
  // rest of the row). colorAt(t) paints the bar from min (t = 0) to max (t = 1); without it the bar is a track
  // filled with the accent up to the value. step rounds the value (1 for whole numbers).
  bool band(const char* id, float& value, float min, float max, float step, float dpi,
            const std::function<ImVec4(float)>& colorAt = {}, float width = 0);
}

}
