#include "ui/I18n.h"
#include "core/io/Png.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace david {

static std::unordered_map<std::string, std::string> s_table;
// Joined names ("Line / Curve / Shapes") translated part by part, kept so the pointers stay valid
static std::unordered_map<std::string, std::string> s_joined;

const char* tr(const char* text) {
  if(s_table.empty()) return text;
  auto it = s_table.find(text);
  if(it != s_table.end()) return it->second.empty() ? text : it->second.c_str();
  std::string s = text;
  if(s.find(" / ") == std::string::npos) return text;
  auto j = s_joined.find(s);
  if(j == s_joined.end()) {
    std::string out;
    for(size_t a = 0; a <= s.size();) {
      size_t b = std::min(s.find(" / ", a), s.size());
      out += (out.empty() ? "" : " / ") + std::string(tr(s.substr(a, b - a).c_str()));
      a = b + 3;
    }
    j = s_joined.emplace(s, out).first;
  }
  return j->second.c_str();
}


static bool readTable(const std::string& file, std::unordered_map<std::string, std::string>& out, std::string& error) {
  std::ifstream in(pathFromUtf8(file));
  if(!in) { error = "can't open " + file; return false; }
  auto j = nlohmann::json::parse(in, nullptr, false);
  if(j.is_discarded() || !j.is_object()) { error = "bad JSON in " + file; return false; }
  for(auto& [k, v] : j.items()) if(v.is_string()) out[k] = v.get<std::string>();
  return true;
}

bool loadLanguage(const std::string& dir, const std::string& code, std::string& error) {
  s_table.clear();
  s_joined.clear();
  if(code.empty() || code == "en") return true;
  std::unordered_map<std::string, std::string> t;
  if(!readTable(dir + code + ".json", t, error)) return false;
  s_table = std::move(t);
  return true;
}

std::vector<std::string> languages(const std::string& dir) {
  std::vector<std::string> out{ "en" };
  std::error_code ec;
  for(auto& f : std::filesystem::directory_iterator(pathFromUtf8(dir), ec)) {
    if(f.path().extension() != ".json") continue;
    std::string code = pathToUtf8(f.path().stem());
    if(code != "en") out.push_back(code);
  }
  std::sort(out.begin() + 1, out.end());
  return out;
}

std::string languageName(const std::string& dir, const std::string& code) {
  std::unordered_map<std::string, std::string> t;
  std::string err;
  if(readTable(dir + code + ".json", t, err) && t.count("_name")) return t["_name"];
  return code;
}

}
