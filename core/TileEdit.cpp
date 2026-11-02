#include "core/TileEdit.h"
#include "core/Document.h"
#include <algorithm>

namespace david {

size_t TileEdit::bytes() const {
  size_t n = sizeof(*this);
  for(auto& e : entries) n += (e.before ? Tile::bytes() : 0) + (e.after ? Tile::bytes() : 0);
  return n;
}

void TileEdit::apply(Document& doc, bool after) {
  Layer* layer = doc.findLayer(layerId);
  if(!layer) return;
  for(auto& e : entries) {
    auto& src = after ? e.after : e.before;
    if(celFrame < layer->frames()) layer->putTileAt(celFrame, e.tx, e.ty, src ? src->clone() : nullptr);
  }
}

void TileRecorder::touch(int tx, int ty) {
  if(tx < 0 || ty < 0 || tx >= _layer.tilesX() || ty >= _layer.tilesY()) return;
  int64_t key = (int64_t(ty) << 32) | uint32_t(tx);
  if(_before.contains(key)) return;
  Tile* t = _layer.tile(tx, ty);
  _before.emplace(key, t ? t->clone() : nullptr);
}

void TileRecorder::touchPixel(int x, int y) {
  if(x < 0 || y < 0) return;
  touch(x / Tile::SIZE, y / Tile::SIZE);
}

void TileRecorder::touchRect(int x0, int y0, int x1, int y1) {
  x0 = std::max(x0, 0); y0 = std::max(y0, 0);
  x1 = std::min(x1, _layer.width() - 1); y1 = std::min(y1, _layer.height() - 1);
  for(int ty = y0 / Tile::SIZE; ty <= y1 / Tile::SIZE; ty++)
    for(int tx = x0 / Tile::SIZE; tx <= x1 / Tile::SIZE; tx++)
      touch(tx, ty);
}

uint32_t TileRecorder::beforePixel(int x, int y) const {
  if(!_layer.contains(x, y)) return 0;
  int tx = x / Tile::SIZE, ty = y / Tile::SIZE;
  auto it = _before.find((int64_t(ty) << 32) | uint32_t(tx));
  if(it == _before.end()) return _layer.get(x, y);
  return it->second ? it->second->get(x % Tile::SIZE, y % Tile::SIZE) : 0;
}

const Tile* TileRecorder::beforeTile(int tx, int ty) const {
  auto it = _before.find((int64_t(ty) << 32) | uint32_t(tx));
  return it == _before.end() ? _layer.tile(tx, ty) : it->second.get();
}

void TileRecorder::restore() {
  for(auto& [key, before] : _before)
    _layer.putTile(int(uint32_t(key)), int(key >> 32), before ? before->clone() : nullptr);
}

std::unique_ptr<TileEdit> TileRecorder::finish(std::string name, const char* icon) {
  auto edit = std::make_unique<TileEdit>();
  edit->name = std::move(name);
  edit->icon = icon;
  edit->layerId = _layer.id();
  edit->celFrame = _layer.frame();
  for(auto& [key, before] : _before) {
    int tx = int(uint32_t(key)), ty = int(key >> 32);
    Tile* now = _layer.tile(tx, ty);
    if(!before && !now) continue;
    if(before && now && before->equals(*now)) continue;
    edit->entries.push_back({ tx, ty, std::move(before), now ? now->clone() : nullptr });
  }
  _before.clear();
  if(edit->entries.empty()) return nullptr;
  return edit;
}

}
