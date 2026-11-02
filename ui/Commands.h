#pragma once
#include <imgui.h>
#include <functional>
#include <string>
#include <vector>

namespace david {

// A named action with an icon and Paint.NET default shortcuts. Menus, shortcuts and (later) the
// shortcut remapping all read from this one list.
struct Command {
  std::string id;
  std::string label;
  const char* icon = "";
  std::vector<ImGuiKeyChord> chords;
  std::function<void()> run;
  std::function<bool()> enabled;
  bool repeat = false;
  bool view = false;   // view-only commands don't finish a pending tool edit first
  std::vector<ImGuiKeyChord> defaults;  // the Paint.NET chords, kept for "reset"
};

class Commands {
public:
  // Runs before any non-view command, so a pending edit (Move, polygon lasso) is committed first
  std::function<void()> beforeEdit;

  // While true (shortcut capture in Settings), no shortcut fires
  bool suspended = false;

  void add(Command c) { c.defaults = c.chords; _list.push_back(std::move(c)); }
  Command* find(const std::string& id);
  std::vector<Command>& list() { return _list; }

  void run(const std::string& id);
  // plainKeys = false skips chords without Ctrl or Alt (while a tool takes typing)
  void handleShortcuts(bool plainKeys = true);
  bool menuItem(const std::string& id, bool selected = false);
  static std::string chordName(ImGuiKeyChord chord);
  // Storage form, e.g. "Ctrl+Shift+Equal" (ImGui key names), and back; 0 when it can't be parsed
  static std::string chordToString(ImGuiKeyChord chord);
  static ImGuiKeyChord parseChord(const std::string& s);
  // Removes the chord from every command but `keep`; returns the id it was taken from, if any
  std::string releaseChord(ImGuiKeyChord chord, const std::string& keep);

private:
  std::vector<Command> _list;
};

}
