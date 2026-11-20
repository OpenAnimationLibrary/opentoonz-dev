#pragma once

#include <QTimer>

#include <algorithm>
#include <deque>
#include <functional>
#include <utility>
#include <vector>

// Main-thread scheduling only. Rendering and cooperative cancellation remain
// with Previewer/TRenderer; no drawing or FX data is accessed by this class.
class PreviewDrawingScheduler {
  QTimer m_timer;
  std::function<bool(int)> m_render;
  std::function<void(int)> m_cancel;
  std::deque<int> m_background;
  int m_foreground         = -1;
  int m_running            = -1;
  bool m_foregroundPending = false;
  bool m_enabled           = false;
  bool m_busy              = false;

  void queueBackground(int frame) {
    if (std::find(m_background.begin(), m_background.end(), frame) ==
        m_background.end())
      m_background.push_back(frame);
  }

  void schedule(int idleDelay = 500) {
    if (!m_enabled || m_busy || m_running >= 0) return;
    if (m_foregroundPending)
      m_timer.start(0);
    else if (!m_background.empty() && !m_timer.isActive())
      m_timer.start(idleDelay);
  }

  void cancelRunning() {
    if (m_running < 0) return;
    int frame = m_running;
    m_running = -1;
    m_cancel(frame);
    if (frame == m_foreground)
      m_foregroundPending = true;
    else
      queueBackground(frame);
  }

  void dispatch() {
    if (!m_enabled || m_busy || m_running >= 0) return;
    if (m_foregroundPending) {
      m_foregroundPending = false;
      m_running           = m_foreground;
    } else if (!m_background.empty()) {
      m_running = m_background.front();
      m_background.pop_front();
    } else
      return;
    if (!m_render(m_running)) {
      m_running = -1;
      schedule();
    }
  }

public:
  PreviewDrawingScheduler(QObject *context, std::function<bool(int)> render,
                          std::function<void(int)> cancel)
      : m_render(std::move(render)), m_cancel(std::move(cancel)) {
    m_timer.setSingleShot(true);
    QObject::connect(&m_timer, &QTimer::timeout, context,
                     [this]() { dispatch(); });
  }

  bool isEnabled() const { return m_enabled; }

  void setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    if (enabled) return;
    m_timer.stop();
    // Restore ordinary rendering, including queued requests, when the option
    // is disabled or playback / Save Previewed Frames takes over.
    std::vector<int> pending;
    if (m_foregroundPending) pending.push_back(m_foreground);
    pending.insert(pending.end(), m_background.begin(), m_background.end());
    m_background.clear();
    m_foregroundPending = false;
    m_running = m_foreground = -1;
    for (int frame : pending) m_render(frame);
  }

  void setBusy(bool busy) {
    m_busy = busy;
    if (!m_enabled) return;
    if (busy) {
      m_timer.stop();
      cancelRunning();
    } else
      schedule();
  }

  void requestForeground(int frame) {
    if (!m_enabled) {
      m_render(frame);
      return;
    }
    if (m_foregroundPending && m_foreground >= 0 && m_foreground != frame)
      queueBackground(m_foreground);
    m_background.erase(
        std::remove(m_background.begin(), m_background.end(), frame),
        m_background.end());
    m_foreground = frame;
    if (m_running == frame) return;
    cancelRunning();
    m_foregroundPending = true;
    m_timer.stop();
    schedule();
  }

  void requestBackground(const std::vector<int> &frames) {
    for (int frame : frames) {
      if (frame >= 0 && frame != m_foreground && frame != m_running)
        queueBackground(frame);
    }
    schedule();
  }

  void invalidate(int frame) {
    if (m_enabled && m_running == frame) {
      cancelRunning();
      schedule();
    }
  }

  void completed(int frame) {
    if (!m_enabled || m_running != frame) return;
    m_running = -1;
    schedule(frame == m_foreground ? 500 : 0);
  }

  void clear() {
    m_timer.stop();
    if (m_enabled && m_running >= 0) m_cancel(m_running);
    m_background.clear();
    m_running = m_foreground = -1;
    m_foregroundPending      = false;
  }
};
