#pragma once
#include <glad/glad.h>
#include <cstdint>
#include <unordered_map>

namespace david {

class Document;
class Layer;

// Small GL textures of each layer for the Layers window, rebuilt when the layer's tiles change
class Thumbnails {
public:
  struct Thumb { GLuint tex = 0; int w = 0, h = 0; };

  Thumb get(const Layer& layer, int maxSize);
  void prune(const Document& doc);
  void clear();

private:
  struct Entry {
    Thumb thumb;
    uint64_t signature = 0;
    uint64_t built = 0;
    int maxSize = 0;
  };
  std::unordered_map<uint64_t, Entry> _map;
};

}
