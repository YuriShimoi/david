#include "render/CanvasRenderer.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/Vector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace david {

#pragma region [ Shaders ]
// Unit quad scaled to a rect in image space, then mapped to clip space by uProj
static const char* QUAD_VS = R"(#version 330 core
layout(location = 0) in vec2 aUnit;
uniform mat4 uProj;
uniform vec4 uRect; // x, y, w, h in image pixels
out vec2 vUv;
void main() {
  vUv = aUnit;
  gl_Position = uProj * vec4(uRect.xy + aUnit * uRect.zw, 0.0, 1.0);
}
)";

static const char* TILE_FS = R"(#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
uniform float uOpacity;
uniform vec4 uTint; // rgb, amount (onion views)
out vec4 oColor;
void main() {
  vec4 c = texture(uTex, vUv);
  oColor = vec4(mix(c.rgb, uTint.rgb, uTint.a), c.a * uOpacity);
}
)";

// Screen-space checkerboard, the same size at any zoom
static const char* CHECKER_FS = R"(#version 330 core
uniform vec4 uA;
uniform vec4 uB;
uniform float uCell;
out vec4 oColor;
void main() {
  vec2 cell = floor(gl_FragCoord.xy / uCell);
  oColor = mod(cell.x + cell.y, 2.0) < 1.0 ? uA : uB;
}
)";

// One-pixel lines on the edges of cells of uCell image pixels (1 × 1 = the pixel grid)
static const char* GRID_FS = R"(#version 330 core
uniform vec4 uColor;
uniform vec2 uScreenOrigin; // framebuffer position (y up) of image (uOffset)
uniform float uZoom;
uniform vec2 uCell;
out vec4 oColor;
void main() {
  vec2 d = vec2(gl_FragCoord.x - uScreenOrigin.x, uScreenOrigin.y - gl_FragCoord.y);
  vec2 cell = uCell * uZoom;
  vec2 f = d - floor(d / cell) * cell;
  if(f.x >= 1.0 && f.y >= 1.0) discard;
  oColor = uColor;
}
)";

// Blends the layer texture (uSrc) over the accumulator (uDst) with a Paint.NET blend mode, straight alpha
static const char* BLEND_FS = R"(#version 330 core
uniform sampler2D uDst;
uniform sampler2D uSrc;
uniform vec2 uSize;
uniform int uMode;
out vec4 oColor;
float ch(float b, float s) {
  if(uMode == 1) return b * s;
  if(uMode == 2) return min(1.0, b + s);
  if(uMode == 3) return s <= 0.0 ? 0.0 : max(0.0, 1.0 - (1.0 - b) / s);
  if(uMode == 4) return s >= 1.0 ? 1.0 : min(1.0, b / (1.0 - s));
  if(uMode == 5) return s >= 1.0 ? 1.0 : min(1.0, b * b / (1.0 - s));
  if(uMode == 6) return b >= 1.0 ? 1.0 : min(1.0, s * s / (1.0 - b));
  if(uMode == 7) return b < 0.5 ? 2.0 * b * s : 1.0 - 2.0 * (1.0 - b) * (1.0 - s);
  if(uMode == 8) return abs(b - s);
  if(uMode == 9) return 1.0 - abs(1.0 - b - s);
  if(uMode == 10) return max(b, s);
  if(uMode == 11) return min(b, s);
  if(uMode == 12) return 1.0 - (1.0 - b) * (1.0 - s);
  if(uMode == 13) return float(int(b * 255.0 + 0.5) ^ int(s * 255.0 + 0.5)) / 255.0;
  return s;
}
void main() {
  vec2 uv = gl_FragCoord.xy / uSize;
  vec4 B = texture(uDst, uv), S = texture(uSrc, uv);
  float wS = S.a * (1.0 - B.a), wF = S.a * B.a, wB = (1.0 - S.a) * B.a, a = wS + wF + wB;
  vec3 f = vec3(ch(B.r, S.r), ch(B.g, S.g), ch(B.b, S.b));
  oColor = a > 0.0 ? vec4((S.rgb * wS + f * wF + B.rgb * wB) / a, a) : vec4(0.0);
}
)";

// Draws the accumulator onto the screen at the viewport
static const char* SHOW_FS = R"(#version 330 core
uniform sampler2D uTex;
uniform vec2 uOrigin;
uniform vec2 uSize;
uniform float uAlpha; // onion frames are shown faded and tinted
uniform vec4 uTint;
uniform int uPremul;  // the texture was drawn with normal blending, so its colors are premultiplied
out vec4 oColor;
void main() {
  vec4 c = texture(uTex, (gl_FragCoord.xy - uOrigin) / uSize);
  if(uPremul == 1 && c.a > 0.0) c.rgb /= c.a;
  oColor = vec4(mix(c.rgb, uTint.rgb, uTint.a), c.a * uAlpha);
}
)";

static GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if(!ok) {
    char log[1024];
    glGetShaderInfoLog(s, sizeof log, nullptr, log);
    std::fprintf(stderr, "shader: %s\n", log);
  }
  return s;
}

static GLuint link(const char* vs, const char* fs) {
  GLuint p = glCreateProgram(), v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
  glAttachShader(p, v);
  glAttachShader(p, f);
  glLinkProgram(p);
  glDeleteShader(v);
  glDeleteShader(f);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  return ok ? p : 0;
}
#pragma endregion

bool CanvasRenderer::init() {
  _tileProgram = link(QUAD_VS, TILE_FS);
  _checkerProgram = link(QUAD_VS, CHECKER_FS);
  _gridProgram = link(QUAD_VS, GRID_FS);
  _blendProgram = link(QUAD_VS, BLEND_FS);
  _showProgram = link(QUAD_VS, SHOW_FS);
  if(!_tileProgram || !_checkerProgram || !_gridProgram || !_blendProgram || !_showProgram) return false;

  const float unit[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
  glGenVertexArrays(1, &_vao);
  glGenBuffers(1, &_vbo);
  glBindVertexArray(_vao);
  glBindBuffer(GL_ARRAY_BUFFER, _vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof unit, unit, GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindVertexArray(0);

  glGenSamplers(1, &_samplerNearest);
  glSamplerParameteri(_samplerNearest, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glSamplerParameteri(_samplerNearest, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenSamplers(1, &_samplerLinear);
  glSamplerParameteri(_samplerLinear, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glSamplerParameteri(_samplerLinear, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  for(GLuint s : { _samplerNearest, _samplerLinear }) {
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
  return true;
}

void CanvasRenderer::shutdown() {
  for(auto& [k, e] : _textures) glDeleteTextures(1, &e.tex);
  _textures.clear();
  for(auto& [k, e] : _vectorViews) glDeleteTextures(1, &e.tex);
  _vectorViews.clear();
  glDeleteSamplers(1, &_samplerNearest);
  glDeleteSamplers(1, &_samplerLinear);
  glDeleteBuffers(1, &_vbo);
  glDeleteVertexArrays(1, &_vao);
  glDeleteProgram(_tileProgram);
  glDeleteProgram(_checkerProgram);
  glDeleteProgram(_gridProgram);
  glDeleteProgram(_blendProgram);
  glDeleteProgram(_showProgram);
  if(_fbo[0]) { glDeleteFramebuffers(3, _fbo); glDeleteTextures(3, _fboTex); }
}

void CanvasRenderer::ensureFbos(int w, int h) {
  if(_fbo[0] && _fboW == w && _fboH == h) return;
  if(!_fbo[0]) { glGenFramebuffers(3, _fbo); glGenTextures(3, _fboTex); }
  for(int i = 0; i < 3; i++) {
    glBindTexture(GL_TEXTURE_2D, _fboTex[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo[i]);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _fboTex[i], 0);
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  _fboW = w; _fboH = h;
}

bool CanvasRenderer::drawVectorView(const Layer& layer, int frame) {
  const VecTree* tree = vectorTreeOf(layer, frame);
  if(!tree || !_view) return false;
  const CanvasView& v = *_view;
  VectorView& e = _vectorViews[layer.id()];
  e.seenFrame = _frame;
  bool stale = !e.tex || e.revision != tree->revision || e.frame != frame || e.w != v.vpW || e.h != v.vpH
               || e.originX != v.originX || e.originY != v.originY || e.zoom != v.zoom;
  if(stale) {
    std::vector<uint32_t> px;
    renderVector(*tree, { v.zoom, 0, 0, v.zoom, -v.originX * v.zoom, -v.originY * v.zoom }, v.vpW, v.vpH, px);
    if(!e.tex) glGenTextures(1, &e.tex);
    glBindTexture(GL_TEXTURE_2D, e.tex);
    if(e.w != v.vpW || e.h != v.vpH) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, v.vpW, v.vpH, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    else glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, v.vpW, v.vpH, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    e.revision = tree->revision; e.frame = frame; e.w = v.vpW; e.h = v.vpH;
    e.originX = v.originX; e.originY = v.originY; e.zoom = v.zoom;
  } else {
    glBindTexture(GL_TEXTURE_2D, e.tex);
  }
  // One texel per screen pixel: no filtering
  glBindSampler(0, _samplerNearest);
  glUniform4f(glGetUniformLocation(_tileProgram, "uRect"), float(v.originX), float(v.originY), float(v.vpW / v.zoom), float(v.vpH / v.zoom));
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindSampler(0, v.zoom < 1.0 ? _samplerLinear : _samplerNearest);
  return true;
}

void CanvasRenderer::drawLayer(const Layer& lay, int frame, double l, double t, double r, double b, float dx, float dy, float mul, const float* tint) {
  GLint opacityLoc = glGetUniformLocation(_tileProgram, "uOpacity");
  static const float NO_TINT[4] = {};
  glUniform4fv(glGetUniformLocation(_tileProgram, "uTint"), 1, tint ? tint : NO_TINT);
  // A vector layer's main copy comes straight from its objects, sharp at any zoom
  if(_view && _view->sharpVectors && lay.kind == ELayerKind::Vector && dx == 0 && dy == 0) {
    glUniform1f(opacityLoc, lay.opacity / 255.f * mul);
    if(drawVectorView(lay, frame)) return;
  }
  // Visible tile range, in the copy's own image coordinates
  int tx0 = std::max(0, int(std::floor((l - dx) / Tile::SIZE))), ty0 = std::max(0, int(std::floor((t - dy) / Tile::SIZE)));
  int tx1 = int(std::floor((r - dx) / Tile::SIZE)), ty1 = int(std::floor((b - dy) / Tile::SIZE));
  {
    const Layer* layer = &lay;
    glUniform1f(opacityLoc, layer->opacity / 255.f * mul);
    for(int ty = ty0; ty <= std::min(ty1, layer->tilesY() - 1); ty++) {
      for(int tx = tx0; tx <= std::min(tx1, layer->tilesX() - 1); tx++) {
        const Tile* tile = layer->tileAt(frame, tx, ty);
        if(!tile) continue;
        TexEntry& e = _textures[{ layer->id(), frame, tx, ty }];
        e.seenFrame = _frame;
        if(!e.tex) {
          glGenTextures(1, &e.tex);
          glBindTexture(GL_TEXTURE_2D, e.tex);
          glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, Tile::SIZE, Tile::SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, tile->data());
          e.revision = tile->revision();
          glGenerateMipmap(GL_TEXTURE_2D);
        } else {
          glBindTexture(GL_TEXTURE_2D, e.tex);
          if(e.revision != tile->revision()) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, Tile::SIZE, Tile::SIZE, GL_RGBA, GL_UNSIGNED_BYTE, tile->data());
            e.revision = tile->revision();
            glGenerateMipmap(GL_TEXTURE_2D);
          }
        }
        glUniform4f(glGetUniformLocation(_tileProgram, "uRect"), dx + float(tx * Tile::SIZE), dy + float(ty * Tile::SIZE), float(Tile::SIZE), float(Tile::SIZE));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
    }
  }
}

void CanvasRenderer::draw(const Document& doc, const CanvasView& v, const CanvasColors& colors) {
  _frame++;
  _view = &v;
  glViewport(v.vpX, v.fbHeight - v.vpY - v.vpH, v.vpW, v.vpH);
  glEnable(GL_SCISSOR_TEST);
  glScissor(v.vpX, v.fbHeight - v.vpY - v.vpH, v.vpW, v.vpH);
  glClearColor(colors.canvasBg[0], colors.canvasBg[1], colors.canvasBg[2], 1);
  glClear(GL_COLOR_BUFFER_BIT);

  // Orthographic projection from image pixels to the viewport, y down
  double l = v.originX, t = v.originY, r = l + v.vpW / v.zoom, b = t + v.vpH / v.zoom;
  float proj[16] = {
    float(2 / (r - l)), 0, 0, 0,
    0, float(2 / (t - b)), 0, 0,
    0, 0, -1, 0,
    float(-(r + l) / (r - l)), float(-(t + b) / (t - b)), 0, 1,
  };
  auto setRect = [](GLuint prog, float x, float y, float w, float h) { glUniform4f(glGetUniformLocation(prog, "uRect"), x, y, w, h); };
  float imgW = float(doc.width()), imgH = float(doc.height());

  glBindVertexArray(_vao);
  glDisable(GL_BLEND);
  glUseProgram(_checkerProgram);
  glUniformMatrix4fv(glGetUniformLocation(_checkerProgram, "uProj"), 1, GL_FALSE, proj);
  glUniform4fv(glGetUniformLocation(_checkerProgram, "uA"), 1, colors.checkerA);
  glUniform4fv(glGetUniformLocation(_checkerProgram, "uB"), 1, colors.checkerB);
  glUniform1f(glGetUniformLocation(_checkerProgram, "uCell"), v.checkerSize);
  // Tiled mode: the copies around the image, one image size each way
  int rx = (v.tiled & 1) ? 1 : 0, ry = (v.tiled & 2) ? 1 : 0;
  setRect(_checkerProgram, -rx * imgW, -ry * imgH, imgW * (1 + 2 * rx), imgH * (1 + 2 * ry));
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  glUseProgram(_tileProgram);
  glUniformMatrix4fv(glGetUniformLocation(_tileProgram, "uProj"), 1, GL_FALSE, proj);
  glUniform1i(glGetUniformLocation(_tileProgram, "uTex"), 0);
  glActiveTexture(GL_TEXTURE0);
  bool zoomedOut = v.zoom < 1.0;
  glBindSampler(0, zoomedOut ? _samplerLinear : _samplerNearest);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

  // Mark every existing tile as alive, so textures of removed tiles can be freed below
  for(auto& layer : doc.layers)
    for(int ty = 0; ty < layer->tilesY(); ty++)
      for(int tx = 0; tx < layer->tilesX(); tx++)
        if(layer->tile(tx, ty))
          if(auto it = _textures.find({ layer->id(), doc.activeFrame, tx, ty }); it != _textures.end()) it->second.seenFrame = _frame;

  const OnionView& on = v.onion;
  static const float RED[4] = { 1.f, 0.25f, 0.25f, 0.6f }, BLUE[4] = { 0.25f, 0.45f, 1.f, 0.6f };
  // Fading for the k-th neighbor (1 = nearest) of n on one side
  auto fade = [&](int k, int n) { return on.opacity * float(n - k + 1) / float(std::max(n, 1)); };
  // The layer onion view: the active layer as is, its neighbors ghosted, the rest hidden or dimmed
  struct Look { bool show; float mul; const float* tint; };
  auto look = [&](int i) -> Look {
    const Layer& layer = *doc.layers[i];
    if(!layer.visible || !layer.opacity) return { false, 0, nullptr };
    if(!on.layers) return { true, 1, nullptr };
    int d = i - doc.activeLayer;
    if(d == 0) return { true, 1, nullptr };
    if(d < 0 && -d <= on.below) return { true, fade(-d, on.below), on.tint ? RED : nullptr };
    if(d > 0 && d <= on.aboveCount) return { true, fade(d, on.aboveCount), on.tint ? BLUE : nullptr };
    return { !on.hideOthers, on.opacity * 0.5f, nullptr };
  };

  bool modes = false;
  for(auto& layer : doc.layers) modes |= layer->visible && layer->opacity && layer->blend != EBlend::Normal;
  auto copies = [&](const Layer& layer, int frame, float mul, const float* tint) {
    for(int oy = -ry; oy <= ry; oy++)
      for(int ox = -rx; ox <= rx; ox++) drawLayer(layer, frame, l, t, r, b, ox * imgW, oy * imgH, mul, tint);
  };

  // Onion frames: the neighbors of the active frame, faded (the nearest strongest) and optionally tinted.
  // A whole frame is composited first, so its own layers don't show through each other.
  auto onionFrames = [&] {
    if(!on.frames || doc.frameCount() < 2) return;
    int n = doc.frameCount(), lo = 0, hi = n - 1;
    if(on.rangeTo >= 0) { lo = std::clamp(on.rangeFrom, 0, n - 1); hi = std::clamp(on.rangeTo, lo, n - 1); }
    if(doc.activeFrame < lo || doc.activeFrame > hi) { lo = 0; hi = n - 1; }
    int span = hi - lo + 1;
    for(int side : { -1, 1 }) {
      int count = side < 0 ? on.before : on.after;
      for(int k = count; k >= 1; k--) {
        int f = doc.activeFrame + side * k;
        if(on.wrap) f = lo + ((f - lo) % span + span) % span;
        if(f < lo || f > hi || f == doc.activeFrame) continue;
        float alpha = fade(k, count);
        const float* tint = on.tint ? (side < 0 ? RED : BLUE) : nullptr;
        if(!on.merged) {
          const Layer& layer = *doc.layers[doc.activeLayer];
          if(layer.visible) copies(layer, f, alpha, tint);
          continue;
        }
        ensureFbos(v.vpW, v.vpH);
        glDisable(GL_SCISSOR_TEST);
        glViewport(0, 0, v.vpW, v.vpH);
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo[2]);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        for(auto& layer : doc.layers) if(layer->visible && layer->opacity) copies(*layer, f, 1, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(v.vpX, v.fbHeight - v.vpY - v.vpH, v.vpW, v.vpH);
        glEnable(GL_SCISSOR_TEST);
        float unitProj[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1 };
        static const float NO_TINT[4] = {};
        glUseProgram(_showProgram);
        glBindSampler(0, 0);
        glUniformMatrix4fv(glGetUniformLocation(_showProgram, "uProj"), 1, GL_FALSE, unitProj);
        glUniform4f(glGetUniformLocation(_showProgram, "uRect"), 0, 0, 1, 1);
        glUniform2f(glGetUniformLocation(_showProgram, "uOrigin"), float(v.vpX), float(v.fbHeight - v.vpY - v.vpH));
        glUniform2f(glGetUniformLocation(_showProgram, "uSize"), float(v.vpW), float(v.vpH));
        glUniform1i(glGetUniformLocation(_showProgram, "uTex"), 0);
        glUniform1f(glGetUniformLocation(_showProgram, "uAlpha"), alpha);
        glUniform1i(glGetUniformLocation(_showProgram, "uPremul"), 1);
        glUniform4fv(glGetUniformLocation(_showProgram, "uTint"), 1, tint ? tint : NO_TINT);
        glBindTexture(GL_TEXTURE_2D, _fboTex[2]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glUseProgram(_tileProgram);
        glBindSampler(0, zoomedOut ? _samplerLinear : _samplerNearest);
      }
    }
  };

  if(!on.above) onionFrames();
  if(!modes) {
    for(int i = 0; i < int(doc.layers.size()); i++) {
      Look k = look(i);
      if(k.show) copies(*doc.layers[i], doc.activeFrame, k.mul, k.tint);
    }
  } else {
    // Each layer alone into _fbo[2], then blended into the accumulator pair _fbo[0] / _fbo[1]
    ensureFbos(v.vpW, v.vpH);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, v.vpW, v.vpH);
    float unitProj[16] = { 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1 };
    int cur = 0;
    glBindFramebuffer(GL_FRAMEBUFFER, _fbo[0]);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    for(int i = 0; i < int(doc.layers.size()); i++) {
      auto& layer = doc.layers[i];
      Look k = look(i);
      if(!k.show) continue;
      glBindFramebuffer(GL_FRAMEBUFFER, _fbo[2]);
      glClear(GL_COLOR_BUFFER_BIT);
      glDisable(GL_BLEND);
      glUseProgram(_tileProgram);
      glBindSampler(0, zoomedOut ? _samplerLinear : _samplerNearest);
      copies(*layer, doc.activeFrame, k.mul, k.tint);
      glBindSampler(0, 0);
      glBindFramebuffer(GL_FRAMEBUFFER, _fbo[1 - cur]);
      glUseProgram(_blendProgram);
      glUniformMatrix4fv(glGetUniformLocation(_blendProgram, "uProj"), 1, GL_FALSE, unitProj);
      glUniform4f(glGetUniformLocation(_blendProgram, "uRect"), 0, 0, 1, 1);
      glUniform2f(glGetUniformLocation(_blendProgram, "uSize"), float(v.vpW), float(v.vpH));
      glUniform1i(glGetUniformLocation(_blendProgram, "uMode"), int(layer->blend));
      glUniform1i(glGetUniformLocation(_blendProgram, "uDst"), 0);
      glUniform1i(glGetUniformLocation(_blendProgram, "uSrc"), 1);
      glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, _fboTex[cur]);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, _fboTex[2]);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      glActiveTexture(GL_TEXTURE0);
      cur = 1 - cur;
    }
    // The result over the checkerboard
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(v.vpX, v.fbHeight - v.vpY - v.vpH, v.vpW, v.vpH);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glUseProgram(_showProgram);
    glUniformMatrix4fv(glGetUniformLocation(_showProgram, "uProj"), 1, GL_FALSE, unitProj);
    glUniform4f(glGetUniformLocation(_showProgram, "uRect"), 0, 0, 1, 1);
    glUniform2f(glGetUniformLocation(_showProgram, "uOrigin"), float(v.vpX), float(v.fbHeight - v.vpY - v.vpH));
    glUniform2f(glGetUniformLocation(_showProgram, "uSize"), float(v.vpW), float(v.vpH));
    glUniform1i(glGetUniformLocation(_showProgram, "uTex"), 0);
    glUniform1f(glGetUniformLocation(_showProgram, "uAlpha"), 1);
    glUniform1i(glGetUniformLocation(_showProgram, "uPremul"), 0);
    glUniform4f(glGetUniformLocation(_showProgram, "uTint"), 0, 0, 0, 0);
    glBindTexture(GL_TEXTURE_2D, _fboTex[cur]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glUseProgram(_tileProgram);
    glBindSampler(0, zoomedOut ? _samplerLinear : _samplerNearest);
  }
  if(on.above) onionFrames();
  glBindSampler(0, 0);

  // The copies are dimmed with the canvas background, so the real image stands out
  if(rx || ry) {
    glUseProgram(_checkerProgram);
    float dim[4] = { colors.canvasBg[0], colors.canvasBg[1], colors.canvasBg[2], 0.45f };
    glUniform4fv(glGetUniformLocation(_checkerProgram, "uA"), 1, dim);
    glUniform4fv(glGetUniformLocation(_checkerProgram, "uB"), 1, dim);
    for(int oy = -ry; oy <= ry; oy++)
      for(int ox = -rx; ox <= rx; ox++) {
        if(!ox && !oy) continue;
        setRect(_checkerProgram, ox * imgW, oy * imgH, imgW, imgH);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
      }
  }

  // Pixel grid above the configured zoom (800% by default), only while the grid is on
  if(v.customGrid && v.gridZoom > 0 && v.zoom > v.gridZoom) {
    glUseProgram(_gridProgram);
    glUniformMatrix4fv(glGetUniformLocation(_gridProgram, "uProj"), 1, GL_FALSE, proj);
    glUniform4fv(glGetUniformLocation(_gridProgram, "uColor"), 1, colors.grid);
    float sx = float(v.vpX + (0 - l) * v.zoom), sy = float(v.fbHeight - (v.vpY + (0 - t) * v.zoom));
    glUniform2f(glGetUniformLocation(_gridProgram, "uScreenOrigin"), sx, sy);
    glUniform1f(glGetUniformLocation(_gridProgram, "uZoom"), float(v.zoom));
    glUniform2f(glGetUniformLocation(_gridProgram, "uCell"), 1, 1);
    setRect(_gridProgram, 0, 0, imgW, imgH);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  // The user's grid, while its cells are big enough on screen to read
  if(v.customGrid && std::min(v.gridW, v.gridH) * v.zoom >= 4) {
    glUseProgram(_gridProgram);
    glUniformMatrix4fv(glGetUniformLocation(_gridProgram, "uProj"), 1, GL_FALSE, proj);
    glUniform4fv(glGetUniformLocation(_gridProgram, "uColor"), 1, colors.customGrid);
    float sx = float(v.vpX + (v.gridX - l) * v.zoom), sy = float(v.fbHeight - (v.vpY + (v.gridY - t) * v.zoom));
    glUniform2f(glGetUniformLocation(_gridProgram, "uScreenOrigin"), sx, sy);
    glUniform1f(glGetUniformLocation(_gridProgram, "uZoom"), float(v.zoom));
    glUniform2f(glGetUniformLocation(_gridProgram, "uCell"), float(v.gridW), float(v.gridH));
    setRect(_gridProgram, -rx * imgW, -ry * imgH, imgW * (1 + 2 * rx), imgH * (1 + 2 * ry));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }

  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_SCISSOR_TEST);

  // Free textures whose tile no longer exists (undone, deleted layer, closed document)
  for(auto it = _textures.begin(); it != _textures.end();) {
    if(it->second.seenFrame != _frame) { glDeleteTextures(1, &it->second.tex); it = _textures.erase(it); }
    else ++it;
  }
  for(auto it = _vectorViews.begin(); it != _vectorViews.end();) {
    if(it->second.seenFrame != _frame) { glDeleteTextures(1, &it->second.tex); it = _vectorViews.erase(it); }
    else ++it;
  }
  _view = nullptr;
}

}
