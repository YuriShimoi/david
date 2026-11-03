#pragma once
#include "ui/tools/Tool.h"
#include <map>
#include <string>
#include <vector>

namespace david {

// Animation playback and the onion views (Piskel's defaults: one frame each way at 20 %, no tint)
struct AnimSettings {
  bool onion = true;            // neighboring frames
  int onionBefore = 1, onionAfter = 1;
  bool onionMerged = false;     // whole frames instead of the active layer
  bool onionAbove = true;
  bool layerOnion = false;      // neighboring layers around the active one
  int layersBelow = 1, layersAbove = 1;
  bool hideOtherLayers = true;  // layers outside the range: hidden (on) or dimmed (off)
  int onionOpacity = 20;        // %, nearest neighbor
  bool onionTint = false;       // before red, after blue
  int loop = 0;                 // ELoop: loop, once, ping-pong
  bool playTag = true;          // playback stays inside the tag under the active frame
  int previewZoom = 0;          // Animation Preview: 0 = fit, else n×
};

// Everything saved in settings.json (config folder). Shortcuts are stored only where they differ from the
// Paint.NET defaults.
struct Settings {
  std::string theme = "dev";
  bool systemAccent = false;
  ToolSettings tools;
  int checkerSize = 8;
  float gridZoom = 8;       // pixel grid shows above this zoom; 0 = never
  // Beta tools: features still in development, off by default so a release behaves as before until turned on
  bool betaVector = false;
  bool vectorPixels = false;    // vector layers drawn as their exported pixels instead of sharp
  int vectorRaster = 0;         // a raster edit on a vector layer: 0 ask, 1 rasterize it, 2 paint on a new layer
  bool wheelZooms = false;  // wheel zooms and Ctrl+wheel scrolls, instead of the Paint.NET way round
  bool floatingWindows = false;  // Paint.NET-style floating tool windows instead of docked ones
  bool colorSquare = false;      // classic square picker instead of the hue wheel
  bool colorAdvanced = false;    // RGB/HSV bands and hex under the picker
  std::string language = "en";
  std::map<std::string, std::vector<float>> floatRects;  // panel id -> x, y, w, h of the floating window
  int historyMb = 512;
  int newW = 800, newH = 600;
  bool showTools = true, showHistory = true, showLayers = true, showColors = true, showProbe = false;
  bool showTilePreview = false, showPalette = false;  // pixel-art windows, hidden by default
  bool showTimeline = false, showAnimPreview = false;  // animation windows, hidden by default
  bool showObject = true;  // vector objects (with vector support on)
  // The tool strip, by tool name: its order (tools not listed follow in their built-in order) and the hidden ones
  std::vector<std::string> toolOrder, hiddenTools;
  AnimSettings anim;
  std::map<std::string, std::vector<std::string>> shortcuts;
  std::map<std::string, bool> mods;
  std::vector<std::string> modWindows;  // open mod windows, "<mod>/<id>"  // mod id -> on/off, where it differs from the default (examples off)

  bool load(const std::string& path, std::string& error);
  bool save(const std::string& path, std::string& error) const;
  std::string dump() const;
};

}
