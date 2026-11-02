#include "core/TextLayout.h"
#include "core/Vector.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_OUTLINE_H
#include <hb.h>
#include <hb-ft.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace david {

namespace {
  struct Face {
    FT_Face ft = nullptr;
    hb_font_t* hb = nullptr;
  };

  // FreeType and the loaded faces live for the whole run (the UI thread only)
  struct Fonts {
    FT_Library lib = nullptr;
    std::map<std::string, Face> faces;

    Fonts() { FT_Init_FreeType(&lib); }
    ~Fonts() {
      for(auto& [key, f] : faces) { hb_font_destroy(f.hb); FT_Done_Face(f.ft); }
      FT_Done_FreeType(lib);
    }

    Face* get(const std::string& path, int index) {
      std::string key = path + "#" + std::to_string(index);
      if(auto it = faces.find(key); it != faces.end()) return &it->second;
      Face f;
      if(!lib || FT_New_Face(lib, path.c_str(), index, &f.ft)) return nullptr;
      // Keep the cache small; big CJK faces hold a lot of memory
      if(faces.size() >= 16) {
        auto old = faces.begin();
        hb_font_destroy(old->second.hb);
        FT_Done_Face(old->second.ft);
        faces.erase(old);
      }
      return &(faces[key] = f);
    }
  };

  Fonts& fonts() {
    static Fonts f;
    return f;
  }
}

TextBlock layoutText(const std::u32string& text, const TextStyle& style, double x, double y, int imageW, int imageH) {
  TextBlock out;
  out.x = x; out.y = y;
  out.mask = std::make_unique<Mask>(imageW, imageH);
  Face* face = fonts().get(style.path, style.index);
  if(!face) return out;
  FT_Face ft = face->ft;
  if(FT_Set_Char_Size(ft, 0, FT_F26Dot6(std::max(style.size, 1.f) * 64), 72, 72)) return out;
  if(face->hb) hb_ft_font_changed(face->hb);
  else face->hb = hb_ft_font_create_referenced(ft);
  out.ok = true;

  double ascent = ft->size->metrics.ascender / 64.0, lineH = ft->size->metrics.height / 64.0;
  if(lineH <= 0) lineH = style.size * 1.2;

  // Shape each line first, so alignment knows the block width
  struct Glyph { unsigned id; double x, dx, dy; };
  struct Shaped { std::vector<Glyph> glyphs; double width = 0; };
  std::vector<Shaped> shaped;
  size_t start = 0;
  while(true) {
    size_t end = text.find(U'\n', start);
    if(end == std::u32string::npos) end = text.size();
    TextLine line;
    line.start = start; line.end = end;
    line.y = y + out.lines.size() * lineH;
    line.height = lineH;
    Shaped s;
    std::vector<double> carets(end - start + 1, -1);
    if(end > start) {
      hb_buffer_t* buf = hb_buffer_create();
      hb_buffer_add_utf32(buf, reinterpret_cast<const uint32_t*>(text.data()), int(text.size()), unsigned(start), int(end - start));
      hb_buffer_guess_segment_properties(buf);
      hb_shape(face->hb, buf, nullptr, 0);
      unsigned n = 0;
      hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
      hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
      double pen = 0;
      for(unsigned i = 0; i < n; i++) {
        size_t c = info[i].cluster - start;
        if(c < carets.size() && carets[c] < 0) carets[c] = pen;
        s.glyphs.push_back({ info[i].codepoint, pen, pos[i].x_offset / 64.0, pos[i].y_offset / 64.0 });
        pen += pos[i].x_advance / 64.0;
      }
      s.width = pen;
      hb_buffer_destroy(buf);
    }
    carets.back() = s.width;
    // Characters inside a cluster (ligatures, marks) share the cluster's caret
    double last = 0;
    for(double& c : carets) { if(c < 0) c = last; last = c; }
    line.carets = std::move(carets);
    out.lines.push_back(std::move(line));
    shaped.push_back(std::move(s));
    if(end >= text.size()) break;
    start = end + 1;
  }
  for(auto& s : shaped) out.width = std::max(out.width, s.width);
  out.height = out.lines.size() * lineH;

  // 12° shear for synthetic italic
  FT_Matrix shear{ 0x10000, FT_Fixed(0.2126 * 0x10000), 0, 0x10000 };
  for(size_t l = 0; l < out.lines.size(); l++) {
    TextLine& line = out.lines[l];
    double off = style.align == 1 ? (out.width - shaped[l].width) / 2 : style.align == 2 ? out.width - shaped[l].width : 0;
    for(double& c : line.carets) c += x + off;
    double baseline = line.y + ascent;
    for(const Glyph& g : shaped[l].glyphs) {
      double gx = x + off + g.x + g.dx;
      double fx = std::floor(gx);
      FT_Vector delta{ FT_Pos((gx - fx) * 64), 0 };
      FT_Set_Transform(ft, style.synthItalic ? &shear : nullptr, &delta);
      FT_Int32 flags = FT_LOAD_NO_BITMAP | (style.antialias ? FT_LOAD_TARGET_LIGHT : FT_LOAD_TARGET_MONO);
      if(FT_Load_Glyph(ft, g.id, flags)) continue;
      if(style.synthBold) FT_GlyphSlot_Embolden(ft->glyph);
      if(FT_Render_Glyph(ft->glyph, style.antialias ? FT_RENDER_MODE_NORMAL : FT_RENDER_MODE_MONO)) continue;
      const FT_Bitmap& bm = ft->glyph->bitmap;
      int ox = int(fx) + ft->glyph->bitmap_left, oy = int(std::round(baseline - g.dy)) - ft->glyph->bitmap_top;
      for(unsigned r = 0; r < bm.rows; r++) {
        int py = oy + int(r);
        if(py < 0 || py >= imageH) continue;
        const unsigned char* row = bm.buffer + r * bm.pitch;
        for(unsigned c = 0; c < bm.width; c++) {
          int px = ox + int(c);
          if(px < 0 || px >= imageW) continue;
          uint8_t v = bm.pixel_mode == FT_PIXEL_MODE_MONO ? ((row[c >> 3] >> (7 - (c & 7))) & 1 ? 255 : 0) : row[c];
          if(v > out.mask->get(px, py)) out.mask->set(px, py, v);
        }
      }
    }
  }
  FT_Set_Transform(ft, nullptr, nullptr);
  return out;
}

#pragma region [ Outlines ]
// Glyph outlines for vector text: the same shaping and line layout as layoutText, as Bezier paths
std::vector<Subpath> textOutline(const VecObject& o) {
  std::vector<Subpath> out;
  TextStyle style;
  style.path = o.font; style.index = o.fontIndex; style.size = float(o.size);
  style.synthBold = o.bold; style.synthItalic = o.italic; style.align = o.align;
  std::u32string text;
  // UTF-8 to UTF-32
  for(size_t i = 0; i < o.text.size();) {
    unsigned char c = o.text[i];
    int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    char32_t cp = n == 1 ? c : n == 2 ? c & 0x1F : n == 3 ? c & 0x0F : c & 0x07;
    for(int k = 1; k < n && i + k < o.text.size(); k++) cp = (cp << 6) | (o.text[i + k] & 0x3F);
    text += cp;
    i += n;
  }
  // The layout gives the lines and carets; a 1×1 mask keeps it from rasterizing anything visible
  TextBlock block = layoutText(text, style, o.x, o.y, 1, 1);
  Face* face = fonts().get(style.path, style.index);
  if(!block.ok || !face) return out;
  FT_Face ft = face->ft;
  FT_Set_Char_Size(ft, 0, FT_F26Dot6(std::max(style.size, 1.f) * 64), 72, 72);
  double ascent = ft->size->metrics.ascender / 64.0;
  FT_Matrix shear{ 0x10000, FT_Fixed(0.2126 * 0x10000), 0, 0x10000 };
  FT_Set_Transform(ft, style.synthItalic ? &shear : nullptr, nullptr);

  struct Ctx { std::vector<Subpath>* out; double ox, oy; Point last; };
  auto pt = [](const Ctx& c, const FT_Vector* v) { return Point{ c.ox + v->x / 64.0, c.oy - v->y / 64.0 }; };
  FT_Outline_Funcs funcs{};
  funcs.move_to = [](const FT_Vector* to, void* u) {
    auto& c = *static_cast<Ctx*>(u);
    Point p{ c.ox + to->x / 64.0, c.oy - to->y / 64.0 };
    c.out->push_back({ { PathNode::corner(p) }, true });
    c.last = p;
    return 0;
  };
  funcs.line_to = [](const FT_Vector* to, void* u) {
    auto& c = *static_cast<Ctx*>(u);
    Point p{ c.ox + to->x / 64.0, c.oy - to->y / 64.0 };
    c.out->back().nodes.push_back(PathNode::corner(p));
    c.last = p;
    return 0;
  };
  funcs.conic_to = [](const FT_Vector* ctl, const FT_Vector* to, void* u) {
    auto& c = *static_cast<Ctx*>(u);
    Point q{ c.ox + ctl->x / 64.0, c.oy - ctl->y / 64.0 }, p{ c.ox + to->x / 64.0, c.oy - to->y / 64.0 };
    // Quadratic to cubic: handles at 2/3 toward the control point
    auto& nodes = c.out->back().nodes;
    nodes.back().out = { c.last.x + (q.x - c.last.x) * 2 / 3, c.last.y + (q.y - c.last.y) * 2 / 3 };
    nodes.push_back({ p, { p.x + (q.x - p.x) * 2 / 3, p.y + (q.y - p.y) * 2 / 3 }, p, ENodeType::Corner });
    c.last = p;
    return 0;
  };
  funcs.cubic_to = [](const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* u) {
    auto& c = *static_cast<Ctx*>(u);
    Point p{ c.ox + to->x / 64.0, c.oy - to->y / 64.0 };
    auto& nodes = c.out->back().nodes;
    nodes.back().out = { c.ox + c1->x / 64.0, c.oy - c1->y / 64.0 };
    nodes.push_back({ p, { c.ox + c2->x / 64.0, c.oy - c2->y / 64.0 }, p, ENodeType::Corner });
    c.last = p;
    return 0;
  };
  (void)pt;

  // The glyphs again, placed by the layout's carets (first caret of each cluster) and baseline
  size_t start = 0;
  for(const TextLine& line : block.lines) {
    std::u32string part = text.substr(line.start, line.end - line.start);
    if(!part.empty()) {
      hb_buffer_t* buf = hb_buffer_create();
      hb_buffer_add_utf32(buf, reinterpret_cast<const uint32_t*>(part.data()), int(part.size()), 0, int(part.size()));
      hb_buffer_guess_segment_properties(buf);
      hb_shape(face->hb, buf, nullptr, 0);
      unsigned n = 0;
      hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
      hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
      double pen = line.carets.front();
      double baseline = line.y + ascent;
      for(unsigned i = 0; i < n; i++) {
        if(!FT_Load_Glyph(ft, info[i].codepoint, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) && ft->glyph->format == FT_GLYPH_FORMAT_OUTLINE) {
          if(style.synthBold) FT_Outline_Embolden(&ft->glyph->outline, FT_Pos(style.size * 64 / 24));
          Ctx ctx{ &out, pen + pos[i].x_offset / 64.0, baseline - pos[i].y_offset / 64.0, {} };
          FT_Outline_Decompose(&ft->glyph->outline, &funcs, &ctx);
        }
        pen += pos[i].x_advance / 64.0;
      }
      hb_buffer_destroy(buf);
    }
    start = line.end + 1;
  }
  (void)start;
  FT_Set_Transform(ft, nullptr, nullptr);
  // A contour closes back on its start point: drop the repeated last node, keeping its incoming handle
  for(auto& s : out) {
    auto& nd = s.nodes;
    if(nd.size() > 1 && std::abs(nd.back().p.x - nd[0].p.x) < 1e-6 && std::abs(nd.back().p.y - nd[0].p.y) < 1e-6) {
      nd[0].in = nd.back().in;
      nd.pop_back();
    }
  }
  return out;
}
#pragma endregion

}
