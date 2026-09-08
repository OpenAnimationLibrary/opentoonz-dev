#pragma once

#include "toonz/cpi.h"
#include "toonz/txshcell.h"
#include "toonz/txshlevelcolumn.h"
#include <QObject>
#include <QCoreApplication>
#include <QPointer>

class TTool;
class QDialog;
class QLabel;
class QComboBox;
class QSpinBox;
class QTableWidget;
class QMenu;
class QKeyEvent;
class TMouseEvent;

class CpiTool final : public QObject {
  Q_DECLARE_TR_FUNCTIONS(CpiTool)
  TTool *m_tool;
  QPointer<QDialog> m_dialog;
  QLabel *m_status    = nullptr;
  QComboBox *m_groups = nullptr, *m_target = nullptr;
  QSpinBox *m_distance = nullptr;
  QTableWidget *m_keys = nullptr;
  TXshLevelColumnP m_column;
  TXshCell m_cell;
  std::string m_group;
  std::set<Cpi::PointId> m_selection;
  TPointD m_first, m_last;
  bool m_dragging = false, m_rectangle = false, m_changed = false;
  bool m_refreshing = false, m_connected = false;
  int m_row = -1;
  Cpi::Snapshot m_before, m_preview;
  bool m_dragGroup = false;
  Cpi::Pose m_startPose;

  bool context(TXshLevelColumnP &column, TXshCell &cell, TVectorImageP &image,
               bool editing = true) const;
  void syncContext();
  void cancelPreview();
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

public:
  explicit CpiTool(TTool *tool, QObject *parent);
  ~CpiTool();
  void activate();
  void deactivate();
  void refresh();
  void openChannels();
  void draw();
  void down(const TPointD &pos, const TMouseEvent &event);
  void drag(const TPointD &pos, const TMouseEvent &event);
  void up(const TPointD &pos, const TMouseEvent &event);
  bool keyDown(QKeyEvent *event);
  void contextMenu(QMenu *menu);
};
