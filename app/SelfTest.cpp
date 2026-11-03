#include "app/App.h"
#include "core/Layer.h"
#include "core/Vector.h"
#include "core/io/Formats.h"
#include "core/Draw.h"
#include "core/Palette.h"
#include "core/Blend.h"
#include <chrono>
#include <filesystem>
#include "core/Composite.h"
#include <glad/glad.h>
#include "core/Gradient.h"
#include "core/io/Png.h"
#include "platform/Paths.h"
#include <cmath>
#include <fstream>
#include <imgui_internal.h>
#include "ui/I18n.h"

namespace david {

// Dev check: drives the canvas with synthetic SDL events, then logs what the document saw
void App::selftestStep(int frame) {
  float d = density();
  auto& v = _nav.view;
  float cx = (v.vpX + v.vpW / 2.f) / d, cy = (v.vpY + v.vpH / 2.f) / d;
  auto push = [](SDL_EventType type, float x, float y, Uint8 button) {
    SDL_Event e{};
    e.type = type;
    if(type == SDL_EVENT_MOUSE_MOTION) { e.motion.x = x; e.motion.y = y; }
    else { e.button.x = x; e.button.y = y; e.button.button = button; e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.clicks = 1; }
    SDL_PushEvent(&e);
  };
  auto drag = [&](float x0, float y0, float x1, float y1, Uint8 button) {
    push(SDL_EVENT_MOUSE_MOTION, x0, y0, 0);
    push(SDL_EVENT_MOUSE_BUTTON_DOWN, x0, y0, button);
    push(SDL_EVENT_MOUSE_MOTION, (x0 + x1) / 2, (y0 + y1) / 2, 0);
    push(SDL_EVENT_MOUSE_MOTION, x1, y1, 0);
    push(SDL_EVENT_MOUSE_BUTTON_UP, x1, y1, button);
  };
  auto use = [&](ETool id) { for(int i = 0; i < int(_tools.size()); i++) if(_tools[i]->id == id) selectTool(i); };
  auto log = [this](const std::string& s) {
    _selftestLog += s + "\n";
    // Also written as it goes, so a crash still shows how far it got
    std::ofstream(pathFromUtf8(_selftestDir + "/selftest-progress.txt"), std::ios::app) << s << "\n";
  };
  auto ix = [&](float wx) { return int(std::floor(_nav.toImageX(wx * d))); };
  auto iy = [&](float wy) { return int(std::floor(_nav.toImageY(wy * d))); };
  auto pixelAt = [&](float wx, float wy) { return _doc->active().get(ix(wx), iy(wy)); };
  auto check = [&](const char* what, bool ok) { log(std::string(ok ? "ok   " : "FAIL ") + what); };
  const uint32_t RED = Color{ 255, 0, 0, 255 }.pack(), HALF = Color{ 0, 0, 255, 128 }.pack();

  // The hot-reload tick runs on wall time, so its check waits on the clock rather than a frame number
  if(_reloadAt && SDL_GetTicks() >= _reloadAt) {
    _reloadAt = 0;
    check("theme file hot reload", colorToHex(_theme.accent) == "#FF00FF");
    _themes.resetToBuiltin("classic");
    selectTheme("dev");
    check("reset to built-in", colorToHex(_themeBase.accent) == "#8BE9FD" && _themeId == "dev");
    _round2 = frame;
  }

  // Round 2 (look review): translation table, flat panels, floating windows, color picker modes
  if(_round2 && frame > _round2) {
    int f = frame - _round2;
    if(f == 1) {
      std::ofstream lang(pathFromUtf8(_selftestDir + "/xx.json"));
      lang << "{ \"_name\": \"Test\", \"Undo\": \"Desfazer\" }";
      lang.close();
      std::string err;
      check("translation file loads", loadLanguage(_selftestDir + "/", "xx", err) && std::string(tr("Undo")) == "Desfazer" && std::string(tr("Redo")) == "Redo");
      loadLanguage("", "en", err);
      check("English is the fallback", std::string(tr("Undo")) == "Undo");
      _cfg.colorAdvanced = true;
      _cfg.colorSquare = true;
    }
    if(f == 5) writeShot(_selftestDir + "/r2-dev-docked.png");
    if(f == 6) setFloating(true);
    if(f == 10) {
      ImGuiWindow* cw = ImGui::FindWindowByName("###colors");
      ImGuiWindow* lw = ImGui::FindWindowByName("###layers");
      check("floating windows are undocked", cw && lw && !cw->DockIsActive && !lw->DockIsActive);
      check("floating rects are kept for the settings", _cfg.floatRects.size() == 3);
      check("tools are a fixed strip, not a window", !ImGui::FindWindowByName("###tools") && ImGui::FindWindowByName("##tools"));
      check("icons centered on capital letters", std::abs(_iconDiff) < 1.0f);
      log("icon offset " + std::to_string(_iconOffset) + " px, residual " + std::to_string(_iconDiff));
      writeShot(_selftestDir + "/r2-dev-floating.png");
      _showSettings = true;
      _settingsPage = 0;
    }
    if(f == 14) { writeShot(_selftestDir + "/r2-dev-settings.png"); _settingsPage = 3; }
    if(f == 17) { writeShot(_selftestDir + "/r2-dev-shortcuts.png"); _showSettings = false; setFloating(false); }
    if(f == 21) {
      check("docked again", ImGui::FindWindowByName("###colors")->DockIsActive);
      // Default layout: Layers and History share the dock node under Colors
      ImGuiWindow* lw = ImGui::FindWindowByName("###layers");
      ImGuiWindow* hw = ImGui::FindWindowByName("###history");
      check("Layers and History are tabs of one node", lw && hw && lw->DockNode && lw->DockNode == hw->DockNode);
      _round2 = 0;
      _round3 = frame;
    }
  }

  // Round 3 (phase 4): shapes, wand, line, text, gradient on a fresh image
  if(_round3 && frame > _round3) {
    int f = frame - _round3;
    // Window coordinates of an image pixel's center
    auto at = [&](double x, double y) { return toScreen(x + 0.5, y + 0.5); };
    auto imgPx = [&](int x, int y) { return _doc->active().get(x, y); };
    auto key = [this](SDL_Keycode k, SDL_Scancode sc) {
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.scancode = sc; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
    };
    auto dragImg = [&](double x0, double y0, double x1, double y1) {
      ImVec2 a = at(x0, y0), b = at(x1, y1);
      drag(a.x, a.y, b.x, b.y, SDL_BUTTON_LEFT);
    };
    auto clickImg = [&](double x, double y) {
      ImVec2 a = at(x, y);
      push(SDL_EVENT_MOUSE_MOTION, a.x, a.y, 0);
      push(SDL_EVENT_MOUSE_BUTTON_DOWN, a.x, a.y, SDL_BUTTON_LEFT);
      push(SDL_EVENT_MOUSE_BUTTON_UP, a.x, a.y, SDL_BUTTON_LEFT);
    };
    const uint32_t BLUE = Color{ 0, 0, 255, 255 }.pack();
    switch(f) {
      case 1:
        setDocument(Document::blank(600, 400, WHITE));
        _nav.actualSize();
        _nav.centerOn(300, 200);
        break;
      case 3:
        use(ETool::Shapes);
        _cfg.tools.shapeKind = int(EShapeKind::Rect);
        _cfg.tools.shapeDraw = 2;
        _cfg.tools.brushSize = 4;
        _cfg.tools.antialias = true;
        _primary = BLACK; _secondary = RED;
        dragImg(100, 100, 300, 250);
        break;
      case 5:
        check("shape stays editable", tool().busy() && _doc->history.count() == 0);
        check("shape outline primary, fill secondary", imgPx(102, 150) == BLACK && imgPx(200, 170) == RED && imgPx(98, 150) == WHITE);
        // Dragging a corner resizes: the opposite corner stays where it was
        dragImg(300, 250, 350, 300);
        break;
      case 6:
        check("dragging a corner resizes the shape", imgPx(348, 298) == BLACK && imgPx(102, 150) == BLACK && imgPx(101, 101) == BLACK);
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      case 7:
        check("Enter commits the shape as one step", !tool().busy() && _doc->history.count() == 1 && _doc->history.at(0).name == "Shapes");
        use(ETool::MagicWand);
        _cfg.tools.tolerance = 0.5f;
        _cfg.tools.edgeStop = false;
        clickImg(200, 170);
        break;
      case 9:
        check("wand selects the fill only", _doc->selection->get(200, 170) == 255 && _doc->selection->get(101, 150) == 0 && _doc->selection->get(50, 50) == 0);
        _commands.run("edit.deselect");
        _settingsSeen = int(_doc->history.count());
        use(ETool::Line);
        _cfg.tools.lineSharp = false;
        _cfg.tools.brushSize = 6;
        _cfg.tools.dash = 0;
        _primary = BLUE;
        dragImg(350, 300, 550, 300);
        break;
      case 11:
        check("line drawn live", imgPx(450, 300) == BLUE && tool().busy());
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 13:
        check("Escape cancels the line", imgPx(450, 300) == WHITE && !tool().busy() && int(_doc->history.count()) == _settingsSeen);
        use(ETool::Text);
        _cfg.tools.fontSize = 40;
        _primary = BLACK;
        clickImg(350, 100);
        break;
      case 15:
        check("text tool takes typing", tool().wantsText());
        for(char c : std::string("Hi")) tool().onText(*this, c);
        break;
      case 17: {
        int ink = 0;
        for(int y = 100; y < 160; y++)
          for(int x = 350; x < 450; x++) ink += imgPx(x, y) != WHITE;
        check("typed text renders", ink > 50);
        writeShot(_selftestDir + "/p4-tools.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      }
      case 19:
        check("Escape commits the text", !tool().busy() && _doc->history.at(_doc->history.count() - 1).name == "Text");
        use(ETool::Gradient);
        _cfg.tools.gradType = 0;
        _cfg.tools.gradSpace = int(EGradSpace::Oklab);
        _primary = BLACK; _secondary = WHITE;
        dragImg(0, 350, 599, 350);
        break;
      case 21: {
        uint8_t mid = Color::unpack(imgPx(300, 380)).r;
        check("OKLab gradient midpoint is perceptual gray", mid > 85 && mid < 115);
        writeShot(_selftestDir + "/p4-gradient.png");
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      }
      case 23:
        check("gradient commits as one step", !tool().busy() && _doc->history.at(_doc->history.count() - 1).name == "Gradient");
        _commands.run("edit.undo");
        break;
      case 25: {
        check("undo gradient", imgPx(5, 5) == WHITE);
        // Free panning: the image follows the drag past where the old clamp held it
        _nav.fit();
        float x0 = toScreen(0, 0).x;
        _nav.panBy(300 * d, 0);
        check("canvas pans freely", std::abs(toScreen(0, 0).x - x0 - 300) < 2);
        _commands.run("view.reset");
        check("reset view centers again", std::abs(toScreen(0, 0).x - x0) < 2);
        // Curve: first drag is a straight segment, a click adds a node, clicking the first node closes it
        use(ETool::Curve);
        _cfg.tools.brushSize = 6;
        _cfg.tools.dash = 0;
        _cfg.tools.arrowStart = _cfg.tools.arrowEnd = false;
        _primary = BLACK;
        dragImg(100, 300, 200, 300);
        clickImg(150, 380);
        clickImg(100, 300);
        break;
      }
      case 27:
        check("clicking the first node closes the curve", !tool().busy() && _doc->history.at(_doc->history.count() - 1).name == "Curve");
        check("closing segment drawn", imgPx(125, 340) != WHITE);
        _themeBase.fontSize = 20;
        refreshTheme();
        break;
      case 29:
        check("font size applies to the UI", std::abs(ImGui::GetStyle().FontSizeBase - 20) < 0.01f);
        writeShot(_selftestDir + "/lr3-font20.png");
        _themeBase.fontSize = 15;
        refreshTheme();
        openSizeDialog(false);
        break;
      case 32:
        writeShot(_selftestDir + "/lr3-resize.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 34:
        openSizeDialog(true);
        break;
      case 37:
        writeShot(_selftestDir + "/lr3-canvas.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 39:
        check("size dialogs close with Escape", !ImGui::IsPopupOpen("###size", ImGuiPopupFlags_AnyPopupId) && _doc->history.count() >= 1);
        // Closing the window sends two events; the question must come once
        requestQuit();
        requestQuit();
        break;
      case 42:
        check("closing asks once, in the app's own dialog", ImGui::IsPopupOpen("###unsaved", ImGuiPopupFlags_AnyPopupId) && _pending && _running);
        writeShot(_selftestDir + "/lr6-close.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 44:
        check("Escape cancels closing", !ImGui::IsPopupOpen("###unsaved", ImGuiPopupFlags_AnyPopupId) && !_pending && _running);
        use(ETool::RectSelect);
        _cfg.tools.selectShape = 0;
        dragImg(100, 100, 200, 140);
        break;
      case 46: {
        check("finishing a selection switches to Move Selected Pixels", tool().id == ETool::MovePixels);
        // Drag the rotate handle (24 px above the top edge) to the right of the center: a quarter turn
        use(ETool::MovePixels);
        ImVec2 top = toScreen(150, 100), c = toScreen(150, 120);
        drag(top.x, top.y - 24 * _dpi, c.x + 80, c.y, SDL_BUTTON_LEFT);
        break;
      }
      case 48: {
        IRect b = _doc->selection->bounds();
        check("the rotate handle turns the selection", b.height() > 90 && b.width() < 50);
        writeShot(_selftestDir + "/lr7-rotate.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      }
      case 50:
        check("Escape puts the selection back", _doc->selection->bounds().width() == 100);
        // The bottom-right corner nub, 50 px out (the box is 100 × 40; dragImg lands on pixel centers)
        { IRect b = _doc->selection->bounds(); dragImg(b.x1 + 1, b.y1 + 1, b.x1 + 50, b.y1 + 50); }
        break;
      case 52: {
        IRect b = _doc->selection->bounds();
        check("a corner nub resizes the selection", b.width() == 150 && b.height() == 90);
        writeShot(_selftestDir + "/lr8-resize.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      }
      case 54:
        check("Escape undoes the resize", _doc->selection->bounds().width() == 100);
        _round3 = 0;
        _round4 = frame;
        break;
    }
  }

  // Round 4 (phase 5): symmetry, tiled wrap, pixel-perfect, shading, outline, autotile, spray, free polygon
  if(_round4 && frame > _round4) {
    int f = frame - _round4;
    auto at = [&](double x, double y) { return toScreen(x + 0.5, y + 0.5); };
    auto imgPx = [&](int x, int y) { return _doc->active().get(x, y); };
    auto dragImg = [&](double x0, double y0, double x1, double y1) {
      ImVec2 a = at(x0, y0), b = at(x1, y1);
      drag(a.x, a.y, b.x, b.y, SDL_BUTTON_LEFT);
    };
    auto key = [this](SDL_Keycode k, SDL_Scancode sc) {
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.scancode = sc; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
    };
    const uint32_t BLUE = Color{ 0, 0, 255, 255 }.pack(), GRAY40 = 0xFF404040u, GRAY80 = 0xFF808080u;
    auto& t = _cfg.tools;
    switch(f) {
      case 1:
        setDocument(Document::blank(200, 100, WHITE));
        _nav.actualSize();
        _nav.centerOn(100, 50);
        _primary = BLACK;
        t.pixelPerfect = false; t.antialias = false; t.shading = false;
        break;
      case 3:
        use(ETool::Pencil);
        t.symmetry = 1;
        dragImg(20, 20, 40, 20);
        break;
      case 5:
        check("symmetry mirrors the stroke", imgPx(30, 20) == BLACK && imgPx(169, 20) == BLACK && imgPx(100, 20) == WHITE);
        t.symmetry = 0;
        t.tiled = 1;
        t.brushSize = 6;
        use(ETool::Brush);
        dragImg(190, 50, 206, 50);
        break;
      case 7:
        check("tiled mode wraps the stroke", imgPx(195, 50) == BLACK && imgPx(4, 50) == BLACK);
        t.tiled = 0;
        t.pixelPerfect = true;
        use(ETool::Pencil);
        {
          // An L corner: right, then down, then right
          ImVec2 a = at(50, 70), b = at(51, 70), c = at(51, 71), e = at(52, 71);
          push(SDL_EVENT_MOUSE_MOTION, a.x, a.y, 0);
          push(SDL_EVENT_MOUSE_BUTTON_DOWN, a.x, a.y, SDL_BUTTON_LEFT);
          push(SDL_EVENT_MOUSE_MOTION, b.x, b.y, 0);
          push(SDL_EVENT_MOUSE_MOTION, c.x, c.y, 0);
          push(SDL_EVENT_MOUSE_MOTION, e.x, e.y, 0);
          push(SDL_EVENT_MOUSE_BUTTON_UP, e.x, e.y, SDL_BUTTON_LEFT);
        }
        break;
      case 9:
        check("pixel-perfect drops the L corner", imgPx(50, 70) == BLACK && imgPx(51, 70) == WHITE && imgPx(51, 71) == BLACK && imgPx(52, 71) == BLACK);
        t.pixelPerfect = false;
        t.palette.colors = { BLACK, GRAY40, GRAY80 };
        _doc->active().set(120, 80, GRAY40);
        t.shading = true;
        dragImg(120, 80, 121, 80);
        break;
      case 11:
        check("shading steps along the palette", imgPx(120, 80) == GRAY80 && imgPx(121, 80) == WHITE);
        t.shading = false;
        t.palette = Palette::defaults();
        {
          auto doc = Document::blank(60, 40, CLEAR);
          for(int y = 10; y < 20; y++) for(int x = 20; x < 30; x++) doc->active().set(x, y, BLACK);
          setDocument(std::move(doc));
        }
        _primary = BLUE;
        _fxRemember.clear();
        openEffect("outline");
        break;
      case 13:
        check("the effect dialog previews live", ImGui::IsPopupOpen("###effect", ImGuiPopupFlags_AnyPopupId) && imgPx(19, 9) == BLUE);
        writeShot(_selftestDir + "/p5-outline.png");
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      case 15:
        check("OK commits the outline as one step", !_fx && _doc->history.count() == 1 && _doc->history.at(0).name == "Outline");
        _commands.run("effect.repeat");
        break;
      case 17:
        check("Ctrl+F repeats the last effect", _doc->history.count() == 2 && imgPx(18, 8) == BLUE);
        {
          // Autotile source: a framed border tile and a floor tile
          auto doc = Document::blank(32, 16, CLEAR);
          for(int y = 0; y < 16; y++)
            for(int x = 0; x < 16; x++) {
              doc->active().set(x, y, x == 0 || y == 0 || x == 15 || y == 15 ? BLACK : RED);
              doc->active().set(16 + x, y, BLUE);
            }
          setDocument(std::move(doc));
        }
        _autotile = { 16, 2, 1 };
        runAutotile();
        break;
      case 19:
        check("autotile makes the 12 x 4 sheet on a new layer", _doc->width() == 192 && _doc->height() == 64 && _doc->layers.size() == 2 && imgPx(9 * 16 + 5, 2 * 16 + 5) == BLUE);
        setDocument(Document::blank(200, 100, WHITE));
        _nav.actualSize();
        _nav.centerOn(100, 50);
        _primary = BLACK;
        t.brushSize = 20;
        use(ETool::Spray);
        { ImVec2 a = at(100, 50); push(SDL_EVENT_MOUSE_MOTION, a.x, a.y, 0); push(SDL_EVENT_MOUSE_BUTTON_DOWN, a.x, a.y, SDL_BUTTON_LEFT); }
        break;
      case 24: { ImVec2 a = at(100, 50); push(SDL_EVENT_MOUSE_BUTTON_UP, a.x, a.y, SDL_BUTTON_LEFT); break; }
      case 26: {
        int dots = 0;
        for(int y = 40; y <= 60; y++) for(int x = 90; x <= 110; x++) dots += imgPx(x, y) == BLACK;
        check("spray keeps adding dots while held", dots > 60 && _doc->history.count() == 1 && imgPx(100, 80) == WHITE);
        use(ETool::Shapes);
        t.shapeKind = int(EShapeKind::Free);
        t.shapeDraw = 1;
        ImVec2 a = at(20, 20), b = at(60, 20), c = at(40, 60);
        push(SDL_EVENT_MOUSE_MOTION, a.x, a.y, 0);
        push(SDL_EVENT_MOUSE_BUTTON_DOWN, a.x, a.y, SDL_BUTTON_LEFT);
        push(SDL_EVENT_MOUSE_MOTION, b.x, b.y, 0);
        push(SDL_EVENT_MOUSE_MOTION, c.x, c.y, 0);
        push(SDL_EVENT_MOUSE_BUTTON_UP, c.x, c.y, SDL_BUTTON_LEFT);
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      }
      case 28:
        check("free polygon fills the outline", imgPx(40, 30) == BLACK && imgPx(25, 50) == WHITE && _doc->history.count() == 2);
        // The pixel-art view for the screenshot
        t.symmetry = 3; t.tiled = 3; t.grid = true; t.gridW = 25; t.gridH = 25;
        _cfg.showTilePreview = true; _cfg.showPalette = true;
        use(ETool::Pencil);
        break;
      case 31:
        check("tile preview and palette windows open", ImGui::FindWindowByName("###tilepreview") && ImGui::FindWindowByName("###palette"));
        writeShot(_selftestDir + "/p5-pixelart.png");
        break;
      case 33: {
        // Dragging the vertical axis nub moves the mirror
        ImVec2 a = toScreen(100.0, 0.0), b = toScreen(60.0, 0.0);
        drag(a.x, a.y - 14 * _dpi, b.x, b.y - 14 * _dpi, SDL_BUTTON_LEFT);
        break;
      }
      case 35: {
        check("the symmetry axis moves by its nub", std::abs(t.symX - 60) < 0.6 && _doc->history.count() == 2);
        // Dropped a few screen px off the center, it snaps back to it
        ImVec2 a = toScreen(60.0, 0.0), b = toScreen(100.0, 0.0);
        drag(a.x, a.y - 14 * _dpi, b.x + 3 * _dpi, b.y - 14 * _dpi, SDL_BUTTON_LEFT);
        break;
      }
      case 37:
        check("the symmetry axis snaps to the center", t.symX < 0);
        t.symmetry = 0; t.tiled = 0; t.grid = false; t.symX = t.symY = -1;
        _cfg.showTilePreview = false; _cfg.showPalette = false;
        _round4 = 0;
        _round5 = frame;
        break;
    }
  }

  // Round 5 (phase 6): blend modes on the canvas, adjustments by shortcut, effect dialogs
  if(_round5 && frame > _round5) {
    int f = frame - _round5;
    auto imgPx = [&](int x, int y) { return _doc->active().get(x, y); };
    auto key = [this](SDL_Keycode k, SDL_Scancode sc) {
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.scancode = sc; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
    };
    // The screen color under an image pixel, read back from the frame being drawn
    auto screenPx = [&](double x, double y) {
      ImVec2 p = toScreen(x + 0.5, y + 0.5);
      int fbW, fbH;
      SDL_GetWindowSizeInPixels(_window, &fbW, &fbH);
      unsigned char c[4] = {};
      glReadPixels(int(p.x * d), fbH - 1 - int(p.y * d), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
      return uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | 0xFF000000u;
    };
    auto near = [](uint32_t a, uint32_t b) {
      for(int s = 0; s < 24; s += 8) if(std::abs(int((a >> s) & 0xFF) - int((b >> s) & 0xFF)) > 2) return false;
      return true;
    };
    const uint32_t ORANGE = Color{ 255, 160, 40, 255 }.pack(), GRAY = Color{ 128, 128, 128, 255 }.pack();
    switch(f) {
      case 1: {
        auto doc = Document::blank(200, 100, ORANGE);
        auto top = std::make_unique<Layer>(200, 100, "Multiply");
        for(int y = 0; y < 100; y++) for(int x = 0; x < 100; x++) top->set(x, y, GRAY);
        top->blend = EBlend::Multiply;
        doc->layers.push_back(std::move(top));
        doc->activeLayer = 1;
        setDocument(std::move(doc));
        _nav.actualSize();
        _nav.centerOn(100, 50);
        break;
      }
      case 4: {
        std::vector<uint32_t> row(200);
        flattenRow(*_doc, 50, row.data());
        uint32_t want = blendPixel(ORANGE, GRAY, 255, EBlend::Multiply);
        check("a Multiply layer matches on the canvas and in the flattened image", row[50] == want && near(screenPx(50, 50), want) && near(screenPx(150, 50), ORANGE));
        writeShot(_selftestDir + "/p6-blend.png");
        _doc->activeLayer = 0;
        Command* inv = _commands.find("effect.invert");
        check("adjustments carry Paint.NET shortcuts", inv && !inv->chords.empty() && inv->chords[0] == (ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_I));
        _commands.run("effect.invert");
        break;
      }
      case 6:
        check("Invert Colors applies at once as one step", imgPx(150, 50) == (ORANGE ^ 0x00FFFFFFu) && _doc->history.at(_doc->history.count() - 1).name == "Invert Colors");
        _commands.run("edit.undo");
        _fxRemember.clear();
        _commands.run("effect.curves");
        break;
      case 8:
        check("the curves dialog opens", ImGui::IsPopupOpen("###effect", ImGuiPopupFlags_AnyPopupId) && _fx && _fx->id == "curves");
        writeShot(_selftestDir + "/p6-curves.png");
        // Drag the white point of the curve down: the preview darkens live
        _fxValues.text["curve"] = "0,0 255,128|0,0 255,255|0,0 255,255|0,0 255,255";
        previewEffect();
        break;
      case 10:
        check("curves preview live", (imgPx(150, 50) & 0xFF) > 110 && (imgPx(150, 50) & 0xFF) < 140);
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 12:
        check("Cancel puts the pixels back", imgPx(150, 50) == ORANGE && !_fx);
        _cfg.tools.palette.colors = { BLACK, WHITE, Color{ 255, 0, 0, 255 }.pack() };
        _commands.run("effect.dither");
        break;
      case 14:
        check("the dither dialog opens with its options", ImGui::IsPopupOpen("###effect", ImGuiPopupFlags_AnyPopupId) && _fx && _fx->id == "dither");
        writeShot(_selftestDir + "/p6-dither.png");
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      case 16: {
        bool onlyPalette = true;
        for(int y = 0; y < 100; y += 7) for(int x = 0; x < 200; x += 7) { uint32_t c = imgPx(x, y); onlyPalette &= c == BLACK || c == WHITE || c == Color{ 255, 0, 0, 255 }.pack(); }
        check("dither leaves only palette colors", onlyPalette && _doc->history.at(_doc->history.count() - 1).name == "Dither");
        _cfg.tools.palette = Palette::defaults();
        _commands.run("layer.props");
        break;
      }
      case 18:
        writeShot(_selftestDir + "/p6-layerprops.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 20:
        openEffect("hue-saturation");
        break;
      case 22:
        // Effect dialogs leave the canvas undimmed so the preview can be judged
        check("effect dialogs don't dim the canvas", _fx && _fx->id == "hue-saturation" && ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg].w == 0);
        writeShot(_selftestDir + "/p6-huesat.png");
        key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
        break;
      case 24:
        check("other dialogs still dim", !_fx && ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg].w > 0);
        _round5 = 0;
        _round6 = frame;
        break;
    }
  }

  // Round 6 (phase 7): the example mods, a broken mod, hot reload, layered files
  if(_round6 && frame > _round6) {
    int f = frame - _round6;
    auto at = [&](double x, double y) { return toScreen(x + 0.5, y + 0.5); };
    auto imgPx = [&](int x, int y) { return _doc->active().get(x, y); };
    auto key = [this](SDL_Keycode k, SDL_Scancode sc) {
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.scancode = sc; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
    };
    auto writeFile = [](const std::string& path, const std::string& text) {
      std::filesystem::create_directories(pathFromUtf8(path).parent_path());
      std::ofstream(pathFromUtf8(path), std::ios::binary) << text;
    };
    const uint32_t GREEN = Color{ 0, 200, 0, 255 }.pack();
    std::string userMods = paths::configDir() + "mods/";
    switch(f) {
      case 1: {
        int ok = 0;
        for(const char* id : { "scanlines", "palette-swap", "checker-brush", "qoi-like", "canvas-info" }) if(ModInfo* m = _mods.find(id)) ok += m->loaded && m->error.empty();
        check("the five example mods load", ok == 5);
        setDocument(Document::blank(120, 80, WHITE));
        _nav.actualSize();
        _nav.centerOn(60, 40);
        _fxRemember.clear();
        openEffect("scanlines");
        break;
      }
      case 3:
        check("a shader mod effect previews on the GPU", _fx && _fx->id == "scanlines" && imgPx(10, 0) != WHITE && imgPx(10, 1) == WHITE);
        writeShot(_selftestDir + "/p7-scanlines.png");
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      case 5: {
        check("the shader effect commits", _doc->history.count() == 1 && _doc->history.at(0).name == "Scanlines");
        const ModCommand* swap = nullptr;
        for(auto& c : _mods.commands()) if(c.id == "palette-swap") swap = &c;
        ParamValues v;
        v.num["from"] = double(WHITE); v.num["to"] = double(GREEN); v.num["tolerance"] = 0;
        if(swap) runModCommand(*swap, v);
        check("a Lua command edits the image as one step", swap && imgPx(10, 1) == GREEN && imgPx(10, 0) != GREEN && _doc->history.count() == 2);
        _commands.run("edit.undo");
        check("undo takes the whole command back", imgPx(10, 1) == WHITE);
        bool found = false;
        for(auto& t : _tools) if(t->id == ETool::Mod && std::string(t->name) == "Checker Brush") found = true;
        check("a Lua tool joins the tool strip", found);
        for(int i = 0; i < int(_tools.size()); i++) if(_tools[i]->id == ETool::Mod) selectTool(i);
        // A clean image: the scanlines above darkened every other row
        setDocument(Document::blank(120, 80, WHITE));
        _primary = BLACK;
        _cfg.tools.brushSize = 9;
        _cfg.tools.antialias = false;
        { ImVec2 a = at(30, 60), b = at(90, 60); drag(a.x, a.y, b.x, b.y, SDL_BUTTON_LEFT); }
        break;
      }
      case 7:
        check("the Lua ink paints a checkerboard", imgPx(60, 60) == BLACK && imgPx(61, 60) == WHITE && imgPx(61, 61) == BLACK);
        _nav.zoomAt(4, toScreen(60.0, 60.0).x * d, toScreen(60.0, 60.0).y * d);
        // A broken mod in the user's folder shows its error
        writeFile(userMods + "broken/mod.json", R"({ "name": "Broken", "kind": "lua", "entry": "main.lua", "api": 1 })");
        writeFile(userMods + "broken/main.lua", "david.effect{ id = 'x', run = function( end }\n");
        writeFile(userMods + "hot/mod.json", R"({ "name": "Hot", "kind": "lua", "entry": "main.lua", "api": 1 })");
        writeFile(userMods + "hot/main.lua", "david.effect{ id = 'hot-one', name = 'Hot One', run = function(img, p) end }\n");
        _mods.scan({ paths::exeDir() + "mods", userMods });
        _mods.apply(_cfg.mods);
        break;
      case 9: {
        writeShot(_selftestDir + "/p7-checker.png");
        ModInfo* b = _mods.find("broken");
        check("a broken mod shows its error", b && !b->loaded && !b->error.empty());
        check("a user mod loads", findEffect("hot-one") != nullptr);
        writeFile(userMods + "hot/main.lua", "david.effect{ id = 'hot-two', name = 'Hot Two', run = function(img, p) end }\n");
        // Make sure the time stamp moves even on coarse file systems
        std::error_code ec;
        auto p = pathFromUtf8(userMods + "hot/main.lua");
        std::filesystem::last_write_time(p, std::filesystem::last_write_time(p, ec) + std::chrono::seconds(2), ec);
        _mods.poll();
        check("editing a mod reloads it", findEffect("hot-two") != nullptr && findEffect("hot-one") == nullptr);
        _showSettings = true;
        _settingsPage = 5;
        break;
      }
      case 12:
        writeShot(_selftestDir + "/p7-mods.png");
        _showSettings = false;
        {
          // A layered image through .david and the native plugin's format
          auto doc = Document::blank(40, 30, WHITE);
          auto top = std::make_unique<Layer>(40, 30, "Top");
          top->set(5, 5, GREEN);
          top->blend = EBlend::Darken;
          doc->layers.push_back(std::move(top));
          setDocument(std::move(doc));
        }
        check("save as .david", saveTo(_selftestDir + "/layers.david"));
        check("save with a plugin format", saveTo(_selftestDir + "/flat.qlike"));
        openFile(_selftestDir + "/layers.david");
        check("a .david file opens with its layers", _doc->layers.size() == 2 && _doc->layers[1]->blend == EBlend::Darken && _doc->layers[1]->get(5, 5) == GREEN);
        openFile(_selftestDir + "/flat.qlike");
        check("a plugin format opens", _doc->layers.size() == 1 && _doc->width() == 40 && imgPx(5, 5) == GREEN);
        // Mod windows: the example's tool window and dialog, and a user mod whose window edits once and one that fails
        writeFile(userMods + "wintest/mod.json", R"({ "name": "Win Test", "kind": "lua", "entry": "main.lua", "api": 1 })");
        writeFile(userMods + "wintest/main.lua",
          "local done = false\n"
          "david.window{ id = 'edit', title = 'Win Edit', draw = function(ui, doc) ui:text('hi'); if not done then done = true; doc:layer().name = 'Renamed' end end }\n"
          "david.window{ id = 'bad', title = 'Win Bad', kind = 'dialog', draw = function(ui, doc) nothing() end }\n");
        _mods.scan({ paths::exeDir() + "mods", userMods });
        _mods.apply(_cfg.mods);
        _cfg.modWindows.clear();
        for(auto k : { "canvas-info/canvas-info", "canvas-info/rename", "wintest/edit", "wintest/bad" }) showModWindow(k, true);
        break;
      case 15: {
        ImGuiWindow* info = ImGui::FindWindowByName("###mod-canvas-info/canvas-info");
        ImGuiWindow* rename = ImGui::FindWindowByName("###mod-canvas-info/rename");
        check("a mod tool window docks with the other windows", info && info->Active && info->DockNode != nullptr);
        check("a mod dialog floats", rename && rename->Active && rename->DockNode == nullptr);
        check("a window's edit is one undo step", _doc->active().name == "Renamed" && _doc->history.count() == 1 && _doc->history.at(0).name == "Win Edit");
        check("a failing window shows its error", _modWindowError.count("wintest/bad") == 1 && _modWindowError.count("canvas-info/canvas-info") == 0);
        showModWindow("wintest/bad", false);
        showModWindow("wintest/edit", false);
        if(info) ImGui::FocusWindow(info);
        break;
      }
      case 17: {
        writeShot(_selftestDir + "/p7-windows.png");
        _cfg.modWindows.clear();
        std::error_code ec;
        std::filesystem::remove_all(pathFromUtf8(userMods + "wintest"), ec);
        _round6 = 0;
        _round7 = frame;
        break;
      }
    }
  }

  // Round 7 (animation): frames, the Timeline, onion skin, layer onion, playback, effects on every frame
  if(_round7 && frame > _round7) {
    int f = frame - _round7;
    float d = ImGui::GetIO().DisplayFramebufferScale.x;
    auto screenPx = [&](double x, double y) {
      ImVec2 p = toScreen(x + 0.5, y + 0.5);
      int fbW, fbH;
      SDL_GetWindowSizeInPixels(_window, &fbW, &fbH);
      unsigned char c[4] = {};
      glReadPixels(int(p.x * d), fbH - 1 - int(p.y * d), 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, c);
      return uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | 0xFF000000u;
    };
    auto block = [&](Layer& l, int x0, int y0, uint32_t c) { for(int y = y0; y < y0 + 10; y++) for(int x = x0; x < x0 + 10; x++) l.set(x, y, c); };
    auto gray = [](uint32_t c) { return int(c & 0xFF); };
    switch(f) {
      case 1: {
        // Frame 1: a black block on a clear image; frame 2 (new, empty) gets a block somewhere else
        setDocument(Document::blank(80, 60, 0));
        _nav.actualSize();
        _nav.centerOn(40, 30);
        block(_doc->active(), 10, 10, BLACK);
        _cfg.showTimeline = true;
        _cfg.showAnimPreview = true;
        _commands.run("anim.new");
        check("New Frame adds an empty frame after the current one", _doc->frameCount() == 2 && _doc->activeFrame == 1 && _doc->active().celEmpty(1));
        block(_doc->active(), 50, 10, BLACK);
        break;
      }
      case 3:
        check("the Timeline and Animation Preview open", ImGui::FindWindowByName("###timeline") && ImGui::FindWindowByName("###animpreview"));
        _cfg.anim.onion = false;
        break;
      case 4:
        // Without onion skin frame 1's block isn't on screen while frame 2 shows (the checkerboard is there);
        // the same spot is compared with onion skin on next
        _probeGray = gray(screenPx(15, 15));
        check("frame 2 shows only its own pixels", _probeGray > 5 && gray(screenPx(55, 15)) < 5);
        _cfg.anim.onion = true;
        break;
      case 6:
        // Onion skin: frame 1's block shows faded (20 %)
        check("onion skin ghosts the previous frame", gray(screenPx(15, 15)) < _probeGray * 0.9 && gray(screenPx(15, 15)) > 5 && gray(screenPx(55, 15)) < 5);
        _cfg.anim.onionTint = true;
        break;
      case 8: {
        uint32_t c = screenPx(15, 15);
        check("tinted onion: the frame before is reddish", int(c & 0xFF) > int((c >> 16) & 0xFF) + 10);
        writeShot(_selftestDir + "/p8-onion.png");
        _cfg.anim.onion = false;
        _cfg.anim.onionTint = false;
        // Layer onion: three layers with blocks over a clear background; the middle one is active
        auto doc = Document::blank(80, 60, 0);
        for(int i = 0; i < 3; i++) {
          auto l = std::make_unique<Layer>(80, 60, "L" + std::to_string(i + 1));
          block(*l, 10 + i * 20, 30, BLACK);
          doc->layers.push_back(std::move(l));
        }
        doc->activeLayer = 2;
        setDocument(std::move(doc));
        _nav.actualSize();
        _nav.centerOn(40, 30);
        _cfg.anim.layerOnion = true;
        _cfg.anim.hideOtherLayers = true;
        _cfg.anim.layersBelow = 1;
        _cfg.anim.layersAbove = 0;
        _cfg.anim.onionOpacity = 60;
        break;
      }
      case 10:
        // Layers: Background, L1 (block at x 10), L2 active (30), L3 (50). One layer below: L1 ghosted, L3 out of range.
        // Hidden layers leave the checkerboard, so the out-of-range spot is compared with itself in the next step.
        check("layer onion: the active layer as is", gray(screenPx(35, 35)) < 5);
        check("layer onion: the layer below is ghosted", gray(screenPx(15, 35)) > 5);
        _probeGray = gray(screenPx(55, 35));
        check("layer onion: layers out of range are hidden", _probeGray > 5);
        _cfg.anim.hideOtherLayers = false;
        break;
      case 12:
        // Dimmed to half the onion opacity: darker than the checkerboard alone, not black
        check("layer onion: out of range layers can be dimmed instead", gray(screenPx(55, 35)) < _probeGray * 0.85 && gray(screenPx(55, 35)) > 5);
        _cfg.anim.layerOnion = false;
        _cfg.anim.onionOpacity = 20;
        break;
      case 13: {
        // Back to frames: three frames, play advances by time
        check("without the layer onion every layer shows", gray(screenPx(55, 35)) < 5);
        auto doc = Document::blank(40, 30, WHITE);
        setDocument(std::move(doc));
        _commands.run("anim.duplicate");
        _commands.run("anim.duplicate");
        setFrameRate(*_doc, 60, "");
        _doc->setActiveFrame(0);
        check("Duplicate Frame copies the frame", _doc->frameCount() == 3 && _doc->layers[0]->getAt(2, 5, 5) == WHITE);
        _commands.run("anim.play");
        _playLast = SDL_GetTicks() - 20;
        break;
      }
      case 16:
        check("playback moves the frames", _player.playing() && _doc->activeFrame != 0);
        _commands.run("anim.play");
        _doc->setActiveFrame(0);
        // Invert with "All frames": every frame changes, as one undo step
        _fxAllFrames = true;
        {
          int steps = _doc->history.count();
          openEffect("invert");
          check("an effect on all frames", _doc->layers[0]->getAt(0, 5, 5) == BLACK && _doc->layers[0]->getAt(2, 5, 5) == BLACK && _doc->history.count() == steps + 1);
          _doc->history.undo(*_doc);
          check("its undo puts every frame back", _doc->layers[0]->getAt(1, 5, 5) == WHITE && _doc->layers[0]->getAt(2, 5, 5) == WHITE);
        }
        _fxAllFrames = false;
        _commands.run("anim.tag");
        break;
      case 18:
        // (by any-popup: popup names need a current window, and this runs after the frame)
        check("New Tag tags the frame and opens its popup", _doc->tags.size() == 1 && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
        _tagEdit = -1;  // the popup closes itself
        writeShot(_selftestDir + "/p8-timeline.png");
        _cfg.showTimeline = false;
        _cfg.showAnimPreview = false;
        _round7 = 0;
        _round8 = frame;
        break;
    }
  }

  // Round 8 (1.1.0 fixes): copy and paste, the larger-paste question, symmetry on Line and Gradient
  if(_round8 && frame > _round8) {
    int f = frame - _round8;
    auto at = [&](double x, double y) { return toScreen(x + 0.5, y + 0.5); };
    auto imgPx = [&](int x, int y) { return _doc->active().get(x, y); };
    auto key = [this](SDL_Keycode k, SDL_Scancode sc) {
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.scancode = sc; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
    };
    switch(f) {
      case 1: {
        setDocument(Document::blank(100, 80, WHITE));
        _nav.actualSize();
        _nav.centerOn(50, 40);
        auto sel = std::make_unique<Mask>(100, 80);
        for(int y = 10; y < 20; y++) { sel->fillSpan(y, 10, 19, 255); for(int x = 10; x < 20; x++) _doc->active().set(x, y, RED); }
        _doc->selection = std::move(sel);
        _commands.run("edit.copy");
        check("Copy puts the selection on the clipboard as PNG and BMP", SDL_HasClipboardData("image/png") && SDL_HasClipboardData("image/bmp"));
        _commands.run("edit.deselect");
        int steps = _doc->history.count();
        _commands.run("edit.paste");
        check("Paste floats the pixels on Move Selected Pixels", tool().id == ETool::MovePixels && tool().busy());
        tool().commit(*this);
        check("the paste lands at the visible top-left as one step", imgPx(0, 0) == RED && imgPx(9, 9) == RED && imgPx(10, 10) == RED && imgPx(25, 25) == WHITE
              && _doc->history.count() == steps + 1 && _doc->history.at(_doc->history.count() - 1).name == "Paste");
        _commands.run("edit.pasteLayer");
        tool().commit(*this);
        check("Paste into New Layer adds a layer", _doc->layers.size() == 2 && _doc->active().get(5, 5) == RED);
        // Larger than the canvas: the question comes first, then the canvas grows to fit
        setDocument(Document::blank(5, 5, WHITE));
        _commands.run("edit.paste");
        check("a paste larger than the canvas asks first", _popup == "###pastebig" && !tool().busy());
        _popup.clear();
        placePaste(true);
        tool().commit(*this);
        check("Expand canvas grows it to the pasted size", _doc->width() == 10 && _doc->height() == 10 && imgPx(9, 9) == RED);
        break;
      }
      case 2: {
        // Symmetry left-right: a line on the left half shows up mirrored on the right
        setDocument(Document::blank(100, 80, WHITE));
        _nav.actualSize();
        _nav.centerOn(50, 40);
        _primary = BLACK;
        _cfg.tools.symmetry = 1; _cfg.tools.symX = -1;
        _cfg.tools.brushSize = 2; _cfg.tools.antialias = false; _cfg.tools.pixelPerfect = false; _cfg.tools.dash = 0;
        use(ETool::Line);
        ImVec2 a = at(10, 30), b = at(30, 30);
        drag(a.x, a.y, b.x, b.y, SDL_BUTTON_LEFT);
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      }
      case 4: {
        check("Line follows symmetry", imgPx(20, 30) == BLACK && imgPx(79, 30) == BLACK && imgPx(50, 30) == WHITE);
        _cfg.tools.gradType = 0;
        _primary = BLACK; _secondary = WHITE;
        use(ETool::Gradient);
        ImVec2 a = at(0, 60), b = at(49, 60);
        drag(a.x, a.y, b.x, b.y, SDL_BUTTON_LEFT);
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      }
      case 6: {
        int l = int(imgPx(10, 60) & 0xFF), r = int(imgPx(89, 60) & 0xFF);
        check("Gradient follows symmetry", std::abs(l - r) <= 2 && l < 100);
        _cfg.tools.symmetry = 0;
        _round8 = 0;
        _round9 = frame;
        break;
      }
    }
  }

  // Round 9 (1.2.0 vector layers): shapes as objects, the Mouse and Node tools, the raster question, text,
  // trace, SVG export and import, object copy and paste
  if(_round9 && frame > _round9) {
    int f = frame - _round9;
    auto at = [&](double x, double y) { return toScreen(x + 0.5, y + 0.5); };
    auto imgPx = [&](int x, int y) { return _doc->active().get(x, y); };
    auto key = [this](SDL_Keycode k, SDL_Scancode sc) {
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = k; e.key.scancode = sc; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
    };
    auto dragImg = [&](double x0, double y0, double x1, double y1) { ImVec2 a = at(x0, y0), b = at(x1, y1); drag(a.x, a.y, b.x, b.y, SDL_BUTTON_LEFT); };
    auto clickImg = [&](double x, double y) {
      ImVec2 a = at(x, y);
      push(SDL_EVENT_MOUSE_MOTION, a.x, a.y, 0);
      push(SDL_EVENT_MOUSE_BUTTON_DOWN, a.x, a.y, SDL_BUTTON_LEFT);
      push(SDL_EVENT_MOUSE_BUTTON_UP, a.x, a.y, SDL_BUTTON_LEFT);
    };
    auto objects = [&]() -> std::vector<VecObject>& { return vectorTree(_doc->active(), _doc->activeFrame).objects; };
    switch(f) {
      case 1:
        _cfg.betaVector = true;
        setDocument(Document::blank(200, 150, WHITE));
        _nav.actualSize();
        _nav.centerOn(100, 75);
        _commands.run("layer.addVector");
        check("Add New Vector Layer makes a vector layer", _doc->layers.size() == 2 && _doc->active().kind == ELayerKind::Vector && vectorMode());
        use(ETool::Shapes);
        _cfg.tools.shapeKind = int(EShapeKind::Rect);
        _cfg.tools.shapeDraw = 2;
        _cfg.tools.brushSize = 4;
        _primary = BLACK; _secondary = RED;
        dragImg(20, 20, 80, 60);
        key(SDLK_RETURN, SDL_SCANCODE_RETURN);
        break;
      case 3: {
        auto& objs = objects();
        check("Shapes on a vector layer makes an object", objs.size() == 1 && objs[0].kind == EVecKind::Rect && objs[0].fill.color == RED && objs[0].stroke.width == 4);
        check("its pixels follow on commit", imgPx(50, 40) == RED && imgPx(22, 40) == BLACK && (imgPx(10, 10) >> 24) == 0);
        use(ETool::Mouse);
        dragImg(50, 40, 60, 50);
        break;
      }
      case 5: {
        auto& objs = objects();
        check("the Mouse tool picks and moves objects", _doc->selectedObjects.size() == 1 && !objs.empty() && std::abs(objs[0].xf.e - 10) < 0.6 && std::abs(objs[0].xf.f - 10) < 0.6
              && imgPx(85, 65) == RED && _doc->history.at(_doc->history.count() - 1).name == "Move Objects");
        writeShot(_selftestDir + "/p9-vector-select.png");
        use(ETool::Node);
        dragImg(30, 30, 10, 10);
        break;
      }
      case 7: {
        auto& objs = objects();
        check("the Node tool turns the shape into a path and moves its node", !objs.empty() && objs[0].kind == EVecKind::Path && (imgPx(14, 14) >> 24) > 0
              && _doc->history.at(_doc->history.count() - 1).name == "Edit Nodes");
        writeShot(_selftestDir + "/p9-vector-nodes.png");
        // A raster tool on a vector layer asks first
        use(ETool::Brush);
        clickImg(150, 120);
        break;
      }
      case 9: {
        check("a raster tool on a vector layer asks first", ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && objects().size() == 1);
        ImGui::ClosePopupToLevel(0, false);
        _afterRaster = nullptr;
        break;
      }
      case 10:
        // Text as an object (any system font the Text tool finds)
        use(ETool::Text);
        clickImg(100, 90);
        break;
      case 12: {
        for(char c : std::string("Hi")) tool().onText(*this, unsigned(c));
        tool().commit(*this);
        auto& objs = objects();
        bool text = objs.size() == 2 && objs[1].kind == EVecKind::Text && objs[1].text == "Hi";
        DRect b = text ? bounds(objs[1], true) : DRect{};
        check("Text on a vector layer makes a text object with outlines", text && !b.empty() && b.width() > 5);
        // Copy and paste objects
        use(ETool::Mouse);
        _doc->selectedObjects = { objs[0].id };
        _commands.run("edit.copy");
        _commands.run("edit.paste");
        check("copied objects paste as objects", objects().size() == 3 && _doc->selectedObjects.size() == 1 && _doc->selectedObjects[0] != objects()[0].id);
        // SVG export and open
        exportSvg(_selftestDir + "/vector.svg");
        std::string err;
        auto back = loadImage(_selftestDir + "/vector.svg", err);
        bool ok = back && back->layers.size() == 1 && back->layers[0]->kind == ELayerKind::Vector;
        const VecTree* t = ok ? vectorTreeOf(*back->layers[0], 0) : nullptr;
        // The file has the Background as an image (skipped on reading) and the vector layer's objects in a group
        check("Export SVG writes the image, Open reads its objects", t && !t->objects.empty());
        // Trace the background (one white region)
        _doc->activeLayer = 0;
        _commands.run("image.trace");
        check("Trace Pixels makes a vector layer of shapes", _doc->layers.size() == 3 && _doc->active().kind == ELayerKind::Vector && objects().size() == 1
              && objects()[0].fill.color == WHITE);
        _commands.run("edit.undo");
        _doc->activeLayer = 1;
        _nav.zoomAt(4, _nav.view.vpX + _nav.view.vpW / 2.0, _nav.view.vpY + _nav.view.vpH / 2.0);
        break;
      }
      case 14:
        writeShot(_selftestDir + "/p9-vector-zoom.png");
        _cfg.vectorPixels = true;
        break;
      case 16:
        writeShot(_selftestDir + "/p9-vector-pixels.png");
        _cfg.vectorPixels = false;
        _cfg.betaVector = false;
        {
          bool hidden = false;
          for(auto& t : _tools) if(t->id == ETool::Node) hidden = toolHidden(*t);
          check("with vector support off the Node tool hides", hidden && !vectorMode());
        }
        _round9 = 0;
        _round10 = frame;
        break;
    }
  }

  // Round 10 (1.2.1): grid snap while moving, the vector tool row, boolean union, double-click into the Node
  // tool and Esc back, the vector Pencil, the Object window
  if(_round10 && frame > _round10) {
    int f = frame - _round10;
    auto at = [&](double x, double y) { return toScreen(x, y); };
    auto objects = [&]() -> std::vector<VecObject>& { return vectorTree(_doc->active(), _doc->activeFrame).objects; };
    auto rect = [](double x, double y, double w, double h) {
      VecObject o;
      o.id = newObjectId(); o.kind = EVecKind::Rect; o.x = x; o.y = y; o.w = w; o.h = h;
      o.fill = VecPaint::solid(RED);
      return o;
    };
    switch(f) {
      case 1: {
        _cfg.betaVector = true;
        setDocument(Document::blank(200, 150, WHITE));
        _nav.actualSize();
        _nav.centerOn(100, 75);
        _commands.run("layer.addVector");
        {
          VectorChange ch(*_doc);
          ch.tree().objects = { rect(20, 20, 40, 30), rect(40, 30, 40, 30) };
          ch.tree().touch();
          ch.commit("Add", "");
        }
        use(ETool::Mouse);
        _doc->selectedObjects = { objects()[0].id, objects()[1].id };
        check("the vector tool row shows on a vector layer", vectorRowShown());
        _commands.run("object.union");
        auto& objs = objects();
        bool one = objs.size() == 1 && objs[0].kind == EVecKind::Path && objs[0].paths.size() == 1;
        DRect b = one ? bounds(objs[0], true) : DRect{};
        check("Union makes one path of the two shapes", one && std::abs(b.x0 - 20) < 0.1 && std::abs(b.x1 - 80) < 0.1 && std::abs(b.y1 - 60) < 0.1
              && objs[0].paths[0].nodes.size() == 8 && _doc->history.at(_doc->history.count() - 1).name == "Union");
        // Grid snap: the moved box lands on the 10 px grid
        _cfg.tools.grid = _cfg.tools.gridSnap = true;
        _cfg.tools.gridW = _cfg.tools.gridH = 10;
        _cfg.tools.gridX = _cfg.tools.gridY = 0;
        ImVec2 a = at(30.5, 30.5), e = at(43.5, 34.5);
        drag(a.x, a.y, e.x, e.y, SDL_BUTTON_LEFT);
        break;
      }
      case 3: {
        DRect b = objects().empty() ? DRect{} : bounds(objects()[0], true);
        check("moving objects snaps them to the grid", !b.empty() && std::abs(b.x0 - 30) < 0.01 && std::abs(b.y0 - 20) < 0.01);
        _cfg.tools.grid = _cfg.tools.gridSnap = false;
        // Double-click an object: straight into the Node tool
        ImVec2 p = at(50, 40);
        push(SDL_EVENT_MOUSE_MOTION, p.x, p.y, 0);
        SDL_Event e{};
        e.button.x = p.x; e.button.y = p.y; e.button.button = SDL_BUTTON_LEFT; e.button.clicks = 2;
        e.type = SDL_EVENT_MOUSE_BUTTON_DOWN; e.button.down = true;
        SDL_PushEvent(&e);
        e.type = SDL_EVENT_MOUSE_BUTTON_UP; e.button.down = false;
        SDL_PushEvent(&e);
        break;
      }
      case 5:
        check("double-clicking an object opens the Node tool on it", tool().id == ETool::Node && _doc->selectedObjects.size() == 1);
        tool().onKey(*this, ImGuiKey_Escape);
        check("Esc goes back to the Mouse tool", tool().id == ETool::Mouse);
        // The vector Pencil: a freehand path
        use(ETool::Pencil);
        {
          ImVec2 a = at(120, 100), m = at(150, 120), e = at(180, 100);
          push(SDL_EVENT_MOUSE_MOTION, a.x, a.y, 0);
          push(SDL_EVENT_MOUSE_BUTTON_DOWN, a.x, a.y, SDL_BUTTON_LEFT);
          for(int i = 1; i <= 20; i++) {
            float t = i / 20.f;
            push(SDL_EVENT_MOUSE_MOTION, a.x + (e.x - a.x) * t, a.y + (m.y - a.y) * 4 * t * (1 - t), 0);
          }
          push(SDL_EVENT_MOUSE_BUTTON_UP, e.x, e.y, SDL_BUTTON_LEFT);
        }
        break;
      case 7: {
        auto& objs = objects();
        bool path = objs.size() == 2 && objs[1].kind == EVecKind::Path && objs[1].paths.size() == 1 && !objs[1].paths[0].closed && objs[1].stroke.paint.visible();
        check("the Pencil on a vector layer draws a fitted path", path && objs[1].paths[0].nodes.size() >= 2 && objs[1].paths[0].nodes.size() <= 6);
        use(ETool::Mouse);
        _doc->selectedObjects = { objs[0].id };
        break;
      }
      case 9: {
        ImGuiWindow* w = ImGui::FindWindowByName("###object");
        ImGuiWindow* c = ImGui::FindWindowByName("###colors");
        check("the Object window opens docked with Colors", w && c && (_cfg.floatingWindows || (w->DockId && w->DockId == c->DockId)));
        ImGui::FocusWindow(w);
        // Raster layers too: Move Selection snaps the moved box to the grid
        _doc->activeLayer = 0;
        _commands.run("edit.selectAll");
        use(ETool::MoveSelection);
        _cfg.tools.grid = _cfg.tools.gridSnap = true;
        ImVec2 a = at(50, 50), e = at(53, 56);
        drag(a.x, a.y, e.x, e.y, SDL_BUTTON_LEFT);
        break;
      }
      case 10: {
        tool().commit(*this);
        IRect b = _doc->selection->bounds();
        check("Move Selection snaps to the grid", b.x0 == 0 && b.y0 == 10);
        _cfg.tools.grid = _cfg.tools.gridSnap = false;
        _doc->activeLayer = 1;
        use(ETool::Mouse);
        break;
      }
      case 11:
        writeShot(_selftestDir + "/p10-object-window.png");
        _cfg.betaVector = false;
        _round10 = 0;
        break;
    }
  }

  // One round on its own: it starts at once, and the rest is skipped
  if(_selftestOnly) {
    if(frame == 3) { if(_selftestOnly == 8) _round8 = frame; else if(_selftestOnly == 9) _round9 = frame; else if(_selftestOnly == 10) _round10 = frame; }
    return;
  }
  switch(frame) {
    #pragma region [ Phase 1: pencil, undo, zoom, save ]
    case 3: _nav.actualSize(); _secondary = RED; use(ETool::Pencil); break;
    case 5: push(SDL_EVENT_MOUSE_MOTION, cx, cy, 0); push(SDL_EVENT_MOUSE_BUTTON_DOWN, cx, cy, SDL_BUTTON_LEFT); break;
    case 6: push(SDL_EVENT_MOUSE_MOTION, cx + 300, cy, 0); break;
    case 7: push(SDL_EVENT_MOUSE_MOTION, cx + 300, cy + 100, 0); break;
    case 8: push(SDL_EVENT_MOUSE_BUTTON_UP, cx + 300, cy + 100, SDL_BUTTON_LEFT); break;
    case 10:
      check("stroke is one history step", _doc->history.count() == 1);
      check("start pixel painted", pixelAt(cx, cy) == _primary);
      check("corner pixel painted", pixelAt(cx + 300, cy) == _primary);
      check("end pixel painted", pixelAt(cx + 300, cy + 100) == _primary);
      check("modified", _doc->modified());
      _commands.run("edit.undo");
      break;
    case 12:
      check("undo clears stroke", pixelAt(cx + 300, cy) != _primary && !_doc->modified());
      _commands.run("edit.redo");
      break;
    case 14:
      check("redo restores stroke", pixelAt(cx + 300, cy) == _primary);
      drag(cx - 100, cy, cx - 100, cy + 50, SDL_BUTTON_RIGHT);
      break;
    case 16:
      check("right button paints secondary", _doc->history.count() == 2 && pixelAt(cx - 100, cy + 25) == _secondary);
      for(int i = 0; i < 4; i++) _nav.zoomStep(1, (cx + 300) * d, cy * d);
      break;
    case 18: {
      check("zoom keeps the point under the cursor", pixelAt(cx + 300, cy) == _primary);
      int px = ix(cx + 300), py = iy(cy);
      std::string path = _selftestDir + "/selftest-save.png", err;
      check("save", saveTo(path));
      auto back = png::load(path, err);
      check("saved file reloads with the stroke", back && back->active().get(px, py) == _primary);
      check("clean after save", !_doc->modified());
      setDocument(Document::blank(800, 600, WHITE));
      _nav.actualSize();
      break;
    }
    #pragma endregion

    #pragma region [ Phase 2: selection, brush, move, bucket, picker, layers ]
    case 20: use(ETool::RectSelect); drag(cx - 50, cy - 50, cx + 50, cy + 50, SDL_BUTTON_LEFT); break;
    case 22: {
      IRect b = _doc->selection->bounds();
      check("rectangle select", _doc->hasSelection() && b.width() == 100 && b.height() == 100);
      use(ETool::Brush);
      _cfg.tools.brushSize = 9;
      _primary = HALF;
      // Back and forth over the same path: one stroke must not build up
      push(SDL_EVENT_MOUSE_MOTION, cx - 120, cy + 20, 0);
      push(SDL_EVENT_MOUSE_BUTTON_DOWN, cx - 120, cy + 20, SDL_BUTTON_LEFT);
      push(SDL_EVENT_MOUSE_MOTION, cx + 120, cy + 20, 0);
      push(SDL_EVENT_MOUSE_MOTION, cx - 120, cy + 20, 0);
      push(SDL_EVENT_MOUSE_BUTTON_UP, cx - 120, cy + 20, SDL_BUTTON_LEFT);
      break;
    }
    case 24: {
      Color in = Color::unpack(pixelAt(cx - 20, cy + 20));
      check("brush paints inside the selection", in.b > 100 && in.r < 140);
      check("brush stays outside the selection", pixelAt(cx - 80, cy + 20) == WHITE);
      check("overlapping stroke has even opacity", pixelAt(cx - 20, cy + 20) == pixelAt(cx + 20, cy + 20));
      _primary = BLACK;
      use(ETool::MovePixels);
      drag(cx, cy, cx + 200, cy, SDL_BUTTON_LEFT);
      break;
    }
    case 26:
      check("move session is open", tool().busy());
      _commands.run("edit.deselect");  // commits the move first
      break;
    case 28:
      check("move committed as one step", _doc->history.count() >= 2 && _doc->history.at(_doc->history.count() - 2).name == "Move Selected Pixels");
      check("moved pixels arrived", Color::unpack(pixelAt(cx + 180, cy + 20)).b > 100);
      check("source left transparent", pixelAt(cx - 20, cy + 20) == 0);
      check("deselect", !_doc->hasSelection());
      use(ETool::Bucket);
      _cfg.tools.tolerance = 0;
      _primary = RED;
      push(SDL_EVENT_MOUSE_MOTION, cx - 20, cy + 20, 0);
      push(SDL_EVENT_MOUSE_BUTTON_DOWN, cx - 20, cy + 20, SDL_BUTTON_LEFT);
      push(SDL_EVENT_MOUSE_BUTTON_UP, cx - 20, cy + 20, SDL_BUTTON_LEFT);
      break;
    case 30:
      check("bucket fills the hole", pixelAt(cx - 49, cy - 49) == RED && pixelAt(cx - 51, cy - 51) == WHITE);
      use(ETool::Picker);
      push(SDL_EVENT_MOUSE_MOTION, cx - 300, cy - 100, 0);
      push(SDL_EVENT_MOUSE_BUTTON_DOWN, cx - 300, cy - 100, SDL_BUTTON_RIGHT);
      push(SDL_EVENT_MOUSE_BUTTON_UP, cx - 300, cy - 100, SDL_BUTTON_RIGHT);
      break;
    case 32:
      check("picker sets the secondary color", _secondary == pixelAt(cx - 300, cy - 100));
      _commands.run("layer.add");
      break;
    case 34:
      check("add layer", _doc->layers.size() == 2 && _doc->activeLayer == 1);
      use(ETool::Pencil);
      _primary = BLACK;
      drag(cx, cy - 150, cx + 50, cy - 150, SDL_BUTTON_LEFT);
      break;
    case 36:
      check("paint on the new layer only", pixelAt(cx + 10, cy - 150) == BLACK && _doc->layers[0]->get(ix(cx + 10), iy(cy - 150)) == WHITE);
      _commands.run("layer.merge");
      break;
    case 38:
      check("merge down", _doc->layers.size() == 1 && pixelAt(cx + 10, cy - 150) == BLACK);
      _commands.run("edit.undo");
      break;
    case 40:
      check("undo merge", _doc->layers.size() == 2);
      _commands.run("edit.selectAll");
      _commands.run("edit.invertSel");
      break;
    case 42:
      check("select all, invert", !_doc->hasSelection());
      use(ETool::RectSelect);
      drag(cx - 200, cy - 200, cx - 100, cy - 100, SDL_BUTTON_LEFT);
      break;
    case 44: {
      auto key = [this](SDL_Keycode k, SDL_Scancode sc, bool down) {
        SDL_Event e{};
        e.key.windowID = SDL_GetWindowID(_window);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = k; e.key.scancode = sc; e.key.down = down;
        SDL_PushEvent(&e);
      };
      use(ETool::Brush);
      key(SDLK_S, SDL_SCANCODE_S, true); key(SDLK_S, SDL_SCANCODE_S, false);
      break;
    }
    case 46: {
      check("S selects Rectangle Select", tool().id == ETool::RectSelect);
      SDL_Event e{};
      e.key.windowID = SDL_GetWindowID(_window);
      e.type = SDL_EVENT_KEY_DOWN; e.key.key = SDLK_S; e.key.scancode = SDL_SCANCODE_S; e.key.down = true;
      SDL_PushEvent(&e);
      e.type = SDL_EVENT_KEY_UP; e.key.down = false;
      SDL_PushEvent(&e);
      break;
    }
    case 48:
      check("S again cycles the selection shape", tool().id == ETool::RectSelect && _cfg.tools.selectShape == 1);
      _cfg.tools.selectShape = 0;
      _cfg.showProbe = true;
      break;
    #pragma endregion

    #pragma region [ Phase 3: settings, themes, shortcuts ]
    case 50: {
      _cfg.tools.brushSize = 33; _cfg.checkerSize = 12; _cfg.wheelZooms = true;
      _cfg.shortcuts = { { "edit.undo", { "Ctrl+U" } } };
      std::string p = _selftestDir + "/settings-roundtrip.json", err;
      Settings back;
      check("settings round trip", _cfg.save(p, err) && back.load(p, err) && back.dump() == _cfg.dump());
      _cfg.checkerSize = 8; _cfg.wheelZooms = false; _cfg.shortcuts.clear();
      ImGuiKeyChord ch = ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Equal;
      check("chord storage round trip", Commands::parseChord(Commands::chordToString(ch)) == ch);
      // Remap Undo to Ctrl+U, then press it
      Command* undo = _commands.find("edit.undo");
      _commands.releaseChord(ImGuiMod_Ctrl | ImGuiKey_U, "edit.undo");
      undo->chords = { ImGuiMod_Ctrl | ImGuiKey_U };
      break;
    }
    case 52: {
      // A new chord's route is live from the frame after it's first requested
      _settingsSeen = _doc->history.index();
      auto key = [this](SDL_Keycode k, SDL_Scancode sc, bool down, SDL_Keymod mod) {
        SDL_Event e{};
        e.key.windowID = SDL_GetWindowID(_window);
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.key = k; e.key.scancode = sc; e.key.down = down; e.key.mod = mod;
        SDL_PushEvent(&e);
      };
      key(SDLK_LCTRL, SDL_SCANCODE_LCTRL, true, SDL_KMOD_LCTRL);
      key(SDLK_U, SDL_SCANCODE_U, true, SDL_KMOD_LCTRL);
      key(SDLK_U, SDL_SCANCODE_U, false, SDL_KMOD_LCTRL);
      key(SDLK_LCTRL, SDL_SCANCODE_LCTRL, false, SDL_KMOD_NONE);
      break;
    }
    case 56: {
      check("remapped Ctrl+U undoes", _doc->history.index() == _settingsSeen - 1);
      Command* undo = _commands.find("edit.undo");
      undo->chords = undo->defaults;
      _cfg.showProbe = false;
      _showSettings = true;
      _settingsPage = 0;
      break;
    }
    case 60: writeShot(_selftestDir + "/p3-dev-settings.png"); selectTheme("classic"); break;
    case 63:
      check("Classic theme loaded", colorToHex(_theme.bg) == "#F6F6F6" && _theme.radius == 4);
      writeShot(_selftestDir + "/p3-classic-settings.png");
      _settingsPage = 3;
      break;
    case 66: writeShot(_selftestDir + "/p3-classic-shortcuts.png"); _showSettings = false; break;
    case 70: {
      writeShot(_selftestDir + "/p3-classic-main.png");
      // Hot reload: edit the theme file on disk; the 1 s tick must pick it up
      Theme t = _themeBase;
      t.accent = hexToColor("#FF00FF");
      std::string err;
      check("user theme saved", _themes.saveUser("classic", t, err));
      _reloadAt = SDL_GetTicks() + 1600;
      break;
    }
    #pragma endregion

  }
}

}
