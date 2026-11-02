#pragma once
#include "scripting/Mods.h"
#include <functional>
#include <memory>
#include <string>

namespace david {

// One sandboxed Lua state with the david API. A mod's effects, commands and tools are collected while its
// entry file runs; they stay valid while the runtime lives.
struct LuaRuntime;

struct LuaRegistrations {
  std::vector<Effect> effects;
  std::vector<ModCommand> commands;
  std::vector<ModTool> tools;
  std::vector<ModWindow> windows;
};

std::shared_ptr<LuaRuntime> luaOpen(const std::string& modId, std::function<void(const std::string&)> log);
bool luaRunFile(LuaRuntime& rt, const std::string& path, std::string& error);
LuaRegistrations& luaRegistrations(LuaRuntime& rt);
// What david.openWindow / closeWindow call (the window id inside the mod)
void luaSetWindowHook(LuaRuntime& rt, std::function<void(const std::string& id, bool open)> hook);
// A script on a document (CLI): david.image, david.args, one undo step
bool luaRunScript(const std::string& path, Document& doc, const std::map<std::string, std::string>& args, const EffectEnv& env, std::string& error);

}
