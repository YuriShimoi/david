#include "core/Effect.h"
#include "core/Document.h"
#include "core/Paint.h"
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <sstream>
#include <thread>

namespace david {

static std::vector<Effect>& registry() {
  static std::vector<Effect> list;
  return list;
}

void registerEffect(Effect e) {
  auto& list = registry();
  for(auto& old : list) if(old.id == e.id) { old = std::move(e); return; }
  list.push_back(std::move(e));
}

void unregisterEffects(const std::string& owner) {
  auto& list = registry();
  list.erase(std::remove_if(list.begin(), list.end(), [&](const Effect& e) { return !owner.empty() && e.owner == owner; }), list.end());
}

const std::vector<Effect>& effects() {
  registerBuiltinEffects();
  return registry();
}

const Effect* findEffect(const std::string& id) {
  for(auto& e : effects()) if(e.id == id) return &e;
  return nullptr;
}

ParamValues defaultValues(const Effect& e) {
  ParamValues v;
  for(auto& p : e.params) {
    if(p.type == EParam::Text || p.type == EParam::Curve) v.text[p.id] = p.text;
    else v.num[p.id] = p.def;
  }
  return v;
}

static bool parseColor(const std::string& s, double& out) {
  std::string h = !s.empty() && s[0] == '#' ? s.substr(1) : s;
  // Short forms: #rgb and #rgba
  if(h.size() == 3 || h.size() == 4) { std::string l; for(char c : h) { l += c; l += c; } h = l; }
  unsigned v = 0;
  if((h.size() != 6 && h.size() != 8) || std::sscanf(h.c_str(), "%x", &v) != 1) return false;
  uint32_t r, g, b, a = 255;
  if(h.size() == 8) { r = v >> 24; g = (v >> 16) & 0xFF; b = (v >> 8) & 0xFF; a = v & 0xFF; }
  else { r = v >> 16; g = (v >> 8) & 0xFF; b = v & 0xFF; }
  out = double(r | g << 8 | b << 16 | a << 24);
  return true;
}

bool parseValues(const Effect& e, const std::string& spec, ParamValues& out, std::string& error) {
  std::stringstream ss(spec);
  std::string item;
  while(std::getline(ss, item, ',')) {
    if(item.empty()) continue;
    size_t eq = item.find('=');
    if(eq == std::string::npos) { error = "expected name=value in '" + item + "'"; return false; }
    std::string k = item.substr(0, eq), val = item.substr(eq + 1);
    auto p = std::find_if(e.params.begin(), e.params.end(), [&](const EffectParam& q) { return q.id == k; });
    if(p == e.params.end()) { error = "unknown parameter '" + k + "' for " + e.id; return false; }
    switch(p->type) {
      case EParam::Text: out.text[k] = val; break;
      case EParam::Curve:
        // CLI form "0:0/128:150/255:255" (commas separate params there)
        out.text[k] = val;
        std::replace(out.text[k].begin(), out.text[k].end(), ':', ',');
        std::replace(out.text[k].begin(), out.text[k].end(), '/', ' ');
        break;
      case EParam::Color:
        if(!parseColor(val, out.num[k])) { error = "bad color '" + val + "'"; return false; }
        break;
      case EParam::Bool: out.num[k] = (val == "1" || val == "true" || val == "on" || val == "yes") ? 1 : 0; break;
      case EParam::Choice: {
        auto c = std::find(p->choices.begin(), p->choices.end(), val);
        if(c != p->choices.end()) { out.num[k] = double(c - p->choices.begin()); break; }
        // Case-insensitive name, or the index
        bool found = false;
        for(size_t i = 0; i < p->choices.size(); i++) {
          std::string a = p->choices[i], b = val;
          std::transform(a.begin(), a.end(), a.begin(), ::tolower);
          std::transform(b.begin(), b.end(), b.begin(), ::tolower);
          if(a == b) { out.num[k] = double(i); found = true; break; }
        }
        if(found) break;
        char* end = nullptr;
        long i = std::strtol(val.c_str(), &end, 10);
        if(*end || i < 0 || i >= long(p->choices.size())) { error = "bad choice '" + val + "' for " + k; return false; }
        out.num[k] = double(i);
        break;
      }
      default: {
        char* end = nullptr;
        double d = std::strtod(val.c_str(), &end);
        if(end == val.c_str() || *end) { error = "bad number '" + val + "' for " + k; return false; }
        out.num[k] = std::clamp(d, p->min, p->max);
      }
    }
  }
  return true;
}

void parallelRows(int h, const std::function<void(int, int)>& fn) {
  int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
  int band = std::max(16, (h + threads - 1) / threads);
  std::vector<std::thread> pool;
  for(int y = band; y < h; y += band) pool.emplace_back(fn, y, std::min(h, y + band));
  fn(0, std::min(h, band));
  for(auto& t : pool) t.join();
}

std::vector<uint8_t> curveTable(const std::string& points) {
  // Points sorted by x; at least the two ends
  std::vector<std::pair<double, double>> p;
  std::stringstream ss(points);
  std::string item;
  while(ss >> item) {
    double x, y;
    if(std::sscanf(item.c_str(), "%lf,%lf", &x, &y) == 2) p.push_back({ std::clamp(x, 0.0, 255.0), std::clamp(y, 0.0, 255.0) });
  }
  std::sort(p.begin(), p.end());
  p.erase(std::unique(p.begin(), p.end(), [](auto& a, auto& b) { return a.first == b.first; }), p.end());
  std::vector<uint8_t> lut(256);
  if(p.size() < 2) { for(int i = 0; i < 256; i++) lut[i] = uint8_t(i); return lut; }
  // Fritsch-Carlson monotone tangents
  size_t n = p.size();
  std::vector<double> d(n - 1), m(n);
  for(size_t i = 0; i + 1 < n; i++) d[i] = (p[i + 1].second - p[i].second) / (p[i + 1].first - p[i].first);
  m[0] = d[0]; m[n - 1] = d[n - 2];
  for(size_t i = 1; i + 1 < n; i++) m[i] = d[i - 1] * d[i] <= 0 ? 0 : (d[i - 1] + d[i]) / 2;
  for(size_t i = 0; i + 1 < n; i++) {
    if(d[i] == 0) { m[i] = m[i + 1] = 0; continue; }
    double a = m[i] / d[i], b = m[i + 1] / d[i], s = a * a + b * b;
    if(s > 9) { double t = 3 / std::sqrt(s); m[i] = t * a * d[i]; m[i + 1] = t * b * d[i]; }
  }
  for(int x = 0; x < 256; x++) {
    double y;
    if(x <= p[0].first) y = p[0].second;
    else if(x >= p[n - 1].first) y = p[n - 1].second;
    else {
      size_t i = 0;
      while(p[i + 1].first < x) i++;
      double h = p[i + 1].first - p[i].first, t = (x - p[i].first) / h, t2 = t * t, t3 = t2 * t;
      y = (2 * t3 - 3 * t2 + 1) * p[i].second + (t3 - 2 * t2 + t) * h * m[i] + (-2 * t3 + 3 * t2) * p[i + 1].second + (t3 - t2) * h * m[i + 1];
    }
    lut[x] = uint8_t(std::clamp(int(std::lround(y)), 0, 255));
  }
  return lut;
}

#pragma region [ Running ]
static Image readRegion(const Layer& layer, IRect r) {
  Image img(r.width(), r.height());
  for(int y = r.y0; y <= r.y1; y++) {
    int ty = y / Tile::SIZE, ry = y % Tile::SIZE;
    for(int x = r.x0; x <= r.x1;) {
      int tx = x / Tile::SIZE, rx = x % Tile::SIZE, n = std::min(Tile::SIZE - rx, r.x1 - x + 1);
      if(const Tile* t = layer.tile(tx, ty)) std::copy_n(t->data() + ry * Tile::SIZE + rx, n, &img.at(x - r.x0, y - r.y0));
      x += n;
    }
  }
  return img;
}

Image runEffect(const Effect& e, const Image& src, const ParamValues& v, const EffectEnv& env) {
  Image dst = src;
  e.run(src, dst, v, env);
  return dst;
}

void applyEffect(Document& doc, TileRecorder& rec, const Effect& e, const ParamValues& v, const EffectEnv& envIn) {
  Layer& layer = rec.layer();
  IRect r = doc.hasSelection() ? doc.selection->bounds() : IRect{ 0, 0, layer.width() - 1, layer.height() - 1 };
  if(r.empty()) return;
  int m = e.margin ? std::max(0, e.margin(v)) : 0;
  IRect src = { std::max(0, r.x0 - m), std::max(0, r.y0 - m), std::min(layer.width() - 1, r.x1 + m), std::min(layer.height() - 1, r.y1 + m) };
  Image in = readRegion(layer, src);
  EffectEnv env = envIn;
  env.originX = src.x0; env.originY = src.y0;
  Image out = runEffect(e, in, v, env);
  paintRegion(doc, rec, r, [&](int x0, int y, int n, const uint32_t*, uint32_t* o) {
    std::copy_n(&out.at(x0 - src.x0, y - src.y0), n, o);
  });
}
#pragma endregion

}
