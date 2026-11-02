#include "core/History.h"
#include "core/Document.h"

namespace david {

void History::push(Document& doc, std::unique_ptr<Action> action) {
  while(int(_steps.size()) > _index) {
    _bytes -= _steps.back()->bytes();
    _steps.pop_back();
  }
  if(_cleanIndex > _index) _cleanIndex = -1;
  if(action->frame == -1) action->frame = doc.activeFrame;
  _bytes += action->bytes();
  _steps.push_back(std::move(action));
  _index++;

  // Drop the oldest steps over the memory cap, but always keep the newest one
  while(_bytes > _cap && _steps.size() > 1) {
    _bytes -= _steps.front()->bytes();
    _steps.erase(_steps.begin());
    _index--;
    _cleanIndex = _cleanIndex > 0 ? _cleanIndex - 1 : -1;
  }
  doc.onChanged();
}

// Undoing a step made on another frame first goes to that frame, so the change is seen
static void showFrame(Document& doc, const Action& a) {
  if(a.frame >= 0 && a.frame < doc.frameCount() && a.frame != doc.activeFrame) doc.setActiveFrame(a.frame);
}

bool History::undo(Document& doc) {
  if(!canUndo()) return false;
  showFrame(doc, *_steps[_index - 1]);
  _steps[--_index]->undo(doc);
  doc.onChanged();
  return true;
}

bool History::redo(Document& doc) {
  if(!canRedo()) return false;
  showFrame(doc, *_steps[_index]);
  _steps[_index++]->redo(doc);
  doc.onChanged();
  return true;
}

void History::goTo(Document& doc, int index) {
  while(_index > index && undo(doc));
  while(_index < index && redo(doc));
}

void History::clear() {
  _steps.clear();
  _index = 0;
  _cleanIndex = 0;
  _bytes = 0;
}

}
