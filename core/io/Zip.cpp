#include "core/io/Zip.h"
#include "core/io/Png.h"
#include <zlib.h>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace david::zip {

static uint32_t u16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint32_t u32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
static void put16(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v)); o.push_back(uint8_t(v >> 8)); }
static void put32(std::vector<uint8_t>& o, uint32_t v) { put16(o, v & 0xFFFF); put16(o, v >> 16); }

static bool inflateRaw(const uint8_t* src, size_t n, size_t outSize, std::vector<uint8_t>& out) {
  out.resize(outSize);
  z_stream z{};
  if(inflateInit2(&z, -MAX_WBITS) != Z_OK) return false;
  z.next_in = const_cast<Bytef*>(src);
  z.avail_in = uInt(n);
  z.next_out = out.data();
  z.avail_out = uInt(outSize);
  int r = inflate(&z, Z_FINISH);
  inflateEnd(&z);
  return r == Z_STREAM_END && z.total_out == outSize;
}

bool read(const std::vector<uint8_t>& b, std::vector<Entry>& out, std::string& error) {
  // End of central directory: the last "PK\5\6" within the final 64 KB + 22 bytes
  if(b.size() < 22) { error = "not a zip file"; return false; }
  size_t eocd = std::string::npos;
  for(size_t i = b.size() - 22; ; i--) {
    if(u32(&b[i]) == 0x06054b50) { eocd = i; break; }
    if(i == 0 || b.size() - i > 65557) break;
  }
  if(eocd == std::string::npos) { error = "not a zip file"; return false; }
  uint32_t count = u16(&b[eocd + 10]), cdOff = u32(&b[eocd + 16]);
  size_t p = cdOff;
  for(uint32_t i = 0; i < count; i++) {
    if(p + 46 > b.size() || u32(&b[p]) != 0x02014b50) { error = "bad central directory"; return false; }
    uint32_t method = u16(&b[p + 10]), csize = u32(&b[p + 20]), usize = u32(&b[p + 24]);
    uint32_t nlen = u16(&b[p + 28]), xlen = u16(&b[p + 30]), clen = u16(&b[p + 32]), local = u32(&b[p + 42]);
    if(p + 46 + nlen > b.size()) { error = "bad central directory"; return false; }
    std::string name(reinterpret_cast<const char*>(&b[p + 46]), nlen);
    p += 46 + nlen + xlen + clen;
    if(!name.empty() && name.back() == '/') continue;
    if(local + 30 > b.size() || u32(&b[local]) != 0x04034b50) { error = "bad local header for " + name; return false; }
    size_t data = local + 30 + u16(&b[local + 26]) + u16(&b[local + 28]);
    if(data + csize > b.size()) { error = "truncated entry " + name; return false; }
    Entry e{ name, {} };
    if(method == 0) e.data.assign(b.begin() + data, b.begin() + data + csize);
    else if(method == 8) { if(!inflateRaw(&b[data], csize, usize, e.data)) { error = "can't inflate " + name; return false; } }
    else { error = "unsupported compression in " + name; return false; }
    out.push_back(std::move(e));
  }
  return true;
}

bool readFile(const std::string& path, std::vector<Entry>& out, std::string& error) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  if(!in) { error = "can't open " + path; return false; }
  std::vector<uint8_t> bytes{ std::istreambuf_iterator<char>(in), {} };
  return read(bytes, out, error);
}

std::vector<uint8_t> write(const std::vector<Entry>& entries, const std::vector<std::string>& stored) {
  std::vector<uint8_t> o, cd;
  for(auto& e : entries) {
    uint32_t crc = uint32_t(crc32(0, e.data.data(), uInt(e.data.size())));
    bool store = std::find(stored.begin(), stored.end(), e.name) != stored.end() || e.data.empty();
    std::vector<uint8_t> body;
    if(!store) {
      body.resize(compressBound(uLong(e.data.size())) + 64);
      z_stream z{};
      deflateInit2(&z, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
      z.next_in = const_cast<Bytef*>(e.data.data());
      z.avail_in = uInt(e.data.size());
      z.next_out = body.data();
      z.avail_out = uInt(body.size());
      deflate(&z, Z_FINISH);
      body.resize(z.total_out);
      deflateEnd(&z);
      // Not worth it: store instead
      if(body.size() >= e.data.size()) store = true;
    }
    const std::vector<uint8_t>& data = store ? e.data : body;
    uint32_t offset = uint32_t(o.size());
    put32(o, 0x04034b50); put16(o, 20); put16(o, 0); put16(o, store ? 0 : 8); put16(o, 0); put16(o, 0x21);
    put32(o, crc); put32(o, uint32_t(data.size())); put32(o, uint32_t(e.data.size()));
    put16(o, uint32_t(e.name.size())); put16(o, 0);
    o.insert(o.end(), e.name.begin(), e.name.end());
    o.insert(o.end(), data.begin(), data.end());

    put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, store ? 0 : 8); put16(cd, 0); put16(cd, 0x21);
    put32(cd, crc); put32(cd, uint32_t(data.size())); put32(cd, uint32_t(e.data.size()));
    put16(cd, uint32_t(e.name.size())); put16(cd, 0); put16(cd, 0); put16(cd, 0); put16(cd, 0); put32(cd, 0); put32(cd, offset);
    cd.insert(cd.end(), e.name.begin(), e.name.end());
  }
  uint32_t cdOff = uint32_t(o.size());
  o.insert(o.end(), cd.begin(), cd.end());
  put32(o, 0x06054b50); put16(o, 0); put16(o, 0); put16(o, uint32_t(entries.size())); put16(o, uint32_t(entries.size()));
  put32(o, uint32_t(cd.size())); put32(o, cdOff); put16(o, 0);
  return o;
}

bool writeFile(const std::string& path, const std::vector<Entry>& entries, std::string& error, const std::vector<std::string>& stored) {
  auto bytes = write(entries, stored);
  std::ofstream out(pathFromUtf8(path), std::ios::binary);
  if(!out) { error = "can't write " + path; return false; }
  out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
  if(!out) { error = "can't write " + path; return false; }
  return true;
}

const Entry* find(const std::vector<Entry>& entries, const std::string& name) {
  for(auto& e : entries) if(e.name == name) return &e;
  return nullptr;
}

}
