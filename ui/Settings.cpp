#include "ui/Settings.h"
#include "core/io/Png.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <fstream>

using json = nlohmann::json;

namespace david {

static json toJson(const Settings& s) {
  const ToolSettings& t = s.tools;
  json j;
  j["theme"] = s.theme;
  j["systemAccent"] = s.systemAccent;
  j["tools"] = {
    { "brushSize", t.brushSize }, { "antialias", t.antialias }, { "pressureSize", t.pressureSize },
    { "pressureOpacity", t.pressureOpacity }, { "tolerance", t.tolerance }, { "fillGlobal", t.fillGlobal },
    { "sampleImage", t.sampleImage }, { "selectMode", int(t.selectMode) }, { "selectShape", t.selectShape }, { "pickerSize", t.pickerSize },
    { "pickerImage", t.pickerImage }, { "pickerSwitchBack", t.pickerSwitchBack },
    { "selectThenMove", t.selectThenMove },
    { "wandGlobal", t.wandGlobal }, { "classicRgb", t.classicRgb }, { "softEdges", t.softEdges }, { "edgeStop", t.edgeStop },
    { "edgeThreshold", t.edgeThreshold }, { "fillOutline", t.fillOutline },
    { "gradType", t.gradType }, { "gradSpace", t.gradSpace }, { "gradDither", t.gradDither }, { "gradAlpha", t.gradAlpha }, { "gradPixel", t.gradPixel },
    { "dash", t.dash }, { "arrowStart", t.arrowStart }, { "arrowEnd", t.arrowEnd }, { "pixelPerfect", t.pixelPerfect }, { "replaceAlpha", t.replaceAlpha }, { "lineSharp", t.lineSharp },
    { "shapeKind", t.shapeKind }, { "shapeDraw", t.shapeDraw }, { "polySides", t.polySides }, { "cornerRadius", t.cornerRadius },
    { "fontFamily", t.fontFamily }, { "fontSize", t.fontSize }, { "bold", t.bold }, { "italic", t.italic }, { "textAlign", t.textAlign },
    { "fontFavorites", t.fontFavorites }, { "fontRecent", t.fontRecent },
    { "tiled", t.tiled }, { "symmetry", t.symmetry }, { "symX", t.symX }, { "symY", t.symY },
    { "grid", t.grid }, { "gridSnap", t.gridSnap }, { "gridW", t.gridW }, { "gridH", t.gridH }, { "gridX", t.gridX }, { "gridY", t.gridY },
    { "shading", t.shading }, { "sprayDensity", t.sprayDensity },
  };
  std::vector<std::string> pal;
  char hex[8];
  for(uint32_t c : t.palette.colors) { std::snprintf(hex, sizeof hex, "%02x%02x%02x", int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF)); pal.push_back(hex); }
  j["palette"] = pal;
  j["canvas"] = { { "checkerSize", s.checkerSize }, { "gridZoom", s.gridZoom }, { "wheelZooms", s.wheelZooms }, { "historyMb", s.historyMb } };
  j["language"] = s.language;
  j["floatingWindows"] = s.floatingWindows;
  j["floatRects"] = s.floatRects;
  j["colors"] = { { "square", s.colorSquare }, { "advanced", s.colorAdvanced } };
  j["newImage"] = { { "width", s.newW }, { "height", s.newH } };
  j["windows"] = { { "tools", s.showTools }, { "history", s.showHistory }, { "layers", s.showLayers }, { "colors", s.showColors }, { "penProbe", s.showProbe },
                    { "tilePreview", s.showTilePreview }, { "palette", s.showPalette },
                    { "timeline", s.showTimeline }, { "animPreview", s.showAnimPreview } };
  const AnimSettings& a = s.anim;
  j["animation"] = {
    { "onion", a.onion }, { "onionBefore", a.onionBefore }, { "onionAfter", a.onionAfter }, { "onionMerged", a.onionMerged },
    { "onionAbove", a.onionAbove }, { "layerOnion", a.layerOnion }, { "layersBelow", a.layersBelow }, { "layersAbove", a.layersAbove },
    { "hideOtherLayers", a.hideOtherLayers }, { "onionOpacity", a.onionOpacity }, { "onionTint", a.onionTint },
    { "loop", a.loop }, { "playTag", a.playTag }, { "previewZoom", a.previewZoom },
  };
  j["shortcuts"] = s.shortcuts;
  j["mods"] = s.mods;
  j["toolStrip"] = { { "order", s.toolOrder }, { "hidden", s.hiddenTools } };
  j["modWindows"] = s.modWindows;
  return j;
}

// Reads a value only when it's there with the right type, so older or hand-edited files still load
template<class T> static void read(const json& j, const char* key, T& out) {
  auto it = j.find(key);
  if(it == j.end()) return;
  try { out = it->get<T>(); } catch(...) {}
}

bool Settings::load(const std::string& path, std::string& error) {
  std::ifstream in(pathFromUtf8(path));
  if(!in) { error = "no settings file"; return false; }
  json j = json::parse(in, nullptr, false);
  if(j.is_discarded() || !j.is_object()) { error = "bad JSON in " + path; return false; }
  read(j, "theme", theme);
  read(j, "systemAccent", systemAccent);
  read(j, "language", language);
  read(j, "floatingWindows", floatingWindows);
  read(j, "floatRects", floatRects);
  if(auto c = j.find("colors"); c != j.end() && c->is_object()) { read(*c, "square", colorSquare); read(*c, "advanced", colorAdvanced); }
  if(auto t = j.find("tools"); t != j.end() && t->is_object()) {
    ToolSettings& ts = tools;
    int mode = int(ts.selectMode);
    read(*t, "brushSize", ts.brushSize); read(*t, "antialias", ts.antialias); read(*t, "pressureSize", ts.pressureSize);
    read(*t, "pressureOpacity", ts.pressureOpacity); read(*t, "tolerance", ts.tolerance); read(*t, "fillGlobal", ts.fillGlobal);
    read(*t, "sampleImage", ts.sampleImage); read(*t, "selectMode", mode); read(*t, "selectShape", ts.selectShape); read(*t, "pickerSize", ts.pickerSize);
    read(*t, "pickerImage", ts.pickerImage); read(*t, "pickerSwitchBack", ts.pickerSwitchBack);
    read(*t, "selectThenMove", ts.selectThenMove);
    read(*t, "wandGlobal", ts.wandGlobal); read(*t, "classicRgb", ts.classicRgb); read(*t, "softEdges", ts.softEdges);
    read(*t, "edgeStop", ts.edgeStop); read(*t, "edgeThreshold", ts.edgeThreshold); read(*t, "fillOutline", ts.fillOutline);
    read(*t, "gradType", ts.gradType); read(*t, "gradSpace", ts.gradSpace); read(*t, "gradDither", ts.gradDither);
    read(*t, "gradAlpha", ts.gradAlpha); read(*t, "gradPixel", ts.gradPixel);
    read(*t, "dash", ts.dash); read(*t, "arrowStart", ts.arrowStart); read(*t, "arrowEnd", ts.arrowEnd);
    read(*t, "pixelPerfect", ts.pixelPerfect); read(*t, "replaceAlpha", ts.replaceAlpha); read(*t, "lineSharp", ts.lineSharp);
    read(*t, "shapeKind", ts.shapeKind); read(*t, "shapeDraw", ts.shapeDraw); read(*t, "polySides", ts.polySides); read(*t, "cornerRadius", ts.cornerRadius);
    read(*t, "fontFamily", ts.fontFamily); read(*t, "fontSize", ts.fontSize); read(*t, "bold", ts.bold); read(*t, "italic", ts.italic);
    read(*t, "textAlign", ts.textAlign); read(*t, "fontFavorites", ts.fontFavorites); read(*t, "fontRecent", ts.fontRecent);
    read(*t, "tiled", ts.tiled); read(*t, "symmetry", ts.symmetry); read(*t, "symX", ts.symX); read(*t, "symY", ts.symY);
    read(*t, "grid", ts.grid); read(*t, "gridSnap", ts.gridSnap); read(*t, "gridW", ts.gridW); read(*t, "gridH", ts.gridH);
    read(*t, "gridX", ts.gridX); read(*t, "gridY", ts.gridY); read(*t, "shading", ts.shading); read(*t, "sprayDensity", ts.sprayDensity);
    ts.tiled &= 3; ts.symmetry &= 3;
    ts.gridW = std::clamp(ts.gridW, 1, 4096); ts.gridH = std::clamp(ts.gridH, 1, 4096);
    ts.sprayDensity = std::clamp(ts.sprayDensity, 1, 500);
    ts.edgeThreshold = std::clamp(ts.edgeThreshold, 0.01f, 1.f);
    ts.gradType = std::clamp(ts.gradType, 0, 6); ts.gradSpace = std::clamp(ts.gradSpace, 0, 4); ts.gradDither = std::clamp(ts.gradDither, 0, 2);
    ts.dash = std::clamp(ts.dash, 0, 2); ts.shapeKind = std::clamp(ts.shapeKind, 0, 3); ts.shapeDraw = std::clamp(ts.shapeDraw, 0, 2);
    ts.polySides = std::clamp(ts.polySides, 3, 24); ts.cornerRadius = std::clamp(ts.cornerRadius, 0.f, 2000.f);
    ts.fontSize = std::clamp(ts.fontSize, 1.f, 1000.f); ts.textAlign = std::clamp(ts.textAlign, 0, 2);
    ts.selectMode = ECombine(std::clamp(mode, 0, 4));
    ts.brushSize = std::clamp(ts.brushSize, 1, 2000);
    ts.tolerance = std::clamp(ts.tolerance, 0.f, 1.f);
  }
  if(auto c = j.find("canvas"); c != j.end() && c->is_object()) {
    read(*c, "checkerSize", checkerSize); read(*c, "gridZoom", gridZoom); read(*c, "wheelZooms", wheelZooms); read(*c, "historyMb", historyMb);
    checkerSize = std::clamp(checkerSize, 2, 64);
    historyMb = std::clamp(historyMb, 32, 65536);
  }
  if(auto n = j.find("newImage"); n != j.end() && n->is_object()) { read(*n, "width", newW); read(*n, "height", newH); }
  if(auto w = j.find("windows"); w != j.end() && w->is_object()) {
    read(*w, "tools", showTools); read(*w, "history", showHistory); read(*w, "layers", showLayers);
    read(*w, "colors", showColors); read(*w, "penProbe", showProbe);
    read(*w, "tilePreview", showTilePreview); read(*w, "palette", showPalette);
    read(*w, "timeline", showTimeline); read(*w, "animPreview", showAnimPreview);
  }
  if(auto a = j.find("animation"); a != j.end() && a->is_object()) {
    AnimSettings& s = anim;
    read(*a, "onion", s.onion); read(*a, "onionBefore", s.onionBefore); read(*a, "onionAfter", s.onionAfter);
    read(*a, "onionMerged", s.onionMerged); read(*a, "onionAbove", s.onionAbove); read(*a, "layerOnion", s.layerOnion);
    read(*a, "layersBelow", s.layersBelow); read(*a, "layersAbove", s.layersAbove); read(*a, "hideOtherLayers", s.hideOtherLayers);
    read(*a, "onionOpacity", s.onionOpacity); read(*a, "onionTint", s.onionTint);
    read(*a, "loop", s.loop); read(*a, "playTag", s.playTag); read(*a, "previewZoom", s.previewZoom);
    s.onionBefore = std::clamp(s.onionBefore, 0, 8); s.onionAfter = std::clamp(s.onionAfter, 0, 8);
    s.layersBelow = std::clamp(s.layersBelow, 0, 8); s.layersAbove = std::clamp(s.layersAbove, 0, 8);
    s.onionOpacity = std::clamp(s.onionOpacity, 5, 90); s.loop = std::clamp(s.loop, 0, 2); s.previewZoom = std::clamp(s.previewZoom, 0, 16);
  }
  read(j, "shortcuts", shortcuts);
  read(j, "mods", mods);
  if(auto ts = j.find("toolStrip"); ts != j.end() && ts->is_object()) {
    read(*ts, "order", toolOrder);
    read(*ts, "hidden", hiddenTools);
  }
  read(j, "modWindows", modWindows);
  std::vector<std::string> pal;
  read(j, "palette", pal);
  if(!pal.empty()) {
    std::string text, err;
    for(auto& c : pal) text += c + '\n';
    Palette p;
    if(p.parse(text, err)) tools.palette = std::move(p);
  }
  return true;
}

std::string Settings::dump() const { return toJson(*this).dump(2); }

bool Settings::save(const std::string& path, std::string& error) const {
  std::ofstream out(pathFromUtf8(path));
  if(!out) { error = "can't write " + path; return false; }
  out << dump() << '\n';
  return bool(out);
}

}
