#include "fullcolorfilltool.h"

#include "toonz/stage2.h"
#include "tools/cursors.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/trasterimageutils.h"
#include "toonz/ttileset.h"
#include "toonz/ttilesaver.h"
#include "toonz/levelproperties.h"
#include "toonz/preferences.h"
#include "toonz/txsheethandle.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/txshcell.h"

#include "tools/toolhandle.h"
#include "tools/toolutils.h"

#include "tenv.h"
#include "tpalette.h"
#include "tsystem.h"
#include "tinbetween.h"
#include "drawutil.h"

#include <vector>

using namespace ToolUtils;

TEnv::IntVar FullColorMinFillDepth("InknpaintFullColorMinFillDepth", 4);
TEnv::IntVar FullColorMaxFillDepth("InknpaintFullColorMaxFillDepth", 12);
TEnv::IntVar FullColorFillRange("InknpaintFullColorFillRange", 0);

#define LINEAR_INTERPOLATION L"Linear"
#define EASE_IN_INTERPOLATION L"Ease In"
#define EASE_OUT_INTERPOLATION L"Ease Out"
#define EASE_IN_OUT_INTERPOLATION L"Ease In/Out"

namespace {

//=============================================================================
// FullColorFillUndo
//-----------------------------------------------------------------------------

class FullColorFillUndo final : public TFullColorRasterUndo {
  FillParameters m_params;
  bool m_saveboxOnly;

public:
  FullColorFillUndo(TTileSetFullColor *tileSet, const FillParameters &params,
                    TXshSimpleLevel *sl, const TFrameId &fid, bool saveboxOnly)
      : TFullColorRasterUndo(tileSet, sl, fid, false, false, 0)
      , m_params(params)
      , m_saveboxOnly(saveboxOnly) {}

  void redo() const override {
    TRasterImageP image = getImage();
    if (!image) return;
    TRaster32P r;
    if (m_saveboxOnly) {
      TRectD temp = image->getBBox();
      TRect ttemp = convert(temp);
      r           = image->getRaster()->extract(ttemp);
    } else
      r = image->getRaster();

    fullColorFill(r, m_params);

    TTool::Application *app = TTool::getApplication();
    if (app) {
      app->getCurrentXsheet()->notifyXsheetChanged();
      notifyImageChanged();
    }
  }

  int getSize() const override {
    return sizeof(*this) + TFullColorRasterUndo::getSize();
  }

  QString getToolName() override {
    return QString("Fill Tool : %1")
        .arg(QString::fromStdWString(m_params.m_fillType));
  }
  int getHistoryType() override { return HistoryType::FillTool; }
};

//=============================================================================
// doFill
//-----------------------------------------------------------------------------

void doFill(const TImageP &img, const TPointD &pos, FillParameters &params,
            bool isShiftFill, TXshSimpleLevel *sl, const TFrameId &fid) {
  TTool::Application *app = TTool::getApplication();
  if (!app || !sl) return;

  if (TRasterImageP ri = TRasterImageP(img)) {
    TPoint offs(0, 0);
    TRaster32P ras = ri->getRaster();
    // only accept 32bpp images for now
    if (!ras.getPointer() || ras->isEmpty()) return;

    ras->lock();

    TTileSetFullColor *tileSet = new TTileSetFullColor(ras->getSize());
    TTileSaverFullColor tileSaver(ras, tileSet);
    TDimension imageSize = ras->getSize();
    TPointD p(imageSize.lx % 2 ? 0.0 : 0.5, imageSize.ly % 2 ? 0.0 : 0.5);

    /*-- params.m_p = convert(pos-p)では、マイナス座標でずれが生じる --*/
    TPointD tmp_p = pos - p;
    params.m_p = TPoint((int)floor(tmp_p.x + 0.5), (int)floor(tmp_p.y + 0.5));

    params.m_p += ras->getCenter();
    params.m_p -= offs;
    params.m_shiftFill = isShiftFill;

    TRect rasRect(ras->getSize());
    if (!rasRect.contains(params.m_p)) {
      ras->unlock();
      return;
    }

    fullColorFill(ras, params, &tileSaver);

    if (tileSaver.getTileSet()->getTileCount() != 0) {
      static int count = 0;
      TSystem::outputDebug("RASTERFILL" + std::to_string(count++) + "\n");
      if (offs != TPoint())
        for (int i = 0; i < tileSet->getTileCount(); i++) {
          TTileSet::Tile *t = tileSet->editTile(i);
          t->m_rasterBounds = t->m_rasterBounds + offs;
        }
      TUndoManager::manager()->add(
          new FullColorFillUndo(tileSet, params, sl, fid,
                                Preferences::instance()->getFillOnlySavebox()));
    }

    sl->getProperties()->setDirtyFlag(true);

    ras->unlock();
  }

  TTool *t = app->getCurrentTool()->getTool();
  if (t) t->notifyImageChanged();
}
};

//=============================================================================
// FullColorFillTool
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------

FullColorFillTool::FullColorFillTool()
    : TTool("T_Fill")
    , m_fillDepth("Fill Depth", 0, 15, 4, 12)
    , m_frameRange("Frame Range:")
    , m_firstRow(-1)
    , m_firstColumn(-1)
    , m_firstFrameSelected(false) {
  bind(TTool::RasterImage);
  m_prop.bind(m_fillDepth);
  m_prop.bind(m_frameRange);
  m_frameRange.addValue(L"Off");
  m_frameRange.addValue(LINEAR_INTERPOLATION);
  m_frameRange.addValue(EASE_IN_INTERPOLATION);
  m_frameRange.addValue(EASE_OUT_INTERPOLATION);
  m_frameRange.addValue(EASE_IN_OUT_INTERPOLATION);
  m_frameRange.setId("FrameRange");
}

void FullColorFillTool::updateTranslation() {
  m_fillDepth.setQStringName(tr("Fill Depth"));
  m_frameRange.setQStringName(tr("Frame Range:"));
  m_frameRange.setItemUIName(L"Off", tr("Off"));
  m_frameRange.setItemUIName(LINEAR_INTERPOLATION, tr("Linear"));
  m_frameRange.setItemUIName(EASE_IN_INTERPOLATION, tr("Ease In"));
  m_frameRange.setItemUIName(EASE_OUT_INTERPOLATION, tr("Ease Out"));
  m_frameRange.setItemUIName(EASE_IN_OUT_INTERPOLATION, tr("Ease In/Out"));
}

FillParameters FullColorFillTool::getFillParameters() const {
  FillParameters params;
  int styleId           = TTool::getApplication()->getCurrentLevelStyleIndex();
  params.m_styleId      = styleId;
  params.m_minFillDepth = (int)m_fillDepth.getValue().first;
  params.m_maxFillDepth = (int)m_fillDepth.getValue().second;

  if (m_level) params.m_palette = m_level->getPalette();
  return params;
}

void FullColorFillTool::leftButtonDown(const TPointD &pos,
                                       const TMouseEvent &e) {
  m_clickPoint  = pos;
  TXshLevel *xl = TTool::getApplication()->getCurrentLevel()->getLevel();
  if (!m_frameRange.getIndex() || !m_firstFrameSelected)
    m_level = xl ? xl->getSimpleLevel() : 0;
  FillParameters params = getFillParameters();
  if (m_frameRange.getIndex()) {
    if (!m_firstFrameSelected) {
      m_firstFrameSelected = true;
      m_firstPoint         = pos;
      m_firstFrameId       = getCurrentFid();
      m_firstRow           = getFrame();
      m_firstColumn        = getColumnIndex();
      invalidate();
      return;
    }
    fillFrameRange(pos, e, params);
    return;
  }

  doFill(getImage(true), pos, params, e.isShiftPressed(), m_level.getPointer(),
         getCurrentFid());
  invalidate();
}

void FullColorFillTool::leftButtonDrag(const TPointD &pos,
                                       const TMouseEvent &e) {
  if (m_frameRange.getIndex()) return;

  FillParameters params = getFillParameters();
  if (m_clickPoint == pos) return;
  if (!m_level || !params.m_palette) return;
  TImageP img = getImage(true);
  TPixel32 fillColor =
      params.m_palette->getStyle(params.m_styleId)->getMainColor();
  if (TRasterImageP ri = img) {
    TRaster32P ras = ri->getRaster();
    if (!ras) return;
    TPointD center = ras->getCenterD();
    TPoint ipos    = convert(pos + center);
    if (!ras->getBounds().contains(ipos)) return;
    TPixel32 pix = ras->pixels(ipos.y)[ipos.x];
    if (pix == fillColor) {
      invalidate();
      return;
    }
  } else
    return;
  doFill(img, pos, params, e.isShiftPressed(), m_level.getPointer(),
         getCurrentFid());
  invalidate();
}

bool FullColorFillTool::onPropertyChanged(std::string propertyName) {
  // Fill Depth
  if (propertyName == m_fillDepth.getName()) {
    FullColorMinFillDepth = (int)m_fillDepth.getValue().first;
    FullColorMaxFillDepth = (int)m_fillDepth.getValue().second;
  } else if (propertyName == m_frameRange.getName()) {
    FullColorFillRange = m_frameRange.getIndex();
    resetFrameRange();
  }
  return true;
}

void FullColorFillTool::onActivate() {
  static bool firstTime = true;
  if (firstTime) {
    m_fillDepth.setValue(TDoublePairProperty::Value(FullColorMinFillDepth,
                                                    FullColorMaxFillDepth));
    m_frameRange.setIndex(FullColorFillRange);
    firstTime = false;
  }
  resetFrameRange();
}

int FullColorFillTool::getCursorId() const {
  int ret = ToolCursor::FillCursor;
  if (ToonzCheck::instance()->getChecks() & ToonzCheck::eBlackBg)
    ret = ret | ToolCursor::Ex_Negate;
  return ret;
}

void FullColorFillTool::draw() {
  if (!m_frameRange.getIndex() || !m_firstFrameSelected) return;
  tglColor(TPixel::Red);
  drawCross(m_firstPoint, 6);
}

void FullColorFillTool::resetFrameRange() {
  m_firstFrameSelected = false;
  m_firstFrameId       = TFrameId();
  m_firstPoint         = TPointD();
  m_firstRow           = -1;
  m_firstColumn        = -1;
}

void FullColorFillTool::fillFrameRange(const TPointD &pos,
                                       const TMouseEvent &e,
                                       const FillParameters &params) {
  TTool::Application *app = TTool::getApplication();
  if (!app || !m_level) {
    resetFrameRange();
    return;
  }

  std::vector<std::pair<TXshSimpleLevel *, TFrameId>> frames;
  if (app->getCurrentFrame()->isEditingScene()) {
    int lastRow = getFrame();
    int step    = m_firstRow <= lastRow ? 1 : -1;
    TFrameId previousFid;
    bool hasPrevious = false;
    TXsheet *xsheet = app->getCurrentXsheet()->getXsheet();
    for (int row = m_firstRow; row != lastRow + step; row += step) {
      TXshCell cell = xsheet->getCell(row, m_firstColumn);
      TXshSimpleLevel *level = cell.getSimpleLevel();
      if (!level || level != m_level.getPointer()) continue;
      TFrameId fid = cell.getFrameId();
      if (hasPrevious && fid == previousFid) continue;
      frames.emplace_back(level, fid);
      previousFid = fid;
      hasPrevious = true;
    }
  } else {
    int firstIndex = m_level->fid2index(m_firstFrameId);
    int lastIndex  = m_level->fid2index(getCurrentFid());
    if (firstIndex < 0 || lastIndex < 0) {
      resetFrameRange();
      return;
    }
    int step       = firstIndex <= lastIndex ? 1 : -1;
    for (int index = firstIndex; index != lastIndex + step; index += step)
      frames.emplace_back(m_level.getPointer(), m_level->index2fid(index));
  }

  if (frames.empty()) {
    resetFrameRange();
    return;
  }

  TInbetween::TweenAlgorithm algorithm = TInbetween::LinearInterpolation;
  if (m_frameRange.getIndex() == 2)
    algorithm = TInbetween::EaseInInterpolation;
  else if (m_frameRange.getIndex() == 3)
    algorithm = TInbetween::EaseOutInterpolation;
  else if (m_frameRange.getIndex() == 4)
    algorithm = TInbetween::EaseInOutInterpolation;

  TUndoManager::manager()->beginBlock();
  int frameCount = frames.size();
  for (int index = 0; index < frameCount; ++index) {
    const auto &[level, fid] = frames[index];
    double t = frameCount > 1 ? static_cast<double>(index) / (frameCount - 1)
                              : 0.5;
    t = TInbetween::interpolation(t, algorithm);
    TPointD point = m_firstPoint * (1 - t) + pos * t;
    FillParameters frameParams = params;
    doFill(level->getFrame(fid, true), point, frameParams, false, level, fid);
  }
  TUndoManager::manager()->endBlock();

  if (e.isShiftPressed()) {
    m_firstPoint   = pos;
    m_firstFrameId = getCurrentFid();
    m_firstRow     = getFrame();
    m_firstColumn  = getColumnIndex();
  } else {
    if (app->getCurrentFrame()->isEditingScene()) {
      app->getCurrentColumn()->setColumnIndex(m_firstColumn);
      app->getCurrentFrame()->setFrame(m_firstRow);
    } else {
      app->getCurrentFrame()->setFid(m_firstFrameId);
    }
    resetFrameRange();
  }
  invalidate();
}

FullColorFillTool FullColorRasterFillTool;