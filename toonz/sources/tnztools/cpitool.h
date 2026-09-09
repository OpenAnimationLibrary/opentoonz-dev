#pragma once

#include "toonz/cpi.h"
#include "toonz/txshcell.h"
#include "toonz/txshlevelcolumn.h"
#include "toonzqt/selection.h"
#include <QObject>
#include <QCoreApplication>
#include <QPointer>

#undef DVAPI
#ifdef TNZTOOLS_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class TTool;
class ToolOptionsBox;
class QDialog;
class QLabel;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QMenu;
class QKeyEvent;
class TMouseEvent;

class DVAPI CpiTool final : public QObject, public TSelection {
  Q_DECLARE_TR_FUNCTIONS(CpiTool)
  TTool *m_tool;
  QPointer<QDialog> m_dialog;
  QLabel *m_status    = nullptr;
  QComboBox *m_groups = nullptr, *m_target = nullptr;
  QSpinBox *m_distance = nullptr;
  QTableWidget *m_keys = nullptr;
  std::vector<QPointer<ToolOptionsBox>> m_bars;
  TXshLevelColumnP m_column;
  TXshCell m_cell;
  std::string m_group;
  std::set<Cpi::PointId> m_selection, m_movingPoints;
  TPointD m_first, m_last, m_cursor;
  std::vector<TPointD> m_lasso;
  bool m_dragging = false, m_rectangle = false, m_changed = false;
  bool m_refreshing = false, m_connected = false, m_enabled = false;
  bool m_entireGroup = false, m_lassoSelection = false, m_cursorVisible = false;
  bool m_addSelection = false, m_removeSelection = false;
  int m_row = -1;
  Cpi::Snapshot m_before;
  Cpi::PreviewSnapshot m_preview;
  Cpi::Pose m_startPose, m_workingPose;
  TVectorImageP m_startImage;
  std::map<Cpi::PointId, double> m_brushWeights;
  double m_brushRadius = 40, m_strength = 0.25;

  bool context(TXshLevelColumnP &column, TXshCell &cell, TVectorImageP &image,
               bool editing = true) const;
  void syncContext();
  void selectLinkedEndpoints(const TVectorImageP &image);
  void commit(Cpi::Snapshot before, Cpi::Snapshot after, const QString &label,
              const std::vector<int> &exposed = {});
  void message(const QString &text);
  void newGroup();
  void renameGroup();
  void removeGroup();
  void key(bool newPair);
  void removeKey();
  void rotateGroup();
  void translateGroup();
  void selectAll();
  void invertSelection();
  void clearSelection();
  void resolveSelectionGroup();
  bool publishPose(const Cpi::Pose &pose);
  void finishEdit();
  void smoothAt(const TPointD &pos);
  QString statusText() const;

public:
  enum Operation { Select, Magnet, Smooth, Unavailable };
  Operation m_operation = Select;
  explicit CpiTool(TTool *tool, QObject *parent);
  ~CpiTool();
  static TTool *interactionTool();
  static CpiTool *session();
  void setOperation(const std::string &toolName);
  bool enabled() const { return m_enabled; }
  bool editable() const;
  void activate();
  void deactivate();
  void cancelPreview();
  void refresh();
  void openChannels();
  ToolOptionsBox *createOptionsBox();
  void draw();
  void move(const TPointD &pos);
  void leave();
  void down(const TPointD &pos, const TMouseEvent &event);
  void drag(const TPointD &pos, const TMouseEvent &event);
  void up(const TPointD &pos, const TMouseEvent &event);
  bool keyDown(QKeyEvent *event);
  void contextMenu(QMenu *menu);
  bool isEmpty() const override { return m_selection.empty(); }
  void selectNone() override;
  void enableCommands() override;
  const std::set<Cpi::PointId> &selectedPoints() const { return m_selection; }
  TVectorImageP image() const;
  bool hitPoint(const TPointD &pos, Cpi::PointId &point) const;
  TRectD selectionBounds() const;
  bool beginTransform(const TPointD &pos);
  bool transform(const TAffine &affine);
  void endTransform();
  bool dragging() const { return m_dragging; }
};
