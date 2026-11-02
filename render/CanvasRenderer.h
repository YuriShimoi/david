#pragma once
#include <glad/glad.h>
#include <cstdint>
#include <unordered_map>

namespace david {

class Document;
class Layer;

struct CanvasColors {
  float canvasBg[4], checkerA[4], checkerB[4], grid[4], customGrid[4];
};

// Ghosted neighbors on the canvas, for drawing only (never saved or exported)
struct OnionView {
  // Neighboring animation frames
  bool frames = false;
  int before = 1, after = 1;
  bool merged = false;          // the whole frame; off = only the active layer (Piskel)
  bool above = true;            // over the current frame (Piskel) or under it
  bool wrap = false;            // while looping, the first frame's neighbors include the last
  int rangeFrom = 0, rangeTo = -1;  // a tag's frames limit the neighbors; -1 = every frame
  // Neighboring layers around the active one
  bool layers = false;
  int below = 1, aboveCount = 1;
  bool hideOthers = true;       // layers outside the range: hidden, or dimmed
  // Shared look
  float opacity = 0.2f;         // nearest neighbor; farther ones fade
  bool tint = false;            // before: red, after: blue
};

// Where the canvas sits and how it's zoomed. All in framebuffer pixels.
struct CanvasView {
  int vpX = 0, vpY = 0, vpW = 1, vpH = 1;  // viewport rect, y down from the framebuffer top
  int fbHeight = 1;
  double originX = 0, originY = 0;         // image coordinate at the viewport's top-left
  double zoom = 1;                         // screen pixels per image pixel
  float checkerSize = 8;
  double gridZoom = 8;                     // pixel grid above this zoom, 0 = never
  int tiled = 0;                           // bit 1: wrapped copies left and right, bit 2: above and below
  bool customGrid = false;                 // the user's grid (cell size and offset in image pixels)
  int gridW = 16, gridH = 16, gridX = 0, gridY = 0;
  OnionView onion;
};

class CanvasRenderer {
public:
  bool init();
  void shutdown();
  void draw(const Document& doc, const CanvasView& view, const CanvasColors& colors);
  size_t textureBytes() const { return _textures.size() * size_t(256 * 256 * 4) * 4 / 3; }

private:
  struct TexEntry {
    GLuint tex = 0;
    uint64_t revision = 0;
    uint64_t seenFrame = 0;
  };
  struct Key {
    uint64_t layer;
    int frame, tx, ty;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const { return std::hash<uint64_t>()(k.layer * 0x9E3779B97F4A7C15ull ^ (uint64_t(k.frame) << 40 | uint64_t(k.ty) << 20 | uint32_t(k.tx))); }
  };

  GLuint _tileProgram = 0, _checkerProgram = 0, _gridProgram = 0;
  // One layer's frame; mul scales its opacity and tint (rgb, amount) recolors it (onion views)
  void drawLayer(const Layer& layer, int frame, double l, double t, double r, double b, float dx, float dy, float mul = 1, const float* tint = nullptr);
  // Blend-mode compositing: each layer is drawn alone into _layerFbo, then blended into the accumulator
  // pair with the mode's shader; the result is drawn over the checkerboard
  GLuint _blendProgram = 0, _showProgram = 0;
  GLuint _fbo[3] = {}, _fboTex[3] = {};
  int _fboW = 0, _fboH = 0;
  void ensureFbos(int w, int h);
  GLuint _vao = 0, _vbo = 0;
  GLuint _samplerNearest = 0, _samplerLinear = 0;
  uint64_t _frame = 0;
  std::unordered_map<Key, TexEntry, KeyHash> _textures;
};

}
