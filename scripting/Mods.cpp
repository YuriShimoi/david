#include "scripting/Mods.h"
#include "core/Document.h"
#include "core/io/Formats.h"
#include "core/io/Png.h"
#include "render/ShaderRunner.h"
#include "scripting/LuaApi.h"
#include "sdk/david_plugin.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace david {

struct ModHost::Loaded {
  std::map<std::string, int64_t> watched;  // file -> mtime, for hot reload
  std::shared_ptr<LuaRuntime> lua;
  void* lib = nullptr;
};

namespace {
int64_t mtime(const std::string& path) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(pathFromUtf8(path), ec);
  return ec ? 0 : int64_t(t.time_since_epoch().count());
}

std::string readText(const std::string& path) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}
}

ModHost::ModHost() = default;

ModHost::~ModHost() {
  for(auto& m : _mods) if(m.loaded) unload(m);
}

ModInfo* ModHost::find(const std::string& id) {
  for(auto& m : _mods) if(m.id == id) return &m;
  return nullptr;
}

#pragma region [ Discovery ]
// The mod in a folder, from its mod.json; false when there is no mod.json
static bool readModInfo(const std::filesystem::path& dir, bool example, ModInfo& m) {
  auto file = dir / "mod.json";
  std::error_code ec;
  if(!std::filesystem::exists(file, ec)) return false;
  {
    m = {};
    m.id = pathToUtf8(dir.filename());
    m.dir = pathToUtf8(dir) + "/";
    m.example = example;
    auto j = nlohmann::json::parse(readText(pathToUtf8(file)), nullptr, false);
    if(j.is_discarded() || !j.is_object()) m.error = "mod.json is not valid JSON";
    else {
      m.name = j.value("name", m.id);
      m.version = j.value("version", "");
      m.kind = j.value("kind", "");
      m.entry = j.value("entry", "");
      m.description = j.value("description", "");
      m.api = j.value("api", 1);
      m.permissions = j.value("permissions", std::vector<std::string>());
      if(m.kind != "shader" && m.kind != "lua" && m.kind != "native") m.error = "\"kind\" must be shader, lua or native";
      else if(m.entry.empty()) m.error = "\"entry\" is missing";
      else if(m.api > 1) m.error = "needs API version " + std::to_string(m.api) + "; this David has 1";
    }
    if(m.name.empty()) m.name = m.id;
  }
  return true;
}

void ModHost::scan(const std::vector<std::string>& roots) {
  auto readMod = [&](const std::filesystem::path& dir, bool example) {
    ModInfo m;
    if(!readModInfo(dir, example, m)) return;
    // A later root with the same id (the config folder) replaces the bundled one
    for(auto& o : _mods) if(o.id == m.id) { if(o.loaded) unload(o); o = std::move(m); return; }
    _mods.push_back(std::move(m));
  };
  for(auto& r : roots) {
    std::error_code ec;
    auto root = pathFromUtf8(r);
    if(!std::filesystem::is_directory(root, ec)) continue;
    std::vector<std::filesystem::path> dirs;
    for(auto& d : std::filesystem::directory_iterator(root, ec)) if(d.is_directory()) dirs.push_back(d.path());
    std::sort(dirs.begin(), dirs.end());
    for(auto& d : dirs) {
      if(d.filename() == "examples") {
        std::vector<std::filesystem::path> ex;
        for(auto& e : std::filesystem::directory_iterator(d, ec)) if(e.is_directory()) ex.push_back(e.path());
        std::sort(ex.begin(), ex.end());
        for(auto& e : ex) readMod(e, true);
      } else readMod(d, false);
    }
  }
}

void ModHost::apply(const std::map<std::string, bool>& enabled) {
  for(auto& m : _mods) {
    auto it = enabled.find(m.id);
    m.enabled = it != enabled.end() ? it->second : !m.example;
    if(m.enabled && !m.loaded && !m.kind.empty()) load(m);
    else if(!m.enabled && m.loaded) unload(m);
  }
}

bool ModHost::poll() {
  bool any = false;
  for(auto& m : _mods) {
    if(!m.enabled) continue;
    auto it = _loaded.find(m.id);
    // A failed mod is retried when its files change too
    std::vector<std::string> files = { m.dir + "mod.json", m.dir + m.entry };
    bool changed = false;
    if(it != _loaded.end()) {
      for(auto& [f, t] : it->second->watched) if(mtime(f) != t) changed = true;
    } else if(!m.error.empty()) {
      static std::map<std::string, int64_t> seen;
      for(auto& f : files) { int64_t t = mtime(f); auto& s = seen[f]; if(s && s != t) changed = true; s = t; }
    }
    if(changed && m.kind != "native") { reload(m.id); any = true; }
  }
  return any;
}

bool ModHost::reload(const std::string& id) {
  ModInfo* m = find(id);
  if(!m) return false;
  if(m->loaded) unload(*m);
  // mod.json may have changed too
  bool enabled = m->enabled;
  auto log = std::move(m->log);
  ModInfo fresh;
  if(!readModInfo(pathFromUtf8(m->dir).parent_path(), m->example, fresh)) return false;
  *m = std::move(fresh);
  m->enabled = enabled;
  m->log = std::move(log);
  return m->enabled && load(*m);
}
#pragma endregion

#pragma region [ Load and unload ]
bool ModHost::load(ModInfo& m) {
  // A bad mod.json stays an error until the file changes
  if(!m.error.empty()) return false;
  m.provides.clear();
  auto l = std::make_unique<Loaded>();
  l->watched[m.dir + "mod.json"] = mtime(m.dir + "mod.json");
  bool ok = false;
  try {
    if(m.kind == "shader") ok = loadShader(m, *l);
    else if(m.kind == "lua") ok = loadLua(m, *l);
    else if(m.kind == "native") ok = loadNative(m, *l);
  } catch(const std::exception& e) {
    m.error = e.what();
    ok = false;
  }
  m.loaded = true;
  _loaded[m.id] = std::move(l);
  if(!ok) { unload(m); m.loaded = false; if(m.error.empty()) m.error = "failed to load"; }
  return ok;
}

void ModHost::unload(ModInfo& m) {
  // What the mod registered goes first, while its code is still there
  unregisterEffects(m.id);
  unregisterFormats(m.id);
  _commands.erase(std::remove_if(_commands.begin(), _commands.end(), [&](const ModCommand& c) { return c.mod == m.id; }), _commands.end());
  _tools.erase(std::remove_if(_tools.begin(), _tools.end(), [&](const ModTool& t) { return t.mod == m.id; }), _tools.end());
  _windows.erase(std::remove_if(_windows.begin(), _windows.end(), [&](const ModWindow& w) { return w.mod == m.id; }), _windows.end());
  auto it = _loaded.find(m.id);
  if(it != _loaded.end()) {
    it->second->lua.reset();
    if(void* lib = it->second->lib) {
#ifdef _WIN32
      FreeLibrary(HMODULE(lib));
#else
      dlclose(lib);
#endif
    }
    // Keep the watch list of a failed mod, so fixing its files reloads it
    _loaded.erase(it);
  }
  m.loaded = false;
}
#pragma endregion

#pragma region [ Shader tier ]
bool parseShaderMod(const std::string& source, const std::string& fallbackId, Effect& out, std::string& error) {
  out = {};
  out.id = fallbackId;
  out.name = fallbackId;
  out.category = "Effects/Mods";
  std::istringstream in(source);
  std::string line;
  int lineNo = 0;
  while(std::getline(in, line)) {
    lineNo++;
    size_t at = line.find("// @");
    if(at == std::string::npos) continue;
    std::istringstream ls(line.substr(at + 4));
    std::string key;
    ls >> key;
    std::string rest;
    std::getline(ls, rest);
    rest.erase(0, rest.find_first_not_of(" \t"));
    while(!rest.empty() && (rest.back() == '\r' || rest.back() == ' ')) rest.pop_back();
    if(key == "name") out.name = rest;
    else if(key == "id") out.id = rest;
    else if(key == "category") out.category = rest;
    else if(key == "dim") out.dim = true;
    else if(key == "param") {
      // id type min..max = default "Label"   (choice: id choice a|b|c = 0 "Label"; color: id color = #RRGGBBAA)
      EffectParam p;
      std::istringstream ps(rest);
      std::string type, range;
      ps >> p.id >> type;
      std::string tail;
      std::getline(ps, tail);
      size_t q = tail.find('"');
      if(q != std::string::npos) { size_t q2 = tail.find('"', q + 1); p.label = tail.substr(q + 1, q2 - q - 1); tail = tail.substr(0, q); }
      if(p.label.empty()) p.label = p.id;
      size_t eq = tail.find('=');
      std::string def = eq == std::string::npos ? "" : tail.substr(eq + 1);
      def.erase(0, def.find_first_not_of(" \t"));
      def.erase(def.find_last_not_of(" \t") + 1);
      range = eq == std::string::npos ? tail : tail.substr(0, eq);
      range.erase(0, range.find_first_not_of(" \t"));
      range.erase(range.find_last_not_of(" \t") + 1);
      if(type == "float" || type == "int") {
        p.type = type == "float" ? EParam::Float : EParam::Int;
        size_t dots = range.find("..");
        if(dots == std::string::npos) { error = "line " + std::to_string(lineNo) + ": a " + type + " param needs min..max"; return false; }
        p.min = std::atof(range.substr(0, dots).c_str());
        p.max = std::atof(range.substr(dots + 2).c_str());
        p.def = def.empty() ? p.min : std::atof(def.c_str());
      } else if(type == "bool") {
        p.type = EParam::Bool; p.max = 1;
        p.def = def == "true" || def == "1" ? 1 : 0;
      } else if(type == "choice") {
        p.type = EParam::Choice;
        std::stringstream cs(range);
        std::string c;
        while(std::getline(cs, c, '|')) p.choices.push_back(c);
        p.max = double(std::max<size_t>(p.choices.size(), 1) - 1);
        p.def = std::atof(def.c_str());
      } else if(type == "color") {
        p.type = EParam::Color;
        std::string h = def[0] == '#' ? def.substr(1) : def;
        unsigned v = 0xFF000000u;
        if(h.size() == 6) { std::sscanf(h.c_str(), "%x", &v); v = (v >> 16) | (v & 0xFF00) | (v & 0xFF) << 16 | 0xFF000000u; }
        else if(h.size() == 8) { std::sscanf(h.c_str(), "%x", &v); v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24); }
        p.def = double(v);
      } else { error = "line " + std::to_string(lineNo) + ": unknown param type '" + type + "'"; return false; }
      out.params.push_back(std::move(p));
    }
  }
  return true;
}

bool ModHost::loadShader(ModInfo& m, Loaded& l) {
  std::string path = m.dir + m.entry;
  std::string source = readText(path);
  if(source.empty()) { m.error = "can't read " + m.entry; return false; }
  l.watched[path] = mtime(path);
  Effect e;
  if(!parseShaderMod(source, m.id, e, m.error)) return false;
  // Compile now, so errors show in Settings > Mods instead of in the middle of a preview
  if(ensureGl && ensureGl()) {
    std::string err;
    if(!checkShader(source, e.params, err)) { m.error = m.entry + ": " + err; return false; }
  }
  e.owner = m.id;
  auto params = e.params;
  auto gl = ensureGl;
  e.run = [source, params, gl](const Image& src, Image& dst, const ParamValues& v, const EffectEnv& env) {
    if(!gl || !gl()) throw std::runtime_error("shader effects need OpenGL 3.3");
    std::string err;
    if(!runShader(source, params, src, dst, v, env, err)) throw std::runtime_error(err);
  };
  m.provides.push_back("effect:" + e.id);
  registerEffect(std::move(e));
  return true;
}
#pragma endregion

#pragma region [ Lua tier ]
bool ModHost::loadLua(ModInfo& m, Loaded& l) {
  std::string path = m.dir + m.entry;
  l.watched[path] = mtime(path);
  ModInfo* mp = &m;
  l.lua = luaOpen(m.id, [mp](const std::string& line) {
    mp->log.push_back(line);
    if(mp->log.size() > 50) mp->log.erase(mp->log.begin());
  });
  std::string mod = m.id;
  luaSetWindowHook(*l.lua, [this, mod](const std::string& id, bool open) { if(showWindow) showWindow(mod + "/" + id, open); });
  if(!luaRunFile(*l.lua, path, m.error)) return false;
  auto& regs = luaRegistrations(*l.lua);
  for(auto& e : regs.effects) { m.provides.push_back("effect:" + e.id); registerEffect(e); }
  for(auto& c : regs.commands) { m.provides.push_back("command:" + c.id); _commands.push_back(c); }
  for(auto& t : regs.tools) { m.provides.push_back("tool:" + t.id); _tools.push_back(t); }
  for(auto& w : regs.windows) { m.provides.push_back("window:" + w.id); _windows.push_back(w); }
  regs = {};  // the copies above hold the runtime; keeping these too would be a cycle
  return true;
}

bool ModHost::runScript(const std::string& path, Document& doc, const std::map<std::string, std::string>& args, const EffectEnv& env, std::string& error) {
  return luaRunScript(path, doc, args, env, error);
}
#pragma endregion

#pragma region [ Native tier ]
namespace {
// The mod whose david_plugin_init is running; the host callbacks register into it
thread_local ModInfo* s_initMod = nullptr;

EParam nativeType(const std::string& t) {
  if(t == "int") return EParam::Int;
  if(t == "bool") return EParam::Bool;
  if(t == "choice") return EParam::Choice;
  if(t == "color") return EParam::Color;
  return EParam::Float;
}

std::string valuesJson(const Effect& e, const ParamValues& v) {
  nlohmann::json j = nlohmann::json::object();
  for(auto& p : e.params) {
    if(p.type == EParam::Text || p.type == EParam::Curve) j[p.id] = v.str(p.id);
    else if(p.type == EParam::Bool) j[p.id] = v.get(p.id) != 0;
    else if(p.type == EParam::Float) j[p.id] = v.get(p.id, p.def);
    else j[p.id] = int64_t(v.get(p.id, p.def));
  }
  return j.dump();
}

int hostRegisterEffect(const david_effect* d) {
  if(!s_initMod || !d || !d->id || !d->run) return 1;
  Effect e;
  e.id = d->id;
  e.name = d->name ? d->name : d->id;
  e.category = d->category ? d->category : "Effects/Mods";
  e.owner = s_initMod->id;
  if(d->params_json && *d->params_json) {
    auto j = nlohmann::json::parse(d->params_json, nullptr, false);
    if(!j.is_array()) return 2;
    for(auto& p : j) {
      EffectParam ep;
      ep.id = p.value("id", "");
      ep.label = p.value("label", ep.id);
      ep.type = nativeType(p.value("type", "float"));
      ep.min = p.value("min", 0.0);
      ep.max = p.value("max", ep.type == EParam::Bool ? 1.0 : 100.0);
      ep.def = p.value("default", ep.min);
      ep.choices = p.value("choices", std::vector<std::string>());
      if(!ep.choices.empty()) ep.max = double(ep.choices.size() - 1);
      if(!ep.id.empty()) e.params.push_back(std::move(ep));
    }
  }
  auto run = d->run;
  void* user = d->user;
  Effect copy = e;
  e.run = [run, user, copy](const Image& src, Image& dst, const ParamValues& v, const EffectEnv&) {
    david_image s{ src.w, src.h, const_cast<uint32_t*>(src.px.data()) }, o{ dst.w, dst.h, dst.px.data() };
    std::string values = valuesJson(copy, v);
    if(int r = run(user, &s, &o, values.c_str())) throw std::runtime_error("the plugin effect failed (" + std::to_string(r) + ")");
  };
  s_initMod->provides.push_back("effect:" + e.id);
  registerEffect(std::move(e));
  return 0;
}

int hostRegisterFormat(const david_format* d) {
  if(!s_initMod || !d || !d->ext || (!d->load && !d->save)) return 1;
  FileFormat f;
  f.ext = d->ext;
  std::transform(f.ext.begin(), f.ext.end(), f.ext.begin(), [](char c) { return char(std::tolower(c)); });
  f.name = d->name ? d->name : f.ext;
  f.owner = s_initMod->id;
  auto load = d->load;
  auto save = d->save;
  void* user = d->user;
  if(load) f.load = [load, user](const std::vector<uint8_t>& bytes, std::string& error) -> std::unique_ptr<Document> {
    david_image img{};
    if(load(user, bytes.data(), bytes.size(), &img) || !img.pixels || img.width <= 0 || img.height <= 0) {
      std::free(img.pixels);
      error = "the plugin couldn't read the file";
      return nullptr;
    }
    auto doc = documentFromPixels(img.width, img.height, img.pixels);
    std::free(img.pixels);
    return doc;
  };
  if(save) f.save = [save, user](const Document& doc, const SaveOptions&, std::string& error) -> std::vector<uint8_t> {
    auto px = flattenPixels(doc);
    david_image img{ doc.width(), doc.height(), px.data() };
    uint8_t* bytes = nullptr;
    size_t size = 0;
    if(save(user, &img, &bytes, &size) || !bytes) { std::free(bytes); error = "the plugin couldn't write the file"; return {}; }
    std::vector<uint8_t> out(bytes, bytes + size);
    std::free(bytes);
    return out;
  };
  s_initMod->provides.push_back("format:" + f.ext);
  registerFormat(std::move(f));
  return 0;
}

void hostLog(const char* msg) {
  if(s_initMod && msg) s_initMod->log.push_back(msg);
}
}

bool ModHost::loadNative(ModInfo& m, Loaded& l) {
#ifdef _WIN32
  std::string file = m.dir + m.entry + ".dll";
  HMODULE lib = LoadLibraryW(pathFromUtf8(file).wstring().c_str());
  if(!lib) { m.error = "can't load " + m.entry + ".dll (error " + std::to_string(GetLastError()) + ")"; return false; }
  auto init = reinterpret_cast<david_plugin_init_fn>(GetProcAddress(lib, "david_plugin_init"));
#else
  std::string file = m.dir + "lib" + m.entry + ".so";
  if(!std::filesystem::exists(pathFromUtf8(file))) file = m.dir + m.entry + ".so";
  void* lib = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
  if(!lib) { m.error = dlerror(); return false; }
  auto init = reinterpret_cast<david_plugin_init_fn>(dlsym(lib, "david_plugin_init"));
#endif
  l.lib = reinterpret_cast<void*>(lib);
  if(!init) { m.error = "no david_plugin_init in " + m.entry; return false; }
  // Memory crosses the boundary through the host's allocator only
  static const david_host host = {
    DAVID_API_VERSION,
    [](size_t n) -> void* { return std::malloc(n); },
    [](void* p) { std::free(p); },
    hostRegisterEffect, hostRegisterFormat, hostLog };
  s_initMod = &m;
  int r = init(&host);
  s_initMod = nullptr;
  if(r) { m.error = "david_plugin_init returned " + std::to_string(r); return false; }
  return true;
}
#pragma endregion

}
