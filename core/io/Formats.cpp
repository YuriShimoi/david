#include "core/io/Formats.h"
#include "core/io/Svg.h"
#include "core/io/Anim.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/io/Png.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace david {

static std::vector<FileFormat>& registry() {
  static std::vector<FileFormat> list;
  return list;
}

void registerFormat(FileFormat f) {
  // Built-ins first, so plugin formats come after them in lists
  registerBuiltinFormats();
  auto& l = registry();
  for(auto& o : l) if(o.ext == f.ext) { o = std::move(f); return; }
  l.push_back(std::move(f));
}

void unregisterFormats(const std::string& owner) {
  auto& l = registry();
  l.erase(std::remove_if(l.begin(), l.end(), [&](const FileFormat& f) { return !owner.empty() && f.owner == owner; }), l.end());
}

const std::vector<FileFormat>& formats() {
  registerBuiltinFormats();
  return registry();
}

static std::string extOf(const std::string& pathOrExt) {
  std::string e = pathOrExt;
  size_t dot = e.find_last_of('.');
  size_t slash = e.find_last_of("/\\");
  if(dot != std::string::npos && (slash == std::string::npos || dot > slash)) e = e.substr(dot + 1);
  std::transform(e.begin(), e.end(), e.begin(), [](char c) { return char(std::tolower(c)); });
  return e;
}

const FileFormat* formatFor(const std::string& pathOrExt) {
  std::string e = extOf(pathOrExt);
  if(e == "jpeg") e = "jpg";
  for(auto& f : formats()) if(f.ext == e) return &f;
  return nullptr;
}

std::unique_ptr<Document> loadImageBytes(const std::vector<uint8_t>& bytes, const std::string& extHint, std::string& error) {
  const FileFormat* f = nullptr;
  for(auto& c : formats()) if(c.load && c.sniff && c.sniff(bytes)) { f = &c; break; }
  if(!f) f = formatFor(extHint);
  if(!f || !f->load) { error = "unknown image format"; return nullptr; }
  return f->load(bytes, error);
}

std::unique_ptr<Document> loadImage(const std::string& path, std::string& error) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  if(!in) { error = "can't open file"; return nullptr; }
  std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(in), {} };
  auto doc = loadImageBytes(bytes, path, error);
  if(doc) doc->path = path;
  return doc;
}

std::vector<uint8_t> saveImageBytes(const Document& doc, const std::string& ext, std::string& error, const SaveOptions& opts) {
  const FileFormat* f = formatFor(ext);
  if(!f || !f->save) { error = "can't save as ." + extOf(ext); return {}; }
  auto bytes = f->save(doc, opts, error);
  if(bytes.empty() && error.empty()) error = "write failed";
  return bytes;
}

bool saveImage(const Document& doc, const std::string& path, std::string& error, const SaveOptions& opts) {
  auto bytes = saveImageBytes(doc, path, error, opts);
  return !bytes.empty() && writeFileAtomic(path, bytes, error);
}

std::unique_ptr<Document> documentFromPixels(int w, int h, const uint32_t* px) {
  auto doc = std::make_unique<Document>(w, h);
  auto layer = std::make_unique<Layer>(w, h, "Background");
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++)
      if(uint32_t c = px[size_t(y) * w + x]) layer->set(x, y, c);
  doc->layers.push_back(std::move(layer));
  return doc;
}

std::vector<uint32_t> flattenPixels(const Document& doc, int frame) {
  std::vector<uint32_t> px(size_t(doc.width()) * doc.height());
  for(int y = 0; y < doc.height(); y++) flattenRow(doc, y, px.data() + size_t(y) * doc.width(), frame);
  return px;
}

bool registerFlatFormats();
bool registerLayeredFormats();

void registerBuiltinFormats() {
  static bool done = false;
  if(done) return;
  done = true;
  FileFormat png;
  png.ext = "png"; png.name = "PNG";
  png.sniff = [](const std::vector<uint8_t>& b) { return b.size() > 8 && !std::memcmp(b.data(), "\x89PNG", 4); };
  // An animation saves as APNG (other viewers show its first frame)
  png.load = [](const std::vector<uint8_t>& b, std::string& err) { return apng::animated(b) ? apng::load(b, err) : png::loadMemory(b, err); };
  png.save = [](const Document& d, const SaveOptions& o, std::string&) {
    if(d.frameCount() < 2 && o.scale <= 1) return png::encodeFlat(d);
    auto frames = animFrames(d, o.tag, o.scale);
    int s = std::max(1, o.scale);
    if(frames.size() == 1) return png::encode(d.width() * s, d.height() * s, [&](int y, uint32_t* row) { std::copy_n(frames[0].px.data() + size_t(y) * d.width() * s, d.width() * s, row); });
    return apng::encode(d.width() * s, d.height() * s, frames, o.loops);
  };
  registerFormat(png);
  registerLayeredFormats();
  registerFlatFormats();
  registerSvgFormat();
}

}
