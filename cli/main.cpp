// david-cli: the headless command line. Data goes to stdout, messages to stderr.
// Exit codes: 0 ok, 1 bad arguments, 2 I/O, 3 effect or script error.
#include "core/Blend.h"
#include "core/Document.h"
#include "core/Effect.h"
#include "core/Palette.h"
#include "core/Pattern.h"
#include "core/Resize.h"
#include "core/TileEdit.h"
#include "core/io/Anim.h"
#include "core/io/Formats.h"
#include "core/io/Png.h"
#include "core/io/Svg.h"
#include "core/Layer.h"
#include "core/Vector.h"
#include "platform/Paths.h"
#include "render/ShaderRunner.h"
#include "scripting/Mods.h"
#include <CLI/CLI.hpp>
#include <SDL3/SDL.h>
#include <glad/glad.h>
#include <nlohmann/json.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <regex>
#include <sstream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using namespace david;
using json = nlohmann::json;

enum { OK = 0, BAD_ARGS = 1, IO = 2, EFFECT = 3 };

namespace {
#pragma region [ GL for shader mods ]
SDL_Window* s_window = nullptr;
SDL_GLContext s_gl = nullptr;

// A hidden window's GL context, made the first time a shader mod runs
bool ensureGl() {
  if(s_gl) return true;
  static bool tried = false;
  if(tried) return false;
  tried = true;
  if(!SDL_Init(SDL_INIT_VIDEO)) return false;
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  s_window = SDL_CreateWindow("david-cli", 16, 16, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
  if(!s_window) return false;
  s_gl = SDL_GL_CreateContext(s_window);
  if(!s_gl || !gladLoadGLLoader(reinterpret_cast<GLADloadproc>(SDL_GL_GetProcAddress))) return false;
  return true;
}

void closeGl() {
  if(!s_gl) return;
  releaseShaders();
  SDL_GL_DestroyContext(s_gl);
  SDL_DestroyWindow(s_window);
  SDL_Quit();
}
#pragma endregion

#pragma region [ Files ]
std::vector<uint8_t> readStdin() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  return { std::istreambuf_iterator<char>(std::cin), {} };
}

bool writeStdout(const std::vector<uint8_t>& bytes) {
#ifdef _WIN32
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  return std::fwrite(bytes.data(), 1, bytes.size(), stdout) == bytes.size() && std::fflush(stdout) == 0;
}

// "sprites/*.png" -> the matching files (wildcards * and ? in the file name only)
std::vector<std::string> glob(const std::string& pattern) {
  auto p = pathFromUtf8(pattern);
  auto dir = p.has_parent_path() ? p.parent_path() : std::filesystem::path(".");
  std::string name = pathToUtf8(p.filename()), rx;
  for(char c : name) {
    if(c == '*') rx += ".*";
    else if(c == '?') rx += '.';
    else if(std::string("\\^$.|+()[]{}").find(c) != std::string::npos) { rx += '\\'; rx += c; }
    else rx += c;
  }
  std::regex re(rx, std::regex::icase);
  std::vector<std::string> out;
  std::error_code ec;
  for(auto& e : std::filesystem::directory_iterator(dir, ec))
    if(e.is_regular_file() && std::regex_match(pathToUtf8(e.path().filename()), re)) out.push_back(pathToUtf8(e.path()));
  std::sort(out.begin(), out.end());
  return out;
}
#pragma endregion

#pragma region [ Listing ]
const char* typeName(EParam t) {
  switch(t) {
    case EParam::Float: return "float";
    case EParam::Int: return "int";
    case EParam::Bool: return "bool";
    case EParam::Choice: return "choice";
    case EParam::Color: return "color";
    case EParam::Text: return "text";
    default: return "curve";
  }
}

json effectJson(const Effect& e) {
  json params = json::array();
  for(auto& p : e.params) {
    json j = { { "id", p.id }, { "label", p.label }, { "type", typeName(p.type) } };
    if(p.type == EParam::Float || p.type == EParam::Int) { j["min"] = p.min; j["max"] = p.max; j["default"] = p.def; }
    else if(p.type == EParam::Choice) { j["choices"] = p.choices; j["default"] = p.choices.empty() ? "" : p.choices[size_t(p.def)]; }
    else if(p.type == EParam::Bool) j["default"] = p.def != 0;
    else if(p.type == EParam::Color) { char h[10]; uint32_t c = uint32_t(p.def); std::snprintf(h, sizeof h, "#%02x%02x%02x%02x", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF, c >> 24); j["default"] = h; }
    else j["default"] = p.text;
    params.push_back(j);
  }
  json j = { { "id", e.id }, { "name", e.name }, { "category", e.category }, { "params", params } };
  if(!e.owner.empty()) j["mod"] = e.owner;
  return j;
}

json infoJson(const Document& doc, const std::string& file) {
  json layers = json::array();
  for(auto& l : doc.layers) layers.push_back({ { "name", l->name }, { "visible", l->visible }, { "opacity", l->opacity }, { "blend", blendId(l->blend) } });
  json pal = json::array();
  for(uint32_t c : doc.palette) { char h[8]; std::snprintf(h, sizeof h, "#%02x%02x%02x", c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF); pal.push_back(h); }
  const FileFormat* f = formatFor(file);
  json frames = json::array();
  for(int i = 0; i < doc.frameCount(); i++) frames.push_back({ { "duration", doc.frameMs(i) }, { "own", doc.frames[i].duration > 0 } });
  json tags = json::array();
  const char* dirs[] = { "forward", "reverse", "pingpong" };
  for(auto& t : doc.tags) tags.push_back({ { "name", t.name }, { "from", t.from + 1 }, { "to", t.to + 1 }, { "direction", dirs[int(t.direction)] } });
  return { { "file", file }, { "format", f ? f->ext : "" }, { "width", doc.width() }, { "height", doc.height() }, { "dpi", doc.dpi },
           { "layers", layers }, { "activeLayer", doc.activeLayer }, { "palette", pal },
           { "fps", doc.fps }, { "frames", frames }, { "tags", tags } };
}
#pragma endregion

struct Job {
  std::vector<std::string> effects, args;
  std::string script, resize, filter = "best", format;
  int quality = 90;
  // Animation
  int frame = 0;            // 1-based frame the effects and the script act on; 0 = the file's active frame
  bool allFrames = false;   // effects on every frame
  double fps = 0;
  std::string tag, sheet, slice;
  int scale = 1, loops = 0;
  std::string trace;        // "pixel": the active layer traced into a vector layer, after the script
  EffectEnv env;
  ModHost* mods = nullptr;
};

// --slice WxH[,pad=P,x=X,y=Y,count=N]: the input cut into frames
int sliceInput(std::unique_ptr<Document>& doc, const Job& job, std::string& error) {
  if(job.slice.empty()) return OK;
  SliceOptions o;
  std::stringstream ss(job.slice);
  std::string part;
  bool first = true;
  while(std::getline(ss, part, ',')) {
    if(first) {
      first = false;
      if(std::sscanf(part.c_str(), "%dx%d", &o.frameW, &o.frameH) != 2) { error = "bad --slice '" + job.slice + "' (use 16x16,pad=1)"; return BAD_ARGS; }
      continue;
    }
    size_t eq = part.find('=');
    std::string k = part.substr(0, eq);
    int v = eq == std::string::npos ? 0 : std::atoi(part.c_str() + eq + 1);
    if(k == "pad") o.padding = v; else if(k == "x") o.offsetX = v; else if(k == "y") o.offsetY = v; else if(k == "count") o.count = v;
    else { error = "unknown --slice option '" + k + "'"; return BAD_ARGS; }
  }
  auto frames = documentFromSheet(*doc, o, error);
  if(!frames) return BAD_ARGS;
  doc = std::move(frames);
  return OK;
}

// The tag index for --tag; -1 when none was asked for
int tagIndex(const Document& doc, const Job& job, std::string& error) {
  if(job.tag.empty()) return -1;
  for(int i = 0; i < int(doc.tags.size()); i++) if(doc.tags[i].name == job.tag) return i;
  error = "no tag '" + job.tag + "'";
  return -2;
}

// Writes the result: an animation (GIF, APNG, WebP), or with --sheet a sprite sheet PNG (and its JSON)
bool writeResult(const Document& doc, const Job& job, const std::string& path, std::string& error) {
  int tag = tagIndex(doc, job, error);
  if(tag == -2) return false;
  if(!job.sheet.empty()) {
    SheetOptions o;
    o.scale = job.scale; o.tag = tag;
    bool withJson = false;
    std::stringstream ss(job.sheet);
    std::string part;
    while(std::getline(ss, part, ',')) {
      size_t eq = part.find('=');
      std::string k = part.substr(0, eq);
      int v = eq == std::string::npos ? 0 : std::atoi(part.c_str() + eq + 1);
      if(k == "cols") o.columns = v; else if(k == "pad") o.padding = v; else if(k == "json") withJson = true;
      else if(k != "yes") { error = "unknown --sheet option '" + k + "'"; return false; }
    }
    std::string json, name = pathToUtf8(pathFromUtf8(path).filename());
    auto sheet = sheetFromDocument(doc, o, name, json);
    if(!png::save(*sheet, path, error)) return false;
    if(withJson) return writeFileAtomic(pathToUtf8(pathFromUtf8(path).replace_extension(".json")), std::vector<uint8_t>(json.begin(), json.end()), error);
    return true;
  }
  SaveOptions opts{ job.quality };
  opts.tag = tag; opts.scale = job.scale; opts.loops = job.loops;
  return saveImage(doc, path, error, opts);
}

// Runs the steps on one image: resize, then the effects in order, then the script
int process(Document& doc, const Job& job, std::string& error) {
  if(job.frame > 0) {
    if(job.frame > doc.frameCount()) { error = "no frame " + std::to_string(job.frame) + " (the image has " + std::to_string(doc.frameCount()) + ")"; return BAD_ARGS; }
    doc.setActiveFrame(job.frame - 1);
  }
  if(job.fps > 0) doc.fps = std::clamp(job.fps, 1.0, 60.0);
  if(!job.resize.empty()) {
    int w = doc.width(), h = doc.height();
    const std::string& r = job.resize;
    if(!r.empty() && r.back() == '%') {
      double k = std::atof(r.c_str()) / 100;
      w = std::max(1, int(std::lround(w * k))); h = std::max(1, int(std::lround(h * k)));
    } else if(std::sscanf(r.c_str(), "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) { error = "bad --resize '" + r + "' (use 200% or 64x64)"; return BAD_ARGS; }
    static const std::map<std::string, EResample> modes = { { "best", EResample::Best }, { "bicubic", EResample::Bicubic }, { "bilinear", EResample::Bilinear },
                                                            { "nearest", EResample::Nearest }, { "supersample", EResample::Supersample } };
    auto m = modes.find(job.filter);
    if(m == modes.end()) { error = "bad --filter '" + job.filter + "'"; return BAD_ARGS; }
    resizeImage(doc, w, h, doc.dpi, m->second, true, "");
  }
  for(auto& spec : job.effects) {
    size_t colon = spec.find(':');
    std::string id = spec.substr(0, colon);
    const Effect* e = findEffect(id);
    if(!e) { error = "no effect '" + id + "' (see --list-effects)"; return BAD_ARGS; }
    ParamValues v = defaultValues(*e);
    if(colon != std::string::npos && !parseValues(*e, spec.substr(colon + 1), v, error)) return BAD_ARGS;
    try {
      // --all-frames: every frame (a cel linked to one already done is skipped)
      int here = doc.activeFrame;
      std::vector<int> frames = { here }, done;
      if(job.allFrames) for(int f = 0; f < doc.frameCount(); f++) if(f != here) frames.push_back(f);
      for(int f : frames) {
        bool shared = false;
        for(int g : done) shared |= doc.active().linked(f, g);
        if(shared) continue;
        done.push_back(f);
        doc.setActiveFrame(f);
        TileRecorder rec(doc.active());
        applyEffect(doc, rec, *e, v, job.env);
      }
      doc.setActiveFrame(here);
    } catch(const std::exception& ex) { error = id + ": " + ex.what(); return EFFECT; }
  }
  if(!job.script.empty()) {
    std::map<std::string, std::string> args;
    for(auto& a : job.args) {
      size_t eq = a.find('=');
      if(eq == std::string::npos) { error = "--arg needs name=value"; return BAD_ARGS; }
      args[a.substr(0, eq)] = a.substr(eq + 1);
    }
    if(!job.mods->runScript(job.script, doc, args, job.env, error)) return EFFECT;
  }
  if(!job.trace.empty()) {
    if(job.trace != "pixel") { error = "bad --trace '" + job.trace + "' (use pixel)"; return BAD_ARGS; }
    // The traced objects replace the layer, so an SVG output holds just them
    Layer& src = doc.active();
    auto objects = tracePixels(src, src.frame());
    auto layer = std::make_unique<Layer>(doc.width(), doc.height(), src.name);
    layer->kind = ELayerKind::Vector;
    layer->fitFrames(doc.frameCount(), doc.activeFrame);
    VecTree& t = vectorTree(*layer, doc.activeFrame);
    t.objects = std::move(objects);
    t.touch();
    rasterizeVectorCel(*layer, doc.activeFrame);
    layer->setFrame(doc.activeFrame);
    doc.layers[doc.activeLayer] = std::move(layer);
  }
  return OK;
}
}

int main(int argc, char** argv) {
  CLI::App app{ "David image editor, headless.\n  david-cli in.png -o out.webp\n  david-cli icon.svg --resize 256x256 -o icon.png\n  david-cli sprite.png --trace pixel -o sprite.svg\n  david-cli in.png --effect gaussian-blur:radius=3 --effect outline:color=#000 -o out.png\n"
                "  david-cli in.png --script mods/x.lua --arg k=v -o -\n  david-cli --batch \"sprites/*.png\" --resize 200% --filter nearest --out-dir big/\n"
                "  david-cli info in.david --json\n  david-cli --list-effects --json", "david-cli" };
  app.set_version_flag("--version", std::string("david-cli ") + DAVID_VERSION + " (mod API 1)");
  std::string input, output, batch, outDir, paletteFile;
  bool listEffects = false, listFormats = false, asJson = false, allMods = false;
  std::vector<std::string> enableMods;
  Job job;
  app.add_option("input", input, "Image to read; - reads stdin");
  app.add_option("-o,--output", output, "Image to write; - writes stdout (PNG unless --format)");
  app.add_option("--effect", job.effects, "Effect to run, id:name=value,... (repeatable, runs in order)");
  app.add_option("--script", job.script, "Lua script to run on the image");
  app.add_option("--arg", job.args, "name=value for the script (repeatable)");
  app.add_option("--resize", job.resize, "New size: 200% or 64x64 (runs before the effects; SVG input renders sharp at the new size)");
  app.add_option("--trace", job.trace, "pixel: trace the active layer into vector shapes, one per color region (write .svg or .david)");
  app.add_option("--filter", job.filter, "Resampling: best, bicubic, bilinear, nearest, supersample");
  app.add_option("--format", job.format, "Output format when it can't come from the file name (png, webp, david, ...)");
  app.add_option("--quality", job.quality, "JPG / WebP quality 1-100 (WebP 100 = lossless)")->check(CLI::Range(1, 100));
  app.add_option("--batch", batch, "Input files by pattern, e.g. \"sprites/*.png\"");
  app.add_option("--out-dir", outDir, "Folder for --batch results");
  app.add_option("--palette", paletteFile, "Palette file for effects that use the current palette (dither)");
  app.add_option("--frame", job.frame, "Frame (1 = first) the effects and the script act on")->check(CLI::PositiveNumber);
  app.add_flag("--all-frames", job.allFrames, "Run the effects on every frame");
  app.add_option("--fps", job.fps, "Frame rate for frames without their own duration")->check(CLI::Range(1.0, 60.0));
  app.add_option("--tag", job.tag, "Only this tag's frames in an animated output (GIF, APNG, WebP, --sheet)");
  app.add_option("--scale", job.scale, "Integer upscale of animated outputs and sheets")->check(CLI::Range(1, 64));
  app.add_option("--loops", job.loops, "Loops of an animated output, 0 = forever")->check(CLI::Range(0, 65535));
  app.add_option("--sheet", job.sheet, "Write a sprite sheet PNG: yes, or cols=8,pad=1,json (JSON in Aseprite's format next to it)");
  app.add_option("--slice", job.slice, "Cut the input into frames: 16x16[,pad=1,x=0,y=0,count=8]");
  app.add_option("--mod", enableMods, "Turn on a mod by id for this run (repeatable)");
  app.add_flag("--all-mods", allMods, "Turn on every mod, examples included");
  app.add_flag("--list-effects", listEffects, "List the effects and their params");
  app.add_flag("--list-formats", listFormats, "List the file formats");
  app.add_flag("--json", asJson, "JSON output for listings and info");
  CLI::App* info = app.add_subcommand("info", "Size, layers and palette of an image");
  std::string infoFile;
  info->add_option("file", infoFile, "Image to describe")->required();
  info->add_flag("--json", asJson, "JSON output");

  try { app.parse(argc, argv); }
  catch(const CLI::ParseError& e) { return app.exit(e) ? BAD_ARGS : OK; }

  // Patterns and mods, from the app folder and the user's config folder
  std::vector<std::string> errs;
  loadPatternDirs({ paths::exeDir() + "patterns", paths::configDir() + "patterns" }, errs);
  ModHost mods;
  mods.ensureGl = ensureGl;
  mods.scan({ paths::exeDir() + "mods", paths::configDir() + "mods" });
  std::map<std::string, bool> enabled;
  {
    std::ifstream in(pathFromUtf8(paths::configDir() + "settings.json"));
    auto j = json::parse(in, nullptr, false);
    if(j.is_object() && j.contains("mods") && j["mods"].is_object())
      for(auto& [k, v] : j["mods"].items()) if(v.is_boolean()) enabled[k] = v.get<bool>();
  }
  if(allMods) for(auto& m : mods.mods()) enabled[m.id] = true;
  for(auto& id : enableMods) enabled[id] = true;
  mods.apply(enabled);
  for(auto& m : mods.mods()) if(m.enabled && !m.error.empty()) std::fprintf(stderr, "mod %s: %s\n", m.id.c_str(), m.error.c_str());
  job.mods = &mods;
  job.env.primary = 0xFF000000u;
  job.env.secondary = 0xFFFFFFFFu;
  static Palette palette = Palette::defaults();
  if(!paletteFile.empty()) {
    std::string err;
    if(!palette.load(paletteFile, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return IO; }
  }
  job.env.palette = &palette;
  job.env.baseDir = pathToUtf8(std::filesystem::current_path()) + "/";

  int code = OK;
  auto done = [&](int c) { closeGl(); return c; };

  if(listEffects) {
    if(asJson) {
      json all = json::array();
      for(auto& e : effects()) all.push_back(effectJson(e));
      std::printf("%s\n", all.dump(2).c_str());
    } else {
      for(auto& e : effects()) {
        std::printf("%-22s %s (%s)\n", e.id.c_str(), e.name.c_str(), e.category.c_str());
        for(auto& p : e.params) {
          std::printf("    %-18s %s", p.id.c_str(), typeName(p.type));
          if(p.type == EParam::Float || p.type == EParam::Int) std::printf(" %g..%g = %g", p.min, p.max, p.def);
          if(p.type == EParam::Choice) { std::printf(" ="); for(size_t i = 0; i < p.choices.size(); i++) std::printf("%s%s", i ? " | " : " ", p.choices[i].c_str()); }
          std::printf("\n");
        }
      }
    }
    return done(OK);
  }
  if(listFormats) {
    json all = json::array();
    for(auto& f : formats()) {
      if(asJson) all.push_back({ { "ext", f.ext }, { "name", f.name }, { "load", bool(f.load) }, { "save", bool(f.save) }, { "layered", f.layered } });
      else std::printf("%-6s %-12s %s%s%s\n", f.ext.c_str(), f.name.c_str(), f.load ? "load " : "", f.save ? "save " : "", f.layered ? "layers" : "");
    }
    if(asJson) std::printf("%s\n", all.dump(2).c_str());
    return done(OK);
  }

  if(*info) {
    std::string err;
    auto doc = infoFile == "-" ? loadImageBytes(readStdin(), "", err) : loadImage(infoFile, err);
    if(!doc) { std::fprintf(stderr, "%s: %s\n", infoFile.c_str(), err.c_str()); return done(IO); }
    json j = infoJson(*doc, infoFile);
    if(asJson) std::printf("%s\n", j.dump(2).c_str());
    else {
      std::printf("%s: %d x %d, %g dpi, %d layer(s)", infoFile.c_str(), doc->width(), doc->height(), doc->dpi, int(doc->layers.size()));
      if(doc->frameCount() > 1) std::printf(", %d frames at %g fps, %d tag(s)", doc->frameCount(), doc->fps, int(doc->tags.size()));
      std::printf("\n");
      for(auto& l : j["layers"]) std::printf("  %s  opacity %d  %s%s\n", l["name"].get<std::string>().c_str(), l["opacity"].get<int>(), l["blend"].get<std::string>().c_str(), l["visible"].get<bool>() ? "" : "  (hidden)");
    }
    return done(OK);
  }

  // Batch: every match into --out-dir, same name, --format or the input's own extension
  if(!batch.empty()) {
    if(outDir.empty()) { std::fprintf(stderr, "--batch needs --out-dir\n"); return done(BAD_ARGS); }
    std::error_code ec;
    std::filesystem::create_directories(pathFromUtf8(outDir), ec);
    auto files = glob(batch);
    if(files.empty()) { std::fprintf(stderr, "no files match %s\n", batch.c_str()); return done(IO); }
    for(auto& f : files) {
      std::string err;
      auto doc = loadImage(f, err);
      if(!doc) { std::fprintf(stderr, "%s: %s\n", f.c_str(), err.c_str()); code = IO; continue; }
      if(int c = sliceInput(doc, job, err)) { std::fprintf(stderr, "%s: %s\n", f.c_str(), err.c_str()); return done(c); }
      if(int c = process(*doc, job, err)) { std::fprintf(stderr, "%s: %s\n", f.c_str(), err.c_str()); code = c; if(c == BAD_ARGS) return done(c); continue; }
      auto in = pathFromUtf8(f);
      std::string ext = job.format.empty() ? pathToUtf8(in.extension()).substr(1) : job.format;
      std::string out = pathToUtf8(pathFromUtf8(outDir) / in.stem()) + "." + ext;
      if(!writeResult(*doc, job, out, err)) { std::fprintf(stderr, "%s: %s\n", out.c_str(), err.c_str()); code = IO; }
    }
    return done(code);
  }

  if(input.empty()) { std::fprintf(stderr, "%s", app.help().c_str()); return done(BAD_ARGS); }
  std::string err;
  auto doc = input == "-" ? loadImageBytes(readStdin(), "", err) : loadImage(input, err);
  if(!doc) { std::fprintf(stderr, "%s: %s\n", input.c_str(), err.c_str()); return done(IO); }
  if(int c = sliceInput(doc, job, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return done(c); }
  if(int c = process(*doc, job, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return done(c); }
  if(output.empty()) return done(OK);
  SaveOptions opts{ job.quality };
  opts.scale = job.scale; opts.loops = job.loops;
  if((opts.tag = tagIndex(*doc, job, err)) == -2) { std::fprintf(stderr, "%s\n", err.c_str()); return done(BAD_ARGS); }
  if(output == "-") {
    auto bytes = saveImageBytes(*doc, job.format.empty() ? "png" : job.format, err, opts);
    if(bytes.empty() || !writeStdout(bytes)) { std::fprintf(stderr, "stdout: %s\n", err.c_str()); return done(IO); }
    return done(OK);
  }
  std::string target = output;
  if(!job.format.empty() && !formatFor(output)) target += "." + job.format;
  if(!writeResult(*doc, job, target, err)) { std::fprintf(stderr, "%s: %s\n", target.c_str(), err.c_str()); return done(IO); }
  return done(OK);
}
