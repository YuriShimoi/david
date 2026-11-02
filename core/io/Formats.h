#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace david {

class Document;

struct SaveOptions {
  int quality = 90;  // JPG and lossy WebP; WebP at 100 is lossless
  // Animations (GIF, APNG, WebP): one tag's frames (-1 = all), an integer upscale, loops (0 = forever)
  int tag = -1;
  int scale = 1;
  int loops = 0;
};

// One file type, by extension. Plugins add their own (native mods).
struct FileFormat {
  std::string ext;    // lower case, no dot
  std::string name;   // "PNG", "OpenRaster"
  bool layered = false;
  std::function<std::unique_ptr<Document>(const std::vector<uint8_t>& bytes, std::string& error)> load;
  std::function<std::vector<uint8_t>(const Document& doc, const SaveOptions& opts, std::string& error)> save;
  std::function<bool(const std::vector<uint8_t>& bytes)> sniff;  // recognizes the bytes (stdin, wrong extension)
  std::string owner;  // the mod that registered it
};

void registerFormat(FileFormat f);
void unregisterFormats(const std::string& owner);
const std::vector<FileFormat>& formats();
// By extension (with or without the dot) or by a path's extension; null when unknown
const FileFormat* formatFor(const std::string& pathOrExt);

std::unique_ptr<Document> loadImage(const std::string& path, std::string& error);
// The format comes from the bytes (signature), or from extHint when they don't say
std::unique_ptr<Document> loadImageBytes(const std::vector<uint8_t>& bytes, const std::string& extHint, std::string& error);
bool saveImage(const Document& doc, const std::string& path, std::string& error, const SaveOptions& opts = {});
std::vector<uint8_t> saveImageBytes(const Document& doc, const std::string& ext, std::string& error, const SaveOptions& opts = {});

// One layer from straight RGBA pixels
std::unique_ptr<Document> documentFromPixels(int w, int h, const uint32_t* px);
std::vector<uint32_t> flattenPixels(const Document& doc, int frame = -1);

void registerBuiltinFormats();

}
