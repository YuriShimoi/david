#include "core/Document.h"
#include <algorithm>
#include <cmath>

namespace david {

Document::Document(int width, int height)
  : selection(std::make_unique<Mask>(width, height)), _width(width), _height(height) {}

std::unique_ptr<Document> Document::blank(int width, int height, uint32_t fill) {
  auto doc = std::make_unique<Document>(width, height);
  auto layer = std::make_unique<Layer>(width, height, "Background");
  layer->fill(fill);
  doc->layers.push_back(std::move(layer));
  return doc;
}

Layer* Document::findLayer(uint64_t id) const {
  for(auto& l : layers) if(l->id() == id) return l.get();
  return nullptr;
}

int Document::indexOf(uint64_t id) const {
  for(int i = 0; i < int(layers.size()); i++) if(layers[i]->id() == id) return i;
  return -1;
}

void Document::replaceImage(int width, int height, std::vector<std::unique_ptr<Layer>> newLayers, std::unique_ptr<Mask> newSelection) {
  _width = width;
  _height = height;
  layers = std::move(newLayers);
  selection = std::move(newSelection);
  activeLayer = std::clamp(activeLayer, 0, int(layers.size()) - 1);
  setActiveFrame(activeFrame);
  onChanged();
}

void Document::setActiveFrame(int f) {
  activeFrame = std::clamp(f, 0, frameCount() - 1);
  for(auto& l : layers) l->setFrame(activeFrame);
}

int Document::frameMs(int f) const {
  int d = f >= 0 && f < frameCount() ? frames[f].duration : 0;
  return d > 0 ? d : std::max(1, int(std::lround(1000 / std::max(fps, 0.1))));
}

}
