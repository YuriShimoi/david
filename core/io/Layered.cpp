// Layered formats: .david (zip: doc.json + one PNG per layer) and OpenRaster .ora
#include "core/io/Formats.h"
#include "core/Blend.h"
#include "core/Document.h"
#include "core/io/Png.h"
#include "core/io/Zip.h"
#include "core/io/Svg.h"
#include "core/Vector.h"
#include "core/Layer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>

namespace david {

namespace {
// A PNG drawn into the layer's current frame at (x, y)
bool pasteFromPng(const std::vector<uint8_t>& bytes, Layer& layer, int x, int y, std::string& error) {
  auto img = png::loadMemory(bytes, error);
  if(!img) return false;
  const Layer& src = img->active();
  for(int sy = 0; sy < src.height(); sy++)
    for(int sx = 0; sx < src.width(); sx++)
      if(uint32_t c = src.get(sx, sy); c && layer.contains(sx + x, sy + y)) layer.set(sx + x, sy + y, c);
  return true;
}

// The layer from a PNG, placed at (x, y) on a w × h canvas
std::unique_ptr<Layer> layerFromPng(const std::vector<uint8_t>& bytes, int w, int h, int x, int y, std::string& error) {
  auto layer = std::make_unique<Layer>(w, h, "Layer");
  if(!pasteFromPng(bytes, *layer, x, y, error)) return nullptr;
  return layer;
}

std::string hex(uint32_t c) {
  char b[8];
  std::snprintf(b, sizeof b, "%02x%02x%02x", int(c & 0xFF), int((c >> 8) & 0xFF), int((c >> 16) & 0xFF));
  return b;
}

#pragma region [ .david ]
std::vector<uint8_t> davidSave(const Document& d, std::string& error) {
  using json = nlohmann::json;
  json j;
  j["app"] = "david";
  j["version"] = 1;
  j["width"] = d.width();
  j["height"] = d.height();
  j["dpi"] = d.dpi;
  j["active"] = d.activeLayer;
  json pal = json::array();
  for(uint32_t c : d.palette) pal.push_back(hex(c));
  j["palette"] = pal;
  // Animation (version 2): frame timing, tags, and per layer one cel per frame: a PNG, a link to an
  // earlier frame's cel, or null for an empty one. "file" stays the first frame, so version 1 readers still
  // open the image.
  bool anim = d.frameCount() > 1;
  if(anim) {
    j["version"] = 2;
    j["fps"] = d.fps;
    j["activeFrame"] = d.activeFrame;
    json frames = json::array();
    for(auto& f : d.frames) frames.push_back({ { "duration", f.duration } });
    j["frames"] = frames;
    json tags = json::array();
    const char* dirs[] = { "forward", "reverse", "pingpong" };
    for(auto& t : d.tags)
      tags.push_back({ { "name", t.name }, { "from", t.from }, { "to", t.to }, { "direction", dirs[int(t.direction)] }, { "color", hex(t.color) } });
    j["tags"] = tags;
  }
  std::vector<zip::Entry> entries;
  json layers = json::array();
  for(size_t i = 0; i < d.layers.size(); i++) {
    const Layer& l = *d.layers[i];
    std::string file = "layers/" + std::to_string(i) + ".png";
    json lj = { { "name", l.name }, { "visible", l.visible }, { "opacity", l.opacity }, { "blend", blendId(l.blend) }, { "file", file } };
    entries.push_back({ file, png::encodeLayer(l, 0) });
    if(entries.back().data.empty()) { error = "can't encode layer " + l.name; return {}; }
    // Vector layers (1.2): the objects as SVG next to their rendered PNG, which older readers load instead
    auto svgOf = [&](int f) -> std::string {
      if(l.kind != ELayerKind::Vector) return "";
      const VecTree* t = vectorTreeOf(l, f);
      if(!t) return "";
      std::string sf = "layers/" + std::to_string(i) + (f ? "/" + std::to_string(f) : std::string()) + ".svg";
      std::string text = svg::write(t->objects, d.width(), d.height());
      entries.push_back({ sf, std::vector<uint8_t>(text.begin(), text.end()) });
      return sf;
    };
    if(l.kind == ELayerKind::Vector) {
      lj["kind"] = "vector";
      if(!anim) if(std::string sf = svgOf(0); !sf.empty()) lj["svg"] = sf;
    }
    if(anim) {
      json cels = json::array();
      for(int f = 0; f < d.frameCount(); f++) {
        int g = 0;
        while(g < f && !l.linked(f, g)) g++;
        if(g < f) { cels.push_back({ { "link", g } }); continue; }
        if(l.celEmpty(f)) { cels.push_back(nullptr); continue; }
        std::string cf = f == 0 ? file : "layers/" + std::to_string(i) + "/" + std::to_string(f) + ".png";
        if(f > 0) entries.push_back({ cf, png::encodeLayer(l, f) });
        json cj = { { "file", cf } };
        if(std::string sf = svgOf(f); !sf.empty()) cj["svg"] = sf;
        cels.push_back(cj);
      }
      lj["cels"] = cels;
    }
    layers.push_back(lj);
  }
  j["layers"] = layers;
  std::string text = j.dump(2);
  entries.insert(entries.begin(), { "doc.json", std::vector<uint8_t>(text.begin(), text.end()) });
  entries.push_back({ "merged.png", png::encodeFlat(d) });
  return zip::write(entries, {});
}

std::unique_ptr<Document> davidLoad(const std::vector<uint8_t>& bytes, std::string& error) {
  std::vector<zip::Entry> entries;
  if(!zip::read(bytes, entries, error)) return nullptr;
  const zip::Entry* docJson = zip::find(entries, "doc.json");
  if(!docJson) { error = "no doc.json"; return nullptr; }
  auto j = nlohmann::json::parse(docJson->data.begin(), docJson->data.end(), nullptr, false);
  if(j.is_discarded() || j.value("app", "") != "david") { error = "bad doc.json"; return nullptr; }
  int w = j.value("width", 0), h = j.value("height", 0);
  if(w <= 0 || h <= 0 || w > 65535 || h > 65535) { error = "bad image size"; return nullptr; }
  auto doc = std::make_unique<Document>(w, h);
  doc->dpi = j.value("dpi", 96.0);
  auto frames = j.value("frames", nlohmann::json::array());
  if(frames.size() > 1) {
    doc->frames.clear();
    for(auto& f : frames) doc->frames.push_back({ std::clamp(f.value("duration", 0), 0, 60000) });
    doc->fps = std::clamp(j.value("fps", 12.0), 1.0, 60.0);
    for(auto& t : j.value("tags", nlohmann::json::array())) {
      FrameTag tag;
      tag.name = t.value("name", "Tag");
      tag.from = std::clamp(t.value("from", 0), 0, doc->frameCount() - 1);
      tag.to = std::clamp(t.value("to", 0), tag.from, doc->frameCount() - 1);
      std::string dir = t.value("direction", "forward");
      tag.direction = dir == "reverse" ? ETagDirection::Reverse : dir == "pingpong" ? ETagDirection::PingPong : ETagDirection::Forward;
      unsigned v;
      if(std::string c = t.value("color", ""); c.size() == 6 && std::sscanf(c.c_str(), "%6x", &v) == 1) tag.color = (v >> 16) | (v & 0xFF00) | (v & 0xFF) << 16 | 0xFF000000u;
      doc->tags.push_back(tag);
    }
  }
  int n = doc->frameCount();
  // A vector cel's objects; its pixels stay the saved PNG (fonts or renderers may differ on this machine)
  auto readSvg = [&](const std::string& file, Layer& layer, int frame) {
    if(file.empty()) return true;
    const zip::Entry* e = zip::find(entries, file);
    if(!e) { error = "missing " + file; return false; }
    svg::ReadResult r;
    if(!svg::read(std::string(e->data.begin(), e->data.end()), r, error)) return false;
    VecTree& t = vectorTree(layer, frame);
    t.objects = std::move(r.objects);
    t.touch();
    return true;
  };
  for(auto& l : j.value("layers", nlohmann::json::array())) {
    std::unique_ptr<Layer> layer;
    auto cels = l.value("cels", nlohmann::json::array());
    if(n > 1 && cels.is_array() && int(cels.size()) == n) {
      layer = std::make_unique<Layer>(w, h, "Layer");
      layer->fitFrames(n, 0);
      for(int f = 0; f < n; f++) {
        const auto& c = cels[f];
        if(c.is_null()) continue;
        if(c.contains("link")) {
          int g = c.value("link", 0);
          if(g >= 0 && g < f) layer->setCel(f, layer->cel(g));
          continue;
        }
        const zip::Entry* e = zip::find(entries, c.value("file", ""));
        if(!e) { error = "missing " + c.value("file", std::string("cel")); return nullptr; }
        layer->setFrame(f);
        if(!pasteFromPng(e->data, *layer, 0, 0, error)) return nullptr;
        if(!readSvg(c.value("svg", ""), *layer, f)) return nullptr;
      }
      layer->setFrame(0);
    } else {
      const zip::Entry* e = zip::find(entries, l.value("file", ""));
      if(!e) { error = "missing " + l.value("file", std::string("layer")); return nullptr; }
      layer = layerFromPng(e->data, w, h, 0, 0, error);
      if(!layer) return nullptr;
      if(!readSvg(l.value("svg", ""), *layer, 0)) return nullptr;
      layer->fitFrames(n, 0);
    }
    layer->name = l.value("name", "Layer");
    if(l.value("kind", "") == "vector") layer->kind = ELayerKind::Vector;
    layer->visible = l.value("visible", true);
    layer->opacity = uint8_t(std::clamp(l.value("opacity", 255), 0, 255));
    layer->blend = blendFromId(l.value("blend", "normal").c_str());
    doc->layers.push_back(std::move(layer));
  }
  if(doc->layers.empty()) { error = "no layers"; return nullptr; }
  doc->activeLayer = std::clamp(j.value("active", 0), 0, int(doc->layers.size()) - 1);
  doc->setActiveFrame(j.value("activeFrame", 0));
  for(auto& c : j.value("palette", std::vector<std::string>())) {
    unsigned v;
    if(c.size() == 6 && std::sscanf(c.c_str(), "%6x", &v) == 1) doc->palette.push_back((v >> 16) | (v & 0xFF00) | (v & 0xFF) << 16 | 0xFF000000u);
  }
  return doc;
}
#pragma endregion

#pragma region [ OpenRaster ]
// Blend modes as OpenRaster composite ops; the ones it lacks keep a david: name (other apps read them as normal)
const char* oraOp(EBlend m) {
  switch(m) {
    case EBlend::Multiply: return "svg:multiply";
    case EBlend::Additive: return "svg:plus";
    case EBlend::ColorBurn: return "svg:color-burn";
    case EBlend::ColorDodge: return "svg:color-dodge";
    case EBlend::Overlay: return "svg:overlay";
    case EBlend::Difference: return "svg:difference";
    case EBlend::Lighten: return "svg:lighten";
    case EBlend::Darken: return "svg:darken";
    case EBlend::Screen: return "svg:screen";
    case EBlend::Normal: return "svg:src-over";
    default: return nullptr;
  }
}

EBlend fromOraOp(const std::string& op) {
  for(int i = 0; i < int(EBlend::COUNT); i++) {
    const char* o = oraOp(EBlend(i));
    if(o && op == o) return EBlend(i);
  }
  if(op.rfind("david:", 0) == 0) return blendFromId(op.c_str() + 6);
  return EBlend::Normal;
}

std::string xmlEscape(const std::string& s) {
  std::string o;
  for(char c : s) {
    switch(c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      default: o += c;
    }
  }
  return o;
}

std::string xmlUnescape(const std::string& s) {
  std::string o;
  for(size_t i = 0; i < s.size(); i++) {
    if(s[i] != '&') { o += s[i]; continue; }
    size_t semi = s.find(';', i);
    if(semi == std::string::npos) { o += s[i]; continue; }
    std::string ent = s.substr(i + 1, semi - i - 1);
    if(ent == "amp") o += '&'; else if(ent == "lt") o += '<'; else if(ent == "gt") o += '>';
    else if(ent == "quot") o += '"'; else if(ent == "apos") o += '\'';
    else if(!ent.empty() && ent[0] == '#') o += char(std::strtol(ent.c_str() + (ent[1] == 'x' ? 2 : 1), nullptr, ent[1] == 'x' ? 16 : 10));
    else { o += s.substr(i, semi - i + 1); }
    i = semi;
  }
  return o;
}

// The attributes of every <tag ...> element, in document order (enough for stack.xml)
std::vector<std::map<std::string, std::string>> elements(const std::string& xml, const std::string& tag) {
  std::vector<std::map<std::string, std::string>> out;
  std::string open = "<" + tag;
  for(size_t p = xml.find(open); p != std::string::npos; p = xml.find(open, p + 1)) {
    char next = xml[p + open.size()];
    if(next != ' ' && next != '\t' && next != '\n' && next != '\r' && next != '/' && next != '>') continue;
    size_t end = xml.find('>', p);
    std::string body = xml.substr(p + open.size(), end - p - open.size());
    std::map<std::string, std::string> attrs;
    for(size_t i = 0; i < body.size();) {
      size_t eq = body.find('=', i);
      if(eq == std::string::npos) break;
      size_t k0 = body.find_first_not_of(" \t\r\n", i);
      std::string key = body.substr(k0, eq - k0);
      key.erase(key.find_last_not_of(" \t\r\n") + 1);
      size_t q = body.find_first_of("\"'", eq);
      if(q == std::string::npos) break;
      size_t q2 = body.find(body[q], q + 1);
      attrs[key] = xmlUnescape(body.substr(q + 1, q2 - q - 1));
      i = q2 + 1;
    }
    out.push_back(std::move(attrs));
  }
  return out;
}

std::vector<uint8_t> oraSave(const Document& d, std::string& error) {
  std::vector<zip::Entry> entries;
  std::string mime = "image/openraster";
  entries.push_back({ "mimetype", std::vector<uint8_t>(mime.begin(), mime.end()) });
  char head[160];
  std::snprintf(head, sizeof head, "<?xml version='1.0' encoding='UTF-8'?>\n<image version=\"0.0.3\" w=\"%d\" h=\"%d\" xres=\"%g\" yres=\"%g\">\n <stack>\n", d.width(), d.height(), d.dpi, d.dpi);
  std::string xml = head;
  // stack.xml lists the top layer first
  for(int i = int(d.layers.size()) - 1; i >= 0; i--) {
    const Layer& l = *d.layers[i];
    std::string file = "data/layer" + std::to_string(i) + ".png";
    const char* op = oraOp(l.blend);
    char attrs[64];
    std::snprintf(attrs, sizeof attrs, "%.4f", l.opacity / 255.0);
    xml += "  <layer name=\"" + xmlEscape(l.name) + "\" src=\"" + file + "\" x=\"0\" y=\"0\" opacity=\"" + attrs + "\" visibility=\"" +
           (l.visible ? "visible" : "hidden") + "\" composite-op=\"" + (op ? op : std::string("david:") + blendId(l.blend)) + "\"" +
           (i == d.activeLayer ? " selected=\"true\"" : "") + "/>\n";
    entries.push_back({ file, png::encodeLayer(l) });
    if(entries.back().data.empty()) { error = "can't encode layer " + l.name; return {}; }
  }
  xml += " </stack>\n</image>\n";
  entries.insert(entries.begin() + 1, { "stack.xml", std::vector<uint8_t>(xml.begin(), xml.end()) });
  entries.push_back({ "mergedimage.png", png::encodeFlat(d) });
  // The thumbnail fits in 256 × 256
  int tw = d.width(), th = d.height();
  double k = std::min(1.0, 256.0 / std::max(tw, th));
  tw = std::max(1, int(tw * k)); th = std::max(1, int(th * k));
  auto flat = flattenPixels(d);
  entries.push_back({ "Thumbnails/thumbnail.png", png::encode(tw, th, [&](int y, uint32_t* row) {
    int sy = std::min(d.height() - 1, int(y / k));
    for(int x = 0; x < tw; x++) row[x] = flat[size_t(sy) * d.width() + std::min(d.width() - 1, int(x / k))];
  }) });
  return zip::write(entries, { "mimetype" });
}

std::unique_ptr<Document> oraLoad(const std::vector<uint8_t>& bytes, std::string& error) {
  std::vector<zip::Entry> entries;
  if(!zip::read(bytes, entries, error)) return nullptr;
  const zip::Entry* stack = zip::find(entries, "stack.xml");
  if(!stack) { error = "no stack.xml"; return nullptr; }
  std::string xml(stack->data.begin(), stack->data.end());
  auto images = elements(xml, "image");
  if(images.empty()) { error = "bad stack.xml"; return nullptr; }
  int w = std::atoi(images[0]["w"].c_str()), h = std::atoi(images[0]["h"].c_str());
  if(w <= 0 || h <= 0 || w > 65535 || h > 65535) { error = "bad image size"; return nullptr; }
  auto doc = std::make_unique<Document>(w, h);
  if(!images[0]["xres"].empty()) doc->dpi = std::atof(images[0]["xres"].c_str());
  auto layers = elements(xml, "layer");
  // Bottom layer first in David
  int active = -1;
  for(int i = int(layers.size()) - 1; i >= 0; i--) {
    auto& a = layers[i];
    const zip::Entry* e = zip::find(entries, a["src"]);
    if(!e) { error = "missing " + a["src"]; return nullptr; }
    auto layer = layerFromPng(e->data, w, h, std::atoi(a["x"].c_str()), std::atoi(a["y"].c_str()), error);
    if(!layer) return nullptr;
    layer->name = a.count("name") ? a["name"] : "Layer";
    layer->visible = a["visibility"] != "hidden";
    layer->opacity = uint8_t(std::clamp(int(std::lround((a.count("opacity") ? std::atof(a["opacity"].c_str()) : 1.0) * 255)), 0, 255));
    layer->blend = fromOraOp(a["composite-op"]);
    if(a["selected"] == "true") active = int(doc->layers.size());
    doc->layers.push_back(std::move(layer));
  }
  if(doc->layers.empty()) { error = "no layers"; return nullptr; }
  doc->activeLayer = active >= 0 ? active : int(doc->layers.size()) - 1;
  return doc;
}
#pragma endregion
}

bool registerLayeredFormats() {
  auto isZipWith = [](const std::vector<uint8_t>& b, const char* name) {
    if(b.size() < 4 || std::memcmp(b.data(), "PK\x03\x04", 4)) return false;
    std::vector<zip::Entry> e;
    std::string err;
    return zip::read(b, e, err) && zip::find(e, name);
  };
  FileFormat f;
  f.ext = "david"; f.name = "David"; f.layered = true;
  f.sniff = [isZipWith](const std::vector<uint8_t>& b) { return isZipWith(b, "doc.json"); };
  f.load = davidLoad;
  f.save = [](const Document& d, const SaveOptions&, std::string& err) { return davidSave(d, err); };
  registerFormat(f);

  f = {};
  f.ext = "ora"; f.name = "OpenRaster"; f.layered = true;
  f.sniff = [isZipWith](const std::vector<uint8_t>& b) { return isZipWith(b, "stack.xml"); };
  f.load = oraLoad;
  f.save = [](const Document& d, const SaveOptions&, std::string& err) { return oraSave(d, err); };
  registerFormat(f);
  return true;
}

}
