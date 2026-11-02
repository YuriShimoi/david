#include "core/Palette.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Oklab.h"
#include "core/io/Png.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>
#include <unordered_map>

namespace david {

static uint32_t rgb(int r, int g, int b) {
  return uint32_t(std::clamp(r, 0, 255)) | uint32_t(std::clamp(g, 0, 255)) << 8 | uint32_t(std::clamp(b, 0, 255)) << 16 | 0xFF000000u;
}

int Palette::find(uint32_t c) const {
  for(int i = 0; i < size(); i++) if(((colors[i] ^ c) & 0xFFFFFF) == 0) return i;
  return -1;
}

int Palette::nearest(uint32_t c, bool rgbSpace) const {
  int best = -1;
  float bestD = 1e30f;
  Lab l = toOklab(c);
  for(int i = 0; i < size(); i++) {
    float d;
    if(rgbSpace) {
      int dr = int(c & 0xFF) - int(colors[i] & 0xFF), dg = int((c >> 8) & 0xFF) - int((colors[i] >> 8) & 0xFF), db = int((c >> 16) & 0xFF) - int((colors[i] >> 16) & 0xFF);
      d = float(dr * dr + dg * dg + db * db);
    } else {
      Lab p = toOklab(colors[i]);
      d = (l.L - p.L) * (l.L - p.L) + (l.a - p.a) * (l.a - p.a) + (l.b - p.b) * (l.b - p.b);
    }
    if(d < bestD) { bestD = d; best = i; }
  }
  return best;
}

#pragma region [ Files ]
bool Palette::parse(const std::string& text, std::string& error) {
  std::istringstream in(text);
  std::string line;
  std::vector<uint32_t> out;
  bool gpl = text.rfind("GIMP Palette", 0) == 0, jasc = text.rfind("JASC-PAL", 0) == 0;
  int lineNo = 0;
  while(std::getline(in, line)) {
    lineNo++;
    if(!line.empty() && line.back() == '\r') line.pop_back();
    size_t s = line.find_first_not_of(" \t");
    if(s == std::string::npos) continue;
    line = line.substr(s);
    if(gpl) {
      // Header lines, then "r g b name"
      if(lineNo == 1 || line[0] == '#' || line.rfind("Name:", 0) == 0 || line.rfind("Columns:", 0) == 0) continue;
      int r, g, b;
      if(std::sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) out.push_back(rgb(r, g, b));
    } else if(jasc) {
      // JASC-PAL, version, count, then "r g b"
      if(lineNo <= 3) continue;
      int r, g, b;
      if(std::sscanf(line.c_str(), "%d %d %d", &r, &g, &b) == 3) out.push_back(rgb(r, g, b));
    } else {
      if(line[0] == ';' || line[0] == '/') continue;
      if(line[0] == '#') line = line.substr(1);
      unsigned v;
      if(line.size() >= 6 && std::sscanf(line.c_str(), "%6x", &v) == 1) out.push_back(rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF));
    }
  }
  if(out.empty()) { error = "no colors found"; return false; }
  colors = std::move(out);
  return true;
}

bool Palette::load(const std::string& path, std::string& error) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  if(!in) { error = "can't open " + path; return false; }
  std::stringstream ss;
  ss << in.rdbuf();
  return parse(ss.str(), error);
}

bool Palette::save(const std::string& path, std::string& error) const {
  std::string ext = pathFromUtf8(path).extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return char(std::tolower(c)); });
  std::ofstream out(pathFromUtf8(path), std::ios::binary);
  if(!out) { error = "can't write " + path; return false; }
  char buf[64];
  if(ext == ".gpl") {
    out << "GIMP Palette\nName: " << pathToUtf8(pathFromUtf8(path).stem()) << "\nColumns: 8\n#\n";
    for(uint32_t c : colors) {
      std::snprintf(buf, sizeof buf, "%3d %3d %3d\t#%02X%02X%02X\n", int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF), int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF));
      out << buf;
    }
  } else if(ext == ".pal") {
    out << "JASC-PAL\r\n0100\r\n" << colors.size() << "\r\n";
    for(uint32_t c : colors) {
      std::snprintf(buf, sizeof buf, "%d %d %d\r\n", int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF));
      out << buf;
    }
  } else {
    for(uint32_t c : colors) {
      std::snprintf(buf, sizeof buf, "%02x%02x%02x\n", int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF));
      out << buf;
    }
  }
  return bool(out);
}
#pragma endregion

#pragma region [ Order ]
void Palette::sortByHue() {
  // Grays first by lightness, then colors by OKLCh hue, then lightness
  auto key = [](uint32_t c) {
    Lab l = toOklab(c);
    float chroma = std::sqrt(l.a * l.a + l.b * l.b);
    float hue = chroma < 0.02f ? -1 : std::atan2(l.b, l.a);
    if(hue < 0 && chroma >= 0.02f) hue += 6.2831853f;
    return std::pair<float, float>(hue, l.L);
  };
  std::stable_sort(colors.begin(), colors.end(), [&](uint32_t a, uint32_t b) { return key(a) < key(b); });
}

void Palette::sortByLightness() {
  std::stable_sort(colors.begin(), colors.end(), [](uint32_t a, uint32_t b) { return toOklab(a).L < toOklab(b).L; });
}

void Palette::unique() {
  std::vector<uint32_t> out;
  for(uint32_t c : colors) {
    bool seen = false;
    for(uint32_t o : out) if(((o ^ c) & 0xFFFFFF) == 0) { seen = true; break; }
    if(!seen) out.push_back(c | 0xFF000000u);
  }
  colors = std::move(out);
}

// 12 hues in three shades plus a gray ramp
Palette Palette::defaults() {
  Palette p;
  for(int shade = 0; shade < 3; shade++) {
    for(int h = 0; h < 12; h++) {
      float s = shade == 0 ? 0.45f : 0.9f, v = shade == 2 ? 0.55f : 1.f;
      float hh = h / 2.f;  // sextant 0..6
      int i = int(hh);
      float f = hh - i, p0 = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
      float r, g, b;
      switch(i % 6) {
        case 0: r = v; g = t; b = p0; break;
        case 1: r = q; g = v; b = p0; break;
        case 2: r = p0; g = v; b = t; break;
        case 3: r = p0; g = q; b = v; break;
        case 4: r = t; g = p0; b = v; break;
        default: r = v; g = p0; b = q; break;
      }
      p.colors.push_back(rgb(int(r * 255 + 0.5f), int(g * 255 + 0.5f), int(b * 255 + 0.5f)));
    }
  }
  for(int i = 0; i < 12; i++) { int v = i * 255 / 11; p.colors.push_back(rgb(v, v, v)); }
  return p;
}
#pragma endregion

#pragma region [ Median cut ]
Palette Palette::fromPixels(const std::vector<uint32_t>& px, int n) {
  // Distinct opaque-ish colors with counts
  std::unordered_map<uint32_t, int> counts;
  for(uint32_t c : px) if((c >> 24) >= 128) counts[c & 0xFFFFFF]++;
  struct Entry { uint32_t c; Lab l; int n; };
  std::vector<Entry> all;
  all.reserve(counts.size());
  for(auto& [c, k] : counts) all.push_back({ c | 0xFF000000u, toOklab(c), k });
  Palette p;
  if(all.empty() || n <= 0) return p;
  if(int(all.size()) <= n) {
    for(auto& e : all) p.colors.push_back(e.c);
    p.sortByLightness();
    return p;
  }

  // Boxes are index ranges into `all`; split the box with the widest weighted spread at its weighted median
  struct Box { int b, e; };
  std::vector<Box> boxes{ { 0, int(all.size()) } };
  auto axisOf = [&](const Box& bx, float& spread) {
    float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
    for(int i = bx.b; i < bx.e; i++) {
      float v[3] = { all[i].l.L, all[i].l.a, all[i].l.b };
      for(int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
    }
    int axis = 0;
    spread = -1;
    for(int k = 0; k < 3; k++) if(hi[k] - lo[k] > spread) { spread = hi[k] - lo[k]; axis = k; }
    return axis;
  };
  while(int(boxes.size()) < n) {
    int pick = -1, axis = 0;
    float bestScore = 0;
    for(int i = 0; i < int(boxes.size()); i++) {
      if(boxes[i].e - boxes[i].b < 2) continue;
      float spread;
      int ax = axisOf(boxes[i], spread);
      double weight = 0;
      for(int j = boxes[i].b; j < boxes[i].e; j++) weight += all[j].n;
      float score = float(spread * std::sqrt(weight));
      if(score > bestScore) { bestScore = score; pick = i; axis = ax; }
    }
    if(pick < 0) break;
    Box bx = boxes[pick];
    auto val = [axis](const Entry& e) { return axis == 0 ? e.l.L : axis == 1 ? e.l.a : e.l.b; };
    std::sort(all.begin() + bx.b, all.begin() + bx.e, [&](const Entry& a, const Entry& b) { return val(a) < val(b); });
    long total = 0;
    for(int j = bx.b; j < bx.e; j++) total += all[j].n;
    long acc = 0;
    int mid = bx.b + 1;
    for(int j = bx.b; j < bx.e - 1; j++) {
      acc += all[j].n;
      mid = j + 1;
      if(acc * 2 >= total) break;
    }
    boxes[pick] = { bx.b, mid };
    boxes.push_back({ mid, bx.e });
  }
  // Each box becomes its weighted OKLab mean
  for(auto& bx : boxes) {
    double L = 0, a = 0, b = 0, w = 0;
    for(int j = bx.b; j < bx.e; j++) { L += all[j].l.L * all[j].n; a += all[j].l.a * all[j].n; b += all[j].l.b * all[j].n; w += all[j].n; }
    Lab m{ float(L / w), float(a / w), float(b / w) };
    float r, g, bl;
    oklabToLinear(m, r, g, bl);
    auto to8 = [](float v) { return int(linearToSrgb(std::clamp(v, 0.f, 1.f)) * 255 + 0.5f); };
    p.colors.push_back(rgb(to8(r), to8(g), to8(bl)));
  }
  p.unique();
  p.sortByLightness();
  return p;
}

Palette Palette::fromImage(const Document& doc, int n) {
  std::vector<uint32_t> px(size_t(doc.width()) * doc.height());
  for(int y = 0; y < doc.height(); y++) flattenRow(doc, y, px.data() + size_t(y) * doc.width());
  if(doc.hasSelection()) {
    std::vector<uint32_t> sel;
    for(int y = 0; y < doc.height(); y++)
      for(int x = 0; x < doc.width(); x++)
        if(doc.selection->get(x, y) >= 128) sel.push_back(px[size_t(y) * doc.width() + x]);
    px = std::move(sel);
  }
  return fromPixels(px, n);
}
#pragma endregion

}
