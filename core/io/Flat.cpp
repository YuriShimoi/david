// Single-layer formats besides PNG: WebP (libwebp), JPG / BMP / TGA / GIF (stb), QOI (own code)
#include "core/io/Formats.h"
#include "core/Document.h"
#include "core/io/Anim.h"
#include <webp/decode.h>
#include <webp/encode.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_GIF
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#include <algorithm>
#include <cstring>

namespace david {

namespace {
bool starts(const std::vector<uint8_t>& b, const char* sig, size_t at = 0) {
  size_t n = std::strlen(sig);
  return b.size() >= at + n && !std::memcmp(b.data() + at, sig, n);
}

#pragma region [ stb ]
std::unique_ptr<Document> stbLoad(const std::vector<uint8_t>& b, std::string& error) {
  int w, h, n;
  stbi_uc* px = stbi_load_from_memory(b.data(), int(b.size()), &w, &h, &n, 4);
  if(!px) { error = stbi_failure_reason() ? stbi_failure_reason() : "can't decode"; return nullptr; }
  auto doc = documentFromPixels(w, h, reinterpret_cast<const uint32_t*>(px));
  stbi_image_free(px);
  return doc;
}

void stbWrite(void* ctx, void* data, int size) {
  auto* out = static_cast<std::vector<uint8_t>*>(ctx);
  out->insert(out->end(), static_cast<uint8_t*>(data), static_cast<uint8_t*>(data) + size);
}

// JPG has no alpha: pixels are put over white first
std::vector<uint8_t> stbSave(const Document& d, const std::string& ext, int quality) {
  auto px = flattenPixels(d);
  std::vector<uint8_t> out;
  int w = d.width(), h = d.height();
  if(ext == "jpg") {
    std::vector<uint8_t> rgb(size_t(w) * h * 3);
    for(size_t i = 0; i < px.size(); i++) {
      uint32_t c = px[i], a = c >> 24;
      for(int k = 0; k < 3; k++) rgb[i * 3 + k] = uint8_t((((c >> (k * 8)) & 0xFF) * a + 255 * (255 - a) + 127) / 255);
    }
    stbi_write_jpg_to_func(stbWrite, &out, w, h, 3, rgb.data(), std::clamp(quality, 1, 100));
  } else if(ext == "bmp") {
    stbi_write_bmp_to_func(stbWrite, &out, w, h, 4, px.data());
  } else {
    stbi_write_tga_to_func(stbWrite, &out, w, h, 4, px.data());
  }
  return out;
}
#pragma endregion

#pragma region [ WebP ]
std::unique_ptr<Document> webpLoad(const std::vector<uint8_t>& b, std::string& error) {
  int w, h;
  uint8_t* px = WebPDecodeRGBA(b.data(), b.size(), &w, &h);
  if(!px) { error = "can't decode WebP"; return nullptr; }
  auto doc = documentFromPixels(w, h, reinterpret_cast<const uint32_t*>(px));
  WebPFree(px);
  return doc;
}

std::vector<uint8_t> webpSave(const Document& d, int quality, std::string& error) {
  auto px = flattenPixels(d);
  uint8_t* out = nullptr;
  auto rgba = reinterpret_cast<const uint8_t*>(px.data());
  size_t n = quality >= 100 ? WebPEncodeLosslessRGBA(rgba, d.width(), d.height(), d.width() * 4, &out)
                            : WebPEncodeRGBA(rgba, d.width(), d.height(), d.width() * 4, float(quality), &out);
  if(!n) { error = "can't encode WebP"; return {}; }
  std::vector<uint8_t> bytes(out, out + n);
  WebPFree(out);
  return bytes;
}
#pragma endregion

#pragma region [ QOI ]
// The "Quite OK Image" format (qoiformat.org), RGBA
uint32_t hashIndex(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return (r * 3 + g * 5 + b * 7 + a * 11) % 64; }

std::unique_ptr<Document> qoiLoad(const std::vector<uint8_t>& b, std::string& error) {
  if(b.size() < 22 || !starts(b, "qoif")) { error = "not a QOI file"; return nullptr; }
  auto be32 = [&](size_t i) { return uint32_t(b[i]) << 24 | uint32_t(b[i + 1]) << 16 | uint32_t(b[i + 2]) << 8 | b[i + 3]; };
  uint32_t w = be32(4), h = be32(8);
  if(!w || !h || w > 65535 || h > 65535) { error = "bad QOI size"; return nullptr; }
  std::vector<uint32_t> px(size_t(w) * h);
  uint8_t idx[64][4] = {};
  uint8_t r = 0, g = 0, bl = 0, a = 255;
  size_t p = 14, end = b.size() - 8;
  int run = 0;
  for(auto& out : px) {
    if(run > 0) run--;
    else if(p < end) {
      uint8_t t = b[p++];
      if(t == 0xFE) { r = b[p++]; g = b[p++]; bl = b[p++]; }
      else if(t == 0xFF) { r = b[p++]; g = b[p++]; bl = b[p++]; a = b[p++]; }
      else if((t >> 6) == 0) { r = idx[t][0]; g = idx[t][1]; bl = idx[t][2]; a = idx[t][3]; }
      else if((t >> 6) == 1) { r += ((t >> 4) & 3) - 2; g += ((t >> 2) & 3) - 2; bl += (t & 3) - 2; }
      else if((t >> 6) == 2) {
        int dg = (t & 63) - 32, x = b[p++];
        r += dg - 8 + ((x >> 4) & 15); g += dg; bl += dg - 8 + (x & 15);
      } else run = t & 63;
      uint8_t* e = idx[hashIndex(r, g, bl, a)];
      e[0] = r; e[1] = g; e[2] = bl; e[3] = a;
    }
    out = r | g << 8 | bl << 16 | uint32_t(a) << 24;
  }
  return documentFromPixels(int(w), int(h), px.data());
}

std::vector<uint8_t> qoiSave(const Document& d) {
  auto px = flattenPixels(d);
  std::vector<uint8_t> o = { 'q', 'o', 'i', 'f' };
  auto put32 = [&](uint32_t v) { for(int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s)); };
  put32(uint32_t(d.width())); put32(uint32_t(d.height()));
  o.push_back(4); o.push_back(0);
  uint8_t idx[64][4] = {};
  uint8_t pr = 0, pg = 0, pb = 0, pa = 255;
  int run = 0;
  for(size_t i = 0; i < px.size(); i++) {
    uint32_t c = px[i];
    uint8_t r = uint8_t(c), g = uint8_t(c >> 8), b = uint8_t(c >> 16), a = uint8_t(c >> 24);
    if(r == pr && g == pg && b == pb && a == pa) {
      if(++run == 62 || i + 1 == px.size()) { o.push_back(uint8_t(0xC0 | (run - 1))); run = 0; }
      continue;
    }
    if(run) { o.push_back(uint8_t(0xC0 | (run - 1))); run = 0; }
    uint32_t h = hashIndex(r, g, b, a);
    if(idx[h][0] == r && idx[h][1] == g && idx[h][2] == b && idx[h][3] == a) o.push_back(uint8_t(h));
    else {
      idx[h][0] = r; idx[h][1] = g; idx[h][2] = b; idx[h][3] = a;
      if(a == pa) {
        int dr = int8_t(r - pr), dg = int8_t(g - pg), db = int8_t(b - pb), drg = dr - dg, dbg = db - dg;
        if(dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) o.push_back(uint8_t(0x40 | (dr + 2) << 4 | (dg + 2) << 2 | (db + 2)));
        else if(dg >= -32 && dg <= 31 && drg >= -8 && drg <= 7 && dbg >= -8 && dbg <= 7) { o.push_back(uint8_t(0x80 | (dg + 32))); o.push_back(uint8_t((drg + 8) << 4 | (dbg + 8))); }
        else { o.push_back(0xFE); o.push_back(r); o.push_back(g); o.push_back(b); }
      } else { o.push_back(0xFF); o.push_back(r); o.push_back(g); o.push_back(b); o.push_back(a); }
    }
    pr = r; pg = g; pb = b; pa = a;
  }
  for(int i = 0; i < 7; i++) o.push_back(0);
  o.push_back(1);
  return o;
}
#pragma endregion
}

bool registerFlatFormats() {
  FileFormat f;
  f.ext = "webp"; f.name = "WebP";
  f.sniff = [](const std::vector<uint8_t>& b) { return starts(b, "RIFF") && starts(b, "WEBP", 8); };
  f.load = [](const std::vector<uint8_t>& b, std::string& err) { return webpanim::animated(b) ? webpanim::load(b, err) : webpLoad(b, err); };
  f.save = [](const Document& d, const SaveOptions& o, std::string& err) {
    if(d.frameCount() < 2) return webpSave(d, o.quality, err);
    int s = std::max(1, o.scale);
    return webpanim::encode(d.width() * s, d.height() * s, animFrames(d, o.tag, s), o.loops, o.quality, err);
  };
  registerFormat(f);

  f = {};
  f.ext = "qoi"; f.name = "QOI";
  f.sniff = [](const std::vector<uint8_t>& b) { return starts(b, "qoif"); };
  f.load = qoiLoad;
  f.save = [](const Document& d, const SaveOptions&, std::string&) { return qoiSave(d); };
  registerFormat(f);

  f = {};
  f.ext = "gif"; f.name = "GIF";
  f.sniff = [](const std::vector<uint8_t>& b) { return starts(b, "GIF8"); };
  f.load = gif::load;
  f.save = [](const Document& d, const SaveOptions& o, std::string&) {
    int s = std::max(1, o.scale);
    return gif::encode(d.width() * s, d.height() * s, animFrames(d, o.tag, s), o.loops);
  };
  registerFormat(f);

  struct Stb { const char* ext; const char* name; const char* sig; bool save; };
  for(Stb s : { Stb{ "jpg", "JPEG", "\xFF\xD8\xFF", true }, Stb{ "bmp", "BMP", "BM", true }, Stb{ "tga", "TGA", nullptr, true } }) {
    f = {};
    f.ext = s.ext; f.name = s.name;
    if(s.sig) { std::string sig = s.sig; f.sniff = [sig](const std::vector<uint8_t>& b) { return starts(b, sig.c_str()); }; }
    f.load = stbLoad;
    if(s.save) { std::string ext = s.ext; f.save = [ext](const Document& d, const SaveOptions& o, std::string&) { return stbSave(d, ext, o.quality); }; }
    registerFormat(f);
  }
  return true;
}

}
