#include "app/App.h"
#include "core/io/Png.h"
#include "platform/Accent.h"
#include "platform/Paths.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace david {

static Uint32 s_themeDialogEvent = 0;
enum EThemeDialog { THEME_IMPORT = 1, THEME_EXPORT = 2 };

// Small bold uppercase muted label, then a 1px line filling the row
static void sectionHeader(const char* text, ImFont* bold, const Theme& t) {
  ImGui::Spacing();
  std::string upper = text;
  for(auto& c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
  ImGui::PushFont(bold, t.fontSize * 0.82f);
  ImGui::TextColored(t.textMuted, "%s", upper.c_str());
  ImGui::PopFont();
  ImVec2 a = ImGui::GetItemRectMax();
  float y = (ImGui::GetItemRectMin().y + a.y) / 2;
  float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  ImGui::GetWindowDrawList()->AddLine({ a.x + 8, y }, { right, y }, ImGui::GetColorU32(t.line), 1);
}

static bool iconToggle(const char* icon, bool& v, const char* tip, const Theme&) {
  return ui::iconToggle(icon, icon, v, tr(tip));
}

// Text buttons keep a fill, so they still read as buttons next to flat icons
static bool textButton(const char* label, const Theme& t) {
  ImGui::PushStyleColor(ImGuiCol_Button, t.raised);
  bool r = ImGui::Button(label);
  ImGui::PopStyleColor();
  return r;
}

// A labeled row: label on the left in a fixed column, widget on the right
static void rowLabel(const char* label, float column) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(label);
  ImGui::SameLine(column);
}

void App::drawSettings() {
  if(!_showSettings) return;
  if(!s_themeDialogEvent) s_themeDialogEvent = SDL_RegisterEvents(1);
  ImGui::SetNextWindowSize({ 760 * _dpi, 560 * _dpi }, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, { 0.5f, 0.5f });
  ImGuiWindowClass wc;
  wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoDocking;
  ImGui::SetNextWindowClass(&wc);
  std::string title = std::string(ICON_FA_GEAR "  ") + tr("Settings") + "###settings";
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, _theme.dialogBorder());
  bool shown = ImGui::Begin(title.c_str(), &_showSettings, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
  ImGui::PopStyleVar();
  if(!shown) {
    ImGui::End();
    return;
  }
  if(_captureId.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape))
    _showSettings = false;

  struct Page { const char* icon; const char* name; };
  static const Page pages[] = {
    { ICON_FA_PALETTE, N_("Appearance") }, { ICON_FA_TOOLBOX, N_("Tools") }, { ICON_FA_IMAGE, N_("Canvas") },
    { ICON_FA_KEYBOARD, N_("Shortcuts") }, { ICON_FA_FOLDER_OPEN, N_("Paths") }, { ICON_FA_PUZZLE_PIECE, N_("Mods") },
    { ICON_FA_FLASK, N_("Beta tools") },
  };
  ImGui::BeginChild("##nav", { 170 * _dpi, 0 });
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2 * _dpi });
  for(int i = 0; i < int(std::size(pages)); i++) {
    ImGui::PushID(i);
    ImVec2 a, b;
    if(ui::listRow("##page", _settingsPage == i, ImGui::GetFrameHeight() * 1.15f, a, b)) _settingsPage = i;
    ui::rowLabel(pages[i].icon, tr(pages[i].name), a, b, ImGui::GetColorU32(ImGuiCol_Text));
    ImGui::PopID();
  }
  // Last: every setting back to the program's defaults, after a warning
  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();
  {
    ImVec2 a, b;
    if(ui::listRow("##resetall", false, ImGui::GetFrameHeight() * 1.15f, a, b)) _popup = "###resetall";
    ui::rowLabel(ICON_FA_ROTATE_LEFT, tr("Reset all settings"), a, b, ImGui::GetColorU32(_theme.bad));
  }
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("##page", { 0, 0 }, ImGuiChildFlags_AlwaysUseWindowPadding);
  switch(_settingsPage) {
    case 0: settingsAppearance(); break;
    case 1: settingsTools(); break;
    case 2: settingsCanvas(); break;
    case 3: settingsShortcuts(); break;
    case 4: settingsPaths(); break;
    case 5: settingsMods(); break;
    case 6: settingsBeta(); break;
  }
  ImGui::EndChild();
  ImGui::End();
}

#pragma region [ Appearance ]
// A miniature of the UI drawn with another theme's colors, so a theme can be judged before switching
static void themePreview(const Theme& t, float dpi, ImFont* bold) {
  ImDrawList* dl = ImGui::GetWindowDrawList();
  auto col = [](const ImVec4& c) { return ImGui::GetColorU32(c); };
  // One grid for everything: outer padding, inner margin, row height and gap
  float pad = 10 * dpi, m = 8 * dpi, rh = 32 * dpi, gap = 8 * dpi, tabH = 26 * dpi, r = t.radius * dpi, fs = t.fontSize * dpi;
  ImVec2 p = ImGui::GetCursorScreenPos();
  float w = std::min(ImGui::GetContentRegionAvail().x, 420 * dpi);
  float h = pad + tabH + m + rh + gap + rh + m + pad;
  dl->AddRectFilled(p, { p.x + w, p.y + h }, col(t.bg), r);
  if(t.borderWidth > 0) dl->AddRect(p, { p.x + w, p.y + h }, col(t.line), r, 0, t.borderWidth);
  auto leftText = [&](ImFont* f, const char* text, ImVec2 a, ImVec2 b, float indent, ImU32 c) {
    float th = (f ? f : ImGui::GetFont())->CalcTextSizeA(fs, FLT_MAX, 0, text).y;
    dl->AddText(f, fs, { a.x + indent, ImTrunc((a.y + b.y - th) / 2) }, c, text);
  };

  // Tab strip: the selected tab's accent line is the top of its own rounded shape, clipped to 2 px
  float tabW = 96 * dpi;
  ImVec2 ta = { p.x + pad, p.y + pad }, tb = { ta.x + tabW, ta.y + tabH };
  dl->AddRectFilled(ta, tb, col(t.surface), r, ImDrawFlags_RoundCornersTop);
  dl->PushClipRect(ta, { tb.x, ta.y + 2 * dpi }, true);
  dl->AddRectFilled(ta, tb, col(t.accent), r, ImDrawFlags_RoundCornersTop);
  dl->PopClipRect();
  ui::centeredText(dl, nullptr, fs, "Image.png", ta, tb, col(t.text));
  ui::centeredText(dl, nullptr, fs, "Sprite.png", { tb.x, ta.y }, { tb.x + tabW, tb.y }, col(t.textMuted));

  // Panel: a tool button, a tool icon and a layer row; then a field and the OK button, both edges on the grid
  ImVec2 qa = { p.x + pad, tb.y }, qb = { p.x + w - pad, p.y + h - pad };
  dl->AddRectFilled(qa, qb, col(t.surface), r, ImDrawFlags_RoundCornersBottom | ImDrawFlags_RoundCornersTopRight);
  float x0 = qa.x + m, x1 = qb.x - m, y1 = qa.y + m, y2 = y1 + rh + gap;
  ImVec2 b1a = { x0, y1 }, b1b = { x0 + rh, y1 + rh };
  dl->AddRectFilled(b1a, b1b, col(t.accent), r);
  ui::centeredIcon(dl, ICON_FA_PAINTBRUSH, b1a, b1b, col(t.onAccent));
  ImVec2 b2a = { b1b.x + gap, y1 }, b2b = { b1b.x + gap + rh, y1 + rh };
  ui::centeredIcon(dl, ICON_FA_ERASER, b2a, b2b, col(t.text));
  ImVec2 la = { b2b.x + gap, y1 }, lb = { x1, y1 + rh };
  dl->AddRectFilled(la, lb, col(t.line), r);
  ui::centeredIcon(dl, ICON_FA_EYE, la, { la.x + rh, lb.y }, col(t.text));
  leftText(nullptr, tr("Background"), la, lb, rh, col(t.text));
  float okW = 80 * dpi;
  ImVec2 oa = { x1 - okW, y2 }, ob = { x1, y2 + rh };
  ImVec2 ia = { x0, y2 }, ib = { oa.x - gap, y2 + rh };
  dl->AddRectFilled(ia, ib, col(t.raised), r);
  leftText(nullptr, colorToHex(t.accent).c_str(), ia, ib, m, col(t.text));
  dl->AddRectFilled(oa, ob, col(t.accent), r);
  ui::centeredText(dl, bold, fs, "OK", oa, ob, col(t.onAccent));
  ImGui::Dummy({ w, h });
}

void App::settingsAppearance() {
  const ThemeStore::Entry* current = _themes.find(_themeId);
  sectionHeader(tr("Theme"), _boldFont, _theme);

  // Theme list on the left, preview of the hovered (or current) theme on the right
  float listW = 220 * _dpi;
  ImGui::BeginChild("##themes", { listW, 170 * _dpi });
  std::string hovered;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2 * _dpi });
  for(auto& e : _themes.list()) {
    ImGui::PushID(e.id.c_str());
    float h = ImGui::GetFrameHeight() * 1.15f;
    ImVec2 a, b;
    if(ui::listRow("##t", e.id == _themeId, h, a, b)) { selectTheme(e.id); _cfg.theme = e.id; }
    if(ImGui::IsItemHovered()) hovered = e.id;
    // Four swatches, then the name, all centered on the row
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4* sw[] = { &e.theme.bg, &e.theme.surface, &e.theme.accent, &e.theme.text };
    float pad = ImGui::GetStyle().FramePadding.x, sh = 12 * _dpi, sy = ImTrunc((a.y + b.y - sh) / 2);
    for(int i = 0; i < 4; i++) {
      ImVec2 s0 = { a.x + pad + i * 11 * _dpi, sy };
      dl->AddRectFilled(s0, { s0.x + 10 * _dpi, s0.y + sh }, ImGui::GetColorU32(*sw[i]), 2 * _dpi);
    }
    float ty = ImTrunc((a.y + b.y - ImGui::GetTextLineHeight()) / 2);
    dl->AddText({ a.x + pad + 44 * _dpi + 8 * _dpi, ty }, ImGui::GetColorU32(ImGuiCol_Text), e.name.c_str());
    if(e.user && e.builtin) dl->AddText({ b.x - pad - ImGui::CalcTextSize(ICON_FA_PEN).x, ty }, ImGui::GetColorU32(_theme.warn), ICON_FA_PEN);
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginGroup();
  const ThemeStore::Entry* shown = _themes.find(hovered.empty() ? _themeId : hovered);
  themePreview(shown ? (shown->id == _themeId ? _theme : shown->theme) : _theme, _dpi, _boldFont);
  ImGui::EndGroup();

  // New (duplicate), import, export, reset, delete
  if(ui::iconButton("##ICON_FA_COPY", ICON_FA_COPY, { 0, 0 })) { std::snprintf(_themeName, sizeof _themeName, "%s copy", _themeBase.name.c_str()); ImGui::OpenPopup("newTheme"); }
  ImGui::SetItemTooltip("%s", tr("New theme from this one"));
  ImGui::SameLine();
  if(ui::iconButton("##ICON_FA_FILE_IMPORT", ICON_FA_FILE_IMPORT, { 0, 0 })) {
    static const SDL_DialogFileFilter f[] = { { "Theme", "json" } };
    SDL_ShowOpenFileDialog([](void*, const char* const* files, int) {
      SDL_Event e{}; e.type = s_themeDialogEvent; e.user.code = THEME_IMPORT;
      e.user.data1 = files && files[0] ? SDL_strdup(files[0]) : nullptr; SDL_PushEvent(&e);
    }, nullptr, _window, f, 1, nullptr, false);
  }
  ImGui::SetItemTooltip("%s", tr("Import"));
  ImGui::SameLine();
  if(ui::iconButton("##ICON_FA_FILE_EXPORT", ICON_FA_FILE_EXPORT, { 0, 0 })) {
    static const SDL_DialogFileFilter f[] = { { "Theme", "json" } };
    std::string def = _themeId + ".theme.json";
    SDL_ShowSaveFileDialog([](void*, const char* const* files, int) {
      SDL_Event e{}; e.type = s_themeDialogEvent; e.user.code = THEME_EXPORT;
      e.user.data1 = files && files[0] ? SDL_strdup(files[0]) : nullptr; SDL_PushEvent(&e);
    }, nullptr, _window, f, 1, def.c_str());
  }
  ImGui::SetItemTooltip("%s", tr("Export"));
  ImGui::SameLine();
  ImGui::BeginDisabled(!current || !current->builtin || !current->user);
  if(ui::iconButton("##ICON_FA_ROTATE_LEFT", ICON_FA_ROTATE_LEFT, { 0, 0 })) { _themes.resetToBuiltin(_themeId); selectTheme(_themeId); }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("%s", tr("Reset to built-in"));
  ImGui::SameLine();
  ImGui::BeginDisabled(!current || current->builtin);
  if(ui::iconButton("##ICON_FA_TRASH", ICON_FA_TRASH, { 0, 0 })) { _themes.remove(_themeId); selectTheme("dev"); }
  ImGui::EndDisabled();
  ImGui::SetItemTooltip("%s", tr("Delete theme"));
  if(platform::hasSystemAccent()) {
    ImGui::SameLine(0, 24 * _dpi);
    if(ImGui::Checkbox(tr("System accent"), &_cfg.systemAccent)) refreshTheme();
  }
  if(!_notice.empty()) { ImGui::SameLine(0, 16 * _dpi); ImGui::TextColored(_theme.warn, "%s", _notice.c_str()); }

  if(ImGui::BeginPopup("newTheme")) {
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(220 * _dpi);
    bool go = ImGui::InputText("##name", _themeName, sizeof _themeName, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    go |= ui::iconButton("##ICON_FA_CHECK", ICON_FA_CHECK, { 0, 0 });
    if(go && *_themeName) {
      std::string err, id = _themes.duplicate(_themeBase, _themeName, err);
      if(!id.empty()) selectTheme(id);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // Import / export results arrive as events from the file dialogs; they're handled here, on the UI thread
  SDL_Event e;
  while(SDL_PeepEvents(&e, 1, SDL_GETEVENT, s_themeDialogEvent, s_themeDialogEvent) > 0) {
    char* path = static_cast<char*>(e.user.data1);
    std::string err;
    if(path && e.user.code == THEME_IMPORT) {
      std::string id = _themes.import(path, err);
      if(!id.empty()) selectTheme(id); else _notice = err;
    }
    if(path && e.user.code == THEME_EXPORT) {
      std::string p = path;
      if(!pathFromUtf8(p).has_extension()) p += ".json";
      if(!_themes.exportTo(_themeId, p, err)) _notice = err;
    }
    SDL_free(path);
  }

  sectionHeader(tr("Interface"), _boldFont, _theme);
  float col = 190 * _dpi;
  rowLabel(tr("Language"), col);
  ImGui::SetNextItemWidth(220 * _dpi);
  std::string langDir = paths::exeDir() + "lang/";
  if(ImGui::BeginCombo("##lang", languageName(langDir, _cfg.language).c_str())) {
    for(auto& code : languages(langDir)) {
      if(ImGui::Selectable(languageName(langDir, code).c_str(), code == _cfg.language)) {
        std::string err;
        if(loadLanguage(langDir, code, err)) _cfg.language = code; else _notice = err;
      }
    }
    ImGui::EndCombo();
  }
  rowLabel(tr("Floating windows"), col);
  bool floating = _cfg.floatingWindows;
  if(ImGui::Checkbox("##floating", &floating)) setFloating(floating);
  ImGui::SetItemTooltip("%s", tr("Tool windows float over the canvas and can be resized, like Paint.NET"));

  // Token editor: edits apply live and are saved to the user copy when a widget is released
  sectionHeader(tr("Colors"), _boldFont, _theme);
  bool changed = false, commit = false;
  auto warnContrast = [&](float ratio, float need) {
    if(ratio >= need) return;
    ImGui::SameLine();
    ImGui::TextColored(_theme.warn, ICON_FA_TRIANGLE_EXCLAMATION);
    ImGui::SetItemTooltip("Contrast %.1f : 1 (needs %.1f)", ratio, need);
  };
  float colW = 190 * _dpi;
  int perRow = std::max(1, int(ImGui::GetContentRegionAvail().x / colW));
  int i = 0;
  for(auto& tok : Theme::colorTokens()) {
    if(i++ % perRow) ImGui::SameLine(((i - 1) % perRow) * colW + ImGui::GetStyle().WindowPadding.x);
    ImVec4& c = _themeBase.*tok.member;
    float v[4] = { c.x, c.y, c.z, c.w };
    ImGui::PushID(tok.key);
    if(ImGui::ColorEdit4("##c", v, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex)) {
      c = { v[0], v[1], v[2], v[3] };
      changed = true;
    }
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::TextUnformatted(tok.key);
    const Theme& t = _themeBase;
    if(!std::strcmp(tok.key, "text")) warnContrast(std::min({ contrastRatio(t.text, t.bg), contrastRatio(t.text, t.surface), contrastRatio(t.text, t.raised) }), 4.5f);
    if(!std::strcmp(tok.key, "textMuted")) warnContrast(contrastRatio(t.textMuted, t.surface), 3.0f);
    if(!std::strcmp(tok.key, "onAccent")) warnContrast(contrastRatio(t.onAccent, t.accent), 4.5f);
    ImGui::PopID();
  }

  sectionHeader(tr("Shape"), _boldFont, _theme);
  Theme defaults = _themes.defaults(_themeId);
  for(auto& tok : Theme::shapeTokens()) {
    rowLabel(tok.key, 140 * _dpi);
    ImGui::SetNextItemWidth(220 * _dpi);
    ImGui::PushID(tok.key);
    float& v = _themeBase.*tok.member;
    if(ImGui::SliderFloat("##s", &v, tok.min, tok.max, "%.0f")) changed = true;
    commit |= ImGui::IsItemDeactivatedAfterEdit();
    // Back to the theme's own value
    float def = defaults.*tok.member;
    ImGui::SameLine();
    ImGui::BeginDisabled(v == def);
    char tip[96];
    std::snprintf(tip, sizeof tip, "%s (%.0f)", tr("Reset to the theme's value"), def);
    if(ui::iconButton("##reset", ICON_FA_ROTATE_LEFT, { ImGui::GetFrameHeight(), ImGui::GetFrameHeight() }, false, tip)) {
      v = def;
      changed = commit = true;
    }
    ImGui::EndDisabled();
    ImGui::PopID();
  }

  if(changed) {
    // Font size is read at font load; everything else is just the style
    refreshTheme();
  }
  if(commit) {
    std::string err;
    if(_themes.saveUser(_themeId, _themeBase, err)) _themeStamp = _themes.stamp(_themeId);
    else _notice = err;
  }
}
#pragma endregion

#pragma region [ Tools and canvas ]
void App::settingsTools() {
  ToolSettings& t = _cfg.tools;
  float col = 190 * _dpi, w = 220 * _dpi;
  sectionHeader(tr("Brush"), _boldFont, _theme);
  rowLabel(tr("Width"), col);
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt("##bw", &t.brushSize, 1, 500, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
  rowLabel(tr("Antialiasing"), col);
  ImGui::Checkbox("##aa", &t.antialias);
  rowLabel(tr("Pressure"), col);
  iconToggle(ICON_FA_PEN_NIB, t.pressureSize, "Size", _theme);
  ImGui::SameLine();
  iconToggle(ICON_FA_DROPLET, t.pressureOpacity, "Opacity", _theme);

  sectionHeader(tr("Fill"), _boldFont, _theme);
  rowLabel(tr("Tolerance"), col);
  ImGui::SetNextItemWidth(w);
  float pct = t.tolerance * 100;
  if(ImGui::SliderFloat("##tol", &pct, 0, 100, "%.0f%%")) t.tolerance = pct / 100;
  rowLabel(tr("Global"), col);
  ImGui::Checkbox("##global", &t.fillGlobal);
  rowLabel(tr("Sample image"), col);
  ImGui::Checkbox("##sample", &t.sampleImage);

  sectionHeader(tr("Color picker"), _boldFont, _theme);
  rowLabel(tr("Sampling"), col);
  ImGui::SetNextItemWidth(w);
  const char* sizes[] = { "1 × 1", "3 × 3", "5 × 5", "9 × 9" };
  int vals[] = { 1, 3, 5, 9 }, cur = 0;
  for(int i = 0; i < 4; i++) if(vals[i] == t.pickerSize) cur = i;
  if(ImGui::Combo("##ps", &cur, sizes, 4)) t.pickerSize = vals[cur];
  rowLabel(tr("Sample image"), col);
  ImGui::Checkbox("##pi", &t.pickerImage);
  rowLabel(tr("Switch back after"), col);
  ImGui::Checkbox("##psb", &t.pickerSwitchBack);

  sectionHeader(tr("Selection"), _boldFont, _theme);
  rowLabel(tr("Then move the pixels"), col);
  ImGui::Checkbox("##stm", &t.selectThenMove);

  // The left tool strip: which tools show, in what order. Hidden tools keep their shortcuts.
  sectionHeader(tr("Tool strip"), _boldFont, _theme);
  ImGui::TextDisabled("%s", tr("Hidden tools keep their shortcuts."));
  std::vector<int> order = stripOrder(false);
  float rowH = ImGui::GetFrameHeight(), rowW = col + w;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  int moveFrom = -1, moveTo = -1;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { ImGui::GetStyle().ItemSpacing.x, 2 * _dpi });
  for(int k = 0; k < int(order.size()); k++) {
    Tool& tool = *_tools[order[k]];
    // Ids follow the tool, so the dragged row stays active while it moves
    ImGui::PushID(tool.name);
    bool shown = !toolHidden(tool);
    if(ImGui::Checkbox("##show", &shown)) {
      auto& h = _cfg.hiddenTools;
      if(shown) std::erase(h, std::string(tool.name));
      else h.push_back(tool.name);
    }
    // The rest of the row is the drag handle: dragging it past a neighbor swaps the two
    ImGui::SameLine();
    ImVec2 a = ImGui::GetCursorScreenPos(), b = { a.x + rowW - ImGui::GetCursorPosX(), a.y + rowH };
    ImGui::InvisibleButton("##drag", { b.x - a.x, rowH });
    bool active = ImGui::IsItemActive();
    if(active || ImGui::IsItemHovered()) {
      dl->AddRectFilled(a, b, ImGui::GetColorU32(active ? _theme.pressed : _theme.hover), ImGui::GetStyle().FrameRounding);
      ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if(active && !ImGui::IsItemHovered()) {
      float dy = ImGui::GetIO().MousePos.y;
      int next = dy < a.y ? k - 1 : dy > b.y ? k + 1 : k;
      if(next != k && next >= 0 && next < int(order.size())) { moveFrom = k; moveTo = next; }
    }
    ImU32 c = ImGui::GetColorU32(shown ? _theme.text : _theme.textMuted);
    float ty = a.y + (rowH - ImGui::GetTextLineHeight()) / 2, pad = 6 * _dpi;
    dl->AddText({ a.x + pad, ty }, c, tool.icon);
    dl->AddText({ a.x + pad + rowH + 8 * _dpi, ty }, c, tr(tool.name));
    ui::centeredIcon(dl, ICON_FA_GRIP_VERTICAL, { b.x - rowH, a.y }, b, ImGui::GetColorU32(_theme.textMuted));
    ImGui::PopID();
  }
  ImGui::PopStyleVar();
  if(moveFrom >= 0) {
    std::swap(order[moveFrom], order[moveTo]);
    _cfg.toolOrder.clear();
    for(int i : order) _cfg.toolOrder.push_back(_tools[i]->name);
  }
  ImGui::Spacing();
  if(textButton(tr("Reset tool strip"), _theme)) { _cfg.toolOrder.clear(); _cfg.hiddenTools.clear(); }
}

void App::settingsCanvas() {
  float col = 190 * _dpi, w = 220 * _dpi;
  sectionHeader(tr("View"), _boldFont, _theme);
  rowLabel(tr("Checkerboard"), col);
  ImGui::SetNextItemWidth(w);
  ImGui::SliderInt("##checker", &_cfg.checkerSize, 2, 32, "%d px");
  rowLabel(tr("Pixel grid"), col);
  ImGui::SetNextItemWidth(w);
  const char* grids[] = { tr("Off"), tr("Above 400%"), tr("Above 800%"), tr("Above 1600%") };
  float gv[] = { 0, 4, 8, 16 };
  int g = 2;
  for(int i = 0; i < 4; i++) if(gv[i] == _cfg.gridZoom) g = i;
  if(ImGui::Combo("##grid", &g, grids, 4)) _cfg.gridZoom = gv[g];
  rowLabel(tr("Wheel zooms"), col);
  ImGui::Checkbox("##wz", &_cfg.wheelZooms);
  ImGui::SetItemTooltip("%s", tr("Off: wheel scrolls, Ctrl+wheel zooms (Paint.NET)"));

  sectionHeader(tr("History"), _boldFont, _theme);
  rowLabel(tr("Memory"), col);
  ImGui::SetNextItemWidth(w);
  if(ImGui::SliderInt("##mem", &_cfg.historyMb, 64, 8192, "%d MB", ImGuiSliderFlags_Logarithmic)) _doc->history.setCap(size_t(_cfg.historyMb) << 20);

  sectionHeader(tr("New image"), _boldFont, _theme);
  rowLabel(tr("Size"), col);
  ImGui::SetNextItemWidth(w / 2 - 4);
  ImGui::InputInt("##nw", &_cfg.newW, 0);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(w / 2 - 4);
  ImGui::InputInt("##nh", &_cfg.newH, 0);
  _cfg.newW = std::clamp(_cfg.newW, 1, 65535);
  _cfg.newH = std::clamp(_cfg.newH, 1, 65535);
}
#pragma endregion

#pragma region [ Beta tools ]
// Features in development stay off by default, so turning one on is the only way they change David
void App::settingsBeta() {
  float col = 190 * _dpi;
  ImGui::TextDisabled("%s", tr("Features still in development. They may change or be incomplete."));
  sectionHeader(tr("Vector support"), _boldFont, _theme);
  rowLabel(tr("Vector layers"), col);
  ImGui::Checkbox("##vector", &_cfg.betaVector);
  ImGui::SetItemTooltip("%s", tr("Vector layers, object and node tools, SVG import and export"));
}
#pragma endregion

#pragma region [ Shortcuts ]
void App::settingsShortcuts() {
  // Capture: the next key with its modifiers becomes the shortcut; Escape alone cancels
  if(!_captureId.empty()) {
    _commands.suspended = true;
    ImGuiIO& io = ImGui::GetIO();
    for(int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; k++) {
      ImGuiKey key = ImGuiKey(k);
      if(ImGui::IsLRModKey(key) || key == ImGuiKey_ReservedForModCtrl || key == ImGuiKey_ReservedForModShift
         || key == ImGuiKey_ReservedForModAlt || key == ImGuiKey_ReservedForModSuper) continue;
      if(key >= ImGuiKey_MouseLeft && key <= ImGuiKey_MouseWheelY) continue;
      if(!ImGui::IsKeyPressed(key, false)) continue;
      ImGuiKeyChord mods = (io.KeyCtrl ? ImGuiMod_Ctrl : 0) | (io.KeyShift ? ImGuiMod_Shift : 0) | (io.KeyAlt ? ImGuiMod_Alt : 0);
      if(key != ImGuiKey_Escape || mods) {
        ImGuiKeyChord chord = mods | key;
        std::string from = _commands.releaseChord(chord, _captureId);
        Command* c = _commands.find(_captureId);
        if(c && std::find(c->chords.begin(), c->chords.end(), chord) == c->chords.end()) c->chords.push_back(chord);
        Command* other = from.empty() ? nullptr : _commands.find(from);
        if(other) {
          // "{key} was on {command}", with the parts filled in after translation
          std::string msg = tr("{key} was on {command}");
          msg.replace(msg.find("{key}"), 5, Commands::chordName(chord));
          msg.replace(msg.find("{command}"), 9, tr(other->label.c_str()));
          _notice = msg;
        } else _notice.clear();
      }
      _captureId.clear();
      break;
    }
  } else {
    _commands.suspended = false;
  }

  ImGui::SetNextItemWidth(260 * _dpi);
  ImGui::InputTextWithHint("##filter", ICON_FA_MAGNIFYING_GLASS, _shortcutFilter, sizeof _shortcutFilter);
  ImGui::SetItemTooltip("%s", tr("Search"));
  ImGui::SameLine();
  if(ui::iconButton("##ICON_FA_ROTATE_LEFT", ICON_FA_ROTATE_LEFT, { 0, 0 })) for(auto& c : _commands.list()) c.chords = c.defaults;
  ImGui::SetItemTooltip("%s", tr("Reset all to Paint.NET"));
  if(!_notice.empty() && _settingsPage == 3) { ImGui::SameLine(); ImGui::TextColored(_theme.warn, ICON_FA_CIRCLE_INFO " %s", _notice.c_str()); }

  std::string filter = _shortcutFilter;
  std::transform(filter.begin(), filter.end(), filter.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerH;
  if(ImGui::BeginTable("##keys", 3, tf, { 0, 0 })) {
    ImGui::TableSetupColumn("##name", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("##keys", ImGuiTableColumnFlags_WidthFixed, 280 * _dpi);
    ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
    for(auto& c : _commands.list()) {
      if(c.label.empty()) continue;
      std::string lower = tr(c.label.c_str());
      std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return char(std::tolower(ch)); });
      if(!filter.empty() && lower.find(filter) == std::string::npos) continue;
      ImGui::PushID(c.id.c_str());
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::Text("%s  %s", c.icon, tr(c.label.c_str()));
      ImGui::TableNextColumn();
      // Each chord is a button: left click re-captures it, right click removes it; + adds another
      int remove = -1;
      ImGui::PushStyleColor(ImGuiCol_Button, _theme.raised);
      for(int i = 0; i < int(c.chords.size()); i++) {
        ImGui::PushID(i);
        bool capturing = _captureId == c.id;
        if(ImGui::Button(Commands::chordName(c.chords[i]).c_str())) { c.chords.erase(c.chords.begin() + i); _captureId = c.id; _notice.clear(); ImGui::PopID(); break; }
        if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) remove = i;
        ImGui::SetItemTooltip("%s", tr("Click to change, right click to remove"));
        (void)capturing;
        ImGui::SameLine();
        ImGui::PopID();
      }
      ImGui::PopStyleColor();
      if(remove >= 0) c.chords.erase(c.chords.begin() + remove);
      if(_captureId == c.id) {
        ImGui::PushStyleColor(ImGuiCol_Button, _theme.accent);
        ImGui::PushStyleColor(ImGuiCol_Text, _theme.onAccent);
        ImGui::Button(ICON_FA_KEYBOARD " ...");
        ImGui::PopStyleColor(2);
      } else if(ui::iconButton("##ICON_FA_PLUS", ICON_FA_PLUS, { 0, 0 })) {
        _captureId = c.id;
        _notice.clear();
      }
      ImGui::TableNextColumn();
      if(c.chords != c.defaults) {
        if(ui::iconButton("##ICON_FA_ROTATE_LEFT", ICON_FA_ROTATE_LEFT, { 0, 0 })) {
          for(ImGuiKeyChord k : c.defaults) _commands.releaseChord(k, c.id);
          c.chords = c.defaults;
        }
        ImGui::SetItemTooltip("%s", tr("Reset"));
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}
#pragma endregion

#pragma region [ Paths ]
void App::settingsPaths() {
  auto pathRow = [&](const char* label, const std::string& path) {
    sectionHeader(label, _boldFont, _theme);
    ImGui::PushID(label);
    if(ui::iconButton("##ICON_FA_FOLDER_OPEN", ICON_FA_FOLDER_OPEN, { 0, 0 })) {
      std::string url = "file:///" + path;
      std::replace(url.begin(), url.end(), '\\', '/');
      SDL_OpenURL(url.c_str());
    }
    ImGui::SetItemTooltip("%s", tr("Open folder"));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", path.c_str());
    ImGui::PopID();
  };
  pathRow(tr("Settings"), paths::configDir());
  pathRow(tr("User themes"), _themes.userDir());
  pathRow(tr("Built-in themes"), paths::exeDir() + "themes");
  pathRow(tr("Languages"), paths::exeDir() + "lang");
  pathRow(tr("Mods"), paths::configDir() + "mods");
  pathRow(tr("Dither patterns"), paths::configDir() + "patterns");
  sectionHeader(tr("Layout"), _boldFont, _theme);
  if(textButton((std::string(ICON_FA_TABLE_COLUMNS "  ") + tr("Reset window layout")).c_str(), _theme)) {
    _cfg.showTools = _cfg.showHistory = _cfg.showLayers = _cfg.showColors = true;
    _freshLayout = true;
  }
}
#pragma endregion

}
