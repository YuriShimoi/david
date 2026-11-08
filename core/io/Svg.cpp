#include "core/io/Svg.h"
#include "core/Document.h"
#include "core/Layer.h"
#include "core/io/Formats.h"
#include "core/io/Png.h"
#include <pugixml.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <array>
#include <climits>
#include <map>
#include <sstream>

namespace david::svg {

static constexpr double PI = 3.14159265358979323846;

#pragma region [ Writing ]
namespace {
std::string num(double v) {
  char buf[32];
  std::snprintf(buf, sizeof buf, "%.4f", v);
  // Trailing zeros off: 12.5000 -> 12.5, 3.0000 -> 3
  std::string s = buf;
  if(s.find('.') != std::string::npos) {
    while(s.back() == '0') s.pop_back();
    if(s.back() == '.') s.pop_back();
  }
  return s == "-0" ? "0" : s;
}

std::string hexColor(uint32_t c) {
  char buf[8];
  std::snprintf(buf, sizeof buf, "#%02x%02x%02x", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
  return buf;
}

std::string pathData(const std::vector<Subpath>& paths) {
  std::string d;
  for(auto& s : paths) {
    size_t n = s.nodes.size();
    if(!n) continue;
    d += "M" + num(s.nodes[0].p.x) + " " + num(s.nodes[0].p.y);
    size_t segs = s.closed ? n : n - 1;
    for(size_t i = 0; i < segs; i++) {
      const PathNode &a = s.nodes[i], &b = s.nodes[(i + 1) % n];
      bool straight = a.out.x == a.p.x && a.out.y == a.p.y && b.in.x == b.p.x && b.in.y == b.p.y;
      if(straight) d += "L" + num(b.p.x) + " " + num(b.p.y);
      else d += "C" + num(a.out.x) + " " + num(a.out.y) + " " + num(b.in.x) + " " + num(b.in.y) + " " + num(b.p.x) + " " + num(b.p.y);
    }
    if(s.closed) d += "Z";
  }
  return d;
}

struct Writer {
  pugi::xml_node defs;
  int gradients = 0;

  std::string paint(const VecPaint& p) {
    if(p.type == EPaintType::None) return "none";
    if(p.type == EPaintType::Solid) return hexColor(p.color);
    std::string id = "g" + std::to_string(++gradients);
    bool linear = p.type == EPaintType::Linear;
    pugi::xml_node g = defs.append_child(linear ? "linearGradient" : "radialGradient");
    g.append_attribute("id") = id.c_str();
    g.append_attribute("gradientUnits") = "userSpaceOnUse";
    if(linear) {
      g.append_attribute("x1") = num(p.a.x).c_str(); g.append_attribute("y1") = num(p.a.y).c_str();
      g.append_attribute("x2") = num(p.b.x).c_str(); g.append_attribute("y2") = num(p.b.y).c_str();
    } else {
      g.append_attribute("cx") = num(p.a.x).c_str(); g.append_attribute("cy") = num(p.a.y).c_str();
      g.append_attribute("r") = num(std::hypot(p.b.x - p.a.x, p.b.y - p.a.y)).c_str();
    }
    g.append_attribute("data-david-space") = int(p.space);
    // Viewers interpolate in sRGB: OKLab and OKLCH gradients get extra stops so they look the same
    std::vector<GradStop> stops = p.stops;
    std::stable_sort(stops.begin(), stops.end(), [](const GradStop& a, const GradStop& b) { return a.pos < b.pos; });
    auto stop = [&](double pos, uint32_t c, bool own) {
      pugi::xml_node s = g.append_child("stop");
      s.append_attribute("offset") = num(pos).c_str();
      s.append_attribute("stop-color") = hexColor(c).c_str();
      if((c >> 24) != 255) s.append_attribute("stop-opacity") = num((c >> 24) / 255.0).c_str();
      if(!own) s.append_attribute("data-david-baked") = 1;
    };
    if(p.space == EGradSpace::Srgb || stops.size() < 2) {
      for(auto& s : stops) stop(s.pos, s.color, true);
    } else {
      GradientSpec spec;
      spec.space = p.space;
      spec.stops = stops;
      std::vector<float> table = gradientTable(spec);
      int n = int(table.size() / 4);
      for(auto& s : stops) stop(s.pos, s.color, true);
      for(int i = 0; i <= 16; i++) {
        double pos = i / 16.0;
        bool taken = std::any_of(stops.begin(), stops.end(), [&](const GradStop& s) { return std::abs(s.pos - pos) < 1e-4; });
        if(taken) continue;
        const float* v = &table[size_t(std::min(n - 1, int(pos * (n - 1)))) * 4];
        uint32_t c = 0;
        for(int k = 0; k < 4; k++) c |= uint32_t(std::clamp(std::lround(v[k]), 0L, 255L)) << (k * 8);
        stop(pos, c, false);
      }
      // Offsets must rise in order
      std::vector<pugi::xml_node> all;
      for(pugi::xml_node s : g.children("stop")) all.push_back(s);
      std::stable_sort(all.begin(), all.end(), [](pugi::xml_node a, pugi::xml_node b) { return a.attribute("offset").as_double() < b.attribute("offset").as_double(); });
      for(auto& s : all) g.append_move(s);
    }
    return "url(#" + id + ")";
  }

  void style(pugi::xml_node e, const VecObject& o) {
    std::string fill = paint(o.fill);
    e.append_attribute("fill") = fill.c_str();
    if(o.fill.type == EPaintType::Solid && (o.fill.color >> 24) != 255) e.append_attribute("fill-opacity") = num((o.fill.color >> 24) / 255.0).c_str();
    const VecStroke& s = o.stroke;
    if(s.paint.visible() && s.width > 0) {
      std::string stroke = paint(s.paint);
      e.append_attribute("stroke") = stroke.c_str();
      if(s.paint.type == EPaintType::Solid && (s.paint.color >> 24) != 255) e.append_attribute("stroke-opacity") = num((s.paint.color >> 24) / 255.0).c_str();
      e.append_attribute("stroke-width") = num(s.width).c_str();
      const char* caps[] = { "butt", "round", "square" };
      const char* joins[] = { "miter", "round", "bevel" };
      if(s.cap != ECap::Butt) e.append_attribute("stroke-linecap") = caps[int(s.cap)];
      if(s.join != EJoin::Miter) e.append_attribute("stroke-linejoin") = joins[int(s.join)];
      if(s.miter != 4) e.append_attribute("stroke-miterlimit") = num(s.miter).c_str();
      if(!s.dash.empty()) {
        std::string d;
        for(double v : s.dash) d += (d.empty() ? "" : " ") + num(v * s.width);
        e.append_attribute("stroke-dasharray") = d.c_str();
      }
      if(s.align != EStrokeAlign::Center) e.append_attribute("data-david-stroke-align") = s.align == EStrokeAlign::Inside ? "inside" : "outside";
    }
  }

  void common(pugi::xml_node e, const VecObject& o) {
    if(!o.xf.identity()) {
      std::string m = "matrix(" + num(o.xf.a) + " " + num(o.xf.b) + " " + num(o.xf.c) + " " + num(o.xf.d) + " " + num(o.xf.e) + " " + num(o.xf.f) + ")";
      e.append_attribute("transform") = m.c_str();
    }
    if(o.opacity < 1) e.append_attribute("opacity") = num(o.opacity).c_str();
    if(!o.visible) e.append_attribute("display") = "none";
    if(o.locked) e.append_attribute("data-david-locked") = 1;
    if(!o.name.empty()) e.append_attribute("data-david-name") = o.name.c_str();
  }

  void object(pugi::xml_node parent, const VecObject& o) {
    pugi::xml_node e;
    switch(o.kind) {
      case EVecKind::Group:
        e = parent.append_child("g");
        common(e, o);
        for(auto& c : o.children) object(e, c);
        return;
      case EVecKind::Rect:
        e = parent.append_child("rect");
        e.append_attribute("x") = num(std::min(o.x, o.x + o.w)).c_str();
        e.append_attribute("y") = num(std::min(o.y, o.y + o.h)).c_str();
        e.append_attribute("width") = num(std::abs(o.w)).c_str();
        e.append_attribute("height") = num(std::abs(o.h)).c_str();
        if(o.radius > 0) e.append_attribute("rx") = num(o.radius).c_str();
        break;
      case EVecKind::Ellipse:
        e = parent.append_child("ellipse");
        e.append_attribute("cx") = num(o.x + o.w / 2).c_str();
        e.append_attribute("cy") = num(o.y + o.h / 2).c_str();
        e.append_attribute("rx") = num(std::abs(o.w) / 2).c_str();
        e.append_attribute("ry") = num(std::abs(o.h) / 2).c_str();
        break;
      case EVecKind::Polygon: {
        e = parent.append_child("polygon");
        std::string pts;
        auto out = outline(o);
        if(!out.empty()) for(auto& n : out[0].nodes) pts += (pts.empty() ? "" : " ") + num(n.p.x) + "," + num(n.p.y);
        e.append_attribute("points") = pts.c_str();
        std::string box = num(o.x) + " " + num(o.y) + " " + num(o.w) + " " + num(o.h);
        e.append_attribute("data-david-box") = box.c_str();
        e.append_attribute("data-david-sides") = o.sides;
        e.append_attribute("data-david-inner") = num(o.inner).c_str();
        break;
      }
      case EVecKind::Text: {
        // Glyph outlines for every viewer; the text itself in David's attributes so it stays editable
        e = parent.append_child("path");
        e.append_attribute("d") = pathData(outline(o)).c_str();
        e.append_attribute("data-david-text") = o.text.c_str();
        e.append_attribute("data-david-font") = o.font.c_str();
        e.append_attribute("data-david-font-index") = o.fontIndex;
        e.append_attribute("data-david-size") = num(o.size).c_str();
        e.append_attribute("data-david-at") = (num(o.x) + " " + num(o.y)).c_str();
        if(o.bold) e.append_attribute("data-david-bold") = 1;
        if(o.italic) e.append_attribute("data-david-italic") = 1;
        if(o.align) e.append_attribute("data-david-align") = o.align;
        break;
      }
      default: {
        e = parent.append_child("path");
        // Rounded corners: their arcs for every viewer, the nodes and radii in David's attributes
        bool round = hasRoundCorners(o.paths);
        e.append_attribute("d") = pathData(round ? roundCorners(o.paths) : o.paths).c_str();
        // Node types, one letter per node (c corner, s smooth, y symmetric), subpaths split by '|'
        std::string types, radii;
        bool any = false;
        for(auto& s : o.paths) {
          if(!types.empty()) { types += '|'; radii += '|'; }
          for(auto& n : s.nodes) {
            types += n.type == ENodeType::Smooth ? 's' : n.type == ENodeType::Symmetric ? 'y' : 'c';
            any |= n.type != ENodeType::Corner;
            radii += (radii.empty() || radii.back() == '|' ? "" : " ") + num(n.radius);
          }
        }
        if(any) e.append_attribute("data-david-nodes") = types.c_str();
        if(round) {
          e.append_attribute("data-david-d") = pathData(o.paths).c_str();
          e.append_attribute("data-david-radius") = radii.c_str();
        }
        break;
      }
    }
    common(e, o);
    style(e, o);
  }
};
}

static std::string base64(const std::vector<uint8_t>& in) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((in.size() + 2) / 3 * 4);
  for(size_t i = 0; i < in.size(); i += 3) {
    uint32_t v = uint32_t(in[i]) << 16 | (i + 1 < in.size() ? uint32_t(in[i + 1]) << 8 : 0) | (i + 2 < in.size() ? in[i + 2] : 0);
    out += T[(v >> 18) & 63];
    out += T[(v >> 12) & 63];
    out += i + 1 < in.size() ? T[(v >> 6) & 63] : '=';
    out += i + 2 < in.size() ? T[v & 63] : '=';
  }
  return out;
}

std::string writeDocument(const Document& d, int frame) {
  if(frame < 0) frame = d.activeFrame;
  pugi::xml_document doc;
  pugi::xml_node root = doc.append_child("svg");
  root.append_attribute("xmlns") = "http://www.w3.org/2000/svg";
  root.append_attribute("xmlns:inkscape") = "http://www.inkscape.org/namespaces/inkscape";
  root.append_attribute("width") = d.width();
  root.append_attribute("height") = d.height();
  root.append_attribute("viewBox") = ("0 0 " + std::to_string(d.width()) + " " + std::to_string(d.height())).c_str();
  Writer wr;
  wr.defs = root.append_child("defs");
  for(auto& lp : d.layers) {
    const Layer& l = *lp;
    pugi::xml_node g = root.append_child("g");
    g.append_attribute("inkscape:groupmode") = "layer";
    g.append_attribute("inkscape:label") = l.name.c_str();
    if(!l.visible) g.append_attribute("display") = "none";
    if(l.opacity < 255) g.append_attribute("opacity") = num(l.opacity / 255.0).c_str();
    const VecTree* t = l.kind == ELayerKind::Vector ? vectorTreeOf(l, frame) : nullptr;
    if(t) { for(auto& o : t->objects) wr.object(g, o); continue; }
    if(l.celEmpty(frame)) continue;
    pugi::xml_node img = g.append_child("image");
    img.append_attribute("width") = d.width();
    img.append_attribute("height") = d.height();
    std::string href = "data:image/png;base64," + base64(png::encodeLayer(l, frame));
    img.append_attribute("href") = href.c_str();
  }
  if(!wr.defs.first_child()) root.remove_child(wr.defs);
  std::ostringstream out;
  doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
  return out.str();
}

std::string write(const std::vector<VecObject>& objects, int w, int h) {
  pugi::xml_document doc;
  pugi::xml_node root = doc.append_child("svg");
  root.append_attribute("xmlns") = "http://www.w3.org/2000/svg";
  root.append_attribute("width") = w;
  root.append_attribute("height") = h;
  root.append_attribute("viewBox") = ("0 0 " + std::to_string(w) + " " + std::to_string(h)).c_str();
  Writer wr;
  wr.defs = root.append_child("defs");
  for(auto& o : objects) wr.object(root, o);
  if(!wr.defs.first_child()) root.remove_child(wr.defs);
  std::ostringstream out;
  doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
  return out.str();
}
#pragma endregion

#pragma region [ Reading ]
namespace {
// Numbers in path data and lists: commas and spaces separate, signs and dots can start a new number
struct Scanner {
  const char* p;
  explicit Scanner(const char* s) : p(s) {}
  void skip() { while(*p && (std::isspace((unsigned char)*p) || *p == ',')) p++; }
  bool number(double& v) {
    skip();
    if(!*p) return false;
    char* end = nullptr;
    v = std::strtod(p, &end);
    if(end == p) return false;
    p = end;
    return true;
  }
  // Arc flags may be written without separators ("1 0" or "10")
  bool flag(bool& v) {
    skip();
    if(*p != '0' && *p != '1') return false;
    v = *p == '1';
    p++;
    return true;
  }
};

std::vector<double> numbers(const std::string& s) {
  std::vector<double> out;
  Scanner sc(s.c_str());
  double v;
  while(sc.number(v)) out.push_back(v);
  return out;
}

Affine parseTransform(const std::string& s) {
  Affine m;
  size_t i = 0;
  while(i < s.size()) {
    size_t open = s.find('(', i);
    if(open == std::string::npos) break;
    size_t close = s.find(')', open);
    if(close == std::string::npos) break;
    std::string name = s.substr(i, open - i);
    name.erase(std::remove_if(name.begin(), name.end(), [](char c) { return std::isspace((unsigned char)c) || c == ','; }), name.end());
    auto v = numbers(s.substr(open + 1, close - open - 1));
    Affine t;
    if(name == "matrix" && v.size() >= 6) t = { v[0], v[1], v[2], v[3], v[4], v[5] };
    else if(name == "translate" && !v.empty()) t = Affine::translate(v[0], v.size() > 1 ? v[1] : 0);
    else if(name == "scale" && !v.empty()) t = Affine::scaling(v[0], v.size() > 1 ? v[1] : v[0]);
    else if(name == "rotate" && !v.empty()) {
      t = Affine::rotation(v[0] * PI / 180);
      if(v.size() >= 3) t = Affine::around({ v[1], v[2] }, t);
    } else if(name == "skewX" && !v.empty()) t = { 1, 0, std::tan(v[0] * PI / 180), 1, 0, 0 };
    else if(name == "skewY" && !v.empty()) t = { 1, std::tan(v[0] * PI / 180), 0, 1, 0, 0 };
    m = m * t;
    i = close + 1;
  }
  return m;
}

const std::map<std::string, uint32_t>& namedColors() {
  static const std::map<std::string, uint32_t> m = {
    { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF },
    { "yellow", 0xFFFF00 }, { "cyan", 0x00FFFF }, { "aqua", 0x00FFFF }, { "magenta", 0xFF00FF }, { "fuchsia", 0xFF00FF },
    { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xC0C0C0 }, { "maroon", 0x800000 }, { "olive", 0x808000 },
    { "lime", 0x00FF00 }, { "teal", 0x008080 }, { "navy", 0x000080 }, { "purple", 0x800080 }, { "orange", 0xFFA500 },
    { "pink", 0xFFC0CB }, { "brown", 0xA52A2A }, { "gold", 0xFFD700 }, { "darkgray", 0xA9A9A9 }, { "lightgray", 0xD3D3D3 },
  };
  return m;
}

// RGB as 0xRRGGBB; false for none, url() and unknown names
bool parseColor(std::string s, uint32_t& rgb) {
  s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return std::isspace((unsigned char)c); }), s.end());
  std::transform(s.begin(), s.end(), s.begin(), [](char c) { return char(std::tolower((unsigned char)c)); });
  if(s.empty() || s == "none") return false;
  if(s[0] == '#') {
    unsigned v = 0;
    if(s.size() == 4 && std::sscanf(s.c_str() + 1, "%3x", &v) == 1) { rgb = ((v >> 8) & 0xF) * 0x110000 + ((v >> 4) & 0xF) * 0x1100 + (v & 0xF) * 0x11; return true; }
    if(s.size() >= 7 && std::sscanf(s.c_str() + 1, "%6x", &v) == 1) { rgb = v; return true; }
    return false;
  }
  if(s.rfind("rgb", 0) == 0) {
    size_t open = s.find('(');
    if(open == std::string::npos) return false;
    std::string inner = s.substr(open + 1);
    bool pct = inner.find('%') != std::string::npos;
    std::replace(inner.begin(), inner.end(), '%', ' ');
    auto v = numbers(inner);
    if(v.size() < 3) return false;
    auto ch = [&](double x) { return uint32_t(std::clamp(std::lround(pct ? x * 2.55 : x), 0L, 255L)); };
    rgb = ch(v[0]) << 16 | ch(v[1]) << 8 | ch(v[2]);
    return true;
  }
  auto& names = namedColors();
  if(auto it = names.find(s); it != names.end()) { rgb = it->second; return true; }
  return false;
}

uint32_t packColor(uint32_t rgb, double alpha) {
  return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16 | uint32_t(std::clamp(std::lround(alpha * 255), 0L, 255L)) << 24;
}

// Inherited presentation attributes
struct Style {
  std::string fill = "#000", stroke = "none";
  double fillOpacity = 1, strokeOpacity = 1, strokeWidth = 1, miter = 4;
  std::string cap = "butt", join = "miter", dash, align;
  bool evenOdd = false;
};

struct Reader {
  pugi::xml_document& doc;
  ReadResult& out;
  std::map<std::string, pugi::xml_node> ids;

  // An attribute, or the same property inside style=""
  std::string prop(pugi::xml_node e, const char* name) {
    std::string st = e.attribute("style").as_string();
    size_t p = 0;
    while(!st.empty() && p < st.size()) {
      size_t semi = st.find(';', p);
      std::string decl = st.substr(p, semi == std::string::npos ? std::string::npos : semi - p);
      size_t colon = decl.find(':');
      if(colon != std::string::npos) {
        std::string k = decl.substr(0, colon), v = decl.substr(colon + 1);
        auto trim = [](std::string& s) { s.erase(0, s.find_first_not_of(" \t\n\r")); s.erase(s.find_last_not_of(" \t\n\r") + 1); };
        trim(k); trim(v);
        if(k == name) return v;
      }
      if(semi == std::string::npos) break;
      p = semi + 1;
    }
    pugi::xml_attribute a = e.attribute(name);
    return a ? a.as_string() : "";
  }

  Style inherit(pugi::xml_node e, Style s) {
    auto take = [&](const char* name, std::string& v) { if(std::string x = prop(e, name); !x.empty() && x != "inherit") v = x; };
    auto takeNum = [&](const char* name, double& v) { if(std::string x = prop(e, name); !x.empty() && x != "inherit") v = std::atof(x.c_str()); };
    take("fill", s.fill); take("stroke", s.stroke);
    takeNum("fill-opacity", s.fillOpacity); takeNum("stroke-opacity", s.strokeOpacity);
    takeNum("stroke-width", s.strokeWidth); takeNum("stroke-miterlimit", s.miter);
    take("stroke-linecap", s.cap); take("stroke-linejoin", s.join); take("stroke-dasharray", s.dash);
    take("data-david-stroke-align", s.align);
    if(std::string r = prop(e, "fill-rule"); !r.empty()) s.evenOdd = r == "evenodd";
    return s;
  }

  // A paint from fill/stroke: a color, or a gradient by url(#id) (in user space, or the object's box)
  VecPaint paint(const std::string& v, double opacity, const std::vector<Subpath>& shape) {
    VecPaint p;
    if(v.rfind("url(", 0) == 0) {
      size_t hash = v.find('#'), close = v.find(')');
      if(hash == std::string::npos || close == std::string::npos) return p;
      auto it = ids.find(v.substr(hash + 1, close - hash - 1));
      if(it == ids.end()) return p;
      gradient(it->second, opacity, shape, p);
      return p;
    }
    uint32_t rgb;
    if(v == "currentColor" || v == "currentcolor") return VecPaint::solid(packColor(0, opacity));
    if(!parseColor(v, rgb)) return p;
    return VecPaint::solid(packColor(rgb, opacity));
  }

  void gradient(pugi::xml_node g, double opacity, const std::vector<Subpath>& shape, VecPaint& p) {
    std::string name = g.name();
    bool linear = name == "linearGradient";
    if(!linear && name != "radialGradient") return;
    // Stops and attributes may come from the gradient it links to
    pugi::xml_node stopsFrom = g;
    for(int hop = 0; hop < 4 && !stopsFrom.child("stop"); hop++) {
      std::string href = stopsFrom.attribute("href").as_string();
      if(href.empty()) href = stopsFrom.attribute("xlink:href").as_string();
      auto it = href.size() > 1 ? ids.find(href.substr(1)) : ids.end();
      if(it == ids.end()) break;
      stopsFrom = it->second;
    }
    p.type = linear ? EPaintType::Linear : EPaintType::Radial;
    p.space = EGradSpace(std::clamp(g.attribute("data-david-space").as_int(int(EGradSpace::Srgb)), 0, int(EGradSpace::COUNT) - 1));
    for(pugi::xml_node s : stopsFrom.children("stop")) {
      if(s.attribute("data-david-baked")) continue;
      uint32_t rgb = 0;
      parseColor(prop(s, "stop-color").empty() ? "#000" : prop(s, "stop-color"), rgb);
      std::string so = prop(s, "stop-opacity");
      std::string off = s.attribute("offset").as_string("0");
      double pos = std::atof(off.c_str()) / (off.find('%') != std::string::npos ? 100 : 1);
      p.stops.push_back({ float(std::clamp(pos, 0.0, 1.0)), packColor(rgb, (so.empty() ? 1 : std::atof(so.c_str())) * opacity) });
    }
    if(p.stops.empty()) { p.type = EPaintType::None; return; }
    bool user = std::string(g.attribute("gradientUnits").as_string()) == "userSpaceOnUse";
    auto len = [&](const char* a, double def) {
      std::string v = g.attribute(a).as_string();
      if(v.empty()) return def;
      return std::atof(v.c_str()) / (v.find('%') != std::string::npos ? 100 : 1);
    };
    Point a, b;
    if(linear) { a = { len("x1", 0), len("y1", 0) }; b = { len("x2", user ? 0 : 1), len("y2", 0) }; }
    else { a = { len("cx", 0.5), len("cy", 0.5) }; double r = len("r", 0.5); b = { a.x + r, a.y }; }
    if(!user) {
      // objectBoundingBox: 0-1 across the shape's own box
      DRect r;
      for(auto& s : shape) for(auto& q : flatten(s, 1)) r.add(q);
      if(r.empty()) r = { 0, 0, 1, 1 };
      auto map = [&](Point q) { return Point{ r.x0 + q.x * r.width(), r.y0 + q.y * r.height() }; };
      a = map(a); b = map(b);
    }
    Affine gt = parseTransform(g.attribute("gradientTransform").as_string());
    p.a = gt.apply(a); p.b = gt.apply(b);
  }

  void applyStyle(VecObject& o, const Style& s, const std::vector<Subpath>& shape) {
    o.fill = paint(s.fill, s.fillOpacity, shape);
    o.stroke.paint = paint(s.stroke, s.strokeOpacity, shape);
    o.stroke.width = std::max(0.0, s.strokeWidth);
    o.stroke.cap = s.cap == "round" ? ECap::Round : s.cap == "square" ? ECap::Square : ECap::Butt;
    o.stroke.join = s.join == "round" ? EJoin::Round : s.join == "bevel" ? EJoin::Bevel : EJoin::Miter;
    o.stroke.miter = s.miter;
    o.stroke.align = s.align == "inside" ? EStrokeAlign::Inside : s.align == "outside" ? EStrokeAlign::Outside : EStrokeAlign::Center;
    if(!s.dash.empty() && s.dash != "none" && o.stroke.width > 0)
      for(double v : numbers(s.dash)) o.stroke.dash.push_back(v / o.stroke.width);
  }

  void common(pugi::xml_node e, VecObject& o) {
    o.id = newObjectId();
    o.xf = parseTransform(e.attribute("transform").as_string());
    std::string op = prop(e, "opacity");
    if(!op.empty()) o.opacity = std::clamp(std::atof(op.c_str()), 0.0, 1.0);
    o.visible = prop(e, "display") != "none" && prop(e, "visibility") != "hidden";
    o.locked = e.attribute("data-david-locked").as_bool();
    o.name = e.attribute("data-david-name").as_string();
    if(o.name.empty()) o.name = e.attribute("id").as_string();
  }

  double attr(pugi::xml_node e, const char* name, double def = 0) {
    std::string v = e.attribute(name).as_string();
    return v.empty() ? def : std::atof(v.c_str());
  }

  // Path data: every command, relative ones and the shorthand curves and arcs included
  static std::vector<Subpath> parsePath(const std::string& d) {
    std::vector<Subpath> out;
    Scanner sc(d.c_str());
    char cmd = 0;
    Point cur{}, start{}, lastCtl{};
    char lastCmd = 0;
    auto sub = [&]() -> Subpath& { if(out.empty()) out.push_back({ { PathNode::corner(cur) }, false }); return out.back(); };
    auto lineTo = [&](Point p) { sub().nodes.push_back(PathNode::corner(p)); cur = p; };
    auto cubicTo = [&](Point c1, Point c2, Point p) {
      Subpath& s = sub();
      s.nodes.back().out = c1;
      s.nodes.push_back({ p, c2, p, ENodeType::Corner });
      cur = p;
    };
    while(true) {
      sc.skip();
      if(!*sc.p) break;
      if(std::isalpha((unsigned char)*sc.p)) cmd = *sc.p++;
      else if(!cmd) break;
      bool rel = std::islower((unsigned char)cmd);
      char up = char(std::toupper((unsigned char)cmd));
      auto pt = [&](double x, double y) { return rel ? Point{ cur.x + x, cur.y + y } : Point{ x, y }; };
      double v[7];
      auto get = [&](int n) { for(int i = 0; i < n; i++) if(!sc.number(v[i])) return false; return true; };
      if(up == 'Z') {
        if(!out.empty()) {
          Subpath& s = out.back();
          s.closed = true;
          // The closing point repeats the start: merge it, keeping its incoming handle
          if(s.nodes.size() > 1 && std::abs(s.nodes.back().p.x - s.nodes[0].p.x) < 1e-9 && std::abs(s.nodes.back().p.y - s.nodes[0].p.y) < 1e-9) {
            s.nodes[0].in = s.nodes.back().in;
            s.nodes.pop_back();
          }
        }
        cur = start;
        lastCmd = 'Z';
        // A new segment after Z starts a new subpath from the same point
        cmd = 0;
        continue;
      }
      if(up == 'M') {
        if(!get(2)) break;
        cur = pt(v[0], v[1]);
        start = cur;
        out.push_back({ { PathNode::corner(cur) }, false });
        cmd = rel ? 'l' : 'L';
      } else if(up == 'L') { if(!get(2)) break; lineTo(pt(v[0], v[1])); }
      else if(up == 'H') { if(!get(1)) break; lineTo({ rel ? cur.x + v[0] : v[0], cur.y }); }
      else if(up == 'V') { if(!get(1)) break; lineTo({ cur.x, rel ? cur.y + v[0] : v[0] }); }
      else if(up == 'C') {
        if(!get(6)) break;
        Point c1 = pt(v[0], v[1]), c2 = pt(v[2], v[3]), p = pt(v[4], v[5]);
        cubicTo(c1, c2, p); lastCtl = c2;
      } else if(up == 'S') {
        if(!get(4)) break;
        Point c1 = (lastCmd == 'C' || lastCmd == 'S') ? Point{ 2 * cur.x - lastCtl.x, 2 * cur.y - lastCtl.y } : cur;
        Point c2 = pt(v[0], v[1]), p = pt(v[2], v[3]);
        cubicTo(c1, c2, p); lastCtl = c2;
      } else if(up == 'Q' || up == 'T') {
        Point q;
        if(up == 'Q') { if(!get(4)) break; q = pt(v[0], v[1]); v[0] = v[2]; v[1] = v[3]; }
        else { if(!get(2)) break; q = (lastCmd == 'Q' || lastCmd == 'T') ? Point{ 2 * cur.x - lastCtl.x, 2 * cur.y - lastCtl.y } : cur; }
        Point p = pt(v[0], v[1]);
        cubicTo({ cur.x + (q.x - cur.x) * 2 / 3, cur.y + (q.y - cur.y) * 2 / 3 }, { p.x + (q.x - p.x) * 2 / 3, p.y + (q.y - p.y) * 2 / 3 }, p);
        lastCtl = q;
      } else if(up == 'A') {
        bool large, sweep;
        if(!get(3) || !sc.flag(large) || !sc.flag(sweep) || !sc.number(v[3]) || !sc.number(v[4])) break;
        Point p = pt(v[3], v[4]);
        arc(cur, p, v[0], v[1], v[2], large, sweep, cubicTo, lineTo);
      } else break;
      lastCmd = up;
    }
    out.erase(std::remove_if(out.begin(), out.end(), [](const Subpath& s) { return s.nodes.size() < 2 && !s.closed; }), out.end());
    return out;
  }

  // SVG endpoint arc to cubics (one per quarter turn at most)
  template<class C, class L> static void arc(Point p0, Point p1, double rx, double ry, double deg, bool large, bool sweep, C cubicTo, L lineTo) {
    rx = std::abs(rx); ry = std::abs(ry);
    if(rx < 1e-9 || ry < 1e-9) { lineTo(p1); return; }
    double phi = deg * PI / 180, cs = std::cos(phi), sn = std::sin(phi);
    double dx = (p0.x - p1.x) / 2, dy = (p0.y - p1.y) / 2;
    double x1 = cs * dx + sn * dy, y1 = -sn * dx + cs * dy;
    double lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
    if(lam > 1) { rx *= std::sqrt(lam); ry *= std::sqrt(lam); }
    double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1, den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double k = (large != sweep ? 1 : -1) * std::sqrt(std::max(0.0, num / den));
    double cxp = k * rx * y1 / ry, cyp = -k * ry * x1 / rx;
    double cx = cs * cxp - sn * cyp + (p0.x + p1.x) / 2, cy = sn * cxp + cs * cyp + (p0.y + p1.y) / 2;
    auto ang = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
    double t1 = ang(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
    double dt = ang((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if(!sweep && dt > 0) dt -= 2 * PI;
    else if(sweep && dt < 0) dt += 2 * PI;
    int segs = std::max(1, int(std::ceil(std::abs(dt) / (PI / 2) - 1e-9)));
    double step = dt / segs, alpha = 4.0 / 3 * std::tan(step / 4);
    auto at = [&](double t) { return Point{ cx + rx * std::cos(t) * cs - ry * std::sin(t) * sn, cy + rx * std::cos(t) * sn + ry * std::sin(t) * cs }; };
    auto der = [&](double t) { return Point{ -rx * std::sin(t) * cs - ry * std::cos(t) * sn, -rx * std::sin(t) * sn + ry * std::cos(t) * cs }; };
    for(int i = 0; i < segs; i++) {
      double a = t1 + step * i, b = a + step;
      Point pa = at(a), pb = i == segs - 1 ? p1 : at(b), da = der(a), db = der(b);
      cubicTo({ pa.x + alpha * da.x, pa.y + alpha * da.y }, { pb.x - alpha * db.x, pb.y - alpha * db.y }, pb);
    }
  }

  void element(pugi::xml_node e, const Style& parentStyle, std::vector<VecObject>& into, int depth) {
    if(depth > 64) return;
    std::string name = e.name();
    if(size_t colon = name.find(':'); colon != std::string::npos) name = name.substr(colon + 1);
    Style s = inherit(e, parentStyle);
    VecObject o;
    if(name == "g" || name == "a" || name == "switch") {
      o.kind = EVecKind::Group;
      common(e, o);
      for(pugi::xml_node c : e.children()) element(c, s, o.children, depth + 1);
      if(!o.children.empty()) into.push_back(std::move(o));
      return;
    }
    if(name == "use") {
      std::string href = e.attribute("href").as_string();
      if(href.empty()) href = e.attribute("xlink:href").as_string();
      auto it = href.size() > 1 ? ids.find(href.substr(1)) : ids.end();
      if(it == ids.end()) { out.skipped++; return; }
      o.kind = EVecKind::Group;
      common(e, o);
      o.xf = o.xf * Affine::translate(attr(e, "x"), attr(e, "y"));
      pugi::xml_node ref = it->second;
      if(std::string(ref.name()) == "symbol") for(pugi::xml_node c : ref.children()) element(c, s, o.children, depth + 1);
      else element(ref, s, o.children, depth + 1);
      if(!o.children.empty()) into.push_back(std::move(o));
      return;
    }
    if(name == "defs" || name == "symbol" || name == "linearGradient" || name == "radialGradient" || name == "title" || name == "desc"
       || name == "metadata" || name == "style" || name == "namedview" || name == "clipPath" || name == "mask") return;
    common(e, o);
    if(name == "rect") {
      o.kind = EVecKind::Rect;
      o.x = attr(e, "x"); o.y = attr(e, "y"); o.w = attr(e, "width"); o.h = attr(e, "height");
      o.radius = std::max(attr(e, "rx", attr(e, "ry")), 0.0);
      if(o.w <= 0 || o.h <= 0) return;
    } else if(name == "circle" || name == "ellipse") {
      o.kind = EVecKind::Ellipse;
      double rx = name == "circle" ? attr(e, "r") : attr(e, "rx"), ry = name == "circle" ? rx : attr(e, "ry");
      o.x = attr(e, "cx") - rx; o.y = attr(e, "cy") - ry; o.w = rx * 2; o.h = ry * 2;
      if(o.w <= 0 || o.h <= 0) return;
    } else if(name == "line") {
      o.kind = EVecKind::Path;
      o.paths = { { { PathNode::corner({ attr(e, "x1"), attr(e, "y1") }), PathNode::corner({ attr(e, "x2"), attr(e, "y2") }) }, false } };
    } else if(name == "polygon" || name == "polyline") {
      if(e.attribute("data-david-box")) {
        auto box = numbers(e.attribute("data-david-box").as_string());
        if(box.size() == 4) {
          o.kind = EVecKind::Polygon;
          o.x = box[0]; o.y = box[1]; o.w = box[2]; o.h = box[3];
          o.sides = e.attribute("data-david-sides").as_int(5);
          o.inner = e.attribute("data-david-inner").as_double(1);
        }
      }
      if(o.kind != EVecKind::Polygon) {
        auto v = numbers(e.attribute("points").as_string());
        Subpath sp;
        sp.closed = name == "polygon";
        for(size_t i = 0; i + 1 < v.size(); i += 2) sp.nodes.push_back(PathNode::corner({ v[i], v[i + 1] }));
        if(sp.nodes.size() < 2) return;
        o.kind = EVecKind::Path;
        o.paths = { sp };
      }
    } else if(name == "path") {
      if(e.attribute("data-david-text")) {
        o.kind = EVecKind::Text;
        o.text = e.attribute("data-david-text").as_string();
        o.font = e.attribute("data-david-font").as_string();
        o.fontIndex = e.attribute("data-david-font-index").as_int();
        o.size = e.attribute("data-david-size").as_double(32);
        auto at = numbers(e.attribute("data-david-at").as_string());
        if(at.size() == 2) { o.x = at[0]; o.y = at[1]; }
        o.bold = e.attribute("data-david-bold").as_bool();
        o.italic = e.attribute("data-david-italic").as_bool();
        o.align = e.attribute("data-david-align").as_int();
      } else {
        o.kind = EVecKind::Path;
        pugi::xml_attribute raw = e.attribute("data-david-d");
        o.paths = parsePath(raw ? raw.as_string() : e.attribute("d").as_string());
        if(o.paths.empty()) return;
        // Corner radii written by David, subpaths split by '|'
        if(raw) {
          std::string radii = e.attribute("data-david-radius").as_string();
          size_t si = 0, start = 0;
          while(si < o.paths.size() && start <= radii.size()) {
            size_t bar = radii.find('|', start);
            auto rs = numbers(radii.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
            for(size_t ni = 0; ni < rs.size() && ni < o.paths[si].nodes.size(); ni++) o.paths[si].nodes[ni].radius = rs[ni];
            if(bar == std::string::npos) break;
            start = bar + 1;
            si++;
          }
        }
        // Node types written by David
        std::string types = e.attribute("data-david-nodes").as_string();
        size_t si = 0, ni = 0;
        for(char c : types) {
          if(c == '|') { si++; ni = 0; continue; }
          if(si < o.paths.size() && ni < o.paths[si].nodes.size())
            o.paths[si].nodes[ni].type = c == 's' ? ENodeType::Smooth : c == 'y' ? ENodeType::Symmetric : ENodeType::Corner;
          ni++;
        }
      }
    } else {
      out.skipped++;
      return;
    }
    applyStyle(o, s, o.kind == EVecKind::Path ? o.paths : outline(o));
    into.push_back(std::move(o));
  }
};
}

std::string pathString(const std::vector<Subpath>& paths) { return pathData(paths); }
std::vector<Subpath> parsePathString(const std::string& d) { return Reader::parsePath(d); }
Affine parseTransformString(const std::string& s) { return parseTransform(s); }

bool read(const std::string& text, ReadResult& out, std::string& error) {
  pugi::xml_document doc;
  pugi::xml_parse_result r = doc.load_buffer(text.data(), text.size());
  if(!r) { error = std::string("bad SVG: ") + r.description(); return false; }
  pugi::xml_node root = doc.child("svg");
  if(!root) for(pugi::xml_node c : doc.children()) if(std::string(c.name()).ends_with("svg")) { root = c; break; }
  if(!root) { error = "no <svg> element"; return false; }
  Reader rd{ doc, out };
  std::function<void(pugi::xml_node)> collect = [&](pugi::xml_node n) {
    if(const char* id = n.attribute("id").as_string(); *id) rd.ids[id] = n;
    for(pugi::xml_node c : n.children()) collect(c);
  };
  collect(root);
  // Size: width/height (px, or other units at 96 dpi), else the viewBox
  auto length = [](const std::string& v, double def) {
    if(v.empty() || v.find('%') != std::string::npos) return def;
    double x = std::atof(v.c_str());
    if(v.find("mm") != std::string::npos) x *= 96 / 25.4;
    else if(v.find("cm") != std::string::npos) x *= 96 / 2.54;
    else if(v.find("in") != std::string::npos) x *= 96;
    else if(v.find("pt") != std::string::npos) x *= 96.0 / 72;
    return x;
  };
  auto vb = numbers(root.attribute("viewBox").as_string());
  bool hasVb = vb.size() == 4 && vb[2] > 0 && vb[3] > 0;
  out.width = length(root.attribute("width").as_string(), hasVb ? vb[2] : 300);
  out.height = length(root.attribute("height").as_string(), hasVb ? vb[3] : 150);
  Affine view;
  if(hasVb) {
    // preserveAspectRatio xMidYMid meet, the default
    double s = std::min(out.width / vb[2], out.height / vb[3]);
    view = Affine::translate((out.width - vb[2] * s) / 2, (out.height - vb[3] * s) / 2) * Affine::scaling(s, s) * Affine::translate(-vb[0], -vb[1]);
  }
  Style base;
  base.fill = "#000";
  std::vector<VecObject> objects;
  for(pugi::xml_node c : root.children()) rd.element(c, rd.inherit(root, base), objects, 0);
  if(!view.identity()) for(auto& o : objects) o.xf = view * o.xf;
  out.objects = std::move(objects);
  return true;
}

std::unique_ptr<Document> loadDocument(const std::string& text, std::string& error) {
  ReadResult r;
  if(!read(text, r, error)) return nullptr;
  int w = std::clamp(int(std::ceil(r.width - 1e-6)), 1, 65535), h = std::clamp(int(std::ceil(r.height - 1e-6)), 1, 65535);
  auto doc = Document::blank(w, h, 0);
  Layer& l = doc->active();
  l.kind = ELayerKind::Vector;
  l.name = "Vector";
  VecTree& t = vectorTree(l, 0);
  t.objects = std::move(r.objects);
  t.touch();
  rasterizeVectorCel(l, 0);
  return doc;
}
#pragma endregion

}

namespace david {

#pragma region [ Trace ]
std::vector<VecObject> tracePixels(const Layer& layer, int frame) {
  int w = layer.width(), h = layer.height();
  std::vector<uint32_t> px(size_t(w) * h);
  for(int y = 0; y < h; y++) for(int x = 0; x < w; x++) px[size_t(y) * w + x] = layer.getAt(frame, x, y);
  std::vector<int> region(px.size(), -1);
  std::vector<VecObject> out;
  // Regions: 4-connected same-color pixels (alpha 0 is nothing)
  std::vector<int> stack;
  int regions = 0;
  for(size_t i = 0; i < px.size(); i++) {
    if(region[i] >= 0 || (px[i] >> 24) == 0) continue;
    uint32_t c = px[i];
    int id = regions++;
    region[i] = id;
    stack.push_back(int(i));
    while(!stack.empty()) {
      int k = stack.back();
      stack.pop_back();
      int x = k % w, y = k / w;
      auto visit = [&](int nx, int ny) {
        if(nx < 0 || ny < 0 || nx >= w || ny >= h) return;
        size_t n = size_t(ny) * w + nx;
        if(region[n] < 0 && px[n] == c) { region[n] = id; stack.push_back(int(n)); }
      };
      visit(x - 1, y); visit(x + 1, y); visit(x, y - 1); visit(x, y + 1);
    }
  }
  // Each region's boundary: the pixel edges between it and anything else, chained into closed loops
  // (outer contour and holes). Directed edges keep the region on their right, so holes wind the other way
  // and the nonzero fill leaves them open.
  auto at = [&](int x, int y) { return x < 0 || y < 0 || x >= w || y >= h ? -1 : region[size_t(y) * w + x]; };
  std::vector<std::vector<std::array<int, 4>>> edges(regions);
  for(int y = 0; y < h; y++)
    for(int x = 0; x < w; x++) {
      int r = region[size_t(y) * w + x];
      if(r < 0) continue;
      if(at(x, y - 1) != r) edges[r].push_back({ x, y, x + 1, y });          // top, left to right
      if(at(x + 1, y) != r) edges[r].push_back({ x + 1, y, x + 1, y + 1 });  // right, down
      if(at(x, y + 1) != r) edges[r].push_back({ x + 1, y + 1, x, y + 1 });  // bottom, right to left
      if(at(x - 1, y) != r) edges[r].push_back({ x, y + 1, x, y });          // left, up
    }
  std::vector<uint32_t> colors(regions);
  for(size_t i = 0; i < px.size(); i++) if(region[i] >= 0) colors[region[i]] = px[i];
  for(int r = 0; r < regions; r++) {
    // From each vertex, the edges leaving it (two at most, at a pinch where diagonal pixels touch)
    std::map<std::pair<int, int>, std::vector<size_t>> from;
    auto& es = edges[r];
    for(size_t i = 0; i < es.size(); i++) from[{ es[i][0], es[i][1] }].push_back(i);
    std::vector<bool> used(es.size(), false);
    VecObject o;
    o.id = newObjectId();
    o.kind = EVecKind::Path;
    o.fill = VecPaint::solid(colors[r]);
    for(size_t s = 0; s < es.size(); s++) {
      if(used[s]) continue;
      Subpath sp;
      sp.closed = true;
      size_t e = s;
      int dx0 = 0, dy0 = 0;
      while(!used[e]) {
        used[e] = true;
        int dx = es[e][2] - es[e][0], dy = es[e][3] - es[e][1];
        // Only corners become nodes: straight runs of edges merge
        if(sp.nodes.empty() || dx != dx0 || dy != dy0) sp.nodes.push_back(PathNode::corner({ double(es[e][0]), double(es[e][1]) }));
        dx0 = dx; dy0 = dy;
        auto& next = from[{ es[e][2], es[e][3] }];
        size_t pick = SIZE_MAX;
        // At a pinch, turn the way that keeps this region's pixels together (a right turn)
        for(size_t n : next) {
          if(used[n]) continue;
          int ndx = es[n][2] - es[n][0], ndy = es[n][3] - es[n][1];
          if(pick == SIZE_MAX || (dx * ndy - dy * ndx) > 0) pick = n;
        }
        if(pick == SIZE_MAX) break;
        e = pick;
      }
      // The loop's start may sit in the middle of a straight run
      if(sp.nodes.size() > 2) {
        auto straight = [&](size_t i) {
          const Point &a = sp.nodes[(i + sp.nodes.size() - 1) % sp.nodes.size()].p, &b = sp.nodes[i].p, &c = sp.nodes[(i + 1) % sp.nodes.size()].p;
          return std::abs((b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x)) < 1e-9;
        };
        if(straight(0)) sp.nodes.erase(sp.nodes.begin());
      }
      if(sp.nodes.size() >= 3) o.paths.push_back(std::move(sp));
    }
    if(!o.paths.empty()) out.push_back(std::move(o));
  }
  return out;
}
#pragma endregion

void registerSvgFormat() {
  FileFormat f;
  f.ext = "svg";
  f.name = "SVG";
  f.layered = true;
  f.sniff = [](const std::vector<uint8_t>& b) {
    std::string head(b.begin(), b.begin() + std::min<size_t>(b.size(), 1024));
    return head.find("<svg") != std::string::npos;
  };
  f.load = [](const std::vector<uint8_t>& b, std::string& err) { return svg::loadDocument(std::string(b.begin(), b.end()), err); };
  f.save = [](const Document& d, const SaveOptions&, std::string&) {
    std::string s = svg::writeDocument(d);
    return std::vector<uint8_t>(s.begin(), s.end());
  };
  registerFormat(f);
}

}
