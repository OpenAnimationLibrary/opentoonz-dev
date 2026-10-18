

#include <tools/modifiers/modifierassistants.h>

// TnzTools includes
#include <tools/tool.h>
#include <tools/replicator.h>
#include <tools/vectorassistants.h>

// TnzLib includes
#include <toonz/tapplication.h>
#include <toonz/txshlevelhandle.h>
#include <toonz/txshsimplelevel.h>
#include <toonz/txshleveltypes.h>
#include <toonz/txsheethandle.h>
#include <toonz/txsheet.h>
#include <toonz/tframehandle.h>
#include <toonz/tobjecthandle.h>
#include <toonz/tcolumnhandle.h>
#include <toonz/dpiscale.h>

// TnzCore includes
#include <tgl.h>
#include <tmetaimage.h>
#include <tstroke.h>
#include <tvectorimage.h>

namespace {

const double vectorAssistantRadius          = 32.0;
const double vectorAssistantInfluence       = 0.9;
const double vectorAssistantSelectionLength = 3.0;

std::vector<VectorAssistants::Guide> scanVectorGuides(TTool *tool,
                                                      const TPointD &start) {
  std::vector<VectorAssistants::Guide> guides;
  if (!tool || !(tool->getToolType() & TTool::LevelTool)) return guides;
  TApplication *app = tool->getApplication();
  if (!app || !app->getCurrentFrame() ||
      app->getCurrentFrame()->isEditingLevel() || !app->getCurrentObject() ||
      app->getCurrentObject()->isSpline() || !app->getCurrentColumn() ||
      !app->getCurrentXsheet() || !app->getCurrentLevel())
    return guides;
  TXshSimpleLevel *level = app->getCurrentLevel()->getSimpleLevel();
  TXsheet *xsheet        = app->getCurrentXsheet()->getXsheet();
  if (!level || !xsheet ||
      (level->getType() != TZP_XSHLEVEL && level->getType() != OVL_XSHLEVEL))
    return guides;
  int column  = app->getCurrentColumn()->getColumnIndex();
  TPointD dpi = getCurrentDpiScale(level, tool->getCurrentFid());
  if (column < 0 || dpi.x <= TConsts::epsilon || dpi.y <= TConsts::epsilon ||
      std::abs(tool->getMatrix().det()) <= TConsts::epsilon)
    return guides;
  // Mouse tracks are in current-level pixels, not scene/world coordinates.
  TAffine worldToTrack =
      TScale(1.0 / dpi.x, 1.0 / dpi.y) * tool->getMatrix().inv();
  int frame = app->getCurrentFrame()->getFrame();
  for (int c : {column - 1, column + 1}) {
    if (c < 0 || c >= xsheet->getColumnCount()) continue;
    TXshColumn *reference = xsheet->getColumn(c);
    if (!reference || !reference->isCamstandVisible() ||
        reference->getOpacity() == 0)
      continue;
    const TXshCell &cell            = xsheet->getCell(frame, c);
    TXshSimpleLevel *referenceLevel = cell.getSimpleLevel();
    if (!referenceLevel || referenceLevel->getType() != PLI_XSHLEVEL) continue;
    TVectorImageP image(cell.getImage(false));
    if (!image) continue;
    TAffine imageToTrack = worldToTrack * tool->getColumnMatrix(c);
    if (std::abs(imageToTrack.det()) <= TConsts::epsilon) continue;
    for (UINT s = 0; s < image->getStrokeCount(); ++s) {
      const TStroke *source = image->getStroke(s);
      TRectD bounds         = imageToTrack * source->getCenterlineBBox();
      bounds                = bounds.enlarge(vectorAssistantRadius);
      if (!bounds.contains(start)) continue;
      TStroke stroke(*source);
      stroke.transform(imageToTrack, false);
      double w = 0.0, distance2 = 0.0;
      if (!stroke.getNearestW(start, w, distance2, false) ||
          !std::isfinite(distance2) ||
          distance2 > vectorAssistantRadius * vectorAssistantRadius)
        continue;
      VectorAssistants::Guide guide;
      guide.closed = stroke.isSelfLoop();
      guide.add(convert(stroke.getControlPoint(0)));
      bool complete = true;
      for (int i = 0; i < stroke.getChunkCount() && complete; ++i)
        complete = VectorAssistants::appendQuadratic(
            guide, convert(stroke.getControlPoint(2 * i)),
            convert(stroke.getControlPoint(2 * i + 1)),
            convert(stroke.getControlPoint(2 * i + 2)));
      if (complete && guide.valid()) {
        if (guide.closed) guide.add(guide.points.front());
        guides.push_back(std::move(guide));
      }
    }
  }
  return guides;
}

class VectorAssistantInterpolator final : public TTrackInterpolator {
public:
  std::vector<VectorAssistants::Guide> guides;
  std::vector<VectorAssistants::State> states;
  int selected = -1;
  bool decided = false;
  const double influence;

  VectorAssistantInterpolator(TTrack &track,
                              std::vector<VectorAssistants::Guide> guides,
                              double magnetism)
      : TTrackInterpolator(track)
      , guides(std::move(guides))
      , influence(vectorAssistantInfluence * std::min(1.0, magnetism)) {}

  TTrackPoint interpolate(double index) override {
    TTrackPoint p = track.calcPointFromOriginal(index);
    double fraction;
    int i = track.floorIndex(index, &fraction);
    if (!decided || selected < 0 || i < 0 || i >= (int)states.size()) return p;
    if (fraction <= TConsts::epsilon) return track[i];
    // Segmentation can query indices in any order. Never mutate stroke history
    // from an interpolation query, or replay/preview would accumulate drift.
    VectorAssistants::State state = states[i];
    p.position = VectorAssistants::follow(guides[selected], state, p.position,
                                          vectorAssistantRadius, influence);
    return p;
  }

  void update(const TTrack &original) {
    int start = std::max(0, original.size() - original.pointsAdded);
    if (!decided) {
      if (original.back().length >= vectorAssistantSelectionLength ||
          original.fixedFinished()) {
        int end = 0;
        while (end + 1 < original.size() &&
               original[end].length < vectorAssistantSelectionLength)
          ++end;
        selected = VectorAssistants::chooseGuide(
            guides, original.front().position,
            original[end].position - original.front().position,
            vectorAssistantRadius);
        decided = true;
      }
      // This prefix is still a preview until guide selection is settled.
      start = 0;
    }
    track.truncate(start);
    states.resize(start);
    VectorAssistants::State state =
        start ? states.back() : VectorAssistants::State();
    for (int i = start; i < original.size(); ++i) {
      TTrackPoint p = track.pointFromOriginal(i);
      if (decided && selected >= 0)
        p.position =
            VectorAssistants::follow(guides[selected], state, p.position,
                                     vectorAssistantRadius, influence);
      track.push_back(p, false);
      states.push_back(state);
    }
    if (decided) track.fix_to(original.fixedSize());
    original.resetChanges();
  }

  void draw() const {
    if (selected < 0) return;
    glPushAttrib(GL_CURRENT_BIT | GL_LINE_BIT | GL_ENABLE_BIT |
                 GL_COLOR_BUFFER_BIT);
    tglColor(TAssistantBase::colorSelect);
    tglEnableLineSmooth(true, 1.0);
    glBegin(GL_LINE_STRIP);
    for (const TPointD &p : guides[selected].points) glVertex2d(p.x, p.y);
    glEnd();
    glPopAttrib();
  }
};

}  // namespace

//*****************************************************************************************
//    TModifierAssistants::Interpolator implementation
//*****************************************************************************************


TTrackPoint
TModifierAssistants::Interpolator::interpolate(double index) {
  TTrackPoint p = track.original ? track.calcPointFromOriginal(index)
                                 : track.interpolateLinear(index);
  return guidelines.empty() ? p : guidelines.front()->smoothTransformPoint(p, magnetism);
}


//*****************************************************************************************
//    TModifierAssistants implementation
//*****************************************************************************************


TModifierAssistants::TModifierAssistants(double magnetism):
  magnetism(magnetism),
  sensitiveLength(50.0) { }


bool
TModifierAssistants::scanAssistants(
  const TPointD *positions,
  int positionsCount,
  TGuidelineList *outGuidelines,
  bool draw,
  bool enabledOnly,
  bool drawGuidelines ) const
{
  if (TInputManager *manager = getManager())
  if (TInputHandler *handler = manager->getHandler())
    return TAssistant::scanAssistants(
      handler->inputGetTool(),
      positions,
      positionsCount,          
      outGuidelines,
      draw,
      enabledOnly,
      magnetism > 0,
      drawGuidelines,
      nullptr );
  return false;
}


void
TModifierAssistants::modifyTrack(
  const TTrack &track,
  TTrackList &outTracks )
{
  if (!track.handler) {
    Handler *handler = new Handler();
    track.handler = handler;
    handler->track = new TTrack(track);
    std::vector<VectorAssistants::Guide> guides;
    if (magnetism > 0.0 && !track.empty() &&
        !scanAssistants(nullptr, 0, nullptr, false, true, false)) {
      TInputManager *manager = getManager();
      TInputHandler *input   = manager ? manager->getHandler() : nullptr;
      TTool *tool            = input ? input->inputGetTool() : nullptr;
      if (!TReplicator::scanReplicators(tool, nullptr, nullptr, false, true,
                                        false, false, nullptr))
        guides = scanVectorGuides(tool, track.front().position);
    }
    if (guides.empty())
      new Interpolator(*handler->track, magnetism);
    else
      new VectorAssistantInterpolator(*handler->track, std::move(guides),
                                      magnetism);
  }
  
  Handler *handler = dynamic_cast<Handler*>(track.handler.getPointer());
  if (!handler)
    return;
  
  outTracks.push_back(handler->track);
  TTrack &subTrack = *handler->track;
  if (auto *vector = dynamic_cast<VectorAssistantInterpolator *>(
          subTrack.getInterpolator().getPointer())) {
    if (track.changed()) vector->update(track);
    return;
  }
  Interpolator *intr = dynamic_cast<Interpolator*>(subTrack.getInterpolator().getPointer());
  if (!intr)
    return;
  
  if (!track.changed())
    return;
  
  // remove points
  int start = track.size() - track.pointsAdded;
  if (start < 0) start = 0;

  if (intr->magnetism && start <= 0) {
    intr->guidelines.clear();
    scanAssistants(&track[0].position, 1, &intr->guidelines, false, true, false);
  }
  
  bool fixed = subTrack.fixedSize() || intr->guidelines.size() <= 1;

  // select guideline
  if (!fixed)
  if (TInputManager *manager = getManager())
  if (TInputHandler *handler = manager->getHandler())
  if (TTool *tool = handler->inputGetTool())
  if (TToolViewer *viewer = tool->getViewer()) {
    TAffine trackToScreen = tool->getMatrix();
    if (tool->getToolType() & TTool::LevelTool)
      if (TObjectHandle *objHandle = TTool::getApplication()->getCurrentObject())
        if (!objHandle->isSpline())
          trackToScreen *= TScale(viewer->getDpiScale().x, viewer->getDpiScale().y);
    trackToScreen *= viewer->get3dViewMatrix().get2d();
    TGuidelineP guideline = TGuideline::findBest(intr->guidelines, track, trackToScreen, fixed);
    if (guideline != intr->guidelines.front())
      for(int i = 1; i < (int)intr->guidelines.size(); ++i)
        if (intr->guidelines[i] == guideline) {
          std::swap(intr->guidelines[i], intr->guidelines.front());
          start = 0;
          break;
        }
  }

  // add points
  subTrack.truncate(start);
  for(int i = start; i < track.size(); ++i)
    subTrack.push_back( intr->interpolate(i), false );
  
  // fix points
  if (fixed || track.fixedFinished())
    subTrack.fix_to(track.fixedSize());

  track.resetChanges();
}

TRectD TModifierAssistants::calcDrawBounds(const TTrackList &tracks,
                                           const THoverList &) {
  for (const TTrackP &track : tracks)
    if (Handler *handler = dynamic_cast<Handler *>(track->handler.getPointer()))
      if (dynamic_cast<VectorAssistantInterpolator *>(
              handler->track->getInterpolator().getPointer()))
        return TConsts::infiniteRectD;
  if (scanAssistants(NULL, 0, NULL, false, false, false))
    return TConsts::infiniteRectD;
  return TRectD();
}

void
TModifierAssistants::drawTrack(const TTrack &track) {
  Handler *handler = dynamic_cast<Handler*>(track.handler.getPointer());
  if (!handler) return;
  
  TTrack &subTrack = *handler->track;
  if (auto *vector = dynamic_cast<VectorAssistantInterpolator *>(
          subTrack.getInterpolator().getPointer())) {
    vector->draw();
    return;
  }
  Interpolator *intr = dynamic_cast<Interpolator*>(subTrack.getInterpolator().getPointer());
  if (!intr) return;
  
  const TGuidelineList &guidelines = intr->guidelines;
  if (guidelines.empty())
    return;
  
  guidelines.front()->draw(true);
  for(TGuidelineList::const_iterator i = guidelines.begin() + 1; i != guidelines.end(); ++i)
    (*i)->draw();
}


void
TModifierAssistants::draw(const TTrackList &tracks, const THoverList &hovers) {
  THoverList allHovers;
  allHovers.reserve(hovers.size() + tracks.size());
  if (tracks.empty()) // hide hovers if track exists
    allHovers.insert(allHovers.end(), hovers.begin(), hovers.end());
  for(TTrackList::const_iterator i = tracks.begin(); i != tracks.end(); ++i)
    if (Handler *handler = dynamic_cast<Handler*>((*i)->handler.getPointer()))
      allHovers.push_back( handler->track->back().position );
  
  // draw assistants and guidelines
  scanAssistants(
    allHovers.empty() ? NULL : &allHovers.front(),
    (int)allHovers.size(),
    nullptr,
    true,
    false,
    true );

  // draw tracks
  TInputModifier::drawTracks(tracks);
}
