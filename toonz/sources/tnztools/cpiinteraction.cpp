#include "cpitool.h"
#include "selectiontool.h"
#include "tools/toolhandle.h"
#include "tools/cursors.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "tgl.h"
#include <QKeyEvent>
#include <algorithm>
#include <memory>

namespace {
using namespace DragSelectionTool;
class CpiInteraction;
class CpiTransform final : public DeformTool {
  CpiTool &m_session;
  FourPoints m_startBox;
  TAffine m_affine;
  std::unique_ptr<Rotation> m_rotation;
  std::unique_ptr<Scale> m_scale;
  std::unique_ptr<MoveSelection> m_move;

public:
  enum Kind { Move, Rotate, Resize, ResizeX, ResizeY };
  CpiTransform(SelectionTool *tool, CpiTool &session, Kind kind)
      : DeformTool(tool), m_session(session), m_startBox(tool->getBBox()) {
    m_startScaleValue = TPointD(1, 1);
    if (kind == Move)
      m_move.reset(new MoveSelection(this));
    else if (kind == Rotate)
      m_rotation.reset(new Rotation(this));
    else
      m_scale.reset(new Scale(this, kind == ResizeX   ? ScaleType::HORIZONTAL
                                    : kind == ResizeY ? ScaleType::VERTICAL
                                                      : ScaleType::GLOBAL));
  }
  void leftButtonDown(const TPointD &pos, const TMouseEvent &e) override {
    DeformTool::leftButtonDown(pos, e);
    if (m_move) m_move->leftButtonDown(pos, e);
    if (m_scale) m_scale->leftButtonDown(pos, e);
  }
  void leftButtonDrag(const TPointD &pos, const TMouseEvent &e) override {
    if (m_move) m_move->leftButtonDrag(pos, e);
    if (m_rotation) m_rotation->leftButtonDrag(pos, e);
    if (m_scale) m_scale->leftButtonDrag(pos, e);
  }
  void addTransformUndo() override {}
  void draw() override {
    if (m_rotation) m_rotation->draw();
  }
  void transform(TAffine affine) override { transform(affine, 0); }
  void transform(TAffine affine, double) override {
    auto next = affine * m_affine;
    if (!m_session.transform(next)) return;
    m_affine = next;
    m_tool->setBBox(m_startBox * m_affine);
    m_tool->setCenter(affine * m_tool->getCenter());
  }
  void applyTransform(FourPoints box, bool = false) override {
    TPointD origin = m_startBox.getP00();
    double width   = m_startBox.getP10().x - origin.x;
    double height  = m_startBox.getP01().y - origin.y;
    if (std::abs(width) < 1e-10 || std::abs(height) < 1e-10) return;
    auto x         = (box.getP10() - box.getP00()) * (1.0 / width);
    auto y         = (box.getP01() - box.getP00()) * (1.0 / height);
    TAffine affine = TTranslation(box.getP00()) *
                     TAffine(x.x, y.x, 0, x.y, y.y, 0) * TTranslation(-origin);
    if (m_session.transform(affine)) {
      m_affine = affine;
      m_tool->setBBox(box);
    }
  }
  TPointD transform(int index, TPointD pos, bool = false) override {
    if (!m_scale) return TPointD(1, 1);
    TPointD value(1, 1);
    auto box = m_scale->bboxScaleInCenter(index, m_startBox, pos, value,
                                          m_scale->getStartCenter(), true);
    applyTransform(box);
    if (!m_scale->scaleInCenter())
      m_tool->setCenter(m_scale->getNewCenter(index, m_startBox, value));
    return value;
  }
};

class CpiInteraction final : public SelectionTool {
  CpiTool m_session;
  std::set<Cpi::PointId> m_boxSelection;
  int m_boxFrame      = -1;
  bool m_customCenter = false, m_moveCenter = false;
  TPointD m_centerStart, m_pointerStart;
  void clearDrag() {
    delete m_dragTool;
    m_dragTool               = nullptr;
    m_moveCenter             = false;
    m_leftButtonMousePressed = false;
  }
  void modifySelectionOnClick(TImageP, const TPointD &,
                              const TMouseEvent &) override {}
  void doOnActivate() override {}
  void doOnDeactivate() override {}

public:
  CpiInteraction()
      : SelectionTool(AllTargets, "T_CPI"), m_session(this, this) {}
  CpiTool *session() { return &m_session; }
  void setNewFreeDeformer() override {}
  TSelection *getSelection() override { return &m_session; }
  bool isSelectionEmpty() override { return m_session.isEmpty(); }
  bool isSelectionEditable() override { return m_session.editable(); }
  ToolOptionsBox *createOptionsBox() override {
    return m_session.createOptionsBox();
  }
  QString updateEnabled(int, int) override {
    bool ready = m_session.editable();
    enable(ready);
    return ready ? QString()
                 : tr("CPI requires an unlocked vector drawing and a supported "
                      "tool.");
  }
  void computeBBox() override {
    if (m_dragTool || m_moveCenter) return;
    auto bounds = m_session.selectionBounds();
    if (m_boxSelection != m_session.selectedPoints() ||
        m_boxFrame != getFrame())
      m_customCenter = false;
    m_boxSelection = m_session.selectedPoints();
    m_boxFrame     = getFrame();
    if (bounds.isEmpty()) {
      m_bboxs.clear();
      m_centers.clear();
      return;
    }
    FourPoints box;
    box = bounds;
    m_bboxs.assign(1, box);
    if (!m_customCenter || m_centers.empty())
      m_centers.assign(
          1, bounds.getP00() + TPointD(bounds.getLx(), bounds.getLy()) * 0.5);
    m_deformValues.m_isSelectionModified =
        true;  // CPI does not animate stroke thickness.
  }
  void onActivate() override {
    m_session.activate();
    clearDrag();
    computeBBox();
  }
  void onDeactivate() override {
    clearDrag();
    m_session.cancelPreview();
    if (!getApplication()->getCurrentTool()->isCpiMode())
      m_session.deactivate();
  }
  void onImageChanged() override {
    if (!m_session.dragging()) clearDrag();
    m_session.refresh();
    computeBBox();
    invalidate();
  }
  void onEnter() override { m_session.activate(); }
  void onLeave() override { m_session.leave(); }
  void updateMatrix() override {
    if (getObjectId().isColumn())
      setMatrix(getColumnMatrix(getObjectId().getIndex()));
    else
      setMatrix(TAffine());
  }
  int getCursorId() const override {
    if (!m_session.editable()) return ToolCursor::CURSOR_NO;
    if (m_session.m_operation == CpiTool::Magnet)
      return ToolCursor::MagnetCursor;
    if (m_session.m_operation == CpiTool::Smooth) return ToolCursor::IronCursor;
    return m_cursorId;
  }
  void mouseMove(const TPointD &pos, const TMouseEvent &e) override {
    if (!m_session.dragging()) clearDrag();
    m_session.move(pos);
    if (m_session.m_operation != CpiTool::Select) return;
    computeBBox();
    SelectionTool::updateAction(pos, e);
    Cpi::PointId hit;
    if (m_session.hitPoint(pos, hit)) {
      m_cursorId = ToolCursor::StrokeSelectCursor;
      m_what     = Outside;
    } else if (m_what == Outside && getBBox().contains(pos) &&
               !e.isShiftPressed() && !e.isCtrlPressed()) {
      m_what     = Inside;
      m_cursorId = ToolCursor::MoveCursor;
    }
  }
  void leftButtonDown(const TPointD &pos, const TMouseEvent &e) override {
    if (!m_session.editable()) return;
    clearDrag();
    computeBBox();
    mouseMove(pos, e);
    if (m_session.m_operation != CpiTool::Select || e.isCtrlPressed()) {
      m_session.down(pos, e);
      return;
    }
    Cpi::PointId hit;
    if (m_session.hitPoint(pos, hit) || m_what == Outside ||
        m_what == ADD_SELECTION) {
      m_session.down(pos, e);
      computeBBox();
      return;
    }
    if (!m_session.beginTransform(pos)) return;
    m_leftButtonMousePressed = true;
    if (m_what == MOVE_CENTER) {
      m_moveCenter   = true;
      m_centerStart  = getCenter();
      m_pointerStart = pos;
      return;
    }
    auto kind = m_what == ROTATION                    ? CpiTransform::Rotate
                : m_what == SCALE || m_what == DEFORM ? CpiTransform::Resize
                : m_what == SCALE_X                   ? CpiTransform::ResizeX
                : m_what == SCALE_Y                   ? CpiTransform::ResizeY
                                                      : CpiTransform::Move;
    m_deformValues.reset();
    m_deformValues.m_isSelectionModified = true;
    m_dragTool = new CpiTransform(this, m_session, kind);
    m_dragTool->leftButtonDown(pos, e);
  }
  void leftButtonDrag(const TPointD &pos, const TMouseEvent &e) override {
    if (!m_session.dragging() || !m_session.editable()) {
      clearDrag();
      m_session.cancelPreview();
      return;
    }
    if (m_moveCenter) {
      setCenter(m_centerStart + pos - m_pointerStart);
      m_customCenter = true;
      invalidate();
    } else if (m_dragTool)
      m_dragTool->leftButtonDrag(pos, e);
    else
      m_session.drag(pos, e);
  }
  void leftButtonUp(const TPointD &pos, const TMouseEvent &e) override {
    if (m_dragTool || m_moveCenter) {
      leftButtonDrag(pos, e);
      clearDrag();
      m_session.endTransform();
    } else
      m_session.up(pos, e);
    computeBBox();
    invalidate();
  }
  void leftButtonDoubleClick(const TPointD &, const TMouseEvent &) override {}
  bool keyDown(QKeyEvent *event) override {
    bool consumed = m_session.keyDown(event);
    if (consumed) {
      clearDrag();
      computeBBox();
    }
    return consumed;
  }
  bool isEventAcceptable(QEvent *) override { return false; }
  void addContextMenuItems(QMenu *menu) override {
    m_session.contextMenu(menu);
  }
  void draw() override {
    m_session.draw();
    if (m_session.m_operation != CpiTool::Select) return;
    if (!m_session.dragging()) clearDrag();
    computeBBox();
    auto current = m_session.image();
    if (current && !m_session.isEmpty())
      drawCommandHandle(current.getPointer());
  }
};
CpiInteraction &interaction() {
  static CpiInteraction tool;
  return tool;
}
}  // namespace
TTool *CpiTool::interactionTool() { return &interaction(); }
CpiTool *CpiTool::session() { return interaction().session(); }
