#pragma once
#include "core/Blend.h"
#include "core/Tile.h"
#include <memory>
#include <string>
#include <vector>

namespace david {

enum class ELayerKind : uint8_t { Raster, Vector };

// One frame's image of a layer: a sparse grid of tiles. Frames can share one cel (linked cels).
struct Cel {
  std::vector<std::unique_ptr<Tile>> tiles;
};

// A sparse grid of tiles per animation frame. Missing tiles (and empty cels) read as transparent and are
// allocated on first write. The tile functions act on the current frame's cel; the Document keeps every
// layer on its active frame, so code that edits "the image" edits the active frame.
class Layer {
public:
  std::string name;
  ELayerKind kind = ELayerKind::Raster;
  bool visible = true;
  uint8_t opacity = 255;
  EBlend blend = EBlend::Normal;

  Layer(int width, int height, std::string name);

  // sameId keeps the id, for undo copies that go back in place; otherwise the copy is a new layer.
  // Every frame is copied, and linked cels stay linked in the copy.
  std::unique_ptr<Layer> clone(bool sameId = false) const;

  uint64_t id() const { return _id; }
  int width() const { return _width; }
  int height() const { return _height; }
  int tilesX() const { return _tilesX; }
  int tilesY() const { return _tilesY; }
  bool contains(int x, int y) const { return x >= 0 && y >= 0 && x < _width && y < _height; }

  Tile* tile(int tx, int ty) const { return tileAt(_frame, tx, ty); }
  Tile& ensureTile(int tx, int ty);
  std::unique_ptr<Tile> takeTile(int tx, int ty);
  void putTile(int tx, int ty, std::unique_ptr<Tile> t) { putTileAt(_frame, tx, ty, std::move(t)); }

  uint32_t get(int x, int y) const { return getAt(_frame, x, y); }
  void set(int x, int y, uint32_t v);
  void fill(uint32_t v);
  size_t allocatedBytes() const;

  #pragma region [ Frames ]
  int frames() const { return int(_cels.size()); }
  int frame() const { return _frame; }
  void setFrame(int f) { _frame = f; }
  Tile* tileAt(int frame, int tx, int ty) const;
  uint32_t getAt(int frame, int x, int y) const;
  void putTileAt(int frame, int tx, int ty, std::unique_ptr<Tile> t);
  bool celEmpty(int frame) const;
  // The frame's cel, made (with no tiles) when the frame had none
  Cel& ensureCel(int frame);
  const std::shared_ptr<Cel>& cel(int frame) const { return _cels[frame]; }
  void setCel(int frame, std::shared_ptr<Cel> c) { _cels[frame] = std::move(c); }
  void insertCel(int frame, std::shared_ptr<Cel> c) { _cels.insert(_cels.begin() + frame, std::move(c)); }
  std::shared_ptr<Cel> removeCel(int frame);
  void moveCel(int from, int to);
  // Two frames that share one cel (an edit to one shows in both)
  bool linked(int a, int b) const { return a != b && _cels[a] && _cels[a] == _cels[b]; }
  // A deep copy of a frame's cel (null for an empty one)
  std::shared_ptr<Cel> copyCel(int frame) const;
  // A new layer joining a document of count frames: its single image goes to the active frame
  void fitFrames(int count, int active);
  #pragma endregion

private:
  uint64_t _id;
  int _width, _height, _tilesX, _tilesY;
  std::vector<std::shared_ptr<Cel>> _cels;
  int _frame = 0;
};

}
