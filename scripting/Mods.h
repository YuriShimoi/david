#pragma once
#include "core/Effect.h"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace david {

class Document;

// A mod found on disk: mods/<name>/mod.json (or mods/examples/<name>/mod.json)
struct ModInfo {
  std::string id;            // folder name, unique
  std::string name, version, kind, entry, description, dir;
  int api = 1;
  std::vector<std::string> permissions;
  bool example = false;      // under mods/examples/: off until turned on
  bool enabled = false, loaded = false;
  std::string error;
  std::vector<std::string> log;
  std::vector<std::string> provides;  // "effect:scanlines", "command:palette-swap", "tool:checker", "format:qlike"
};

// A Lua command: edits the document inside one undo step
struct ModCommand {
  std::string id, name, menu, mod;
  std::vector<EffectParam> params;  // asked in a dialog first when not empty
  std::function<bool(Document& doc, const ParamValues& values, const EffectEnv& env, std::string& error)> run;
};

// What a mod window draws with: one call per widget, laid out top to bottom (labels on the left). The app
// implements it with ImGui; widgets return true when the user changed the value or pressed the button.
struct ModUi {
  virtual ~ModUi() = default;
  virtual void text(const std::string& s, bool muted) = 0;
  virtual void header(const std::string& s) = 0;
  virtual void separator() = 0;
  virtual void sameLine() = 0;
  virtual void spacing() = 0;
  virtual bool button(const std::string& label, float width) = 0;
  virtual bool checkbox(const std::string& label, bool& v) = 0;
  virtual bool slider(const std::string& label, double& v, double min, double max, double step) = 0;
  virtual bool choice(const std::string& label, int& index, const std::vector<std::string>& options) = 0;
  virtual bool color(const std::string& label, uint32_t& c) = 0;
  virtual bool input(const std::string& label, std::string& s) = 0;
  virtual float width() = 0;
  virtual void close() = 0;
};

// A Lua window: a tool window that docks like Layers, or a dialog that floats like Settings
struct ModWindow {
  std::string id, title, menu, mod;
  bool tool = true;
  float width = 0, height = 0;  // first-use size in points (0 = David picks)
  // Draws the contents; document edits made in one call are one undo step. false with an error stops the window.
  std::function<bool(ModUi& ui, Document& doc, const EffectEnv& env, std::string& error)> draw;
};

// A Lua tool: the freehand controller and round point shape with a Lua ink
struct ModTool {
  std::string id, name, key, icon, mod;
  std::function<uint32_t(int x, int y, uint32_t before, uint32_t color)> ink;
};

// Finds, loads and unloads mods of the three tiers, and watches their files for hot reload
class ModHost {
public:
  ModHost();
  ~ModHost();

  // Makes a GL 3.3 context current for shader mods; false when there is none (the effect then fails)
  std::function<bool()> ensureGl;

  void scan(const std::vector<std::string>& roots);
  // Loads the enabled mods and unloads the others; `enabled` overrides the defaults (examples off, others on)
  void apply(const std::map<std::string, bool>& enabled);
  // Reloads mods whose files changed; true when one did
  bool poll();
  bool reload(const std::string& id);

  std::vector<ModInfo>& mods() { return _mods; }
  ModInfo* find(const std::string& id);
  const std::vector<ModCommand>& commands() const { return _commands; }
  const std::vector<ModTool>& tools() const { return _tools; }
  const std::vector<ModWindow>& windows() const { return _windows; }
  // Opens or closes a window by "<mod>/<id>" (david.openWindow / closeWindow)
  std::function<void(const std::string& key, bool open)> showWindow;

  // A one-off Lua script on a document (CLI --script): `david.image` is the document, `david.args` the arguments
  bool runScript(const std::string& path, Document& doc, const std::map<std::string, std::string>& args, const EffectEnv& env, std::string& error);

  struct Loaded;

private:
  std::vector<ModInfo> _mods;
  std::vector<ModCommand> _commands;
  std::vector<ModTool> _tools;
  std::vector<ModWindow> _windows;
  std::map<std::string, std::unique_ptr<Loaded>> _loaded;

  bool load(ModInfo& m);
  void unload(ModInfo& m);
  bool loadShader(ModInfo& m, Loaded& l);
  bool loadLua(ModInfo& m, Loaded& l);
  bool loadNative(ModInfo& m, Loaded& l);
};

// Parses a shader mod's header comments into an effect (no GL needed)
bool parseShaderMod(const std::string& source, const std::string& fallbackId, Effect& out, std::string& error);

}
