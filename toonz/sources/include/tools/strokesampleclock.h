#pragma once

#ifndef STROKESAMPLECLOCK_INCLUDED
#define STROKESAMPLECLOCK_INCLUDED

#include <algorithm>
#include <cstdint>

// Keep brush elapsed time independent of event delivery and input-state
// ordering.
class TStrokeSampleClock {
  bool m_started              = false;
  bool m_hasSource            = false;
  bool m_estimated            = true;
  std::int64_t m_lastDispatch = 0;
  std::int64_t m_lastSource   = 0;
  double m_sourceTime         = 0.0;
  double m_time               = 0.0;

public:
  void reset() { *this = TStrokeSampleClock(); }

  double update(std::int64_t sourceMilliseconds,
                std::int64_t dispatchNanoseconds) {
    if (!m_started) {
      m_started      = true;
      m_hasSource    = sourceMilliseconds > 0;
      m_lastSource   = sourceMilliseconds;
      m_lastDispatch = dispatchNanoseconds;
      m_estimated    = !m_hasSource;
      return m_time;
    }

    const double dispatchDelta =
        std::max<std::int64_t>(0, dispatchNanoseconds - m_lastDispatch) * 1e-9;
    m_lastDispatch = std::max(m_lastDispatch, dispatchNanoseconds);
    m_estimated    = true;

    if (sourceMilliseconds > 0) {
      if (m_hasSource) {
        if (sourceMilliseconds >= m_lastSource) {
          m_time = m_sourceTime + (sourceMilliseconds - m_lastSource) * 0.001;
          m_estimated = sourceMilliseconds == m_lastSource;
        }
        // A reset or wrap starts a new source epoch at the current sample time.
      } else {
        m_time += dispatchDelta;
      }
      m_hasSource  = true;
      m_lastSource = sourceMilliseconds;
      m_sourceTime = m_time;
    } else if (!m_hasSource) {
      m_time += dispatchDelta;
    }
    // A missing timestamp within a sourced stroke keeps its sample time until
    // a valid source timestamp supplies the elapsed interval. Do not let a
    // delayed dispatch move the source anchor and distort later valid samples.
    return m_time;
  }

  bool isEstimated() const { return m_estimated; }
};

#endif
