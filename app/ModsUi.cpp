#include "app/App.h"
#include "core/io/Png.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <filesystem>

namespace david {

void App::showError(const std::string& message) {
  _error = message;
  _popup = "error";
}

void App::openGuide() {
  // The guide in the UI language when it has a translation
  std::string dir = paths::exeDir() + "docs/modding/";
  std::error_code ec;
  if(_cfg.language != "en" && std::filesystem::exists(pathFromUtf8(dir + _cfg.language + "/index.html"), ec)) dir += _cfg.language + "/";
  std::string url = "file:///" + dir + "index.html";
  std::replace(url.begin(), url.end(), '\\', '/');
  SDL_OpenURL(url.c_str());
}

#pragma region [ Menu ]
// Commands by their menu path ("Mods/Color" = a Color submenu), then the guide and the mods folder
void App::drawModsMenu() {
  if(!ImGui::BeginMenu(tr("Mods"))) return;
  std::vector<std::string> subs;
  for(auto& c : _mods.commands()) {
    std::string sub = c.menu.size() > 5 && c.menu.rfind("Mods/", 0) == 0 ? c.menu.substr(5) : "";
    if(std::find(subs.begin(), subs.end(), sub) == subs.end()) subs.push_back(sub);
  }
  std::sort(subs.begin(), subs.end());
  for(auto& sub : subs) {
    bool open = sub.empty() || ImGui::BeginMenu(tr(sub.c_str()));
    if(!open) continue;
    for(auto& c : _mods.commands()) {
      std::string csub = c.menu.size() > 5 && c.menu.rfind("Mods/", 0) == 0 ? c.menu.substr(5) : "";
      if(csub != sub) continue;
      std::string label = std::string(ICON_FA_PUZZLE_PIECE "  ") + tr(c.name.c_str()) + (c.params.empty() ? "" : "...") + "##" + c.id;
      if(ImGui::MenuItem(label.c_str())) {
        commitTool();
        if(c.params.empty()) runModCommand(c, {});
        else {
          Effect shape;
          shape.params = c.params;
          _cmdValues = defaultValues(shape);
          for(auto& p : c.params) if(p.type == EParam::Color) _cmdValues.num[p.id] = double(_primary);
          _cmdId = c.id;
          _popup = "###modcmd";
        }
      }
    }
    if(!sub.empty()) ImGui::EndMenu();
  }
  if(_mods.commands().empty() && _mods.windows().empty()) {
    ImGui::BeginDisabled();
    ImGui::MenuItem((std::string(ICON_FA_PUZZLE_PIECE "  ") + tr("No mod commands")).c_str());
    ImGui::EndDisabled();
  }
  // Windows, checked while open, under their own menu path
  if(!_mods.windows().empty()) {
    ImGui::Separator();
    for(auto& w : _mods.windows()) {
      std::string sub = w.menu.size() > 5 && w.menu.rfind("Mods/", 0) == 0 ? w.menu.substr(5) : "";
      if(!sub.empty() && !ImGui::BeginMenu((tr(sub.c_str()) + std::string("##w")).c_str())) continue;
      std::string key = w.mod + "/" + w.id;
      std::string label = std::string(w.tool ? ICON_FA_TABLE_COLUMNS "  " : ICON_FA_WINDOW_RESTORE "  ") + tr(w.title.c_str()) + "##win" + key;
      if(ImGui::MenuItem(label.c_str(), nullptr, modWindowOpen(key))) showModWindow(key, !modWindowOpen(key));
      if(!sub.empty()) ImGui::EndMenu();
    }
  }
  ImGui::Separator();
  if(ImGui::MenuItem((std::string(ICON_FA_SLIDERS "  ") + tr("Manage mods")).c_str())) { _showSettings = true; _settingsPage = 5; }
  if(ImGui::MenuItem((std::string(ICON_FA_FOLDER_OPEN "  ") + tr("Open mods folder")).c_str())) {
    std::string url = "file:///" + paths::configDir() + "mods";
    std::error_code ec;
    std::filesystem::create_directories(pathFromUtf8(paths::configDir() + "mods"), ec);
    std::replace(url.begin(), url.end(), '\\', '/');
    SDL_OpenURL(url.c_str());
  }
  if(ImGui::MenuItem((std::string(ICON_FA_BOOK "  ") + tr("Modding guide")).c_str())) openGuide();
  ImGui::EndMenu();
}

#pragma region [ Windows ]
namespace {
// The Lua ui object: labels on the left (like the effect dialogs), widgets after them
class ImGuiModUi : public ModUi {
public:
  ImGuiModUi(float dpi, const Theme& theme, ImFont* bold) : _dpi(dpi), _theme(theme), _bold(bold) {}
  bool closed = false;

  void text(const std::string& s, bool muted) override {
    if(muted) ImGui::PushStyleColor(ImGuiCol_Text, _theme.textMuted);
    ImGui::TextWrapped("%s", s.c_str());
    if(muted) ImGui::PopStyleColor();
  }
  void header(const std::string& s) override {
    // Small bold uppercase muted label, then a 1px line filling the row (Settings' section headers)
    ImGui::Spacing();
    std::string upper = s;
    for(auto& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
    ImGui::PushFont(_bold, _theme.fontSize * 0.82f);
    ImGui::TextColored(_theme.textMuted, "%s", upper.c_str());
    ImGui::PopFont();
    ImVec2 a = ImGui::GetItemRectMax();
    float y = (ImGui::GetItemRectMin().y + a.y) / 2;
    float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    if(right > a.x + 8) ImGui::GetWindowDrawList()->AddLine({ a.x + 8, y }, { right, y }, ImGui::GetColorU32(_theme.line), 1);
  }
  void separator() override { ImGui::Separator(); }
  void sameLine() override { ImGui::SameLine(); }
  void spacing() override { ImGui::Spacing(); }
  float width() override { return ImGui::GetContentRegionAvail().x / _dpi; }
  void close() override { closed = true; }

  bool button(const std::string& label, float width) override {
    ImGui::PushID(_n++);
    float w = width < 0 ? ImGui::GetContentRegionAvail().x : width * _dpi;
    bool r = ui::dialogButton(shown(label).c_str(), false, w);
    ImGui::PopID();
    return r;
  }
  bool checkbox(const std::string& label, bool& v) override {
    ImGui::PushID(_n++);
    bool r = ImGui::Checkbox(shown(label).c_str(), &v);
    ImGui::PopID();
    return r;
  }
  bool slider(const std::string& label, double& v, double min, double max, double step) override {
    labelled(label);
    float f = float(v);
    bool r = ui::band("##v", f, float(min), float(max), float(step), _dpi, {}, ImGui::GetContentRegionAvail().x);
    if(r) v = f;
    ImGui::PopID();
    return r;
  }
  bool choice(const std::string& label, int& index, const std::vector<std::string>& options) override {
    labelled(label);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool r = false;
    if(ImGui::BeginCombo("##v", index < int(options.size()) ? options[index].c_str() : "")) {
      for(int i = 0; i < int(options.size()); i++)
        if(ImGui::Selectable(options[i].c_str(), i == index)) { index = i; r = true; }
      ImGui::EndCombo();
    }
    ImGui::PopID();
    return r;
  }
  bool color(const std::string& label, uint32_t& c) override {
    labelled(label);
    float f[4] = { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f };
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool r = ImGui::ColorEdit4("##v", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf);
    if(r) {
      c = 0;
      for(int k = 0; k < 4; k++) c |= uint32_t(std::clamp(f[k], 0.f, 1.f) * 255 + 0.5f) << (k * 8);
    }
    ImGui::PopID();
    return r;
  }
  bool input(const std::string& label, std::string& s) override {
    labelled(label);
    char buf[1024];
    std::snprintf(buf, sizeof buf, "%s", s.c_str());
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool r = ImGui::InputText("##v", buf, sizeof buf);
    if(r) s = buf;
    ImGui::PopID();
    return r;
  }

private:
  float _dpi;
  const Theme& _theme;
  ImFont* _bold;
  int _n = 0;  // widgets get their place as id, so two with the same label don't clash

  static std::string shown(const std::string& label) { return label.substr(0, label.find("##")); }
  // The label column, a third of the row (at most 130 pt); leaves an id pushed for the widget
  void labelled(const std::string& label) {
    ImGui::PushID(_n++);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(shown(label).c_str());
    ImGui::SameLine(std::min(130 * _dpi, ImGui::GetContentRegionAvail().x * 0.35f + ImGui::GetCursorPosX()));
  }
};
}

bool App::modWindowOpen(const std::string& key) const {
  return std::find(_cfg.modWindows.begin(), _cfg.modWindows.end(), key) != _cfg.modWindows.end();
}

void App::showModWindow(const std::string& key, bool open) {
  auto& list = _cfg.modWindows;
  auto it = std::find(list.begin(), list.end(), key);
  if(open && it == list.end()) list.push_back(key);
  else if(!open && it != list.end()) list.erase(it);
}

// Tool windows dock like Layers (first as a tab next to it); dialogs float like Settings
void App::drawModWindows() {
  // A copy: a draw may close or open windows, and a reload replaces the list
  std::vector<ModWindow> windows = _mods.windows();
  for(auto& w : windows) {
    std::string key = w.mod + "/" + w.id;
    if(!modWindowOpen(key)) continue;
    ImVec2 size = { (w.width > 0 ? w.width : w.tool ? 260 : 420) * _dpi, (w.height > 0 ? w.height : w.tool ? 300 : 320) * _dpi };
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGuiWindowClass wc;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse;
    if(w.tool && !_cfg.floatingWindows) {
      wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton | ImGuiDockNodeFlags_NoWindowMenuButton;
      if(_nodeLayers) ImGui::SetNextWindowDockID(_nodeLayers, ImGuiCond_FirstUseEver);
    } else {
      wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoDocking;
      flags |= ImGuiWindowFlags_NoDocking;
      ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
    }
    ImGui::SetNextWindowClass(&wc);
    std::string title = std::string(ICON_FA_PUZZLE_PIECE "  ") + tr(w.title.c_str()) + "###mod-" + key;
    bool open = true;
    // Floating ones (dialogs, tool windows while windows float) get the dialogs' border; docked ones none
    bool floating = flags & ImGuiWindowFlags_NoDocking;
    if(floating) ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, _theme.dialogBorder());
    bool visible = ImGui::Begin(title.c_str(), &open, flags);
    if(floating) ImGui::PopStyleVar();
    if(visible) {
      if(!w.tool && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsAnyItemActive()) open = false;
      auto err = _modWindowError.find(key);
      if(err != _modWindowError.end()) {
        ImGui::PushStyleColor(ImGuiCol_Text, _theme.bad);
        ImGui::TextWrapped("%s", err->second.c_str());
        ImGui::PopStyleColor();
        ImGui::TextDisabled("%s", tr("Fix the mod and save it: it reloads."));
      } else {
        ImGuiModUi ui(_dpi, _theme, _boldFont);
        std::string error;
        if(!w.draw(ui, *_doc, effectEnv(), error)) {
          _modWindowError[key] = error;
          if(ModInfo* m = _mods.find(w.mod)) m->log.push_back(w.title + ": " + error);
        }
        if(ui.closed) open = false;
      }
    }
    ImGui::End();
    if(!open) showModWindow(key, false);
  }
}
#pragma endregion

void App::runModCommand(const ModCommand& c, const ParamValues& v) {
  std::string err;
  if(!c.run(*_doc, v, effectEnv(), err)) showError(c.name + ": " + err);
}

void App::drawCommandDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  const ModCommand* cmd = nullptr;
  for(auto& c : _mods.commands()) if(c.id == _cmdId) cmd = &c;
  std::string title = std::string(ICON_FA_PUZZLE_PIECE "  ") + (cmd ? tr(cmd->name.c_str()) : "") + "###modcmd";
  if(!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    if(!_cmdId.empty() && _popup != "###modcmd" && !ImGui::IsPopupOpen("###modcmd", ImGuiPopupFlags_AnyPopupId)) _cmdId.clear();
    return;
  }
  if(!cmd) { _cmdId.clear(); ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
  for(auto& p : cmd->params) if(p.shown(_cmdValues)) paramWidget(p, _cmdValues, _dpi, [](const std::string&) {});
  ImGui::Spacing();
  float bw = 100 * _dpi;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x));
  bool ok = ui::dialogButton(tr("OK"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  ImGui::SameLine();
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape);
  if(ok || cancel) {
    ImGui::CloseCurrentPopup();
    ModCommand copy = *cmd;
    ParamValues v = _cmdValues;
    _cmdId.clear();
    if(ok) runModCommand(copy, v);
  }
  ImGui::EndPopup();
}
#pragma endregion

#pragma region [ Settings > Mods ]
void App::settingsMods() {
  ImGui::PushStyleColor(ImGuiCol_Text, _theme.textMuted);
  ImGui::TextWrapped("%s", tr("Mods live in the app's mods folder and in your config folder's mods folder. Edits to shader and Lua files reload by themselves."));
  ImGui::PopStyleColor();
  ImGui::Spacing();
  float btn = ImGui::GetFrameHeight();
  if(ui::iconButton("##rescan", ICON_FA_ROTATE_RIGHT, { btn, btn }, false, tr("Look for new mods"))) {
    _mods.scan({ paths::exeDir() + "mods", paths::configDir() + "mods" });
    _mods.apply(_cfg.mods);
  }
  ImGui::SameLine();
  if(ui::iconButton("##guide", ICON_FA_BOOK, { btn, btn }, false, tr("Modding guide"))) openGuide();
  ImGui::Separator();
  if(_mods.mods().empty()) ImGui::TextDisabled("%s", tr("No mods found."));
  for(auto& m : _mods.mods()) {
    ImGui::PushID(m.id.c_str());
    bool on = m.enabled;
    if(ImGui::Checkbox("##on", &on)) {
      // Stored only where it differs from the default (examples start off)
      if(on == !m.example) _cfg.mods.erase(m.id);
      else _cfg.mods[m.id] = on;
      _mods.apply(_cfg.mods);
    }
    ImGui::SameLine();
    const char* kindIcon = m.kind == "shader" ? ICON_FA_WAND_MAGIC_SPARKLES : m.kind == "lua" ? ICON_FA_CODE : ICON_FA_MICROCHIP;
    ImGui::TextUnformatted(kindIcon);
    ImGui::SameLine();
    ImGui::PushFont(_boldFont, 0);
    ImGui::TextUnformatted(m.name.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("%s %s%s", m.version.c_str(), m.kind.c_str(), m.example ? "  · example" : "");
    if(!m.description.empty()) ImGui::TextWrapped("%s", m.description.c_str());
    if(!m.error.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, _theme.bad);
      ImGui::TextWrapped("%s %s", ICON_FA_CIRCLE_EXCLAMATION, m.error.c_str());
      ImGui::PopStyleColor();
    } else if(m.enabled && !m.provides.empty()) {
      std::string list;
      for(auto& p : m.provides) list += (list.empty() ? "" : ", ") + p;
      ImGui::TextDisabled("%s", list.c_str());
    }
    bool hasTool = std::any_of(m.provides.begin(), m.provides.end(), [](const std::string& p) { return p.rfind("tool:", 0) == 0; });
    if(hasTool) ImGui::TextDisabled("%s", tr("New or changed tools appear after a restart."));
    if(!m.log.empty() && ImGui::TreeNode("##log", "%s (%d)", tr("Log"), int(m.log.size()))) {
      for(auto& line : m.log) ImGui::TextDisabled("%s", line.c_str());
      ImGui::TreePop();
    }
    ImGui::Separator();
    ImGui::PopID();
  }
}
#pragma endregion

}
