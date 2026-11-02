#include "core/Layer.h"
#include "core/Color.h"
#include "core/Vector.h"
#include <algorithm>
#include <atomic>
#include <map>

namespace david {

static std::atomic<uint64_t> s_nextLayerId = 1;

Layer::Layer(int width, int height, std::string name)
  : name(std::move(name)), _id(s_nextLayerId++), _width(width), _height(height),
    _tilesX((width + Tile::SIZE - 1) / Tile::SIZE), _tilesY((height + Tile::SIZE - 1) / Tile::SIZE) {
  _cels.resize(1);
}

std::unique_ptr<Layer> Layer::clone(bool sameId) const {
  auto l = std::make_unique<Layer>(_width, _height, name);
  if(sameId) l->_id = _id;
  l->kind = kind;
  l->visible = visible;
  l->opacity = opacity;
  l->blend = blend;
  l->_frame = _frame;
  l->_cels.clear();
  std::map<const Cel*, std::shared_ptr<Cel>> copies;
  for(int f = 0; f < frames(); f++) {
    const Cel* c = _cels[f].get();
    if(!c) { l->_cels.push_back(nullptr); continue; }
    auto& copy = copies[c];
    if(!copy) copy = copyCel(f);
    l->_cels.push_back(copy);
  }
  return l;
}

Cel& Layer::ensureCel(int frame) {
  auto& c = _cels[frame];
  if(!c) {
    c = std::make_shared<Cel>();
    c->tiles.resize(size_t(_tilesX) * _tilesY);
  }
  return *c;
}

Tile& Layer::ensureTile(int tx, int ty) {
  auto& t = ensureCel(_frame).tiles[ty * _tilesX + tx];
  if(!t) t = std::make_unique<Tile>();
  return *t;
}

std::unique_ptr<Tile> Layer::takeTile(int tx, int ty) {
  auto& c = _cels[_frame];
  return c ? std::move(c->tiles[ty * _tilesX + tx]) : nullptr;
}

Tile* Layer::tileAt(int frame, int tx, int ty) const {
  const auto& c = _cels[frame];
  return c ? c->tiles[ty * _tilesX + tx].get() : nullptr;
}

uint32_t Layer::getAt(int frame, int x, int y) const {
  if(!contains(x, y)) return CLEAR;
  const Tile* t = tileAt(frame, x / Tile::SIZE, y / Tile::SIZE);
  return t ? t->get(x % Tile::SIZE, y % Tile::SIZE) : CLEAR;
}

void Layer::putTileAt(int frame, int tx, int ty, std::unique_ptr<Tile> t) {
  if(!t && !_cels[frame]) return;
  if(t) t->touch();
  ensureCel(frame).tiles[ty * _tilesX + tx] = std::move(t);
}

void Layer::set(int x, int y, uint32_t v) {
  if(!contains(x, y)) return;
  ensureTile(x / Tile::SIZE, y / Tile::SIZE).set(x % Tile::SIZE, y % Tile::SIZE, v);
}

void Layer::fill(uint32_t v) {
  if(v == CLEAR) {
    if(_cels[_frame]) for(auto& t : _cels[_frame]->tiles) t.reset();
    return;
  }
  Cel& c = ensureCel(_frame);
  for(int ty = 0; ty < _tilesY; ty++) {
    for(int tx = 0; tx < _tilesX; tx++) {
      // Pixels past the image edge stay transparent, so edge tiles are filled only inside the bounds
      auto t = std::make_unique<Tile>();
      int w = std::min(Tile::SIZE, _width - tx * Tile::SIZE);
      int h = std::min(Tile::SIZE, _height - ty * Tile::SIZE);
      for(int y = 0; y < h; y++)
        std::fill_n(t->data() + y * Tile::SIZE, w, v);
      t->touch();
      c.tiles[ty * _tilesX + tx] = std::move(t);
    }
  }
}

size_t Layer::allocatedBytes() const {
  // Linked cels count once
  size_t n = 0;
  for(int f = 0; f < frames(); f++) {
    if(!_cels[f]) continue;
    bool seen = false;
    for(int g = 0; g < f && !seen; g++) seen = _cels[g] == _cels[f];
    if(seen) continue;
    for(auto& t : _cels[f]->tiles) if(t) n += Tile::bytes();
  }
  return n;
}

bool Layer::celEmpty(int frame) const {
  if(!_cels[frame]) return true;
  for(auto& t : _cels[frame]->tiles) if(t && !t->isEmpty()) return false;
  return true;
}

std::shared_ptr<Cel> Layer::removeCel(int frame) {
  auto c = std::move(_cels[frame]);
  _cels.erase(_cels.begin() + frame);
  return c;
}

void Layer::moveCel(int from, int to) {
  auto c = std::move(_cels[from]);
  _cels.erase(_cels.begin() + from);
  _cels.insert(_cels.begin() + to, std::move(c));
}

std::shared_ptr<Cel> Layer::copyCel(int frame) const {
  const auto& src = _cels[frame];
  if(!src) return nullptr;
  auto c = std::make_shared<Cel>();
  c->tiles.resize(src->tiles.size());
  for(size_t i = 0; i < src->tiles.size(); i++)
    if(src->tiles[i]) c->tiles[i] = src->tiles[i]->clone();
  if(src->vec) c->vec = std::make_shared<VecTree>(*src->vec);
  return c;
}

void Layer::fitFrames(int count, int active) {
  if(frames() == count) return;
  auto image = _cels[_frame];
  _cels.assign(size_t(count), nullptr);
  _frame = std::clamp(active, 0, count - 1);
  _cels[_frame] = std::move(image);
}

}
