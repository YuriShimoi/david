#include "render/Thumbnails.h"
#include "core/Document.h"
#include <SDL3/SDL_timer.h>
#include <algorithm>
#include <vector>

namespace david {

Thumbnails::Thumb Thumbnails::get(const Layer& layer, int maxSize) {
  uint64_t sig = 1469598103934665603ull;
  for(int ty = 0; ty < layer.tilesY(); ty++)
    for(int tx = 0; tx < layer.tilesX(); tx++) {
      const Tile* t = layer.tile(tx, ty);
      sig = (sig ^ (t ? t->revision() : 0)) * 1099511628211ull;
    }

  Entry& e = _map[layer.id()];
  uint64_t now = SDL_GetTicks();
  // While painting, rebuild at most ~6 times a second
  bool stale = e.signature != sig || e.maxSize != maxSize;
  if(e.thumb.tex && (!stale || now - e.built < 150)) return e.thumb;

  double scale = std::min(1.0, double(maxSize) / std::max(layer.width(), layer.height()));
  int w = std::max(1, int(layer.width() * scale)), h = std::max(1, int(layer.height() * scale));
  // Area average with premultiplied alpha, so thin lines and small details still show (point sampling skipped
  // them, and the thumbnail looked different from the canvas). Missing tiles count as transparent.
  int lw = layer.width(), lh = layer.height();
  std::vector<int> binX(lw), binY(lh);
  for(int x = 0; x < lw; x++) binX[x] = std::min(w - 1, int(int64_t(x) * w / lw));
  for(int y = 0; y < lh; y++) binY[y] = std::min(h - 1, int(int64_t(y) * h / lh));
  std::vector<uint64_t> sum(size_t(w) * h * 4, 0);
  std::vector<uint32_t> countX(w, 0), countY(h, 0);
  for(int x = 0; x < lw; x++) countX[binX[x]]++;
  for(int y = 0; y < lh; y++) countY[binY[y]]++;
  for(int ty = 0; ty < layer.tilesY(); ty++)
    for(int tx = 0; tx < layer.tilesX(); tx++) {
      const Tile* t = layer.tile(tx, ty);
      if(!t) continue;
      int x0 = tx * Tile::SIZE, y0 = ty * Tile::SIZE;
      int x1 = std::min(lw, x0 + Tile::SIZE), y1 = std::min(lh, y0 + Tile::SIZE);
      for(int y = y0; y < y1; y++) {
        const uint32_t* row = t->data() + (y - y0) * Tile::SIZE;
        uint64_t* srow = sum.data() + size_t(binY[y]) * w * 4;
        for(int x = x0; x < x1; x++) {
          uint32_t c = row[x - x0], a = c >> 24;
          if(!a) continue;
          uint64_t* s = srow + size_t(binX[x]) * 4;
          s[0] += (c & 0xFF) * a; s[1] += ((c >> 8) & 0xFF) * a; s[2] += ((c >> 16) & 0xFF) * a; s[3] += a;
        }
      }
    }
  std::vector<uint32_t> px(size_t(w) * h);
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      size_t i = size_t(y) * w + x;
      const uint64_t* s = sum.data() + i * 4;
      if(!s[3]) continue;
      uint64_t n = uint64_t(countX[x]) * countY[y];
      uint32_t r = uint32_t(s[0] / s[3]), g = uint32_t(s[1] / s[3]), b = uint32_t(s[2] / s[3]), a = uint32_t((s[3] + n / 2) / n);
      px[i] = r | g << 8 | b << 16 | std::min(a, 255u) << 24;
    }

  if(!e.thumb.tex) glGenTextures(1, &e.thumb.tex);
  glBindTexture(GL_TEXTURE_2D, e.thumb.tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  e.thumb.w = w;
  e.thumb.h = h;
  e.signature = sig;
  e.built = now;
  e.maxSize = maxSize;
  return e.thumb;
}

void Thumbnails::prune(const Document& doc) {
  for(auto it = _map.begin(); it != _map.end();) {
    if(!doc.findLayer(it->first)) { glDeleteTextures(1, &it->second.thumb.tex); it = _map.erase(it); }
    else ++it;
  }
}

void Thumbnails::clear() {
  for(auto& [id, e] : _map) glDeleteTextures(1, &e.thumb.tex);
  _map.clear();
}

}
