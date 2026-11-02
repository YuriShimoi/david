#include "core/Autotile.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace david {

namespace {
// Each output tile lists its four quarters (TL, TR, BL, BR) as { source quarter, clockwise turns }, or { -1 } for
// plain floor. Source quarters come from the border tile: 0 TL, 1 TR, 2 BL, 3 BR.
struct Q { int src = -1, turns = 0; };
using Conf = std::array<Q, 4>;
const Q F{ -1, 0 };
const Conf AUTOTILE[48] = {
  {{ {3,3},{3,0},{0,2},{0,0} }}, {{ {3,3},{0,3},{0,2},{2,1} }}, {{ {0,3},{0,3},{2,2},{2,1} }}, {{ {0,3},{3,0},{2,2},{0,0} }},
  {{ F,{2,0},{2,2},{2,1} }}, {{ {0,3},{0,3},{2,2},F }}, {{ {0,3},{0,3},F,{2,1} }}, {{ {2,3},F,{2,2},{2,1} }},
  {{ {3,3},{0,3},{0,2},F }}, {{ {2,3},{2,0},F,F }}, {{ {0,3},{0,3},F,F }}, {{ {0,3},{3,0},F,{0,0} }},

  {{ {0,2},{0,0},{0,2},{0,0} }}, {{ {0,2},{2,0},{0,2},{2,1} }}, {{ {2,3},{2,0},{2,2},{2,1} }}, {{ {2,3},{0,0},{2,2},{0,0} }},
  {{ {0,2},{2,0},{0,2},F }}, {{ {2,3},F,F,F }}, {{ F,{2,0},F,F }}, {{ {2,3},{0,0},F,{0,0} }},
  {{ {0,2},F,{0,2},F }}, {{ {2,3},F,F,{2,1} }}, {{ {1,0},{1,0},{1,0},{1,0} }}, {{ F,{2,0},F,{2,1} }},

  {{ {0,2},{0,0},{3,2},{3,1} }}, {{ {0,2},{2,0},{3,2},{0,1} }}, {{ {2,3},{2,0},{0,1},{0,1} }}, {{ {2,3},{0,0},{0,1},{3,1} }},
  {{ {0,2},F,{0,2},{2,1} }}, {{ F,F,{2,2},F }}, {{ F,F,F,{2,1} }}, {{ F,{0,0},{2,2},{0,0} }},
  {{ {2,3},F,{2,2},F }}, {{ F,F,F,F }}, {{ F,{2,0},{2,2},F }}, {{ F,{0,0},F,{0,0} }},

  {{ {3,3},{3,0},{3,2},{3,1} }}, {{ {3,3},{0,3},{3,2},{0,1} }}, {{ {0,3},{0,3},{0,1},{0,1} }}, {{ {0,3},{3,0},{0,1},{3,1} }},
  {{ {2,3},{2,0},F,{2,1} }}, {{ {2,3},F,{0,1},{0,1} }}, {{ F,{2,0},{0,1},{0,1} }}, {{ {2,3},{2,0},{2,2},F }},
  {{ {0,2},F,{3,2},{0,1} }}, {{ F,F,{0,1},{0,1} }}, {{ F,F,{2,2},{2,1} }}, {{ F,{0,0},{0,1},{3,1} }},
};
// [left, top, right, bottom] sides a quarter trims (rotated with it), as in panda
const std::array<int, 4> BSLICE0{ 0, 0, 1, 0 }, BSLICE2{ 1, 0, 0, 1 }, BSLICE3{ 0, 1, 1, 0 };

std::array<int, 4> rotR(std::array<int, 4> a, int n) {
  for(int i = 0; i < ((n % 4) + 4) % 4; i++) std::rotate(a.rbegin(), a.rbegin() + 1, a.rend());
  return a;
}

int rnd(double v) { return int(std::lround(v)); }

Image crop(const Image& img, double fx0, double fy0, double fx1, double fy1) {
  int x0 = rnd(fx0), y0 = rnd(fy0), x1 = rnd(fx1), y1 = rnd(fy1);
  Image out(std::max(0, x1 - x0), std::max(0, y1 - y0));
  for(int y = 0; y < out.h; y++)
    for(int x = 0; x < out.w; x++)
      if(img.contains(x0 + x, y0 + y)) out.at(x, y) = img.at(x0 + x, y0 + y);
  return out;
}

// Clockwise quarter turns
Image turn(const Image& src, int k) {
  Image img = src;
  for(int t = 0; t < ((k % 4) + 4) % 4; t++) {
    Image out(img.h, img.w);
    for(int y = 0; y < img.h; y++)
      for(int x = 0; x < img.w; x++) out.at(img.h - 1 - y, x) = img.at(x, y);
    img = std::move(out);
  }
  return img;
}

// Inclusive rectangle cleared to transparent (PIL draw.rectangle)
void clear(Image& img, double fx0, double fy0, double fx1, double fy1) {
  int x0 = rnd(fx0), y0 = rnd(fy0), x1 = rnd(fx1), y1 = rnd(fy1);
  for(int y = std::max(0, y0); y <= std::min(img.h - 1, y1); y++)
    for(int x = std::max(0, x0); x <= std::min(img.w - 1, x1); x++) img.at(x, y) = 0;
}

// Plain copy, or blended by the source's own alpha (PIL paste with mask), per channel like panda
void paste(Image& dst, const Image& src, double fx, double fy, bool masked) {
  int x = rnd(fx), y = rnd(fy);
  for(int j = 0; j < src.h; j++)
    for(int i = 0; i < src.w; i++) {
      int dx = x + i, dy = y + j;
      if(!dst.contains(dx, dy)) continue;
      uint32_t s = src.at(i, j);
      if(!masked) { dst.at(dx, dy) = s; continue; }
      uint32_t d = dst.at(dx, dy), m = s >> 24, out = 0;
      for(int c = 0; c < 32; c += 8) out |= uint32_t(std::lround((((s >> c) & 0xFF) * m + ((d >> c) & 0xFF) * (255 - m)) / 255.0)) << c;
      dst.at(dx, dy) = out;
    }
}
}

const int AUTOTILE_MASKS[48] = { 16, 20, 84, 80, 213, 92, 116, 87, 28, 125, 124, 112, 17, 21, 85, 81, 29, 127, 253, 113, 31, 119, -1, 245,
  1, 5, 69, 65, 23, 223, 247, 209, 95, 255, 221, 241, 0, 4, 68, 64, 117, 71, 197, 93, 7, 199, 215, 193 };

Image autotile(const Image& src, int grid, int border, int corner) {
  grid = std::max(1, grid);
  if(border >= grid / 2.0) border = int(std::floor(grid / 2.0 - 1));
  if(border < 0) border = 0;
  if(corner >= grid / 2.0 - border) corner = int(std::floor(grid / 2.0 - border - 1));
  if(corner < 0) corner = 0;
  Image base = crop(src, 0, 0, grid, grid), floor = crop(src, grid, 0, grid * 2, grid);
  double h2 = grid / 2.0;
  const std::array<double, 4> slices[4] = { { 0, 0, h2, h2 }, { h2, 0, double(grid), h2 }, { 0, h2, h2, double(grid) }, { h2, h2, double(grid), double(grid) } };
  Image quarters[4];
  for(int i = 0; i < 4; i++) quarters[i] = crop(base, slices[i][0], slices[i][1], slices[i][2], slices[i][3]);
  Image out(grid * 12, grid * 4);
  for(int t = 0; t < 48; t++) {
    int col = t % 12, row = t / 12;
    for(int s = 0; s < 4; s++) {
      const Q& q = AUTOTILE[t][s];
      double pos[2] = { col * grid + slices[s][0], row * grid + slices[s][1] };
      std::array<double, 4> f = slices[s];
      if(q.src < 0) { paste(out, crop(floor, f[0], f[1], f[2], f[3]), pos[0], pos[1], false); continue; }
      Image tile = turn(quarters[q.src], q.turns);
      double fpos[2] = { pos[0], pos[1] };
      if(border && (q.src == 0 || q.src == 3)) {
        auto r = rotR(q.src == 0 ? BSLICE0 : BSLICE3, q.turns);
        f = { f[0] + r[0] * border, f[1] + r[1] * border, f[2] - r[2] * border, f[3] - r[3] * border };
        fpos[0] += r[0] * border;
        fpos[1] += r[1] * border;
      }
      Image trimmed = floor;
      if(corner && (q.src == 2 || q.src == 3)) {
        auto r = rotR(BSLICE2, q.turns);
        clear(trimmed, r[0] ? f[2] - corner : f[0], r[1] ? f[3] - corner : f[1], r[2] ? f[0] + corner - 1 : f[2], r[3] ? f[1] + corner - 1 : f[3]);
      }
      if(q.src != 1) paste(out, crop(trimmed, f[0], f[1], f[2], f[3]), fpos[0], fpos[1], false);
      paste(out, tile, pos[0], pos[1], true);
    }
  }
  return out;
}

}
