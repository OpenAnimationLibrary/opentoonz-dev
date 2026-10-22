#include <tools/strokesmoothnessdiagnostics.h>

#include <tools/inputmanager.h>
#include <tools/tool.h>
#include <tstroke.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QScopedValueRollback>
#include <QStringList>
#include <QTabletEvent>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace StrokeSmoothnessDiagnostics {
namespace {

struct TraceFile {
  QFile file;
  QByteArray pending;

  ~TraceFile() { flush(); }

  void open(const QString &path, const char *header) {
    file.setFileName(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      qWarning() << "Cannot open stroke trace:" << path << file.errorString();
      return;
    }
    pending.reserve(256 * 1024);
    pending.append(header);
  }

  void flush() {
    if (!file.isOpen() || pending.isEmpty()) return;
    if (file.write(pending) != pending.size() || !file.flush()) {
      qWarning() << "Cannot write stroke trace:" << file.errorString();
      file.close();
    }
    pending.clear();
  }

  void flushIfFull() {
    if (pending.size() >= 256 * 1024) flush();
  }
};

struct State {
  bool initialized = false;
  bool enabled = false;
  bool rawActive = false;
  bool replayConsumed = false;
  bool replaying = false;
  bool suppressTrigger = false;
  qint64 stroke = 0;
  qint64 rawEvent = 0;
  qint64 acceptedEvent = 0;
  QString session;
  std::map<TTrack::Id, qint64> strokes;
  TraceFile raw;
  TraceFile accepted;
  TraceFile geometry;
};

bool envEnabled(const char *name) {
  const QByteArray value = qgetenv(name).trimmed().toLower();
  return !value.isEmpty() && value != "0" && value != "false" && value != "no";
}

State &state() {
  static State s;
  if (s.initialized) return s;
  s.initialized = true;
  s.enabled = envEnabled("OPENTOONZ_INPUT_TRACE");
  if (!s.enabled) return s;

  const QByteArray requested = qgetenv("OPENTOONZ_INPUT_TRACE_DIR");
  const QString directory = requested.isEmpty()
                                ? QDir::tempPath() + "/opentoonz-stroke-diagnostics"
                                : QString::fromLocal8Bit(requested);
  if (!QDir().mkpath(directory)) {
    qWarning() << "Cannot create stroke trace directory:" << directory;
    s.enabled = false;
    return s;
  }
  s.session = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") +
              '-' + QString::number(QCoreApplication::applicationPid());
  const QString prefix = directory + "/stroke-" + s.session;
  s.raw.open(prefix + "-raw.csv",
             "session,stroke,event,stage,event_type,coordinate_space,"
             "source_timestamp,arrival_timestamp,sample_time,time_estimated,"
             "x,y,pressure,device_id,pointer_type,buttons,modifiers,"
             "is_high_frequent\n");
  s.accepted.open(prefix + "-accepted.csv",
                  "session,stroke,root_track,track,event,stage,event_type,"
                  "coordinate_space,pipeline_timestamp,sample_time,event_time,"
                  "x,y,pressure,tilt_x,tilt_y,device_id,touch_id,has_pressure,"
                  "has_tilt,final\n");
  s.geometry.open(prefix + "-geometry.csv",
                  "session,stroke,root_track,track,stage,index,coordinate_space,"
                  "x,y,pressure,thickness,sample_time,event_time,length,final\n");
  return s;
}

void configure(QTextStream &out) { out.setRealNumberPrecision(17); }

qint64 strokeFor(State &s, const TTrack &track) {
  const TTrack::Id root = track.root()->id;
  const auto found = s.strokes.find(root);
  if (found != s.strokes.end()) return found->second;
  const qint64 stroke = s.rawActive ? s.stroke : ++s.stroke;
  s.strokes.emplace(root, stroke);
  return stroke;
}

const char *phase(QEvent::Type type) {
  switch (type) {
  case QEvent::TabletPress:
    return "press";
  case QEvent::TabletRelease:
    return "release";
  default:
    return "move";
  }
}

void flush(State &s) {
  s.raw.flush();
  s.accepted.flush();
  s.geometry.flush();
}

struct ReplaySample {
  TPointD position;
  TPointD tilt;
  double pressure = 0.5;
  double sampleTime = -1.0;
  TTimerTicks ticks = 0;
  TInputState::DeviceId device = 1;
  TInputState::TouchId touch = 0;
  bool hasPressure = true;
  bool hasTilt = false;
  bool final = false;
};

bool csvBool(const QString &value) {
  const QString text = value.trimmed().toLower();
  return text == "1" || text == "true" || text == "yes";
}

std::vector<ReplaySample> loadReplay(const QString &path) {
  std::vector<ReplaySample> result;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    qWarning() << "Cannot read stroke replay:" << path;
    return result;
  }
  QTextStream in(&file);
  const QStringList header = in.readLine().split(',');
  // Raw/viewer coordinates are never suitable for accepted-input replay.
  if (!header.contains("pipeline_timestamp") || !header.contains("x") ||
      !header.contains("y")) {
    qWarning() << "Stroke replay requires an accepted/tool-coordinate CSV";
    return result;
  }
  QString selectedStroke = QString::fromLocal8Bit(qgetenv("OPENTOONZ_INPUT_REPLAY_STROKE"));
  QString selectedSession;
  bool selected = false;
  std::map<std::pair<qint64, qint64>, size_t> last;
  std::map<std::pair<qint64, qint64>, TTimerTicks> firstTicks;
  while (!in.atEnd()) {
    const QStringList fields = in.readLine().split(',');
    if (fields.size() != header.size()) continue;
    auto value = [&](const char *name) {
      const int column = header.indexOf(QLatin1String(name));
      return column >= 0 ? fields[column] : QString();
    };
    const QString stroke = value("stroke");
    if (!selectedStroke.isEmpty() && stroke != selectedStroke) continue;
    if (!selected) {
      selectedStroke = stroke;
      selectedSession = value("session");
      selected = true;
    }
    if (stroke != selectedStroke || value("session") != selectedSession) continue;
    if (header.contains("forwarded") && !csvBool(value("forwarded"))) continue;
    if (header.contains("coordinate_space") && value("coordinate_space") != "tool") {
      qWarning() << "Stroke replay rejected coordinates outside tool space";
      return {};
    }

    bool okX = false, okY = false, okTicks = false;
    ReplaySample sample;
    sample.position.x = value("x").toDouble(&okX);
    sample.position.y = value("y").toDouble(&okY);
    sample.ticks = value("pipeline_timestamp").toLongLong(&okTicks);
    if (!okX || !okY || !okTicks || sample.ticks < 0 ||
        !std::isfinite(sample.position.x) || !std::isfinite(sample.position.y)) {
      qWarning() << "Stroke replay contains invalid position or time data";
      return {};
    }
    auto number = [&](const char *name, double fallback) {
      if (!header.contains(QLatin1String(name))) return fallback;
      bool ok = false;
      const double parsed = value(name).toDouble(&ok);
      return ok ? parsed : std::numeric_limits<double>::quiet_NaN();
    };
    sample.pressure = number("pressure", 0.5);
    sample.tilt.x = number("tilt_x", 0.0);
    sample.tilt.y = number("tilt_y", 0.0);
    if (!std::isfinite(sample.pressure) || sample.pressure < 0.0 ||
        sample.pressure > 1.0 || !std::isfinite(sample.tilt.x) ||
        !std::isfinite(sample.tilt.y)) {
      qWarning() << "Stroke replay contains invalid pressure or tilt";
      return {};
    }
    if (header.contains("device_id")) sample.device = value("device_id").toLongLong();
    if (header.contains("touch_id")) sample.touch = value("touch_id").toLongLong();
    if (header.contains("has_pressure")) sample.hasPressure = csvBool(value("has_pressure"));
    if (header.contains("has_tilt")) sample.hasTilt = csvBool(value("has_tilt"));
    sample.final = csvBool(value("final"));
    const auto key = std::make_pair(sample.device, sample.touch);
    firstTicks.emplace(key, sample.ticks);
    sample.sampleTime = number("sample_time", (sample.ticks - firstTicks[key]) * TToolTimer::step);
    if (!std::isfinite(sample.sampleTime) || sample.sampleTime < 0.0 ||
        (!result.empty() && sample.ticks < result.back().ticks)) {
      qWarning() << "Stroke replay requires finite, ordered time data";
      return {};
    }
    last[key] = result.size();
    result.push_back(sample);
    if (result.size() > 1000000) {
      qWarning() << "Stroke replay exceeded one million samples";
      return {};
    }
  }
  // A recording interrupted before pen-up is still a bounded replay gesture.
  for (const auto &entry : last) result[entry.second].final = true;
  return result;
}

}  // namespace

void beginStroke() {
  State &s = state();
  if (!s.enabled) return;
  ++s.stroke;
  s.rawActive = true;
}

void endRawStroke() {
  State &s = state();
  if (!s.enabled) return;
  s.rawActive = false;
  flush(s);
}

void recordRawTabletEvent(const QTabletEvent &event, bool highFrequent) {
  State &s = state();
  if (!s.enabled || !s.raw.file.isOpen()) return;
  QTextStream out(&s.raw.pending, QIODevice::WriteOnly | QIODevice::Append);
  configure(out);
  out << s.session << ',' << (s.rawActive ? s.stroke : 0) << ',' << s.rawEvent++ << ",raw,"
      << phase(event.type()) << ",viewer_logical," << event.timestamp() << ','
      << TToolTimer::ticks() << ",,," << event.posF().x() << ','
      << event.posF().y() << ',' << event.pressure() << ',' << event.uniqueId()
      << ',' << int(event.pointerType()) << ',' << int(event.buttons()) << ','
      << int(event.modifiers()) << ',' << int(highFrequent) << '\n';
  out.flush();
  s.raw.flushIfFull();
}

void recordDispatchedTabletEvent(const TMouseEvent &event, const char *eventPhase) {
  State &s = state();
  if (!s.enabled || !s.raw.file.isOpen()) return;
  QTextStream out(&s.raw.pending, QIODevice::WriteOnly | QIODevice::Append);
  configure(out);
  out << s.session << ',' << (s.rawActive ? s.stroke : 0) << ',' << s.rawEvent++ << ",dispatched,"
      << eventPhase << ",viewer_logical," << event.sourceTimestamp() << ','
      << event.time() << ',' << event.sampleTime() << ','
      << int(event.isSampleTimeEstimated()) << ',' << event.mousePos().x() << ','
      << event.mousePos().y() << ',' << event.m_pressure << ",,,"
      << int(event.buttons()) << ',' << int(event.getModifiersMask()) << ','
      << int(event.isHighFrequent()) << '\n';
  out.flush();
  s.raw.flushIfFull();
}

void recordAccepted(const TTrack &track, TTimerTicks ticks, const char *reason) {
  State &s = state();
  if (!s.enabled || !s.accepted.file.isOpen() || track.empty()) return;
  const qint64 stroke = strokeFor(s, track);
  const TTrackPoint &p = track.back();
  QTextStream out(&s.accepted.pending, QIODevice::WriteOnly | QIODevice::Append);
  configure(out);
  out << s.session << ',' << stroke << ',' << track.root()->id << ',' << track.id
      << ',' << s.acceptedEvent++ << ",accepted," << reason << ",tool,"
      << ticks << ',' << p.time + track.rootTimeOffset << ',' << p.eventTime
      << ',' << p.position.x << ',' << p.position.y << ',' << p.pressure << ','
      << p.tilt.x << ',' << p.tilt.y << ',' << track.deviceId << ','
      << track.touchId << ',' << int(track.hasPressure) << ','
      << int(track.hasTilt) << ',' << int(p.final) << '\n';
  out.flush();
  s.accepted.flushIfFull();
}

void recordGeometry(const TTrackList &tracks) {
  State &s = state();
  if (!s.enabled || !s.geometry.file.isOpen()) return;
  for (const TTrackP &track : tracks) {
    const qint64 stroke = strokeFor(s, *track);
    for (int i = 0; i < track->size(); ++i) {
      const TTrackPoint &p = (*track)[i];
      QTextStream out(&s.geometry.pending, QIODevice::WriteOnly | QIODevice::Append);
      configure(out);
      out << s.session << ',' << stroke << ',' << track->root()->id << ','
          << track->id << ",reconstructed," << i << ",tool,"
          << p.position.x << ',' << p.position.y << ',' << p.pressure << ",,"
          << p.time << ',' << p.eventTime << ',' << p.length << ','
          << int(p.final) << '\n';
      out.flush();
      s.geometry.flushIfFull();
    }
  }
}

void recordVectorStroke(const TTrackList &tracks, size_t outputIndex,
                        const TStroke &stroke) {
  State &s = state();
  if (!s.enabled || !s.geometry.file.isOpen()) return;
  // StrokeGenerator discards empty output tracks before fitting.
  const TTrack *source = nullptr;
  for (const TTrackP &track : tracks) {
    if (track->empty()) continue;
    if (outputIndex-- == 0) {
      source = track.getPointer();
      break;
    }
  }
  if (!source) return;
  const qint64 strokeId = strokeFor(s, *source);
  auto point = [&](const char *stage, int index, const TThickPoint &p, bool final) {
    QTextStream out(&s.geometry.pending, QIODevice::WriteOnly | QIODevice::Append);
    configure(out);
    out << s.session << ',' << strokeId << ',' << source->root()->id << ','
        << source->id << ',' << stage << ',' << index << ",tool,"
        << p.x << ',' << p.y << ",," << p.thick << ",,,," << int(final) << '\n';
    out.flush();
    s.geometry.flushIfFull();
  };
  const int controlCount = stroke.getControlPointCount();
  for (int i = 0; i < controlCount; ++i)
    point("fitted_control", i, stroke.getControlPoint(i), i == controlCount - 1);

  // Evaluate each quadratic, not its control polygon. These are diagnostic
  // samples, not an exact continuous-curve Hausdorff measurement.
  int index = 0;
  const int chunks = stroke.getChunkCount();
  for (int chunk = 0; chunk < chunks; ++chunk) {
    const TThickPoint a = stroke.getControlPoint(2 * chunk);
    const TThickPoint b = stroke.getControlPoint(2 * chunk + 1);
    const TThickPoint c = stroke.getControlPoint(2 * chunk + 2);
    for (int step = chunk ? 1 : 0; step <= 32; ++step) {
      const double t = step / 32.0;
      const double u = 1.0 - t;
      point("fitted_curve", index++,
            TThickPoint(u * u * a.x + 2 * u * t * b.x + t * t * c.x,
                        u * u * a.y + 2 * u * t * b.y + t * t * c.y,
                        u * u * a.thick + 2 * u * t * b.thick + t * t * c.thick),
            chunk == chunks - 1 && step == 32);
    }
  }
}

void finishStroke(const TTrackList &tracks) {
  State &s = state();
  if (!s.enabled) return;
  flush(s);
  for (const TTrackP &track : tracks) s.strokes.erase(track->root()->id);
}

bool maybeReplay(TInputManager &manager, bool final) {
  State &s = state();
  if (s.replaying) return false;
  if (s.suppressTrigger) {
    if (final) s.suppressTrigger = false;
    return true;
  }
  if (s.replayConsumed) return false;
  const QByteArray path = qgetenv("OPENTOONZ_INPUT_REPLAY");
  if (path.isEmpty()) {
    s.replayConsumed = true;
    return false;
  }
  s.replayConsumed = true;
  const std::vector<ReplaySample> samples = loadReplay(QString::fromLocal8Bit(path));
  if (samples.empty()) return false;
  const TTimerTicks baseTicks = TToolTimer::ticks();
  const TTimerTicks firstTicks = samples.front().ticks;
  if (samples.back().ticks - firstTicks >
      std::numeric_limits<TTimerTicks>::max() - baseTicks) {
    qWarning() << "Stroke replay time range overflows the application clock";
    return false;
  }
  bool ok = false;
  const int requestedBatch = qgetenv("OPENTOONZ_INPUT_REPLAY_BATCH").toInt(&ok);
  const size_t batch = ok ? size_t(std::max(1, std::min(requestedBatch, 65536))) : 1;
  QScopedValueRollback<bool> replayGuard(s.replaying, true);
  beginStroke();
  for (size_t i = 0; i < samples.size(); ++i) {
    const ReplaySample &sample = samples[i];
    manager.trackEvent(sample.device, sample.touch, sample.position,
                       sample.pressure, sample.tilt, sample.hasPressure,
                       sample.hasTilt, sample.final,
                       baseTicks + (sample.ticks - firstTicks), sample.sampleTime);
    if (sample.final || (i + 1) % batch == 0 || i + 1 == samples.size())
      manager.processTracks();
  }
  endRawStroke();
  // Do not append the remainder of the physical trigger gesture to the replay.
  s.suppressTrigger = !final;
  return true;
}

}  // namespace StrokeSmoothnessDiagnostics
