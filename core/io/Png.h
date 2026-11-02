#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <vector>

namespace david {

class Document;
class Layer;

// Fills row y with straight RGBA
using RowSource = std::function<void(int y, uint32_t* row)>;

std::filesystem::path pathFromUtf8(const std::string& utf8);
std::string pathToUtf8(const std::filesystem::path& p);
// Writes to path.tmp, then renames over path
bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error);

namespace png {
  // Any PNG (palette, gray, 16-bit, interlaced) becomes one RGBA8 layer
  std::unique_ptr<Document> load(const std::string& path, std::string& error);
  std::unique_ptr<Document> load(std::istream& in, std::string& error);
  std::unique_ptr<Document> loadMemory(const std::vector<uint8_t>& bytes, std::string& error);
  // Writes the flattened image; RGB when every pixel is opaque, else RGBA
  bool save(const Document& doc, const std::string& path, std::string& error);
  // alpha: -1 = RGB when every pixel is opaque, 0 = RGB, 1 = RGBA
  std::vector<uint8_t> encode(int w, int h, const RowSource& rowFn, int alpha = -1);
  // One frame of the layer (-1 = its current frame)
  std::vector<uint8_t> encodeLayer(const Layer& layer, int frame = -1);
  std::vector<uint8_t> encodeFlat(const Document& doc);
}

}
