#include "core/Actions.h"
#include "core/Composite.h"
#include "core/Document.h"
#include "core/Text.h"
#include "core/TileEdit.h"
#include <algorithm>

namespace david {

#pragma region [ Compound ]
void CompoundAction::undo(Document& doc) {
  for(auto it = parts.rbegin(); it != parts.rend(); ++it) (*it)->undo(doc);
}

void CompoundAction::redo(Document& doc) {
  for(auto& p : parts) p->redo(doc);
}

size_t CompoundAction::bytes() const {
  size_t n = sizeof(*this);
  for(auto& p : parts) n += p->bytes();
  return n;
}
#pragma endregion

#pragma region [ Selection ]
static size_t maskBytes(const Mask* m) {
  if(!m) return 0;
  IRect b = m->bounds();
  return b.empty() ? 64 : size_t(b.width()) * b.height();
}

void SelectionEdit::undo(Document& doc) { doc.selection = before->clone(); }
void SelectionEdit::redo(Document& doc) { doc.selection = after->clone(); }
size_t SelectionEdit::bytes() const { return sizeof(*this) + maskBytes(before.get()) + maskBytes(after.get()); }

void pushSelection(Document& doc, std::unique_ptr<Mask> after, std::string name, const char* icon) {
  if(after->equals(*doc.selection)) return;
  auto a = std::make_unique<SelectionEdit>();
  a->name = std::move(name);
  a->icon = icon;
  a->before = doc.selection->clone();
  a->after = after->clone();
  doc.selection = std::move(after);
  doc.history.push(doc, std::move(a));
}
#pragma endregion

#pragma region [ Layers ]
size_t LayerInsert::bytes() const { return sizeof(*this) + (layer ? layer->allocatedBytes() : 0); }

void LayerInsert::apply(Document& doc, bool removing) {
  if(removing) {
    doc.layers.erase(doc.layers.begin() + index);
    doc.activeLayer = std::clamp(index - 1, 0, int(doc.layers.size()) - 1);
  } else {
    // A layer made for a one-frame image joins an animation with its image on the active frame
    if(layer->frames() != doc.frameCount()) doc.adopt(*layer);
    doc.layers.insert(doc.layers.begin() + index, layer->clone(true));
    doc.layers[index]->setFrame(doc.activeFrame);
    doc.activeLayer = index;
  }
}

void LayerMove::undo(Document& doc) {
  auto l = std::move(doc.layers[to]);
  doc.layers.erase(doc.layers.begin() + to);
  doc.layers.insert(doc.layers.begin() + from, std::move(l));
  doc.activeLayer = from;
}

void LayerMove::redo(Document& doc) {
  auto l = std::move(doc.layers[from]);
  doc.layers.erase(doc.layers.begin() + from);
  doc.layers.insert(doc.layers.begin() + to, std::move(l));
  doc.activeLayer = to;
}

void LayerPropsEdit::undo(Document& doc) {
  if(Layer* l = doc.findLayer(layerId)) before.applyTo(*l);
}

void LayerPropsEdit::redo(Document& doc) {
  if(Layer* l = doc.findLayer(layerId)) after.applyTo(*l);
}

void addLayer(Document& doc, std::unique_ptr<Layer> layer, int index, std::string name, const char* icon) {
  auto a = std::make_unique<LayerInsert>();
  a->name = std::move(name);
  a->icon = icon;
  a->index = index;
  a->layer = std::move(layer);
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}

void deleteLayer(Document& doc, int index, const char* icon) {
  if(doc.layers.size() <= 1) return;
  auto a = std::make_unique<LayerInsert>();
  a->name = N_("Delete Layer");
  a->icon = icon;
  a->index = index;
  a->remove = true;
  a->layer = doc.layers[index]->clone(true);
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}

void moveLayer(Document& doc, int from, int to, const char* icon) {
  if(to < 0 || to >= int(doc.layers.size()) || from == to) return;
  auto a = std::make_unique<LayerMove>();
  a->name = to > from ? N_("Move Layer Up") : N_("Move Layer Down");
  a->icon = icon;
  a->from = from;
  a->to = to;
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}

// The upper layer is composited onto the lower one (with its opacity), then removed
void mergeDown(Document& doc, int index, const char* icon) {
  if(index <= 0 || index >= int(doc.layers.size())) return;
  Layer& upper = *doc.layers[index];
  Layer& lower = *doc.layers[index - 1];
  if(doc.frameCount() > 1) {
    // Every frame: the merged layer replaces both (whole-layer copies; links in the lower layer are split)
    auto merged = lower.clone(true);
    for(int f = 0; f < doc.frameCount(); f++) {
      merged->setCel(f, lower.copyCel(f));
      if(!upper.visible) continue;
      merged->setFrame(f);
      for(int ty = 0; ty < upper.tilesY(); ty++)
        for(int tx = 0; tx < upper.tilesX(); tx++) {
          const Tile* src = upper.tileAt(f, tx, ty);
          if(!src) continue;
          Tile& dst = merged->ensureTile(tx, ty);
          for(int i = 0; i < Tile::PIXELS; i++) dst.data()[i] = blendPixel(dst.data()[i], src->data()[i], upper.opacity, upper.blend);
          dst.touch();
        }
    }
    merged->setFrame(doc.activeFrame);
    auto step = std::make_unique<CompoundAction>();
    step->name = N_("Merge Layer Down");
    step->icon = icon;
    auto part = [&](int at, bool remove, std::unique_ptr<Layer> l) {
      auto a = std::make_unique<LayerInsert>();
      a->index = at; a->remove = remove; a->layer = std::move(l);
      a->redo(doc);
      step->parts.push_back(std::move(a));
    };
    part(index, true, upper.clone(true));
    part(index - 1, true, doc.layers[index - 1]->clone(true));
    part(index - 1, false, std::move(merged));
    doc.history.push(doc, std::move(step));
    return;
  }
  TileRecorder rec(lower);
  if(upper.visible) {
    for(int ty = 0; ty < upper.tilesY(); ty++) {
      for(int tx = 0; tx < upper.tilesX(); tx++) {
        const Tile* src = upper.tile(tx, ty);
        if(!src) continue;
        rec.touch(tx, ty);
        Tile& dst = lower.ensureTile(tx, ty);
        for(int i = 0; i < Tile::PIXELS; i++) dst.data()[i] = blendPixel(dst.data()[i], src->data()[i], upper.opacity, upper.blend);
        dst.touch();
      }
    }
  }
  auto merged = std::make_unique<CompoundAction>();
  merged->name = N_("Merge Layer Down");
  merged->icon = icon;
  if(auto edit = rec.finish("", "")) merged->parts.push_back(std::move(edit));
  auto del = std::make_unique<LayerInsert>();
  del->index = index;
  del->remove = true;
  del->layer = upper.clone(true);
  del->redo(doc);
  merged->parts.push_back(std::move(del));
  doc.history.push(doc, std::move(merged));
}

void setLayerProps(Document& doc, int index, const LayerProps& props, const char* icon) {
  Layer& l = *doc.layers[index];
  LayerProps before = LayerProps::of(l);
  if(before == props) return;
  auto a = std::make_unique<LayerPropsEdit>();
  a->name = before.visible != props.visible && before.name == props.name && before.opacity == props.opacity
    ? (props.visible ? N_("Show Layer") : N_("Hide Layer")) : N_("Layer Properties");
  a->icon = icon;
  a->layerId = l.id();
  a->before = before;
  a->after = props;
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}
#pragma endregion

}
