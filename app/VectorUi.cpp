#include "app/App.h"
#include "core/Layer.h"
#include "core/Vector.h"
#include "core/io/Png.h"
#include "core/io/Svg.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include "ui/ToolIcons.h"
#include "ui/tools/VectorTools.h"
#include <imgui_internal.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <fstream>
#include <iterator>

namespace david {

static constexpr double PI = 3.14159265358979323846;

#pragma region [ Raster edits on vector layers ]
namespace {
// Rasterize Layer: the layer keeps its pixels and drops its objects (undo brings them back)
class RasterizeEdit : public Action {
public:
  uint64_t layerId = 0;
  std::vector<std::shared_ptr<VecTree>> trees;  // per frame
  void undo(Document& doc) override {
    Layer* l = doc.findLayer(layerId);
    if(!l) return;
    l->kind = ELayerKind::Vector;
    for(int f = 0; f < l->frames() && f < int(trees.size()); f++) if(trees[f] && l->cel(f)) l->cel(f)->vec = trees[f];
    doc.onChanged();
  }
  void redo(Document& doc) override {
    Layer* l = doc.findLayer(layerId);
    if(!l) return;
    l->kind = ELayerKind::Raster;
    for(int f = 0; f < l->frames(); f++) if(l->cel(f)) l->cel(f)->vec.reset();
    doc.onChanged();
  }
  size_t bytes() const override { return sizeof(*this) + trees.size() * 256; }
};
}

void App::rasterizeLayer(int index) {
  Layer& l = *_doc->layers[index];
  if(l.kind != ELayerKind::Vector) return;
  commitTool();
  auto edit = std::make_unique<RasterizeEdit>();
  edit->name = N_("Rasterize Layer");
  edit->icon = ICON_FA_IMAGE;
  edit->layerId = l.id();
  for(int f = 0; f < l.frames(); f++) edit->trees.push_back(l.cel(f) ? l.cel(f)->vec : nullptr);
  edit->redo(*_doc);
  _doc->history.push(*_doc, std::move(edit));
  _doc->selectedObjects.clear();
}

bool App::rasterGuard(std::function<void()> then) {
  if(_doc->active().kind != ELayerKind::Vector) return true;
  if(_cfg.vectorRaster == 1) { rasterizeLayer(_doc->activeLayer); return true; }
  if(_cfg.vectorRaster == 2) { layerOp("add"); return true; }
  _afterRaster = std::move(then);
  _rasterRemember = false;
  _popup = "###vecraster";
  return false;
}

void App::drawRasterDialog() {
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, { 0.5f, 0.5f });
  std::string title = std::string(ICON_FA_BEZIER_CURVE "  ") + tr("Vector layer") + "###vecraster";
  bool open = true;
  if(!ImGui::BeginPopupModal(title.c_str(), &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) return;
  ImGui::Spacing();
  ImGui::TextUnformatted(tr("This paints pixels, and the active layer is a vector layer."));
  ImGui::TextDisabled("%s", tr("Rasterize it (its objects become pixels), or paint on a new layer above it."));
  ImGui::Spacing();
  ImGui::Checkbox(tr("Remember my choice"), &_rasterRemember);
  ImGui::SetItemTooltip("%s", tr("Settings > Beta tools asks again"));
  ImGui::Spacing();
  float bw = 150 * _dpi;
  ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - bw * 3 - ImGui::GetStyle().ItemSpacing.x * 2));
  bool raster = ui::dialogButton(tr("Rasterize layer"), true, bw) || ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
  ImGui::SameLine();
  bool layer = ui::dialogButton(tr("New layer above"), false, bw);
  ImGui::SameLine();
  bool cancel = ui::dialogButton(tr("Cancel"), false, bw) || ImGui::IsKeyPressed(ImGuiKey_Escape) || !open;
  if(raster || layer || cancel) {
    ImGui::CloseCurrentPopup();
    auto then = std::move(_afterRaster);
    _afterRaster = nullptr;
    if(!cancel) {
      if(_rasterRemember) _cfg.vectorRaster = raster ? 1 : 2;
      if(raster) rasterizeLayer(_doc->activeLayer);
      else layerOp("add");
      if(then) then();
    }
  }
  ImGui::EndPopup();
}
#pragma endregion

#pragma region [ Per frame ]
void App::vectorFrame() {
  // Picked objects belong to one layer; switching layers drops them
  uint64_t id = _doc->active().id();
  if(id != _objectsLayer) { _doc->selectedObjects.clear(); _objectsLayer = id; }
  _nav.view.sharpVectors = _cfg.betaVector && !_cfg.vectorPixels;
}
#pragma endregion

#pragma region [ SVG and trace ]
void App::importSvg(const std::string& path, bool newDocument) {
  std::ifstream in(pathFromUtf8(path), std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::string err;
  if(newDocument) {
    auto doc = svg::loadDocument(text, err);
    if(!doc) { showError(std::string(tr("Couldn't open the SVG:")) + " " + err); return; }
    // std::function wants a copyable action: the document rides in a shared holder
    auto holder = std::make_shared<std::unique_ptr<Document>>(std::move(doc));
    confirmDiscard([this, holder] { if(*holder) setDocument(std::move(*holder)); });
    return;
  }
  svg::ReadResult r;
  if(!svg::read(text, r, err)) { showError(std::string(tr("Couldn't open the SVG:")) + " " + err); return; }
  commitTool();
  Document& doc = *_doc;
  auto layer = std::make_unique<Layer>(doc.width(), doc.height(), pathToUtf8(pathFromUtf8(path).stem()));
  layer->kind = ELayerKind::Vector;
  layer->fitFrames(doc.frameCount(), doc.activeFrame);
  VecTree& t = vectorTree(*layer, doc.activeFrame);
  t.objects = std::move(r.objects);
  t.touch();
  rasterizeVectorCel(*layer, doc.activeFrame);
  layer->setFrame(doc.activeFrame);
  addLayer(doc, std::move(layer), doc.activeLayer + 1, N_("Import SVG"), ICON_FA_FILE_IMPORT);
}

void App::exportSvg(const std::string& path) {
  commitTool();
  std::string p = path;
  if(!pathFromUtf8(p).has_extension()) p += ".svg";
  std::string text = svg::writeDocument(*_doc);
  std::string err;
  if(!writeFileAtomic(p, std::vector<uint8_t>(text.begin(), text.end()), err)) showError(std::string(tr("Couldn't save:")) + " " + err);
}

void App::traceLayer() {
  commitTool();
  Document& doc = *_doc;
  Layer& src = doc.active();
  auto objects = tracePixels(src, src.frame());
  if(objects.empty()) { showError(tr("The layer has no pixels to trace.")); return; }
  auto layer = std::make_unique<Layer>(doc.width(), doc.height(), src.name + " " + tr("traced"));
  layer->kind = ELayerKind::Vector;
  layer->fitFrames(doc.frameCount(), doc.activeFrame);
  VecTree& t = vectorTree(*layer, doc.activeFrame);
  t.objects = std::move(objects);
  t.touch();
  rasterizeVectorCel(*layer, doc.activeFrame);
  layer->setFrame(doc.activeFrame);
  addLayer(doc, std::move(layer), doc.activeLayer + 1, N_("Trace Pixels"), ICON_FA_BEZIER_CURVE);
}
#pragma endregion


#pragma region [ Vector tool row ]
static ImVec4 toVec(uint32_t c) {
  return { (c & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, ((c >> 16) & 0xFF) / 255.f, (c >> 24) / 255.f };
}

static uint32_t fromVec(const float* v) {
  auto b = [](float f) { return uint32_t(std::clamp(f, 0.f, 1.f) * 255 + 0.5f); };
  return b(v[0]) | (b(v[1]) << 8) | (b(v[2]) << 16) | (b(v[3]) << 24);
}

// The tools that work on vector layers
static const ETool VECTOR_TOOLS[] = { ETool::Mouse, ETool::Node, ETool::Shapes, ETool::Curve, ETool::Line, ETool::Pencil, ETool::Text };

bool App::vectorRowShown() {
  return vectorMode() && std::find(std::begin(VECTOR_TOOLS), std::end(VECTOR_TOOLS), tool().id) != std::end(VECTOR_TOOLS);
}

// Gravit's tool bar: the vector tools, then the actions on the picked objects in groups
void App::vectorToolRow() {
  float gap = 2 * _dpi;
  for(ETool id : VECTOR_TOOLS) {
    for(int i = 0; i < int(_tools.size()); i++) {
      if(_tools[i]->id != id) continue;
      Tool& t = *_tools[i];
      // The Curve tool draws paths node by node: the pen
      const char* icon = id == ETool::Curve ? ICON_PH_PEN_NIB : t.icon;
      std::string tip = std::string(tr(id == ETool::Curve ? N_("Pen") : t.name)) + "  (" + ImGui::GetKeyName(t.key) + ")";
      ImGui::PushID(i);
      if(ui::iconButton("##vt", icon, { 0, 0 }, i == _tool, tip.c_str())) selectTool(i);
      ImGui::PopID();
      ImGui::SameLine(0, gap);
    }
  }
  const EObjOp groups[][4] = {
    { EObjOp::FlipH, EObjOp::FlipV, EObjOp::RotateLeft, EObjOp::RotateRight },
    { EObjOp::Group, EObjOp::Ungroup, EObjOp::COUNT, EObjOp::COUNT },
    { EObjOp::Union, EObjOp::Subtract, EObjOp::Intersect, EObjOp::Exclude },
    { EObjOp::Forward, EObjOp::Backward, EObjOp::COUNT, EObjOp::COUNT },
    { EObjOp::ToPath, EObjOp::Duplicate, EObjOp::Delete, EObjOp::COUNT },
  };
  for(auto& g : groups) {
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical, 1);
    for(EObjOp op : g) {
      if(op == EObjOp::COUNT) continue;
      ImGui::SameLine(0, gap);
      std::string id = objectOpId(op);
      Command* c = _commands.find(id);
      std::string tip = tr(objectOpInfo(op).name);
      if(c && !c->chords.empty()) tip += "  (" + Commands::chordName(c->chords[0]) + ")";
      ImGui::BeginDisabled(!objectOpReady(*this, op));
      if(ui::iconButton(("##" + id).c_str(), objectOpInfo(op).icon, { 0, 0 }, false, tip.c_str())) _commands.run(id);
      ImGui::EndDisabled();
    }
  }
}
#pragma endregion

#pragma region [ Object window ]
namespace {
// A labeled row of the Object window: the label on the left, the field filling the rest
void rowStart(const char* label) {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", tr(label));
  ImGui::TableNextColumn();
  ImGui::SetNextItemWidth(-FLT_MIN);
}

bool beginRows(const char* id, float dpi) {
  if(!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) return false;
  ImGui::TableSetupColumn("##l", ImGuiTableColumnFlags_WidthFixed, 72 * dpi);
  ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthStretch);
  return true;
}

// An on/off toggle and a color swatch; true when the paint is on
bool paintRow(const char* id, VecPaint& paint, uint32_t fallback, bool& changed) {
  bool on = paint.visible();
  if(ImGui::Checkbox((std::string("##on") + id).c_str(), &on)) {
    paint = on ? VecPaint::solid(fallback) : VecPaint{};
    changed = true;
  }
  ImGui::SameLine();
  if(paint.type == EPaintType::Linear || paint.type == EPaintType::Radial) {
    ImGui::TextDisabled("%s", tr(paint.type == EPaintType::Linear ? "Linear gradient" : "Radial gradient"));
    return on;
  }
  ImVec4 v = toVec(paint.type == EPaintType::Solid ? paint.color : fallback);
  float c[4] = { v.x, v.y, v.z, v.w };
  ImGui::BeginDisabled(!on);
  ImGui::SetNextItemWidth(-FLT_MIN);
  if(ImGui::ColorEdit4((std::string("##c") + id).c_str(), c, ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_DisplayHex)) {
    paint = VecPaint::solid(fromVec(c));
    changed = true;
  }
  ImGui::EndDisabled();
  return on;
}

// The dash patterns the window offers, in line widths
const std::vector<double> DASHES[] = { {}, { 3, 2 }, { 1, 1 } };

std::string header(const char* icon, const char* label, const char* id) { return std::string(icon) + "  " + tr(label) + "###" + id; }
}

void App::drawObjectWindow() {
  if(!_cfg.betaVector || !_cfg.showObject) return;
  if(beginPanel(13, ICON_TB_VECTOR, N_("Object"), "object", &_cfg.showObject)) {
    auto& objs = vectorTree(_doc->active(), _doc->active().frame()).objects;
    auto& sel = _doc->selectedObjects;
    if(vectorMode()) std::erase_if(sel, [&](uint64_t id) { return !findObject(objs, id); });
    if(!vectorMode()) ImGui::TextDisabled("%s", tr("Objects of a vector layer show here"));
    else if(sel.empty()) ImGui::TextDisabled("%s", tr("No object picked"));
    else {
      // Every field shows the first picked object and changes them all: live while dragged, one step after
      VecObject first = *findObject(objs, sel[0]);
      auto edit = [&](auto fn) {
        if(!_objectStyle) _objectStyle = std::make_unique<VectorChange>(*_doc);
        for(uint64_t id : sel) if(VecObject* o = findObject(_objectStyle->tree().objects, id)) fn(*o);
        vectorChanged(*_doc, _doc->active(), false);
      };
      float dpi = _dpi;
      ImGuiTreeNodeFlags open = ImGuiTreeNodeFlags_DefaultOpen;

      // Name, or how many are picked
      if(sel.size() == 1) {
        char name[128];
        std::snprintf(name, sizeof name, "%s", first.name.c_str());
        ImGui::SetNextItemWidth(-FLT_MIN);
        if(ImGui::InputTextWithHint("##name", tr("Name"), name, sizeof name)) edit([&](VecObject& o) { o.name = name; });
      } else ImGui::Text("%d %s", int(sel.size()), tr("objects"));

      // Place and size of the picked objects' box; the angle of a single object
      DRect box;
      for(uint64_t id : sel) if(const VecObject* o = findObject(objs, id)) box.add(bounds(*o, true));
      if(ImGui::CollapsingHeader(header(ICON_TB_ARROWS_DIAGONAL, N_("Transform"), "xf").c_str(), open) && !box.empty() && beginRows("##xft", dpi)) {
        float x = float(box.x0), y = float(box.y0), w = float(box.width()), hgt = float(box.height());
        rowStart(N_("Position"));
        float pos[2] = { x, y };
        if(ImGui::DragFloat2("##pos", pos, 0.5f, -1e6f, 1e6f, "%.1f")) {
          Affine m = Affine::translate(pos[0] - x, pos[1] - y);
          edit([&](VecObject& o) { o.xf = m * o.xf; });
        }
        rowStart(N_("Size"));
        float size[2] = { w, hgt };
        if(ImGui::DragFloat2("##size", size, 0.5f, 0.1f, 1e6f, "%.1f")) {
          double sx = w > 1e-6f ? std::max(0.1f, size[0]) / w : 1, sy = hgt > 1e-6f ? std::max(0.1f, size[1]) / hgt : 1;
          Affine m = Affine::around({ box.x0, box.y0 }, Affine::scaling(sx, sy));
          edit([&](VecObject& o) { o.xf = m * o.xf; });
        }
        if(sel.size() == 1) {
          rowStart(N_("Angle"));
          double now = std::atan2(first.xf.b, first.xf.a);
          float deg = float(now * 180 / PI);
          if(ImGui::DragFloat("##angle", &deg, 0.5f, -360, 360, "%.1f°")) {
            Affine m = Affine::around({ (box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2 }, Affine::rotation(deg * PI / 180 - now));
            edit([&](VecObject& o) { o.xf = m * o.xf; });
          }
        }
        ImGui::EndTable();
      }

      if(ImGui::CollapsingHeader(header(ICON_TB_ADJUSTMENTS_HORIZONTAL, N_("Appearance"), "look").c_str(), open) && beginRows("##lookt", dpi)) {
        if(first.kind == EVecKind::Rect) {
          rowStart(N_("Corner"));
          float r = float(first.radius);
          if(ImGui::DragFloat("##radius", &r, 0.25f, 0, 2000, "%.1f")) edit([&](VecObject& o) { if(o.kind == EVecKind::Rect) o.radius = std::max(0.f, r); });
        }
        if(first.kind == EVecKind::Polygon) {
          rowStart(N_("Sides"));
          int sides = first.sides;
          if(ImGui::SliderInt("##sides", &sides, 3, 24)) edit([&](VecObject& o) { if(o.kind == EVecKind::Polygon) o.sides = sides; });
          rowStart(N_("Star"));
          float inner = float(first.inner * 100);
          if(ImGui::SliderFloat("##inner", &inner, 10, 100, "%.0f%%")) edit([&](VecObject& o) { if(o.kind == EVecKind::Polygon) o.inner = inner / 100; });
          ImGui::SetItemTooltip("%s", tr("Star: inner radius"));
        }
        if(first.kind == EVecKind::Text) {
          rowStart(N_("Font size"));
          float fs = float(first.size);
          if(ImGui::DragFloat("##fsize", &fs, 0.25f, 1, 2000, "%.0f px")) edit([&](VecObject& o) { if(o.kind == EVecKind::Text) o.size = std::max(1.f, fs); });
        }
        rowStart(N_("Opacity"));
        float op = float(first.opacity * 100);
        if(ui::band("##opacity", op, 0, 100, 1, dpi)) edit([&](VecObject& o) { o.opacity = op / 100; });
        ImGui::EndTable();
      }

      if(first.kind != EVecKind::Group) {
        if(ImGui::CollapsingHeader(header(ICON_TB_BUCKET_DROPLET, N_("Fill"), "fill").c_str(), open) && beginRows("##fillt", dpi)) {
          rowStart(N_("Color"));
          VecPaint fill = first.fill;
          bool changed = false;
          paintRow("fill", fill, _secondary, changed);
          if(changed) edit([&](VecObject& o) { o.fill = fill; });
          ImGui::EndTable();
        }

        if(ImGui::CollapsingHeader(header(ICON_TB_BORDER_OUTER, N_("Border"), "line").c_str(), open) && beginRows("##linet", dpi)) {
          VecStroke st = first.stroke;
          bool changed = false;
          rowStart(N_("Color"));
          bool on = paintRow("line", st.paint, _primary, changed);
          if(changed) edit([&](VecObject& o) { o.stroke.paint = st.paint; if(st.paint.visible() && o.stroke.width <= 0) o.stroke.width = 1; });
          ImGui::BeginDisabled(!on);
          rowStart(N_("Width"));
          float w = float(st.width);
          if(ImGui::DragFloat("##width", &w, 0.25f, 0, 500, "%.1f px")) edit([&](VecObject& o) { o.stroke.width = std::max(0.f, w); });
          rowStart(N_("Position"));
          int align = int(st.align);
          const char* aligns[] = { tr("Center"), tr("Inside"), tr("Outside") };
          if(ImGui::Combo("##align", &align, aligns, 3)) edit([&](VecObject& o) { o.stroke.align = EStrokeAlign(align); });
          rowStart(N_("Ends"));
          int cap = int(st.cap);
          const char* caps[] = { tr("Flat"), tr("Round"), tr("Square") };
          if(ImGui::Combo("##cap", &cap, caps, 3)) edit([&](VecObject& o) { o.stroke.cap = ECap(cap); });
          rowStart(N_("Corners"));
          int join = int(st.join);
          const char* joins[] = { tr("Miter"), tr("Round"), tr("Bevel") };
          if(ImGui::Combo("##join", &join, joins, 3)) edit([&](VecObject& o) { o.stroke.join = EJoin(join); });
          rowStart(N_("Dash"));
          int dash = 0;
          for(int i = 0; i < 3; i++) if(st.dash == DASHES[i]) dash = i;
          const char* dashes[] = { tr("Solid"), tr("Dashed"), tr("Dotted") };
          if(ImGui::Combo("##dash", &dash, dashes, 3)) edit([&](VecObject& o) { o.stroke.dash = DASHES[dash]; });
          ImGui::EndDisabled();
          ImGui::EndTable();
        }
      }
    }
  }
  ImGui::End();
  // The field let go: the edit becomes one step
  if(_objectStyle && !ImGui::IsAnyItemActive()) {
    _objectStyle->commit(N_("Object Style"), ICON_TB_VECTOR);
    _objectStyle.reset();
  }
}
#pragma endregion

}
