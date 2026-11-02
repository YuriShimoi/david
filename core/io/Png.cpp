#include "core/io/Png.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Text.h"
#include <png.h>
#include <cstring>
#include <fstream>
#include <sstream>

namespace david {

std::filesystem::path pathFromUtf8(const std::string& utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string pathToUtf8(const std::filesystem::path& p) {
  auto s = p.u8string();
  return std::string(s.begin(), s.end());
}

namespace png {

#pragma region [ Load ]
static void readFn(png_structp ps, png_bytep data, png_size_t n) {
  auto* in = static_cast<std::istream*>(png_get_io_ptr(ps));
  if(!in->read(reinterpret_cast<char*>(data), n)) png_error(ps, N_("unexpected end of file"));
}

static void errorFn(png_structp ps, png_const_charp msg) {
  *static_cast<std::string*>(png_get_error_ptr(ps)) = msg;
  png_longjmp(ps, 1);
}

static void warnFn(png_structp, png_const_charp) {}

std::unique_ptr<Document> load(const std::string& path, std::string& error) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  if(!in) { error = N_("can't open file"); return nullptr; }
  auto doc = load(in, error);
  if(doc) doc->path = path;
  return doc;
}

std::unique_ptr<Document> loadMemory(const std::vector<uint8_t>& bytes, std::string& error) {
  std::istringstream in(std::string(bytes.begin(), bytes.end()), std::ios::binary);
  return load(in, error);
}

std::unique_ptr<Document> load(std::istream& in, std::string& error) {
  png_byte sig[8];
  if(!in.read(reinterpret_cast<char*>(sig), 8) || png_sig_cmp(sig, 0, 8)) { error = N_("not a PNG file"); return nullptr; }

  png_structp ps = png_create_read_struct(PNG_LIBPNG_VER_STRING, &error, errorFn, warnFn);
  png_infop info = png_create_info_struct(ps);
  // Everything that owns memory is declared before setjmp, so an error longjmp can't skip a destructor
  std::unique_ptr<Document> doc;
  std::unique_ptr<Layer> layer;
  std::vector<png_byte> image, row;
  std::vector<png_bytep> rows;

  if(setjmp(png_jmpbuf(ps))) {
    png_destroy_read_struct(&ps, &info, nullptr);
    return nullptr;
  }
  png_set_read_fn(ps, &in, readFn);
  png_set_sig_bytes(ps, 8);
  png_set_user_limits(ps, 65535, 65535);
  png_read_info(ps, info);

  png_uint_32 w, h;
  int depth, type, interlace;
  png_get_IHDR(ps, info, &w, &h, &depth, &type, &interlace, nullptr, nullptr);
  if(type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(ps);
  if(type == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(ps);
  if(png_get_valid(ps, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(ps);
  if(depth == 16) png_set_strip_16(ps);
  if(type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(ps);
  if(!(type & PNG_COLOR_MASK_ALPHA) && !png_get_valid(ps, info, PNG_INFO_tRNS)) png_set_filler(ps, 0xFF, PNG_FILLER_AFTER);
  int passes = png_set_interlace_handling(ps);
  png_read_update_info(ps, info);

  doc = std::make_unique<Document>(int(w), int(h));
  layer = std::make_unique<Layer>(int(w), int(h), "Background");
  auto copyRow = [&](int y, const png_byte* src) {
    int ty = y / Tile::SIZE, ry = y % Tile::SIZE;
    for(int tx = 0; tx < layer->tilesX(); tx++) {
      int x0 = tx * Tile::SIZE, n = std::min(Tile::SIZE, int(w) - x0);
      std::memcpy(layer->ensureTile(tx, ty).data() + ry * Tile::SIZE, src + size_t(x0) * 4, size_t(n) * 4);
    }
  };

  if(passes == 1) {
    // Row by row, so a huge image doesn't need a second full-size buffer
    row.resize(size_t(w) * 4);
    for(png_uint_32 y = 0; y < h; y++) {
      png_read_row(ps, row.data(), nullptr);
      copyRow(int(y), row.data());
    }
  } else {
    image.resize(size_t(w) * h * 4);
    rows.resize(h);
    for(png_uint_32 y = 0; y < h; y++) rows[y] = image.data() + size_t(y) * w * 4;
    png_read_image(ps, rows.data());
    for(png_uint_32 y = 0; y < h; y++) copyRow(int(y), rows[y]);
  }
  png_read_end(ps, nullptr);
  png_destroy_read_struct(&ps, &info, nullptr);

  // Tiles that came out fully transparent are dropped, keeping the layer sparse
  for(int ty = 0; ty < layer->tilesY(); ty++)
    for(int tx = 0; tx < layer->tilesX(); tx++)
      if(layer->tile(tx, ty)->isEmpty()) layer->takeTile(tx, ty);
  for(int ty = 0; ty < layer->tilesY(); ty++)
    for(int tx = 0; tx < layer->tilesX(); tx++)
      if(Tile* t = layer->tile(tx, ty)) t->touch();

  doc->layers.push_back(std::move(layer));
  return doc;
}
#pragma endregion

#pragma region [ Save ]
static void writeFn(png_structp ps, png_bytep data, png_size_t n) {
  auto* out = static_cast<std::ostream*>(png_get_io_ptr(ps));
  if(!out->write(reinterpret_cast<const char*>(data), n)) png_error(ps, N_("write failed"));
}

static void flushFn(png_structp) {}

// rowFn fills one row of straight RGBA; RGB is written when every pixel is opaque
static bool writePng(std::ostream& out, int w, int h, const RowSource& rowFn, std::string& error, int alpha = -1) {
  std::vector<uint32_t> row(w);
  bool opaque = alpha != 1;
  for(int y = 0; y < h && opaque && alpha < 0; y++) {
    rowFn(y, row.data());
    for(uint32_t v : row) if((v >> 24) != 0xFF) { opaque = false; break; }
  }
  png_structp ps = png_create_write_struct(PNG_LIBPNG_VER_STRING, &error, errorFn, warnFn);
  png_infop info = png_create_info_struct(ps);
  std::vector<png_byte> rgb;
  if(setjmp(png_jmpbuf(ps))) {
    png_destroy_write_struct(&ps, &info);
    return false;
  }
  png_set_write_fn(ps, &out, writeFn, flushFn);
  png_set_IHDR(ps, info, w, h, 8, opaque ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);
  png_write_info(ps, info);
  if(opaque) rgb.resize(size_t(w) * 3);
  for(int y = 0; y < h; y++) {
    rowFn(y, row.data());
    if(opaque) {
      for(int x = 0; x < w; x++) {
        uint32_t v = row[x];
        rgb[x * 3] = png_byte(v); rgb[x * 3 + 1] = png_byte(v >> 8); rgb[x * 3 + 2] = png_byte(v >> 16);
      }
      png_write_row(ps, rgb.data());
    } else {
      png_write_row(ps, reinterpret_cast<png_bytep>(row.data()));
    }
  }
  png_write_end(ps, nullptr);
  png_destroy_write_struct(&ps, &info);
  return true;
}

std::vector<uint8_t> encode(int w, int h, const RowSource& rowFn, int alpha) {
  std::ostringstream out(std::ios::binary);
  std::string error;
  if(!writePng(out, w, h, rowFn, error, alpha)) return {};
  std::string s = out.str();
  return { s.begin(), s.end() };
}

std::vector<uint8_t> encodeLayer(const Layer& layer, int frame) {
  if(frame < 0) frame = layer.frame();
  return encode(layer.width(), layer.height(), [&](int y, uint32_t* row) {
    int ty = y / Tile::SIZE, ry = y % Tile::SIZE;
    for(int tx = 0; tx < layer.tilesX(); tx++) {
      int x0 = tx * Tile::SIZE, n = std::min(Tile::SIZE, layer.width() - x0);
      if(const Tile* t = layer.tileAt(frame, tx, ty)) std::memcpy(row + x0, t->data() + ry * Tile::SIZE, size_t(n) * 4);
      else std::fill_n(row + x0, n, 0u);
    }
  });
}

std::vector<uint8_t> encodeFlat(const Document& doc) {
  return encode(doc.width(), doc.height(), [&](int y, uint32_t* row) { flattenRow(doc, y, row); });
}

bool save(const Document& doc, const std::string& path, std::string& error) {
  auto bytes = encodeFlat(doc);
  if(bytes.empty()) { error = N_("write failed"); return false; }
  return writeFileAtomic(path, bytes, error);
}
#pragma endregion

}

bool writeFileAtomic(const std::string& path, const std::vector<uint8_t>& bytes, std::string& error) {
  // A temp file first, so a failed save never destroys the old file
  auto target = pathFromUtf8(path);
  auto temp = target;
  temp += ".tmp";
  {
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if(!out) { error = N_("can't write file"); return false; }
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    out.close();
    if(!out) { error = N_("write failed"); std::error_code ec; std::filesystem::remove(temp, ec); return false; }
  }
  std::error_code ec;
  std::filesystem::rename(temp, target, ec);
  if(ec) { error = ec.message(); std::filesystem::remove(temp, ec); return false; }
  return true;
}

}
