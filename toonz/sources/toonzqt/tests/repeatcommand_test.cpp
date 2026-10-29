#include "toonzqt/menubarcommand.h"
#include "tundo.h"

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
  check(!repeat->isEnabled(), "History-panel Undo must clear Repeat");
  um->redo();
  check(order == std::vector<int>({0, 4, 1, 2, 3}), "Redo lost a Repeat step");
  check(!repeat->isEnabled(), "Redo must not become a Repeat target");

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
  check(!repeat->isEnabled(), "Unsupported edits must clear the old target");
  back->trigger();
  auto before = order;
  um->add(
      new OrderUndo(order, before));  // e.g. a brush gesture, outside dispatch
  check(!repeat->isEnabled(), "Tool edits must clear the old target");

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
  check(!repeat->isEnabled(), "Scene/history reset must clear Repeat");
  std::cout << "Repeat command tests passed" << std::endl;
}
