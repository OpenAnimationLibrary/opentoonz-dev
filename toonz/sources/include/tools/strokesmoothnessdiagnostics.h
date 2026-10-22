#pragma once

#ifndef STROKESMOOTHNESSDIAGNOSTICS_INCLUDED
#define STROKESMOOTHNESSDIAGNOSTICS_INCLUDED

#include <tools/track.h>

class QTabletEvent;
class TInputManager;
class TMouseEvent;
class TStroke;

#undef DVAPI
#undef DVVAR
#ifdef TNZTOOLS_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

// Opt-in diagnostics, called on the application input thread. State lives in
// tnztools so the viewer executable and tools share the same session and IDs.
namespace StrokeSmoothnessDiagnostics {

DVAPI void beginStroke();
DVAPI void endRawStroke();
DVAPI void recordRawTabletEvent(const QTabletEvent &event, bool highFrequent);
DVAPI void recordDispatchedTabletEvent(const TMouseEvent &event,
                                       const char *phase);
DVAPI void recordAccepted(const TTrack &track, TTimerTicks ticks,
                          const char *reason = "sample");
DVAPI void recordGeometry(const TTrackList &tracks);
DVAPI void recordVectorStroke(const TTrackList &tracks, size_t outputIndex,
                              const TStroke &stroke);
DVAPI void finishStroke(const TTrackList &tracks);
DVAPI bool maybeReplay(TInputManager &manager, bool final);

}  // namespace StrokeSmoothnessDiagnostics

#endif
