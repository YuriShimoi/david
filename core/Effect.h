#pragma once
#include "core/Mask.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace david {

class Document;
class TileRecorder;
struct Palette;

// A plain RGBA8 image (straight alpha, same packing as the tiles)
struct Image {
  int w = 0, h = 0;
  std::vector<uint32_t> px;
  Image() = default;
  Image(int w, int h, uint32_t fill = 0) : w(w), h(h), px(size_t(w) * h, fill) {}
  uint32_t& at(int x, int y) { return px[size_t(y) * w + x]; }
  uint32_t at(int x, int y) const { return px[size_t(y) * w + x]; }
  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
};

enum class EParam : uint8_t { Float, Int, Bool, Choice, Color, Text, Curve };

// One effect parameter. The GUI dialog, Lua and the CLI all read the same list.
struct EffectParam {
  std::string id;
  std::string label;
  EParam type = EParam::Float;
  double min = 0, max = 1, def = 0;
  std::vector<std::string> choices;  // Choice: the names; the value is the index
  std::string text;                  // Text and Curve: default (a curve is "x,y x,y ...", channels split by '|')
  std::string showIf;                // "method=4" or "method=5|6|7": only shown (in dialogs) when that param has one of the values
  bool shown(const struct ParamValues& v) const;
};

// Values by param id. Colors are packed RGBA in the double; text params live in `text`.
struct ParamValues {
  std::map<std::string, double> num;
  std::map<std::string, std::string> text;
  double get(const std::string& id, double fallback = 0) const { auto it = num.find(id); return it == num.end() ? fallback : it->second; }
  int geti(const std::string& id, int fallback = 0) const { return int(get(id, fallback)); }
  uint32_t color(const std::string& id) const { return uint32_t(get(id)); }
  std::string str(const std::string& id) const { auto it = text.find(id); return it == text.end() ? "" : it->second; }
};

inline bool EffectParam::shown(const ParamValues& v) const {
  if(showIf.empty()) return true;
  size_t eq = showIf.find('=');
  int cur = v.geti(showIf.substr(0, eq));
  for(size_t p = eq + 1; p < showIf.size();) {
    size_t bar = showIf.find('|', p);
    if(std::stoi(showIf.substr(p, bar - p)) == cur) return true;
    p = bar == std::string::npos ? showIf.size() : bar + 1;
  }
  return false;
}

// What an effect may read besides its params
struct EffectEnv {
  uint32_t primary = 0xFF000000u, secondary = 0xFFFFFFFFu;
  const Palette* palette = nullptr;
  int originX = 0, originY = 0;  // image position of src(0, 0), for patterns that must line up across runs
  std::string baseDir;           // where relative files (patterns, palettes) are looked up
};

struct Effect {
  std::string id;          // stable name used by the CLI and Lua, e.g. "outline"
  std::string name;        // menu label
  std::string category;    // "Adjustments", "Effects/Blurs", ...
  const char* icon = "";
  std::vector<EffectParam> params;
  // Extra pixels around the selection the effect reads (blur radius, outline thickness)
  std::function<int(const ParamValues&)> margin;
  // dst starts as a copy of src; both are the selection bounds plus the margin
  std::function<void(const Image& src, Image& dst, const ParamValues&, const EffectEnv&)> run;
  bool hidden = false;     // not in the menus (used by CLI/Lua only)
  std::string chord;       // Paint.NET's default shortcut, e.g. "Ctrl+Shift+L"
  std::string owner;       // the mod that registered it (empty = built in)
  bool dim = false;        // dims the window behind its dialog; off keeps the canvas as is to judge the preview
};

// run may throw std::runtime_error (mods); applyEffect and runEffect pass it on
void registerEffect(Effect e);
void unregisterEffects(const std::string& owner);
const std::vector<Effect>& effects();
const Effect* findEffect(const std::string& id);
ParamValues defaultValues(const Effect& e);
// "radius=3,color=#000" (CLI form) onto the defaults; false with an error for unknown params or bad values
bool parseValues(const Effect& e, const std::string& spec, ParamValues& out, std::string& error);

// Runs the effect on the active layer inside the selection (or the whole layer), recording into rec
void applyEffect(Document& doc, TileRecorder& rec, const Effect& e, const ParamValues& v, const EffectEnv& env);
// Runs it on a whole image (CLI, Lua)
Image runEffect(const Effect& e, const Image& src, const ParamValues& v, const EffectEnv& env);

// Runs fn(y0, y1) over [0, h) in parallel bands
void parallelRows(int h, const std::function<void(int y0, int y1)>& fn);
// A 256-entry lookup from curve points "x,y x,y ..." (0-255), monotone cubic between them
std::vector<uint8_t> curveTable(const std::string& points);

// The built-in effects register themselves once
void registerBuiltinEffects();

}
