#include "core/Pattern.h"
#include "core/Document.h"
#include "core/Gradient.h"
#include "core/io/Png.h"
#include "core/io/Zip.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace david {

static std::vector<Pattern>& list() {
  static std::vector<Pattern> l;
  if(l.empty()) {
    // Bayer matrices by the recursive rule
    for(int n : { 2, 4, 8 }) {
      std::vector<int> m = { 0 };
      for(int s = 1; s < n; s *= 2) {
        std::vector<int> next(size_t(s) * 2 * s * 2);
        for(int y = 0; y < s; y++)
          for(int x = 0; x < s; x++) {
            int v = m[size_t(y) * s + x] * 4;
            next[size_t(y) * s * 2 + x] = v;
            next[size_t(y) * s * 2 + x + s] = v + 2;
            next[size_t(y + s) * s * 2 + x] = v + 3;
            next[size_t(y + s) * s * 2 + x + s] = v + 1;
          }
        m = std::move(next);
      }
      Pattern p = patternFromValues("bayer" + std::to_string(n), n, n, m, n * n - 1);
      p.name = "Bayer " + std::to_string(n) + "×" + std::to_string(n);
      l.push_back(std::move(p));
    }
    Pattern bn;
    bn.id = "bluenoise"; bn.name = "Blue noise"; bn.w = bn.h = 64;
    bn.th = blueNoise();
    for(auto& t : bn.th) t = std::clamp(t, 0.5f / 4096, 1 - 0.5f / 4096);
    l.push_back(std::move(bn));
  }
  return l;
}

const std::vector<Pattern>& patterns() { return list(); }

const Pattern* findPattern(const std::string& id) {
  for(auto& p : list()) if(p.id == id) return &p;
  return nullptr;
}

void addPattern(Pattern p) {
  auto& l = list();
  for(auto& o : l) if(o.id == p.id) { o = std::move(p); return; }
  l.push_back(std::move(p));
}

Pattern patternFromValues(const std::string& id, int w, int h, const std::vector<int>& values, int max) {
  Pattern p;
  p.id = p.name = id; p.w = w; p.h = h;
  if(max < 0) max = values.empty() ? 0 : *std::max_element(values.begin(), values.end());
  for(int v : values) p.th.push_back((std::clamp(v, 0, max) + 0.5f) / (max + 1));
  return p;
}

Pattern patternFromPixels(const std::string& id, int w, int h, const std::vector<uint32_t>& rgba) {
  auto lum = [](uint32_t c) { return int(c & 0xFF) * 299 + int((c >> 8) & 0xFF) * 587 + int((c >> 16) & 0xFF) * 114; };
  std::vector<int> levels;
  for(uint32_t c : rgba) levels.push_back(lum(c));
  std::vector<int> sorted = levels;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
  std::vector<int> ranks;
  for(int l : levels) ranks.push_back(int(std::lower_bound(sorted.begin(), sorted.end(), l) - sorted.begin()));
  return patternFromValues(id, w, h, ranks, int(sorted.size()) - 1);
}

#pragma region [ Files ]
static bool fromPng(const std::string& id, const std::vector<uint8_t>& bytes, Pattern& out, std::string& error) {
  auto doc = png::loadMemory(bytes, error);
  if(!doc) return false;
  std::vector<uint32_t> px;
  for(int y = 0; y < doc->height(); y++) for(int x = 0; x < doc->width(); x++) px.push_back(doc->active().get(x, y));
  out = patternFromPixels(id, doc->width(), doc->height(), px);
  return true;
}

static bool fromMatrix(const std::string& id, const std::string& text, bool csv, Pattern& out, std::string& error) {
  std::vector<std::vector<int>> rows;
  int max = -1;
  if(csv) {
    std::istringstream in(text);
    std::string line;
    while(std::getline(in, line)) {
      std::vector<int> row;
      std::stringstream ls(line);
      std::string cell;
      while(std::getline(ls, cell, ',')) { try { row.push_back(std::stoi(cell)); } catch(...) {} }
      if(!row.empty()) rows.push_back(row);
    }
  } else {
    auto j = nlohmann::json::parse(text, nullptr, false);
    if(j.is_discarded()) { error = "bad JSON"; return false; }
    if(j.is_object()) { max = j.value("max", -1); j = j.value("matrix", nlohmann::json::array()); }
    try { rows = j.get<std::vector<std::vector<int>>>(); } catch(...) { error = "expected a matrix of integers"; return false; }
  }
  if(rows.empty() || rows[0].empty()) { error = "empty matrix"; return false; }
  int w = int(rows[0].size());
  std::vector<int> values;
  for(auto& r : rows) {
    if(int(r.size()) != w) { error = "rows of different lengths"; return false; }
    values.insert(values.end(), r.begin(), r.end());
  }
  out = patternFromValues(id, w, int(rows.size()), values, max);
  return true;
}

bool loadPatternFile(const std::string& path, std::vector<Pattern>& out, std::string& error) {
  auto p = pathFromUtf8(path);
  std::string ext = pathToUtf8(p.extension()), stem = pathToUtf8(p.stem());
  std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return char(std::tolower(c)); });
  std::ifstream in(p, std::ios::binary);
  if(!in) { error = "can't open " + path; return false; }
  std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(in), {} };
  Pattern pat;
  if(ext == ".png") {
    if(!fromPng(stem, bytes, pat, error)) return false;
    out.push_back(std::move(pat));
    return true;
  }
  if(ext == ".json" || ext == ".csv") {
    if(!fromMatrix(stem, std::string(bytes.begin(), bytes.end()), ext == ".csv", pat, error)) return false;
    out.push_back(std::move(pat));
    return true;
  }
  if(ext == ".aseprite-extension" || ext == ".zip") {
    std::vector<zip::Entry> entries;
    if(!zip::read(bytes, entries, error)) return false;
    // package.json may sit at the root or one folder down
    const zip::Entry* pkg = nullptr;
    std::string base;
    for(auto& e : entries) {
      std::string n = e.name;
      if(n == "package.json" || (n.size() > 13 && n.substr(n.size() - 13) == "/package.json" && n.find('/') == n.size() - 13)) { pkg = &e; base = n.substr(0, n.size() - 12); break; }
    }
    if(!pkg) { error = "no package.json in " + path; return false; }
    auto j = nlohmann::json::parse(pkg->data.begin(), pkg->data.end(), nullptr, false);
    if(j.is_discarded()) { error = "bad package.json in " + path; return false; }
    auto mats = j["contributes"]["ditheringMatrices"];
    if(!mats.is_array() || mats.empty()) { error = "no ditheringMatrices in " + path; return false; }
    for(auto& m : mats) {
      std::string file = m.value("path", ""), id = m.value("id", ""), name = m.value("name", id);
      if(file.rfind("./", 0) == 0) file = file.substr(2);
      const zip::Entry* img = zip::find(entries, base + file);
      if(!img || id.empty()) { error = "missing " + file + " in " + path; return false; }
      std::string fext = file.size() > 4 ? file.substr(file.size() - 4) : "";
      bool ok = fext == ".png" ? fromPng(id, img->data, pat, error) : fromMatrix(id, std::string(img->data.begin(), img->data.end()), fext == ".csv", pat, error);
      if(!ok) return false;
      pat.name = name;
      out.push_back(std::move(pat));
    }
    return true;
  }
  error = "unknown pattern file " + path;
  return false;
}

void loadPatternDirs(const std::vector<std::string>& dirs, std::vector<std::string>& errors) {
  for(auto& d : dirs) {
    std::error_code ec;
    auto dir = pathFromUtf8(d);
    if(!std::filesystem::is_directory(dir, ec)) continue;
    std::vector<std::filesystem::path> files;
    for(auto& f : std::filesystem::directory_iterator(dir, ec)) {
      std::string ext = pathToUtf8(f.path().extension());
      std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return char(std::tolower(c)); });
      if(f.is_regular_file() && (ext == ".png" || ext == ".json" || ext == ".csv" || ext == ".aseprite-extension")) files.push_back(f.path());
    }
    std::sort(files.begin(), files.end());
    for(auto& f : files) {
      std::vector<Pattern> got;
      std::string err;
      if(loadPatternFile(pathToUtf8(f), got, err)) for(auto& p : got) addPattern(std::move(p));
      else errors.push_back(err);
    }
  }
}
#pragma endregion

}
