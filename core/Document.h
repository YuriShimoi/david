#pragma once
#include "core/History.h"
#include "core/Layer.h"
#include "core/Mask.h"
#include <memory>
#include <string>
#include <vector>

namespace david {

// One animation frame: how long it shows. The images are the layers' cels at the frame's index.
struct Frame {
  int duration = 0;  // ms; 0 = the document's frame rate
  bool operator==(const Frame&) const = default;
};

enum class ETagDirection : uint8_t { Forward, Reverse, PingPong };

// A named frame range ("walk", "idle"); playback and export can be limited to one
struct FrameTag {
  std::string name;
  int from = 0, to = 0;  // inclusive frame indexes
  ETagDirection direction = ETagDirection::Forward;
  uint32_t color = 0xFFD9A35Bu;
  bool operator==(const FrameTag&) const = default;
};

class Document {
public:
  std::vector<std::unique_ptr<Layer>> layers;
  int activeLayer = 0;
  std::string path;
  History history;
  std::unique_ptr<Mask> selection;  // empty = no selection (tools act on the whole layer)
  uint64_t revision = 0;
  double dpi = 96;  // print resolution, pixels per inch
  std::vector<uint32_t> palette;  // saved in .david files; empty = none
  std::vector<uint64_t> selectedObjects;  // vector objects picked on the active layer (not saved)

  // Animation: every layer has one cel per frame. A one-frame document is a plain image.
  std::vector<Frame> frames = { Frame{} };
  int activeFrame = 0;
  double fps = 12;
  std::vector<FrameTag> tags;

  Document(int width, int height);
  static std::unique_ptr<Document> blank(int width, int height, uint32_t fill);

  int width() const { return _width; }
  int height() const { return _height; }
  bool modified() const { return !history.isClean(); }
  bool hasSelection() const { return !selection->empty(); }
  // Coverage that clips edits: the selection, or everything when nothing is selected
  uint8_t clip(int x, int y) const { return selection->empty() ? 255 : selection->get(x, y); }

  Layer& active() { return *layers[activeLayer]; }
  Layer* findLayer(uint64_t id) const;
  int indexOf(uint64_t id) const;

  int frameCount() const { return int(frames.size()); }
  // Puts every layer on frame f (tools, effects and the canvas act on the active frame)
  void setActiveFrame(int f);
  // How long frame f shows, in ms
  int frameMs(int f) const;
  // A layer joining the document gets a cel per frame, its image on the active frame
  void adopt(Layer& layer) const { layer.fitFrames(frameCount(), activeFrame); layer.setFrame(activeFrame); }

  // Whole-image swap for resize and canvas size (their undo step owns the copies)
  void replaceImage(int width, int height, std::vector<std::unique_ptr<Layer>> newLayers, std::unique_ptr<Mask> newSelection);

  // Called after any change, so views know to redraw and refresh thumbnails
  void onChanged() { revision++; }

private:
  int _width, _height;
};

}
