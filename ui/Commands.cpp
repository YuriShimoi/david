#include "ui/Commands.h"
#include "ui/I18n.h"
#include <algorithm>

namespace david {

Command* Commands::find(const std::string& id) {
  for(auto& c : _list) if(c.id == id) return &c;
  return nullptr;
}

static void invoke(Commands& cmds, Command& c) {
  if(!c.run) return;
  if(!c.view && cmds.beforeEdit) cmds.beforeEdit();
  if(!c.enabled || c.enabled()) c.run();
}

void Commands::run(const std::string& id) {
  if(Command* c = find(id)) invoke(*this, *c);
}

void Commands::handleShortcuts(bool plainKeys) {
  if(suspended || ImGui::GetIO().WantTextInput) return;
  for(auto& c : _list) {
    for(ImGuiKeyChord chord : c.chords) {
      if(!plainKeys && !(chord & (ImGuiMod_Ctrl | ImGuiMod_Alt))) continue;
      if(ImGui::Shortcut(chord, ImGuiInputFlags_RouteGlobal | (c.repeat ? ImGuiInputFlags_Repeat : 0))) {
        invoke(*this, c);
        break;
      }
    }
  }
}

bool Commands::menuItem(const std::string& id, bool selected) {
  Command* c = find(id);
  if(!c) return false;
  std::string label = std::string(c->icon) + (*c->icon ? "  " : "") + tr(c->label.c_str()) + "##" + c->id;
  std::string keys = c->chords.empty() ? "" : chordName(c->chords[0]);
  bool on = !c->enabled || c->enabled();
  if(ImGui::MenuItem(label.c_str(), keys.c_str(), selected, on)) {
    invoke(*this, *c);
    return true;
  }
  return false;
}

std::string Commands::chordName(ImGuiKeyChord chord) {
  std::string s;
  if(chord & ImGuiMod_Ctrl) s += std::string(tr("Ctrl")) + "+";
  if(chord & ImGuiMod_Shift) s += std::string(tr("Shift")) + "+";
  if(chord & ImGuiMod_Alt) s += std::string(tr("Alt")) + "+";
  ImGuiKey key = ImGuiKey(chord & ~ImGuiMod_Mask_);
  switch(key) {
    case ImGuiKey_Equal: s += "+"; break;
    case ImGuiKey_Minus: s += "-"; break;
    case ImGuiKey_LeftBracket: s += "["; break;
    case ImGuiKey_RightBracket: s += "]"; break;
    case ImGuiKey_KeypadAdd: s += "Num +"; break;
    case ImGuiKey_KeypadSubtract: s += "Num -"; break;
    case ImGuiKey_PageUp: s += tr("PgUp"); break;
    case ImGuiKey_PageDown: s += tr("PgDn"); break;
    case ImGuiKey_Delete: s += tr("Del"); break;
    case ImGuiKey_Backspace: s += tr("Backspace"); break;
    default: s += ImGui::GetKeyName(key); break;
  }
  return s;
}

}

namespace david {

std::string Commands::chordToString(ImGuiKeyChord chord) {
  std::string s;
  if(chord & ImGuiMod_Ctrl) s += "Ctrl+";
  if(chord & ImGuiMod_Shift) s += "Shift+";
  if(chord & ImGuiMod_Alt) s += "Alt+";
  return s + ImGui::GetKeyName(ImGuiKey(chord & ~ImGuiMod_Mask_));
}

ImGuiKeyChord Commands::parseChord(const std::string& s) {
  ImGuiKeyChord mods = 0;
  size_t pos = 0;
  for(;;) {
    size_t plus = s.find('+', pos);
    if(plus == std::string::npos || plus == pos) break;
    std::string part = s.substr(pos, plus - pos);
    if(part == "Ctrl") mods |= ImGuiMod_Ctrl;
    else if(part == "Shift") mods |= ImGuiMod_Shift;
    else if(part == "Alt") mods |= ImGuiMod_Alt;
    else break;
    pos = plus + 1;
  }
  std::string key = s.substr(pos);
  for(int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++)
    if(key == ImGui::GetKeyName(ImGuiKey(k))) return mods | k;
  return 0;
}

std::string Commands::releaseChord(ImGuiKeyChord chord, const std::string& keep) {
  std::string taken;
  for(auto& c : _list) {
    if(c.id == keep) continue;
    auto it = std::find(c.chords.begin(), c.chords.end(), chord);
    if(it != c.chords.end()) { c.chords.erase(it); taken = c.id; }
  }
  return taken;
}

}
