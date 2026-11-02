#pragma once
#include "core/History.h"
#include "core/Layer.h"
#include "core/Mask.h"

namespace david {

// Several actions as one History step (e.g. move = pixels + selection)
class CompoundAction : public Action {
public:
  std::vector<std::unique_ptr<Action>> parts;
  void undo(Document& doc) override;
  void redo(Document& doc) override;
  size_t bytes() const override;
};

class SelectionEdit : public Action {
public:
  std::unique_ptr<Mask> before, after;
  void undo(Document& doc) override;
  void redo(Document& doc) override;
  size_t bytes() const override;
};

// Insert (or, reversed, remove) one layer at an index
class LayerInsert : public Action {
public:
  int index = 0;
  bool remove = false;
  std::unique_ptr<Layer> layer; // the copy to insert; for a removal, the removed layer
  void undo(Document& doc) override { apply(doc, !remove); }
  void redo(Document& doc) override { apply(doc, remove); }
  size_t bytes() const override;
private:
  void apply(Document& doc, bool removing);
};

class LayerMove : public Action {
public:
  int from = 0, to = 0;
  void undo(Document& doc) override;
  void redo(Document& doc) override;
  size_t bytes() const override { return sizeof(*this); }
};

struct LayerProps {
  std::string name;
  bool visible = true;
  uint8_t opacity = 255;
  EBlend blend = EBlend::Normal;
  bool operator==(const LayerProps&) const = default;
  static LayerProps of(const Layer& l) { return { l.name, l.visible, l.opacity, l.blend }; }
  void applyTo(Layer& l) const { l.name = name; l.visible = visible; l.opacity = opacity; l.blend = blend; }
};

class LayerPropsEdit : public Action {
public:
  uint64_t layerId = 0;
  LayerProps before, after;
  void undo(Document& doc) override;
  void redo(Document& doc) override;
  size_t bytes() const override { return sizeof(*this); }
};

// Helpers that build, apply and push the step in one call
void pushSelection(Document& doc, std::unique_ptr<Mask> after, std::string name, const char* icon);
void addLayer(Document& doc, std::unique_ptr<Layer> layer, int index, std::string name, const char* icon);
void deleteLayer(Document& doc, int index, const char* icon);
void moveLayer(Document& doc, int from, int to, const char* icon);
void mergeDown(Document& doc, int index, const char* icon);
void setLayerProps(Document& doc, int index, const LayerProps& props, const char* icon);

}
