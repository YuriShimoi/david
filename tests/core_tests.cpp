// Small self-checking test runner for david_core. Usage: david_tests [extra.png ...]
#include "core/Actions.h"
#include "core/Animation.h"
#include "core/io/Anim.h"
#include "core/Autotile.h"
#include "core/Blend.h"
#include "core/Pattern.h"
#include "core/io/Zip.h"
#include "core/io/Formats.h"
#include "core/Effect.h"
#include "core/Palette.h"
#include "core/Color.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Draw.h"
#include "core/Gradient.h"
#include "core/Oklab.h"
#include "core/Paint.h"
#include "core/Raster.h"
#include "core/Resize.h"
#include "core/TextLayout.h"
#include "core/Stroke.h"
#include "core/TileEdit.h"
#include "core/io/Png.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iterator>

using namespace david;

static int s_failed = 0;
#define CHECK(...) do { if(!(__VA_ARGS__)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__); s_failed++; } } while(0)

static std::vector<char> readBytes(const std::filesystem::path& p) {
  std::ifstream in(p, std::ios::binary);
  return { std::istreambuf_iterator<char>(in), {} };
}

static bool samePixels(const Document& a, const Document& b) {
  if(a.width() != b.width() || a.height() != b.height()) return false;
  std::vector<uint32_t> ra(a.width()), rb(b.width());
  for(int y = 0; y < a.height(); y++) {
    flattenRow(a, y, ra.data());
    flattenRow(b, y, rb.data());
    if(ra != rb) return false;
  }
  return true;
}

static void testUndoAcrossTiles() {
  auto doc = Document::blank(600, 300, WHITE);
  Layer& layer = doc->active();
  TileRecorder rec(layer);
  // A horizontal line crossing the 256 and 512 tile edges
  for(int x = 200; x < 560; x++) { rec.touchPixel(x, 255); layer.set(x, 255, BLACK); }
  for(int y = 250; y < 262; y++) { rec.touchPixel(300, y); layer.set(300, y, BLACK); }
  auto edit = rec.finish("Pencil", "");
  CHECK(edit && edit->entries.size() == 4);
  doc->history.push(*doc, std::move(edit));
  CHECK(doc->modified());
  CHECK(layer.get(256, 255) == BLACK && layer.get(300, 261) == BLACK);

  doc->history.undo(*doc);
  CHECK(layer.get(256, 255) == WHITE && layer.get(300, 261) == WHITE && layer.get(559, 255) == WHITE);
  CHECK(!doc->modified());
  doc->history.redo(*doc);
  CHECK(layer.get(256, 255) == BLACK && layer.get(559, 255) == BLACK);

  // Writing the same value again records nothing
  TileRecorder same(layer);
  same.touchPixel(256, 255); layer.set(256, 255, BLACK);
  CHECK(same.finish("Pencil", "") == nullptr);
}

static void testSparseUndo() {
  Document doc(512, 512);
  doc.layers.push_back(std::make_unique<Layer>(512, 512, "Layer 1"));
  Layer& layer = doc.active();
  TileRecorder rec(layer);
  rec.touchPixel(10, 10); layer.set(10, 10, BLACK);
  doc.history.push(doc, rec.finish("Pencil", ""));
  CHECK(layer.tile(0, 0) != nullptr && layer.tile(1, 1) == nullptr);
  doc.history.undo(doc);
  CHECK(layer.tile(0, 0) == nullptr);
}

static void testHistoryCap() {
  auto doc = Document::blank(256, 256, WHITE);
  doc->history.setCap(Tile::bytes() * 5);
  for(int i = 0; i < 10; i++) {
    TileRecorder rec(doc->active());
    rec.touchPixel(i, 0); doc->active().set(i, 0, BLACK);
    doc->history.push(*doc, rec.finish("Pencil", ""));
  }
  CHECK(doc->history.count() < 10 && doc->history.bytes() <= Tile::bytes() * 5);
  CHECK(doc->modified());
}

static void testBlend() {
  CHECK(blendOver(WHITE, BLACK) == BLACK);
  CHECK(blendOver(BLACK, CLEAR) == BLACK);
  CHECK(blendOver(CLEAR, Color{ 10, 20, 30, 40 }.pack()) == Color{ 10, 20, 30, 40 }.pack());
  Color half = Color::unpack(blendOver(WHITE, Color{ 0, 0, 0, 128 }.pack()));
  CHECK(half.a == 255 && half.r > 120 && half.r < 135);
}

static void testPngRoundTrip(const std::filesystem::path& dir) {
  std::string err;
  auto doc = Document::blank(300, 270, CLEAR);
  for(int y = 0; y < 270; y++)
    for(int x = 0; x < 300; x++)
      if((x + y) % 3) doc->active().set(x, y, Color{ uint8_t(x), uint8_t(y), uint8_t(x ^ y), uint8_t((x * 7 + y) & 0xFF) }.pack());

  auto a = pathToUtf8(dir / "rgba-\xc3\xa7.png");
  CHECK(png::save(*doc, a, err));
  auto back = png::load(a, err);
  CHECK(back && samePixels(*doc, *back));

  // Open + save with no edits gives the same bytes
  auto b = pathToUtf8(dir / "rgba-2.png");
  CHECK(back && png::save(*back, b, err));
  CHECK(readBytes(pathFromUtf8(a)) == readBytes(pathFromUtf8(b)));

  auto opaque = Document::blank(513, 2, WHITE);
  opaque->active().set(512, 1, BLACK);
  auto c = pathToUtf8(dir / "rgb.png");
  CHECK(png::save(*opaque, c, err));
  auto back2 = png::load(c, err);
  CHECK(back2 && samePixels(*opaque, *back2));
}

// External files: pixels survive open/save, and a second open/save is byte-identical
static void testExternal(const std::string& path, const std::filesystem::path& dir) {
  std::string err;
  auto doc = png::load(path, err);
  CHECK(doc != nullptr);
  if(!doc) { std::printf("  %s: %s\n", path.c_str(), err.c_str()); return; }
  auto a = pathToUtf8(dir / "ext-a.png"), b = pathToUtf8(dir / "ext-b.png");
  CHECK(png::save(*doc, a, err));
  auto back = png::load(a, err);
  CHECK(back && samePixels(*doc, *back));
  CHECK(back && png::save(*back, b, err));
  CHECK(readBytes(pathFromUtf8(a)) == readBytes(pathFromUtf8(b)));
  std::printf("  %s: %dx%d ok\n", path.c_str(), doc->width(), doc->height());
}

static void testStrokeNoBuildup() {
  auto doc = Document::blank(300, 100, WHITE);
  Stroke st(*doc, doc->active(), EInk::Paint, Color{ 0, 0, 0, 128 }.pack());
  st.moveTo(10, 50, 8, 1);
  st.dab(10, 50, 8, 1, true);
  st.lineTo(200, 50, 8, 1, true);
  st.lineTo(10, 50, 8, 1, true);   // back over itself
  uint32_t mid = doc->active().get(100, 50);
  CHECK(Color::unpack(mid).r > 120 && Color::unpack(mid).r < 135);
  doc->history.push(*doc, st.finish("Paintbrush", ""));
  CHECK(doc->history.count() == 1);
}

// Replace puts the color's alpha in place: half-transparent red over white leaves half-transparent red, and
// full transparency erases
static void testReplaceInk() {
  auto doc = Document::blank(100, 40, WHITE);
  Stroke st(*doc, doc->active(), EInk::Replace, Color{ 255, 0, 0, 128 }.pack());
  st.moveTo(10, 10, 4, 1);
  st.lineTo(90, 10, 4, 1, false);
  CHECK(doc->active().get(50, 10) == Color{ 255, 0, 0, 128 }.pack());
  Stroke clear(*doc, doc->active(), EInk::Replace, 0);
  clear.moveTo(10, 30, 4, 1);
  clear.lineTo(90, 30, 4, 1, false);
  CHECK((doc->active().get(50, 30) >> 24) == 0 && doc->active().get(50, 20) == WHITE);
}

static void testSelectionClip() {
  auto doc = Document::blank(100, 100, WHITE);
  doc->selection = rectMask(100, 100, 10, 10, 20, 20);
  Stroke st(*doc, doc->active(), EInk::Paint, BLACK);
  st.moveTo(0, 15, 3, 1);
  st.lineTo(99, 15, 3, 1, false);
  CHECK(doc->active().get(15, 15) == BLACK);
  CHECK(doc->active().get(25, 15) == WHITE && doc->active().get(5, 15) == WHITE);
}

static void testMaskOps() {
  auto a = rectMask(600, 600, 0, 0, 300, 300);
  auto b = rectMask(600, 600, 200, 200, 500, 500);
  auto u = a->clone(); u->combine(*b, ECombine::Union);
  CHECK(u->get(450, 450) == 255 && u->get(100, 100) == 255 && u->get(100, 450) == 0);
  auto i = a->clone(); i->combine(*b, ECombine::Intersect);
  IRect r = i->bounds();
  CHECK(r.x0 == 200 && r.y0 == 200 && r.x1 == 299 && r.y1 == 299);
  auto x = a->clone(); x->combine(*b, ECombine::Xor);
  CHECK(x->get(250, 250) == 0 && x->get(100, 100) == 255);
  auto e = a->clone(); e->combine(*b, ECombine::Exclude);
  CHECK(e->get(250, 250) == 0 && e->get(100, 100) == 255);
  auto all = std::make_unique<Mask>(600, 600); all->fillAll(); all->invert();
  CHECK(all->empty());
  auto el = ellipseMask(600, 600, 100, 100, 300, 200);
  CHECK(el->get(200, 150) == 255 && el->get(101, 101) == 0 && el->get(200, 99) == 0);
  bool partial = false;
  for(int x = 200; x < 300; x++) partial |= el->get(x, 185) > 0 && el->get(x, 185) < 255;
  CHECK(partial);
  auto tri = polygonMask(600, 600, { { 10, 10 }, { 100, 10 }, { 10, 100 } });
  CHECK(tri->get(20, 20) == 255 && tri->get(90, 90) == 0);
  CHECK(maskOutline(*a).size() == 4);
}

static void testFlood() {
  auto doc = Document::blank(600, 300, WHITE);
  for(int y = 0; y < 300; y++) doc->active().set(300, y, BLACK);  // a wall across tile edges
  auto m = floodMask(*doc, &doc->active(), 10, 10, { .tolerance = 0 });
  CHECK(m->get(299, 299) == 255 && m->get(300, 0) == 0 && m->get(301, 0) == 0);
  auto g = floodMask(*doc, &doc->active(), 10, 10, { .tolerance = 0, .contiguous = false });
  CHECK(g->get(301, 0) == 255 && g->get(300, 0) == 0);
  auto all = floodMask(*doc, nullptr, 10, 10, { .tolerance = 1 });
  CHECK(all->get(300, 5) == 255);
}

static void testLayersUndo() {
  auto doc = Document::blank(300, 300, WHITE);
  addLayer(*doc, std::make_unique<Layer>(300, 300, "Layer 2"), 1, "Add New Layer", "");
  CHECK(doc->layers.size() == 2 && doc->activeLayer == 1);
  doc->active().set(5, 5, Color{ 255, 0, 0, 255 }.pack());
  setLayerProps(*doc, 1, { "Layer 2", true, 128 }, "");
  mergeDown(*doc, 1, "");
  CHECK(doc->layers.size() == 1);
  Color c = Color::unpack(doc->layers[0]->get(5, 5));
  CHECK(c.r == 255 && c.g > 120 && c.g < 135);
  doc->history.undo(*doc);
  CHECK(doc->layers.size() == 2 && doc->layers[0]->get(5, 5) == WHITE && doc->layers[1]->opacity == 128);
  doc->history.undo(*doc);
  CHECK(doc->layers[1]->opacity == 255);
  moveLayer(*doc, 1, 0, "");
  CHECK(doc->layers[0]->name == "Layer 2");
  doc->history.undo(*doc);
  deleteLayer(*doc, 0, "");
  CHECK(doc->layers.size() == 1 && doc->layers[0]->name == "Layer 2");
  doc->history.undo(*doc);
  CHECK(doc->layers.size() == 2 && doc->layers[0]->name == "Background");
}

static void testMove() {
  auto doc = Document::blank(600, 300, WHITE);
  for(int y = 10; y < 20; y++) for(int x = 10; x < 20; x++) doc->active().set(x, y, BLACK);
  auto sel = rectMask(600, 300, 10, 10, 20, 20);
  Lifted l = lift(doc->active(), *sel);
  TileRecorder rec(doc->active());
  Xform xf; xf.dx = 300; xf.dy = 5;
  stampLifted(doc->active(), rec, *sel, l, xf, false);
  CHECK(doc->active().get(15, 15) == 0 && doc->active().get(315, 20) == BLACK && doc->active().get(309, 20) == WHITE);
  rec.restore();
  CHECK(doc->active().get(15, 15) == BLACK && doc->active().get(315, 20) == WHITE);
  auto moved = transformMask(*sel, xf);
  CHECK(moved->get(315, 20) == 255 && moved->get(15, 15) == 0);
  xf.dx = 0; xf.dy = 0; xf.angle = 3.14159265358979 / 2; xf.cx = 15; xf.cy = 15;
  stampLifted(doc->active(), rec, *sel, l, xf, true);
  CHECK(doc->active().get(15, 15) == BLACK);
}

static void testOklabFlood() {
  CHECK(colorDistance(BLACK, WHITE) > 0.99f && colorDistance(BLACK, WHITE) <= 1.f);
  CHECK(colorDistance(0x00FF0000, 0x0000FF00) == 0);  // two transparent pixels always match
  // Two light-gray halves split by a 1-px dark line; the halves differ by one step
  auto doc = Document::blank(64, 32, Color{ 200, 200, 200, 255 }.pack());
  Layer& l = doc->active();
  for(int y = 0; y < 32; y++) {
    l.set(31, y, Color{ 40, 40, 40, 255 }.pack());
    for(int x = 32; x < 64; x++) l.set(x, y, Color{ 201, 201, 201, 255 }.pack());
  }
  FloodOptions o;
  o.tolerance = 0;
  auto exact = floodMask(*doc, &l, 5, 5, o);
  CHECK(exact->get(30, 5) == 255 && exact->get(31, 5) == 0 && exact->get(40, 5) == 0);
  o.tolerance = 1;
  auto all = floodMask(*doc, &l, 5, 5, o);
  CHECK(all->get(31, 5) == 255 && all->get(40, 5) == 255);
  o.edgeStop = 0.2f;
  auto stopped = floodMask(*doc, &l, 5, 5, o);
  CHECK(stopped->get(20, 5) == 255 && stopped->get(40, 5) == 0);
  // Soft: full near the seed color, partial near the tolerance
  o = {};
  o.tolerance = colorDistance(Color{ 200, 200, 200, 255 }.pack(), Color{ 201, 201, 201, 255 }.pack()) * 1.5f;
  o.soft = true;
  o.contiguous = false;
  auto soft = floodMask(*doc, &l, 5, 5, o);
  CHECK(soft->get(5, 5) == 255 && soft->get(40, 5) > 0 && soft->get(40, 5) < 255);
  // Fill to outline: everything but the line
  o = {};
  o.tolerance = 0;
  o.toOutline = true;
  o.outline = Color{ 40, 40, 40, 255 }.pack();
  auto inside = floodMask(*doc, &l, 5, 5, o);
  CHECK(inside->get(20, 5) == 255 && inside->get(31, 5) == 0 && inside->get(40, 5) == 0);
  // Global mode reaches the far side without the line
  o = {};
  o.tolerance = 0.05f;
  o.contiguous = false;
  auto global = floodMask(*doc, &l, 5, 5, o);
  CHECK(global->get(40, 5) == 255 && global->get(31, 5) == 0);
}

static void testGradient() {
  GradientSpec g;
  g.a = { 0, 0 }; g.b = { 100, 0 };
  g.stops = { { 0, BLACK }, { 1, WHITE } };
  CHECK(gradientPos(g, 50, 7) == 0.5f && gradientPos(g, -5, 0) == 0 && gradientPos(g, 500, 0) == 1);
  g.type = EGradType::Radial;
  CHECK(std::abs(gradientPos(g, 0, 50) - 0.5f) < 1e-4f);
  g.type = EGradType::Linear;
  auto t = gradientTable(g);
  CHECK(t[0] < 0.01f && t[4095 * 4] > 254.5f && t[4095 * 4 + 3] > 254.5f);
  // OKLab's midpoint is perceptual mid-gray (sRGB ~99), not sRGB's 128
  float mid = t[2048 * 4];
  CHECK(mid > 90 && mid < 110);
  g.space = EGradSpace::Srgb;
  CHECK(std::abs(gradientTable(g)[2048 * 4] - 127.5f) < 1);
  // A fade to transparent keeps the color instead of darkening (premultiplied mixing)
  g.stops = { { 0, Color{ 255, 0, 0, 255 }.pack() }, { 1, Color{ 0, 0, 0, 0 }.pack() } };
  auto fade = gradientTable(g);
  CHECK(fade[2048 * 4] > 250 && fade[2048 * 4 + 3] > 120 && fade[2048 * 4 + 3] < 135);
  // Blue noise covers every rank once
  auto& bn = blueNoise();
  std::vector<float> sorted(bn);
  std::sort(sorted.begin(), sorted.end());
  CHECK(bn.size() == 4096 && std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end());
  // Rendered and undone as one edit
  auto doc = Document::blank(300, 40, WHITE);
  g = {};
  g.a = { 0, 0 }; g.b = { 300, 0 };
  g.stops = { { 0, BLACK }, { 1, WHITE } };
  g.dither = EDither::BlueNoise;
  LiveEdit edit;
  renderGradient(*doc, edit.redraw(*doc), g);
  uint32_t c = doc->active().get(150, 10);
  CHECK((c & 0xFF) > 80 && (c & 0xFF) < 120);
  renderGradient(*doc, edit.redraw(*doc), g);
  edit.commit(*doc, "Gradient", "");
  CHECK(doc->history.count() == 1);
  doc->history.undo(*doc);
  CHECK(doc->active().get(150, 10) == WHITE);
}

static void testDraw() {
  // A 10.5 × 4 rectangle covers 42 pixels' worth of coverage
  auto m = fillContours(64, 64, { { { 2, 2 }, { 12.5, 2 }, { 12.5, 6 }, { 2, 6 } } }, true);
  double sum = 0;
  for(int y = 0; y < 64; y++) for(int x = 0; x < 64; x++) sum += m->get(x, y) / 255.0;
  CHECK(std::abs(sum - 42) < 0.5);
  // A ring: outer minus inner
  auto ring = fillContours(64, 64, { shapeContour(EShapeKind::Rect, 10, 10, 30, 30, 0, 0, 0), shapeContour(EShapeKind::Rect, 10, 10, 30, 30, 2, 0, 0) }, true);
  CHECK(ring->get(10, 20) == 255 && ring->get(11, 20) == 255 && ring->get(12, 20) == 0 && ring->get(20, 20) == 0);
  // Pixel-perfect diagonal: no pixel has both a horizontal and a vertical neighbor
  Mask p(64, 64);
  pixelPolyline(p, { { 0.5, 0.5 }, { 20.5, 10.5 } }, true);
  bool clean = true;
  for(int y = 1; y < 63; y++)
    for(int x = 1; x < 63; x++)
      if(p.get(x, y) && (p.get(x - 1, y) || p.get(x + 1, y)) && (p.get(x, y - 1) || p.get(x, y + 1))) clean = false;
  CHECK(clean && p.get(0, 0) && p.get(20, 10));
  // Dashes: 30 px line with 4 on / 2 off makes 5 dashes
  CHECK(dashed({ { 0, 0 }, { 30, 0 } }, 4, 2).size() == 5);
  Mask s(64, 64);
  strokePolyline(s, { { 10, 32 }, { 50, 32 } }, 4, true);
  CHECK(s.get(30, 31) == 255 && s.get(30, 34) == 0);
}

static void testText() {
  // Needs any system font; skipped when none of the usual ones is there
  std::string font;
  for(const char* f : { "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
                        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf" })
    if(std::filesystem::exists(f)) { font = f; break; }
  if(font.empty()) { std::printf("skip text: no known font\n"); return; }
  TextStyle st;
  st.path = font;
  st.size = 24;
  auto b = layoutText(U"Hi\nthere", st, 10, 10, 200, 100);
  CHECK(b.ok && b.lines.size() == 2 && b.lines[1].start == 3 && b.lines[1].carets.size() == 6);
  CHECK(!b.mask->bounds().empty() && b.mask->bounds().x0 >= 10 && b.width > 20);
  CHECK(b.lines[0].carets[1] > b.lines[0].carets[0]);
  st.align = 2;
  auto r = layoutText(U"Hi\nthere", st, 10, 10, 200, 100);
  CHECK(std::abs(r.lines[0].carets.back() - r.lines[1].carets.back()) < 0.01);
}

static void testResize() {
  // A 1-px black/white checkerboard averages to mid gray when halved (with gamma: linear-light mid gray)
  Layer l(64, 64, "L");
  for(int y = 0; y < 64; y++) for(int x = 0; x < 64; x++) l.set(x, y, (x + y) % 2 ? WHITE : BLACK);
  auto half = resampleLayer(l, 32, 32, EResample::Supersample, false);
  uint8_t g = Color::unpack(half->get(10, 10)).r;
  CHECK(half->width() == 32 && g >= 126 && g <= 129);
  auto halfGamma = resampleLayer(l, 32, 32, EResample::Best, true);
  g = Color::unpack(halfGamma->get(10, 10)).r;
  CHECK(g >= 185 && g <= 190);
  auto up = resampleLayer(l, 128, 128, EResample::Nearest, false);
  CHECK(up->get(0, 0) == BLACK && up->get(1, 1) == BLACK && up->get(2, 0) == WHITE);
  // Transparent edges don't darken (premultiplied filtering)
  Layer t(8, 8, "T");
  t.set(3, 3, Color{ 255, 0, 0, 255 }.pack());
  auto smooth = resampleLayer(t, 16, 16, EResample::Bilinear, false);
  Color c = Color::unpack(smooth->get(6, 6));
  CHECK(c.a > 0 && c.r == 255 && c.g == 0);

  auto doc = Document::blank(100, 50, WHITE);
  doc->active().set(0, 0, BLACK);
  resizeCanvas(*doc, 120, 70, 1, 1, Color{ 255, 0, 0, 255 }.pack(), "");
  CHECK(doc->width() == 120 && doc->height() == 70 && doc->active().get(10, 10) == BLACK && doc->active().get(0, 0) == Color{ 255, 0, 0, 255 }.pack());
  resizeImage(*doc, 60, 35, 72, EResample::Best, true, "");
  CHECK(doc->width() == 60 && doc->dpi == 72 && doc->history.count() == 2);
  doc->history.undo(*doc);
  doc->history.undo(*doc);
  CHECK(doc->width() == 100 && doc->height() == 50 && doc->active().get(0, 0) == BLACK && doc->dpi == 96);
  doc->history.redo(*doc);
  CHECK(doc->width() == 120);
}

static void testPalette(const std::filesystem::path& dir) {
  Palette p;
  std::string err;
  CHECK(p.parse("GIMP Palette\nName: t\nColumns: 4\n#\n255 0 0 red\n  0 255 0\n0 0 255\n", err) && p.size() == 3);
  CHECK(p.colors[0] == 0xFF0000FFu && p.colors[2] == 0xFFFF0000u);
  for(const char* ext : { ".gpl", ".pal", ".hex" }) {
    auto path = pathToUtf8(dir / (std::string("pal") + ext));
    Palette q;
    CHECK(p.save(path, err) && q.load(path, err) && q.colors == p.colors);
  }
  CHECK(p.find(0x800000FFu) == 0 && p.find(0xFF123456u) == -1);
  CHECK(p.nearest(0xFF0000E0u) == 0);
  p.sortByLightness();
  CHECK(p.colors[0] == 0xFFFF0000u);  // blue is the darkest

  // Four colors in different amounts come back exactly
  std::vector<uint32_t> px;
  uint32_t four[] = { 0xFF000000u, 0xFFFFFFFFu, 0xFF2040C0u, 0xFF10A060u };
  for(int i = 0; i < 4; i++) px.insert(px.end(), 50 + i * 30, four[i]);
  Palette m = Palette::fromPixels(px, 4);
  CHECK(m.size() == 4);
  for(uint32_t c : four) CHECK(m.find(c) >= 0);
  // More colors than asked: a gradient cut to 8
  px.clear();
  for(int i = 0; i < 256; i++) px.push_back(0xFF000000u | uint32_t(i) | uint32_t(255 - i) << 16);
  CHECK(Palette::fromPixels(px, 8).size() == 8);
}

static void testStrokeSpace() {
  // Symmetry: a dab on the left gets its mirror on the right
  auto doc = Document::blank(100, 60, WHITE);
  {
    Stroke st(*doc, doc->active(), EInk::Paint, BLACK);
    StrokeSpace sp; sp.mirrorX = true; sp.axisX = 50; sp.mirrorY = true; sp.axisY = 30;
    st.setSpace(sp);
    st.cover(10, 5, 255);
    doc->history.push(*doc, st.finish("Pencil", ""));
  }
  Layer& l = doc->active();
  CHECK(l.get(10, 5) == BLACK && l.get(89, 5) == BLACK && l.get(10, 54) == BLACK && l.get(89, 54) == BLACK);
  CHECK(l.get(90, 5) == WHITE && l.get(88, 5) == WHITE);
  // Wrap: a dab past the right edge lands on the left
  {
    Stroke st(*doc, doc->active(), EInk::Paint, BLACK);
    StrokeSpace sp; sp.wrapX = true; sp.wrapY = true;
    st.setSpace(sp);
    st.dab(99.5, 30.5, 3, 1, false);
    st.cover(-1, -1, 255);
    doc->history.push(*doc, st.finish("Brush", ""));
  }
  CHECK(l.get(99, 30) == BLACK && l.get(1, 30) == BLACK && l.get(99, 59) == BLACK);
  // Uncover puts the pixel back
  {
    Stroke st(*doc, doc->active(), EInk::Paint, BLACK);
    st.cover(40, 40, 255);
    st.uncover(40, 40);
    CHECK(l.get(40, 40) == WHITE && !st.finish("x", ""));
  }
  // Shade ink steps along the palette, only for swatch colors
  Palette pal;
  pal.colors = { 0xFF000000u, 0xFF404040u, 0xFF808080u };
  l.set(20, 20, 0xFF404040u);
  {
    Stroke st(*doc, doc->active(), EInk::Shade, 0);
    st.setShade(&pal, 1);
    st.cover(20, 20, 255);
    st.cover(20, 20, 255);
    st.cover(21, 20, 255);
  }
  CHECK(l.get(20, 20) == 0xFF808080u && l.get(21, 20) == WHITE);
}

static void testOutline() {
  auto doc = Document::blank(20, 20, CLEAR);
  Layer& l = doc->active();
  for(int y = 8; y < 12; y++) for(int x = 8; x < 12; x++) l.set(x, y, BLACK);
  const Effect* e = findEffect("outline");
  CHECK(e != nullptr);
  if(!e) return;
  ParamValues v = defaultValues(*e);
  v.num["color"] = double(0xFF0000FFu);
  TileRecorder rec(l);
  applyEffect(*doc, rec, *e, v, {});
  // 8-connected outside: the ring around the square, corners included
  CHECK(l.get(7, 7) == 0xFF0000FFu && l.get(12, 10) == 0xFF0000FFu && l.get(9, 9) == BLACK && l.get(6, 6) == CLEAR);
  rec.restore();
  std::string err;
  CHECK(parseValues(*e, "side=inside,connect=4,thickness=1,color=#00ff00", v, err));
  TileRecorder rec2(l);
  applyEffect(*doc, rec2, *e, v, {});
  CHECK(l.get(8, 8) == 0xFF00FF00u && l.get(9, 9) == BLACK && l.get(7, 7) == CLEAR);
  CHECK(!parseValues(*e, "nope=1", v, err));
}

static void testAutotile() {
  // Border tile: red with a black frame; floor: green
  int g = 16;
  Image src(g * 2, g);
  for(int y = 0; y < g; y++)
    for(int x = 0; x < g; x++) {
      bool edge = x == 0 || y == 0 || x == g - 1 || y == g - 1;
      src.at(x, y) = edge ? BLACK : 0xFF0000FFu;
      src.at(g + x, y) = 0xFF00FF00u;
    }
  Image sheet = autotile(src, g, 2, 1);
  CHECK(sheet.w == g * 12 && sheet.h == g * 4);
  // Tile 33 (row 2, col 9) is all floor; tile 22 is the border tile's top-right quarter everywhere
  CHECK(sheet.at(9 * g + 5, 2 * g + 5) == 0xFF00FF00u);
  CHECK(sheet.at(10 * g + 0, 1 * g + 0) == BLACK && sheet.at(10 * g + 3, 1 * g + 3) == 0xFF0000FFu);
  CHECK(AUTOTILE_MASKS[33] == 255 && AUTOTILE_MASKS[22] == -1);
}

static uint32_t rgb(int r, int g, int b, int a = 255) { return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; }

static Image runOn(const char* id, const Image& src, const char* spec = "", const EffectEnv& env = {}) {
  const Effect* e = findEffect(id);
  CHECK(e != nullptr);
  if(!e) return src;
  ParamValues v = defaultValues(*e);
  std::string err;
  CHECK(parseValues(*e, spec, v, err));
  return runEffect(*e, src, v, env);
}

static void testBlendModes() {
  uint32_t gray = rgb(128, 128, 128), white = WHITE;
  CHECK(blendPixel(white, gray, 255, EBlend::Multiply) == rgb(128, 128, 128));
  CHECK(blendPixel(rgb(100, 100, 100), rgb(50, 50, 50), 255, EBlend::Difference) == rgb(50, 50, 50));
  CHECK(blendPixel(rgb(10, 20, 30), rgb(40, 50, 60), 255, EBlend::Normal) == blendOver(rgb(10, 20, 30), rgb(40, 50, 60)));
  // Over a transparent backdrop every mode is plain "over"
  CHECK(blendPixel(0, gray, 255, EBlend::Multiply) == gray);
  CHECK(blendFromId(blendId(EBlend::ColorDodge)) == EBlend::ColorDodge);
  // The flattened image uses the layer's mode
  auto doc = Document::blank(4, 4, WHITE);
  auto top = std::make_unique<Layer>(4, 4, "top");
  top->fill(gray);
  top->blend = EBlend::Screen;
  doc->layers.push_back(std::move(top));
  std::vector<uint32_t> row(4);
  flattenRow(*doc, 0, row.data());
  CHECK(row[0] == WHITE);
  doc->layers[0]->fill(BLACK);
  flattenRow(*doc, 0, row.data());
  CHECK(row[0] == gray);
}

static void testAdjustments() {
  Image img(4, 1);
  img.px = { rgb(0, 0, 0), rgb(255, 0, 0), rgb(10, 200, 100, 128), rgb(128, 128, 128) };
  Image o = runOn("invert", img);
  CHECK(o.px[0] == WHITE && o.px[1] == rgb(0, 255, 255) && (o.px[2] >> 24) == 128);
  o = runOn("black-and-white", img);
  CHECK(o.px[1] == rgb(76, 76, 76) && o.px[3] == rgb(128, 128, 128));
  o = runOn("posterize", img, "levels=2");
  CHECK(o.px[3] == rgb(255, 255, 255) && o.px[2] == rgb(0, 255, 0, 128));
  o = runOn("levels", img);
  CHECK(o.px == img.px);
  o = runOn("levels", img, "inWhite=128");
  CHECK(o.px[3] == WHITE);
  o = runOn("curves", img);
  CHECK(o.px == img.px);
  o = runOn("curves", img, "curve=0:255/255:0");
  CHECK(o.px[0] == WHITE && o.px[1] == rgb(0, 255, 255));
  o = runOn("brightness-contrast", img);
  CHECK(o.px == img.px);
  o = runOn("hue-saturation", img);
  for(int i = 0; i < 4; i++)
    for(int s = 0; s < 32; s += 8) CHECK(std::abs(int((o.px[i] >> s) & 0xFF) - int((img.px[i] >> s) & 0xFF)) <= 1);
  o = runOn("hue-saturation", img, "saturation=0");
  CHECK(std::abs(int(o.px[1] & 0xFF) - int((o.px[1] >> 8) & 0xFF)) <= 2);
  o = runOn("sepia", img);
  CHECK(o.px[0] == BLACK && (o.px[3] & 0xFF) > ((o.px[3] >> 16) & 0xFF));
  // Auto-level stretches a dull ramp to the full range
  Image dull(100, 1);
  for(int i = 0; i < 100; i++) dull.px[i] = rgb(100 + i / 2, 100 + i / 2, 100 + i / 2);
  o = runOn("auto-level", dull);
  CHECK((o.px[0] & 0xFF) < 5 && (o.px[99] & 0xFF) > 250);
}

static void testFilters() {
  // Opaque red left half, transparent right half: the blurred edge stays red, it only fades out
  Image img(40, 10);
  for(int y = 0; y < 10; y++) for(int x = 0; x < 20; x++) img.at(x, y) = rgb(255, 0, 0);
  Image o = runOn("gaussian-blur", img, "radius=6");
  uint32_t edge = o.at(20, 5);
  CHECK((edge >> 24) > 40 && (edge >> 24) < 215 && (edge & 0xFF) == 255 && ((edge >> 8) & 0xFF) == 0);
  CHECK((o.at(19, 5) >> 24) > (o.at(21, 5) >> 24));
  o = runOn("pixelate", img, "size=10");
  CHECK(o.at(0, 0) == o.at(9, 9) && o.at(15, 0) == rgb(255, 0, 0));
  o = runOn("drop-shadow", img, "x=3,y=0,opacity=100");
  CHECK(o.at(21, 5) == BLACK && o.at(10, 5) == rgb(255, 0, 0) && o.at(30, 5) == 0);
  Image n1 = runOn("add-noise", img), n2 = runOn("add-noise", img);
  CHECK(n1.px == n2.px && n1.px != img.px);
  o = runOn("rotate-zoom", img);
  CHECK(o.at(5, 5) == rgb(255, 0, 0) && o.at(35, 5) == 0);
  o = runOn("rotate-zoom", img, "angle=180");
  CHECK(o.at(35, 5) == rgb(255, 0, 0) && o.at(5, 5) == 0);
  o = runOn("sharpen", img);
  CHECK(o.at(5, 5) == rgb(255, 0, 0));
}

static void testDither(const std::filesystem::path& dir) {
  // 50% gray between black and white, matched in sRGB
  Image img(64, 64, rgb(128, 128, 128));
  EffectEnv env;
  env.primary = BLACK; env.secondary = WHITE;
  auto blacks = [](const Image& o) { int n = 0; for(uint32_t c : o.px) n += c == BLACK; return n; };
  Image o = runOn("dither", img, "palette=3,space=1,method=1,amount=100", env);
  CHECK(blacks(o) > 64 * 64 * 4 / 10 && blacks(o) < 64 * 64 * 6 / 10);
  for(uint32_t c : o.px) CHECK(c == BLACK || c == WHITE);
  o = runOn("dither", img, "palette=3,space=1,method=1,amount=0", env);
  CHECK(blacks(o) == 64 * 64 || blacks(o) == 0);
  o = runOn("dither", img, "palette=3,space=1,method=5", env);
  CHECK(blacks(o) > 64 * 64 * 4 / 10 && blacks(o) < 64 * 64 * 6 / 10);
  // Band 0: flat, like posterize
  o = runOn("dither", img, "palette=3,space=1,method=3,band=0", env);
  CHECK(blacks(o) == 64 * 64 || blacks(o) == 0);
  // From image: an image already in 4 colors comes back unchanged
  Image four(8, 8);
  uint32_t cs[] = { rgb(255, 0, 0), rgb(0, 0, 255), rgb(20, 20, 20), rgb(240, 240, 0) };
  for(int i = 0; i < 64; i++) four.px[i] = cs[i % 4];
  o = runOn("dither", four, "palette=2,colors=4", env);
  CHECK(o.px == four.px);

  // Dither Blur: a black/white edge gets a mixed band, but only the two source colors; a soft edge dithers its alpha
  Image edge(32, 8, WHITE);
  for(int y = 0; y < 8; y++) for(int x = 0; x < 16; x++) edge.at(x, y) = BLACK;
  o = runOn("dither-blur", edge, "radius=6", env);
  for(uint32_t c : o.px) CHECK(c == BLACK || c == WHITE);
  int mixed = 0;
  for(int x = 0; x < 32; x++) { int b = 0; for(int y = 0; y < 8; y++) b += o.at(x, y) == BLACK; mixed += b > 0 && b < 8; }
  CHECK(mixed >= 3 && o.at(0, 0) == BLACK && o.at(31, 0) == WHITE);
  Image dot(16, 16);
  for(int y = 4; y < 12; y++) for(int x = 4; x < 12; x++) dot.at(x, y) = rgb(255, 0, 0);
  o = runOn("dither-blur", dot, "radius=4", env);
  for(uint32_t c : o.px) CHECK(c == 0 || c == rgb(255, 0, 0));

  // Patterns: Bayer built-ins, a JSON matrix, a CSV, a PNG and an Aseprite-style pack
  const Pattern* b4 = findPattern("bayer4");
  CHECK(b4 && b4->w == 4 && std::abs(b4->at(0, 0) - 0.5f / 16) < 1e-6f && std::abs(b4->at(1, 1) - 4.5f / 16) < 1e-6f);
  std::vector<Pattern> got;
  std::string err;
  { std::ofstream(dir / "m.json") << "{ \"matrix\": [[0, 2], [3, 1]] }"; }
  CHECK(loadPatternFile(pathToUtf8(dir / "m.json"), got, err) && got.back().w == 2 && std::abs(got.back().at(1, 1) - 1.5f / 4) < 1e-6f);
  { std::ofstream(dir / "m.csv") << "0,1,2\n3,4,5\n"; }
  CHECK(loadPatternFile(pathToUtf8(dir / "m.csv"), got, err) && got.back().w == 3 && got.back().h == 2);
  auto pdoc = Document::blank(2, 2, BLACK);
  pdoc->active().set(1, 0, rgb(200, 200, 200));
  pdoc->active().set(0, 1, rgb(100, 100, 100));
  pdoc->active().set(1, 1, WHITE);
  CHECK(png::save(*pdoc, pathToUtf8(dir / "pat.png"), err));
  CHECK(loadPatternFile(pathToUtf8(dir / "pat.png"), got, err) && std::abs(got.back().at(0, 1) - 1.5f / 4) < 1e-6f && std::abs(got.back().at(1, 1) - 3.5f / 4) < 1e-6f);
  auto pngBytes = readBytes(dir / "pat.png");
  std::vector<zip::Entry> pack = {
    { "package.json", {} },
    { "matrices/pat.png", std::vector<uint8_t>(pngBytes.begin(), pngBytes.end()) },
  };
  std::string pkg = R"({ "name": "test", "contributes": { "ditheringMatrices": [ { "id": "test-2x2", "name": "Test 2x2", "path": "./matrices/pat.png" } ] } })";
  pack[0].data.assign(pkg.begin(), pkg.end());
  CHECK(zip::writeFile(pathToUtf8(dir / "pack.aseprite-extension"), pack, err));
  std::vector<zip::Entry> back;
  CHECK(zip::readFile(pathToUtf8(dir / "pack.aseprite-extension"), back, err) && back.size() == 2 && back[1].data == pack[1].data);
  got.clear();
  CHECK(loadPatternFile(pathToUtf8(dir / "pack.aseprite-extension"), got, err) && got.size() == 1 && got[0].id == "test-2x2" && got[0].name == "Test 2x2");
  addPattern(got[0]);
  o = runOn("dither", img, "palette=3,space=1,method=4,pattern=test-2x2", env);
  CHECK(blacks(o) == 64 * 64 / 2);
  // A compressible entry is deflated and still reads back
  std::vector<zip::Entry> big = { { "a.txt", std::vector<uint8_t>(5000, 'a') } };
  auto bytes = zip::write(big);
  CHECK(bytes.size() < 1000 && zip::read(bytes, back = {}, err) && back[0].data == big[0].data);
}

static void testAnimation(const std::filesystem::path& dir) {
  const uint32_t R = rgb(255, 0, 0), G = rgb(0, 255, 0), B = rgb(0, 0, 255);
  // Frames: a new empty frame, painting on it leaves frame 1 alone, undo goes back to the frame it was made on
  auto doc = Document::blank(300, 20, WHITE);
  Document& d = *doc;
  addFrame(d, 0, ENewFrame::Empty, "");
  CHECK(d.frameCount() == 2 && d.activeFrame == 1 && d.active().frames() == 2 && d.active().celEmpty(1));
  {
    TileRecorder rec(d.active());
    rec.touchPixel(290, 5);
    d.active().set(290, 5, R);
    d.history.push(d, rec.finish("paint", ""));
  }
  CHECK(d.active().getAt(1, 290, 5) == R && d.active().getAt(0, 290, 5) == WHITE);
  d.setActiveFrame(0);
  d.history.undo(d);
  CHECK(d.activeFrame == 1 && d.active().getAt(1, 290, 5) == 0 && d.active().getAt(0, 290, 5) == WHITE);
  d.history.redo(d);
  CHECK(d.active().getAt(1, 290, 5) == R);
  // Duplicate copies, linked shares, unlink splits
  addFrame(d, 1, ENewFrame::Duplicate, "");
  CHECK(d.frameCount() == 3 && d.active().getAt(2, 290, 5) == R && !d.active().linked(1, 2));
  addFrame(d, 2, ENewFrame::Linked, "");
  CHECK(d.frameCount() == 4 && d.active().linked(2, 3));
  d.active().set(10, 10, G);
  CHECK(d.active().getAt(2, 10, 10) == G);
  unlinkCel(d, 0, 3, "");
  d.active().set(11, 10, B);
  CHECK(!d.active().linked(2, 3) && d.active().getAt(3, 11, 10) == B && d.active().getAt(2, 11, 10) == 0);
  d.history.undo(d);
  CHECK(d.active().linked(2, 3));
  // A layer added to an animation gets a cel per frame, its image on the active frame
  auto top = std::make_unique<Layer>(300, 20, "Top");
  top->set(1, 1, B);
  d.setActiveFrame(2);
  addLayer(d, std::move(top), 1, "Add", "");
  CHECK(d.layers[1]->frames() == 4 && d.layers[1]->getAt(2, 1, 1) == B && d.layers[1]->celEmpty(0));
  // Tags follow inserted and deleted frames; moving and reversing keep the cels with their frames
  setTags(d, { FrameTag{ "walk", 1, 3 } }, "tag", "");
  addFrame(d, 0, ENewFrame::Empty, "");
  CHECK(d.tags[0].from == 2 && d.tags[0].to == 4);
  deleteFrames(d, 1, 1, "");
  CHECK(d.frameCount() == 4 && d.tags[0].from == 1 && d.tags[0].to == 3);
  uint32_t at1 = d.layers[0]->getAt(1, 290, 5);
  moveFrames(d, 1, 1, 3, "");
  CHECK(d.layers[0]->getAt(3, 290, 5) == at1);
  d.history.undo(d);
  CHECK(d.layers[0]->getAt(1, 290, 5) == at1);
  reverseFrames(d, 0, 3, "");
  CHECK(d.layers[0]->getAt(2, 290, 5) == at1);
  d.history.undo(d);
  // Merge down and resize work on every frame
  int frames = d.frameCount();
  mergeDown(d, 1, "");
  CHECK(d.layers.size() == 1 && d.layers[0]->frames() == frames && d.layers[0]->getAt(2, 1, 1) == B && d.layers[0]->getAt(0, 1, 1) == WHITE);
  d.history.undo(d);
  CHECK(d.layers.size() == 2 && d.layers[1]->getAt(2, 1, 1) == B);
  resizeImage(d, 600, 40, 96, EResample::Nearest, false, "");
  CHECK(d.layers[1]->getAt(2, 2, 2) == B && d.layers[0]->getAt(1, 580, 10) == R && d.layers[0]->getAt(0, 580, 10) == WHITE);
  d.history.undo(d);
  // Durations and playback
  setFrameRate(d, 10, "");
  setFrameDuration(d, 1, 1, 250, "");
  CHECK(d.frameMs(0) == 100 && d.frameMs(1) == 250);
  d.setActiveFrame(0);
  Player p;
  p.start(d, nullptr);
  CHECK(p.advance(d, 50, ELoop::Loop, nullptr) == -1 && p.advance(d, 60, ELoop::Loop, nullptr) == 1);
  CHECK(p.advance(d, 250, ELoop::Loop, nullptr) == 2);
  FrameTag pp{ "pp", 0, 2, ETagDirection::PingPong };
  auto order = playOrder(d, &pp);
  CHECK((order == std::vector<int>{ 0, 1, 2, 1 }));
  Player once;
  once.start(d, nullptr, 3);
  CHECK(once.advance(d, 1000, ELoop::Once, nullptr) == 3 && !once.playing());

  // .david keeps frames, cels, links, durations and tags
  addFrame(d, 3, ENewFrame::Linked, "");
  std::string err;
  CHECK(saveImage(d, pathToUtf8(dir / "anim.david"), err));
  auto back = loadImage(pathToUtf8(dir / "anim.david"), err);
  CHECK(back && back->frameCount() == d.frameCount() && back->layers.size() == 2 && back->fps == 10 && back->frames[1].duration == 250);
  CHECK(back && back->tags.size() == 1 && back->tags[0].name == "walk" && back->layers[0]->linked(3, 4));
  bool same = back != nullptr;
  for(int f = 0; same && f < d.frameCount(); f++)
    for(int y = 0; y < 20; y += 3) for(int x = 0; x < 300; x += 7) same &= back->layers[0]->getAt(f, x, y) == d.layers[0]->getAt(f, x, y) && back->layers[1]->getAt(f, x, y) == d.layers[1]->getAt(f, x, y);
  CHECK(same);

  // GIF, APNG and WebP keep the frames and their timing
  auto small = Document::blank(6, 4, 0);
  Document& s = *small;
  s.active().set(0, 0, R);
  addFrame(s, 0, ENewFrame::Empty, "");
  s.active().set(1, 1, G);
  addFrame(s, 1, ENewFrame::Empty, "");
  s.active().set(2, 2, B);
  setFrameDuration(s, 2, 2, 300, "");
  setFrameRate(s, 10, "");  // GIF keeps 1/100 s
  for(const char* ext : { "gif", "png", "webp" }) {
    SaveOptions o;
    o.quality = 100;
    auto bytes = saveImageBytes(s, ext, err, o);
    auto a = loadImageBytes(bytes, ext, err);
    CHECK(a && a->frameCount() == 3);
    if(!a) continue;
    CHECK(a->layers[0]->getAt(0, 0, 0) == R && a->layers[0]->getAt(1, 1, 1) == G && a->layers[0]->getAt(2, 2, 2) == B && (a->layers[0]->getAt(1, 0, 0) >> 24) == 0);
    CHECK(a->frameMs(2) == 300 && a->frameMs(0) == s.frameMs(0));
  }
  // Scaled export
  SaveOptions big;
  big.scale = 3;
  auto scaled = loadImageBytes(saveImageBytes(s, "gif", err, big), "gif", err);
  CHECK(scaled && scaled->width() == 18 && scaled->layers[0]->getAt(2, 8, 8) == B);

  // Sprite sheet out and back
  std::string json;
  SheetOptions so;
  so.columns = 2; so.padding = 1;
  auto sheet = sheetFromDocument(s, so, "s.png", json);
  CHECK(sheet->width() == 2 * 6 + 3 && sheet->height() == 2 * 4 + 3 && json.find("\"duration\": 300") != std::string::npos);
  SliceOptions sl;
  sl.frameW = 6; sl.frameH = 4; sl.padding = 1; sl.offsetX = 1; sl.offsetY = 1;
  auto cut = documentFromSheet(*sheet, sl, err);
  CHECK(cut && cut->frameCount() == 3 && cut->layers[0]->getAt(2, 2, 2) == B && cut->layers[0]->getAt(0, 0, 0) == R);

  // Layers to frames and back
  auto lay = Document::blank(4, 4, R);
  auto l2 = std::make_unique<Layer>(4, 4, "b");
  l2->fill(G);
  addLayer(*lay, std::move(l2), 1, "", "");
  layersToFrames(*lay, "");
  CHECK(lay->layers.size() == 1 && lay->frameCount() == 2 && lay->layers[0]->getAt(1, 0, 0) == G);
  framesToLayers(*lay, "");
  CHECK(lay->layers.size() == 2 && lay->frameCount() == 1 && lay->layers[1]->get(0, 0) == G);
  lay->history.undo(*lay);
  lay->history.undo(*lay);
  CHECK(lay->layers.size() == 2 && lay->frameCount() == 1 && lay->layers[0]->get(0, 0) == R);
}

static void testFormats(const std::filesystem::path& dir) {
  // A layered image: background, a Multiply layer at half opacity, a hidden layer
  auto doc = Document::blank(70, 40, rgb(200, 100, 50));
  auto mid = std::make_unique<Layer>(70, 40, "Mid & <layer>");
  for(int y = 5; y < 30; y++) for(int x = 10; x < 60; x++) mid->set(x, y, rgb(10, 20, 250, 200));
  mid->blend = EBlend::Multiply;
  mid->opacity = 128;
  doc->layers.push_back(std::move(mid));
  auto hidden = std::make_unique<Layer>(70, 40, "Hidden");
  hidden->set(1, 1, WHITE);
  hidden->visible = false;
  hidden->blend = EBlend::Reflect;
  doc->layers.push_back(std::move(hidden));
  doc->activeLayer = 1;
  doc->dpi = 300;
  doc->palette = { rgb(1, 2, 3), rgb(250, 251, 252) };
  std::string err;
  for(const char* ext : { "david", "ora" }) {
    auto path = pathToUtf8(dir / (std::string("layers.") + ext));
    CHECK(saveImage(*doc, path, err));
    auto back = loadImage(path, err);
    CHECK(back != nullptr);
    if(!back) continue;
    CHECK(back->width() == 70 && back->height() == 40 && back->layers.size() == 3 && back->activeLayer == 1 && std::abs(back->dpi - 300) < 0.01);
    CHECK(back->layers[1]->name == "Mid & <layer>" && back->layers[1]->blend == EBlend::Multiply && back->layers[1]->opacity == 128);
    CHECK(!back->layers[2]->visible && back->layers[2]->blend == EBlend::Reflect && back->layers[2]->get(1, 1) == WHITE);
    CHECK(samePixels(*doc, *back));
    if(std::string(ext) == "david") CHECK(back->palette == doc->palette);
  }
  // OpenRaster's mimetype is the first entry, stored
  std::vector<zip::Entry> ora;
  CHECK(zip::readFile(pathToUtf8(dir / "layers.ora"), ora, err) && ora[0].name == "mimetype" && std::string(ora[0].data.begin(), ora[0].data.end()) == "image/openraster");
  auto oraBytes = readBytes(dir / "layers.ora");
  CHECK(oraBytes.size() > 38 && std::string(oraBytes.data() + 30, 8) == "mimetype");
  // Flat formats: lossless ones keep every pixel (and alpha), JPG comes back close
  auto flat = Document::blank(33, 17, CLEAR);
  for(int y = 0; y < 17; y++) for(int x = 0; x < 33; x++) flat->active().set(x, y, rgb(x * 7, y * 15, (x * y) & 255, (x + y) % 3 ? 255 : 128));
  // (BMP from stb drops alpha, so it is checked on an opaque image below)
  for(const char* ext : { "png", "qoi", "tga" }) {
    auto bytes = saveImageBytes(*flat, ext, err, {});
    auto back = loadImageBytes(bytes, ext, err);
    CHECK(back && samePixels(*flat, *back));
  }
  auto webp = loadImageBytes(saveImageBytes(*flat, "webp", err, { 100 }), "", err);
  CHECK(webp && samePixels(*flat, *webp));
  auto opaque = Document::blank(32, 32, rgb(90, 160, 220));
  auto bmp = loadImageBytes(saveImageBytes(*opaque, "bmp", err, {}), "", err);
  CHECK(bmp && samePixels(*opaque, *bmp));
  auto jpg = loadImageBytes(saveImageBytes(*opaque, "jpg", err, { 95 }), "", err);
  CHECK(jpg && std::abs(int(jpg->active().get(16, 16) & 0xFF) - 90) < 6);
  CHECK(formatFor("x.JPEG") && formatFor("x.JPEG")->ext == "jpg" && !formatFor("x.unknown"));
}

int main(int argc, char** argv) {
  auto dir = std::filesystem::temp_directory_path() / "david-tests";
  std::filesystem::create_directories(dir);
  testUndoAcrossTiles();
  testSparseUndo();
  testHistoryCap();
  testBlend();
  testPngRoundTrip(dir);
  testStrokeNoBuildup();
  testReplaceInk();
  testSelectionClip();
  testMaskOps();
  testFlood();
  testLayersUndo();
  testMove();
  testOklabFlood();
  testGradient();
  testDraw();
  testText();
  testResize();
  testPalette(dir);
  testStrokeSpace();
  testOutline();
  testAutotile();
  testBlendModes();
  testAdjustments();
  testFilters();
  testDither(dir);
  testFormats(dir);
  testAnimation(dir);
  for(int i = 1; i < argc; i++) testExternal(argv[i], dir);
  std::printf(s_failed ? "%d FAILED\n" : "all passed\n", s_failed);
  return s_failed ? 1 : 0;
}
