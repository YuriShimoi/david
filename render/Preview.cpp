#include "render/Preview.h"
#include "core/Composite.h"
#include "core/Document.h"
#include <algorithm>
#include <vector>

namespace david {

void PreviewTexture::update(const Document& doc, int maxSize, uint64_t nowMs, int frame) {
  if(frame < 0) frame = doc.activeFrame;
  bool same = _doc == &doc && _frame == frame && _maxSize == maxSize;
  if(same && _revision == doc.revision) return;
  // Big images refresh at most ~6 times a second while they change
  bool big = size_t(doc.width()) * doc.height() > 1024 * 1024;
  if(same && big && nowMs - _builtAt < 160) return;
  _doc = &doc;
  _frame = frame;
  _maxSize = maxSize;
  _revision = doc.revision;
  _builtAt = nowMs;

  double k = std::min(1.0, double(maxSize) / std::max(doc.width(), doc.height()));
  _w = std::max(1, int(doc.width() * k)); _h = std::max(1, int(doc.height() * k));
  std::vector<uint32_t> row(doc.width()), px(size_t(_w) * _h);
  int lastY = -1;
  for(int y = 0; y < _h; y++) {
    int sy = std::min(doc.height() - 1, int(y / k));
    if(sy != lastY) { flattenRow(doc, sy, row.data(), frame); lastY = sy; }
    for(int x = 0; x < _w; x++) px[size_t(y) * _w + x] = row[std::min(doc.width() - 1, int(x / k))];
  }
  if(!_tex) {
    glGenTextures(1, &_tex);
    glBindTexture(GL_TEXTURE_2D, _tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  }
  glBindTexture(GL_TEXTURE_2D, _tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, _w, _h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
  glBindTexture(GL_TEXTURE_2D, 0);
}

void PreviewTexture::clear() {
  if(_tex) glDeleteTextures(1, &_tex);
  _tex = 0;
  _doc = nullptr;
  _revision = ~0ull;
  _frame = -1;
}

}
