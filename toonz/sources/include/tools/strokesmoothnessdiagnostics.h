#pragma once

#ifndef STROKESMOOTHNESSDIAGNOSTICS_INCLUDED
#define STROKESMOOTHNESSDIAGNOSTICS_INCLUDED

#include <tools/inputmanager.h>
#include <tools/tooltimer.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTabletEvent>
#include <QTextStream>

#include <algorithm>
#include <vector>

namespace StrokeSmoothnessDiagnostics {

struct ReplaySample {
  TPointD position;
  double pressure = 0.5;
  double timestamp = 0.0;
  bool final = false;
};

struct State {
  bool initialized = false;
  bool traceEnabled = false;
  bool replayConsumed = false;
  bool replaying = false;
  int stroke = 0;
  int rawEvent = 0;
  int acceptedEvent = 0;
  QString directory;
  QString session;
  QFile rawFile;
  QFile acceptedFile;
  QFile geometryFile;
};

inline State &state() {
  static State s;
  return s;
}

inline bool envEnabled(const char *name) {
  QByteArray value = qgetenv(name).trimmed().toLower();
  return !value.isEmpty() && value != "0" && value != "false" &&
         value != "no";
}

inline void initialize() {
  State &s = state();
  if (s.initialized) return;
  s.initialized   = true;
  s.traceEnabled = envEnabled("OPENTOONZ_INPUT_TRACE");
  if (!s.traceEnabled) return;

  QByteArray requested = qgetenv("OPENTOONZ_INPUT_TRACE_DIR");
  s.directory = requested.isEmpty()
                    ? QDir::tempPath() + "/opentoonz-stroke-diagnostics"
                    : QString::fromLocal8Bit(requested);
  QDir().mkpath(s.directory);
  s.session = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");

  s.rawFile.setFileName(s.directory + "/stroke-" + s.session + "-raw.csv");
  s.acceptedFile.setFileName(s.directory + "/stroke-" + s.session +
                             "-accepted.csv");
  s.geometryFile.setFileName(s.directory + "/stroke-" + s.session +
                             "-geometry.csv");

  if (s.rawFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&s.rawFile);
    out << "stroke,event,event_type,source_timestamp,arrival_timestamp,x,y,"
           "pressure,is_tablet,is_high_frequent,forwarded\n";
    out.flush();
  }
  if (s.acceptedFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&s.acceptedFile);
    out << "stroke,event,pipeline_timestamp,x,y,pressure,device_id,final,"
           "forwarded\n";
    out.flush();
  }
  if (s.geometryFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QTextStream out(&s.geometryFile);
    out << "stroke,stage,index,x,y,pressure,time,length,final\n";
    out.flush();
  }
}

inline void beginStroke() {
  initialize();
  State &s       = state();
  ++s.stroke;
  s.rawEvent      = 0;
  s.acceptedEvent = 0;
}

inline const char *eventTypeName(QEvent::Type type) {
  switch (type) {
  case QEvent::TabletPress:
    return "press";
  case QEvent::TabletRelease:
    return "release";
  case QEvent::TabletMove:
    return "move";
  default:
    return "other";
  }
}

inline void recordRawTabletEvent(QTabletEvent *event, const QPointF &position,
                                 double pressure, bool highFrequent,
                                 bool forwarded) {
  initialize();
  State &s = state();
  if (!s.traceEnabled || !s.rawFile.isOpen()) return;

  QTextStream out(&s.rawFile);
  out.setRealNumberNotation(QTextStream::FixedNotation);
  out.setRealNumberPrecision(9);
  out << s.stroke << ',' << s.rawEvent++ << ',' << eventTypeName(event->type())
      << ',' << event->timestamp() << ',' << TToolTimer::ticks() << ','
      << position.x() << ',' << position.y() << ',' << pressure << ",1,"
      << (highFrequent ? 1 : 0) << ',' << (forwarded ? 1 : 0) << '\n';
  out.flush();
}

inline void recordAccepted(TInputState::DeviceId deviceId,
                           const TPointD &position, double pressure, bool final,
                           TTimerTicks ticks) {
  initialize();
  State &s = state();
  if (!s.traceEnabled || !s.acceptedFile.isOpen()) return;

  QTextStream out(&s.acceptedFile);
  out.setRealNumberNotation(QTextStream::FixedNotation);
  out.setRealNumberPrecision(9);
  out << s.stroke << ',' << s.acceptedEvent++ << ',' << ticks << ','
      << position.x << ',' << position.y << ',' << pressure << ','
      << static_cast<int>(deviceId) << ',' << (final ? 1 : 0) << ",1\n";
  out.flush();
}

inline void recordGeometry(const TTrackList &tracks) {
  initialize();
  State &s = state();
  if (!s.traceEnabled || !s.geometryFile.isOpen()) return;

  QTextStream out(&s.geometryFile);
  out.setRealNumberNotation(QTextStream::FixedNotation);
  out.setRealNumberPrecision(9);
  for (const TTrackP &track : tracks) {
    for (int i = 0; i < track->size(); ++i) {
      const TTrackPoint &p = (*track)[i];
      out << s.stroke << ",output," << i << ',' << p.position.x << ','
          << p.position.y << ',' << p.pressure << ',' << p.time << ','
          << p.length << ',' << (p.final ? 1 : 0) << '\n';
    }
  }
  out.flush();
}

inline bool csvBool(const QString &value) {
  QString v = value.trimmed().toLower();
  return v == "1" || v == "true" || v == "yes";
}

inline std::vector<ReplaySample> loadReplay(const QString &path) {
  std::vector<ReplaySample> samples;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return samples;

  QTextStream in(&file);
  if (in.atEnd()) return samples;
  QStringList header = in.readLine().split(',');
  auto column = [&header](const QString &name) { return header.indexOf(name); };
  const int xCol         = column("x");
  const int yCol         = column("y");
  const int pressureCol  = column("pressure");
  const int forwardedCol = column("forwarded");
  const int finalCol     = column("final");
  const int timeCol      = column("pipeline_timestamp");

  // Replay is intentionally limited to the accepted/tool-coordinate format.
  // Raw tablet CSV positions are viewer/device coordinates and must not be fed
  // directly to TInputManager as if they were tool coordinates.
  if (xCol < 0 || yCol < 0 || timeCol < 0) return samples;

  while (!in.atEnd()) {
    QString line = in.readLine();
    if (line.trimmed().isEmpty()) continue;
    QStringList fields = line.split(',');
    if (xCol >= fields.size() || yCol >= fields.size() ||
        timeCol >= fields.size())
      continue;
    if (forwardedCol >= 0 && forwardedCol < fields.size() &&
        !csvBool(fields[forwardedCol]))
      continue;

    bool okX = false, okY = false, okTime = false;
    double x         = fields[xCol].toDouble(&okX);
    double y         = fields[yCol].toDouble(&okY);
    double timestamp = fields[timeCol].toDouble(&okTime);
    if (!okX || !okY || !okTime) continue;

    ReplaySample sample;
    sample.position  = TPointD(x, y);
    sample.timestamp = timestamp;
    if (pressureCol >= 0 && pressureCol < fields.size())
      sample.pressure = fields[pressureCol].toDouble();
    if (finalCol >= 0 && finalCol < fields.size())
      sample.final = csvBool(fields[finalCol]);
    samples.push_back(sample);
  }

  if (!samples.empty() &&
      std::none_of(samples.begin(), samples.end(),
                   [](const ReplaySample &sample) { return sample.final; }))
    samples.back().final = true;
  return samples;
}

inline bool maybeReplay(TInputManager &manager) {
  State &s = state();
  if (s.replaying || s.replayConsumed) return false;

  QByteArray replayPath = qgetenv("OPENTOONZ_INPUT_REPLAY");
  if (replayPath.isEmpty()) return false;
  s.replayConsumed = true;

  std::vector<ReplaySample> samples =
      loadReplay(QString::fromLocal8Bit(replayPath));
  if (samples.empty()) return false;

  beginStroke();
  s.replaying = true;
  const double firstTimestamp = samples.front().timestamp;
  const TTimerTicks baseTicks = TToolTimer::ticks();
  for (const ReplaySample &sample : samples) {
    const double delta = std::max(0.0, sample.timestamp - firstTimestamp);
    const TTimerTicks ticks = baseTicks + static_cast<TTimerTicks>(delta);
    manager.trackEvent(1, 0, sample.position, sample.pressure, TPointD(), true,
                       false, sample.final, ticks);
    manager.processTracks();
  }
  s.replaying = false;
  return true;
}

}  // namespace StrokeSmoothnessDiagnostics

#endif  // STROKESMOOTHNESSDIAGNOSTICS_INCLUDED
