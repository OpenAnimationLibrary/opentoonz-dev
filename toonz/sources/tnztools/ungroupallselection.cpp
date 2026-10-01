#include "tools/imagegrouping.h"

// TnzTools includes
#include "tools/strokeselection.h"
#include "tools/tool.h"
#include "tools/toolhandle.h"
#include "tools/toolutils.h"

// TnzQt includes
#include "toonzqt/dvdialog.h"
#include "toonzqt/menubarcommand.h"
#include "toonzqt/selectioncommandids.h"
#include "toonzqt/tselectionhandle.h"

// TnzLib includes
#include "toonz/txshlevelhandle.h"

// TnzCore includes
#include "tthreadmessage.h"
#include "tundo.h"
#include "tvectorimage.h"

// Qt includes
#include <QAction>
#include <QMutexLocker>

#include <algorithm>

//=============================================================================
namespace {
//-----------------------------------------------------------------------------
// Selection-scoped recursive ungroup
//
// The behavior in this file is adapted from Tahoma2D PR 2064 by @manongjohn:
// https://github.com/tahoma2d/tahoma2d/pull/2064
//
// Tahoma2D removes nested group IDs directly. OpenToonz uses the hierarchy
// snapshot mechanism established by this PR instead, so the operation can keep
// the entered outer group while reusing the same structural Undo model as the
// native OpenToonz foundation.
//-----------------------------------------------------------------------------

struct SelectedGroupSpan {
  int m_fromStroke;
  int m_count;
  int m_depth;
};

struct SelectedGroupingSnapshot {
  std::vector<SelectedGroupSpan> m_groups;
  int m_maxDepth;
  int m_enteredGroupDepth;
  int m_enteredGroupStroke;

  SelectedGroupingSnapshot()
      : m_maxDepth(0)
      , m_enteredGroupDepth(0)
      , m_enteredGroupStroke(-1) {}
};

SelectedGroupingSnapshot captureSelectedGrouping(TVectorImage *vimg) {
  SelectedGroupingSnapshot snapshot;
  const int strokeCount = (int)vimg->getStrokeCount();

  for (int i = 0; i < strokeCount; i++) {
    int depth = vimg->getGroupDepth(i);
    if (depth > snapshot.m_maxDepth) snapshot.m_maxDepth = depth;
  }

  for (int depth = 1; depth <= snapshot.m_maxDepth; depth++) {
    for (int i = 0; i < strokeCount;) {
      if (vimg->getGroupDepth(i) < depth) {
        i++;
        continue;
      }

      const int fromStroke = i++;
      while (i < strokeCount &&
             vimg->getCommonGroupDepth(fromStroke, i) >= depth)
        i++;

      SelectedGroupSpan span = {fromStroke, i - fromStroke, depth};
      snapshot.m_groups.push_back(span);
    }
  }

  snapshot.m_enteredGroupDepth = vimg->isInsideGroup();
  if (snapshot.m_enteredGroupDepth > 0) {
    for (int i = 0; i < strokeCount; i++) {
      if (vimg->isEnteredGroupStroke(i)) {
        snapshot.m_enteredGroupStroke = i;
        break;
      }
    }
  }

  return snapshot;
}

void ungroupEveryGroupWithoutUndo(TVectorImage *vimg) {
  bool foundGroup;
  do {
    foundGroup = false;
    for (int i = 0; i < (int)vimg->getStrokeCount();) {
      if (!vimg->isStrokeGrouped(i)) {
        i++;
        continue;
      }

      i += vimg->ungroup(i);
      foundGroup = true;
    }
  } while (foundGroup);
}

void restoreSelectedGroupingWithoutUndo(
    TVectorImage *vimg, const SelectedGroupingSnapshot &snapshot) {
  ungroupEveryGroupWithoutUndo(vimg);

  for (int depth = snapshot.m_maxDepth; depth > 0; depth--) {
    for (const SelectedGroupSpan &span : snapshot.m_groups) {
      if (span.m_depth == depth)
        vimg->group(span.m_fromStroke, span.m_count);
    }
  }

  if (snapshot.m_enteredGroupDepth > 0 &&
      snapshot.m_enteredGroupStroke >= 0) {
    for (int depth = 0; depth < snapshot.m_enteredGroupDepth; depth++)
      if (!vimg->enterGroup(snapshot.m_enteredGroupStroke)) break;
  }
}

bool spanContainsSelection(const SelectedGroupSpan &span,
                           StrokeSelection *selection) {
  for (int i = span.m_fromStroke; i < span.m_fromStroke + span.m_count; i++)
    if (selection->isSelected(i)) return true;
  return false;
}

SelectedGroupingSnapshot makeSelectionUngroupAllSnapshot(
    const SelectedGroupingSnapshot &source, StrokeSelection *selection) {
  SelectedGroupingSnapshot target = source;
  target.m_groups.clear();
  target.m_maxDepth = 0;

  for (const SelectedGroupSpan &span : source.m_groups) {
    // The entered hierarchy is the editing context, not part of the selected
    // group to flatten. Only group levels below that context are removed.
    if (span.m_depth > source.m_enteredGroupDepth &&
        spanContainsSelection(span, selection))
      continue;

    target.m_groups.push_back(span);
    target.m_maxDepth = std::max(target.m_maxDepth, span.m_depth);
  }

  return target;
}

//=============================================================================
// SelectedUngroupAllUndo
//-----------------------------------------------------------------------------

class SelectedUngroupAllUndo final : public ToolUtils::TToolUndo {
  SelectedGroupingSnapshot m_before;
  SelectedGroupingSnapshot m_after;

  void apply(const SelectedGroupingSnapshot &snapshot) const {
    TVectorImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;

    QMutexLocker lock(image->getMutex());
    restoreSelectedGroupingWithoutUndo(image.getPointer(), snapshot);
    notifyImageChanged();
  }

public:
  SelectedUngroupAllUndo(TXshSimpleLevel *level, const TFrameId &frameId,
                         const SelectedGroupingSnapshot &before,
                         const SelectedGroupingSnapshot &after)
      : ToolUtils::TToolUndo(level, frameId)
      , m_before(before)
      , m_after(after) {}

  void undo() const override { apply(m_before); }
  void redo() const override { apply(m_after); }

  int getSize() const override {
    return sizeof(*this) +
           (m_before.m_groups.capacity() + m_after.m_groups.capacity()) *
               sizeof(SelectedGroupSpan);
  }

  QString getToolName() override { return QObject::tr("Ungroup All"); }
};

//=============================================================================
// Command registration
//-----------------------------------------------------------------------------

class UngroupAllSelectedHandler final : public CommandHandlerInterface {
public:
  void execute() override {
    StrokeSelection *selection =
        dynamic_cast<StrokeSelection *>(TSelection::getCurrent());
    if (!selection) return;

    TGroupCommand *command = selection->getGroupCommand();
    if (command) command->ungroupSelectedAll();
  }
};

class UngroupAllSelectedActionsCreator final : public AuxActionsCreator {
public:
  void createActions(QObject *parent) override {
    QAction *action = new QAction(QObject::tr("Ungroup All"), parent);
    CommandManager *commandManager = CommandManager::instance();
    commandManager->define(MI_UngroupAllSelected, MenuEditCommandType, "",
                           action, "ungroup");
    commandManager->setHandler(MI_UngroupAllSelected,
                               new UngroupAllSelectedHandler());

    auto updateEnabled = [action](TSelection *selection) {
      action->setEnabled(dynamic_cast<StrokeSelection *>(selection) != nullptr);
    };

    updateEnabled(TSelection::getCurrent());
    QObject::connect(
        TSelectionHandle::getCurrent(), &TSelectionHandle::selectionSwitched,
        action, [updateEnabled](TSelection *, TSelection *current) {
          updateEnabled(current);
        });
  }
};

UngroupAllSelectedActionsCreator s_ungroupAllSelectedActionsCreator;

//-----------------------------------------------------------------------------
}  // namespace
//-----------------------------------------------------------------------------

void TGroupCommand::ungroupSelectedAll() {
  // Match ordinary Ungroup eligibility: the selected strokes must represent
  // complete group(s) at the current entered-group depth.
  if (!(getGroupingOptions() & UNGROUP)) return;

  TTool *tool = TTool::getApplication()->getCurrentTool()->getTool();
  if (!tool) return;

  TVectorImage *vimg = (TVectorImage *)tool->getImage(true);
  if (!vimg) return;

  if (!m_sel || !m_sel->isEditable()) {
    DVGui::error(
        QObject::tr("The selection cannot be ungrouped. It is not editable."));
    return;
  }

  QMutexLocker lock(vimg->getMutex());
  SelectedGroupingSnapshot before = captureSelectedGrouping(vimg);
  SelectedGroupingSnapshot target =
      makeSelectionUngroupAllSnapshot(before, m_sel);

  restoreSelectedGroupingWithoutUndo(vimg, target);
  SelectedGroupingSnapshot after = captureSelectedGrouping(vimg);
  tool->notifyImageChanged();

  TXshSimpleLevel *level =
      TTool::getApplication()->getCurrentLevel()->getSimpleLevel();
  TUndoManager::manager()->add(new SelectedUngroupAllUndo(
      level, tool->getCurrentFid(), before, after));
}
