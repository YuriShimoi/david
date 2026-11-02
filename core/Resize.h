#pragma once
#include "core/History.h"
#include "core/Layer.h"
#include "core/Mask.h"
#include <memory>
#include <vector>

namespace david {

class Document;

enum class EResample : uint8_t { Best, Bicubic, Bilinear, Nearest, Supersample, COUNT };

// A resampled copy of the layer. Filters run on premultiplied alpha (and in linear light when gamma is on);
// downscaling widens the filter so it averages every source pixel instead of skipping them.
std::unique_ptr<Layer> resampleLayer(const Layer& src, int w, int h, EResample mode, bool gamma);
// The layer on a canvas of w × h with its old pixels at (dx, dy); new area filled with fill
std::unique_ptr<Layer> reframeLayer(const Layer& src, int w, int h, int dx, int dy, uint32_t fill);

// Swaps the whole image (size, layers, selection) as one undo step
class DocumentReplace : public Action {
public:
  struct State {
    int width = 0, height = 0;
    double dpi = 96;
    std::vector<std::unique_ptr<Layer>> layers;
    std::unique_ptr<Mask> selection;
  };
  State before, after;
  void undo(Document& doc) override { apply(doc, before); }
  void redo(Document& doc) override { apply(doc, after); }
  size_t bytes() const override;

private:
  static void apply(Document& doc, State& s);
};

void resizeImage(Document& doc, int w, int h, double dpi, EResample mode, bool gamma, const char* icon);
// anchorX/Y: 0 = left/top, 1 = middle, 2 = right/bottom. Only the bottom layer gets the fill.
void resizeCanvas(Document& doc, int w, int h, int anchorX, int anchorY, uint32_t fill, const char* icon);

}
