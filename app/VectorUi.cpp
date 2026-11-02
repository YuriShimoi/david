#include "app/App.h"
#include "core/Layer.h"
#include "core/Vector.h"
#include "core/io/Png.h"
#include "core/io/Svg.h"
#include "core/Text.h"
#include "ui/I18n.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include <algorithm>
#include <fstream>
#include <iterator>

namespace david {

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

}
