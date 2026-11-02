// The Lua API (Lua 5.4 through sol2). Every //@api block documents one function or property; the modding
// guide's reference is generated from them by tools/gen-modding-docs.py.
#include "scripting/LuaApi.h"
#include "core/Document.h"
#include "core/Effect.h"
#include "core/Palette.h"
#include "core/io/Png.h"
#include "scripting/DocEditor.h"
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace david {

// Closures handed to David (effects, commands, tools) hold the runtime, so a Lua function never outlives its state
struct LuaRuntime : std::enable_shared_from_this<LuaRuntime> {
  sol::state lua;
  std::string mod;
  std::function<void(const std::string&)> log;
  LuaRegistrations regs;
  std::function<void(const std::string&, bool)> showWindow;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
};

namespace {
constexpr int CALL_SECONDS = 10;

// Aborts a call that runs too long (an endless loop in a mod would freeze the app)
void watchdog(lua_State* L, lua_Debug*) {
  auto* rt = static_cast<LuaRuntime*>(sol::state_view(L)["__david_rt"].get<void*>());
  if(rt && std::chrono::steady_clock::now() > rt->deadline) luaL_error(L, "the script ran longer than %d seconds", CALL_SECONDS);
}

// Calls fn with a time limit; a Lua error comes back as the message
template<class... A> sol::protected_function_result call(LuaRuntime& rt, const sol::protected_function& fn, A&&... args) {
  rt.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(CALL_SECONDS);
  auto r = fn(std::forward<A>(args)...);
  rt.deadline = std::chrono::steady_clock::time_point::max();
  return r;
}

std::string errorOf(const sol::protected_function_result& r) {
  sol::error e = r;
  return e.what();
}

// What an effect's run function sees: reads come from the source, writes go to the result
struct LImage { const Image* src; Image* dst; int ox, oy; };
// The document inside a command or script, with the colors and palette of the moment
struct LDoc { DocEditor* ed; EffectEnv env; };
struct LLayer {
  DocEditor* ed;
  uint64_t id;
  int index() const { return ed->doc().indexOf(id); }
  Layer* layer() const { return ed->doc().findLayer(id); }
};

EParam paramType(const std::string& t) {
  if(t == "int") return EParam::Int;
  if(t == "bool") return EParam::Bool;
  if(t == "choice") return EParam::Choice;
  if(t == "color") return EParam::Color;
  if(t == "text") return EParam::Text;
  if(t == "curve") return EParam::Curve;
  return EParam::Float;
}

std::vector<EffectParam> readParams(sol::optional<sol::table> t) {
  std::vector<EffectParam> out;
  if(!t) return out;
  for(auto& [k, v] : *t) {
    if(!v.is<sol::table>()) continue;
    sol::table p = v;
    EffectParam e;
    e.id = p.get_or<std::string>("id", "");
    e.label = p.get_or<std::string>("label", e.id);
    e.type = paramType(p.get_or<std::string>("type", "float"));
    e.min = p.get_or("min", 0.0);
    e.max = p.get_or("max", e.type == EParam::Bool ? 1.0 : 100.0);
    if(e.type == EParam::Text || e.type == EParam::Curve) e.text = p.get_or<std::string>("default", "");
    else e.def = p.get_or("default", e.min);
    if(sol::optional<sol::table> ch = p["choices"]) {
      for(auto& [ck, cv] : *ch) if(cv.is<std::string>()) e.choices.push_back(cv.as<std::string>());
      e.max = double(std::max<size_t>(e.choices.size(), 1) - 1);
    }
    e.showIf = p.get_or<std::string>("showIf", "");
    if(!e.id.empty()) out.push_back(std::move(e));
  }
  return out;
}

sol::table valuesTable(sol::state_view L, const Effect& e, const ParamValues& v) {
  sol::table t = L.create_table();
  for(auto& p : e.params) {
    if(p.type == EParam::Text || p.type == EParam::Curve) t[p.id] = v.str(p.id);
    else if(p.type == EParam::Bool) t[p.id] = v.get(p.id) != 0;
    else if(p.type == EParam::Float) t[p.id] = v.get(p.id, p.def);
    else t[p.id] = lua_Integer(v.get(p.id, p.def));
  }
  return t;
}

void bindTypes(LuaRuntime& rt) {
  sol::state& L = rt.lua;
  L.new_usertype<LImage>("Image", sol::no_constructor,
    //@api lua Image.width
    //@sig img.width -> integer
    //@doc Width of the area the effect runs on (the selection's bounds plus the effect's margin).
    "width", sol::property([](const LImage& i) { return i.src->w; }),
    //@api lua Image.height
    //@sig img.height -> integer
    //@doc Height of the area.
    "height", sol::property([](const LImage& i) { return i.src->h; }),
    //@api lua Image.x
    //@sig img.x, img.y -> integer
    //@doc Image position of the area's top-left pixel, for patterns that must line up across the image.
    "x", sol::property([](const LImage& i) { return i.ox; }),
    "y", sol::property([](const LImage& i) { return i.oy; }),
    //@api lua Image:get
    //@sig img:get(x, y) -> color
    //@doc The pixel before the effect (0 outside the area). Coordinates are relative to the area, from 0.
    //@ex local c = img:get(x, y)
    "get", [](const LImage& i, int x, int y) -> lua_Integer { return i.src->contains(x, y) ? lua_Integer(i.src->at(x, y)) : 0; },
    //@api lua Image:set
    //@sig img:set(x, y, color)
    //@doc Writes a pixel of the result. The selection still clips what reaches the layer.
    //@ex img:set(x, y, david.rgba(255, 0, 0))
    "set", [](LImage& i, int x, int y, lua_Integer c) { if(i.dst->contains(x, y)) i.dst->at(x, y) = uint32_t(c); });

  L.new_usertype<LLayer>("Layer", sol::no_constructor,
    //@api lua Layer.name
    //@sig layer.name -> string (read and write)
    //@doc The layer's name. Changing it is part of the command's undo step.
    "name", sol::property([](const LLayer& l) { return l.layer() ? l.layer()->name : std::string(); },
                          [](LLayer& l, const std::string& n) { if(l.layer()) { auto p = LayerProps::of(*l.layer()); p.name = n; l.ed->setProps(l.index(), p); } }),
    //@api lua Layer.visible
    //@sig layer.visible -> boolean (read and write)
    "visible", sol::property([](const LLayer& l) { return l.layer() && l.layer()->visible; },
                             [](LLayer& l, bool v) { if(l.layer()) { auto p = LayerProps::of(*l.layer()); p.visible = v; l.ed->setProps(l.index(), p); } }),
    //@api lua Layer.opacity
    //@sig layer.opacity -> integer 0-255 (read and write)
    "opacity", sol::property([](const LLayer& l) { return l.layer() ? int(l.layer()->opacity) : 0; },
                             [](LLayer& l, int v) { if(l.layer()) { auto p = LayerProps::of(*l.layer()); p.opacity = uint8_t(std::clamp(v, 0, 255)); l.ed->setProps(l.index(), p); } }),
    //@api lua Layer.blend
    //@sig layer.blend -> string (read and write)
    //@doc The blend mode id: normal, multiply, additive, color-burn, color-dodge, reflect, glow, overlay, difference, negation, lighten, darken, screen, xor.
    //@ex layer.blend = "multiply"
    "blend", sol::property([](const LLayer& l) { return std::string(l.layer() ? blendId(l.layer()->blend) : "normal"); },
                           [](LLayer& l, const std::string& v) { if(l.layer()) { auto p = LayerProps::of(*l.layer()); p.blend = blendFromId(v.c_str()); l.ed->setProps(l.index(), p); } }),
    //@api lua Layer:get
    //@sig layer:get(x, y) -> color
    //@doc A pixel of the layer (0 outside the image).
    "get", [](const LLayer& l, int x, int y) -> lua_Integer { return l.ed->get(l.index(), x, y); },
    //@api lua Layer:set
    //@sig layer:set(x, y, color)
    //@doc Writes a pixel. It ignores the selection; check doc:selected(x, y) to respect it.
    //@ex if doc:selected(x, y) > 0 then layer:set(x, y, c) end
    "set", [](LLayer& l, int x, int y, lua_Integer c) { l.ed->set(l.index(), x, y, uint32_t(c)); });

  L.new_usertype<LDoc>("Document", sol::no_constructor,
    //@api lua Document.width
    //@sig doc.width, doc.height -> integer
    //@doc The image size in pixels.
    "width", sol::property([](const LDoc& d) { return d.ed->doc().width(); }),
    "height", sol::property([](const LDoc& d) { return d.ed->doc().height(); }),
    //@api lua Document.primary
    //@sig doc.primary, doc.secondary -> color
    //@doc The primary and secondary colors.
    "primary", sol::property([](const LDoc& d) -> lua_Integer { return d.env.primary; }),
    "secondary", sol::property([](const LDoc& d) -> lua_Integer { return d.env.secondary; }),
    //@api lua Document.palette
    //@sig doc.palette -> { color, ... }
    //@doc The current palette, in order (a copy; changing the table changes nothing).
    "palette", sol::property([](const LDoc& d, sol::this_state s) {
      sol::table t = sol::state_view(s).create_table();
      if(d.env.palette) for(size_t i = 0; i < d.env.palette->colors.size(); i++) t[i + 1] = lua_Integer(d.env.palette->colors[i]);
      return t;
    }),
    //@api lua Document.activeLayer
    //@sig doc.activeLayer -> integer
    //@doc Index of the active layer; 1 is the bottom layer.
    "activeLayer", sol::property([](const LDoc& d) { return d.ed->doc().activeLayer + 1; }),
    //@api lua Document:layerCount
    //@sig doc:layerCount() -> integer
    "layerCount", [](const LDoc& d) { return int(d.ed->doc().layers.size()); },
    //@api lua Document:layer
    //@sig doc:layer(index) -> Layer
    //@doc The layer at index (1 = bottom); without an index, the active layer.
    //@ex local top = doc:layer(doc:layerCount())
    "layer", [](LDoc& d, sol::optional<int> i) -> sol::optional<LLayer> {
      int idx = i ? *i - 1 : d.ed->doc().activeLayer;
      if(idx < 0 || idx >= int(d.ed->doc().layers.size())) return sol::nullopt;
      return LLayer{ d.ed, d.ed->doc().layers[idx]->id() };
    },
    //@api lua Document:addLayer
    //@sig doc:addLayer(name) -> Layer
    //@doc Adds an empty layer above the active one and makes it active.
    "addLayer", [](LDoc& d, const std::string& name) { int i = d.ed->addLayer(name); return LLayer{ d.ed, d.ed->doc().layers[i]->id() }; },
    //@api lua Document:deleteLayer
    //@sig doc:deleteLayer(index) -> boolean
    //@doc Deletes a layer; the last one can't be deleted.
    "deleteLayer", [](LDoc& d, int i) { return d.ed->deleteLayer(i - 1); },
    //@api lua Document:selected
    //@sig doc:selected(x, y) -> integer 0-255
    //@doc How much the pixel is selected; 255 everywhere when nothing is selected.
    "selected", [](const LDoc& d, int x, int y) { const Document& doc = d.ed->doc(); return x < 0 || y < 0 || x >= doc.width() || y >= doc.height() ? 0 : int(doc.clip(x, y)); },
    //@api lua Document:selectionBounds
    //@sig doc:selectionBounds() -> x, y, w, h | nil
    //@doc The selection's bounding box, or nil when nothing is selected.
    "selectionBounds", [](const LDoc& d) -> std::tuple<sol::optional<int>, sol::optional<int>, sol::optional<int>, sol::optional<int>> {
      if(!d.ed->doc().hasSelection()) return { sol::nullopt, sol::nullopt, sol::nullopt, sol::nullopt };
      IRect b = d.ed->doc().selection->bounds();
      return { b.x0, b.y0, b.width(), b.height() };
    },
    //@api lua Document:frameCount
    //@sig doc:frameCount() -> integer
    //@doc How many animation frames the document has (1 for a still image).
    "frameCount", [](const LDoc& d) { return d.ed->doc().frameCount(); },
    //@api lua Document.activeFrame
    //@sig doc.activeFrame -> integer (read and write)
    //@doc The frame layers are read and written on; 1 is the first. Setting it moves to that frame.
    //@ex for f = 1, doc:frameCount() do doc.activeFrame = f; doc:applyEffect("invert-colors") end
    "activeFrame", sol::property([](const LDoc& d) { return d.ed->doc().activeFrame + 1; },
                                 [](LDoc& d, int f) { d.ed->setFrame(f - 1); }),
    //@api lua Document.fps
    //@sig doc.fps -> number (read and write)
    //@doc Frames per second for the frames without their own duration (1-60).
    "fps", sol::property([](const LDoc& d) { return d.ed->doc().fps; },
                         [](LDoc& d, double v) { AnimState s = AnimState::of(d.ed->doc()); s.fps = std::clamp(v, 1.0, 60.0); d.ed->setAnim(s); }),
    //@api lua Document:frameDuration
    //@sig doc:frameDuration(frame) -> integer
    //@doc How long the frame shows, in ms (its own duration, or the frame rate's).
    "frameDuration", [](const LDoc& d, int f) { return d.ed->doc().frameMs(f - 1); },
    //@api lua Document:setFrameDuration
    //@sig doc:setFrameDuration(frame, ms)
    //@doc Gives the frame its own duration; 0 goes back to the frame rate.
    "setFrameDuration", [](LDoc& d, int f, int ms) {
      AnimState s = AnimState::of(d.ed->doc());
      if(f >= 1 && f <= int(s.frames.size())) { s.frames[f - 1].duration = std::clamp(ms, 0, 60000); d.ed->setAnim(s); }
    },
    //@api lua Document:addFrame
    //@sig doc:addFrame([kind]) -> integer
    //@doc Adds a frame after the active one and makes it active: "empty" (the default), "copy" or "linked" (shares the active frame's images). Returns its number.
    //@ex local f = doc:addFrame("copy")
    "addFrame", [](LDoc& d, sol::optional<std::string> kind) {
      ENewFrame k = !kind || *kind == "empty" ? ENewFrame::Empty : *kind == "linked" ? ENewFrame::Linked : ENewFrame::Duplicate;
      return d.ed->addFrame(k) + 1;
    },
    //@api lua Document:deleteFrame
    //@sig doc:deleteFrame(frame) -> boolean
    //@doc Deletes a frame; the last one can't be deleted.
    "deleteFrame", [](LDoc& d, int f) { return d.ed->deleteFrame(f - 1); },
    //@api lua Document.tags
    //@sig doc.tags -> { { name, from, to, direction }, ... }
    //@doc The frame tags (a copy). direction is "forward", "reverse" or "pingpong".
    "tags", sol::property([](const LDoc& d, sol::this_state s) {
      sol::state_view L(s);
      sol::table t = L.create_table();
      const char* dirs[] = { "forward", "reverse", "pingpong" };
      int i = 1;
      for(auto& tag : d.ed->doc().tags)
        t[i++] = L.create_table_with("name", tag.name, "from", tag.from + 1, "to", tag.to + 1, "direction", dirs[int(tag.direction)]);
      return t;
    }),
    //@api lua Document:applyEffect
    //@sig doc:applyEffect(id [, params])
    //@doc Runs an effect (built in or from a mod) on the active layer inside the selection, as part of the command's undo step.
    //@ex doc:applyEffect("gaussian-blur", { radius = 4 })
    "applyEffect", [](LDoc& d, const std::string& id, sol::optional<sol::table> params) {
      const Effect* e = findEffect(id);
      if(!e) throw std::runtime_error("no effect '" + id + "'");
      ParamValues v = defaultValues(*e);
      if(params) for(auto& [k, val] : *params) {
        std::string key = k.as<std::string>();
        if(val.is<std::string>()) v.text[key] = val.as<std::string>();
        else if(val.is<bool>()) v.num[key] = val.as<bool>() ? 1 : 0;
        else if(val.is<double>()) v.num[key] = val.as<double>();
      }
      applyEffect(d.ed->doc(), d.ed->recorder(d.ed->doc().activeLayer), *e, v, d.env);
    });

  L.new_usertype<ModUi>("Ui", sol::no_constructor,
    //@api lua Ui:text
    //@sig ui:text(text) / ui:muted(text)
    //@doc A line of text; muted is the dimmer color of hints.
    //@ex ui:text("Layers: " .. doc:layerCount())
    "text", [](ModUi& u, const std::string& s) { u.text(s, false); },
    "muted", [](ModUi& u, const std::string& s) { u.text(s, true); },
    //@api lua Ui:header
    //@sig ui:header(text)
    //@doc A section header (upper case, muted), like the ones in Settings.
    "header", [](ModUi& u, const std::string& s) { u.header(s); },
    //@api lua Ui:separator
    //@sig ui:separator() / ui:spacing() / ui:sameLine()
    //@doc A line across the window, a little vertical space, or the next widget on the same row as the last one.
    "separator", [](ModUi& u) { u.separator(); },
    "spacing", [](ModUi& u) { u.spacing(); },
    "sameLine", [](ModUi& u) { u.sameLine(); },
    //@api lua Ui:button
    //@sig ui:button(label [, width]) -> pressed
    //@doc A button; true on the frame it was clicked. width in points; -1 fills the row.
    //@ex if ui:button("Clear") then doc:layer():set(0, 0, 0) end
    "button", [](ModUi& u, const std::string& label, sol::optional<float> w) { return u.button(label, w.value_or(0)); },
    //@api lua Ui:checkbox
    //@sig ui:checkbox(label, value) -> value, changed
    //@doc A check box. Keep the value in a variable of the mod and pass it back each time.
    //@ex snap = ui:checkbox("Snap", snap)
    "checkbox", [](ModUi& u, const std::string& label, bool v) { bool c = u.checkbox(label, v); return std::make_tuple(v, c); },
    //@api lua Ui:slider
    //@sig ui:slider(label, value, min, max [, step]) -> value, changed
    //@doc A bar slider with a number field, like the effect dialogs. step rounds the value (default 1; 0.01 for fractions).
    //@ex size = ui:slider("Size", size, 1, 64)
    "slider", [](ModUi& u, const std::string& label, double v, double min, double max, sol::optional<double> step) {
      bool c = u.slider(label, v, min, max, step.value_or(1));
      return std::make_tuple(v, c);
    },
    //@api lua Ui:choice
    //@sig ui:choice(label, index, { name, ... }) -> index, changed
    //@doc A drop-down list; index starts at 1.
    //@ex mode = ui:choice("Mode", mode, { "Add", "Replace" })
    "choice", [](ModUi& u, const std::string& label, int index, sol::table options) {
      std::vector<std::string> names;
      for(size_t i = 1; i <= options.size(); i++) names.push_back(options.get_or<std::string>(i, ""));
      int i = std::clamp(index, 1, std::max(1, int(names.size()))) - 1;
      bool c = u.choice(label, i, names);
      return std::make_tuple(i + 1, c);
    },
    //@api lua Ui:color
    //@sig ui:color(label, color) -> color, changed
    //@doc A color swatch that opens a picker.
    //@ex tint = ui:color("Tint", tint)
    "color", [](ModUi& u, const std::string& label, lua_Integer c) {
      uint32_t v = uint32_t(c);
      bool ch = u.color(label, v);
      return std::make_tuple(lua_Integer(v), ch);
    },
    //@api lua Ui:input
    //@sig ui:input(label, text) -> text, changed
    //@doc A text field.
    //@ex prefix = ui:input("Prefix", prefix)
    "input", [](ModUi& u, const std::string& label, std::string s) { bool c = u.input(label, s); return std::make_tuple(s, c); },
    //@api lua Ui:width
    //@sig ui:width() -> number
    //@doc The width left on the current row, in points.
    "width", [](ModUi& u) { return u.width(); },
    //@api lua Ui:close
    //@sig ui:close()
    //@doc Closes the window after this frame (the menu or david.openWindow opens it again).
    "close", [](ModUi& u) { u.close(); });
}

void bindDavid(LuaRuntime& rt) {
  sol::state& L = rt.lua;
  sol::table david = L.create_named_table("david");
  //@api lua david.apiVersion
  //@sig david.apiVersion -> integer
  //@doc The API version (1). mod.json's "api" must not be higher.
  david["apiVersion"] = 1;
  //@api lua david.rgba
  //@sig david.rgba(r, g, b [, a]) -> color
  //@doc Packs 0-255 channels into a color. Alpha defaults to 255. Colors are plain integers.
  //@ex local red = david.rgba(255, 0, 0)
  david["rgba"] = [](int r, int g, int b, sol::optional<int> a) -> lua_Integer {
    auto c = [](int v) { return uint32_t(std::clamp(v, 0, 255)); };
    return lua_Integer(c(r) | c(g) << 8 | c(b) << 16 | c(a.value_or(255)) << 24);
  };
  //@api lua david.channels
  //@sig david.channels(color) -> r, g, b, a
  //@doc Unpacks a color into 0-255 channels.
  //@ex local r, g, b, a = david.channels(img:get(x, y))
  david["channels"] = [](lua_Integer c) { uint32_t v = uint32_t(c); return std::make_tuple(int(v & 0xFF), int((v >> 8) & 0xFF), int((v >> 16) & 0xFF), int(v >> 24)); };
  //@api lua david.log
  //@sig david.log(...)
  //@doc Writes a line to the mod's log (Settings > Mods). print does the same.
  auto logFn = [&rt](sol::variadic_args va, sol::this_state s) {
    std::string line;
    sol::state_view L(s);
    sol::protected_function tostr = L["tostring"];
    for(auto v : va) { sol::object o = v; std::string part = tostr(o); line += (line.empty() ? "" : "\t") + part; }
    if(rt.log) rt.log(line);
  };
  david["log"] = logFn;
  L["print"] = logFn;

  //@api lua david.effect
  //@sig david.effect{ id, name, category, params, run, dim }
  //@doc Registers an effect. params is a list of { id, label, type, min, max, default, choices, showIf } (types: float, int, bool, choice, color, text, curve); David builds the dialog, previews live, and the CLI can run it by id. run(img, p) gets an Image and the values by id. The window behind the dialog stays as is so the preview can be judged; dim = true darkens it like other dialogs.
  //@ex david.effect{ id = "darken", name = "Darken", params = { { id = "amount", type = "int", max = 100, default = 50 } }, run = function(img, p) ... end }
  david["effect"] = [&rt](sol::table t) {
    Effect e;
    e.id = t.get_or<std::string>("id", "");
    if(e.id.empty()) throw std::runtime_error("david.effect needs an id");
    e.name = t.get_or<std::string>("name", e.id);
    e.category = t.get_or<std::string>("category", "Effects/Mods");
    e.dim = t.get_or("dim", false);
    e.params = readParams(t["params"]);
    e.owner = rt.mod;
    int margin = t.get_or("margin", 0);
    if(margin) e.margin = [margin](const ParamValues&) { return margin; };
    sol::protected_function run = t["run"];
    std::shared_ptr<LuaRuntime> r = rt.shared_from_this();
    Effect copy = e;
    e.run = [r, run, copy](const Image& src, Image& dst, const ParamValues& v, const EffectEnv& env) {
      LImage img{ &src, &dst, env.originX, env.originY };
      auto res = call(*r, run, img, valuesTable(r->lua, copy, v));
      if(!res.valid()) throw std::runtime_error(errorOf(res));
    };
    rt.regs.effects.push_back(std::move(e));
  };

  //@api lua david.command
  //@sig david.command{ id, name, menu, params, run }
  //@doc Registers a command under the Mods menu (menu = "Mods/Submenu" nests it). With params (the same list as david.effect) David first asks for them in a dialog. run(doc, p) edits the Document; everything it changes is one undo step, and an error undoes it all.
  //@ex david.command{ id = "flip-names", name = "Uppercase Layer Names", run = function(doc) for i = 1, doc:layerCount() do local l = doc:layer(i); l.name = l.name:upper() end end }
  david["command"] = [&rt](sol::table t) {
    ModCommand c;
    c.id = t.get_or<std::string>("id", "");
    if(c.id.empty()) throw std::runtime_error("david.command needs an id");
    c.name = t.get_or<std::string>("name", c.id);
    c.menu = t.get_or<std::string>("menu", "Mods");
    c.mod = rt.mod;
    c.params = readParams(t["params"]);
    sol::protected_function run = t["run"];
    std::shared_ptr<LuaRuntime> r = rt.shared_from_this();
    std::string name = c.name;
    Effect shape;
    shape.params = c.params;
    c.run = [r, run, name, shape](Document& doc, const ParamValues& values, const EffectEnv& env, std::string& error) {
      DocEditor ed(doc);
      LDoc d{ &ed, env };
      sol::protected_function_result res;
      try { res = call(*r, run, &d, valuesTable(r->lua, shape, values)); }
      catch(const std::exception& ex) { error = ex.what(); return false; }
      if(!res.valid()) { error = errorOf(res); return false; }
      ed.commit(name, "");
      return true;
    };
    rt.regs.commands.push_back(std::move(c));
  };

  //@api lua david.window
  //@sig david.window{ id, title, kind, menu, width, height, draw }
  //@doc Registers a window. kind = "tool" (default) docks like Layers and History, or floats when Settings > Appearance has floating windows; kind = "dialog" floats over the canvas like Settings. It's listed in the Mods menu (menu = "Mods/Submenu" nests it) and keeps its open state between sessions. draw(ui, doc) runs every frame while it's open: build it with the Ui widgets, keep values in the mod's own variables. Document edits made in one draw call are one undo step named after the window, so edit on a button press rather than while a slider moves.
  //@ex david.window{ id = "info", title = "Canvas Info", draw = function(ui, doc) ui:text(doc.width .. " x " .. doc.height) end }
  david["window"] = [&rt](sol::table t) {
    ModWindow w;
    w.id = t.get_or<std::string>("id", "");
    if(w.id.empty()) throw std::runtime_error("david.window needs an id");
    w.title = t.get_or<std::string>("title", w.id);
    w.menu = t.get_or<std::string>("menu", "Mods");
    std::string kind = t.get_or<std::string>("kind", "tool");
    if(kind != "tool" && kind != "dialog") throw std::runtime_error("david.window: kind is \"tool\" or \"dialog\"");
    w.tool = kind == "tool";
    w.width = t.get_or("width", 0.f);
    w.height = t.get_or("height", 0.f);
    w.mod = rt.mod;
    sol::protected_function draw = t["draw"];
    if(!draw.valid()) throw std::runtime_error("david.window needs a draw function");
    std::shared_ptr<LuaRuntime> r = rt.shared_from_this();
    std::string title = w.title;
    w.draw = [r, draw, title](ModUi& ui, Document& doc, const EffectEnv& env, std::string& error) {
      DocEditor ed(doc);
      LDoc d{ &ed, env };
      sol::protected_function_result res;
      try { res = call(*r, draw, &ui, &d); }
      catch(const std::exception& ex) { error = ex.what(); return false; }
      if(!res.valid()) { error = errorOf(res); return false; }
      ed.commit(title, "");
      return true;
    };
    rt.regs.windows.push_back(std::move(w));
  };

  //@api lua david.openWindow
  //@sig david.openWindow(id) / david.closeWindow(id)
  //@doc Opens or closes one of the mod's windows, e.g. a dialog from a button in a tool window.
  //@ex if ui:button("Options...") then david.openWindow("options") end
  david["openWindow"] = [&rt](const std::string& id) { if(rt.showWindow) rt.showWindow(id, true); };
  david["closeWindow"] = [&rt](const std::string& id) { if(rt.showWindow) rt.showWindow(id, false); };

  //@api lua david.tool
  //@sig david.tool{ id, name, key, ink }
  //@doc Registers a painting tool: the freehand stroke and round brush of the Paintbrush, with your ink. ink(x, y, before, color) returns the new color of a pixel (or nil to leave it); the result is mixed in by the brush's coverage. key is an optional single letter. Tools appear after a restart.
  //@ex david.tool{ id = "checker", name = "Checker Brush", ink = function(x, y, before, color) if (x + y) % 2 == 0 then return color end end }
  david["tool"] = [&rt](sol::table t) {
    ModTool tl;
    tl.id = t.get_or<std::string>("id", "");
    if(tl.id.empty()) throw std::runtime_error("david.tool needs an id");
    tl.name = t.get_or<std::string>("name", tl.id);
    tl.key = t.get_or<std::string>("key", "");
    tl.mod = rt.mod;
    sol::protected_function ink = t["ink"];
    std::shared_ptr<LuaRuntime> r = rt.shared_from_this();
    tl.ink = [r, ink](int x, int y, uint32_t before, uint32_t color) -> uint32_t {
      auto res = call(*r, ink, x, y, lua_Integer(before), lua_Integer(color));
      if(!res.valid()) { if(r->log) r->log(errorOf(res)); return before; }
      sol::object o = res;
      return o.is<lua_Integer>() ? uint32_t(o.as<lua_Integer>()) : before;
    };
    rt.regs.tools.push_back(std::move(tl));
  };
}
}

std::shared_ptr<LuaRuntime> luaOpen(const std::string& modId, std::function<void(const std::string&)> log) {
  auto rt = std::make_shared<LuaRuntime>();
  rt->mod = modId;
  rt->log = std::move(log);
  sol::state& L = rt->lua;
  // The sandbox: no os, io, package or debug; no loading code from files or strings
  L.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8);
  for(const char* n : { "dofile", "loadfile", "load", "require", "loadstring" }) L[n] = sol::lua_nil;
  L["__david_rt"] = static_cast<void*>(rt.get());
  lua_sethook(L.lua_state(), watchdog, LUA_MASKCOUNT, 100000);
  bindTypes(*rt);
  bindDavid(*rt);
  return rt;
}

bool luaRunFile(LuaRuntime& rt, const std::string& path, std::string& error) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  if(!in) { error = "can't open " + path; return false; }
  std::stringstream ss;
  ss << in.rdbuf();
  std::string code = ss.str();
  if(!code.empty() && code[0] == 0x1B) { error = "compiled Lua chunks are not allowed"; return false; }
  rt.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(CALL_SECONDS);
  try {
    auto res = rt.lua.safe_script(code, sol::script_pass_on_error, "@" + pathToUtf8(pathFromUtf8(path).filename()), sol::load_mode::text);
    rt.deadline = std::chrono::steady_clock::time_point::max();
    if(!res.valid()) { error = errorOf(res); return false; }
  } catch(const std::exception& e) {
    rt.deadline = std::chrono::steady_clock::time_point::max();
    error = e.what();
    return false;
  }
  return true;
}

LuaRegistrations& luaRegistrations(LuaRuntime& rt) { return rt.regs; }

void luaSetWindowHook(LuaRuntime& rt, std::function<void(const std::string& id, bool open)> hook) { rt.showWindow = std::move(hook); }

bool luaRunScript(const std::string& path, Document& doc, const std::map<std::string, std::string>& args, const EffectEnv& env, std::string& error) {
  auto rt = luaOpen("script", [](const std::string& s) { std::fprintf(stderr, "%s\n", s.c_str()); });
  DocEditor ed(doc);
  LDoc d{ &ed, env };
  //@api lua david.image
  //@sig david.image -> Document
  //@doc In a script run by the CLI (--script), the image being processed. Edits go to the output.
  rt->lua["david"]["image"] = &d;
  //@api lua david.args
  //@sig david.args -> { name = value }
  //@doc In a CLI script, the --arg name=value pairs, as strings.
  //@ex local size = tonumber(david.args.size or "8")
  sol::table a = rt->lua.create_table();
  for(auto& [k, v] : args) a[k] = v;
  rt->lua["david"]["args"] = a;
  if(!luaRunFile(*rt, path, error)) return false;
  ed.commit("Script", "");
  return true;
}

}
