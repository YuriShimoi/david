#pragma once
#include <memory>
#include <string>
#include <vector>

namespace david {

class Document;

// One undoable step. The History window shows its name and icon.
class Action {
public:
  std::string name;
  const char* icon = "";
  // The animation frame the step was made on (set when pushed); undo and redo show that frame again.
  // -2: steps that change the frames themselves and put the active frame back on their own.
  int frame = -1;

  virtual ~Action() = default;
  virtual void undo(Document& doc) = 0;
  virtual void redo(Document& doc) = 0;
  virtual size_t bytes() const = 0;
};

class History {
public:
  void push(Document& doc, std::unique_ptr<Action> action);
  bool undo(Document& doc);
  bool redo(Document& doc);
  void goTo(Document& doc, int index);
  void clear();

  bool canUndo() const { return _index > 0; }
  bool canRedo() const { return _index < int(_steps.size()); }
  int index() const { return _index; }
  int count() const { return int(_steps.size()); }
  const Action& at(int i) const { return *_steps[i]; }

  // The clean index is the step the file was last saved at; -1 when that step was dropped
  void markClean() { _cleanIndex = _index; }
  bool isClean() const { return _cleanIndex == _index; }

  size_t bytes() const { return _bytes; }
  void setCap(size_t bytes) { _cap = bytes; }

private:
  std::vector<std::unique_ptr<Action>> _steps;
  int _index = 0;
  int _cleanIndex = 0;
  size_t _bytes = 0;
  size_t _cap = size_t(512) << 20;
};

}
