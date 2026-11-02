#include "ui/tools/Tool.h"
#include "core/Document.h"
#include "core/Paint.h"
#include "core/TextLayout.h"
#include "platform/Fonts.h"
#include "platform/Paths.h"
#include "ui/CanvasNav.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/ToolIcons.h"
#include "ui/Widgets.h"
#include <SDL3/SDL_mouse.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>

namespace david {

static bool toggle(const char* icon, bool& v, const char* tip) {
  return ui::iconToggle(icon, icon, v, tr(tip));
}

static std::string lowerCase(std::string s) {
  for(char& c : s) c = char(std::tolower((unsigned char)c));
  return s;
}

// The installed fonts, listed once in the background on first use
class FontList {
public:
  const std::vector<FontFamily>* get() {
    if(!_started) {
      _started = true;
      _future = std::async(std::launch::async, systemFonts);
    }
    if(!_ready && _future.valid() && _future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
      _list = _future.get();
      _ready = true;
    }
    return _ready ? &_list : nullptr;
  }

  const FontFamily* find(const std::string& name) {
    auto* list = get();
    if(!list) return nullptr;
    std::string want = lowerCase(name);
    for(auto& f : *list) if(lowerCase(f.name) == want) return &f;
    return nullptr;
  }

  // The family used when none is chosen yet
  const FontFamily* fallback() {
    for(const char* n : { "Segoe UI", "Ubuntu", "Noto Sans", "DejaVu Sans", "Arial", "Liberation Sans" })
      if(auto* f = find(n)) return f;
    auto* list = get();
    return list && !list->empty() ? &list->front() : nullptr;
  }

private:
  bool _started = false, _ready = false;
  std::future<std::vector<FontFamily>> _future;
  std::vector<FontFamily> _list;
};

static FontList s_fonts;

// Click to place the caret and type; the text stays editable (font, size, style, color) until a click away,
// Escape, a tool switch or another edit. The nub at the top-left moves it.
class TextTool : public Tool {
public:
  TextTool() : Tool(ETool::Text, ICON_TB_TYPOGRAPHY, N_("Text"), ImGuiKey_T) {}

  void down(ToolHost& h, const ToolInput& in) override {
    Point p{ in.x, in.y };
    if(_editing) {
      ImVec2 nub = h.toScreen(_block.x, _block.y);
      if(overNub(in, nub, h.dpi())) { _moving = true; _grab = p; return; }
      double pad = 4 / h.nav().view.zoom;
      if(p.x >= _block.x - pad && p.x <= _block.x + std::max(_block.width, 1.0) + pad && p.y >= _block.y - pad && p.y <= _block.y + _block.height + pad) {
        _caret = hitCaret(p);
        return;
      }
      commit(h);
    }
    s_fonts.get();
    _editing = true;
    _text.clear();
    _caret = 0;
    _x = p.x; _y = p.y;
    _color = h.color(in.button != SDL_BUTTON_RIGHT);
    render(h);
  }

  void drag(ToolHost& h, const ToolInput& in) override {
    if(!_moving) return;
    _x += in.x - _grab.x; _y += in.y - _grab.y;
    _grab = { in.x, in.y };
    render(h);
  }

  void up(ToolHost&, const ToolInput&) override { _moving = false; }

  bool busy() const override { return _editing; }

  ECursor cursor(ToolHost& h, const ToolInput& at) override {
    if(_moving || (_editing && overNub(at, h.toScreen(_block.x, _block.y), h.dpi()))) return ECursor::Move;
    return ECursor::Text;
  }
  bool wantsText() const override { return _editing; }

  void commit(ToolHost& h) override {
    if(!_editing) return;
    if(_text.empty()) _edit.cancel();
    else {
      _edit.commit(h.doc(), name, icon);
      remember(h);
    }
    _editing = _moving = false;
    h.doc().onChanged();
  }

  void cancel(ToolHost& h) override { commit(h); }

  void onText(ToolHost& h, unsigned int c) override {
    if(!_editing || c < 32 || c == 127) return;
    _text.insert(_text.begin() + _caret, char32_t(c));
    _caret++;
    render(h);
  }

  bool onKey(ToolHost& h, ImGuiKey k) override {
    if(!_editing) return false;
    if(k == ImGuiKey_Escape) commit(h);
    else if(k == ImGuiKey_Enter || k == ImGuiKey_KeypadEnter) {
      if(ImGui::GetIO().KeyCtrl) { commit(h); return true; }
      _text.insert(_text.begin() + _caret, U'\n');
      _caret++;
      render(h);
    } else if(k == ImGuiKey_Backspace) {
      if(_caret > 0) { _text.erase(_caret - 1, 1); _caret--; render(h); }
    } else return false;
    return true;
  }

  void keys(ToolHost& h) override {
    if(!_editing) return;
    auto pressed = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };
    size_t line = lineOf(_caret);
    if(pressed(ImGuiKey_LeftArrow) && _caret > 0) _caret--;
    if(pressed(ImGuiKey_RightArrow) && _caret < _text.size()) _caret++;
    if(pressed(ImGuiKey_Home) && line < _block.lines.size()) _caret = _block.lines[line].start;
    if(pressed(ImGuiKey_End) && line < _block.lines.size()) _caret = _block.lines[line].end;
    if((pressed(ImGuiKey_UpArrow) || pressed(ImGuiKey_DownArrow)) && line < _block.lines.size()) {
      int dir = ImGui::IsKeyDown(ImGuiKey_UpArrow) ? -1 : 1;
      int target = int(line) + dir;
      if(target >= 0 && target < int(_block.lines.size())) {
        const TextLine& from = _block.lines[line];
        double x = from.carets[_caret - from.start];
        _caret = hitCaret({ x, _block.lines[target].y + _block.lines[target].height / 2 });
      }
    }
    if(pressed(ImGuiKey_Delete) && _caret < _text.size()) { _text.erase(_caret, 1); render(h); }
  }

  void overlay(ToolHost& h, ImDrawList* dl, const ToolInput& at) override {
    if(!_editing) return;
    float dpi = h.dpi();
    double w = std::max(_block.width, h.settings().fontSize * 0.3);
    ImVec2 a = h.toScreen(_block.x, _block.y), b = h.toScreen(_block.x + w, _block.y + std::max(_block.height, double(h.settings().fontSize)));
    antsLine(dl, a, { b.x, a.y }, 0, dpi); antsLine(dl, { b.x, a.y }, b, 0, dpi);
    antsLine(dl, b, { a.x, b.y }, 0, dpi); antsLine(dl, { a.x, b.y }, a, 0, dpi);
    drawNub(dl, a, dpi, overNub(at, a, dpi));
    // Blinking caret
    size_t line = lineOf(_caret);
    if(line < _block.lines.size() && std::fmod(ImGui::GetTime(), 1.0) < 0.6) {
      const TextLine& l = _block.lines[line];
      double x = l.carets[std::min(_caret - l.start, l.carets.size() - 1)];
      ImVec2 c0 = h.toScreen(x, l.y), c1 = h.toScreen(x, l.y + l.height);
      dl->AddLine(c0, c1, IM_COL32(0, 0, 0, 255), 3 * dpi);
      dl->AddLine(c0, c1, IM_COL32(255, 255, 255, 255), 1 * dpi);
    }
  }

  void options(ToolHost& h) override {
    auto& s = h.settings();
    float dpi = h.dpi();
    bool changed = false;
    changed |= fontPicker(h);
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_TB_TEXT_SIZE);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * dpi);
    changed |= ImGui::DragFloat("##size", &s.fontSize, 0.5f, 1, 1000, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetItemTooltip("%s", tr("Font size"));
    ImGui::SameLine();
    changed |= toggle(ICON_TB_BOLD, s.bold, N_("Bold"));
    ImGui::SameLine();
    changed |= toggle(ICON_TB_ITALIC, s.italic, N_("Italic"));
    ImGui::SameLine();
    const char* aligns[] = { ICON_TB_ALIGN_LEFT, ICON_TB_ALIGN_CENTER, ICON_TB_ALIGN_RIGHT };
    const char* alignTips[] = { N_("Align left"), N_("Center"), N_("Align right") };
    for(int i = 0; i < 3; i++) {
      bool on = s.textAlign == i;
      if(toggle(aligns[i], on, alignTips[i])) { s.textAlign = i; changed = true; }
      ImGui::SameLine(0, 2);
    }
    changed |= s.antialias != _antialias;
    _antialias = s.antialias;
    if(changed && _editing) render(h);
  }

private:
  LiveEdit _edit;
  TextBlock _block;
  std::u32string _text;
  size_t _caret = 0;
  double _x = 0, _y = 0;
  Point _grab{};
  uint32_t _color = 0;
  bool _editing = false, _moving = false, _antialias = true;
  std::string _query;

  size_t lineOf(size_t caret) const {
    for(size_t i = 0; i < _block.lines.size(); i++)
      if(caret <= _block.lines[i].end) return i;
    return _block.lines.empty() ? 0 : _block.lines.size() - 1;
  }

  size_t hitCaret(Point p) const {
    if(_block.lines.empty()) return 0;
    size_t li = 0;
    for(size_t i = 0; i < _block.lines.size(); i++) if(p.y >= _block.lines[i].y) li = i;
    const TextLine& l = _block.lines[li];
    size_t best = 0;
    for(size_t i = 0; i < l.carets.size(); i++)
      if(std::abs(l.carets[i] - p.x) < std::abs(l.carets[best] - p.x)) best = i;
    return l.start + best;
  }

  // The chosen family's face; synthetic bold/italic when the family has no such face
  TextStyle style(ToolHost& h) {
    auto& s = h.settings();
    TextStyle st;
    st.size = s.fontSize;
    st.align = s.textAlign;
    st.antialias = s.antialias;
    const FontFamily* fam = s.fontFamily.empty() ? s_fonts.fallback() : s_fonts.find(s.fontFamily);
    if(!fam) fam = s_fonts.fallback();
    if(const FontFace* f = fam ? pickFace(*fam, s.bold, s.italic) : nullptr) {
      st.path = f->path;
      st.index = f->index;
      st.synthBold = s.bold && f->weight < 600;
      st.synthItalic = s.italic && !f->italic;
    } else {
      st.path = paths::uiFont(s.bold);
      st.synthItalic = s.italic;
    }
    return st;
  }

  void render(ToolHost& h) {
    Document& doc = h.doc();
    TileRecorder& rec = _edit.redraw(doc);
    _block = layoutText(_text, style(h), _x, _y, doc.width(), doc.height());
    if(_block.ok) paintCoverage(doc, rec, { { _block.mask.get(), _color } });
    doc.onChanged();
  }

  void remember(ToolHost& h) {
    auto& s = h.settings();
    std::string name = s.fontFamily;
    if(name.empty()) return;
    auto& r = s.fontRecent;
    r.erase(std::remove(r.begin(), r.end(), name), r.end());
    r.insert(r.begin(), name);
    if(r.size() > 8) r.resize(8);
  }

  // A button with the current family, opening a searchable list where every name shows in its own font
  bool fontPicker(ToolHost& h) {
    auto& s = h.settings();
    float dpi = h.dpi();
    const FontFamily* current = s.fontFamily.empty() ? s_fonts.fallback() : s_fonts.find(s.fontFamily);
    std::string label = std::string(ICON_TB_TYPOGRAPHY) + "  " + (current ? current->name : s.fontFamily.empty() ? tr("Font") : s.fontFamily) + "##font";
    if(ImGui::Button(label.c_str(), { 170 * dpi, 0 })) { ImGui::OpenPopup("##fonts"); _query.clear(); s_fonts.get(); }
    ImGui::SetItemTooltip("%s", tr("Font"));
    bool changed = false;
    if(!ImGui::BeginPopup("##fonts")) return false;
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", _query.c_str());
    ImGui::SetNextItemWidth(320 * dpi);
    if(ImGui::InputTextWithHint("##search", tr("Search fonts"), buf, sizeof(buf))) _query = buf;
    const std::vector<FontFamily>* list = s_fonts.get();
    if(!list) {
      ImGui::TextDisabled("%s", tr("Loading fonts…"));
      ImGui::EndPopup();
      return false;
    }

    // Rows: favorites and recent fonts first when not searching; ranked matches when searching
    struct Row { const FontFamily* fam; const char* header; };
    std::vector<Row> rows;
    auto isFav = [&](const std::string& n) { return std::find(s.fontFavorites.begin(), s.fontFavorites.end(), n) != s.fontFavorites.end(); };
    if(_query.empty()) {
      auto section = [&](const std::vector<std::string>& names, const char* header) {
        bool first = true;
        for(auto& n : names)
          if(const FontFamily* f = s_fonts.find(n)) {
            if(first) rows.push_back({ nullptr, header });
            first = false;
            rows.push_back({ f, nullptr });
          }
      };
      section(s.fontFavorites, N_("Favorites"));
      section(s.fontRecent, N_("Recent"));
      rows.push_back({ nullptr, N_("All fonts") });
      for(auto& f : *list) rows.push_back({ &f, nullptr });
    } else {
      std::vector<std::pair<int, const FontFamily*>> hits;
      for(auto& f : *list)
        if(int score = fontMatchScore(f.name, _query)) hits.push_back({ score + (isFav(f.name) ? 50 : 0), &f });
      std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first > b.first; });
      for(auto& [score, f] : hits) rows.push_back({ f, nullptr });
      if(rows.empty()) rows.push_back({ nullptr, N_("No font matches") });
    }

    float rowH = ImGui::GetFrameHeight() * 1.25f;
    ImGui::BeginChild("##list", { 320 * dpi, 340 * dpi });
    ImGuiListClipper clip;
    clip.Begin(int(rows.size()), rowH);
    while(clip.Step()) {
      for(int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
        const Row& r = rows[i];
        ImGui::PushID(i);
        if(r.header) {
          ImGui::Dummy({ 0, (rowH - ImGui::GetTextLineHeight()) / 2 });
          ImGui::TextDisabled("%s", tr(r.header));
          ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowH - ImGui::GetTextLineHeight() - (rowH - ImGui::GetTextLineHeight()) / 2 - ImGui::GetStyle().ItemSpacing.y);
          ImGui::PopID();
          continue;
        }
        bool fav = isFav(r.fam->name);
        if(ui::iconButton("##fav", fav ? ICON_TB_STAR : ICON_TB_STAR_HALF, { rowH, rowH }, fav, fav ? tr("Remove from favorites") : tr("Add to favorites"))) {
          if(fav) s.fontFavorites.erase(std::remove(s.fontFavorites.begin(), s.fontFavorites.end(), r.fam->name), s.fontFavorites.end());
          else s.fontFavorites.push_back(r.fam->name);
        }
        ImGui::SameLine(0, 4 * dpi);
        // The name in its own font once it's loaded (fonts load between frames)
        const FontFace* face = pickFace(*r.fam, false, false);
        ImFont* preview = face ? h.previewFont(face->path, face->index) : nullptr;
        bool selected = current == r.fam;
        if(preview) ImGui::PushFont(preview, 0.0f);
        bool clicked = ImGui::Selectable(r.fam->name.c_str(), selected, ImGuiSelectableFlags_None, { 0, rowH });
        if(preview) ImGui::PopFont();
        if(clicked) {
          s.fontFamily = r.fam->name;
          changed = true;
          ImGui::CloseCurrentPopup();
        }
        ImGui::PopID();
      }
    }
    ImGui::EndChild();
    ImGui::EndPopup();
    return changed;
  }
};

std::unique_ptr<Tool> makeText() { return std::make_unique<TextTool>(); }

}
