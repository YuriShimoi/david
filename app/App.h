#pragma once
#include "app/Cursors.h"
#include "core/Actions.h"
#include "core/Animation.h"
#include "core/io/Anim.h"
#include <map>
#include "core/Color.h"
#include "core/Text.h"
#include "core/Vector.h"
#include "core/Document.h"
#include "core/Effect.h"
#include "core/Paint.h"
#include "core/Raster.h"
#include "render/CanvasRenderer.h"
#include "render/Thumbnails.h"
#include "render/Preview.h"
#include "scripting/Mods.h"
#include "ui/CanvasNav.h"
#include "ui/Commands.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/ThemeStore.h"
#include "ui/tools/Tool.h"
#include <SDL3/SDL.h>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace david {

bool paramWidget(const EffectParam& p, ParamValues& v, float dpi, const std::function<void(const std::string&)>& browse);

class App : public ToolHost {
public:
  bool init(int argc, char** argv);
  void run();
  void shutdown();

  #pragma region [ ToolHost ]
  Document& doc() override { return *_doc; }
  CanvasNav& nav() override { return _nav; }
  ToolSettings& settings() override { return _cfg.tools; }
  const Theme& theme() override { return _theme; }
  uint32_t& color(bool primary) override { return primary ? _primary : _secondary; }
  float dpi() override { return _dpi; }
  ImVec2 toScreen(double ix, double iy) override;
  ImFont* previewFont(const std::string& path, int index) override;
  void switchBack() override { selectTool(_previousTool); }
  void useTool(ETool id) override;
  bool vectorMode() override;
  #pragma endregion

private:
  SDL_Window* _window = nullptr;
  SDL_GLContext _gl = nullptr;
  bool _running = true;
  uint64_t _redrawUntil = 0;
  std::string _title;
  std::string _iniPath;
  bool _freshLayout = false, _focusLayers = false;
  std::string _shotPath;   // --shot <png>: save the window after a few frames and quit (dev checks)
  int _shotFrames = 0;
  std::string _selftestDir; // --selftest <dir>: scripted input through the real event path, report in <dir>
  int _selftestOnly = 0;    // --selftest-round N: only that round (8 or later), for checking new work
  std::string _selftestLog;

  std::unique_ptr<Document> _doc;
  uint64_t _docSeen = 0;
  CanvasRenderer _renderer;
  Thumbnails _thumbs;
  CanvasNav _nav;
  Commands _commands;
  Cursors _cursors;
  void updateCursor();
  Theme _theme;          // what the UI uses: the theme file plus the system accent when that's on
  Theme _themeBase;      // the theme file as loaded
  ThemeStore _themes;
  std::string _themeId, _themeFont, _settingsPath, _settingsSaved;
  int64_t _themeStamp = 0;
  Settings _cfg;
  float _dpi = 1;
  ImFont* _boldFont = nullptr;
  float _iconOffset = 0;   // GlyphOffset.y of the merged icon font, measured so icons center on capital letters
  int _iconCalibration = 0;
  float _iconDiff = 0;     // last measured icon-to-capital center difference
  bool _fontReload = false;

  uint32_t _primary = BLACK, _secondary = WHITE;
  bool _editPrimary = true;
  std::vector<std::unique_ptr<Tool>> _tools;
  int _tool = 0, _previousTool = 0;

  #pragma region [ Canvas input ]
  enum class EGesture { None, Tool, Pan, Axis };
  EGesture _gesture = EGesture::None;
  int _gestureButton = 0;
  int _historySeen = -1;
  bool _spaceDown = false;
  bool _textInput = false;
  // Font previews: loaded between frames, at most a few per frame
  std::map<std::string, ImFont*> _previewFonts;
  std::vector<std::pair<std::string, int>> _previewQueue;
  void loadPreviewFonts();
  float _panX = 0, _panY = 0;
  bool _cursorIn = false;
  ToolInput _hover;
  std::vector<Segment> _outline;
  uint64_t _outlineSerial = 0;
  #pragma endregion

  #pragma region [ Pen ]
  bool _penDown = false;
  float _pressure = 1;
  struct PenProbe { SDL_PenID id = 0; float pressure = 0, tiltX = 0, tiltY = 0; bool down = false, eraser = false; int events = 0, axisEvents = 0; } _probe;
  #pragma endregion

  #pragma region [ Windows ]
  ImGuiID _nodeTools = 0, _nodeHistory = 0, _nodeLayers = 0, _nodeColors = 0;
  int _resetWindow = 0; // 6-8, 11: put that window back in its default dock node this frame
  bool _floatReset = false; // place every panel at its floating spot this frame
  std::unordered_map<int, float> _panelWidth;
  std::unordered_map<int, ImVec4> _panelRect;  // last frame's floating rects (x, y, w, h), for snapping
  float _hue = 0;
  uint32_t _hsvTarget = 0;
  #pragma endregion

  #pragma region [ Settings window ]
  bool _showSettings = false;
  int _settingsPage = 0;
  std::string _previewTheme;
  std::string _captureId;      // command whose new shortcut is being captured
  char _shortcutFilter[64] = {};
  char _themeName[64] = {};
  std::string _notice;
  int _settingsSeen = 0;
  uint64_t _reloadAt = 0;
  int _round2 = 0, _round3 = 0, _round4 = 0, _round5 = 0, _round6 = 0, _round7 = 0, _round8 = 0, _round9 = 0, _round10 = 0;
  int _probeGray = 0;  // a screen value one self-test step compares with the next
  bool _selftestMods = false;  // the self-test turns the example mods on
  #pragma endregion

  #pragma region [ Dialogs ]
  std::string _popup;
  std::function<void()> _pending, _afterSave;
  std::string _error;
  LayerProps _propsBefore;
  char _propsName[128] = {};
  #pragma endregion

  void setupCommands();
  void loadFonts();
  void applyTheme();
  void selectTheme(const std::string& id);
  void refreshTheme();
  bool onTick();
  void syncShortcuts();
  void drawSettings();
  void settingsAppearance();
  void settingsTools();
  void settingsCanvas();
  void settingsBeta();
  void settingsShortcuts();
  void settingsPaths();
  void updateTitle();

  void handleEvent(const SDL_Event& e);
  void canvasEvent(const SDL_Event& e, bool uiHasMouse);
  void pointer(int phase, float wx, float wy, int button, bool pen, int clicks);
  ToolInput makeInput(float wx, float wy, int button, bool pen) const;
  float density() const;
  bool animating() const;

  Tool& tool() { return *_tools[_tool]; }
  void selectTool(int index);
  // The tools in the strip's order: every tool, or only the shown ones
  std::vector<int> stripOrder(bool shownOnly) const;
  bool toolHidden(const Tool& t) const;
  void toolKey(ImGuiKey key);
  void textInput();
  void commitTool();

  void frame();
  void saveShot(int fbW, int fbH);
  void writeShot(const std::string& path);
  void selftestStep(int frame);
  void drawMenu();
  void drawToolBar();
  void drawStatusBar();
  void drawDialogs();
  struct SizeDialog {
    bool canvas = false, percent = false, keep = true, gamma = true;
    float pct = 100;
    int w = 0, h = 0, resUnit = 0, printUnit = 0, resample = 0, anchor = 4, fill = 0;
    double dpi = 96;
  } _size;
  void openSizeDialog(bool canvas);
  void drawUnsavedDialog();
  bool _pendingClosing = false;
  void drawSizeDialog();
  void drawOverlays();
  void buildDefaultLayout(ImGuiID dock);
  bool beginPanel(int which, const char* icon, const char* name, const char* id, bool* open);
  void floatingPlace(int which);
  void snapFloating(int which);
  void calibrateIcons();
  void commandButton(const char* id);
  void setFloating(bool on);
  void drawToolStrip();
  void drawHistoryWindow();
  void drawLayersWindow();
  void drawColorsWindow();
  void drawPenProbe();

  #pragma region [ Pixel art ]
  PreviewTexture _preview;
  float _previewZoom = 0;  // 0 = fit
  int _palSel = -1, _palEdit = -1, _palFromImage = 16;
  int _axisDrag = 0;       // 1: the vertical symmetry axis, 2: the horizontal one
  struct { int grid = 16, border = 2, corner = 1; } _autotile;
  void pixelArtBar();
  void drawGridDialog();
  void drawSymmetry(ImDrawList* dl);
  int axisAt(const ToolInput& in);
  void dragAxis(const ToolInput& in);
  void drawTilePreview();
  void drawPaletteWindow();
  void showPaletteDialog(bool save);
  void paletteFileChosen(const std::string& path, bool save);
  void drawAutotileDialog();
  void runAutotile();
  #pragma endregion

  #pragma region [ Animation ]
  Player _player, _previewPlayer;
  bool _previewPaused = false;
  uint64_t _playLast = 0;
  int _selFrom = -1, _selTo = -1;  // frames selected in the Timeline (-1: just the active frame)
  int _dragFrame = -1;             // the frame header being dragged to move frames
  float _dragX = 0;
  float _tlScrollY = 0;
  int _tagEdit = -1;               // the tag in the tag popup
  int _durationMs = 100;
  ImGuiID _dockId = 0, _nodeTimeline = 0;
  std::map<int, PreviewTexture> _animFrames;  // Animation Preview: a texture per frame
  bool playing() const;
  void tickPlayback();
  void goToFrame(int f);
  void frameOp(const char* what);
  void selectedFrames(int& from, int& to) const;
  const FrameTag* playTag() const;
  void setOnionView();
  void drawTimeline();
  void drawAnimPreview();
  void onionOptions();
  void layerOnionOptions();
  void drawTagPopup();
  // File > Export Animation and Import Sprite Sheet
  struct ExportAnim {
    int format = 0;  // GIF, APNG, WebP, sprite sheet, PNG sequence
    int tag = -1, scale = 1, loops = 0, columns = 0, padding = 0;
    bool json = true;
  } _export;
  struct ImportSheet {
    std::unique_ptr<Document> sheet;
    SliceOptions o;
  } _import;
  void drawAnimDialogs();
  void exportAnimation(const std::string& path);
  // A native file dialog whose answer comes back as a file event (code: EFileDialog in App.cpp)
  void pickFile(int code, bool save, const SDL_DialogFileFilter* filters, int count);
  void sheetChosen(const std::string& path);
  #pragma endregion

  #pragma region [ Effects ]
  const Effect* _fx = nullptr;
  ParamValues _fxValues;
  LiveEdit _fxLive;
  bool _fxAllFrames = false;  // OK applies the effect to every frame (or the Timeline's selected frames)
  std::string _lastEffect;
  std::map<std::string, ParamValues> _fxRemember;
  EffectEnv effectEnv();
  void openEffect(const std::string& id);
  void previewEffect();
  void finishEffect(bool ok);
  void repeatEffect();
  void drawEffectDialog();
  void effectsMenu(const char* top);

  #pragma region [ Mods ]
  ModHost _mods;
  std::string _cmdId;        // the mod command whose dialog is open
  ParamValues _cmdValues;
  void drawModsMenu();
  void settingsMods();
  void runModCommand(const ModCommand& c, const ParamValues& v);
  void drawCommandDialog();
  // Dialogs are the raised color, like their fields: these give the fields the darker background (pop 3 colors)
  void pushFieldColors();
  // Every setting back to the defaults (theme, shortcuts, tools, windows and layout, mods on/off)
  void resetAllSettings();
  void drawResetDialog();
  // Clipboard (app/Clipboard.cpp)
  enum class EPaste : uint8_t { Layer, NewLayer, NewImage };
  Lifted _pasted;
  EPaste _pasteWhere = EPaste::Layer;
  void copyImage(bool merged, bool cut);
  void pasteImage(EPaste where);
  void placePaste(bool grow);
  void drawPasteDialog();
  // Vector layers (app/VectorUi.cpp)
  std::function<void()> _afterRaster;  // what a raster edit on a vector layer was about to do
  bool _rasterRemember = false;
  uint64_t _objectsLayer = 0;          // the layer selectedObjects belong to
  // True when the active layer takes raster edits; on a vector layer asks first (rasterize, or a new layer)
  // and runs then once it can
  bool rasterGuard(std::function<void()> then);
  void rasterizeLayer(int index);
  void drawRasterDialog();
  void vectorFrame();
  void importSvg(const std::string& path, bool newDocument);
  void exportSvg(const std::string& path);
  void traceLayer();
  // The vector tool row on the tool bar, and the Object window (the picked objects' look and place)
  std::unique_ptr<VectorChange> _objectStyle;  // a field edit in progress: live, one step when it lets go
  bool vectorRowShown();
  void vectorToolRow();
  void drawObjectWindow();
  // Mod windows: open ones by "<mod>/<id>" are in _cfg.modWindows; a window whose draw failed shows its error
  std::map<std::string, std::string> _modWindowError;
  bool modWindowOpen(const std::string& key) const;
  void showModWindow(const std::string& key, bool open);
  void drawModWindows();
  void openGuide();
  void showError(const std::string& message);
  #pragma endregion
  std::string _fxFileParam;
  void showEffectFileDialog(const std::string& param);
  #pragma endregion

  void selectionOp(const char* what);
  void layerOp(const char* what);

  void setDocument(std::unique_ptr<Document> doc);
  void openFile(const std::string& path);
  bool saveTo(const std::string& path);
  void save();
  void saveAs();
  void showOpenDialog();
  void confirmDiscard(std::function<void()> action, bool closing = false);
  void requestQuit();
};

}
