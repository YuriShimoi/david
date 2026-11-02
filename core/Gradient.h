#pragma once
#include "core/Raster.h"
#include <vector>

namespace david {

class Document;
class TileRecorder;

enum class EGradType : uint8_t { Linear, Reflected, Diamond, Radial, Conical, SpiralCW, SpiralCCW, COUNT };
enum class EGradSpace : uint8_t { Srgb, Linear, Oklab, OklchShort, OklchLong, COUNT };
enum class EDither : uint8_t { Off, Bayer, BlueNoise, COUNT };

struct GradStop {
  float pos;       // 0-1
  uint32_t color;  // straight RGBA8; in transparency mode only the alpha is used
};

struct GradientSpec {
  EGradType type = EGradType::Linear;
  EGradSpace space = EGradSpace::Oklab;
  EDither dither = EDither::Off;
  bool alphaMode = false;  // multiplies the layer's alpha instead of painting color
  bool pixelArt = false;   // only the stop colors, ordered-dithered between neighbors
  Point a{}, b{};
  std::vector<GradStop> stops;  // sorted by pos, at least two
  // Symmetry: pixels across an axis from the start point take the gradient of their mirror image
  bool mirrorX = false, mirrorY = false;
  double axisX = 0, axisY = 0;
};

// Position 0-1 of image point p along the gradient (repeating types wrap, the others clamp)
float gradientPos(const GradientSpec& g, double x, double y);
// 4096 baked colors as float RGBA 0-255, interpolated in the spec's space
std::vector<float> gradientTable(const GradientSpec& g);
// Paints the gradient over the active layer (inside the selection), recording the tiles in rec
void renderGradient(Document& doc, TileRecorder& rec, const GradientSpec& g);

// 64×64 tileable blue noise thresholds in [0, 1), made once by void-and-cluster
const std::vector<float>& blueNoise();
float bayer4(int x, int y);

}
