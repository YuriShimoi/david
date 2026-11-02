#pragma once
#include "core/History.h"
#include "core/Tile.h"
#include <unordered_map>

namespace david {

class Layer;

// Undo step holding before/after copies of only the tiles an edit touched. A null copy means "no tile".
class TileEdit : public Action {
public:
  struct Entry {
    int tx, ty;
    std::unique_ptr<Tile> before, after;
  };

  uint64_t layerId = 0;
  int celFrame = 0;  // the frame whose cel was edited
  std::vector<Entry> entries;

  void undo(Document& doc) override { apply(doc, false); }
  void redo(Document& doc) override { apply(doc, true); }
  size_t bytes() const override;

private:
  void apply(Document& doc, bool after);
};

// Records tiles as an edit touches them. Call touch() before writing to a tile, then finish() to get the step.
class TileRecorder {
public:
  explicit TileRecorder(Layer& layer) : _layer(layer) {}

  Layer& layer() { return _layer; }
  void touch(int tx, int ty);
  void touchPixel(int x, int y);
  void touchRect(int x0, int y0, int x1, int y1);
  bool empty() const { return _before.empty(); }
  // The pixel as it was before this edit touched its tile
  uint32_t beforePixel(int x, int y) const;
  // The tile as it was before this edit touched it (null = no tile); the current tile when not touched yet
  const Tile* beforeTile(int tx, int ty) const;
  // Puts every touched tile back to its "before" state (used by live previews such as Move)
  void restore();

  // Returns null when nothing actually changed
  std::unique_ptr<TileEdit> finish(std::string name, const char* icon);

private:
  Layer& _layer;
  std::unordered_map<int64_t, std::unique_ptr<Tile>> _before;
};

}
