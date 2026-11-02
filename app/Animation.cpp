#include "app/App.h"
#include "core/io/Formats.h"
#include "core/io/Png.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace david {

#pragma region [ Playback ]
bool App::playing() const {
  return _doc->frameCount() > 1 && (_player.playing() || (_cfg.showAnimPreview && !_previewPaused));
}

// The tag playback stays in: the first one holding the active frame
const FrameTag* App::playTag() const {
  if(!_cfg.anim.playTag) return nullptr;
  for(auto& t : _doc->tags) if(_doc->activeFrame >= t.from && _doc->activeFrame <= t.to) return &t;
  return nullptr;
}

void App::tickPlayback() {
  uint64_t now = SDL_GetTicks();
  double dt = _playLast ? double(now - _playLast) : 0;
  _playLast = now;
  if(_doc->frameCount() < 2) { _player.stop(); _previewPlayer.stop(); return; }
  ELoop loop = ELoop(_cfg.anim.loop);
  if(_player.playing()) {
    int f = _player.advance(*_doc, dt, loop, playTag());
    if(f >= 0) _doc->setActiveFrame(f);
  }
  // The preview loops on its own, also while you draw (Piskel)
  if(_cfg.showAnimPreview && !_previewPaused) {
    if(!_previewPlayer.playing()) _previewPlayer.start(*_doc, playTag(), _doc->activeFrame);
    _previewPlayer.advance(*_doc, dt, loop == ELoop::Once ? ELoop::Loop : loop, playTag());
  }
}

void App::goToFrame(int f) {
  f = std::clamp(f, 0, _doc->frameCount() - 1);
  if(f == _doc->activeFrame) return;
  commitTool();
  _doc->setActiveFrame(f);
}

void App::selectedFrames(int& from, int& to) const {
  if(_selFrom < 0 || _selTo < 0) { from = to = _doc->activeFrame; return; }
  from = std::clamp(std::min(_selFrom, _selTo), 0, _doc->frameCount() - 1);
  to = std::clamp(std::max(_selFrom, _selTo), 0, _doc->frameCount() - 1);
}

void App::frameOp(const char* what) {
  Document& doc = *_doc;
  int n = doc.frameCount(), a = doc.activeFrame, from, to;
  selectedFrames(from, to);
  auto is = [&](const char* s) { return !std::strcmp(what, s); };
  if(is("play")) {
    if(_player.playing()) _player.stop();
    else if(n > 1) { commitTool(); _player.start(doc, playTag()); _playLast = SDL_GetTicks(); }
    return;
  }
  if(is("prev")) { goToFrame((a + n - 1) % n); _selFrom = _selTo = -1; return; }
  if(is("next")) { goToFrame((a + 1) % n); _selFrom = _selTo = -1; return; }
  if(is("first")) { goToFrame(playTag() ? playTag()->from : 0); return; }
  if(is("last")) { goToFrame(playTag() ? playTag()->to : n - 1); return; }
  commitTool();
  _player.stop();
  if(is("new")) addFrame(doc, a, ENewFrame::Empty, ICON_TB_SQUARE_PLUS);
  else if(is("duplicate")) addFrame(doc, a, ENewFrame::Duplicate, ICON_TB_COPY);
  else if(is("linked")) addFrame(doc, a, ENewFrame::Linked, ICON_TB_LINK);
  else if(is("delete")) deleteFrames(doc, from, to, ICON_TB_TRASH);
  else if(is("reverse")) reverseFrames(doc, from, to, ICON_TB_SWITCH_HORIZONTAL);
  else if(is("link")) linkCels(doc, doc.activeLayer, from, to, ICON_TB_LINK);
  else if(is("unlink")) unlinkCel(doc, doc.activeLayer, a, ICON_TB_UNLINK);
  else if(is("clearCel")) clearCel(doc, doc.activeLayer, a, ICON_TB_TRASH);
  else if(is("tag")) {
    auto tags = doc.tags;
    FrameTag t;
    t.name = std::string(tr("Tag")) + " " + std::to_string(tags.size() + 1);
    t.from = from; t.to = to;
    static const uint32_t COLORS[] = { 0xFFD9A35Bu, 0xFF5BB0D9u, 0xFF7BD95Bu, 0xFFD95BB6u, 0xFF5B6BD9u };
    t.color = COLORS[tags.size() % std::size(COLORS)];
    tags.push_back(t);
    setTags(doc, tags, N_("New Tag"), ICON_TB_TAG);
    _tagEdit = int(doc.tags.size()) - 1;
    _popup = "##tag";
  }
  _selFrom = _selTo = -1;
}

// The canvas's onion views from the settings; frames aren't ghosted while playing
void App::setOnionView() {
  const AnimSettings& a = _cfg.anim;
  OnionView& on = _nav.view.onion;
  on.frames = a.onion && !_player.playing();
  on.before = a.onionBefore; on.after = a.onionAfter;
  on.merged = a.onionMerged; on.above = a.onionAbove;
  on.layers = a.layerOnion;
  on.below = a.layersBelow; on.aboveCount = a.layersAbove;
  on.hideOthers = a.hideOtherLayers;
  on.opacity = a.onionOpacity / 100.f;
  on.tint = a.onionTint;
  const FrameTag* tag = playTag();
  on.rangeFrom = tag ? tag->from : 0;
  on.rangeTo = tag ? tag->to : -1;
}
#pragma endregion

#pragma region [ Onion options ]
void App::onionOptions() {
  AnimSettings& a = _cfg.anim;
  ImGui::TextDisabled("%s", tr("Onion skin"));
  ImGui::SetNextItemWidth(140 * _dpi);
  ImGui::SliderInt(tr("Frames before"), &a.onionBefore, 0, 8);
  ImGui::SetNextItemWidth(140 * _dpi);
  ImGui::SliderInt(tr("Frames after"), &a.onionAfter, 0, 8);
  ImGui::SetNextItemWidth(140 * _dpi);
  ImGui::SliderInt(tr("Opacity"), &a.onionOpacity, 5, 90, "%d%%");
  ImGui::Checkbox(tr("Tint (before red, after blue)"), &a.onionTint);
  ImGui::Checkbox(tr("Whole frame (all layers)"), &a.onionMerged);
  ImGui::Checkbox(tr("Above the frame"), &a.onionAbove);
}

void App::layerOnionOptions() {
  AnimSettings& a = _cfg.anim;
  ImGui::TextDisabled("%s", tr("Layer onion"));
  ImGui::SetNextItemWidth(140 * _dpi);
  ImGui::SliderInt(tr("Layers below"), &a.layersBelow, 0, 8);
  ImGui::SetNextItemWidth(140 * _dpi);
  ImGui::SliderInt(tr("Layers above"), &a.layersAbove, 0, 8);
  ImGui::SetNextItemWidth(140 * _dpi);
  ImGui::SliderInt(tr("Opacity"), &a.onionOpacity, 5, 90, "%d%%");
  ImGui::Checkbox(tr("Tint (below red, above blue)"), &a.onionTint);
  ImGui::Checkbox(tr("Hide layers outside the range"), &a.hideOtherLayers);
}
#pragma endregion

#pragma region [ Timeline ]
// The tag popup: name, range, direction, color, delete
void App::drawTagPopup() {
  if(!ImGui::BeginPopup("##tag")) return;
  Document& doc = *_doc;
  if(_tagEdit < 0 || _tagEdit >= int(doc.tags.size())) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
  auto tags = doc.tags;
  FrameTag& t = tags[_tagEdit];
  bool changed = false;
  char name[64];
  std::snprintf(name, sizeof name, "%s", t.name.c_str());
  ImGui::SetNextItemWidth(160 * _dpi);
  ImGui::InputText(tr("Name"), name, sizeof name);
  if(ImGui::IsItemDeactivatedAfterEdit()) { t.name = name; changed = true; }
  int range[2] = { t.from + 1, t.to + 1 };
  ImGui::SetNextItemWidth(160 * _dpi);
  if(ImGui::InputInt2(tr("Frames"), range) || ImGui::IsItemDeactivatedAfterEdit()) {
    int n = doc.frameCount();
    int a = std::clamp(range[0], 1, n) - 1, b = std::clamp(range[1], 1, n) - 1;
    if(a != t.from || b != t.to) { t.from = std::min(a, b); t.to = std::max(a, b); changed = true; }
  }
  const char* dirs[] = { tr("Forward"), tr("Reverse"), tr("Ping-pong") };
  int dir = int(t.direction);
  ImGui::SetNextItemWidth(160 * _dpi);
  if(ImGui::Combo(tr("Direction"), &dir, dirs, 3)) { t.direction = ETagDirection(dir); changed = true; }
  float col[3] = { (t.color & 0xFF) / 255.f, ((t.color >> 8) & 0xFF) / 255.f, ((t.color >> 16) & 0xFF) / 255.f };
  if(ImGui::ColorEdit3(tr("Color"), col, ImGuiColorEditFlags_NoInputs)) {
    t.color = 0xFF000000u | uint32_t(col[0] * 255 + 0.5f) | uint32_t(col[1] * 255 + 0.5f) << 8 | uint32_t(col[2] * 255 + 0.5f) << 16;
    changed = true;
  }
  if(changed) setTags(doc, tags, N_("Edit Tag"), ICON_TB_TAG);
  ImGui::Separator();
  if(ImGui::Button(tr("Delete Tag"))) {
    tags.erase(tags.begin() + _tagEdit);
    setTags(doc, tags, N_("Delete Tag"), ICON_TB_TAG);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

void App::drawTimeline() {
  if(!_cfg.showTimeline) return;
  if(beginPanel(11, ICON_TB_MOVIE, N_("Timeline"), "timeline", &_cfg.showTimeline)) {
    Document& doc = *_doc;
    int n = doc.frameCount();
    if(Command* c = _commands.find("anim.play")) c->icon = _player.playing() ? ICON_TB_PLAYER_PAUSE : ICON_TB_PLAYER_PLAY;
    if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput
       && ImGui::IsKeyPressed(ImGuiKey_Enter, false)) frameOp("play");

    // Tool row: playback, frames, onion, loop, frame rate, duration, tags
    auto row = [&](const char* id) { commandButton(id); ImGui::SameLine(0, 2 * _dpi); };
    for(auto id : { "anim.first", "anim.prev", "anim.play", "anim.next", "anim.last" }) row(id);
    ImGui::SameLine(0, 6 * _dpi);
    for(auto id : { "anim.new", "anim.duplicate", "anim.linked", "anim.delete" }) row(id);
    ImGui::SameLine(0, 6 * _dpi);
    std::string onionTip = std::string(tr("Onion skin")) + "  (Alt+O)  · " + tr("right click: options");
    ui::iconToggle("##onion", ICON_TB_GHOST, _cfg.anim.onion, onionTip.c_str());
    if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) ImGui::OpenPopup("##onionopts");
    if(ImGui::BeginPopup("##onionopts")) { onionOptions(); ImGui::EndPopup(); }
    ImGui::SameLine(0, 2 * _dpi);
    const char* loopIcons[] = { ICON_TB_REPEAT, ICON_TB_REPEAT_ONCE, ICON_TB_ARROWS_RIGHT_LEFT };
    const char* loopNames[] = { N_("Loop"), N_("Play once"), N_("Ping-pong") };
    if(ui::iconButton("##loop", loopIcons[_cfg.anim.loop], { ImGui::GetFrameHeight() * 1.2f, 0 }, false, tr(loopNames[_cfg.anim.loop]))) ImGui::OpenPopup("##loopmenu");
    if(ImGui::BeginPopup("##loopmenu")) {
      for(int i = 0; i < 3; i++) if(ImGui::MenuItem(tr(loopNames[i]), nullptr, _cfg.anim.loop == i)) _cfg.anim.loop = i;
      ImGui::Separator();
      ImGui::MenuItem(tr("Play inside the current tag"), nullptr, &_cfg.anim.playTag);
      ImGui::EndPopup();
    }
    ImGui::SameLine(0, 6 * _dpi);
    int fps = int(std::lround(doc.fps));
    ImGui::SetNextItemWidth(70 * _dpi);
    ImGui::InputInt("##fps", &fps, 0, 0);
    if(ImGui::IsItemDeactivatedAfterEdit()) setFrameRate(doc, fps, ICON_TB_CLOCK);
    ImGui::SetItemTooltip("%s", tr("Frames per second (frames without their own duration)"));
    ImGui::SameLine(0, 2 * _dpi);
    ImGui::TextDisabled("%s", tr("fps"));
    ImGui::SameLine(0, 6 * _dpi);
    int from, to;
    selectedFrames(from, to);
    if(!ImGui::IsAnyItemActive()) _durationMs = doc.frameMs(from);
    ImGui::SetNextItemWidth(70 * _dpi);
    ImGui::InputInt("##ms", &_durationMs, 0, 0);
    if(ImGui::IsItemDeactivatedAfterEdit()) setFrameDuration(doc, from, to, std::clamp(_durationMs, 1, 60000), ICON_TB_CLOCK);
    ImGui::SetItemTooltip("%s", tr("Duration of the selected frames"));
    ImGui::SameLine(0, 2 * _dpi);
    ImGui::TextDisabled("%s", tr("ms"));
    ImGui::SameLine(0, 6 * _dpi);
    commandButton("anim.tag");
    ImGui::SameLine(0, 6 * _dpi);
    commandButton("file.exportAnim");

    // The grid: layer names on the left; tags, frame numbers and cels on the right
    float rowH = ImGui::GetFrameHeight(), cellW = std::max(rowH, 24 * _dpi), nameW = 120 * _dpi;
    float tagH = doc.tags.empty() ? 0 : std::round(rowH * 0.75f), headH = rowH;
    int layers = int(doc.layers.size());
    ImDrawList* dl = nullptr;
    ImU32 cText = ImGui::GetColorU32(_theme.text), cMuted = ImGui::GetColorU32(_theme.textMuted), cAccent = ImGui::GetColorU32(_theme.accent);
    ImU32 cRaised = ImGui::GetColorU32(_theme.raised), cHover = ImGui::GetColorU32(_theme.hover), cLine = ImGui::GetColorU32(_theme.line);

    // The names scroll with the cels (the cels child scrolls; this one follows it)
    ImGui::BeginChild("##names", { nameW, 0 }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    {
      ImGui::SetScrollY(_tlScrollY);
      dl = ImGui::GetWindowDrawList();
      ImVec2 o = ImGui::GetCursorScreenPos();
      float y0 = o.y + tagH + headH;
      for(int r = 0; r < layers; r++) {
        int i = layers - 1 - r;
        ImVec2 a = { o.x, y0 + r * rowH }, b = { o.x + nameW, a.y + rowH };
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        if(ImGui::InvisibleButton("##name", { nameW, rowH })) { commitTool(); doc.activeLayer = i; doc.onChanged(); }
        ImGui::PopID();
        if(i == doc.activeLayer) dl->AddRectFilled(a, b, cRaised, 4 * _dpi);
        dl->PushClipRect(a, { b.x - 4 * _dpi, b.y }, true);
        dl->AddText({ a.x + 6 * _dpi, a.y + (rowH - ImGui::GetTextLineHeight()) / 2 }, doc.layers[i]->visible ? cText : cMuted, doc.layers[i]->name.c_str());
        dl->PopClipRect();
      }
      ImGui::SetCursorScreenPos(o);
      ImGui::Dummy({ nameW, tagH + headH + layers * rowH });
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 0);

    ImGui::BeginChild("##cels", { 0, 0 }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    {
      dl = ImGui::GetWindowDrawList();
      ImVec2 o = ImGui::GetCursorScreenPos();
      float w = n * cellW, h = tagH + headH + layers * rowH;
      ImGui::InvisibleButton("##grid", { std::max(w, 1.f), std::max(h, 1.f) }, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
      bool hovered = ImGui::IsItemHovered();
      ImVec2 m = ImGui::GetIO().MousePos;
      int mf = std::clamp(int((m.x - o.x) / cellW), 0, n - 1);
      float my = m.y - o.y;
      int zone = my < tagH ? 0 : my < tagH + headH ? 1 : 2;  // tags, frame numbers, cels
      int mr = std::clamp(int((my - tagH - headH) / rowH), 0, layers - 1), ml = layers - 1 - mr;
      bool shift = ImGui::GetIO().KeyShift;

      // Clicks
      if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if(zone == 0) {
          for(int i = 0; i < int(doc.tags.size()); i++)
            if(mf >= doc.tags[i].from && mf <= doc.tags[i].to) { _tagEdit = i; _popup = "##tag"; break; }
        } else {
          if(shift) { if(_selFrom < 0) _selFrom = doc.activeFrame; _selTo = mf; }
          else {
            int f0, f1;
            selectedFrames(f0, f1);
            // A press inside a range keeps it, so the range can be dragged
            if(zone == 1 && f1 > f0 && mf >= f0 && mf <= f1) {}
            else _selFrom = _selTo = -1;
          }
          goToFrame(mf);
          if(zone == 2 && ml != doc.activeLayer) { commitTool(); doc.activeLayer = ml; doc.onChanged(); }
          if(zone == 1 && !shift) { _dragFrame = mf; _dragX = m.x; }
        }
      }
      if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right) && zone > 0) {
        int f0, f1;
        selectedFrames(f0, f1);
        if(mf < f0 || mf > f1) { _selFrom = _selTo = -1; goToFrame(mf); }
        if(zone == 2 && ml != doc.activeLayer) { commitTool(); doc.activeLayer = ml; doc.onChanged(); }
        ImGui::OpenPopup(zone == 1 ? "##framemenu" : "##celmenu");
      }
      // Dragging frame numbers moves the selected frames; a thin line shows where they go
      int dropAt = -1;
      if(_dragFrame >= 0) {
        if(std::abs(m.x - _dragX) > 4 * _dpi) dropAt = std::clamp(int((m.x - o.x) / cellW + 0.5f), 0, n);
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
          if(dropAt >= 0) {
            int f0, f1;
            selectedFrames(f0, f1);
            if(_dragFrame < f0 || _dragFrame > f1) f0 = f1 = _dragFrame;
            // Drop position counted without the moved frames
            int dest = dropAt > f1 ? dropAt - (f1 - f0 + 1) : std::min(dropAt, f0);
            if(dropAt < f0 || dropAt > f1 + 1) { commitTool(); _player.stop(); moveFrames(doc, f0, f1, dest, ICON_TB_SWITCH_HORIZONTAL); _selFrom = _selTo = -1; }
          }
          _dragFrame = -1;
        }
      }

      // Drawing: selection and active frame columns, active layer row, then cels
      int f0, f1;
      selectedFrames(f0, f1);
      float cy = o.y + tagH + headH;
      dl->AddRectFilled({ o.x, cy + (layers - 1 - doc.activeLayer) * rowH }, { o.x + w, cy + (layers - doc.activeLayer) * rowH }, cRaised);
      if(f1 > f0) dl->AddRectFilled({ o.x + f0 * cellW, o.y + tagH }, { o.x + (f1 + 1) * cellW, o.y + h }, cHover);
      ImVec4 act = _theme.accent;
      act.w = 0.22f;
      dl->AddRectFilled({ o.x + doc.activeFrame * cellW, o.y + tagH }, { o.x + (doc.activeFrame + 1) * cellW, o.y + h }, ImGui::GetColorU32(act));
      for(int i = 0; i < int(doc.tags.size()); i++) {
        const FrameTag& t = doc.tags[i];
        ImVec2 a = { o.x + t.from * cellW + 1, o.y + 1 }, b = { o.x + (t.to + 1) * cellW - 1, o.y + tagH - 2 };
        dl->AddRectFilled(a, b, t.color, 3 * _dpi);
        dl->PushClipRect(a, b, true);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.8f, { a.x + 4 * _dpi, a.y + (tagH - 3 - ImGui::GetFontSize() * 0.8f) / 2 }, IM_COL32(20, 20, 20, 255), t.name.c_str());
        dl->PopClipRect();
      }
      char num[16];
      for(int f = 0; f < n; f++) {
        float x = o.x + f * cellW;
        std::snprintf(num, sizeof num, "%d", f + 1);
        ImVec2 ts = ImGui::CalcTextSize(num);
        dl->AddText({ x + (cellW - ts.x) / 2, o.y + tagH + (headH - ts.y) / 2 }, f == doc.activeFrame ? cText : cMuted, num);
        // A frame with its own duration has a mark under its number
        if(doc.frames[f].duration > 0) dl->AddLine({ x + cellW * 0.3f, o.y + tagH + headH - 2 * _dpi }, { x + cellW * 0.7f, o.y + tagH + headH - 2 * _dpi }, cAccent, 1.5f * _dpi);
        dl->AddLine({ x + cellW, o.y + tagH }, { x + cellW, o.y + h }, cLine);
        for(int r = 0; r < layers; r++) {
          const Layer& l = *doc.layers[layers - 1 - r];
          ImVec2 c = { x + cellW / 2, cy + r * rowH + rowH / 2 };
          float rad = 4 * _dpi;
          bool content = !l.celEmpty(f);
          if(f > 0 && l.linked(f, f - 1)) dl->AddLine({ c.x - cellW, c.y }, c, cAccent, 2 * _dpi);
          if(content) dl->AddCircleFilled(c, rad, l.visible ? cText : cMuted);
          else dl->AddCircle(c, rad * 0.75f, cMuted, 0, 1.2f * _dpi);
        }
      }
      if(dropAt >= 0) dl->AddLine({ o.x + dropAt * cellW, o.y + tagH }, { o.x + dropAt * cellW, o.y + h }, cAccent, 2 * _dpi);
      if(hovered && zone == 2) ImGui::SetTooltip("%s · %s %d · %d ms", doc.layers[ml]->name.c_str(), tr("frame"), mf + 1, doc.frameMs(mf));
      if(_player.playing()) ImGui::SetScrollX(std::clamp(doc.activeFrame * cellW - ImGui::GetWindowWidth() / 2, 0.f, ImGui::GetScrollMaxX()));

      if(ImGui::BeginPopup("##framemenu")) {
        for(auto id : { "anim.new", "anim.duplicate", "anim.linked", "anim.delete" }) _commands.menuItem(id);
        ImGui::Separator();
        for(auto id : { "anim.reverse", "anim.tag" }) _commands.menuItem(id);
        ImGui::EndPopup();
      }
      if(ImGui::BeginPopup("##celmenu")) {
        for(auto id : { "anim.link", "anim.unlink", "anim.clearCel" }) _commands.menuItem(id);
        ImGui::EndPopup();
      }
      _tlScrollY = ImGui::GetScrollY();
    }
    ImGui::EndChild();
  }
  ImGui::End();
}
#pragma endregion

#pragma region [ Animation preview ]
void App::drawAnimPreview() {
  if(!_cfg.showAnimPreview) return;
  if(beginPanel(12, ICON_TB_KEYFRAMES, N_("Animation Preview"), "animpreview", &_cfg.showAnimPreview)) {
    Document& doc = *_doc;
    float btn = ImGui::GetFrameHeight();
    if(ui::iconButton("##pp", _previewPaused ? ICON_TB_PLAYER_PLAY : ICON_TB_PLAYER_PAUSE, { btn, btn }, false, tr(_previewPaused ? "Play" : "Pause"))) {
      _previewPaused = !_previewPaused;
      _previewPlayer.stop();
    }
    ImGui::SameLine(0, 2 * _dpi);
    const char* zooms[] = { tr("Fit"), "1×", "2×", "4×", "8×" };
    int zi = _cfg.anim.previewZoom == 0 ? 0 : _cfg.anim.previewZoom == 1 ? 1 : _cfg.anim.previewZoom == 2 ? 2 : _cfg.anim.previewZoom == 4 ? 3 : 4;
    ImGui::SetNextItemWidth(64 * _dpi);
    if(ImGui::Combo("##zoom", &zi, zooms, 5)) _cfg.anim.previewZoom = zi == 0 ? 0 : 1 << (zi - 1);
    int f = _previewPaused || doc.frameCount() < 2 || !_previewPlayer.playing() ? doc.activeFrame : _previewPlayer.current();
    f = std::clamp(f, 0, doc.frameCount() - 1);
    ImGui::SameLine();
    ImGui::TextDisabled("%d / %d", f + 1, doc.frameCount());

    // One texture per frame, each rebuilt when the document changes
    for(auto it = _animFrames.begin(); it != _animFrames.end();) {
      if(it->first >= doc.frameCount()) { it->second.clear(); it = _animFrames.erase(it); }
      else ++it;
    }
    PreviewTexture& tex = _animFrames[f];
    tex.update(doc, 512, SDL_GetTicks(), f);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float z = _cfg.anim.previewZoom > 0 ? _cfg.anim.previewZoom * float(doc.width()) / tex.width()
                                        : std::min(avail.x / tex.width(), avail.y / tex.height());
    ImVec2 size = { tex.width() * z, tex.height() * z };
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 c = { std::round(p.x + std::max(0.f, (avail.x - size.x) / 2)), std::round(p.y + std::max(0.f, (avail.y - size.y) / 2)) };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p, { p.x + avail.x, p.y + avail.y }, true);
    dl->AddRectFilled(c, { c.x + size.x, c.y + size.y }, ImGui::GetColorU32(_theme.checkerA));
    dl->AddImage(ImTextureRef(ImTextureID(tex.tex())), c, { c.x + size.x, c.y + size.y });
    dl->PopClipRect();
    ImGui::Dummy(avail);
  }
  ImGui::End();
}
#pragma endregion

#pragma region [ Export and import ]
void App::drawAnimDialogs() {
  drawTagPopup();
  Document& doc = *_doc;
  ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
  float w = 180 * _dpi;

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(ICON_TB_TABLE_EXPORT "  ") + tr("Export Animation") + "###exportanim";
  if(ImGui::BeginPopupModal(title.c_str(), nullptr, flags)) {
    ExportAnim& e = _export;
    pushFieldColors();
    const char* formats[] = { "GIF", "APNG", "WebP", tr("Sprite sheet (PNG)"), tr("PNG sequence") };
    ImGui::SetNextItemWidth(w);
    ImGui::Combo(tr("Format"), &e.format, formats, 5);
    std::vector<std::string> tagNames = { tr("All frames") };
    for(auto& t : doc.tags) tagNames.push_back(t.name);
    if(e.tag >= int(doc.tags.size())) e.tag = -1;
    ImGui::SetNextItemWidth(w);
    if(ImGui::BeginCombo(tr("Frames"), tagNames[e.tag + 1].c_str())) {
      for(int i = -1; i < int(doc.tags.size()); i++) if(ImGui::Selectable(tagNames[i + 1].c_str(), e.tag == i)) e.tag = i;
      ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(w);
    ImGui::SliderInt(tr("Scale"), &e.scale, 1, 16, "%d×");
    if(e.format <= 2) {
      ImGui::SetNextItemWidth(w);
      ImGui::InputInt(tr("Loops (0 = forever)"), &e.loops, 0);
      e.loops = std::clamp(e.loops, 0, 65535);
    }
    if(e.format == 3) {
      ImGui::SetNextItemWidth(w);
      ImGui::InputInt(tr("Columns (0 = square)"), &e.columns, 0);
      ImGui::SetNextItemWidth(w);
      ImGui::InputInt(tr("Padding"), &e.padding, 0);
      e.columns = std::clamp(e.columns, 0, 4096); e.padding = std::clamp(e.padding, 0, 256);
      ImGui::Checkbox(tr("JSON next to it (Aseprite format)"), &e.json);
    }
    ImGui::PopStyleColor(3);
    ImGui::Spacing();
    bool ok = ui::dialogButton(tr("Export..."), true, 110 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::SameLine();
    bool cancel = ui::dialogButton(tr("Cancel"), false, 110 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if(ok) {
      static const SDL_DialogFileFilter GIF[] = { { "GIF", "gif" } }, PNG[] = { { "PNG", "png" } }, WEBP[] = { { "WebP", "webp" } };
      const SDL_DialogFileFilter* f = e.format == 0 ? GIF : e.format == 2 ? WEBP : PNG;
      pickFile(6, true, f, 1);
    }
    if(ok || cancel) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  title = std::string(ICON_TB_TABLE_IMPORT "  ") + tr("Import Sprite Sheet") + "###importsheet";
  if(ImGui::BeginPopupModal(title.c_str(), nullptr, flags)) {
    SliceOptions& o = _import.o;
    const Document* sheet = _import.sheet.get();
    pushFieldColors();
    if(sheet) ImGui::TextDisabled("%d × %d", sheet->width(), sheet->height());
    float half = 86 * _dpi;
    ImGui::SetNextItemWidth(half); ImGui::InputInt("##fw", &o.frameW, 0);
    ImGui::SameLine(0, 4 * _dpi);
    ImGui::SetNextItemWidth(half); ImGui::InputInt(tr("Frame size"), &o.frameH, 0);
    ImGui::SetNextItemWidth(half); ImGui::InputInt("##ox", &o.offsetX, 0);
    ImGui::SameLine(0, 4 * _dpi);
    ImGui::SetNextItemWidth(half); ImGui::InputInt(tr("Offset"), &o.offsetY, 0);
    ImGui::SetNextItemWidth(w); ImGui::InputInt(tr("Padding"), &o.padding, 0);
    ImGui::SetNextItemWidth(w); ImGui::InputInt(tr("Frames (0 = all)"), &o.count, 0);
    o.frameW = std::clamp(o.frameW, 1, 16384); o.frameH = std::clamp(o.frameH, 1, 16384);
    o.offsetX = std::max(0, o.offsetX); o.offsetY = std::max(0, o.offsetY); o.padding = std::clamp(o.padding, 0, 256); o.count = std::max(0, o.count);
    if(sheet) {
      int cols = (sheet->width() - o.offsetX + o.padding) / (o.frameW + o.padding), rows = (sheet->height() - o.offsetY + o.padding) / (o.frameH + o.padding);
      ImGui::TextDisabled("%s: %d × %d", tr("Grid"), std::max(0, cols), std::max(0, rows));
    }
    ImGui::PopStyleColor(3);
    ImGui::Spacing();
    bool ok = ui::dialogButton(tr("OK"), true, 110 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Enter);
    ImGui::SameLine();
    bool cancel = ui::dialogButton(tr("Cancel"), false, 110 * _dpi) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    if(ok && sheet) {
      std::string err;
      auto doc2 = documentFromSheet(*sheet, o, err);
      if(doc2) { commitTool(); setDocument(std::move(doc2)); _cfg.showTimeline = true; }
      else { _error = err; _popup = "error"; }
    }
    if(ok || cancel) { ImGui::CloseCurrentPopup(); _import.sheet.reset(); }
    ImGui::EndPopup();
  }
}

void App::sheetChosen(const std::string& path) {
  std::string err;
  _import.sheet = loadImage(path, err);
  if(!_import.sheet) { _error = err; _popup = "error"; return; }
  // A guess for the frame size: square frames as tall as the sheet (one row), else the current values
  if(_import.sheet->width() % _import.sheet->height() == 0) _import.o.frameW = _import.o.frameH = _import.sheet->height();
  _popup = "###importsheet";
}

void App::exportAnimation(const std::string& path) {
  const ExportAnim& e = _export;
  std::string p = path, err;
  const char* exts[] = { ".gif", ".png", ".webp", ".png", ".png" };
  if(!pathFromUtf8(p).has_extension()) p += exts[e.format];
  bool ok = true;
  if(e.format <= 2) {
    SaveOptions o;
    o.tag = e.tag; o.scale = e.scale; o.loops = e.loops; o.quality = 100;
    auto bytes = saveImageBytes(*_doc, e.format == 0 ? "gif" : e.format == 1 ? "png" : "webp", err, o);
    ok = !bytes.empty() && writeFileAtomic(p, bytes, err);
  } else if(e.format == 3) {
    SheetOptions o;
    o.columns = e.columns; o.padding = e.padding; o.scale = e.scale; o.tag = e.tag;
    std::string json, name = pathToUtf8(pathFromUtf8(p).filename());
    auto sheet = sheetFromDocument(*_doc, o, name, json);
    ok = png::save(*sheet, p, err);
    if(ok && e.json) {
      auto jp = pathFromUtf8(p).replace_extension(".json");
      ok = writeFileAtomic(pathToUtf8(jp), std::vector<uint8_t>(json.begin(), json.end()), err);
    }
  } else {
    // name_001.png, name_002.png, ...
    auto frames = animFrames(*_doc, e.tag, e.scale);
    auto fp = pathFromUtf8(p);
    std::string stem = pathToUtf8(fp.stem()), dir = pathToUtf8(fp.parent_path());
    int s = std::max(1, e.scale), fw = _doc->width() * s;
    for(size_t i = 0; i < frames.size() && ok; i++) {
      char num[16];
      std::snprintf(num, sizeof num, "_%03d.png", int(i + 1));
      const auto& px = frames[i].px;
      auto bytes = png::encode(fw, _doc->height() * s, [&](int y, uint32_t* row) { std::copy_n(px.data() + size_t(y) * fw, fw, row); });
      ok = writeFileAtomic(pathToUtf8(pathFromUtf8(dir) / pathFromUtf8(stem + num)), bytes, err);
    }
  }
  if(!ok) { _error = err.empty() ? "export failed" : err; _popup = "error"; }
}
#pragma endregion

}
