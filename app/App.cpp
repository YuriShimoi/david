#include "app/App.h"
#include "core/Pattern.h"
#include "render/ShaderRunner.h"
#include "core/io/Formats.h"
#include "core/io/Png.h"
#include "platform/Accent.h"
#include "platform/Paths.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/tools/VectorTools.h"
#include "ui/Widgets.h"
#include <glad/glad.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <imgui_internal.h>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace david {

static Uint32 s_fileEvent = 0, s_tickEvent = 0;
enum EFileDialog { FILE_OPEN = 1, FILE_SAVE = 2, FILE_PALETTE_LOAD = 3, FILE_PALETTE_SAVE = 4, FILE_EFFECT_PARAM = 5, FILE_EXPORT_ANIM = 6, FILE_IMPORT_SHEET = 7,
                   FILE_IMPORT_SVG = 8, FILE_EXPORT_SVG = 9 };
// Open lists every format that loads; Save As every one that saves
static const SDL_DialogFileFilter OPEN_FILTER[] = { { "Images", "png;david;ora;webp;jpg;jpeg;bmp;tga;gif;qoi" }, { "All files", "*" } };
// With vector support on, SVG opens too
static const SDL_DialogFileFilter OPEN_FILTER_SVG[] = { { "Images", "png;david;ora;webp;jpg;jpeg;bmp;tga;gif;qoi;svg" }, { "All files", "*" } };
static const SDL_DialogFileFilter SVG_FILTER[] = { { "SVG", "svg" } };
static const SDL_DialogFileFilter SAVE_FILTER[] = {
  { "PNG", "png" }, { "David (layers)", "david" }, { "OpenRaster (layers)", "ora" }, { "WebP", "webp" }, { "GIF", "gif" },
  { "JPEG", "jpg;jpeg" }, { "BMP", "bmp" }, { "TGA", "tga" }, { "QOI", "qoi" } };
static const SDL_DialogFileFilter PALETTE_FILTER[] = { { "Palettes", "gpl;pal;hex" } };

static void SDLCALL onFileDialog(void* userdata, const char* const* files, int) {
  SDL_Event ev{};
  ev.type = s_fileEvent;
  ev.user.code = int(intptr_t(userdata));
  ev.user.data1 = files && files[0] ? SDL_strdup(files[0]) : nullptr;
  SDL_PushEvent(&ev);
}

#pragma region [ Init ]
bool App::init(int argc, char** argv) {
  for(int i = 1; i + 1 < argc; i++)
    if(std::string(argv[i]) == "--selftest") { paths::overrideConfigDir(std::string(argv[i + 1]) + "/config"); _selftestMods = true; }
  if(!SDL_Init(SDL_INIT_VIDEO)) { SDL_Log("SDL_Init: %s", SDL_GetError()); return false; }
  s_fileEvent = SDL_RegisterEvents(2);
  s_tickEvent = s_fileEvent + 1;

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

  float scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  if(scale <= 0) scale = 1;
  _window = SDL_CreateWindow("David", int(1280 * scale), int(800 * scale),
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if(!_window) { SDL_Log("window: %s", SDL_GetError()); return false; }
#ifndef _WIN32
  {
    std::string err;
    if(auto icon = png::load(paths::exeDir() + "assets/icons/david-256.png", err)) {
      Layer& l = icon->active();
      std::vector<uint32_t> px(size_t(l.width()) * l.height());
      for(int y = 0; y < l.height(); y++) for(int x = 0; x < l.width(); x++) px[size_t(y) * l.width() + x] = l.get(x, y);
      if(SDL_Surface* s = SDL_CreateSurfaceFrom(l.width(), l.height(), SDL_PIXELFORMAT_RGBA32, px.data(), l.width() * 4)) {
        SDL_SetWindowIcon(_window, s);
        SDL_DestroySurface(s);
      }
    }
  }
#endif
  SDL_SetWindowPosition(_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  _gl = SDL_GL_CreateContext(_window);
  if(!_gl) { SDL_Log("GL: %s", SDL_GetError()); return false; }
  SDL_GL_MakeCurrent(_window, _gl);
  SDL_GL_SetSwapInterval(1);
  if(!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) { SDL_Log("glad failed"); return false; }
  if(!_renderer.init()) { SDL_Log("renderer init failed"); return false; }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  // The system cursor stays as the user set it: ImGui never swaps it (resize arrows, text beam, hidden)
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
  io.ConfigInputTextCursorBlink = false;
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  // Versioned name: a new default layout (Colors over Layers + History, tools as a strip) starts fresh
  _iniPath = paths::configDir() + "layout-2.ini";
  _freshLayout = !std::filesystem::exists(pathFromUtf8(_iniPath));
  io.IniFilename = _iniPath.c_str();
  ImGui_ImplSDL3_InitForOpenGL(_window, _gl);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  std::string err;
  _settingsPath = paths::configDir() + "settings.json";
  // Dither patterns: the bundled ones, then the user's (same id replaces)
  std::vector<std::string> patternErrors;
  loadPatternDirs({ paths::exeDir() + "patterns", paths::configDir() + "patterns" }, patternErrors);
  for(auto& e : patternErrors) std::fprintf(stderr, "pattern: %s\n", e.c_str());
  _cfg.load(_settingsPath, err);
  _settingsSaved = _cfg.dump();
  if(!loadLanguage(paths::exeDir() + "lang/", _cfg.language, err)) _cfg.language = "en";
  _themes.init(paths::exeDir() + "themes/", paths::configDir() + "themes/");
  selectTheme(_cfg.theme);
  // Mods load before the tools, so their tools join the strip
  _mods.ensureGl = [] { return true; };
  _mods.showWindow = [this](const std::string& key, bool open) { showModWindow(key, open); };
  _mods.scan({ paths::exeDir() + "mods", paths::configDir() + "mods" });
  if(_selftestMods) for(auto& m : _mods.mods()) if(m.example) _cfg.mods[m.id] = true;
  _mods.apply(_cfg.mods);
  _tools = createTools();
  for(auto& t : _mods.tools()) _tools.push_back(makeModTool(t));
  // The program starts on the Mouse tool, so a first click never paints
  for(int i = 0; i < int(_tools.size()); i++) if(_tools[i]->id == ETool::Mouse) _tool = _previousTool = i;
  setupCommands();
  for(auto& [id, chords] : _cfg.shortcuts) {
    if(Command* c = _commands.find(id)) {
      c->chords.clear();
      for(auto& ch : chords) if(ImGuiKeyChord k = Commands::parseChord(ch)) c->chords.push_back(k);
    }
  }
  // A 1 s tick checks the theme file for edits (hot reload) and saves changed settings
  SDL_AddTimer(1000, [](void*, SDL_TimerID, Uint32 interval) -> Uint32 {
    SDL_Event e{};
    e.type = s_tickEvent;
    SDL_PushEvent(&e);
    return interval;
  }, nullptr);

  setDocument(Document::blank(_cfg.newW, _cfg.newH, WHITE));
  bool dev = false;
  for(int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if(arg == "--dev") dev = true;
    else if(arg == "--shot" && i + 1 < argc) _shotPath = argv[++i];
    else if(arg == "--selftest" && i + 1 < argc) { _selftestDir = argv[++i]; _shotPath = _selftestDir + "/selftest.png"; }
    else if(arg == "--selftest-round" && i + 1 < argc) _selftestOnly = std::atoi(argv[++i]);
    else openFile(arg);
  }
  if(dev || !_shotPath.empty()) {
    // Dev check: put the window on a secondary display when there is one, out of the way
    int n = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&n);
    for(int i = 0; i < n; i++) {
      if(displays[i] == SDL_GetPrimaryDisplay()) continue;
      SDL_Rect r;
      SDL_GetDisplayUsableBounds(displays[i], &r);
      SDL_SetWindowPosition(_window, r.x + 40, r.y + 40);
      break;
    }
    SDL_free(displays);
    // Dev runs start from the default layout and don't touch the user's saved one
    if(!_shotPath.empty()) { io.IniFilename = nullptr; _freshLayout = true; }
    // The real pointer may rest over a panel; scripted input must reach the canvas
    if(!_selftestDir.empty()) io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
  } else {
    SDL_MaximizeWindow(_window);
  }
  SDL_ShowWindow(_window);
  return true;
}

void App::loadFonts() {
  ImGuiIO& io = ImGui::GetIO();
  io.Fonts->Clear();
  _previewFonts.clear();
  std::string fa = paths::exeDir() + "assets/fonts/fa-solid-900.ttf";
  // The tools' Tabler / Phosphor icons, at U+EA00..
  std::string own = paths::exeDir() + "assets/fonts/david-icons.ttf";
  // Some UI fonts (DejaVu) have their own glyphs in the private-use area, which would hide the icons
  static const ImWchar noPrivateUse[] = { 0xE000, 0xF8FF, 0 };
  auto addWithIcons = [&](const std::string& path) {
    ImFontConfig base;
    base.GlyphExcludeRanges = noPrivateUse;
    ImFont* f = path.empty() ? io.Fonts->AddFontDefault() : io.Fonts->AddFontFromFileTTF(path.c_str(), _theme.fontSize, &base);
    if(!f) f = io.Fonts->AddFontDefault();
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = _theme.fontSize * 1.1f; // icons line up in menus
    cfg.GlyphOffset.y = _iconOffset;
    io.Fonts->AddFontFromFileTTF(fa.c_str(), _theme.fontSize * 0.9f, &cfg);
    io.Fonts->AddFontFromFileTTF(own.c_str(), _theme.fontSize, &cfg);
    return f;
  };
  addWithIcons(_theme.font.empty() ? paths::uiFont(false) : _theme.font);
  _boldFont = addWithIcons(paths::uiFont(true));
}

void App::selectTheme(const std::string& requested) {
  // "light" was the Classic theme's old id
  std::string id = requested == "light" ? "classic" : requested;
  std::string err;
  if(!_themes.load(id, _themeBase, err)) {
    if(id != "dev" && _themes.find("dev")) { selectTheme("dev"); return; }
    _themeBase = Theme::dev();
  }
  _themeId = _cfg.theme = _themes.find(id) ? id : "dev";
  _themeStamp = _themes.stamp(_themeId);
  refreshTheme();
}

// Rebuilds the effective theme (system accent on top) and reloads fonts only when the font changed
void App::refreshTheme() {
  _theme = _themeBase;
  uint32_t accent;
  if(_cfg.systemAccent && platform::systemAccent(accent)) {
    ImVec4 a = { (accent & 0xFF) / 255.f, ((accent >> 8) & 0xFF) / 255.f, ((accent >> 16) & 0xFF) / 255.f, 1 };
    _theme.accent = _theme.nub = a;
    _theme.grid = { a.x, a.y, a.z, _theme.grid.w };
    _theme.accentHover = { a.x + (1 - a.x) * 0.15f, a.y + (1 - a.y) * 0.15f, a.z + (1 - a.z) * 0.15f, 1 };
    ImVec4 white = hexToColor("#FFFFFF"), dark = hexToColor("#1A1A1A");
    _theme.onAccent = contrastRatio(white, a) >= contrastRatio(dark, a) ? white : dark;
  }
  if(_theme.font != _themeFont || !_boldFont) { _themeFont = _theme.font; _iconCalibration = 0; loadFonts(); }
  applyTheme();
}

void App::syncShortcuts() {
  _cfg.shortcuts.clear();
  for(auto& c : _commands.list()) {
    if(c.chords == c.defaults) continue;
    auto& list = _cfg.shortcuts[c.id];
    for(ImGuiKeyChord k : c.chords) list.push_back(Commands::chordToString(k));
  }
}

// Returns true when something visible changed and a frame is needed
bool App::onTick() {
  bool changed = false;
  int64_t stamp = _themes.stamp(_themeId);
  if(stamp != _themeStamp) {
    _themeStamp = stamp;
    std::string err;
    Theme t;
    if(_themes.load(_themeId, t, err)) { _themeBase = t; refreshTheme(); _themes.scan(); changed = true; }
  }
  syncShortcuts();
  // Hot reload of mods whose files changed; not while a dialog holds one of their effects or commands
  if(!_fx && _cmdId.empty() && _mods.poll()) { changed = true; _modWindowError.clear(); }
  std::string now = _cfg.dump();
  if(now != _settingsSaved) {
    std::string err;
    if(_shotPath.empty() && _cfg.save(_settingsPath, err)) _settingsSaved = now;
  }
  return changed;
}

void App::applyTheme() {
  _dpi = SDL_GetWindowDisplayScale(_window);
  if(_dpi <= 0) _dpi = 1;
  _theme.apply(ImGui::GetStyle(), _dpi);
  ui::setAccent(_theme.accent, _theme.accentHover, _theme.onAccent);
}

// Switching to floating undocks every panel to its Paint.NET corner; switching back rebuilds the docked layout
void App::setFloating(bool on) {
  _cfg.floatingWindows = on;
  if(on) _floatReset = true;
  else _freshLayout = true;
}

void App::setupCommands() {
  _commands.beforeEdit = [this] { commitTool(); };
  auto hasUndo = [this] { return _doc->history.canUndo() || tool().undoable() || tool().busy(); };
  auto hasRedo = [this] { return _doc->history.canRedo(); };
  auto hasSel = [this] { return _doc->hasSelection(); };
  auto add = [this](Command c) { _commands.add(std::move(c)); };
  auto view = [this](Command c) { c.view = true; _commands.add(std::move(c)); };
  const int CTRL = ImGuiMod_Ctrl, SHIFT = ImGuiMod_Shift, ALT = ImGuiMod_Alt;
  view({ "settings.open", N_("Settings"), ICON_FA_GEAR, { ALT | ImGuiKey_X }, [this] { _showSettings = !_showSettings; } });

  add({ "file.new", N_("New"), ICON_FA_FILE, { CTRL | ImGuiKey_N }, [this] {
    _newFromClipboard = clipboardImageSize(_newW, _newH);
    if(!_newFromClipboard) { _newW = _cfg.newW; _newH = _cfg.newH; }
    _popup = "###new";
  } });
  add({ "file.open", N_("Open"), ICON_FA_FOLDER_OPEN, { CTRL | ImGuiKey_O }, [this] { confirmDiscard([this] { showOpenDialog(); }); } });
  add({ "file.save", N_("Save"), ICON_FA_FLOPPY_DISK, { CTRL | ImGuiKey_S }, [this] { save(); } });
  add({ "file.saveAs", N_("Save As"), ICON_FA_FILE_EXPORT, { CTRL | SHIFT | ImGuiKey_S }, [this] { saveAs(); } });
  add({ "file.exit", N_("Exit"), ICON_FA_RIGHT_FROM_BRACKET, { CTRL | ImGuiKey_Q }, [this] { requestQuit(); } });

  add({ "edit.undo", N_("Undo"), ICON_FA_ROTATE_LEFT, { CTRL | ImGuiKey_Z }, [this] {
    // The Pen's last node goes before the finished edits; anything else pending is finished first
    if(tool().undo(*this)) return;
    commitTool();
    _doc->history.undo(*_doc);
  }, hasUndo, true, true });
  add({ "edit.redo", N_("Redo"), ICON_FA_ROTATE_RIGHT, { CTRL | ImGuiKey_Y, CTRL | SHIFT | ImGuiKey_Z }, [this] { _doc->history.redo(*_doc); }, hasRedo, true });
  add({ "edit.cut", N_("Cut"), ICON_FA_SCISSORS, { CTRL | ImGuiKey_X }, [this] { copyImage(false, true); }, hasSel });
  add({ "edit.copy", N_("Copy"), ICON_FA_COPY, { CTRL | ImGuiKey_C }, [this] { copyImage(false, false); } });
  add({ "edit.copyMerged", N_("Copy Merged"), ICON_FA_LAYER_GROUP, { CTRL | SHIFT | ImGuiKey_C }, [this] { copyImage(true, false); } });
  add({ "edit.paste", N_("Paste"), ICON_FA_PASTE, { CTRL | ImGuiKey_V }, [this] { pasteImage(EPaste::Layer); } });
  add({ "edit.pasteLayer", N_("Paste into New Layer"), ICON_FA_PASTE, { CTRL | SHIFT | ImGuiKey_V }, [this] { pasteImage(EPaste::NewLayer); } });
  add({ "edit.pasteImage", N_("Paste into New Image"), ICON_FA_PASTE, { CTRL | ALT | ImGuiKey_V }, [this] { pasteImage(EPaste::NewImage); } });
  add({ "edit.selectAll", N_("Select All"), ICON_FA_OBJECT_UNGROUP, { CTRL | ImGuiKey_A }, [this] { selectionOp("all"); } });
  add({ "edit.deselect", N_("Deselect"), ICON_FA_XMARK, { CTRL | ImGuiKey_D }, [this] { selectionOp("none"); }, [this] { return _doc->hasSelection() || (vectorMode() && !_doc->selectedObjects.empty()); } });
  add({ "edit.invertSel", N_("Invert Selection"), ICON_FA_CIRCLE_HALF_STROKE, { CTRL | ImGuiKey_I }, [this] { selectionOp("invert"); } });
  add({ "edit.erase", N_("Erase Selection"), ICON_FA_ERASER, { ImGuiKey_Delete }, [this] { selectionOp("erase"); }, hasSel });
  add({ "edit.fill", N_("Fill Selection"), ICON_FA_FILL_DRIP, {}, [this] { selectionOp("fill"); }, hasSel });

  view({ "view.zoomIn", N_("Zoom In"), ICON_FA_MAGNIFYING_GLASS_PLUS, { CTRL | ImGuiKey_Equal, CTRL | ImGuiKey_KeypadAdd }, [this] { _nav.zoomStepCenter(1); }, nullptr, true });
  view({ "view.zoomOut", N_("Zoom Out"), ICON_FA_MAGNIFYING_GLASS_MINUS, { CTRL | ImGuiKey_Minus, CTRL | ImGuiKey_KeypadSubtract }, [this] { _nav.zoomStepCenter(-1); }, nullptr, true });
  view({ "view.fit", N_("Zoom to Window"), ICON_FA_EXPAND, { CTRL | ImGuiKey_B }, [this] { _nav.fit(); } });
  view({ "view.reset", N_("Reset View"), ICON_FA_ARROWS_TO_CIRCLE, { CTRL | SHIFT | ImGuiKey_B }, [this] { _nav.fit(); } });
  view({ "view.actual", N_("Actual Size"), ICON_FA_MAGNIFYING_GLASS, { CTRL | ImGuiKey_0, CTRL | SHIFT | ImGuiKey_A }, [this] { _nav.actualSize(); } });
  view({ "view.history", N_("History"), ICON_FA_CLOCK_ROTATE_LEFT, { ImGuiKey_F6 }, [this] { _cfg.showHistory = !_cfg.showHistory; } });
  view({ "view.layers", N_("Layers"), ICON_FA_LAYER_GROUP, { ImGuiKey_F7 }, [this] { _cfg.showLayers = !_cfg.showLayers; } });
  view({ "view.colors", N_("Colors"), ICON_FA_PALETTE, { ImGuiKey_F8 }, [this] { _cfg.showColors = !_cfg.showColors; } });
  for(int i = 6; i <= 8; i++) {
    bool* shown[] = { nullptr, &_cfg.showHistory, &_cfg.showLayers, &_cfg.showColors };
    const char* names[] = { "", N_("Reset History Window"), N_("Reset Layers Window"), N_("Reset Colors Window") };
    bool* flag = shown[i - 5];
    view({ "view.reset" + std::to_string(i), names[i - 5], ICON_FA_ARROWS_TO_DOT, { CTRL | SHIFT | (ImGuiKey_F1 + i - 1) }, [this, i, flag] { *flag = true; _resetWindow = i; } });
  }
  view({ "view.probe", N_("Pen Probe"), ICON_FA_PEN_NIB, {}, [this] { _cfg.showProbe = !_cfg.showProbe; } });
  // Vector objects
  view({ "view.object", N_("Object"), ICON_TB_VECTOR, {}, [this] { _cfg.showObject = !_cfg.showObject; } });
  view({ "view.reset13", N_("Reset Object Window"), ICON_FA_ARROWS_TO_DOT, {}, [this] { _cfg.showObject = true; _resetWindow = 13; } });
  for(int i = 0; i < int(EObjOp::COUNT); i++) {
    EObjOp op = EObjOp(i);
    const ObjOpInfo& info = objectOpInfo(op);
    std::vector<ImGuiKeyChord> keys;
    if(op == EObjOp::Group) keys = { CTRL | ImGuiKey_G };
    if(op == EObjOp::Ungroup) keys = { CTRL | SHIFT | ImGuiKey_G };
    add({ objectOpId(op), info.name, info.icon, keys, [this, op] { runObjectOp(*this, op); }, [this, op] { return objectOpReady(*this, op); } });
  }
  view({ "view.tilePreview", N_("Tile Preview"), ICON_FA_TABLE_CELLS_LARGE, {}, [this] { _cfg.showTilePreview = !_cfg.showTilePreview; } });
  view({ "view.palette", N_("Palette"), ICON_FA_SWATCHBOOK, {}, [this] { _cfg.showPalette = !_cfg.showPalette; } });
  // Animation
  view({ "view.timeline", N_("Timeline"), ICON_TB_MOVIE, { ImGuiKey_F9 }, [this] { _cfg.showTimeline = !_cfg.showTimeline; } });
  view({ "view.reset11", N_("Reset Timeline Window"), ICON_FA_ARROWS_TO_DOT, { CTRL | SHIFT | ImGuiKey_F9 }, [this] { _cfg.showTimeline = true; _resetWindow = 11; } });
  view({ "view.animPreview", N_("Animation Preview"), ICON_TB_KEYFRAMES, {}, [this] { _cfg.showAnimPreview = !_cfg.showAnimPreview; } });
  view({ "view.onion", N_("Onion Skin"), ICON_TB_GHOST, { ALT | ImGuiKey_O }, [this] { _cfg.anim.onion = !_cfg.anim.onion; } });
  view({ "view.layerOnion", N_("Layer Onion"), ICON_TB_GHOST_2, { ALT | ImGuiKey_L }, [this] { _cfg.anim.layerOnion = !_cfg.anim.layerOnion; } });
  auto animated = [this] { return _doc->frameCount() > 1; };
  // , and . belong to the Line and Curve tools' arrow heads while one of them is the tool (Paint.NET)
  auto stepping = [this] { return _doc->frameCount() > 1 && tool().id != ETool::Line && tool().id != ETool::Curve; };
  view({ "anim.play", N_("Play / Pause"), ICON_TB_PLAYER_PLAY, {}, [this] { frameOp("play"); }, animated });
  view({ "anim.prev", N_("Previous Frame"), ICON_TB_PLAYER_TRACK_PREV, { ImGuiKey_Comma }, [this] { frameOp("prev"); }, stepping, true });
  view({ "anim.next", N_("Next Frame"), ICON_TB_PLAYER_TRACK_NEXT, { ImGuiKey_Period }, [this] { frameOp("next"); }, stepping, true });
  view({ "anim.first", N_("First Frame"), ICON_TB_PLAYER_SKIP_BACK, { ImGuiKey_Home }, [this] { frameOp("first"); }, animated });
  view({ "anim.last", N_("Last Frame"), ICON_TB_PLAYER_SKIP_FORWARD, { ImGuiKey_End }, [this] { frameOp("last"); }, animated });
  add({ "anim.new", N_("New Frame"), ICON_TB_SQUARE_PLUS, { ALT | ImGuiKey_N }, [this] { frameOp("new"); } });
  add({ "anim.duplicate", N_("Duplicate Frame"), ICON_TB_COPY, { ALT | ImGuiKey_D }, [this] { frameOp("duplicate"); } });
  add({ "anim.linked", N_("New Linked Frame"), ICON_TB_LINK, {}, [this] { frameOp("linked"); } });
  add({ "anim.delete", N_("Delete Frames"), ICON_TB_TRASH, {}, [this] { frameOp("delete"); }, animated });
  add({ "anim.reverse", N_("Reverse Frames"), ICON_TB_SWITCH_HORIZONTAL, {}, [this] { frameOp("reverse"); }, [this] { int a, b; selectedFrames(a, b); return b > a; } });
  add({ "anim.link", N_("Link Cels"), ICON_TB_LINK, {}, [this] { frameOp("link"); }, [this] { int a, b; selectedFrames(a, b); return b > a; } });
  add({ "anim.unlink", N_("Unlink Cel"), ICON_TB_UNLINK, {}, [this] { frameOp("unlink"); }, [this] {
    const Layer& l = _doc->active();
    for(int f = 0; f < l.frames(); f++) if(l.linked(f, _doc->activeFrame)) return true;
    return false;
  } });
  add({ "anim.clearCel", N_("Clear Cel"), ICON_TB_TRASH, {}, [this] { frameOp("clearCel"); }, [this] { return bool(_doc->active().cel(_doc->activeFrame)); } });
  add({ "anim.tag", N_("New Tag"), ICON_TB_TAG, {}, [this] { frameOp("tag"); } });
  add({ "anim.layersToFrames", N_("Layers to Frames"), ICON_TB_MOVIE, {}, [this] { commitTool(); layersToFrames(*_doc, ICON_TB_MOVIE); }, [this] { return _doc->layers.size() > 1; } });
  add({ "anim.framesToLayers", N_("Frames to Layers"), ICON_FA_LAYER_GROUP, {}, [this] { commitTool(); framesToLayers(*_doc, ICON_FA_LAYER_GROUP); }, animated });
  view({ "file.exportAnim", N_("Export Animation..."), ICON_TB_TABLE_EXPORT, {}, [this] { _popup = "###exportanim"; } });
  view({ "file.importSheet", N_("Import Sprite Sheet..."), ICON_TB_TABLE_IMPORT, {}, [this] {
    static const SDL_DialogFileFilter f[] = { { "Images", "png;webp;gif;bmp;tga;qoi;jpg;jpeg" } };
    SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_IMPORT_SHEET)), _window, f, 1, nullptr, false);
  } });
  view({ "view.tiled", N_("Tiled Mode"), ICON_FA_TABLE_CELLS_LARGE, {}, [this] { _cfg.tools.tiled = _cfg.tools.tiled ? 0 : 3; } });
  view({ "view.symmetry", N_("Symmetry"), ICON_FA_ARROWS_LEFT_RIGHT_TO_LINE, {}, [this] { _cfg.tools.symmetry = (_cfg.tools.symmetry + 1) % 4; } });
  view({ "view.grid", N_("Grid"), ICON_FA_TABLE_CELLS, { CTRL | ImGuiKey_Apostrophe }, [this] { _cfg.tools.grid = !_cfg.tools.grid; } });
  view({ "view.gridSnap", N_("Snap to Grid"), ICON_FA_MAGNET, {}, [this] { _cfg.tools.gridSnap = !_cfg.tools.gridSnap; } });
  view({ "view.gridSettings", N_("Grid Settings"), ICON_FA_TABLE_CELLS, {}, [this] { _popup = "###grid"; } });
  add({ "image.autotile", N_("Autotile"), ICON_FA_PUZZLE_PIECE, {}, [this] { _popup = "###autotile"; } });
  add({ "effect.repeat", N_("Repeat Last Effect"), ICON_FA_ROTATE_RIGHT, { CTRL | ImGuiKey_F }, [this] { repeatEffect(); }, [this] { return !_lastEffect.empty(); } });
  for(auto& e : effects()) {
    std::vector<ImGuiKeyChord> chords;
    if(ImGuiKeyChord c = e.chord.empty() ? 0 : Commands::parseChord(e.chord)) chords.push_back(c);
    add({ "effect." + e.id, e.name, e.icon, chords, [this, id = e.id] { openEffect(id); } });
  }

  add({ "layer.add", N_("Add New Layer"), ICON_FA_SQUARE_PLUS, { CTRL | SHIFT | ImGuiKey_N }, [this] { layerOp("add"); } });
  add({ "layer.delete", N_("Delete Layer"), ICON_FA_TRASH, { CTRL | SHIFT | ImGuiKey_Delete }, [this] { layerOp("delete"); }, [this] { return _doc->layers.size() > 1; } });
  add({ "layer.duplicate", N_("Duplicate Layer"), ICON_FA_CLONE, { CTRL | SHIFT | ImGuiKey_D }, [this] { layerOp("duplicate"); } });
  add({ "layer.merge", N_("Merge Layer Down"), ICON_FA_ANGLES_DOWN, { CTRL | ImGuiKey_M }, [this] { layerOp("merge"); }, [this] { return _doc->activeLayer > 0; } });
  add({ "layer.up", N_("Move Layer Up"), ICON_FA_ARROW_UP, { ALT | ImGuiKey_PageUp }, [this] { layerOp("up"); }, [this] { return _doc->activeLayer < int(_doc->layers.size()) - 1; } });
  add({ "layer.down", N_("Move Layer Down"), ICON_FA_ARROW_DOWN, { ALT | ImGuiKey_PageDown }, [this] { layerOp("down"); }, [this] { return _doc->activeLayer > 0; } });
  add({ "layer.props", N_("Layer Properties"), ICON_FA_SLIDERS, { ImGuiKey_F4 }, [this] { layerOp("props"); } });
  // Vector layers (Settings > Beta tools > Vector support)
  auto beta = [this] { return _cfg.betaVector; };
  add({ "layer.addVector", N_("Add New Vector Layer"), ICON_FA_BEZIER_CURVE, { CTRL | ALT | ImGuiKey_N }, [this] { layerOp("addVector"); }, beta });
  add({ "layer.rasterize", N_("Rasterize Layer"), ICON_FA_IMAGE, {}, [this] { rasterizeLayer(_doc->activeLayer); }, [this] { return _doc->active().kind == ELayerKind::Vector; } });
  view({ "view.vectorPixels", N_("Pixel Preview"), ICON_FA_CHESS_BOARD, {}, [this] { _cfg.vectorPixels = !_cfg.vectorPixels; }, beta });
  add({ "file.importSvg", N_("Import SVG..."), ICON_FA_FILE_IMPORT, {}, [this] {
    SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_IMPORT_SVG)), _window, SVG_FILTER, 1, nullptr, false);
  }, beta });
  add({ "file.exportSvg", N_("Export SVG..."), ICON_FA_FILE_EXPORT, {}, [this] {
    SDL_ShowSaveFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_EXPORT_SVG)), _window, SVG_FILTER, 1, nullptr);
  }, beta });
  add({ "image.trace", N_("Trace Pixels"), ICON_FA_WAND_MAGIC_SPARKLES, {}, [this] { traceLayer(); }, beta });

  // Single-key tool and color shortcuts (Paint.NET); a key shared by several tools cycles through them
  std::vector<ImGuiKey> toolKeys;
  for(auto& t : _tools) if(t->key != ImGuiKey_None && std::find(toolKeys.begin(), toolKeys.end(), t->key) == toolKeys.end()) toolKeys.push_back(t->key);
  for(ImGuiKey key : toolKeys) {
    // Named after the tools sharing the key
    std::string label;
    const char* icon = "";
    for(auto& t : _tools) if(t->key == key) { label += (label.empty() ? "" : " / ") + std::string(t->name); if(!*icon) icon = t->icon; }
    add({ std::string("tool.") + ImGui::GetKeyName(key), label, icon, { key }, [this, key] { toolKey(key); } });
  }
  view({ "color.swap", N_("Swap Colors"), ICON_FA_RIGHT_LEFT, { ImGuiKey_X }, [this] { std::swap(_primary, _secondary); } });
  view({ "brush.smaller", N_("Brush Width -1"), ICON_FA_CIRCLE_MINUS, { ImGuiKey_LeftBracket }, [this] { _cfg.tools.brushSize = std::max(1, _cfg.tools.brushSize - 1); }, nullptr, true });
  view({ "brush.bigger", N_("Brush Width +1"), ICON_FA_CIRCLE_PLUS, { ImGuiKey_RightBracket }, [this] { _cfg.tools.brushSize = std::min(2000, _cfg.tools.brushSize + 1); }, nullptr, true });
  view({ "brush.smaller5", N_("Brush Width -5"), ICON_FA_CIRCLE_MINUS, { CTRL | ImGuiKey_LeftBracket }, [this] { _cfg.tools.brushSize = std::max(1, _cfg.tools.brushSize - 5); }, nullptr, true });
  view({ "brush.bigger5", N_("Brush Width +5"), ICON_FA_CIRCLE_PLUS, { CTRL | ImGuiKey_RightBracket }, [this] { _cfg.tools.brushSize = std::min(2000, _cfg.tools.brushSize + 5); }, nullptr, true });
  add({ "image.crop", N_("Crop to Selection"), ICON_FA_CROP_SIMPLE, { CTRL | SHIFT | ImGuiKey_X }, [this] { cropToSelection(); }, hasSel });
  add({ "image.resize", N_("Resize Image"), ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER, { CTRL | ImGuiKey_R }, [this] { openSizeDialog(false); } });
  add({ "image.canvas", N_("Canvas Size"), ICON_FA_EXPAND, { CTRL | SHIFT | ImGuiKey_R }, [this] { openSizeDialog(true); } });
  add({ "help.about", N_("About"), ICON_DAVID_LOGO, {}, [this] { _popup = "###about"; } });
}
#pragma endregion

#pragma region [ Loop ]
void App::run() {
  while(_running) {
    // Sleep until something happens; keep drawing briefly after input so hover states and tooltips settle.
    // Marching ants and tool previews tick at ~12 fps.
    if(SDL_GetTicks() > _redrawUntil && _shotPath.empty()) {
      // Playback needs every frame on time (up to 60 fps); the ants and previews are fine at ~12
      if(playing()) SDL_WaitEventTimeout(nullptr, 4);
      else if(animating()) SDL_WaitEventTimeout(nullptr, 80);
      else SDL_WaitEvent(nullptr);
    }
    SDL_Event e;
    bool active = SDL_GetTicks() <= _redrawUntil || animating() || !_shotPath.empty();
    while(SDL_PollEvent(&e)) {
      // The tick alone doesn't redraw unless it changed something
      if(e.type == s_tickEvent) { if(!onTick()) continue; }
      else handleEvent(e);
      _redrawUntil = SDL_GetTicks() + 700;
      active = true;
    }
    if(active) frame();
  }
}

bool App::animating() const {
  return _doc->hasSelection() || _tools[_tool]->busy() || _cfg.showProbe || playing();
}

void App::shutdown() {
  std::string err;
  syncShortcuts();
  if(_shotPath.empty()) _cfg.save(_settingsPath, err);
  // Mod tools hold Lua functions: they go before _mods (declared later, so destroyed first) closes the Lua states
  _tools.clear();
  _thumbs.clear();
  _preview.clear();
  releaseShaders();
  _renderer.shutdown();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_GL_DestroyContext(_gl);
  SDL_DestroyWindow(_window);
  SDL_Quit();
}

float App::density() const {
  float d = SDL_GetWindowPixelDensity(_window);
  return d > 0 ? d : 1;
}

void App::handleEvent(const SDL_Event& e) {
  bool uiHasMouse = ImGui::GetIO().WantCaptureMouse;
  ImGui_ImplSDL3_ProcessEvent(&e);
  switch(e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
      requestQuit();
      break;
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
      applyTheme();
      break;
    case SDL_EVENT_DROP_FILE:
      if(e.drop.data) {
        std::string path = e.drop.data;
        commitTool();
        confirmDiscard([this, path] { openFile(path); });
      }
      break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
      if(e.key.key == SDLK_SPACE) _spaceDown = e.type == SDL_EVENT_KEY_DOWN && !ImGui::GetIO().WantTextInput;
      break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP:
    case SDL_EVENT_PEN_MOTION:
    case SDL_EVENT_PEN_AXIS:
      canvasEvent(e, uiHasMouse);
      break;
    default:
      if(e.type == s_fileEvent) {
        char* path = static_cast<char*>(e.user.data1);
        if(e.user.code == FILE_OPEN && path) openFile(path);
        if(e.user.code == FILE_EXPORT_ANIM && path) exportAnimation(path);
        if(e.user.code == FILE_IMPORT_SVG && path) importSvg(path, false);
        if(e.user.code == FILE_EXPORT_SVG && path) exportSvg(path);
        if(e.user.code == FILE_IMPORT_SHEET && path) sheetChosen(path);
        if(e.user.code == FILE_EFFECT_PARAM && path && _fx) { _fxValues.text[_fxFileParam] = path; previewEffect(); }
        if((e.user.code == FILE_PALETTE_LOAD || e.user.code == FILE_PALETTE_SAVE) && path) paletteFileChosen(path, e.user.code == FILE_PALETTE_SAVE);
        if(e.user.code == FILE_SAVE) {
          auto after = std::move(_afterSave);
          _afterSave = nullptr;
          if(path) {
            std::string p = path;
            if(!pathFromUtf8(p).has_extension()) p += ".png";
            if(saveTo(p) && after) after();
          }
        }
        SDL_free(path);
      }
      break;
  }
}

// Moves the icon font so a typical icon's center matches the center of a capital letter. Runs on the first
// frames (and after a font change) and converges in one or two font reloads.
void App::calibrateIcons() {
  if(_iconCalibration >= 3) return;
  ImFontBaked* baked = ImGui::GetFontBaked();
  if(!baked) return;
  // Copy each value right away: looking up a glyph can load it and move the glyph array
  ImFontGlyph* g = baked->FindGlyph('H');
  if(!g) { _iconCalibration = 3; return; }
  float capCenter = (g->Y0 + g->Y1) / 2;
  g = baked->FindGlyph(0xF013); // gear: a centered, full-height icon
  if(!g) { _iconCalibration = 3; return; }
  float diff = capCenter - (g->Y0 + g->Y1) / 2;
  if(std::abs(diff) > ImGui::GetFontSize()) { _iconCalibration = 3; return; }
  _iconDiff = diff;
  _iconCalibration++;
  if(std::abs(diff) < 0.5f) { _iconCalibration = 3; return; }
  // The offset is given at the icon font's base size and scales with the rendered size
  _iconOffset += diff * (_theme.fontSize * 0.9f) / ImGui::GetFontSize();
  _fontReload = true;
}

// David sets the pointer itself (ImGui's cursor changes are off): system shapes over the UI, a hand over
// anything clickable, and the tool's cursor over the canvas
void App::updateCursor() {
  ImGuiContext& g = *ImGui::GetCurrentContext();
  bool overCanvas = _gesture == EGesture::Tool || _gesture == EGesture::Pan || (_cursorIn && !ImGui::GetIO().WantCaptureMouse);
  if(g.MovingWindow) _cursors.ui(ImGuiMouseCursor_ResizeAll, false);
  else if(!overCanvas) _cursors.ui(ImGui::GetMouseCursor(), ImGui::IsAnyItemHovered());
  else if(_gesture == EGesture::Pan || _spaceDown) _cursors.canvas(ECursor::Move, tool(), _dpi);
  else _cursors.canvas(tool().cursor(*this, _hover), tool(), _dpi);
}

// While the tool takes typing (text tool), SDL text input stays on and typed characters go to the tool
void App::textInput() {
  bool want = tool().wantsText();
  if(want && !ImGui::GetIO().WantTextInput) {
    if(!SDL_TextInputActive(_window)) SDL_StartTextInput(_window);
    auto& queue = ImGui::GetIO().InputQueueCharacters;
    for(ImWchar c : queue) tool().onText(*this, c);
    queue.resize(0);
    _textInput = true;
  } else if(_textInput && !want) {
    SDL_StopTextInput(_window);
    _textInput = false;
  }
}

ImFont* App::previewFont(const std::string& path, int index) {
  std::string key = path + "#" + std::to_string(index);
  if(auto it = _previewFonts.find(key); it != _previewFonts.end()) return it->second;
  if(std::find(_previewQueue.begin(), _previewQueue.end(), std::pair{ path, index }) == _previewQueue.end()) _previewQueue.push_back({ path, index });
  return nullptr;
}

void App::loadPreviewFonts() {
  if(_previewQueue.empty()) return;
  ImGuiIO& io = ImGui::GetIO();
  // A font ImGui can't read is a normal case here (odd system fonts), not a programming error
  bool asserts = io.ConfigErrorRecoveryEnableAssert, tips = io.ConfigErrorRecoveryEnableTooltip;
  io.ConfigErrorRecoveryEnableAssert = io.ConfigErrorRecoveryEnableTooltip = false;
  // A few per frame keeps scrolling smooth; a font that fails stays null so it isn't retried
  for(int n = 0; n < 6 && !_previewQueue.empty(); n++) {
    auto [path, index] = _previewQueue.front();
    _previewQueue.erase(_previewQueue.begin());
    ImFontConfig cfg;
    cfg.FontNo = index;
    cfg.Flags |= ImFontFlags_NoLoadError;
    _previewFonts[path + "#" + std::to_string(index)] = io.Fonts->AddFontFromFileTTF(path.c_str(), _theme.fontSize, &cfg);
  }
  io.ConfigErrorRecoveryEnableAssert = asserts;
  io.ConfigErrorRecoveryEnableTooltip = tips;
  _redrawUntil = SDL_GetTicks() + 700;
}

void App::frame() {
  if(_fontReload) { _fontReload = false; loadFonts(); }
  loadPreviewFonts();
  // Resize, canvas size and their undo change the image size under the view
  if(_doc) _nav.setImage(_doc->width(), _doc->height());
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
  calibrateIcons();

  // Enter, Escape and Backspace go to the current tool first (finish a polygon, commit a move)
  if(!ImGui::GetIO().WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
    for(ImGuiKey k : { ImGuiKey_Enter, ImGuiKey_KeypadEnter, ImGuiKey_Escape, ImGuiKey_Backspace }) {
      if(!ImGui::IsKeyPressed(k, k == ImGuiKey_Backspace)) continue;
      if(!tool().onKey(*this, k) && k == ImGuiKey_Backspace && !tool().wantsText()) _commands.run("edit.fill");
    }
    tool().keys(*this);
  }
  textInput();
  _commands.handleShortcuts(!tool().wantsText());
  drawMenu();
  drawToolBar();
  drawStatusBar();
  drawToolStrip();
  ImGuiID dock = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingOverCentralNode);
  if(_freshLayout) { buildDefaultLayout(dock); _freshLayout = false; if(_cfg.floatingWindows) _floatReset = true; }
  _dockId = dock;
  tickPlayback();
  drawLayersWindow();
  drawHistoryWindow();
  drawColorsWindow();
  drawTilePreview();
  drawPaletteWindow();
  drawTimeline();
  drawAnimPreview();
  drawObjectWindow();
  drawPenProbe();
  if(_gesture == EGesture::Tool) tool().tick(*this);
  drawModWindows();
  drawSettings();
  _resetWindow = 0;
  _floatReset = false;
  drawDialogs();

  // The canvas fills the dockspace's central node
  float d = ImGui::GetIO().DisplayFramebufferScale.x;
  int fbW, fbH;
  SDL_GetWindowSizeInPixels(_window, &fbW, &fbH);
  if(ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dock))
    _nav.setViewport(int(central->Pos.x * d), int(central->Pos.y * d), int(central->Size.x * d), int(central->Size.y * d), fbH);
  drawOverlays();

  updateCursor();
  ImGui::Render();
  glViewport(0, 0, fbW, fbH);
  glClearColor(_theme.bg.x, _theme.bg.y, _theme.bg.z, 1);
  glClear(GL_COLOR_BUFFER_BIT);

  CanvasColors colors;
  auto copy = [](float* dst, const ImVec4& c) { dst[0] = c.x; dst[1] = c.y; dst[2] = c.z; dst[3] = c.w; };
  copy(colors.canvasBg, _theme.canvasBg);
  copy(colors.checkerA, _theme.checkerA);
  copy(colors.checkerB, _theme.checkerB);
  copy(colors.grid, ImVec4(0.5f, 0.5f, 0.5f, 0.45f));
  copy(colors.customGrid, _theme.grid);
  _nav.view.checkerSize = std::round(_cfg.checkerSize * _dpi);
  _nav.view.gridZoom = _cfg.gridZoom;
  const auto& t = _cfg.tools;
  _nav.view.tiled = t.tiled;
  _nav.view.customGrid = t.grid;
  _nav.view.gridW = t.gridW; _nav.view.gridH = t.gridH; _nav.view.gridX = t.gridX; _nav.view.gridY = t.gridY;
  setOnionView();
  vectorFrame();
  _renderer.draw(*_doc, _nav.view, colors);

  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if(!_shotPath.empty()) {
    _shotFrames++;
    if(!_selftestDir.empty()) selftestStep(_shotFrames);
    if(_shotFrames >= (_selftestDir.empty() ? 20 : 320) && !_reloadAt && !_round2 && !_round3 && !_round4 && !_round5 && !_round6 && !_round7 && !_round8 && !_round9 && !_round10 && !_round11) {
      if(!_selftestDir.empty()) {
        std::ofstream out(pathFromUtf8(_selftestDir + "/selftest.txt"));
        out << _selftestLog;
      }
      saveShot(fbW, fbH);
    }
  }
  SDL_GL_SwapWindow(_window);
  if(_docSeen != _doc->revision) { _docSeen = _doc->revision; updateTitle(); _thumbs.prune(*_doc); }
}

void App::saveShot(int fbW, int fbH) {
  (void)fbW; (void)fbH;
  writeShot(_shotPath);
  _running = false;
}

// Reads the back buffer of the frame just drawn
void App::writeShot(const std::string& path) {
  int fbW, fbH;
  SDL_GetWindowSizeInPixels(_window, &fbW, &fbH);
  std::vector<uint32_t> px(size_t(fbW) * fbH);
  glPixelStorei(GL_PACK_ALIGNMENT, 4);
  glReadBuffer(GL_BACK);
  glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  Document shot(fbW, fbH);
  shot.layers.push_back(std::make_unique<Layer>(fbW, fbH, "shot"));
  for(int y = 0; y < fbH; y++)
    for(int x = 0; x < fbW; x++)
      shot.active().set(x, y, px[size_t(fbH - 1 - y) * fbW + x] | 0xFF000000);
  std::string err;
  if(!png::save(shot, path, err)) SDL_Log("shot: %s", err.c_str());
}

void App::updateTitle() {
  std::string name = _doc->path.empty() ? tr("Untitled") : pathToUtf8(pathFromUtf8(_doc->path).filename());
  std::string title = name + (_doc->modified() ? "*" : "") + " - David";
  if(title != _title) { _title = title; SDL_SetWindowTitle(_window, title.c_str()); }
}
#pragma endregion

#pragma region [ Menu and bars ]
void App::drawMenu() {
  if(!ImGui::BeginMainMenuBar()) return;
  if(ImGui::BeginMenu(tr("File"))) {
    for(auto id : { "file.new", "file.open", "file.save", "file.saveAs" }) _commands.menuItem(id);
    ImGui::Separator();
    _commands.menuItem("file.importSheet");
    _commands.menuItem("file.exportAnim");
    ImGui::Separator();
    if(ImGui::MenuItem((std::string(ICON_FA_GEAR "  ") + tr("Settings")).c_str(), nullptr, _showSettings)) _showSettings = !_showSettings;
    _commands.menuItem("file.exit");
    ImGui::EndMenu();
  }
  if(ImGui::BeginMenu(tr("Edit"))) {
    _commands.menuItem("edit.undo");
    _commands.menuItem("edit.redo");
    ImGui::Separator();
    for(auto id : { "edit.cut", "edit.copy", "edit.copyMerged", "edit.paste", "edit.pasteLayer", "edit.pasteImage" }) _commands.menuItem(id);
    ImGui::Separator();
    for(auto id : { "edit.selectAll", "edit.deselect", "edit.invertSel" }) _commands.menuItem(id);
    ImGui::Separator();
    _commands.menuItem("edit.erase");
    std::string fill = std::string(ICON_FA_FILL_DRIP "  ") + tr("Fill Selection") + "##fill";
    if(ImGui::MenuItem(fill.c_str(), tr("Backspace"), false, _doc->hasSelection())) _commands.run("edit.fill");
    ImGui::EndMenu();
  }
  // Image holds the view too: zoom, windows, pixel art and animation
  if(ImGui::BeginMenu(tr("Image"))) {
    _commands.menuItem("image.crop");
    _commands.menuItem("image.resize");
    _commands.menuItem("image.canvas");
    ImGui::Separator();
    effectsMenu("Layers");
    for(auto id : { "view.zoomIn", "view.zoomOut", "view.fit", "view.actual", "view.reset" }) _commands.menuItem(id);
    ImGui::Separator();
    _commands.menuItem("view.history", _cfg.showHistory);
    _commands.menuItem("view.layers", _cfg.showLayers);
    _commands.menuItem("view.colors", _cfg.showColors);
    ImGui::Separator();
    if(ImGui::BeginMenu((std::string(ICON_FA_CHESS_BOARD "  ") + tr("Pixel Art")).c_str())) {
      _commands.menuItem("view.tilePreview", _cfg.showTilePreview);
      _commands.menuItem("view.palette", _cfg.showPalette);
      ImGui::Separator();
      _commands.menuItem("view.tiled", _cfg.tools.tiled != 0);
      _commands.menuItem("view.symmetry", _cfg.tools.symmetry != 0);
      _commands.menuItem("view.grid", _cfg.tools.grid);
      _commands.menuItem("view.gridSnap", _cfg.tools.gridSnap);
      _commands.menuItem("view.gridSettings");
      ImGui::EndMenu();
    }
    if(ImGui::BeginMenu((std::string(ICON_TB_MOVIE "  ") + tr("Animation")).c_str())) {
      _commands.menuItem("view.timeline", _cfg.showTimeline);
      _commands.menuItem("view.animPreview", _cfg.showAnimPreview);
      ImGui::Separator();
      _commands.menuItem("view.onion", _cfg.anim.onion);
      _commands.menuItem("view.layerOnion", _cfg.anim.layerOnion);
      ImGui::Separator();
      for(auto id : { "anim.play", "anim.prev", "anim.next", "anim.new", "anim.duplicate", "anim.delete" }) _commands.menuItem(id);
      ImGui::Separator();
      _commands.menuItem("anim.layersToFrames");
      _commands.menuItem("anim.framesToLayers");
      ImGui::EndMenu();
    }
    if(_cfg.betaVector && ImGui::BeginMenu((std::string(ICON_FA_BEZIER_CURVE "  ") + tr("Vector")).c_str())) {
      for(auto id : { "layer.addVector", "layer.rasterize", "image.trace" }) _commands.menuItem(id);
      ImGui::Separator();
      _commands.menuItem("file.importSvg");
      _commands.menuItem("file.exportSvg");
      ImGui::Separator();
      _commands.menuItem("view.vectorPixels", _cfg.vectorPixels);
      _commands.menuItem("view.object", _cfg.showObject);
      ImGui::EndMenu();
    }
    _commands.menuItem("view.probe", _cfg.showProbe);
    _commands.menuItem("image.autotile");
    ImGui::EndMenu();
  }
  if(ImGui::BeginMenu(tr("Adjustments"))) {
    effectsMenu("Adjustments");
    ImGui::EndMenu();
  }
  if(ImGui::BeginMenu(tr("Effects"))) {
    effectsMenu("Effects");
    ImGui::EndMenu();
  }
  drawModsMenu();
  if(ImGui::BeginMenu(tr("Help"))) {
    if(ImGui::MenuItem((std::string(ICON_FA_BOOK "  ") + tr("Modding guide")).c_str())) openGuide();
    _commands.menuItem("help.about");
    ImGui::EndMenu();
  }

  // Window toggles on the right: lit when the window is open
  struct Toggle { const char* id; bool* shown; };
  // Only the icon's color shows the state: accent when open, muted when closed
  Toggle toggles[] = { { "view.colors", &_cfg.showColors }, { "view.layers", &_cfg.showLayers }, { "view.history", &_cfg.showHistory }, { "view.timeline", &_cfg.showTimeline } };
  float btn = ImGui::GetFrameHeight();
  ImGui::SameLine(ImGui::GetWindowWidth() - (btn + 2) * std::size(toggles) - ImGui::GetStyle().WindowPadding.x - 8 * _dpi);
  for(auto& t : toggles) {
    Command* c = _commands.find(t.id);
    if(!c) continue;
    ImVec2 a = ImGui::GetCursorScreenPos(), b = { a.x + btn, a.y + btn };
    if(ImGui::InvisibleButton(t.id, { btn, btn })) _commands.run(t.id);
    ImGui::SetItemTooltip("%s", tr(c->label.c_str()));
    // Hover wins over the open state, so the pointer always gets feedback
    ImVec4 col = ImGui::IsItemHovered() ? _theme.text : *t.shown ? _theme.accent : _theme.textMuted;
    ui::centeredIcon(ImGui::GetWindowDrawList(), c->icon, a, b, ImGui::GetColorU32(col));
    ImGui::SameLine(0, 2);
  }
  ImGui::EndMainMenuBar();
}

void App::drawStatusBar() {
  ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_MenuBar;
  float h = ImGui::GetFrameHeight();
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, _theme.bg);
  if(ImGui::BeginViewportSideBar("##status", ImGui::GetMainViewport(), ImGuiDir_Down, h, flags)) {
    if(ImGui::BeginMenuBar()) {
      ImGui::TextDisabled(ICON_FA_ARROW_POINTER);
      ImGui::SameLine();
      if(_cursorIn) ImGui::Text("%d, %d", int(std::floor(_hover.x)), int(std::floor(_hover.y)));
      else ImGui::TextDisabled("-");
      ImGui::SameLine(0, 24 * _dpi);
      ImGui::TextDisabled(ICON_FA_EXPAND);
      ImGui::SameLine();
      ImGui::Text("%d × %d", _doc->width(), _doc->height());
      // The box the selection covers: its top-left corner and size (the picked objects' box on a vector layer)
      DRect box;
      if(vectorMode() && !_doc->selectedObjects.empty()) {
        const auto& objs = vectorTree(_doc->active(), _doc->active().frame()).objects;
        for(uint64_t id : _doc->selectedObjects) if(const VecObject* o = findObject(objs, id)) box.add(bounds(*o, true));
      } else if(_doc->hasSelection()) {
        IRect b = _doc->selection->bounds();
        box = { double(b.x0), double(b.y0), double(b.x1 + 1), double(b.y1 + 1) };
      }
      if(!box.empty()) {
        ImGui::SameLine(0, 24 * _dpi);
        ImGui::TextDisabled(ICON_FA_CROP_SIMPLE);
        ImGui::SetItemTooltip("%s", tr("Selection: position and size"));
        ImGui::SameLine();
        ImGui::Text("%g, %g", std::round(box.x0 * 10) / 10, std::round(box.y0 * 10) / 10);
        ImGui::SameLine(0, 12 * _dpi);
        ImGui::Text("%g × %g", std::round(box.width() * 10) / 10, std::round(box.height() * 10) / 10);
      }

      // Zoom controls on the right
      char zoom[32];
      std::snprintf(zoom, sizeof zoom, "%g%%", std::round(_nav.view.zoom * 1000) / 10);
      float btn = ImGui::GetFrameHeight();
      float w = btn * 3 + ImGui::CalcTextSize("10000%").x + ImGui::GetStyle().ItemSpacing.x * 3;
      ImGui::SameLine(ImGui::GetWindowWidth() - w - ImGui::GetStyle().WindowPadding.x);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 0, ImGui::GetStyle().FramePadding.y });
      Command* reset = _commands.find("view.reset");
      std::string resetTip = std::string(tr("Reset View")) + (reset && !reset->chords.empty() ? "  (" + Commands::chordName(reset->chords[0]) + ")" : "");
      if(ui::iconButton("##zreset", ICON_FA_ARROWS_TO_CIRCLE, { btn, btn }, false, resetTip.c_str())) _commands.run("view.reset");
      ImGui::SameLine();
      if(ui::iconButton("##zout", ICON_FA_MAGNIFYING_GLASS_MINUS, { btn, btn }, false, tr("Zoom Out"))) _nav.zoomStepCenter(-1);
      ImGui::SameLine();
      if(ImGui::Button(zoom, { ImGui::CalcTextSize("10000%").x, 0 })) _nav.actualSize();
      ImGui::SetItemTooltip("%s", tr("Actual Size"));
      ImGui::SameLine();
      if(ui::iconButton("##zin", ICON_FA_MAGNIFYING_GLASS_PLUS, { btn, btn }, false, tr("Zoom In"))) _nav.zoomStepCenter(1);
      ImGui::PopStyleVar();
      ImGui::EndMenuBar();
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
}

static bool iconButton(const char* icon, const char* tip, bool primary = false, const Theme* = nullptr) {
  float s = ImGui::GetFrameHeight() * 1.6f;
  return ui::iconButton(icon, icon, { s, s * 0.75f }, primary, tip ? tr(tip) : nullptr);
}

void App::drawDialogs() {
  if(!_popup.empty()) { ImGui::OpenPopup(_popup.c_str()); _popup.clear(); }
  ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  auto enter = [] { return ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter); };

  drawSizeDialog();
  drawUnsavedDialog();
  drawResetDialog();
  drawPasteDialog();
  drawRasterDialog();
  drawEffectDialog();
  drawCommandDialog();
  drawGridDialog();
  drawAutotileDialog();
  drawAnimDialogs();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string newTitle = std::string(ICON_FA_FILE "  ") + tr("New Image") + "###new";
  bool newOpen = true;
  if(ImGui::BeginPopupModal(newTitle.c_str(), &newOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    pushFieldColors();
    float labelW = 80 * _dpi, fieldW = 120 * _dpi;
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Width:"));
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(fieldW);
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputInt("##w", &_newW, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("pixels"));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Height:"));
    ImGui::SameLine(labelW);
    ImGui::SetNextItemWidth(fieldW);
    ImGui::InputInt("##h", &_newH, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("pixels"));
    ImGui::PopStyleColor(3);
    _newW = std::clamp(_newW, 1, 65535);
    _newH = std::clamp(_newH, 1, 65535);
    ImGui::Spacing();
    ImGui::Spacing();
    float bw = 100 * _dpi;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x));
    bool ok = ui::dialogButton(tr("OK"), true, bw) || enter();
    ImGui::SameLine();
    bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !newOpen;
    if(ok) {
      ImGui::CloseCurrentPopup();
      if(!_newFromClipboard) { _cfg.newW = _newW; _cfg.newH = _newH; }
      confirmDiscard([this, w = _newW, h = _newH] { setDocument(Document::blank(w, h, WHITE)); });
    } else if(cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  // Layer properties edit the layer live; OK records one History step, cancel puts it back
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, { 0.5f, 0.5f });
  if(ImGui::BeginPopupModal("layerProps", nullptr, flags)) {
    Layer& l = _doc->active();
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(220 * _dpi);
    if(ImGui::InputText("##name", _propsName, sizeof _propsName)) l.name = _propsName;
    if(ImGui::Button(l.visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH, { ImGui::GetFrameHeight() * 1.3f, 0 })) { l.visible = !l.visible; _doc->onChanged(); }
    ImGui::SetItemTooltip("%s", tr("Visible"));
    ImGui::SameLine();
    int opacity = l.opacity;
    ImGui::SetNextItemWidth(220 * _dpi - ImGui::GetFrameHeight() * 1.3f - ImGui::GetStyle().ItemSpacing.x);
    if(ImGui::SliderInt("##opacity", &opacity, 0, 255)) { l.opacity = uint8_t(opacity); _doc->onChanged(); }
    ImGui::SetItemTooltip("%s", tr("Opacity"));
    int mode = int(l.blend);
    ImGui::SetNextItemWidth(220 * _dpi);
    if(ImGui::BeginCombo("##blend", tr(blendName(l.blend)))) {
      for(int i = 0; i < int(EBlend::COUNT); i++)
        if(ImGui::Selectable(tr(blendName(EBlend(i))), i == mode)) { l.blend = EBlend(i); _doc->onChanged(); }
      ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr("Blend mode"));
    bool ok = iconButton(ICON_FA_CHECK, nullptr, true, &_theme) || enter();
    ImGui::SameLine();
    bool cancel = iconButton(ICON_FA_XMARK, nullptr) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if(ok || cancel) {
      LayerProps after = LayerProps::of(l);
      _propsBefore.applyTo(l);
      if(ok) setLayerProps(*_doc, _doc->activeLayer, after, ICON_FA_SLIDERS);
      _doc->onChanged();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, { 0.5f, 0.5f });
  if(ImGui::BeginPopupModal("error", nullptr, flags)) {
    ImGui::PushStyleColor(ImGuiCol_Text, _theme.bad);
    ImGui::TextUnformatted(ICON_FA_CIRCLE_EXCLAMATION);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(tr(_error.c_str()));
    if(iconButton(ICON_FA_CHECK, nullptr, true, &_theme) || enter() || ImGui::IsKeyPressed(ImGuiKey_Escape))
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string aboutTitle = std::string(ICON_DAVID_LOGO "  ") + tr("About") + "###about";
  bool aboutOpen = true;
  if(ImGui::BeginPopupModal(aboutTitle.c_str(), &aboutOpen, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    // The logo (the white icon, in the theme's text color) next to the name
    float logo = std::round(ImGui::GetFontSize() * 2.6f);
    ImGui::Spacing();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({ logo, logo });
    ui::centeredText(ImGui::GetWindowDrawList(), ImGui::GetFont(), logo, ICON_DAVID_LOGO, p, { p.x + logo, p.y + logo }, ImGui::GetColorU32(_theme.text));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(_boldFont, _theme.fontSize * 1.6f);
    ImGui::TextUnformatted("David");
    ImGui::PopFont();
    ImGui::TextDisabled("%s", tr("Raster paint program with pixel-art and animation tools"));
    ImGui::EndGroup();
    ImGui::Spacing();
    float labelW = 80 * _dpi;
    auto row = [&](const char* label, const char* value) {
      ImGui::TextUnformatted(tr(label));
      ImGui::SameLine(labelW);
      ImGui::TextDisabled("%s", value);
    };
    row(N_("Version:"), DAVID_VERSION);
    row(N_("License:"), "MIT");
    ImGui::Spacing();
    ImGui::Spacing();
    float bw = 100 * _dpi;
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw));
    if(ui::dialogButton(tr("OK"), true, bw) || enter() || ImGui::IsKeyPressed(ImGuiKey_Escape) || !aboutOpen)
      ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
#pragma endregion

#pragma region [ Files ]
void App::setDocument(std::unique_ptr<Document> doc) {
  if(_doc) commitTool();
  _doc = std::move(doc);
  _nav.setImage(_doc->width(), _doc->height());
  _nav.fit();
  _docSeen = ~0ull;
  _outlineSerial = 0;
  _doc->history.setCap(size_t(_cfg.historyMb) << 20);
  _player.stop();
  _previewPlayer.stop();
  _selFrom = _selTo = _dragFrame = _tagEdit = -1;
  for(auto& [f, t] : _animFrames) t.clear();
  _animFrames.clear();
}

void App::openFile(const std::string& path) {
  std::string err;
  auto doc = loadImage(path, err);
  if(!doc) { _error = err; _popup = "error"; return; }
  // A .david file carries its palette
  if(!doc->palette.empty()) _cfg.tools.palette.colors = doc->palette;
  setDocument(std::move(doc));
}

bool App::saveTo(const std::string& path) {
  commitTool();
  std::string err;
  _doc->palette = _cfg.tools.palette.colors;
  if(!saveImage(*_doc, path, err)) { _error = err; _popup = "error"; return false; }
  _doc->path = path;
  _doc->history.markClean();
  _doc->onChanged();
  return true;
}

void App::pickFile(int code, bool save, const SDL_DialogFileFilter* filters, int count) {
  if(save) SDL_ShowSaveFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(code)), _window, filters, count, nullptr);
  else SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(code)), _window, filters, count, nullptr, false);
}

void App::save() {
  if(_doc->path.empty()) saveAs();
  else saveTo(_doc->path);
}

void App::saveAs() {
  SDL_ShowSaveFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_SAVE)), _window, SAVE_FILTER, int(std::size(SAVE_FILTER)),
                         _doc->path.empty() ? nullptr : _doc->path.c_str());
}

// Cancel, Save (and close), Close / Don't save; the X in the title bar cancels
void App::drawUnsavedDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(_pendingClosing ? tr("Close") : tr("Unsaved changes")) + "###unsaved";
  bool open = true;
  if(!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    // Closed by the X (or never opened): nothing waits any more
    if(!ImGui::IsPopupOpen("###unsaved")) _pending = nullptr;
    return;
  }
  std::string name = _doc->path.empty() ? tr("Untitled") : pathToUtf8(pathFromUtf8(_doc->path).filename());
  ImGui::Spacing();
  ImGui::Text("%s \"%s\"?", tr("Save the changes to"), name.c_str());
  ImGui::Spacing();
  ImGui::Spacing();
  float bw = 120 * _dpi, sp = ImGui::GetStyle().ItemSpacing.x;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 3 - sp * 2));
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !open;
  ImGui::SameLine();
  bool doSave = ui::dialogButton(_pendingClosing ? tr("Save and close") : tr("Save"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  ImGui::SameLine();
  bool discard = ui::dialogButton(_pendingClosing ? tr("Close") : tr("Don't save"), false, bw);
  if(cancel || doSave || discard) {
    ImGui::CloseCurrentPopup();
    auto action = std::move(_pending);
    _pending = nullptr;
    if(doSave) {
      _afterSave = std::move(action);
      if(_doc->path.empty()) saveAs();
      else if(saveTo(_doc->path) && _afterSave) { auto after = std::move(_afterSave); _afterSave = nullptr; after(); }
    } else if(discard && action) {
      action();
    }
  }
  ImGui::EndPopup();
}

void App::drawResetDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(ICON_FA_TRIANGLE_EXCLAMATION "  ") + tr("Reset all settings") + "###resetall";
  bool open = true;
  if(!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
  ImGui::Spacing();
  ImGui::TextUnformatted(tr("Every setting goes back to David's defaults and your current ones are lost:"));
  ImGui::TextDisabled("%s", tr("theme, language, shortcuts, tool options and palette, the tool strip, window layout,"));
  ImGui::TextDisabled("%s", tr("animation and canvas options, and which mods are on."));
  ImGui::TextDisabled("%s", tr("Your images, theme files and mods stay as they are."));
  ImGui::Spacing();
  ImGui::Spacing();
  float bw = 130 * _dpi;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x));
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !open;
  ImGui::SameLine();
  // The destructive action is the red one, and Enter doesn't trigger it
  ImGui::PushStyleColor(ImGuiCol_Button, _theme.bad);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, _theme.bad);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, _theme.bad);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
  bool reset = ImGui::Button(tr("Reset everything"), { bw, 0 });
  ImGui::PopStyleColor(4);
  if(reset) resetAllSettings();
  if(reset || cancel) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void App::resetAllSettings() {
  commitTool();
  _cfg = Settings{};
  std::string err;
  loadLanguage(paths::exeDir() + "lang/", _cfg.language, err);
  selectTheme(_cfg.theme);
  for(auto& c : _commands.list()) c.chords = c.defaults;
  _mods.apply(_cfg.mods);
  _modWindowError.clear();
  // The default docked layout is built again on the next frame; the 1 s tick saves the file
  _freshLayout = true;
}

void App::showPaletteDialog(bool save) {
  if(save) SDL_ShowSaveFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_PALETTE_SAVE)), _window, PALETTE_FILTER, 1, nullptr);
  else SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_PALETTE_LOAD)), _window, PALETTE_FILTER, 1, nullptr, false);
}

void App::showEffectFileDialog(const std::string& param) {
  _fxFileParam = param;
  SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_EFFECT_PARAM)), _window, PALETTE_FILTER, 1, nullptr, false);
}

void App::showOpenDialog() {
  if(_cfg.betaVector) SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_OPEN)), _window, OPEN_FILTER_SVG, int(std::size(OPEN_FILTER_SVG)), nullptr, false);
  else SDL_ShowOpenFileDialog(onFileDialog, reinterpret_cast<void*>(intptr_t(FILE_OPEN)), _window, OPEN_FILTER, int(std::size(OPEN_FILTER)), nullptr, false);
}

// Asks before the image's changes would be lost; the answer runs the action (or saves first)
void App::confirmDiscard(std::function<void()> action, bool closing) {
  if(!_doc || !_doc->modified()) { action(); return; }
  // One question at a time (closing the window sends two events)
  if(_pending) return;
  _pending = std::move(action);
  _pendingClosing = closing;
  _popup = "###unsaved";
}

void App::requestQuit() {
  commitTool();
  confirmDiscard([this] { _running = false; }, true);
}
#pragma endregion

}
