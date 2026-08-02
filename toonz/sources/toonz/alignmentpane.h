#pragma once

#ifndef ALIGNMENTPANE_H
#define ALIGNMENTPANE_H

#include <QFrame>

class QComboBox;
class QPushButton;
class TSelection;
class TXshLevel;

enum AlignType : int {
  AlignLeft,
  AlignRight,
  AlignTop,
  AlignBottom,
  AlignCenterHorizontal,
  AlignCenterVertical,
  DistributeHorizontal,
  DistributeVertical
};

enum AlignMethod { SelectionArea, SmallestObject, LargestObject, Camera };

class AlignmentPane final : public QFrame {
  Q_OBJECT

public:
  explicit AlignmentPane(QWidget *parent = nullptr);

private slots:
  void onAlignMethodChanged(int index);
  void onLevelSwitched(TXshLevel *oldLevel);
  void onSelectionSwitched(TSelection *oldSelection,
                           TSelection *newSelection);
  void onToolSwitched();

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent *event) override;

private:
  void updateButtons();

  QComboBox *m_alignMethod;
  QPushButton *m_alignLeft;
  QPushButton *m_alignRight;
  QPushButton *m_alignTop;
  QPushButton *m_alignBottom;
  QPushButton *m_alignCenterHorizontal;
  QPushButton *m_alignCenterVertical;
  QPushButton *m_distributeHorizontal;
  QPushButton *m_distributeVertical;
};

#endif  // ALIGNMENTPANE_H
