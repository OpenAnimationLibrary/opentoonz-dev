#pragma once

#include "bulletstage.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class BulletPreview;

class BulletPhysicsPopup final : public QDialog {
  Q_OBJECT
  QTableWidget *m_table;
  QSpinBox *m_first, *m_last, *m_substeps;
  QDoubleSpinBox *m_gravity, *m_width, *m_mass, *m_scale, *m_floorY;
  QDoubleSpinBox *m_lower, *m_upper;
  QCheckBox *m_floor;
  QPushButton *m_bakeButton;
  QLabel *m_status;
  QSlider *m_frame;
  BulletPreview *m_preview;
  std::vector<int> m_columns;
  OTBullet::Bake m_bake;

public:
  BulletPhysicsPopup();

protected:
  void showEvent(QShowEvent *event) override;

private:
  void invalidate();
  void reloadSelection();
  void simulate();
  void bake();
};
