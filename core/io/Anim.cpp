// Animated files (GIF, APNG, animated WebP) and sprite sheets
#include "core/io/Anim.h"
#include "core/Animation.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Palette.h"
#include "core/io/Formats.h"
#include "core/io/Png.h"
#include <webp/decode.h>
#include <webp/demux.h>
#include <webp/encode.h>
#include <webp/mux.h>
#include <zlib.h>
#define STBI_NO_STDIO
#include <stb_image.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace david {

#pragma region [ Frames ]
std::vector<AnimFrame> animFrames(const Document& doc, int tag, int scale) {
  scale = std::max(1, scale);
  const FrameTag* t = tag >= 0 && tag < int(doc.tags.size()) ? &doc.tags[tag] : nullptr;
  int w = doc.width(), h = doc.height();
  std::vector<AnimFrame> out;
  std::map<int, size_t> done;  // a frame played twice (ping-pong) is flattened once
  for(int f : playOrder(doc, t)) {
    if(auto it = done.find(f); it != done.end()) { out.push_back(out[it->second]); continue; }
    AnimFrame a;
    a.ms = doc.frameMs(f);
    auto px = flattenPixels(doc, f);
    if(scale == 1) a.px = std::move(px);
    else {
      a.px.resize(size_t(w) * scale * h * scale);
      for(int y = 0; y < h * scale; y++)
        for(int x = 0; x < w * scale; x++) a.px[size_t(y) * w * scale + x] = px[size_t(y / scale) * w + x / scale];
    }
    done[f] = out.size();
    out.push_back(std::move(a));
  }
  return out;
}

std::unique_ptr<Document> documentFromFrames(int w, int h, const std::vector<AnimFrame>& frames) {
  if(frames.empty()) return nullptr;
  auto doc = documentFromPixels(w, h, frames[0].px.data());
  if(frames.size() == 1) return doc;
  // The most common frame length sets the frame rate; the rest keep their own
  std::map<int, int> count;
  for(auto& f : frames) count[std::max(10, f.ms)]++;
  int common = std::max_element(count.begin(), count.end(), [](auto& a, auto& b) { return a.second < b.second; })->first;
  doc->fps = std::clamp(1000.0 / common, 1.0, 60.0);
  int rateMs = doc->frameMs(0);
  doc->frames.clear();
  Layer& l = *doc->layers[0];
  l.fitFrames(int(frames.size()), 0);
  // fitFrames dropped the first image's cel into frame 0 already; fill the rest
  for(size_t i = 0; i < frames.size(); i++) {
    int ms = std::max(10, frames[i].ms);
    doc->frames.push_back({ ms == rateMs ? 0 : ms });
    if(i == 0) continue;
    l.setFrame(int(i));
    for(int y = 0; y < h; y++)
      for(int x = 0; x < w; x++)
        if(uint32_t c = frames[i].px[size_t(y) * w + x]) l.set(x, y, c);
  }
  doc->setActiveFrame(0);
  return doc;
}
#pragma endregion

#pragma region [ GIF ]
namespace gif {
namespace {
// GIF-flavored LZW: variable code size up to 12 bits, LSB first (from Daedalus shared/gif.js, F-222)
std::vector<uint8_t> lzw(const std::vector<uint8_t>& px, int min) {
  int clear = 1 << min, eoi = clear + 1, size = min + 1, next = eoi + 1, nbits = 0;
  uint32_t buf = 0;
  std::vector<uint8_t> out;
  auto emit = [&](int code) {
    buf |= uint32_t(code) << nbits;
    nbits += size;
    while(nbits >= 8) { out.push_back(uint8_t(buf & 255)); buf >>= 8; nbits -= 8; }
  };
  std::unordered_map<int, int> dict;
  emit(clear);
  int cur = px.empty() ? 0 : px[0];
  for(size_t i = 1; i < px.size(); i++) {
    int k = px[i], key = cur * 4096 + k;
    if(auto it = dict.find(key); it != dict.end()) { cur = it->second; continue; }
    emit(cur);
    if(next < 4096) {
      dict[key] = next++;
      if(next > (1 << size) && size < 12) size++;
    } else {
      emit(clear);
      dict.clear();
      size = min + 1;
      next = eoi + 1;
    }
    cur = k;
  }
  emit(cur);
  emit(eoi);
  if(nbits > 0) out.push_back(uint8_t(buf & 255));
  return out;
}
}

std::vector<uint8_t> encode(int w, int h, const std::vector<AnimFrame>& frames, int loops) {
  // Palette: the opaque colors, exact up to 255 of them; the slot after them is transparency
  std::unordered_map<uint32_t, int> index;
  std::vector<uint32_t> colors;
  for(auto& f : frames)
    for(uint32_t c : f.px)
      if((c >> 24) >= 128 && index.emplace(c & 0xFFFFFF, int(colors.size())).second) colors.push_back(c & 0xFFFFFF);
  Palette reduced;
  if(colors.size() > 255) {
    std::vector<uint32_t> all;
    for(auto& f : frames) for(uint32_t c : f.px) if((c >> 24) >= 128) all.push_back(c | 0xFF000000u);
    reduced = Palette::fromPixels(all, 255);
    colors.clear();
    for(uint32_t c : reduced.colors) colors.push_back(c & 0xFFFFFF);
    index.clear();
  }
  auto lookup = [&](uint32_t c) {
    auto it = index.find(c & 0xFFFFFF);
    if(it != index.end()) return it->second;
    int i = reduced.nearest(c | 0xFF000000u);
    index.emplace(c & 0xFFFFFF, i);
    return i;
  };
  int transparent = int(colors.size()), bits = 1;
  while((1 << bits) < transparent + 1) bits++;
  std::vector<uint8_t> out = { 'G', 'I', 'F', '8', '9', 'a' };
  auto u16 = [&](int v) { out.push_back(uint8_t(v & 255)); out.push_back(uint8_t((v >> 8) & 255)); };
  u16(w); u16(h);
  out.push_back(uint8_t(0x80 | (bits - 1) << 4 | (bits - 1)));
  out.push_back(0); out.push_back(0);
  for(int i = 0; i < (1 << bits); i++) {
    uint32_t c = i < int(colors.size()) ? colors[i] : 0;
    out.push_back(uint8_t(c)); out.push_back(uint8_t(c >> 8)); out.push_back(uint8_t(c >> 16));
  }
  if(frames.size() > 1) {
    const char* ns = "NETSCAPE2.0";
    out.push_back(0x21); out.push_back(0xFF); out.push_back(11);
    out.insert(out.end(), ns, ns + 11);
    out.push_back(3); out.push_back(1);
    u16(loops); out.push_back(0);
  }
  for(auto& f : frames) {
    // Dispose to background, so transparent pixels don't keep the frame before them
    out.push_back(0x21); out.push_back(0xF9); out.push_back(4); out.push_back(uint8_t(2 << 2 | 1));
    u16(std::max(1, int(std::lround(f.ms / 10.0)))); out.push_back(uint8_t(transparent)); out.push_back(0);
    out.push_back(0x2C); u16(0); u16(0); u16(w); u16(h); out.push_back(0);
    std::vector<uint8_t> px(size_t(w) * h);
    for(size_t i = 0; i < px.size(); i++) px[i] = uint8_t((f.px[i] >> 24) < 128 ? transparent : lookup(f.px[i]));
    int min = std::max(2, bits);
    out.push_back(uint8_t(min));
    auto data = lzw(px, min);
    for(size_t i = 0; i < data.size(); i += 255) {
      size_t n = std::min<size_t>(255, data.size() - i);
      out.push_back(uint8_t(n));
      out.insert(out.end(), data.begin() + i, data.begin() + i + n);
    }
    out.push_back(0);
  }
  out.push_back(0x3B);
  return out;
}

std::unique_ptr<Document> load(const std::vector<uint8_t>& bytes, std::string& error) {
  int* delays = nullptr;
  int w = 0, h = 0, n = 0, comp = 0;
  stbi_uc* px = stbi_load_gif_from_memory(bytes.data(), int(bytes.size()), &delays, &w, &h, &n, &comp, 4);
  if(!px) { error = stbi_failure_reason() ? stbi_failure_reason() : "can't decode GIF"; return nullptr; }
  std::vector<AnimFrame> frames(std::max(n, 1));
  for(int i = 0; i < n; i++) {
    auto* p = reinterpret_cast<const uint32_t*>(px) + size_t(i) * w * h;
    frames[i].px.assign(p, p + size_t(w) * h);
    // Browsers show delays under 2/100 s as 1/10 s
    int d = delays ? delays[i] : 100;
    frames[i].ms = d < 20 ? 100 : d;
  }
  stbi_image_free(px);
  stbi_image_free(delays);
  return documentFromFrames(w, h, frames);
}
}
#pragma endregion

#pragma region [ APNG ]
namespace apng {
namespace {
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
void put32(std::vector<uint8_t>& o, uint32_t v) { for(int s = 24; s >= 0; s -= 8) o.push_back(uint8_t(v >> s)); }
void put16(std::vector<uint8_t>& o, uint16_t v) { o.push_back(uint8_t(v >> 8)); o.push_back(uint8_t(v)); }

struct Chunk { std::string type; std::vector<uint8_t> data; };

std::vector<Chunk> chunks(const std::vector<uint8_t>& b) {
  std::vector<Chunk> out;
  for(size_t p = 8; p + 12 <= b.size();) {
    uint32_t len = be32(&b[p]);
    if(p + 12 + len > b.size()) break;
    out.push_back({ std::string(reinterpret_cast<const char*>(&b[p + 4]), 4), std::vector<uint8_t>(b.begin() + p + 8, b.begin() + p + 8 + len) });
    p += 12 + len;
  }
  return out;
}

void writeChunk(std::vector<uint8_t>& o, const char* type, const std::vector<uint8_t>& data) {
  put32(o, uint32_t(data.size()));
  size_t start = o.size();
  o.insert(o.end(), type, type + 4);
  o.insert(o.end(), data.begin(), data.end());
  uLong crc = crc32(0, o.data() + start, uInt(o.size() - start));
  put32(o, uint32_t(crc));
}

const uint8_t SIG[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
}

bool animated(const std::vector<uint8_t>& bytes) {
  if(bytes.size() < 8 || std::memcmp(bytes.data(), SIG, 8)) return false;
  for(auto& c : chunks(bytes)) {
    if(c.type == "acTL") return true;
    if(c.type == "IDAT") return false;
  }
  return false;
}

std::vector<uint8_t> encode(int w, int h, const std::vector<AnimFrame>& frames, int loops) {
  // One color type for every frame: RGB only when all of them are opaque
  bool opaque = true;
  for(auto& f : frames) for(uint32_t c : f.px) if((c >> 24) != 0xFF) { opaque = false; break; }
  std::vector<uint8_t> o(SIG, SIG + 8);
  uint32_t seq = 0;
  for(size_t i = 0; i < frames.size(); i++) {
    const auto& px = frames[i].px;
    auto one = png::encode(w, h, [&](int y, uint32_t* row) { std::memcpy(row, px.data() + size_t(y) * w, size_t(w) * 4); }, opaque ? 0 : 1);
    auto cs = chunks(one);
    if(i == 0) {
      for(auto& c : cs) if(c.type == "IHDR") writeChunk(o, "IHDR", c.data);
      std::vector<uint8_t> actl;
      put32(actl, uint32_t(frames.size())); put32(actl, uint32_t(loops));
      writeChunk(o, "acTL", actl);
    }
    std::vector<uint8_t> fctl;
    put32(fctl, seq++); put32(fctl, uint32_t(w)); put32(fctl, uint32_t(h)); put32(fctl, 0); put32(fctl, 0);
    put16(fctl, uint16_t(std::clamp(frames[i].ms, 1, 65535))); put16(fctl, 1000);
    fctl.push_back(0); fctl.push_back(0);  // dispose none, blend source
    writeChunk(o, "fcTL", fctl);
    for(auto& c : cs) {
      if(c.type != "IDAT") continue;
      if(i == 0) { writeChunk(o, "IDAT", c.data); continue; }
      std::vector<uint8_t> fdat;
      put32(fdat, seq++);
      fdat.insert(fdat.end(), c.data.begin(), c.data.end());
      writeChunk(o, "fdAT", fdat);
    }
  }
  writeChunk(o, "IEND", {});
  return o;
}

std::unique_ptr<Document> load(const std::vector<uint8_t>& bytes, std::string& error) {
  auto cs = chunks(bytes);
  std::vector<uint8_t> ihdr;
  std::vector<Chunk> extra;  // PLTE, tRNS and friends go into every frame's PNG
  for(auto& c : cs) {
    if(c.type == "IHDR") ihdr = c.data;
    else if(c.type == "PLTE" || c.type == "tRNS" || c.type == "gAMA" || c.type == "sRGB" || c.type == "iCCP") extra.push_back(c);
  }
  if(ihdr.size() < 13) { error = "bad PNG header"; return nullptr; }
  int w = int(be32(&ihdr[0])), h = int(be32(&ihdr[4]));
  if(w <= 0 || h <= 0 || w > 65535 || h > 65535) { error = "bad image size"; return nullptr; }

  struct Region { int x = 0, y = 0, w = 0, h = 0, ms = 100, dispose = 0, blend = 0; std::vector<uint8_t> data; bool idat = false; };
  std::vector<Region> regions;
  Region* cur = nullptr;
  for(auto& c : cs) {
    if(c.type == "fcTL" && c.data.size() >= 26) {
      Region r;
      r.w = int(be32(&c.data[4])); r.h = int(be32(&c.data[8]));
      r.x = int(be32(&c.data[12])); r.y = int(be32(&c.data[16]));
      int num = c.data[20] << 8 | c.data[21], den = c.data[22] << 8 | c.data[23];
      r.ms = int(std::lround(num * 1000.0 / (den ? den : 100)));
      r.dispose = c.data[24]; r.blend = c.data[25];
      regions.push_back(std::move(r));
      cur = &regions.back();
    } else if(c.type == "IDAT" && cur) {
      cur->idat = true;
      cur->data.insert(cur->data.end(), c.data.begin(), c.data.end());
    } else if(c.type == "fdAT" && cur && c.data.size() > 4) {
      cur->data.insert(cur->data.end(), c.data.begin() + 4, c.data.end());
    }
  }
  if(regions.empty()) { error = "no frames"; return nullptr; }

  std::vector<uint32_t> canvas(size_t(w) * h, 0), saved;
  std::vector<AnimFrame> frames;
  for(auto& r : regions) {
    if(r.w <= 0 || r.h <= 0 || r.x + r.w > w || r.y + r.h > h || r.data.empty()) continue;
    // The region as a standalone PNG: the header with its size, the shared chunks, its image data
    std::vector<uint8_t> one(SIG, SIG + 8), hd = ihdr;
    hd[0] = uint8_t(r.w >> 24); hd[1] = uint8_t(r.w >> 16); hd[2] = uint8_t(r.w >> 8); hd[3] = uint8_t(r.w);
    hd[4] = uint8_t(r.h >> 24); hd[5] = uint8_t(r.h >> 16); hd[6] = uint8_t(r.h >> 8); hd[7] = uint8_t(r.h);
    writeChunk(one, "IHDR", hd);
    for(auto& e : extra) writeChunk(one, e.type.c_str(), e.data);
    writeChunk(one, "IDAT", r.data);
    writeChunk(one, "IEND", {});
    std::string err;
    auto img = png::loadMemory(one, err);
    if(!img) { error = err; return nullptr; }
    if(r.dispose == 2) saved = canvas;
    const Layer& src = img->active();
    for(int y = 0; y < r.h; y++)
      for(int x = 0; x < r.w; x++) {
        uint32_t c = src.get(x, y);
        uint32_t& d = canvas[size_t(r.y + y) * w + r.x + x];
        d = r.blend == 1 ? blendOver(d, c) : c;
      }
    frames.push_back({ canvas, std::max(10, r.ms) });
    if(r.dispose == 1)
      for(int y = 0; y < r.h; y++) std::fill_n(&canvas[size_t(r.y + y) * w + r.x], r.w, 0u);
    else if(r.dispose == 2) canvas = saved;
  }
  if(frames.empty()) { error = "no frames"; return nullptr; }
  return documentFromFrames(w, h, frames);
}
}
#pragma endregion

#pragma region [ Animated WebP ]
namespace webpanim {
bool animated(const std::vector<uint8_t>& bytes) {
  WebPBitstreamFeatures f;
  return WebPGetFeatures(bytes.data(), bytes.size(), &f) == VP8_STATUS_OK && f.has_animation;
}

std::vector<uint8_t> encode(int w, int h, const std::vector<AnimFrame>& frames, int loops, int quality, std::string& error) {
  WebPAnimEncoderOptions eo;
  WebPAnimEncoderOptionsInit(&eo);
  eo.anim_params.loop_count = loops;
  eo.allow_mixed = 0;
  WebPAnimEncoder* enc = WebPAnimEncoderNew(w, h, &eo);
  if(!enc) { error = "can't start the WebP encoder"; return {}; }
  WebPConfig cfg;
  WebPConfigInit(&cfg);
  cfg.lossless = quality >= 100;
  cfg.quality = float(std::clamp(quality, 0, 100));
  int t = 0;
  bool ok = true;
  for(auto& f : frames) {
    WebPPicture pic;
    WebPPictureInit(&pic);
    pic.width = w; pic.height = h; pic.use_argb = 1;
    ok = WebPPictureImportRGBA(&pic, reinterpret_cast<const uint8_t*>(f.px.data()), w * 4) && WebPAnimEncoderAdd(enc, &pic, t, &cfg);
    WebPPictureFree(&pic);
    if(!ok) break;
    t += f.ms;
  }
  WebPData data;
  WebPDataInit(&data);
  std::vector<uint8_t> out;
  if(ok && WebPAnimEncoderAdd(enc, nullptr, t, nullptr) && WebPAnimEncoderAssemble(enc, &data)) out.assign(data.bytes, data.bytes + data.size);
  else error = "can't encode WebP";
  WebPDataClear(&data);
  WebPAnimEncoderDelete(enc);
  return out;
}

std::unique_ptr<Document> load(const std::vector<uint8_t>& bytes, std::string& error) {
  WebPData data = { bytes.data(), bytes.size() };
  WebPAnimDecoderOptions o;
  WebPAnimDecoderOptionsInit(&o);
  o.color_mode = MODE_RGBA;
  WebPAnimDecoder* dec = WebPAnimDecoderNew(&data, &o);
  if(!dec) { error = "can't decode WebP"; return nullptr; }
  WebPAnimInfo info;
  WebPAnimDecoderGetInfo(dec, &info);
  int w = int(info.canvas_width), h = int(info.canvas_height), prev = 0;
  std::vector<AnimFrame> frames;
  while(WebPAnimDecoderHasMoreFrames(dec)) {
    uint8_t* buf;
    int ts;
    if(!WebPAnimDecoderGetNext(dec, &buf, &ts)) break;
    // The timestamp is when the frame ends
    auto* p = reinterpret_cast<const uint32_t*>(buf);
    frames.push_back({ std::vector<uint32_t>(p, p + size_t(w) * h), std::max(10, ts - prev) });
    prev = ts;
  }
  WebPAnimDecoderDelete(dec);
  if(frames.empty()) { error = "no frames"; return nullptr; }
  return documentFromFrames(w, h, frames);
}
}
#pragma endregion

#pragma region [ Sprite sheets ]
std::unique_ptr<Document> sheetFromDocument(const Document& doc, const SheetOptions& o, const std::string& image, std::string& json) {
  const FrameTag* tag = o.tag >= 0 && o.tag < int(doc.tags.size()) ? &doc.tags[o.tag] : nullptr;
  std::vector<int> order;
  if(tag) for(int f = tag->from; f <= tag->to; f++) order.push_back(f);
  else for(int f = 0; f < doc.frameCount(); f++) order.push_back(f);
  int n = int(order.size()), s = std::max(1, o.scale), pad = std::max(0, o.padding);
  int fw = doc.width() * s, fh = doc.height() * s;
  int cols = o.columns > 0 ? std::min(o.columns, n) : std::max(1, int(std::ceil(std::sqrt(double(n)))));
  int rows = (n + cols - 1) / cols;
  int sw = cols * fw + (cols + 1) * pad, sh = rows * fh + (rows + 1) * pad;
  auto sheet = Document::blank(sw, sh, 0);
  Layer& l = sheet->active();
  nlohmann::ordered_json frames = nlohmann::ordered_json::object();
  std::string base = image.substr(0, image.rfind('.'));
  for(int i = 0; i < n; i++) {
    int x0 = pad + (i % cols) * (fw + pad), y0 = pad + (i / cols) * (fh + pad);
    auto px = flattenPixels(doc, order[i]);
    for(int y = 0; y < fh; y++)
      for(int x = 0; x < fw; x++)
        if(uint32_t c = px[size_t(y / s) * doc.width() + x / s]) l.set(x0 + x, y0 + y, c);
    frames[base + " " + std::to_string(i) + ".png"] = {
      { "frame", { { "x", x0 }, { "y", y0 }, { "w", fw }, { "h", fh } } },
      { "rotated", false }, { "trimmed", false },
      { "spriteSourceSize", { { "x", 0 }, { "y", 0 }, { "w", fw }, { "h", fh } } },
      { "sourceSize", { { "w", fw }, { "h", fh } } },
      { "duration", doc.frameMs(order[i]) },
    };
  }
  nlohmann::ordered_json tags = nlohmann::ordered_json::array();
  const char* dirs[] = { "forward", "reverse", "pingpong" };
  for(auto& t : doc.tags) {
    if(tag && &t != tag) continue;
    int from = tag ? 0 : t.from, to = tag ? n - 1 : t.to;
    tags.push_back({ { "name", t.name }, { "from", from }, { "to", to }, { "direction", dirs[int(t.direction)] } });
  }
  nlohmann::ordered_json j = {
    { "frames", frames },
    { "meta", { { "app", "David" }, { "version", "1" }, { "image", image }, { "format", "RGBA8888" },
                { "size", { { "w", sw }, { "h", sh } } }, { "scale", std::to_string(s) }, { "frameTags", tags } } },
  };
  json = j.dump(2);
  return sheet;
}

std::unique_ptr<Document> documentFromSheet(const Document& sheet, const SliceOptions& o, std::string& error) {
  int fw = o.frameW, fh = o.frameH, pad = std::max(0, o.padding);
  if(fw <= 0 || fh <= 0) { error = "bad frame size"; return nullptr; }
  int cols = (sheet.width() - o.offsetX + pad) / (fw + pad), rows = (sheet.height() - o.offsetY + pad) / (fh + pad);
  if(cols <= 0 || rows <= 0) { error = "the frame is bigger than the image"; return nullptr; }
  auto px = flattenPixels(sheet);
  std::vector<AnimFrame> frames;
  for(int i = 0; i < cols * rows && (o.count <= 0 || int(frames.size()) < o.count); i++) {
    int x0 = o.offsetX + (i % cols) * (fw + pad), y0 = o.offsetY + (i / cols) * (fh + pad);
    AnimFrame f;
    f.px.resize(size_t(fw) * fh);
    for(int y = 0; y < fh; y++)
      for(int x = 0; x < fw; x++) f.px[size_t(y) * fw + x] = px[size_t(y0 + y) * sheet.width() + x0 + x];
    frames.push_back(std::move(f));
  }
  // Empty cells at the end of the last row aren't frames
  if(o.skipEmpty && o.count <= 0)
    while(frames.size() > 1 && std::all_of(frames.back().px.begin(), frames.back().px.end(), [](uint32_t c) { return (c >> 24) == 0; })) frames.pop_back();
  auto doc = documentFromFrames(fw, fh, frames);
  if(doc) doc->fps = 12;
  return doc;
}
#pragma endregion

}
