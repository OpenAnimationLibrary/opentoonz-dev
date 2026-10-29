#include "toonzqt/menubarcommand.h"
#include "tundo.h"
#include "toonzqt/selection.h"
#include "../../tnztools/selectionrepeat.h"
#include "../../tnztools/animaterepeat.h"

#include <QApplication>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <vector>

namespace {

void check(bool condition, const char *message) {
  if (condition) return;
  std::cerr << message << std::endl;
  std::exit(1);
}

class Handler final : public CommandHandlerInterface {
  std::function<void()> m_run;

public:
  explicit Handler(std::function<void()> run) : m_run(run) {}
  void execute() override { m_run(); }
};

// Like vector ordering undo items, redo restores a saved result. Repeating this
// snapshot would fail to move the selected stroke another position backward.
class OrderUndo final : public TUndo {
  std::vector<int> &m_order;
  std::vector<int> m_before, m_after;

public:
  OrderUndo(std::vector<int> &order, const std::vector<int> &before)
      : m_order(order), m_before(before), m_after(order) {}
  int getSize() const override { return sizeof(*this); }
  void undo() const override { m_order = m_before; }
  void redo() const override { m_order = m_after; }
};

class ValueUndo final : public TUndo {
  int &m_value;
  int m_before, m_after;

public:
  ValueUndo(int &value, int before)
      : m_value(value), m_before(before), m_after(value) {}
  int getSize() const override { return sizeof(*this); }
  void undo() const override { m_value = m_before; }
  void redo() const override { m_value = m_after; }
};

class EditorSelection final : public TSelection {
  bool m_safe;

public:
  int edits = 0;
  explicit EditorSelection(bool safe) : m_safe(safe) {}
  bool isEmpty() const override { return false; }
  void selectNone() override {}
  void edit() { ++edits; }
  void step(int value) { edits += value; }
  void enableCommands() override {
    if (m_safe) {
      enableRepeatableCommand(this, "scoped", &EditorSelection::edit);
      enableRepeatableCommand(this, "scopedStep", &EditorSelection::step, 3);
    } else {
      enableCommand(this, "scoped", &EditorSelection::edit);
      enableCommand(this, "scopedStep", &EditorSelection::step, 3);
    }
  }
};

}  // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  CommandManager *cm = CommandManager::instance();
  TUndoManager *um   = TUndoManager::manager();
  um->setUndoMemorySize(10);

  auto define = [&](const char *id, const char *name,
                    std::function<void()> run) {
    auto action = new DVAction(name, &app);
    cm->define(id, MenuEditCommandType, "", action);
    cm->setHandler(id, new Handler(run));
    return action;
  };

  std::vector<int> order = {0, 1, 2, 3, 4};
  int selected           = 4;
  auto moveBack          = [&]() {
    auto position = std::find(order.begin(), order.end(), selected);
    if (position == order.begin() || position == order.end()) return;
    auto before = order;
    std::iter_swap(position, position - 1);
    um->beginBlock();
    um->add(new OrderUndo(order, before));
    um->endBlock();
  };

  QAction *back        = define("back", "&Move Back One", moveBack);
  QAction *repeat      = define("repeat", "&Repeat", []() {});
  QAction *navigate    = define("navigate", "Navigate", []() {});
  QAction *unsupported = define("unsupported", "Other Edit", moveBack);
  cm->setRepeatCommand("repeat");
  cm->setRepeatable("back");
  check(!repeat->isEnabled(), "Repeat must start disabled");
  cm->execute("repeat");
  check(order.back() == 4, "Empty Repeat must do nothing");

  back->trigger();
  check(order == std::vector<int>({0, 1, 2, 4, 3}), "Initial ordering failed");
  check(repeat->isEnabled(), "Repeat must become available");
  check(repeat->toolTip().contains("Move Back One"), "Missing Repeat target");
  repeat->trigger();
  cm->execute("repeat");
  check(order == std::vector<int>({0, 4, 1, 2, 3}),
        "Successive Repeat must execute a fresh relative edit");
  check(um->getHistoryCount() == 3, "Each Repeat needs a separate undo step");
  um->undo();
  check(order == std::vector<int>({0, 1, 4, 2, 3}), "Undo lost a Repeat step");
  check(repeat->isEnabled(), "History-panel Undo must preserve Repeat");
  um->redo();
  check(order == std::vector<int>({0, 4, 1, 2, 3}), "Redo lost a Repeat step");
  check(repeat->isEnabled() && repeat->toolTip().contains("Move Back One"),
        "Redo must preserve the original Repeat target");

  // A changed selection is used when Repeat runs; navigation preserves it.
  cm->execute("back");
  selected = 3;
  navigate->trigger();
  repeat->trigger();
  check(order == std::vector<int>({4, 0, 1, 3, 2}),
        "Repeat must use the current selection after navigation");

  // Selection-specific handlers may be replaced or temporarily unavailable.
  cm->enable("back", false);
  check(!repeat->isEnabled(), "Disabled target must disable Repeat");
  auto unchanged = order;
  cm->execute("repeat");
  check(order == unchanged, "Programmatic Repeat bypassed target state");
  cm->enable("back", true);
  cm->setHandler("back", nullptr);
  check(!repeat->isEnabled(), "Missing handler must disable Repeat");
  int replacementRuns = 0;
  cm->setHandler("back", new Handler([&]() { ++replacementRuns; }));
  repeat->trigger();
  check(replacementRuns == 1, "Repeat used a stale selection handler");
  cm->setHandler("back", new Handler(moveBack));

  unsupported->trigger();
  check(repeat->isEnabled() && repeat->toolTip().contains("Move Back One"),
        "Unsupported edits must preserve the old target");
  back->trigger();
  auto before = order;
  um->add(
      new OrderUndo(order, before));  // e.g. a brush gesture, outside dispatch
  check(repeat->isEnabled(),
        "Unsupported tool edits must preserve the old target");

  // Nested helpers belong to the outer registered command.
  QAction *outer =
      define("outer", "Outer Edit", [&]() { cm->execute("unsupported"); });
  cm->setRepeatable("outer");
  selected = 2;
  outer->trigger();
  check(repeat->toolTip().contains("Outer Edit"), "Nested helper stole Repeat");
  repeat->trigger();
  check(order == std::vector<int>({4, 3, 2, 0, 1}), "Nested Repeat failed");

  // A target cannot recursively invoke Repeat while it is being repeated.
  int recursiveRuns = 0;
  cm->setHandler("outer", new Handler([&]() {
                   ++recursiveRuns;
                   cm->repeatLastCommand();
                 }));
  repeat->trigger();
  check(recursiveRuns == 1, "Repeat recursed into itself");

  // Tracking must work when history is capped and its item count cannot grow.
  um->setUndoMemorySize(0);
  cm->execute("back");
  check(repeat->isEnabled(), "Capped history prevented command tracking");
  repeat->trigger();
  check(order.front() == selected && um->getHistoryCount() == 1,
        "Repeat failed while old undo items were being evicted");
  auto boundary = order;
  repeat->trigger();
  check(order == boundary && um->getHistoryCount() == 1,
        "Repeat at the ordering boundary must not change history");
  um->reset();
  check(repeat->isEnabled(), "Undo-memory reset on Save must preserve Repeat");
  cm->clearRepeat();  // application sceneSwitched handler
  check(!repeat->isEnabled(), "Scene changes must clear Repeat");
  um->setUndoMemorySize(10);

  // Repeat permission belongs to the current editor, not a shared clipboard ID.
  QAction *scoped     = define("scoped", "Scoped Paste", []() {});
  QAction *scopedStep = define("scopedStep", "Scoped Step", []() {});
  {
    EditorSelection drawing(true), browser(false);
    TSelection::setCurrent(&drawing);
    scoped->trigger();
    repeat->trigger();
    check(drawing.edits == 2, "Editor-specific repeatable command failed");
    TSelection::setCurrent(&browser);
    check(!repeat->isEnabled(), "File browser inherited Repeat permission");
    cm->execute("repeat");
    check(browser.edits == 0, "Repeat invoked an ineligible current handler");
    TSelection::setCurrent(&drawing);
    scopedStep->trigger();
    repeat->trigger();
    check(drawing.edits == 8, "Repeat lost the parameterized editor handler");
    TSelection::setCurrent(nullptr);
  }

  // An Apply operation captures values, then resolves current targets at
  // replay.
  int first = 0, second = 0, setting = 7;
  int *current  = &first;
  bool editable = true;
  QObject context;
  const auto remember = [&](QObject *owner, int amount) {
    cm->recordRepeatAction(
        owner, "Parameter Edit",
        [&, amount]() {
          int before = *current;
          *current += amount;
          um->add(new ValueUndo(*current, before));
        },
        [&]() { return editable; });
  };
  remember(&context, setting);
  // An unrelated edit between recording and switching frames must not erase it.
  int unrelated = 1;
  um->add(new ValueUndo(unrelated, 0));
  setting = 42;  // subsequent UI changes do not alter the applied parameters
  repeat->trigger();
  current = &second;
  repeat->trigger();
  check(first == 7 && second == 7,
        "Repeat lost captured parameters/current target");
  check(
      um->getHistoryCount() == 3,
      "Parameter replay must add normal undo steps alongside unrelated edits");
  um->undo();
  check(second == 0 && repeat->isEnabled(),
        "Parameter Undo must preserve Repeat");
  um->redo();
  check(second == 7, "Parameter redo failed");
  remember(&context, 2);
  editable = false;
  cm->refreshRepeatAction();
  check(!repeat->isEnabled(), "Invalid parameter target must disable Repeat");
  cm->execute("repeat");
  check(second == 7, "Parameter replay ignored eligibility");
  editable = true;
  cm->refreshRepeatAction();
  repeat->trigger();
  check(second == 9,
        "Parameter recipe did not survive a temporary invalid target");

  // Recipes can replace themselves; destroying their UI owner clears them.
  int recipeRuns = 0;
  cm->recordRepeatAction(
      &context, "Replace Recipe",
      [&]() {
        ++recipeRuns;
        remember(&context, 3);
        cm->repeatLastCommand();  // re-entrant Repeat is ignored
      },
      []() { return true; });
  repeat->trigger();
  repeat->trigger();
  check(recipeRuns == 1 && second == 12,
        "Recipe replacement or recursion guard failed");
  {
    QObject temporary;
    remember(&temporary, 9);
  }
  check(!repeat->isEnabled(), "Destroyed recipe owner must clear Repeat");
  cm->execute("repeat");
  check(second == 12, "Repeat invoked a destroyed owner");
  remember(&context, 3);
  selected = 1;
  back->trigger();
  check(repeat->toolTip().contains("Move Back One"),
        "Command failed to replace recipe");
  remember(&context, 3);
  um->reset();
  check(repeat->isEnabled(), "Undo-memory reset failed to preserve a recipe");
  cm->clearRepeat();
  check(!repeat->isEnabled(), "Scene change failed to clear a recipe");

  // Use production affine capture/rebasing: four repeated quarter turns return
  // to the starting point, scaling compounds, and nudges retain world distance.
  const auto near = [](TPointD a, TPointD b) { return norm2(a - b) < 1e-16; };
  TAffine affine;
  check(SelectionRepeat::capture({0, 0}, {2, 0}, {0, 2}, {0, 0}, {0, 2},
                                 {-2, 0}, affine),
        "Rotation capture failed");
  TPointD point(11, 10), center(10, 10);
  TAffine rotation = SelectionRepeat::rebase(affine, {0, 0}, center);
  check(near(rotation * point, {10, 11}),
        "Repeat did not use the current pivot");
  for (int i = 0; i < 4; ++i) point = rotation * point;
  check(near(point, {11, 10}), "Repeated rotation did not compound");
  check(SelectionRepeat::capture({0, 0}, {2, 0}, {0, 2}, {0, 0}, {4, 0}, {0, 4},
                                 affine),
        "Scale capture failed");
  TAffine scale = SelectionRepeat::rebase(affine, {0, 0}, center);
  check(near(scale * (scale * point), {14, 10}),
        "Repeated scale did not compound");
  TAffine nudge = SelectionRepeat::rebase(TTranslation(7, -3), {1, 2}, center);
  check(near(nudge * center, {17, 7}),
        "Nudge distance changed with selection pivot");
  check(!SelectionRepeat::capture({0, 0}, {0, 0}, {0, 2}, {0, 0}, {1, 0},
                                  {0, 2}, affine),
        "Singular selection frame must not be repeatable");
  // Animate gestures repeat differences/factors; text entry repeats a value.
  double value = 0;
  auto move    = AnimateRepeat::Value::capture(2.0, 5.0, false, false);
  check(move.apply(10.0, value) && value == 13.0,
        "Animate move must use the current frame/object value");
  check(move.apply(value, value) && value == 16.0,
        "Animate move must accumulate its original offset");
  auto turn = AnimateRepeat::Value::capture(15.0, 20.0, false, false);
  check(turn.apply(30.0, value) && value == 35.0,
        "Animate rotation must retain the angular difference");
  auto grow = AnimateRepeat::Value::capture(0.5, 0.75, true, false);
  check(grow.apply(2.0, value) && value == 3.0 && grow.apply(value, value) &&
            value == 4.5,
        "Animate scale must compound its factor on the current target");
  auto flip = AnimateRepeat::Value::capture(1.0, -1.0, true, false);
  check(flip.apply(2.0, value) && value == -2.0 && flip.apply(value, value) &&
            value == 2.0,
        "Signed scale repeat failed");
  auto set = AnimateRepeat::Value::capture(0.5, 0.75, true, true);
  check(set.apply(2.0, value) && value == 0.75 && set.apply(value, value) &&
            value == 0.75,
        "Typed scale values must remain absolute");
  auto fromZero = AnimateRepeat::Value::capture(0.0, 0.5, true, false);
  check(fromZero.apply(2.0, value) && value == 0.5,
        "Scale from zero must avoid an undefined ratio");
  auto overflow = AnimateRepeat::Value::capture(1.0, 1e300, true, false);
  check(!overflow.apply(1e300, value), "Animate Repeat must reject overflow");
  std::cout << "Repeat command tests passed" << std::endl;
}
