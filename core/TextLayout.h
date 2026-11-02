#pragma once
#include "core/Mask.h"
#include <string>
#include <vector>

namespace david {

struct TextStyle {
  std::string path;  // font file
  int index = 0;     // face inside a collection
  float size = 32;   // pixels per em
  bool synthBold = false, synthItalic = false;
  int align = 0;     // left, center, right
  bool antialias = true;
};

struct TextLine {
  double y = 0, height = 0;    // top and height in image pixels
  size_t start = 0, end = 0;   // character range, end exclusive (the '\n' is not part of the line)
  std::vector<double> carets;  // x of the caret before each character, plus one past the end
};

struct TextBlock {
  bool ok = false;             // false when the font can't be loaded
  std::unique_ptr<Mask> mask;  // coverage over the whole image
  std::vector<TextLine> lines;
  double x = 0, y = 0, width = 0, height = 0;
};

// Shapes the text with HarfBuzz and rasterizes it with FreeType; (x, y) is the top-left of the first line.
// Lines split at '\n'. Synthetic bold and italic stand in for missing faces.
TextBlock layoutText(const std::u32string& text, const TextStyle& style, double x, double y, int imageW, int imageH);

}
