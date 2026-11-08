#pragma once
#include "core/Palette.h"
#include "core/Raster.h"
#include <string>
#include <imgui.h>
#include <memory>
#include <vector>

namespace david {

class CanvasNav;
class Document;
struct Theme;

enum class ETool : uint8_t {
  RectSelect, Lasso, EllipseSelect, PolyLasso, MovePixels, MoveSelection,
  Zoom, Pan, Bucket, Brush, Eraser, Pencil, Picker,
  MagicWand, Gradient, Line, Shapes, Text, Mouse, Curve, Spray, Node, Mod, COUNT
};

// The pointer over the canvas: system cursors, or the tool's own icon
enum class ECursor : uint8_t { Arrow, Cross, Move, Text, Pointer, Icon };

struct ToolInput {
  double x = 0, y = 0;     // image coordinates
  float sx = 0, sy = 0;    // ImGui screen coordinates
  float pressure = 1;
  int button = 1;          // SDL_BUTTON_LEFT / SDL_BUTTON_RIGHT of the press
  bool ctrl = false, shift = false, alt = false;
  bool pen = false;
  bool doubleClick = false;
};

// Options shared by the tools, shown on the tool bar (saved with the settings in phase 3)
struct ToolSettings {
  int brushSize = 4;
  bool antialias = true;
  bool pressureSize = true;
  bool pressureOpacity = false;
  float tolerance = 0.5f;
  bool fillGlobal = false;
  bool sampleImage = false;
  ECombine selectMode = ECombine::Replace;
  int selectShape = 0;  // rectangle, ellipse, lasso, polygon
  int pickerSize = 1;
  bool pickerImage = false;
  bool pickerSwitchBack = false;
  bool selectThenMove = true;  // finishing a selection switches to Move Selected Pixels
  // Magic wand and bucket
  bool wandGlobal = false;
  bool classicRgb = false;
  bool softEdges = false;
  bool edgeStop = false;
  float edgeThreshold = 0.2f;
  bool fillOutline = false;
  // Gradient
  int gradType = 0;
  int gradSpace = 2;  // OKLab
  bool gradAlpha = false;
  bool gradPixel = false;
  // Line and shapes
  int dash = 0;  // solid, dash, dot
  bool arrowStart = false, arrowEnd = false;
  bool pixelPerfect = false;
  bool replaceAlpha = false;  // brushes and the pencil replace pixels (alpha too) instead of blending over them
  bool lineSharp = false;  // the line's middle point: a curve through it, or a sharp corner
  int shapeKind = 0;
  int shapeDraw = 0;  // outline, fill, both
  int polySides = 5;
  float cornerRadius = 16;
  // Text
  std::string fontFamily;
  std::string fontStyle;
  float fontSize = 32;
  bool bold = false, italic = false;
  int textAlign = 0;
  std::vector<std::string> fontFavorites, fontRecent;
  // Pixel art
  int tiled = 0;        // bit 1: wrap X, bit 2: wrap Y
  int symmetry = 0;     // bit 1: mirror left-right, bit 2: mirror top-bottom
  double symX = -1, symY = -1;  // axis position in image pixels; negative = image center
  bool grid = false, gridSnap = false;
  int gridW = 16, gridH = 16, gridX = 0, gridY = 0;
  bool shading = false;
  int sprayDensity = 30;  // dots per tick
  Palette palette = Palette::defaults();
};

// The stroke space (symmetry, tiled wrap) the settings give for this document
struct StrokeSpace;
StrokeSpace strokeSpace(const ToolSettings& s, const Document& doc);
// A point snapped to the grid lines when grid snap is on
void snapToGrid(const ToolSettings& s, double& x, double& y);

// What a tool can reach in the app
class ToolHost {
public:
  virtual ~ToolHost() = default;
  virtual Document& doc() = 0;
  virtual CanvasNav& nav() = 0;
  virtual ToolSettings& settings() = 0;
  virtual const Theme& theme() = 0;
  virtual uint32_t& color(bool primary) = 0;
  virtual float dpi() = 0;
  virtual ImVec2 toScreen(double ix, double iy) = 0;
  // A UI font made from a font file for previews (the font list); null until it is loaded between frames
  virtual ImFont* previewFont(const std::string& path, int index) = 0;
  virtual void switchBack() = 0;
  virtual void useTool(ETool id) = 0;
  // Vector support is on (Beta tools) and the active layer is a vector layer: drawing tools make objects
  virtual bool vectorMode() = 0;
};

class Tool {
public:
  ETool id;
  const char* icon;
  const char* name;
  ImGuiKey key;

  Tool(ETool id, const char* icon, const char* name, ImGuiKey key) : id(id), icon(icon), name(name), key(key) {}
  virtual ~Tool() = default;

  virtual void down(ToolHost&, const ToolInput&) {}
  virtual void drag(ToolHost&, const ToolInput&) {}
  virtual void up(ToolHost&, const ToolInput&) {}
  // Called every frame while the button is held (spray)
  virtual void tick(ToolHost&) {}
  // A gesture or a pending edit (Move, polygon lasso) is in progress
  virtual bool busy() const { return false; }
  // Finishes a pending edit into History; called before tool switches and other edits
  virtual void commit(ToolHost&) {}
  virtual void cancel(ToolHost& h) { commit(h); }
  // The tool becomes current
  virtual void activate(ToolHost&) {}
  // Enter, Escape, Backspace while this tool is current; true when handled
  virtual bool onKey(ToolHost&, ImGuiKey) { return false; }
  // Called every frame while no text field has focus, for the tool's own plain keys (shape cycle, arrows)
  virtual void keys(ToolHost&) {}
  // Typing goes to the tool (text tool) instead of the shortcuts while this is true
  virtual bool wantsText() const { return false; }
  virtual void onText(ToolHost&, unsigned int) {}
  virtual void overlay(ToolHost&, ImDrawList*, const ToolInput& hover) {}
  virtual void options(ToolHost&) {}
  // Sections the tool adds to the Object window (the Node tool's picked nodes)
  virtual void inspector(ToolHost&) {}
  // Ctrl+Z while the tool has an edit going: true when it took back a step of its own (the Pen's last node)
  virtual bool undoable() const { return false; }
  virtual bool undo(ToolHost&) { return false; }
  // The pointer to show over the canvas at this hover position
  virtual ECursor cursor(ToolHost&, const ToolInput&) { return ECursor::Icon; }
  // Pressing the tool's key again: true when the tool moved to its next variant itself (selection shapes)
  virtual bool cycle(ToolHost&) { return false; }
  // Pasted pixels to float over the active layer at their bounds (Move Selected Pixels); false when unsupported
  virtual bool paste(ToolHost&, Lifted&&) { return false; }
};

std::vector<std::unique_ptr<Tool>> createTools();
// A Lua mod's tool: the Paintbrush's stroke with the mod's ink
struct ModTool;
std::unique_ptr<Tool> makeModTool(const ModTool& def);

// The flood settings for the wand and bucket; global flips contiguous
FloodOptions floodOptions(const ToolSettings& s, bool global, uint32_t outline);
// Shared tool bar options for the wand and bucket
void floodOptionsBar(ToolHost& h, bool& global, bool bucket);
// A round handle; hot when the pointer is over it
void drawNub(ImDrawList* dl, ImVec2 p, float dpi, bool hot = false);
bool overNub(const ToolInput& in, ImVec2 p, float dpi);

// Dashed two-tone line, the same look as the marching ants
void antsLine(ImDrawList* dl, ImVec2 a, ImVec2 b, float phase, float dpi);

}
