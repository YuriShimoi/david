#pragma once
#include "ui/Theme.h"
#include <string>
#include <vector>

namespace david {

// Built-in themes live next to the app, user themes in the config folder. A user file with the same id as a
// built-in one overrides it (that's how a built-in theme gets edited), and "reset" deletes that copy.
class ThemeStore {
public:
  struct Entry {
    std::string id;       // file name without .theme.json
    std::string name;
    bool builtin = false; // a built-in file exists
    bool user = false;    // a user file exists
    Theme theme;          // loaded for the list swatches and preview
  };

  void init(const std::string& builtinDir, const std::string& userDir);
  void scan();
  const std::vector<Entry>& list() const { return _list; }
  const Entry* find(const std::string& id) const;
  std::string userDir() const { return _userDir; }

  std::string pathOf(const std::string& id) const;
  bool load(const std::string& id, Theme& out, std::string& error) const;
  bool saveUser(const std::string& id, const Theme& theme, std::string& error);
  bool resetToBuiltin(const std::string& id);
  // The values a theme started from: its built-in file, or Dev for themes made by the user
  Theme defaults(const std::string& id) const;
  bool remove(const std::string& id);
  std::string duplicate(const Theme& theme, const std::string& name, std::string& error);
  std::string import(const std::string& file, std::string& error);
  bool exportTo(const std::string& id, const std::string& file, std::string& error) const;
  int64_t stamp(const std::string& id) const;

private:
  std::string _builtinDir, _userDir;
  std::vector<Entry> _list;
  std::string uniqueId(const std::string& name) const;
};

}
