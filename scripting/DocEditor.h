#pragma once
#include "core/Vector.h"
#include "core/Actions.h"
#include "core/Animation.h"
#include "core/TileEdit.h"
#include <map>
#include <memory>
#include <string>

namespace david {

class Document;

// Edits a document as one undo step: pixel writes are recorded per layer, layer changes are applied as actions
// in order. commit() pushes the step; rollback() puts everything back (a script failed).
class DocEditor {
public:
  explicit DocEditor(Document& doc) : _doc(doc), _step(std::make_unique<CompoundAction>()) {}
  ~DocEditor() { rollback(); }

  Document& doc() { return _doc; }
  uint32_t get(int layer, int x, int y) const;
  void set(int layer, int x, int y, uint32_t c);
  // A recorder for the layer (effects write through it)
  TileRecorder& recorder(int layer);
  int addLayer(const std::string& name, bool vector = false);  // above the active layer; returns its index
  // A vector layer's objects on the active frame, to edit in place (null for a raster layer); the change joins
  // the step and the layer's pixels follow
  VecTree* vectors(int layer);
  bool deleteLayer(int index);
  void setProps(int index, const LayerProps& props);
  // Frames: switching frames finishes the pixel edits so far (they belong to the frame they were made on)
  void setFrame(int f);
  int addFrame(ENewFrame kind);  // after the active frame; returns its index
  bool deleteFrame(int f);
  void setAnim(const AnimState& s);

  void commit(const std::string& name, const char* icon);
  void rollback();

private:
  Document& _doc;
  std::unique_ptr<CompoundAction> _step;
  std::map<uint64_t, std::unique_ptr<TileRecorder>> _recs;
  struct VecBefore { int frame; VecTree tree; };
  std::map<uint64_t, VecBefore> _vecs;
  bool _done = false;
  // Pixel edits so far become parts, so later layer changes undo in the right order
  void flush();
};

}
