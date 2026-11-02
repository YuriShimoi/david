#include "scripting/DocEditor.h"
#include "core/Document.h"

namespace david {

uint32_t DocEditor::get(int layer, int x, int y) const {
  if(layer < 0 || layer >= int(_doc.layers.size())) return 0;
  const Layer& l = *_doc.layers[layer];
  return l.contains(x, y) ? l.get(x, y) : 0;
}

TileRecorder& DocEditor::recorder(int layer) {
  Layer& l = *_doc.layers[layer];
  auto& rec = _recs[l.id()];
  if(!rec) rec = std::make_unique<TileRecorder>(l);
  return *rec;
}

void DocEditor::set(int layer, int x, int y, uint32_t c) {
  if(layer < 0 || layer >= int(_doc.layers.size())) return;
  Layer& l = *_doc.layers[layer];
  if(!l.contains(x, y)) return;
  recorder(layer).touchPixel(x, y);
  l.set(x, y, c);
}

void DocEditor::flush() {
  for(auto& [id, rec] : _recs)
    if(auto edit = rec->finish("", "")) _step->parts.push_back(std::move(edit));
  _recs.clear();
}

int DocEditor::addLayer(const std::string& name) {
  flush();
  auto act = std::make_unique<LayerInsert>();
  act->index = _doc.activeLayer + 1;
  act->layer = std::make_unique<Layer>(_doc.width(), _doc.height(), name);
  act->redo(_doc);
  int index = act->index;
  _doc.activeLayer = index;
  _step->parts.push_back(std::move(act));
  return index;
}

bool DocEditor::deleteLayer(int index) {
  if(index < 0 || index >= int(_doc.layers.size()) || _doc.layers.size() < 2) return false;
  flush();
  auto act = std::make_unique<LayerInsert>();
  act->index = index;
  act->remove = true;
  act->layer = _doc.layers[index]->clone(true);
  act->redo(_doc);
  _step->parts.push_back(std::move(act));
  return true;
}

void DocEditor::setProps(int index, const LayerProps& props) {
  if(index < 0 || index >= int(_doc.layers.size())) return;
  flush();
  auto act = std::make_unique<LayerPropsEdit>();
  act->layerId = _doc.layers[index]->id();
  act->before = LayerProps::of(*_doc.layers[index]);
  act->after = props;
  act->redo(_doc);
  _step->parts.push_back(std::move(act));
}

void DocEditor::setFrame(int f) {
  if(f < 0 || f >= _doc.frameCount() || f == _doc.activeFrame) return;
  flush();
  _doc.setActiveFrame(f);
}

int DocEditor::addFrame(ENewFrame kind) {
  flush();
  auto a = makeAddFrame(_doc, _doc.activeFrame, kind);
  a->redo(_doc);
  _step->parts.push_back(std::move(a));
  return _doc.activeFrame;
}

bool DocEditor::deleteFrame(int f) {
  if(f < 0 || f >= _doc.frameCount() || _doc.frameCount() < 2) return false;
  flush();
  auto a = makeDeleteFrame(_doc, f);
  a->redo(_doc);
  _step->parts.push_back(std::move(a));
  return true;
}

void DocEditor::setAnim(const AnimState& s) {
  flush();
  auto a = std::make_unique<AnimEdit>();
  a->before = AnimState::of(_doc);
  a->after = s;
  a->redo(_doc);
  _step->parts.push_back(std::move(a));
}

void DocEditor::commit(const std::string& name, const char* icon) {
  if(_done) return;
  flush();
  _done = true;
  if(_step->parts.empty()) return;
  _step->name = name;
  _step->icon = icon;
  _doc.history.push(_doc, std::move(_step));
  _doc.onChanged();
}

void DocEditor::rollback() {
  if(_done) return;
  _done = true;
  for(auto& [id, rec] : _recs) rec->restore();
  _recs.clear();
  for(auto it = _step->parts.rbegin(); it != _step->parts.rend(); ++it) (*it)->undo(_doc);
  _step->parts.clear();
  _doc.onChanged();
}

}
