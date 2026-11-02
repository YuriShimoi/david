#include "core/Animation.h"
#include "core/Composite.h"
#include "core/Text.h"
#include <algorithm>

namespace david {

#pragma region [ Undo steps ]
size_t FrameInsert::bytes() const {
  size_t n = sizeof(*this);
  for(auto& [id, c] : cels)
    if(c) for(auto& t : c->tiles) if(t) n += Tile::bytes();
  return n;
}

void FrameInsert::apply(Document& doc, bool removing) {
  if(removing) {
    doc.frames.erase(doc.frames.begin() + index);
    for(auto& l : doc.layers) l->removeCel(index);
    doc.tags = tagsWithout;
    doc.setActiveFrame(std::min(index, doc.frameCount() - 1));
  } else {
    doc.frames.insert(doc.frames.begin() + index, meta);
    for(auto& l : doc.layers) {
      auto it = cels.find(l->id());
      l->insertCel(index, it == cels.end() ? nullptr : it->second);
    }
    doc.tags = tagsWith;
    doc.setActiveFrame(index);
  }
}

void FrameMove::apply(Document& doc, int a, int b) {
  Frame f = doc.frames[a];
  doc.frames.erase(doc.frames.begin() + a);
  doc.frames.insert(doc.frames.begin() + b, f);
  for(auto& l : doc.layers) l->moveCel(a, b);
  doc.setActiveFrame(b);
}

void AnimEdit::apply(Document& doc, const AnimState& s) {
  doc.frames = s.frames;
  doc.fps = s.fps;
  doc.tags = s.tags;
}

void CelEdit::apply(Document& doc, const std::shared_ptr<Cel>& c) {
  if(Layer* l = doc.findLayer(layerId); l && celFrame < l->frames()) l->setCel(celFrame, c);
}

size_t StructureEdit::bytes() const {
  size_t n = sizeof(*this);
  for(auto* s : { &before, &after }) for(auto& l : s->layers) n += l->allocatedBytes();
  return n;
}

StructureEdit::State StructureEdit::of(const Document& doc) {
  State s;
  for(auto& l : doc.layers) s.layers.push_back(l->clone(true));
  s.anim = AnimState::of(doc);
  s.activeLayer = doc.activeLayer;
  s.activeFrame = doc.activeFrame;
  return s;
}

void StructureEdit::apply(Document& doc, const State& s) {
  doc.layers.clear();
  for(auto& l : s.layers) doc.layers.push_back(l->clone(true));
  doc.frames = s.anim.frames;
  doc.fps = s.anim.fps;
  doc.tags = s.anim.tags;
  doc.activeLayer = std::clamp(s.activeLayer, 0, int(doc.layers.size()) - 1);
  doc.setActiveFrame(s.activeFrame);
}
#pragma endregion

#pragma region [ Frame operations ]
// Tags after inserting (+1) or removing (-1) the frame at index
static std::vector<FrameTag> shiftTags(std::vector<FrameTag> tags, int index, int delta) {
  std::vector<FrameTag> out;
  for(auto t : tags) {
    if(delta > 0) {
      if(t.from >= index) t.from++;
      if(t.to >= index) t.to++;
    } else {
      if(t.from > index) t.from--;
      if(t.to >= index) t.to--;
      if(t.to < t.from) continue;  // the tag's only frame went away
    }
    out.push_back(t);
  }
  return out;
}

std::unique_ptr<FrameInsert> makeAddFrame(const Document& doc, int at, ENewFrame kind) {
  at = std::clamp(at, 0, doc.frameCount() - 1);
  auto a = std::make_unique<FrameInsert>();
  a->name = kind == ENewFrame::Empty ? N_("New Frame") : kind == ENewFrame::Linked ? N_("New Linked Frame") : N_("Duplicate Frame");
  a->index = at + 1;
  a->meta = kind == ENewFrame::Empty ? Frame{} : doc.frames[at];
  for(auto& l : doc.layers) {
    if(kind == ENewFrame::Duplicate) a->cels[l->id()] = l->copyCel(at);
    else if(kind == ENewFrame::Linked) a->cels[l->id()] = l->cel(at);
  }
  a->tagsWithout = doc.tags;
  a->tagsWith = shiftTags(doc.tags, at + 1, 1);
  // A new frame right after a tag's last frame joins the tag
  for(auto& t : a->tagsWith) if(t.to == at) t.to = at + 1;
  return a;
}

void addFrame(Document& doc, int at, ENewFrame kind, const char* icon) {
  auto a = makeAddFrame(doc, at, kind);
  a->icon = icon;
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}

std::unique_ptr<FrameInsert> makeDeleteFrame(const Document& doc, int f) {
  auto a = std::make_unique<FrameInsert>();
  a->name = N_("Delete Frame");
  a->index = f;
  a->remove = true;
  a->meta = doc.frames[f];
  for(auto& l : doc.layers) if(l->cel(f)) a->cels[l->id()] = l->cel(f);
  a->tagsWith = doc.tags;
  a->tagsWithout = shiftTags(doc.tags, f, -1);
  return a;
}

void deleteFrames(Document& doc, int from, int to, const char* icon) {
  from = std::max(0, from);
  to = std::min(to, doc.frameCount() - 1);
  if(to < from || to - from + 1 >= doc.frameCount()) return;
  auto step = std::make_unique<CompoundAction>();
  step->name = to > from ? N_("Delete Frames") : N_("Delete Frame");
  step->icon = icon;
  step->frame = -2;
  for(int f = to; f >= from; f--) {
    auto a = makeDeleteFrame(doc, f);
    a->redo(doc);
    step->parts.push_back(std::move(a));
  }
  doc.history.push(doc, std::move(step));
}

void moveFrames(Document& doc, int from, int to, int dest, const char* icon) {
  int n = doc.frameCount();
  from = std::max(0, from);
  to = std::min(to, n - 1);
  int count = to - from + 1;
  dest = std::clamp(dest, 0, n - count);
  if(count <= 0 || dest == from) return;
  auto step = std::make_unique<CompoundAction>();
  step->name = count > 1 ? N_("Move Frames") : N_("Move Frame");
  step->icon = icon;
  step->frame = -2;
  auto move = [&](int a, int b) {
    auto m = std::make_unique<FrameMove>();
    m->from = a; m->to = b;
    m->redo(doc);
    step->parts.push_back(std::move(m));
  };
  // One frame at a time, in an order that keeps the block together
  if(dest < from) for(int i = 0; i < count; i++) move(from + i, dest + i);
  else for(int i = count - 1; i >= 0; i--) move(from + i, dest + i);
  doc.setActiveFrame(dest);
  doc.history.push(doc, std::move(step));
}

void reverseFrames(Document& doc, int from, int to, const char* icon) {
  from = std::max(0, from);
  to = std::min(to, doc.frameCount() - 1);
  if(to <= from) return;
  auto step = std::make_unique<CompoundAction>();
  step->name = N_("Reverse Frames");
  step->icon = icon;
  step->frame = -2;
  // The last frame of the range moves to the front, then the next to second place, and so on
  for(int i = 0; i < to - from; i++) {
    auto m = std::make_unique<FrameMove>();
    m->from = to; m->to = from + i;
    m->redo(doc);
    step->parts.push_back(std::move(m));
  }
  doc.history.push(doc, std::move(step));
}

static void pushAnim(Document& doc, const AnimState& after, const char* name, const char* icon) {
  auto a = std::make_unique<AnimEdit>();
  a->name = name;
  a->icon = icon;
  a->before = AnimState::of(doc);
  a->after = after;
  if(a->before.frames == after.frames && a->before.fps == after.fps && a->before.tags == after.tags) return;
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}

void setFrameDuration(Document& doc, int from, int to, int ms, const char* icon) {
  AnimState s = AnimState::of(doc);
  for(int f = std::max(0, from); f <= std::min(to, doc.frameCount() - 1); f++) s.frames[f].duration = std::max(0, ms);
  pushAnim(doc, s, N_("Frame Duration"), icon);
}

void setFrameRate(Document& doc, double fps, const char* icon) {
  AnimState s = AnimState::of(doc);
  s.fps = std::clamp(fps, 1.0, 60.0);
  pushAnim(doc, s, N_("Frame Rate"), icon);
}

void setTags(Document& doc, std::vector<FrameTag> tags, const char* name, const char* icon) {
  AnimState s = AnimState::of(doc);
  s.tags = std::move(tags);
  pushAnim(doc, s, name, icon);
}

static void pushCel(CompoundAction& step, Document& doc, Layer& l, int f, std::shared_ptr<Cel> after) {
  auto a = std::make_unique<CelEdit>();
  a->layerId = l.id();
  a->celFrame = f;
  a->before = l.cel(f);
  a->after = std::move(after);
  a->redo(doc);
  step.parts.push_back(std::move(a));
}

void linkCels(Document& doc, int layer, int from, int to, const char* icon) {
  Layer& l = *doc.layers[layer];
  from = std::max(0, from);
  to = std::min(to, doc.frameCount() - 1);
  if(to <= from) return;
  // An empty first cel gets an image to share, so drawing on any linked frame shows on all
  l.ensureCel(from);
  auto step = std::make_unique<CompoundAction>();
  step->name = N_("Link Cels");
  step->icon = icon;
  for(int f = from + 1; f <= to; f++) if(l.cel(f) != l.cel(from)) pushCel(*step, doc, l, f, l.cel(from));
  if(!step->parts.empty()) doc.history.push(doc, std::move(step));
}

void unlinkCel(Document& doc, int layer, int f, const char* icon) {
  Layer& l = *doc.layers[layer];
  bool shared = false;
  for(int g = 0; g < l.frames(); g++) shared |= l.linked(f, g);
  if(!shared) return;
  auto step = std::make_unique<CompoundAction>();
  step->name = N_("Unlink Cel");
  step->icon = icon;
  pushCel(*step, doc, l, f, l.copyCel(f));
  doc.history.push(doc, std::move(step));
}

void clearCel(Document& doc, int layer, int f, const char* icon) {
  Layer& l = *doc.layers[layer];
  if(!l.cel(f)) return;
  auto step = std::make_unique<CompoundAction>();
  step->name = N_("Clear Cel");
  step->icon = icon;
  pushCel(*step, doc, l, f, nullptr);
  doc.history.push(doc, std::move(step));
}
static void pushStructure(Document& doc, StructureEdit::State after, const char* name, const char* icon) {
  auto a = std::make_unique<StructureEdit>();
  a->name = name;
  a->icon = icon;
  a->before = StructureEdit::of(doc);
  a->after = std::move(after);
  a->redo(doc);
  doc.history.push(doc, std::move(a));
}

void layersToFrames(Document& doc, const char* icon) {
  if(doc.layers.size() < 2) return;
  StructureEdit::State s;
  auto l = std::make_unique<Layer>(doc.width(), doc.height(), "Frames");
  int n = int(doc.layers.size());
  l->fitFrames(n, 0);
  for(int i = 0; i < n; i++) l->setCel(i, doc.layers[i]->copyCel(doc.activeFrame));
  s.layers.push_back(std::move(l));
  s.anim = { std::vector<Frame>(size_t(n)), doc.fps, {} };
  pushStructure(doc, std::move(s), N_("Layers to Frames"), icon);
}

void framesToLayers(Document& doc, const char* icon) {
  if(doc.frameCount() < 2) return;
  StructureEdit::State s;
  std::vector<uint32_t> row(doc.width());
  for(int f = 0; f < doc.frameCount(); f++) {
    auto l = std::make_unique<Layer>(doc.width(), doc.height(), std::string("Frame ") + std::to_string(f + 1));
    for(int y = 0; y < doc.height(); y++) {
      flattenRow(doc, y, row.data(), f);
      for(int x = 0; x < doc.width(); x++) if(row[x]) l->set(x, y, row[x]);
    }
    s.layers.push_back(std::move(l));
  }
  s.anim = { { Frame{} }, doc.fps, {} };
  s.activeLayer = doc.activeFrame;
  pushStructure(doc, std::move(s), N_("Frames to Layers"), icon);
}
#pragma endregion

#pragma region [ Playback ]
static std::vector<int> baseOrder(const Document& doc, const FrameTag* tag) {
  std::vector<int> order;
  int from = 0, to = doc.frameCount() - 1;
  if(tag) { from = std::clamp(tag->from, 0, to); to = std::clamp(tag->to, from, to); }
  for(int f = from; f <= to; f++) order.push_back(f);
  if(tag && tag->direction == ETagDirection::Reverse) std::reverse(order.begin(), order.end());
  return order;
}

std::vector<int> playOrder(const Document& doc, const FrameTag* tag) {
  std::vector<int> order = baseOrder(doc, tag);
  // Ping-pong goes back without repeating the end frames
  if(tag && tag->direction == ETagDirection::PingPong)
    for(int i = int(order.size()) - 2; i > 0; i--) order.push_back(order[i]);
  return order;
}

void Player::start(const Document& doc, const FrameTag* tag, int from) {
  _order = baseOrder(doc, tag);
  auto it = std::find(_order.begin(), _order.end(), from < 0 ? doc.activeFrame : from);
  _pos = it == _order.end() ? 0 : int(it - _order.begin());
  _dir = 1;
  _left = doc.frameMs(_order[_pos]);
  _playing = true;
}

int Player::advance(const Document& doc, double ms, ELoop loop, const FrameTag* tag) {
  if(!_playing) return -1;
  // The document changed under playback (frames deleted): start over from the current frame
  if(_order != baseOrder(doc, tag)) start(doc, tag, current());
  if(_order.size() < 2) return -1;
  bool bounce = loop == ELoop::PingPong || (tag && tag->direction == ETagDirection::PingPong);
  int last = int(_order.size()) - 1, start = _pos;
  _left -= ms;
  // Several frames can pass in one step (short durations, a slow app frame), but at most one full pass
  for(int guard = 0; _left <= 0 && guard <= last * 2; guard++) {
    int next = _pos + _dir;
    if(next > last || next < 0) {
      if(bounce) { _dir = -_dir; next = _pos + _dir; }
      else if(loop == ELoop::Once) { _playing = false; break; }
      else next = 0;
    }
    _pos = next;
    _left += doc.frameMs(_order[_pos]);
  }
  if(_left <= 0) _left = doc.frameMs(_order[_pos]);
  return _pos != start || !_playing ? _order[_pos] : -1;
}
#pragma endregion

}
