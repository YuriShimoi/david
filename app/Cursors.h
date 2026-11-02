#pragma once
#include "ui/tools/Tool.h"
#include <SDL3/SDL_mouse.h>
#include <map>
#include <string>

namespace david {

// The pointer shape: system cursors for the UI and most tools, the tool's own icon for the painting tools
class Cursors {
public:
  ~Cursors();
  // ImGui's wanted cursor over the UI; a hovered clickable item gets the hand
  void ui(ImGuiMouseCursor c, bool clickable);
  void canvas(ECursor c, const Tool& tool, float scale);

private:
  std::map<SDL_SystemCursor, SDL_Cursor*> _system;
  std::map<std::string, SDL_Cursor*> _icons;
  SDL_Cursor* _current = nullptr;

  void set(SDL_Cursor* c);
  SDL_Cursor* system(SDL_SystemCursor id);
  SDL_Cursor* icon(const Tool& tool, float scale);
};

}
