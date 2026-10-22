

#include <tools/modifiers/modifiersimplify.h>
#include <algorithm>
#include <cmath>

namespace {

// Bound both the work per candidate and the uncommitted straight-line tail.
constexpr int maxPendingPoints = 64;

bool isCorner(const TTrackPoint &a, const TTrackPoint &b,
              const TTrackPoint &c) {
  const TPointD ab = b.position - a.position;
  const TPointD bc = c.position - b.position;
  const double ab2 = ab.x * ab.x + ab.y * ab.y;
  const double bc2 = bc.x * bc.x + bc.y * bc.y;
  if (ab2 <= TConsts::epsilon || bc2 <= TConsts::epsilon) return false;
  const double dot = ab.x * bc.x + ab.y * bc.y;
  return dot <= 0.0 || dot * dot < 0.5 * ab2 * bc2;
}

bool isPressureExtremum(const TTrackPoint &a, const TTrackPoint &b,
                        const TTrackPoint &c) {
  return (a.pressure < b.pressure && c.pressure <= b.pressure) ||
         (a.pressure > b.pressure && c.pressure >= b.pressure);
}

bool canReplace(const TTrack &track, int first, int last, double error2,
                double pressureError, int pressureExponent) {
  const TTrackPoint &a = track[first];
  const TTrackPoint &b = track[last];
  const TPointD ab     = b.position - a.position;
  const double length2 = ab.x * ab.x + ab.y * ab.y;
  // Retain stationary samples, including pressure-only changes.
  if (length2 <= TConsts::epsilon) return false;

  const double pa = std::pow(a.pressure, pressureExponent);
  const double pb = std::pow(b.pressure, pressureExponent);
  for (int i = first + 1; i < last; ++i) {
    const TTrackPoint &p = track[i];
    const TPointD ap     = p.position - a.position;
    const double t =
        std::max(0.0, std::min(1.0, (ap.x * ab.x + ap.y * ab.y) / length2));
    if (tdistance2(p.position, a.position + ab * t) > error2) return false;
    if (pressureError >= 0.0 &&
        std::abs(std::pow(p.pressure, pressureExponent) -
                 (pa + (pb - pa) * t)) > pressureError)
      return false;
  }
  return true;
}

}  // namespace

//*****************************************************************************************
//    TModifierSimplify implementation
//*****************************************************************************************


TModifierSimplify::TModifierSimplify(double step):
  step(step) { }


void
TModifierSimplify::modifyTrack(
  const TTrack &track,
  TTrackList &outTracks )
{
  if (!track.handler) {
    Handler *handler = new Handler();
    track.handler = handler;
    handler->track = new TTrack(track);
    new Interpolator(*handler->track);
  }

  Handler *handler = dynamic_cast<Handler*>(track.handler.getPointer());
  if (!handler)
    return;
  
  outTracks.push_back(handler->track);
  TTrack &subTrack = *handler->track;

  if (!track.changed())
    return;

  // Reconsider only the tail after the last stable anchor. A new anchor is
  // fixed only when the point which selected it is also fixed upstream.
  subTrack.truncate(subTrack.fixedSize());
  if (!track.empty()) {
    int first = 0;
    if (subTrack.empty())
      subTrack.push_back(subTrack.pointFromOriginal(0), track.fixedSize() > 0);
    else
      first = track.floorIndex(subTrack.back().originalIndex);

    const double error2 = std::max(0.0, step) * std::max(0.0, step);
    const int exponent  = std::max(1, pressureExponent);
    for (int last = first + 2; last < track.size(); ++last) {
      const bool preserve =
          isCorner(track[last - 2], track[last - 1], track[last]) ||
          (pressureError >= 0.0 &&
           isPressureExtremum(track[last - 2], track[last - 1], track[last]));
      if (preserve || last - first >= maxPendingPoints ||
          !canReplace(track, first, last, error2, pressureError, exponent)) {
        first = last - 1;
        subTrack.push_back(subTrack.pointFromOriginal(first),
                           last < track.fixedSize());
      }
    }
    if (first < track.size() - 1)
      subTrack.push_back(subTrack.pointFromOriginal(track.size() - 1), false);
    if (track.fixedFinished()) subTrack.fix_all();
  }

  track.resetChanges();
}

