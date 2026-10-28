#pragma once

#ifndef WINHANDWRITINGRECOGNIZER_H
#define WINHANDWRITINGRECOGNIZER_H

#include <QMutex>
#include <QPolygonF>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <QWaitCondition>

// All Windows Ink COM objects are created, used and released on this thread.
// A pending request is replaced by the newest snapshot, never queued
// unboundedly.
class WinHandwritingRecognizer final : public QThread {
  Q_OBJECT

  QMutex m_mutex;
  QWaitCondition m_wake;
  QVector<QPolygonF> m_strokes;
  quint64 m_revision = 0;
  int m_recognizer   = 0;
  bool m_pending     = false;
  bool m_stopping    = false;
  bool m_restarting  = false;

public:
  explicit WinHandwritingRecognizer(QObject *parent = nullptr);
  ~WinHandwritingRecognizer() override;
  void recognize(const QVector<QPolygonF> &strokes, int recognizer,
                 quint64 revision);
  void cancelPending();
  void recheck();

signals:
  void recognizersReady(const QStringList &names);
  void recognized(quint64 revision, const QString &text, const QString &error);

protected:
  void run() override;
};

#endif
