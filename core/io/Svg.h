#pragma once
#include "core/Vector.h"
#include <memory>
#include <string>
#include <vector>

namespace david::svg {

// The objects as an SVG document of w × h. David's own attributes (data-david-*) keep what SVG can't say
// (shape kinds, node types, text font files, OKLab gradients, inside/outside strokes), so a .david file
// reads back exactly; other viewers see the same picture.
std::string write(const std::vector<VecObject>& objects, int w, int h);

// Parses SVG into objects in pixels (the viewBox mapped onto width × height). Paths, basic shapes, groups,
// transforms, solid and gradient paint, strokes, opacity and <use> are read; other elements are skipped and
// counted in skipped.
struct ReadResult {
  std::vector<VecObject> objects;
  double width = 0, height = 0;
  int skipped = 0;
};
bool read(const std::string& text, ReadResult& out, std::string& error);
// Pieces of the syntax, for scripts: path data ("M0 0 L10 0 Z") both ways, and a transform list
std::string pathString(const std::vector<Subpath>& paths);
std::vector<Subpath> parsePathString(const std::string& d);
Affine parseTransformString(const std::string& s);

// The whole image as SVG: vector layers as groups of objects, raster layers as embedded PNG images. Each layer
// is an Inkscape layer (a <g> with inkscape:groupmode="layer"), which other viewers read as a plain group.
std::string writeDocument(const Document& doc, int frame = -1);
// A new document of the SVG's size (rounded up) with its objects on one vector layer
std::unique_ptr<Document> loadDocument(const std::string& text, std::string& error);

}

namespace david {
// Exact pixel-art trace: each run of same-colored pixels (4-connected) becomes one filled polygon, holes included,
// so the objects render back to the very same pixels. Fully transparent pixels are skipped.
std::vector<VecObject> tracePixels(const Layer& layer, int frame);
void registerSvgFormat();
}
