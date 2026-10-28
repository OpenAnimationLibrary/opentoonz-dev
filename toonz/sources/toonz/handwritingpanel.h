#pragma once

#ifndef HANDWRITINGPANEL_H
#define HANDWRITINGPANEL_H

#include "pane.h"
#include "toonz/txshsoundtextlevel.h"

#include <QMap>
#include <QPolygonF>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class HandwritingSurface;
class WinHandwritingRecognizer;
class TXsheet;
class ToonzScene;

class HandwritingPanel final : public TPanel {
  Q_OBJECT

  struct Draft {
    TXshSoundTextLevelP level;
    QVector<QPolygonF> strokes;
    QString prefix, text, recognized, expected;
    bool manual = false, conflict = false;
  };
  QMap<QPair<quintptr, int>, Draft> m_drafts;
  QPair<quintptr, int> m_key;
  ToonzScene *m_scene = nullptr;
  TXsheet *m_xsheet   = nullptr;
  int m_column = -1, m_row = -1;
  bool m_bound = false, m_writing = false;
  quint64 m_revision = 1;
  QTimer m_recognitionTimer, m_transferTimer;
  WinHandwritingRecognizer *m_recognizer;
  HandwritingSurface *m_surface;
  QComboBox *m_languages;
  QCheckBox *m_automatic, *m_follow;
  QLabel *m_destination, *m_status;
  QPlainTextEdit *m_text;
  QPushButton *m_transfer;

  Draft *draft();
  bool validTarget() const;
  void invalidate();
  void bindRow(int row, int column);
  void bindSelected();
  void frameChanged();
  void sheetChanged();
  void clearBinding();
  void resetScene();
  void inkChanged();
  void recognize();
  void receiveRecognition(quint64 revision, const QString &text,
                          const QString &error);
  void updateText();
  void transfer(bool automatic);
  void recheck();

protected:
  void showEvent(QShowEvent *) override;
  void hideEvent(QHideEvent *) override;

public:
  explicit HandwritingPanel(QWidget *parent = nullptr);
};

#endif
