#pragma once
#include "core/Mask.h"
#include "core/TileEdit.h"
#include <functional>

namespace david {

class Document;

// Writes one row of the edit: n pixels from (x0, y), given their values before the edit
using RowPainter = std::function<void(int x0, int y, int n, const uint32_t* before, uint32_t* out)>;

// Rewrites the pixels of r on the recorder's layer from their pre-edit values, one task per tile in parallel.
// The selection clips the result.
void paintRegion(Document& doc, TileRecorder& rec, IRect r, const RowPainter& fn);

// Blends a color through each coverage mask in order (a fill, then its outline)
struct CoveragePass { const Mask* mask; uint32_t color; };
void paintCoverage(Document& doc, TileRecorder& rec, std::initializer_list<CoveragePass> passes);

// Straight-alpha mix from a to b by k (0-255), done premultiplied so transparent pixels don't darken edges
uint32_t mixPixel(uint32_t a, uint32_t b, int k);

// An edit that is redrawn from scratch on every change until it is committed (gradient, line, shapes, text)
class LiveEdit {
public:
  bool active() const { return bool(_rec); }
  // Puts the layer back to its pre-edit state and returns the recorder to draw the new version with
  TileRecorder& redraw(Document& doc);
  void commit(Document& doc, std::string name, const char* icon);
  // The finished edit, for a caller that pushes it with more (null when nothing changed)
  std::unique_ptr<TileEdit> take(std::string name, const char* icon);
  void cancel();

private:
  std::unique_ptr<TileRecorder> _rec;
  uint64_t _layerId = 0;
};

}
