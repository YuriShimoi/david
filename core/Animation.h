#pragma once
#include "core/Actions.h"
#include "core/Document.h"
#include <map>

namespace david {

#pragma region [ Undo steps ]
// Insert (or, reversed, remove) one frame: its timing, every layer's cel and the tags as they are with it
class FrameInsert : public Action {
public:
  int index = 0;
  bool remove = false;
  Frame meta;
  std::map<uint64_t, std::shared_ptr<Cel>> cels;  // by layer id; missing = empty cel
  std::vector<FrameTag> tagsWith, tagsWithout;
  FrameInsert() { frame = -2; }
  void undo(Document& doc) override { apply(doc, !remove); }
  void redo(Document& doc) override { apply(doc, remove); }
  size_t bytes() const override;
private:
  void apply(Document& doc, bool removing);
};

class FrameMove : public Action {
public:
  int from = 0, to = 0;
  FrameMove() { frame = -2; }
  void undo(Document& doc) override { apply(doc, to, from); }
  void redo(Document& doc) override { apply(doc, from, to); }
  size_t bytes() const override { return sizeof(*this); }
private:
  static void apply(Document& doc, int a, int b);
};

// Frame timing, frame rate and tags, swapped as a whole
struct AnimState {
  std::vector<Frame> frames;
  double fps = 12;
  std::vector<FrameTag> tags;
  static AnimState of(const Document& doc) { return { doc.frames, doc.fps, doc.tags }; }
};

class AnimEdit : public Action {
public:
  AnimState before, after;
  AnimEdit() { frame = -2; }
  void undo(Document& doc) override { apply(doc, before); }
  void redo(Document& doc) override { apply(doc, after); }
  size_t bytes() const override { return sizeof(*this) + (before.frames.size() + after.frames.size()) * sizeof(Frame); }
private:
  static void apply(Document& doc, const AnimState& s);
};

// One layer's cel at one frame swapped (link, unlink, clear)
class CelEdit : public Action {
public:
  uint64_t layerId = 0;
  int celFrame = 0;
  std::shared_ptr<Cel> before, after;
  void undo(Document& doc) override { apply(doc, before); }
  void redo(Document& doc) override { apply(doc, after); }
  size_t bytes() const override { return sizeof(*this); }
private:
  void apply(Document& doc, const std::shared_ptr<Cel>& c);
};
// Layers and frames swapped as a whole (layers to frames and back)
class StructureEdit : public Action {
public:
  struct State {
    std::vector<std::unique_ptr<Layer>> layers;
    AnimState anim;
    int activeLayer = 0, activeFrame = 0;
  };
  State before, after;
  StructureEdit() { frame = -2; }
  void undo(Document& doc) override { apply(doc, before); }
  void redo(Document& doc) override { apply(doc, after); }
  size_t bytes() const override;
  static State of(const Document& doc);
private:
  static void apply(Document& doc, const State& s);
};
#pragma endregion

#pragma region [ Frame operations ]
// A new frame after `at`: empty, a copy of frame `at`, or linked to it (shares its cels)
enum class ENewFrame : uint8_t { Empty, Duplicate, Linked };
void addFrame(Document& doc, int at, ENewFrame kind, const char* icon);
// The steps, not applied yet, for callers that collect them (scripts: one undo step per command)
std::unique_ptr<FrameInsert> makeAddFrame(const Document& doc, int at, ENewFrame kind);
std::unique_ptr<FrameInsert> makeDeleteFrame(const Document& doc, int f);
// Frames from..to (inclusive); the last frame can't be deleted
void deleteFrames(Document& doc, int from, int to, const char* icon);
// Frames from..to moved so the first lands at index dest (counted without the moved frames)
void moveFrames(Document& doc, int from, int to, int dest, const char* icon);
void reverseFrames(Document& doc, int from, int to, const char* icon);
// Duration in ms for frames from..to (0 = the frame rate)
void setFrameDuration(Document& doc, int from, int to, int ms, const char* icon);
void setFrameRate(Document& doc, double fps, const char* icon);
void setTags(Document& doc, std::vector<FrameTag> tags, const char* name, const char* icon);
// The layer's cels at from..to all become the cel of frame from
void linkCels(Document& doc, int layer, int from, int to, const char* icon);
// The layer's cel at frame f gets its own copy
void unlinkCel(Document& doc, int layer, int f, const char* icon);
void clearCel(Document& doc, int layer, int f, const char* icon);
// Every layer becomes a frame of one layer, bottom layer first (how GIF frames open in Paint.NET)
void layersToFrames(Document& doc, const char* icon);
// Every frame, flattened, becomes a layer; the document is one frame after
void framesToLayers(Document& doc, const char* icon);
#pragma endregion

#pragma region [ Playback ]
enum class ELoop : uint8_t { Loop, Once, PingPong };

// The frames a tag (or the whole document when tag is null) plays, in order for one pass
std::vector<int> playOrder(const Document& doc, const FrameTag* tag);

// Advances the active frame by real time: frame durations, loop mode, and an optional tag
class Player {
public:
  bool playing() const { return _playing; }
  // Starts at frame from (-1 = the active frame)
  void start(const Document& doc, const FrameTag* tag, int from = -1);
  int current() const { return _order.empty() ? 0 : _order[_pos]; }
  void stop() { _playing = false; }
  // Elapsed ms since the last call; returns the frame to show (or -1 when nothing changes)
  int advance(const Document& doc, double ms, ELoop loop, const FrameTag* tag);

private:
  bool _playing = false;
  double _left = 0;  // ms left on the current frame
  int _pos = 0, _dir = 1;
  std::vector<int> _order;
};
#pragma endregion

}
