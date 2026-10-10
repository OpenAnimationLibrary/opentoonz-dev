#pragma once

#include <QPointer>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>
#include <Qt>

class QWidget;

class QMenu;
class TPanel;
class MainWindow;

// Named fullscreen layouts, stored apart from the windowed room.
class FullScreenWorkspace final {
  struct FrozenPanel {
    QPointer<TPanel> panel;
    bool floating = false;
    bool visible  = false;
    bool created  = false;
    bool touched  = false;
    Qt::WindowFlags flags;
    QRect geometry;
    QPointer<QWidget> parentWidget;
  };

  struct Item {
    QString panelType;
    QRectF norm;
  };

  bool m_active = false;
  bool m_holding = false;
  bool m_hasSessionVisible = false;
  QString m_activeName;
  QString m_pendingName;
  QVector<FrozenPanel> m_frozen;
  QVector<QPointer<TPanel>> m_sessionVisible;
  QVector<QPointer<TPanel>> m_placed;
  QStringList m_registeredCommandIds;

  FullScreenWorkspace() = default;

  QStringList readNames() const;
  QVector<Item> readItems(const QString &name) const;
  void writeItems(const QString &name, const QVector<Item> &items);
  void removeStored(const QString &name);

  QVector<Item> captureVisiblePanels() const;
  void begin(QString name);
  void end();
  void placeItems(const QVector<Item> &items);
  void concealForeignPanels(const QVector<Item> &items);
  void raisePlaced();
  void holdVisibleFloatingPanels();

public:
  static FullScreenWorkspace &instance();

  void onViewerFullScreenChanged(bool active);
  void raiseCompanionPanels();
  void holdPanel(TPanel *panel);

  void open(const QString &name);
  void registerCommands();
  void save();
  void saveAs();
  void renameWorkspace(const QString &name);
  void deleteWorkspace(const QString &name);

  static void attachMenu(QMenu *workspaceMenu);
  static void addSaveActions(QMenu *menu);
  void fillMenu(QMenu *menu);
};
