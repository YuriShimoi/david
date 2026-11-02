#include "app/Cursors.h"
#include "ui/ToolIcons.h"
#include "platform/Paths.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <vector>

namespace david {

Cursors::~Cursors() {
  for(auto& [id, c] : _system) SDL_DestroyCursor(c);
  for(auto& [key, c] : _icons) if(c) SDL_DestroyCursor(c);
}

void Cursors::set(SDL_Cursor* c) {
  if(!c || c == _current) return;
  SDL_SetCursor(c);
  _current = c;
}

SDL_Cursor* Cursors::system(SDL_SystemCursor id) {
  auto& c = _system[id];
  if(!c) c = SDL_CreateSystemCursor(id);
  return c;
}

void Cursors::ui(ImGuiMouseCursor c, bool clickable) {
  SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
  switch(c) {
    case ImGuiMouseCursor_TextInput: id = SDL_SYSTEM_CURSOR_TEXT; break;
    case ImGuiMouseCursor_ResizeAll: id = SDL_SYSTEM_CURSOR_MOVE; break;
    case ImGuiMouseCursor_ResizeNS: id = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
    case ImGuiMouseCursor_ResizeEW: id = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
    case ImGuiMouseCursor_ResizeNESW: id = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
    case ImGuiMouseCursor_ResizeNWSE: id = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
    case ImGuiMouseCursor_Hand: id = SDL_SYSTEM_CURSOR_POINTER; break;
    case ImGuiMouseCursor_NotAllowed: id = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
    default: if(clickable) id = SDL_SYSTEM_CURSOR_POINTER; break;
  }
  set(system(id));
}

void Cursors::canvas(ECursor c, const Tool& tool, float scale) {
  switch(c) {
    case ECursor::Cross: set(system(SDL_SYSTEM_CURSOR_CROSSHAIR)); break;
    case ECursor::Move: set(system(SDL_SYSTEM_CURSOR_MOVE)); break;
    case ECursor::Text: set(system(SDL_SYSTEM_CURSOR_TEXT)); break;
    case ECursor::Pointer: set(system(SDL_SYSTEM_CURSOR_POINTER)); break;
    case ECursor::Icon: {
      SDL_Cursor* ic = icon(tool, scale);
      set(ic ? ic : system(SDL_SYSTEM_CURSOR_CROSSHAIR));
      break;
    }
    default: set(system(SDL_SYSTEM_CURSOR_DEFAULT)); break;
  }
}

// Where the icon "touches" the canvas, as a fraction of the glyph box (Font Awesome draws tips bottom-left)
static void hotspot(ETool id, float& fx, float& fy) {
  fx = 0; fy = 1;
  if(id == ETool::Bucket) { fx = 1; fy = 1; }
  if(id == ETool::Zoom) { fx = 0.42f; fy = 0.42f; }
  if(id == ETool::Eraser) { fx = 0.2f; fy = 0.85f; }
}

static uint32_t decodeUtf8(const char* s) {
  const unsigned char* u = reinterpret_cast<const unsigned char*>(s);
  if(u[0] < 0x80) return u[0];
  if((u[0] >> 5) == 6) return ((u[0] & 0x1F) << 6) | (u[1] & 0x3F);
  if((u[0] >> 4) == 14) return ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F);
  return ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
}

// The tool's icon in black with a white outline, like the system arrow, cached per tool and scale
SDL_Cursor* Cursors::icon(const Tool& tool, float scale) {
  std::string key = std::string(tool.icon) + "@" + std::to_string(int(scale * 100));
  if(auto it = _icons.find(key); it != _icons.end()) return it->second;
  SDL_Cursor*& out = _icons[key];
  FT_Library lib;
  if(FT_Init_FreeType(&lib)) return out = nullptr;
  FT_Face face;
  uint32_t cp = decodeUtf8(tool.icon);
  std::string font = paths::exeDir() + (cp >= ICON_MIN_DAVID && cp <= ICON_MAX_DAVID ? "assets/fonts/david-icons.ttf" : "assets/fonts/fa-solid-900.ttf");
  if(FT_New_Face(lib, font.c_str(), 0, &face)) { FT_Done_FreeType(lib); return out = nullptr; }
  int size = std::max(12, int(std::round(18 * scale)));
  FT_Set_Pixel_Sizes(face, 0, size);
  if(!FT_Load_Char(face, cp, FT_LOAD_RENDER) && face->glyph->bitmap.width > 0) {
    const FT_Bitmap& bm = face->glyph->bitmap;
    int pad = std::max(2, int(std::round(1.5f * scale))), gw = int(bm.width), gh = int(bm.rows);
    int w = gw + pad * 2, h = gh + pad * 2;
    std::vector<float> glyph(size_t(w) * h, 0.f), ring(size_t(w) * h, 0.f);
    for(int y = 0; y < gh; y++)
      for(int x = 0; x < gw; x++) glyph[size_t(y + pad) * w + x + pad] = bm.buffer[y * bm.pitch + x] / 255.f;
    // Outline: the glyph dilated by a disc of radius pad
    for(int y = 0; y < h; y++)
      for(int x = 0; x < w; x++) {
        float m = 0;
        for(int dy = -pad; dy <= pad; dy++)
          for(int dx = -pad; dx <= pad; dx++) {
            int sx = x + dx, sy = y + dy;
            if(sx < 0 || sy < 0 || sx >= w || sy >= h || dx * dx + dy * dy > pad * pad + 1) continue;
            m = std::max(m, glyph[size_t(sy) * w + sx]);
          }
        ring[size_t(y) * w + x] = m;
      }
    if(SDL_Surface* surf = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA32)) {
      for(int y = 0; y < h; y++) {
        uint8_t* row = static_cast<uint8_t*>(surf->pixels) + y * surf->pitch;
        for(int x = 0; x < w; x++) {
          float g = glyph[size_t(y) * w + x], a = std::max(g, ring[size_t(y) * w + x]);
          uint8_t c = uint8_t(std::round((1 - g) * 255));
          row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = c;
          row[x * 4 + 3] = uint8_t(std::round(a * 255));
        }
      }
      float fx, fy;
      hotspot(tool.id, fx, fy);
      out = SDL_CreateColorCursor(surf, pad + int(std::round(fx * (gw - 1))), pad + int(std::round(fy * (gh - 1))));
      SDL_DestroySurface(surf);
    }
  }
  FT_Done_Face(face);
  FT_Done_FreeType(lib);
  return out;
}

}
