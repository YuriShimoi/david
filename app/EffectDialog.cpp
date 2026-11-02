#include "core/Text.h"
#include "app/App.h"
#include "core/Pattern.h"
#include "platform/Paths.h"
#include <sstream>
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace david {

EffectEnv App::effectEnv() {
  EffectEnv env;
  env.primary = _primary;
  env.secondary = _secondary;
  env.palette = &_cfg.tools.palette;
  env.baseDir = paths::exeDir();
  return env;
}

// Opens the effect's dialog with its last values (colors start at the primary color), previewing live
void App::openEffect(const std::string& id) {
  const Effect* e = findEffect(id);
  if(!e) return;
  if(!rasterGuard([this, id] { openEffect(id); })) return;
  commitTool();
  _fx = e;
  auto last = _fxRemember.find(id);
  if(last != _fxRemember.end()) _fxValues = last->second;
  else {
    _fxValues = defaultValues(*e);
    for(auto& p : e->params) if(p.type == EParam::Color) _fxValues.num[p.id] = double(_primary);
  }
  if(e->params.empty()) {
    // Nothing to set: apply at once
    previewEffect();
    finishEffect(true);
    return;
  }
  previewEffect();
  _popup = "###effect";
}

void App::previewEffect() {
  if(!_fx) return;
  TileRecorder& rec = _fxLive.redraw(*_doc);
  try {
    applyEffect(*_doc, rec, *_fx, _fxValues, effectEnv());
  } catch(const std::exception& e) {
    // A mod's effect failed: nothing of it stays, and the dialog closes with the message
    std::string msg = _fx->name + ": " + e.what();
    _fxLive.cancel();
    _fx = nullptr;
    showError(msg);
  }
  _doc->onChanged();
}

void App::finishEffect(bool ok) {
  if(!_fx) { _fxLive.cancel(); return; }
  if(ok && _fxAllFrames && _doc->frameCount() > 1) {
    // The previewed frame, then every other one (a cel linked to one already done is skipped), as one step
    Document& doc = *_doc;
    auto step = std::make_unique<CompoundAction>();
    step->name = _fx->name;
    step->icon = _fx->icon;
    if(auto edit = _fxLive.take("", "")) step->parts.push_back(std::move(edit));
    int from, to, here = doc.activeFrame;
    selectedFrames(from, to);
    if(from == to) { from = 0; to = doc.frameCount() - 1; }
    std::vector<int> done = { here };
    EffectEnv env = effectEnv();
    try {
      for(int f = from; f <= to; f++) {
        bool shared = false;
        for(int g : done) shared |= f == g || doc.active().linked(f, g);
        if(shared) continue;
        done.push_back(f);
        doc.setActiveFrame(f);
        TileRecorder rec(doc.active());
        applyEffect(doc, rec, *_fx, _fxValues, env);
        if(auto edit = rec.finish("", "")) step->parts.push_back(std::move(edit));
      }
    } catch(const std::exception& e) {
      showError(_fx->name + ": " + e.what());
    }
    doc.setActiveFrame(here);
    if(!step->parts.empty()) doc.history.push(doc, std::move(step));
    _fxRemember[_fx->id] = _fxValues;
    _lastEffect = _fx->id;
  } else if(ok) {
    _fxLive.commit(*_doc, _fx->name, _fx->icon);
    _fxRemember[_fx->id] = _fxValues;
    _lastEffect = _fx->id;
  } else {
    _fxLive.cancel();
  }
  _doc->onChanged();
  _fx = nullptr;
}

// Ctrl+F: the last effect again with the same values, no dialog
void App::repeatEffect() {
  const Effect* e = findEffect(_lastEffect);
  if(!e) return;
  commitTool();
  _fx = e;
  _fxValues = _fxRemember[e->id];
  previewEffect();
  finishEffect(true);
}

// Curves: one point list per channel ("all|red|green|blue"); click adds a point, drag moves it,
// right click removes it. The ends can move up and down only.
static bool curveWidget(const EffectParam& p, ParamValues& v, float dpi) {
  static int channel = 0;
  std::vector<std::string> parts;
  {
    std::stringstream ss(v.str(p.id));
    std::string part;
    while(std::getline(ss, part, '|')) parts.push_back(part);
    while(parts.size() < 4) parts.push_back("0,0 255,255");
  }
  const char* names[] = { N_("RGB"), N_("Red"), N_("Green"), N_("Blue") };
  for(int i = 0; i < 4; i++) {
    if(i) ImGui::SameLine();
    if(ImGui::RadioButton(tr(names[i]), channel == i)) channel = i;
  }
  std::vector<std::pair<int, int>> pts;
  {
    std::stringstream ss(parts[channel]);
    std::string item;
    while(ss >> item) { int x, y; if(std::sscanf(item.c_str(), "%d,%d", &x, &y) == 2) pts.push_back({ x, y }); }
    std::sort(pts.begin(), pts.end());
    if(pts.size() < 2) pts = { { 0, 0 }, { 255, 255 } };
  }
  float size = 256 * dpi;
  ImVec2 a = ImGui::GetCursorScreenPos(), b = { a.x + size, a.y + size };
  ImGui::InvisibleButton("##curve", { size, size });
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBg));
  for(int i = 1; i < 4; i++) {
    dl->AddLine({ a.x + size * i / 4, a.y }, { a.x + size * i / 4, b.y }, ImGui::GetColorU32(ImGuiCol_Border));
    dl->AddLine({ a.x, a.y + size * i / 4 }, { b.x, a.y + size * i / 4 }, ImGui::GetColorU32(ImGuiCol_Border));
  }
  auto toScr = [&](float x, float y) { return ImVec2{ a.x + x / 255 * size, b.y - y / 255 * size }; };
  std::string text;
  for(auto& [x, y] : pts) text += std::to_string(x) + "," + std::to_string(y) + " ";
  auto lut = curveTable(text);
  ImU32 colors[] = { IM_COL32(230, 230, 230, 255), IM_COL32(255, 85, 85, 255), IM_COL32(80, 250, 123, 255), IM_COL32(139, 233, 253, 255) };
  for(int x = 0; x < 255; x++) dl->AddLine(toScr(float(x), lut[x]), toScr(float(x + 1), lut[x + 1]), colors[channel], 1.5f * dpi);
  static int dragging = -1;
  bool changed = false;
  ImVec2 m = ImGui::GetIO().MousePos;
  int mx = std::clamp(int(std::lround((m.x - a.x) / size * 255)), 0, 255), my = std::clamp(int(std::lround((b.y - m.y) / size * 255)), 0, 255);
  int hot = -1;
  for(int i = 0; i < int(pts.size()); i++) {
    ImVec2 q = toScr(float(pts[i].first), float(pts[i].second));
    if(std::abs(q.x - m.x) < 7 * dpi && std::abs(q.y - m.y) < 7 * dpi) hot = i;
    dl->AddCircleFilled(q, (i == hot || i == dragging ? 5.f : 4.f) * dpi, colors[channel]);
  }
  if(ImGui::IsItemActivated()) {
    if(hot >= 0) dragging = hot;
    else { pts.push_back({ mx, my }); std::sort(pts.begin(), pts.end()); for(int i = 0; i < int(pts.size()); i++) if(pts[i] == std::pair(mx, my)) dragging = i; changed = true; }
  }
  if(ImGui::IsItemActive() && dragging >= 0 && dragging < int(pts.size())) {
    // The end points keep their x; inner points stay between their neighbors
    int x = dragging == 0 ? 0 : dragging == int(pts.size()) - 1 ? 255 : std::clamp(mx, pts[dragging - 1].first + 1, pts[dragging + 1].first - 1);
    if(pts[dragging] != std::pair(x, my)) { pts[dragging] = { x, my }; changed = true; }
  }
  if(!ImGui::IsItemActive()) dragging = -1;
  if(ImGui::IsItemClicked(ImGuiMouseButton_Right) && hot > 0 && hot < int(pts.size()) - 1) { pts.erase(pts.begin() + hot); changed = true; }
  if(changed) {
    std::string s;
    for(auto& [x, y] : pts) s += (s.empty() ? "" : " ") + std::to_string(x) + "," + std::to_string(y);
    parts[channel] = s;
    v.text[p.id] = parts[0] + "|" + parts[1] + "|" + parts[2] + "|" + parts[3];
  }
  return changed;
}

// The bar colors of a number param whose name says what it changes; empty = the plain track
static std::function<ImVec4(float)> paramColors(const EffectParam& p) {
  auto gray = [](float v) { return ImVec4(v, v, v, 1); };
  const std::string& id = p.id;
  if(id == "hue") return [](float t) {
    // A hue shift: the middle is no change (red stays red)
    float h = std::fmod(t * 360 + 180, 360) / 60, x = 1 - std::fabs(std::fmod(h, 2.f) - 1);
    int k = int(h) % 6;
    float r[6] = { 1, x, 0, 0, x, 1 }, g[6] = { x, 1, 1, x, 0, 0 }, b[6] = { 0, 0, x, 1, 1, x };
    return ImVec4(r[k], g[k], b[k], 1);
  };
  if(id == "saturation") return [](float t) { return ImVec4(0.6f + 0.4f * t, 0.6f - 0.45f * t, 0.6f - 0.45f * t, 1); };
  if(id == "brightness" || id == "lightness" || id == "inBlack" || id == "inWhite" || id == "outBlack" || id == "outWhite")
    return [gray](float t) { return gray(t); };
  return {};
}

// One widget per param; true when a value changed
bool paramWidget(const EffectParam& p, ParamValues& v, float dpi, const std::function<void(const std::string&)>& browse) {
  if(p.type == EParam::Curve) {
    ImGui::PushID(p.id.c_str());
    bool c = curveWidget(p, v, dpi);
    ImGui::PopID();
    return c;
  }
  ImGui::PushID(p.id.c_str());
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(tr(p.label.c_str()));
  ImGui::SameLine(130 * dpi);
  ImGui::SetNextItemWidth(220 * dpi);
  bool changed = false;
  double& val = v.num[p.id];
  switch(p.type) {
    case EParam::Float:
    case EParam::Int: {
      float f = float(val);
      if(ui::band("##v", f, float(p.min), float(p.max), p.type == EParam::Int ? 1.f : 0.01f, dpi, paramColors(p), 220 * dpi)) { val = f; changed = true; }
      break;
    }
    case EParam::Bool: {
      bool b = val != 0;
      if(ImGui::Checkbox("##v", &b)) { val = b; changed = true; }
      break;
    }
    case EParam::Choice: {
      int i = int(val);
      std::vector<const char*> names;
      for(auto& c : p.choices) names.push_back(tr(c.c_str()));
      if(ImGui::Combo("##v", &i, names.data(), int(names.size()))) { val = i; changed = true; }
      break;
    }
    case EParam::Color: {
      uint32_t c = uint32_t(val);
      float f[4] = { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f };
      if(ImGui::ColorEdit4("##v", f, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
        uint32_t o = 0;
        for(int k = 0; k < 4; k++) o |= uint32_t(std::clamp(f[k], 0.f, 1.f) * 255 + 0.5f) << (k * 8);
        val = double(o);
        changed = true;
      }
      break;
    }
    case EParam::Curve: break;
    case EParam::Text: {
      // The dither pattern picks from the loaded patterns
      if(p.id == "pattern") {
        const Pattern* cur = findPattern(v.str(p.id));
        if(ImGui::BeginCombo("##v", cur ? cur->name.c_str() : tr("(none)"))) {
          for(auto& pt : patterns())
            if(ImGui::Selectable(pt.name.c_str(), cur == &pt)) { v.text[p.id] = pt.id; changed = true; }
          ImGui::EndCombo();
        }
        break;
      }
      char buf[512];
      std::snprintf(buf, sizeof buf, "%s", v.str(p.id).c_str());
      // A file param gets a browse button next to it
      bool file = p.id == "file";
      if(file) ImGui::SetNextItemWidth(220 * dpi - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x);
      if(ImGui::InputText("##v", buf, sizeof buf)) { v.text[p.id] = buf; changed = true; }
      if(file) {
        ImGui::SameLine();
        if(ui::iconButton("##browse", ICON_FA_FOLDER_OPEN, { ImGui::GetFrameHeight(), ImGui::GetFrameHeight() }, false, tr("Browse"))) browse(p.id);
      }
      break;
    }
  }
  ImGui::PopID();
  return changed;
}

void App::drawEffectDialog() {
  // ImGui paints the dim at the end of the frame, so the color is set here rather than pushed
  ImGui::GetStyle().Colors[ImGuiCol_ModalWindowDimBg].w = _fx && !_fx->dim ? 0.f : 0.5f;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = (_fx ? std::string(_fx->icon) + "  " + tr(_fx->name.c_str()) : std::string()) + "###effect";
  if(!ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    // Closed some other way: drop the preview
    if(_fx && !ImGui::IsPopupOpen("###effect", ImGuiPopupFlags_AnyPopupId) && _popup != "###effect") finishEffect(false);
    return;
  }
  if(!_fx) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
  bool changed = false;
  for(auto& p : _fx->params) if(p.shown(_fxValues)) changed |= paramWidget(p, _fxValues, _dpi, [this](const std::string& id) { showEffectFileDialog(id); });
  if(changed) previewEffect();
  if(_doc->frameCount() > 1) {
    int from, to;
    selectedFrames(from, to);
    std::string label = to > from ? std::string(tr("Frames")) + " " + std::to_string(from + 1) + "-" + std::to_string(to + 1) : tr("All frames");
    ImGui::Checkbox(label.c_str(), &_fxAllFrames);
    ImGui::SetItemTooltip("%s", tr("The preview shows the current frame; OK applies to the others too"));
  }
  ImGui::Spacing();
  if(ui::iconButton("##reset", ICON_FA_ROTATE_LEFT, { ImGui::GetFrameHeight(), ImGui::GetFrameHeight() }, false, tr("Defaults"))) {
    _fxValues = defaultValues(*_fx);
    for(auto& p : _fx->params) if(p.type == EParam::Color) _fxValues.num[p.id] = double(_primary);
    previewEffect();
  }
  ImGui::SameLine();
  float bw = 100 * _dpi;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 2 - ImGui::GetStyle().ItemSpacing.x));
  bool ok = ui::dialogButton(tr("OK"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  ImGui::SameLine();
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape);
  if(ok || cancel) {
    finishEffect(ok);
    ImGui::CloseCurrentPopup();
  }
  ImGui::EndPopup();
}

// Adjustments and Effects menus, built from the registered effects ("Effects/Blurs" = Effects > Blurs)
void App::effectsMenu(const char* top) {
  std::vector<std::string> subs;
  for(auto& e : effects()) {
    if(e.hidden || e.category.rfind(top, 0) != 0) continue;
    std::string sub = e.category.size() > std::strlen(top) + 1 ? e.category.substr(std::strlen(top) + 1) : "";
    if(std::find(subs.begin(), subs.end(), sub) == subs.end()) subs.push_back(sub);
  }
  if(!std::strcmp(top, "Effects")) {
    _commands.menuItem("effect.repeat");
    if(!subs.empty()) ImGui::Separator();
  }
  for(auto& sub : subs) {
    bool open = sub.empty() || ImGui::BeginMenu(tr(sub.c_str()));
    if(!open) continue;
    for(auto& e : effects())
      if(!e.hidden && e.category == (sub.empty() ? std::string(top) : std::string(top) + "/" + sub)) {
        // Mods loaded after start have no command (and no shortcut) yet
        if(_commands.find("effect." + e.id)) _commands.menuItem("effect." + e.id);
        else if(ImGui::MenuItem((std::string(ICON_FA_PUZZLE_PIECE "  ") + tr(e.name.c_str()) + "##" + e.id).c_str())) openEffect(e.id);
      }
    if(!sub.empty()) ImGui::EndMenu();
  }
}

}
