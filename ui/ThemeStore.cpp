#include "ui/ThemeStore.h"
#include "core/io/Png.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace david {

static const char* EXT = ".theme.json";

static bool isThemeFile(const fs::path& p, std::string& id) {
  std::string name = pathToUtf8(p.filename());
  size_t n = std::strlen(EXT);
  if(name.size() <= n || name.compare(name.size() - n, n, EXT)) return false;
  id = name.substr(0, name.size() - n);
  return true;
}

void ThemeStore::init(const std::string& builtinDir, const std::string& userDir) {
  _builtinDir = builtinDir;
  _userDir = userDir;
  std::error_code ec;
  fs::create_directories(pathFromUtf8(_userDir), ec);
  scan();
}

void ThemeStore::scan() {
  std::map<std::string, Entry> found;
  auto walk = [&](const std::string& dir, bool builtin) {
    std::error_code ec;
    for(auto& f : fs::directory_iterator(pathFromUtf8(dir), ec)) {
      std::string id;
      if(!f.is_regular_file() || !isThemeFile(f.path(), id)) continue;
      Entry& e = found[id];
      e.id = id;
      (builtin ? e.builtin : e.user) = true;
    }
  };
  walk(_builtinDir, true);
  walk(_userDir, false);
  _list.clear();
  for(auto& [id, e] : found) {
    std::string err;
    if(!load(id, e.theme, err)) continue;
    e.name = e.theme.name.empty() ? id : e.theme.name;
    _list.push_back(std::move(e));
  }
  // Dev first, then Classic, then the rest by name
  auto rank = [](const Entry& e) { return e.id == "dev" ? 0 : e.id == "classic" ? 1 : 2; };
  std::stable_sort(_list.begin(), _list.end(), [&](const Entry& a, const Entry& b) {
    return rank(a) != rank(b) ? rank(a) < rank(b) : a.name < b.name;
  });
}

const ThemeStore::Entry* ThemeStore::find(const std::string& id) const {
  for(auto& e : _list) if(e.id == id) return &e;
  return nullptr;
}

std::string ThemeStore::pathOf(const std::string& id) const {
  std::string user = _userDir + id + EXT;
  std::error_code ec;
  if(fs::exists(pathFromUtf8(user), ec)) return user;
  return _builtinDir + id + EXT;
}

bool ThemeStore::load(const std::string& id, Theme& out, std::string& error) const {
  return out.load(pathOf(id), error);
}

bool ThemeStore::saveUser(const std::string& id, const Theme& theme, std::string& error) {
  bool ok = theme.save(_userDir + id + EXT, error);
  if(ok) scan();
  return ok;
}

Theme ThemeStore::defaults(const std::string& id) const {
  Theme t = Theme::dev();
  std::string err;
  if(const Entry* e = find(id); e && e->builtin) t.load(_builtinDir + id + EXT, err);
  return t;
}

bool ThemeStore::resetToBuiltin(const std::string& id) {
  const Entry* e = find(id);
  if(!e || !e->builtin || !e->user) return false;
  std::error_code ec;
  fs::remove(pathFromUtf8(_userDir + id + EXT), ec);
  scan();
  return !ec;
}

bool ThemeStore::remove(const std::string& id) {
  const Entry* e = find(id);
  if(!e || e->builtin) return false;
  std::error_code ec;
  fs::remove(pathFromUtf8(_userDir + id + EXT), ec);
  scan();
  return !ec;
}

std::string ThemeStore::uniqueId(const std::string& name) const {
  std::string slug;
  for(unsigned char c : name) {
    if(std::isalnum(c)) slug += char(std::tolower(c));
    else if(!slug.empty() && slug.back() != '-') slug += '-';
  }
  while(!slug.empty() && slug.back() == '-') slug.pop_back();
  if(slug.empty()) slug = "theme";
  std::string id = slug;
  for(int i = 2; find(id); i++) id = slug + "-" + std::to_string(i);
  return id;
}

std::string ThemeStore::duplicate(const Theme& theme, const std::string& name, std::string& error) {
  Theme copy = theme;
  copy.name = name;
  std::string id = uniqueId(name);
  return saveUser(id, copy, error) ? id : "";
}

std::string ThemeStore::import(const std::string& file, std::string& error) {
  Theme t;
  if(!t.load(file, error)) return "";
  std::string id;
  if(!isThemeFile(pathFromUtf8(file), id)) id = pathToUtf8(pathFromUtf8(file).stem());
  if(find(id)) id = uniqueId(t.name.empty() ? id : t.name);
  return saveUser(id, t, error) ? id : "";
}

bool ThemeStore::exportTo(const std::string& id, const std::string& file, std::string& error) const {
  Theme t;
  return load(id, t, error) && t.save(file, error);
}

int64_t ThemeStore::stamp(const std::string& id) const {
  std::error_code ec;
  auto t = fs::last_write_time(pathFromUtf8(pathOf(id)), ec);
  return ec ? 0 : int64_t(t.time_since_epoch().count());
}

}
