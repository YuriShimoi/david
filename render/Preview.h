#pragma once
#include <glad/glad.h>
#include <cstdint>

namespace david {

class Document;

// The flattened image as one GL texture (nearest sampling, repeat wrap), at most maxSize on its long side.
// Rebuilt when the document changes, at most every few frames, so big images don't flatten per dab.
class PreviewTexture {
public:
  GLuint tex() const { return _tex; }
  int width() const { return _w; }
  int height() const { return _h; }
  // frame -1 = the active frame
  void update(const Document& doc, int maxSize, uint64_t nowMs, int frame = -1);
  void clear();

private:
  GLuint _tex = 0;
  int _w = 0, _h = 0;
  uint64_t _revision = ~0ull, _builtAt = 0;
  int _frame = -1, _maxSize = 0;
  const Document* _doc = nullptr;
};

}
