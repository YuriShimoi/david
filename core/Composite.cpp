#include "core/Composite.h"
#include "core/Blend.h"
#include "core/Document.h"
#include <algorithm>
#include <cstring>

namespace david {

uint32_t blendOver(uint32_t dst, uint32_t src, uint8_t opacity) {
  uint32_t sa = ((src >> 24) * opacity + 127) / 255;
  if(sa == 0) return dst;
  uint32_t da = dst >> 24;
  if(sa == 255 || da == 0) return (src & 0x00FFFFFF) | (sa << 24);

  // outA = sa + da*(1-sa); outC = (sc*sa + dc*da*(1-sa)) / outA, all in 0-255 fixed point
  uint32_t dw = da * (255 - sa);
  uint32_t outA255 = sa * 255 + dw;
  uint32_t out = 0;
  for(int s = 0; s < 24; s += 8) {
    uint32_t sc = (src >> s) & 0xFF, dc = (dst >> s) & 0xFF;
    uint32_t c = (sc * sa * 255 + dc * dw + outA255 / 2) / outA255;
    out |= c << s;
  }
  return out | (((outA255 + 127) / 255) << 24);
}

void flattenRow(const Document& doc, int y, uint32_t* out, int frame) {
  if(frame < 0) frame = doc.activeFrame;
  int w = doc.width();
  std::fill_n(out, w, 0u);
  int ty = y / Tile::SIZE, ry = y % Tile::SIZE;
  bool first = true;
  for(auto& layer : doc.layers) {
    if(!layer->visible || layer->opacity == 0) continue;
    for(int tx = 0; tx < layer->tilesX(); tx++) {
      const Tile* t = layer->tileAt(frame, tx, ty);
      if(!t) continue;
      int x0 = tx * Tile::SIZE, n = std::min(Tile::SIZE, w - x0);
      const uint32_t* src = t->data() + ry * Tile::SIZE;
      if(first && layer->opacity == 255 && layer->blend == EBlend::Normal) { std::memcpy(out + x0, src, n * 4); continue; }
      for(int i = 0; i < n; i++) out[x0 + i] = blendPixel(out[x0 + i], src[i], layer->opacity, layer->blend);
    }
    first = false;
  }
}

}
