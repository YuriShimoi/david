#include "core/Effect.h"
#include "core/Composite.h"
#include <algorithm>
#include <vector>

namespace david {

// Distance in steps (4- or 8-connected) from the nearest source pixel, up to limit; limit + 1 = farther
static std::vector<int> stepDistance(const Image& img, bool fromSolid, bool eight, int limit) {
  int w = img.w, h = img.h;
  std::vector<int> dist(size_t(w) * h, limit + 1);
  std::vector<int> frontier, next;
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
      if(((img.at(x, y) >> 24) >= 128) == fromSolid) { dist[size_t(y) * w + x] = 0; frontier.push_back(y * w + x); }
  static const int dx8[] = { 1, -1, 0, 0, 1, 1, -1, -1 }, dy8[] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  int dirs = eight ? 8 : 4;
  for(int d = 1; d <= limit && !frontier.empty(); d++) {
    next.clear();
    for(int i : frontier) {
      int x = i % w, y = i / w;
      for(int k = 0; k < dirs; k++) {
        int nx = x + dx8[k], ny = y + dy8[k];
        if(nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
        int& nd = dist[size_t(ny) * w + nx];
        if(nd > d) { nd = d; next.push_back(ny * w + nx); }
      }
    }
    frontier.swap(next);
  }
  return dist;
}

void registerOutline() {
  Effect e;
  e.id = "outline";
  e.name = "Outline";
  e.category = "Effects/Object";
  e.params = {
    { "side", "Side", EParam::Choice, 0, 1, 0, { "Outside", "Inside" } },
    { "connect", "Neighbors", EParam::Choice, 0, 1, 1, { "4", "8" } },
    { "thickness", "Thickness", EParam::Int, 1, 16, 1 },
    { "color", "Color", EParam::Color, 0, 0, double(0xFF000000u) },
  };
  e.margin = [](const ParamValues& v) { return v.geti("thickness", 1) + 1; };
  e.run = [](const Image& src, Image& dst, const ParamValues& v, const EffectEnv&) {
    bool inside = v.geti("side") == 1, eight = v.geti("connect", 1) == 1;
    int t = std::clamp(v.geti("thickness", 1), 1, 64);
    uint32_t color = v.color("color");
    // Outside: transparent pixels near solid ones; inside: solid pixels near transparent ones
    auto dist = stepDistance(src, !inside, eight, t);
    for(int y = 0; y < src.h; y++)
      for(int x = 0; x < src.w; x++) {
        int d = dist[size_t(y) * src.w + x];
        if(d >= 1 && d <= t) dst.at(x, y) = blendOver(src.at(x, y), color);
      }
  };
  registerEffect(std::move(e));
}

}
