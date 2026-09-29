//------------------------------------------------------
/*! Finger Tool : 線のノイズを埋めるツール
 */
#include "tstroke.h"
#include "tools/toolutils.h"
#include "tools/tool.h"
#include "tmathutil.h"
#include "tools/cursors.h"
#include "drawutil.h"
#include "tcolorstyles.h"
#include "tundo.h"
#include "tvectorimage.h"
#include "ttoonzimage.h"
#include "tproperty.h"
#include "toonz/strokegenerator.h"
#include "toonz/ttilesaver.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/observer.h"
#include "toonz/toonzimageutils.h"
#include "toonz/levelproperties.h"
#include "toonz/stage2.h"
#include "toonz/ttileset.h"
#include "toonz/rasterstrokegenerator.h"
#include "toonz/preferences.h"
#include "tgl.h"
#include "tenv.h"

#include "trop.h"

#include "tinbetween.h"
#include "ttile.h"

#include "toonz/tpalettehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/tframehandle.h"
#include "tools/toolhandle.h"

// For Qt translation support
#include <QCoreApplication>

#include "tools/stylepicker.h"
#include "toonzqt/tselectionhandle.h"
#include "toonzqt/styleselection.h"
#include "historytypes.h"

#include <algorithm>
#include <cmath>

using namespace ToolUtils;

TEnv::IntVar FingerInvert("InknpaintFingerInvert", 0);
TEnv::DoubleVar FingerSize("InknpaintFingerSize", 10);
TEnv::IntVar FingerMode("InknpaintFingerMode", 0);  
TEnv::IntVar FingerPick("InknpaintFingerPick", 1);
TEnv::IntVar FingerSelective("InknpaintFingerSelective", 1);
TEnv::IntVar FingerThicknessStrength("FingerThicknessStrength", 40);
TEnv::IntVar FingerThicknessContract("FingerThicknessContract", 0);

//-----------------------------------------------------------------------------

const int BackgroundStyle = 0;

//-----------------------------------------------------------------------------

namespace {

// Work on a snapshot so that a dab changes only the boundary that existed at
// its start.  The paint channel is deliberately left alone.
TRect thicknessDab(const TRasterCM32P &ras, const TPoint &center, int size,
                   int strength, int inkId, bool contract) {
  if (!ras || inkId <= 0) return TRect();
  const int radius = std::max(1, size / 2);
  const int reach  = std::max(1, (strength + 24) / 25);
  TRect rect(center.x - radius - reach, center.y - radius - reach,
             center.x + radius + reach, center.y + radius + reach);
  rect = rect * ras->getBounds();
  if (rect.isEmpty()) return rect;
  TRasterCM32P before = ras->extract(rect)->clone();
  bool changed        = false;
  for (int y = std::max(rect.y0, center.y - radius);
       y <= std::min(rect.y1, center.y + radius); ++y) {
    for (int x = std::max(rect.x0, center.x - radius);
         x <= std::min(rect.x1, center.x + radius); ++x) {
      const int dx = x - center.x, dy = y - center.y;
      if (dx * dx + dy * dy > radius * radius) continue;
      TPixelCM32 old      = before->pixels(y - rect.y0)[x - rect.x0];
      const bool selected = old.getInk() == inkId && old.getTone() < 255;
      if (contract != selected) continue;
      int bestTone = contract ? 0 : 255;
      bool found   = false;
      for (int oy = -reach; oy <= reach; ++oy) {
        for (int ox = -reach; ox <= reach; ++ox) {
          if (ox * ox + oy * oy > reach * reach) continue;
          const int nx = x + ox, ny = y + oy;
          if (nx < rect.x0 || nx > rect.x1 || ny < rect.y0 || ny > rect.y1)
            continue;
          TPixelCM32 neighbor = before->pixels(ny - rect.y0)[nx - rect.x0];
          const bool ink =
              neighbor.getInk() == inkId && neighbor.getTone() < 255;
          if (contract ? !ink : ink) {
            found    = true;
            bestTone = contract ? std::max(bestTone, (int)neighbor.getTone())
                                : std::min(bestTone, (int)neighbor.getTone());
          }
        }
      }
      if (!found) continue;
      // Each pass approaches the target tone; subsequent passes build up.
      const int tone =
          contract
              ? std::min(255, (int)old.getTone() +
                                  std::max(1, strength * (255 - old.getTone()) /
                                                  100))
              : std::max(
                    0, (int)old.getTone() -
                           std::max(
                               1, strength * (old.getTone() - bestTone) / 100));
      TPixelCM32 &dst = ras->pixels(y)[x];
      if (contract) {
        if (tone != old.getTone()) {
          dst.setTone(tone);
          changed = true;
        }
      } else if (old.getTone() == 255 || old.getInk() == inkId) {
        const int newTone =
            old.getInk() == inkId
                ? tone
                : 255 - std::max(1, strength * (255 - bestTone) / 100);
        if (old.getInk() != inkId || old.getTone() != newTone) {
          dst.setInk(inkId);
          dst.setTone(newTone);
          changed = true;
        }
      }
    }
  }
  return changed ? rect : TRect();
}

class FingerThicknessUndo final : public TRasterUndo {
  std::vector<TPoint> m_dabs;
  int m_size, m_strength, m_inkId;
  bool m_contract;

public:
  FingerThicknessUndo(TTileSetCM32 *tiles, const std::vector<TPoint> &dabs,
                      int size, int strength, int inkId, bool contract,
                      TXshSimpleLevel *level, const TFrameId &frameId)
      : TRasterUndo(tiles, level, frameId, false, false, 0)
      , m_dabs(dabs)
      , m_size(size)
      , m_strength(strength)
      , m_inkId(inkId)
      , m_contract(contract) {}

  void redo() const override {
    TToonzImageP image = m_level->getFrame(m_frameId, true);
    if (!image) return;
    for (const TPoint &dab : m_dabs)
      thicknessDab(image->getRaster(), dab, m_size, m_strength, m_inkId,
                   m_contract);
    ToolUtils::updateSaveBox(m_level, m_frameId);
    TTool::getApplication()->getCurrentXsheet()->notifyXsheetChanged();
    notifyImageChanged();
  }
  int getSize() const override {
    return sizeof(*this) + m_dabs.size() * sizeof(TPoint) +
           TRasterUndo::getSize();
  }
  QString getToolName() override { return QString("Finger Tool (Thickness)"); }
  int getHistoryType() override { return HistoryType::FingerTool; }
};

class FingerUndo final : public TRasterUndo {
  std::vector<TThickPoint> m_points;
  int m_styleId;
  ColorType m_colorType = PAINT;
  bool m_invert;

public:
  FingerUndo(TTileSetCM32 *tileSet, const std::vector<TThickPoint> &points,
             int styleId, bool invert, TXshSimpleLevel *level,
             const TFrameId &frameId)
      : TRasterUndo(tileSet, level, frameId, false, false, 0)
      , m_points(points)
      , m_styleId(styleId)
      , m_invert(invert) {}

  void redo() const override {
    TToonzImageP image = m_level->getFrame(m_frameId, true);
    TRasterCM32P ras   = image->getRaster();
    RasterStrokeGenerator m_rasterTrack(ras, FINGER, m_colorType, m_styleId,
                                        m_points[0], m_invert, 0, false, false);
    m_rasterTrack.setPointsSequence(m_points);
    m_rasterTrack.generateStroke(true);
    image->setSavebox(image->getSavebox() +
                      m_rasterTrack.getBBox(m_rasterTrack.getPointsSequence()));

    ToolUtils::updateSaveBox();

    TTool::getApplication()->getCurrentXsheet()->notifyXsheetChanged();
    notifyImageChanged();
  }

  int getSize() const override {
    return sizeof(*this) + TRasterUndo::getSize();
  }

  QString getToolName() override { return QString("Finger Tool"); }
  int getHistoryType() override { return HistoryType::FingerTool; }
};

//-------------------------------------------------------------------------------------------

void drawLine(const TPointD &point, const TPointD &centre, bool horizontal,
              bool isDecimal) {
  if (!isDecimal) {
    if (horizontal) {
      tglDrawSegment(TPointD(point.x - 1.5, point.y + 0.5) + centre,
                     TPointD(point.x - 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.y - 0.5, -point.x + 1.5) + centre,
                     TPointD(point.y - 0.5, -point.x + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, -point.y + 0.5) + centre,
                     TPointD(-point.x - 0.5, -point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, point.x - 0.5) + centre,
                     TPointD(-point.y - 0.5, point.x + 0.5) + centre);

      tglDrawSegment(TPointD(point.y - 0.5, point.x + 0.5) + centre,
                     TPointD(point.y - 0.5, point.x - 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 0.5, -point.y + 0.5) + centre,
                     TPointD(point.x - 1.5, -point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, -point.x + 0.5) + centre,
                     TPointD(-point.y - 0.5, -point.x + 1.5) + centre);
      tglDrawSegment(TPointD(-point.x - 0.5, point.y + 0.5) + centre,
                     TPointD(-point.x + 0.5, point.y + 0.5) + centre);
    } else {
      tglDrawSegment(TPointD(point.x - 1.5, point.y + 1.5) + centre,
                     TPointD(point.x - 1.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 1.5, point.y + 0.5) + centre,
                     TPointD(point.x - 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 0.5, -point.x + 1.5) + centre,
                     TPointD(point.y - 0.5, -point.x + 1.5) + centre);
      tglDrawSegment(TPointD(point.y - 0.5, -point.x + 1.5) + centre,
                     TPointD(point.y - 0.5, -point.x + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, -point.y - 0.5) + centre,
                     TPointD(-point.x + 0.5, -point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, -point.y + 0.5) + centre,
                     TPointD(-point.x - 0.5, -point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 1.5, point.x - 0.5) + centre,
                     TPointD(-point.y - 0.5, point.x - 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, point.x - 0.5) + centre,
                     TPointD(-point.y - 0.5, point.x + 0.5) + centre);

      tglDrawSegment(TPointD(point.y + 0.5, point.x - 0.5) + centre,
                     TPointD(point.y - 0.5, point.x - 0.5) + centre);
      tglDrawSegment(TPointD(point.y - 0.5, point.x - 0.5) + centre,
                     TPointD(point.y - 0.5, point.x + 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 1.5, -point.y - 0.5) + centre,
                     TPointD(point.x - 1.5, -point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 1.5, -point.y + 0.5) + centre,
                     TPointD(point.x - 0.5, -point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 1.5, -point.x + 1.5) + centre,
                     TPointD(-point.y - 0.5, -point.x + 1.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, -point.x + 1.5) + centre,
                     TPointD(-point.y - 0.5, -point.x + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, point.y + 1.5) + centre,
                     TPointD(-point.x + 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, point.y + 0.5) + centre,
                     TPointD(-point.x - 0.5, point.y + 0.5) + centre);
    }
  } else {
    if (horizontal) {
      tglDrawSegment(TPointD(point.x - 0.5, point.y + 0.5) + centre,
                     TPointD(point.x + 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 0.5, point.x - 0.5) + centre,
                     TPointD(point.y + 0.5, point.x + 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 0.5, -point.x + 0.5) + centre,
                     TPointD(point.y + 0.5, -point.x - 0.5) + centre);
      tglDrawSegment(TPointD(point.x + 0.5, -point.y - 0.5) + centre,
                     TPointD(point.x - 0.5, -point.y - 0.5) + centre);
      tglDrawSegment(TPointD(-point.x - 0.5, -point.y - 0.5) + centre,
                     TPointD(-point.x + 0.5, -point.y - 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, -point.x + 0.5) + centre,
                     TPointD(-point.y - 0.5, -point.x - 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, point.x - 0.5) + centre,
                     TPointD(-point.y - 0.5, point.x + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, point.y + 0.5) + centre,
                     TPointD(-point.x - 0.5, point.y + 0.5) + centre);
    } else {
      tglDrawSegment(TPointD(point.x - 0.5, point.y + 1.5) + centre,
                     TPointD(point.x - 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 0.5, point.y + 0.5) + centre,
                     TPointD(point.x + 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 1.5, point.x - 0.5) + centre,
                     TPointD(point.y + 0.5, point.x - 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 0.5, point.x - 0.5) + centre,
                     TPointD(point.y + 0.5, point.x + 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 1.5, -point.x + 0.5) + centre,
                     TPointD(point.y + 0.5, -point.x + 0.5) + centre);
      tglDrawSegment(TPointD(point.y + 0.5, -point.x + 0.5) + centre,
                     TPointD(point.y + 0.5, -point.x - 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 0.5, -point.y - 1.5) + centre,
                     TPointD(point.x - 0.5, -point.y - 0.5) + centre);
      tglDrawSegment(TPointD(point.x - 0.5, -point.y - 0.5) + centre,
                     TPointD(point.x + 0.5, -point.y - 0.5) + centre);

      tglDrawSegment(TPointD(-point.x + 0.5, -point.y - 1.5) + centre,
                     TPointD(-point.x + 0.5, -point.y - 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, -point.y - 0.5) + centre,
                     TPointD(-point.x - 0.5, -point.y - 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 1.5, -point.x + 0.5) + centre,
                     TPointD(-point.y - 0.5, -point.x + 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, -point.x + 0.5) + centre,
                     TPointD(-point.y - 0.5, -point.x - 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 1.5, point.x - 0.5) + centre,
                     TPointD(-point.y - 0.5, point.x - 0.5) + centre);
      tglDrawSegment(TPointD(-point.y - 0.5, point.x - 0.5) + centre,
                     TPointD(-point.y - 0.5, point.x + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, point.y + 1.5) + centre,
                     TPointD(-point.x + 0.5, point.y + 0.5) + centre);
      tglDrawSegment(TPointD(-point.x + 0.5, point.y + 0.5) + centre,
                     TPointD(-point.x - 0.5, point.y + 0.5) + centre);
    }
  }
}

//-------------------------------------------------------------------------------------------------------

void drawEmptyCircle(int thick, const TPointD &mousePos, bool isPencil,
                     bool isLxEven, bool isLyEven) {
  TPointD pos = mousePos;
  if (isLxEven) pos.x += 0.5;
  if (isLyEven) pos.y += 0.5;
  if (!isPencil)
    tglDrawCircle(pos, (thick + 1) * 0.5);
  else {
    int x = 0, y = tround((thick * 0.5) - 0.5);
    int d           = 3 - 2 * (int)(thick * 0.5);
    bool horizontal = true, isDecimal = thick % 2 != 0;
    drawLine(TPointD(x, y), pos, horizontal, isDecimal);
    while (y > x) {
      if (d < 0) {
        d          = d + 4 * x + 6;
        horizontal = true;
      } else {
        d          = d + 4 * (x - y) + 10;
        horizontal = false;
        y--;
      }
      x++;
      drawLine(TPointD(x, y), pos, horizontal, isDecimal);
    }
  }
}

}  // namespace

//-----------------------------------------------------------------------------

class FingerTool final : public TTool {
  Q_DECLARE_TR_FUNCTIONS(FingerTool)

  RasterStrokeGenerator *m_rasterTrack;

  bool m_firstTime;

  double m_pointSize, m_distance2;

  bool m_selecting;
  TTileSaverCM32 *m_tileSaver;

  TPointD m_brushPos;

  TIntProperty m_toolSize;
  TEnumProperty m_mode;
  TBoolProperty m_pick;
  TBoolProperty m_invert;
  TBoolProperty m_emptyOnly;
  TIntProperty m_strength;
  TBoolProperty m_contract;
  std::vector<TPoint> m_thicknessDabs;
  TPoint m_lastThicknessDab;
  int m_thicknessStyle     = 0;
  int m_thicknessSize      = 0;
  int m_thicknessStrength  = 0;
  bool m_thicknessContract = false;

  TPropertyGroup m_prop;
  int m_cursor;

  int m_oldStyle = 0;

  /*---	作業中のFrameIdをクリック時に保存し、マウスリリース時（Undoの登録時）
                  に別のフレームに移動している場合があるため ---*/
  TFrameId m_workingFrameId;

  /*-- 最初のクリックでStyleを切り替える --*/
  void pick(const TPointD &pos);
  void addThicknessDabs(const TPointD &pos);

public:
  FingerTool();

  void draw() override;
  void update(TToonzImageP ti, TRectD area);

  void updateTranslation() override;

  void leftButtonDown(const TPointD &pos, const TMouseEvent &e) override;
  void leftButtonDrag(const TPointD &pos, const TMouseEvent &e) override;
  void leftButtonUp(const TPointD &pos, const TMouseEvent &) override;
  void mouseMove(const TPointD &pos, const TMouseEvent &e) override;
  void onEnter() override;
  void onLeave() override;
  void onActivate() override;
  void onDeactivate() override;
  bool onPropertyChanged(std::string propertyName) override;

  TPropertyGroup *getProperties(int targetType) override { return &m_prop; }
  ToolType getToolType() const override { return TTool::LevelWriteTool; }
  int getCursorId() const override { return m_cursor; }

  int getColorClass() const { return 2; }

  /*--
   * ドラッグ中にツールが切り替わった場合に備え、onDeactivateにもMouseReleaseと同じ処理を行う
   * --*/
  void finishBrush();
};

FingerTool fingerTool;

//=============================================================================
//
//  InkPaintTool implementation
//
//-----------------------------------------------------------------------------

FingerTool::FingerTool()
    : TTool("T_Finger")
    , m_rasterTrack(0)
    , m_pointSize(-1)
    , m_selecting(false)
    , m_tileSaver(0)
    , m_cursor(ToolCursor::EraserCursor)
    , m_toolSize("Size:", 1, 1000, 10, false)
    , m_mode("Mode:")
    , m_pick("Pick", true)
    , m_invert("Invert", false)
    , m_emptyOnly("Empty Only", true)
    , m_strength("Strength:", 1, 100, 40)
    , m_contract("Contract", false)
    , m_firstTime(true)
    , m_workingFrameId(TFrameId()) {
  bind(TTool::ToonzImage);

  m_toolSize.setNonLinearSlider();
  //ColorType
  m_mode.addValue(L"Line");
  m_mode.addValue(L"Area");
  m_mode.addValue(L"Thickness");
  m_prop.bind(m_toolSize);
  m_prop.bind(m_mode);
  m_prop.bind(m_pick);
  m_prop.bind(m_invert);
  m_prop.bind(m_emptyOnly);
  m_prop.bind(m_strength);
  m_prop.bind(m_contract);

  m_emptyOnly.setId("EmptyOnly");
  m_invert.setId("Invert");
  m_contract.setId("Contract");
}

//-----------------------------------------------------------------------------

void FingerTool::updateTranslation() {
  m_toolSize.setQStringName(tr("Size:"));
  m_mode.setQStringName(tr("Mode:"));
  m_pick.setQStringName(tr("Pick"));
  m_invert.setQStringName(tr("Invert", NULL));
  m_emptyOnly.setQStringName(tr("Empty Only", NULL));
  m_strength.setQStringName(tr("Strength:"));
  m_contract.setQStringName(tr("Contract"));
}

//-----------------------------------------------------------------------------

void FingerTool::draw() {
  if (m_pointSize == -1) {
    return;
  }

  // If toggled off, don't draw brush outline
  if (!Preferences::instance()->isCursorOutlineEnabled()) return;

  TToonzImageP ti = (TToonzImageP)getImage(false);
  if (!ti) return;
  TRasterP ras = ti->getRaster();
  int lx       = ras->getLx();
  int ly       = ras->getLy();

  if ((ToonzCheck::instance()->getChecks() & ToonzCheck::eInk) ||
      (ToonzCheck::instance()->getChecks() & ToonzCheck::ePaint))
    glColor3d(0.5, 0.8, 0.8);
  else
    glColor3d(1.0, 0.0, 0.0);

  drawEmptyCircle(m_toolSize.getValue(), m_brushPos, true, lx % 2 == 0,
                  ly % 2 == 0);
}

//-----------------------------------------------------------------------------

const UINT pointCount = 20;

//-----------------------------------------------------------------------------

bool FingerTool::onPropertyChanged(std::string propertyName) {
  /*-- サイズ --*/
  if (propertyName == m_toolSize.getName()) {
    FingerSize = m_toolSize.getValue();
    double x   = m_toolSize.getValue();

    double minRange = 1;
    double maxRange = 100;

    double minSize = 0.01;
    double maxSize = 100;

    m_pointSize =
        (x - minRange) / (maxRange - minRange) * (maxSize - minSize) + minSize;
    invalidate();
  }
  
  // Mode
  else if (propertyName == m_mode.getName()) {
    FingerMode = (ColorType)(m_mode.getIndex());
  }

  // Pick
  else if (propertyName == m_pick.getName()) {
    FingerPick = (int)(m_pick.getValue());
  }

  // Invert
  else if (propertyName == m_invert.getName()) {
    FingerInvert = (int)(m_invert.getValue());
  }

  else if (propertyName == m_emptyOnly.getName()) {
    FingerSelective = (int)(m_emptyOnly.getValue());
  } else if (propertyName == m_strength.getName()) {
    FingerThicknessStrength = m_strength.getValue();
  } else if (propertyName == m_contract.getName()) {
    FingerThicknessContract = m_contract.getValue() ? 1 : 0;
  }

  return true;
}

//-----------------------------------------------------------------------------

void FingerTool::leftButtonDown(const TPointD &pos, const TMouseEvent &e) {
  if (m_pick.getValue()) pick(pos);

  m_selecting = true;
  TImageP image(getImage(true));

  if (TToonzImageP ti = image) {
    TRasterCM32P ras = ti->getRaster();
    if (ras) {
      if (m_mode.getIndex() == 2) {
        m_thicknessStyle = TTool::getApplication()->getCurrentLevelStyleIndex();
        m_thicknessSize  = m_toolSize.getValue();
        m_thicknessStrength = m_strength.getValue();
        m_thicknessContract = m_contract.getValue();
        m_thicknessDabs.clear();
        if (m_thicknessStyle <= 0) {
          m_selecting = false;
          return;
        }
        m_tileSaver = new TTileSaverCM32(ras, new TTileSetCM32(ras->getSize()));
        m_workingFrameId = getFrameId();
        addThicknessDabs(pos);
        return;
      }
      int thickness = m_toolSize.getValue();
      int styleId   = TTool::getApplication()->getCurrentLevelStyleIndex();
      TTileSetCM32 *tileSet = new TTileSetCM32(ras->getSize());
      m_tileSaver           = new TTileSaverCM32(ras, tileSet);
      m_rasterTrack         = new RasterStrokeGenerator(
          ras, FINGER, (ColorType)m_mode.getIndex(), styleId,
          TThickPoint(pos + convert(ras->getCenter()), thickness),
          m_mode.getIndex() == 1 ? m_emptyOnly.getValue() : false,
          m_mode.getIndex() == 0 ? m_invert.getValue() : false,
          false, false);

      /*-- 作業中Fidを現在のFIDにする --*/
      m_workingFrameId = getFrameId();

      m_tileSaver->save(m_rasterTrack->getLastRect());
      TRect modifiedBbox = m_rasterTrack->generateLastPieceOfStroke(true);
      invalidate();
    }
  }
}

//-----------------------------------------------------------------------------

void FingerTool::leftButtonDrag(const TPointD &pos, const TMouseEvent &e) {
  if (!m_selecting) return;

  if (m_mode.getIndex() == 2 && m_tileSaver) {
    addThicknessDabs(pos);
    return;
  }

  m_brushPos = TPointD(tround(pos.x - 0.5), tround(pos.y - 0.5));

  if (TToonzImageP ri = TImageP(getImage(true))) {
    /*---	マウスを動かしながらショートカットで切り替わった場合、
                    いきなりleftButtonDragから呼ばれることがあり、
                    m_rasterTrackが無くて落ちることがある。 ---*/
    if (m_rasterTrack) {
      int thickness = m_toolSize.getValue();
      m_rasterTrack->add(TThickPoint(
          m_brushPos + convert(ri->getRaster()->getCenter()), thickness));
      m_tileSaver->save(m_rasterTrack->getLastRect());
      TRect modifiedBbox = m_rasterTrack->generateLastPieceOfStroke(true);
      invalidate();
    }
  }
}

//-----------------------------------------------------------------------------

void FingerTool::leftButtonUp(const TPointD &pos, const TMouseEvent &) {
  if (!m_selecting) return;

  m_brushPos = TPointD(tround(pos.x - 0.5), tround(pos.y - 0.5));

  finishBrush();
  if(m_pick.getValue())
    getApplication()->setCurrentLevelStyleIndex(m_oldStyle);
}

//-----------------------------------------------------------------------------

void FingerTool::mouseMove(const TPointD &pos, const TMouseEvent &e) {
  m_brushPos = TPointD(tround(pos.x - 0.5), tround(pos.y - 0.5));
  invalidate();
}

//-----------------------------------------------------------------------------

void FingerTool::onEnter() {
  if (m_firstTime) {
    m_invert.setValue(FingerInvert ? 1 : 0);
    m_toolSize.setValue(FingerSize);
    m_mode.setIndex(FingerMode);
    m_pick.setValue(FingerPick ? 1 : 0);
    m_emptyOnly.setValue(FingerSelective ? 1 : 0);
    m_strength.setValue(FingerThicknessStrength);
    m_contract.setValue(FingerThicknessContract != 0);
    m_firstTime = false;
  }
  double x = m_toolSize.getValue();

  double minRange = 1;
  double maxRange = 100;

  double minSize = 0.01;
  double maxSize = 100;

  m_pointSize =
      (x - minRange) / (maxRange - minRange) * (maxSize - minSize) + minSize;

  if ((TToonzImageP)getImage(false))
    m_cursor = ToolCursor::PenCursor;
  else
    m_cursor = ToolCursor::CURSOR_NO;
}

//-----------------------------------------------------------------------------

void FingerTool::onLeave() { m_pointSize = -1; }

//-----------------------------------------------------------------------------

void FingerTool::onActivate() { onEnter(); }

//-----------------------------------------------------------------------------

void FingerTool::onDeactivate() {
  /*---
   * マウスドラッグ中(m_selecting=true)にツールが切り替わったときに線を終わらせる
   * ---*/
  if (m_selecting) finishBrush();
}

//-----------------------------------------------------------------------------
/*!
 * ドラッグ中にツールが切り替わった場合に備え、onDeactivateにもMouseReleaseと同じ処理を行う
 */
void FingerTool::finishBrush() {
  if (m_tileSaver && !m_rasterTrack) {
    TTool::Application *app = TTool::getApplication();
    TXshSimpleLevel *level  = app->getCurrentLevel()->getSimpleLevel();
    TFrameId frameId =
        m_workingFrameId.isEmptyFrame() ? getCurrentFid() : m_workingFrameId;
    if (level && !m_thicknessDabs.empty()) {
      TUndoManager::manager()->add(new FingerThicknessUndo(
          m_tileSaver->getTileSet(), m_thicknessDabs, m_thicknessSize,
          m_thicknessStrength, m_thicknessStyle, m_thicknessContract, level,
          frameId));
      ToolUtils::updateSaveBox(level, frameId);
      notifyImageChanged(frameId);
    }
    delete m_tileSaver;
    m_tileSaver = nullptr;
    m_thicknessDabs.clear();
    m_workingFrameId = TFrameId();
    m_selecting      = false;
    invalidate();
    return;
  }
  if (TToonzImageP ti = (TToonzImageP)getImage(true)) {
    if (m_rasterTrack) {
      int thickness = m_toolSize.getValue();
      m_rasterTrack->add(TThickPoint(
          m_brushPos + convert(ti->getRaster()->getCenter()), thickness));
      m_tileSaver->save(m_rasterTrack->getLastRect());
      TRect modifiedBbox = m_rasterTrack->generateLastPieceOfStroke(true, true);

      TTool::Application *app   = TTool::getApplication();
      TXshLevel *level          = app->getCurrentLevel()->getLevel();
      TXshSimpleLevelP simLevel = level->getSimpleLevel();

      TFrameId frameId =
          m_workingFrameId.isEmptyFrame() ? getCurrentFid() : m_workingFrameId;

      TUndoManager::manager()->add(new FingerUndo(
          m_tileSaver->getTileSet(), m_rasterTrack->getPointsSequence(),
          m_rasterTrack->getStyleId(), m_rasterTrack->isSelective(),
          simLevel.getPointer(), frameId));
      ToolUtils::updateSaveBox();

      /*! FIdを指定して、作業中にフレームが動いても、
              クリック時のFidのサムネイルが更新されるようにする。
      */
      notifyImageChanged(frameId);

      invalidate();
      delete m_rasterTrack;
      m_rasterTrack = 0;
      delete m_tileSaver;
      m_tileSaver = nullptr;

      /*-- 作業中fIdをリセット --*/
      m_workingFrameId = TFrameId();
    }
  }
  m_selecting = false;
}

void FingerTool::addThicknessDabs(const TPointD &pos) {
  TToonzImageP image = (TToonzImageP)getImage(true);
  if (!image || !m_tileSaver) return;
  TRasterCM32P ras = image->getRaster();
  if (!ras) return;
  TPoint target(tround(pos.x + ras->getCenter().x),
                tround(pos.y + ras->getCenter().y));
  const int spacing = std::max(1, m_thicknessSize / 4);
  TPoint start      = m_thicknessDabs.empty() ? target : m_lastThicknessDab;
  const double dx = target.x - start.x, dy = target.y - start.y;
  const double distance = std::sqrt(dx * dx + dy * dy);
  const int steps = m_thicknessDabs.empty() ? 1 : (int)(distance / spacing);
  for (int i = 1; i <= steps; ++i) {
    TPoint dab       = m_thicknessDabs.empty()
                           ? target
                           : TPoint(tround(start.x + dx * i / steps),
                                    tround(start.y + dy * i / steps));
    const int reach  = std::max(1, (m_thicknessStrength + 24) / 25);
    const int radius = std::max(1, m_thicknessSize / 2) + reach;
    m_tileSaver->save(
        TRect(dab.x - radius, dab.y - radius, dab.x + radius, dab.y + radius));
    TRect changed = thicknessDab(ras, dab, m_thicknessSize, m_thicknessStrength,
                                 m_thicknessStyle, m_thicknessContract);
    if (!changed.isEmpty()) image->setSavebox(image->getSavebox() + changed);
    m_thicknessDabs.push_back(dab);
    m_lastThicknessDab = dab;
  }
  if (steps) {
    m_brushPos = TPointD(tround(pos.x - 0.5), tround(pos.y - 0.5));
    notifyImageChanged();
    invalidate();
  }
}

void FingerTool::pick(const TPointD &pos) {
  m_oldStyle = getApplication()->getCurrentLevelStyleIndex();
  int modeValue;
  if (m_mode.getIndex() == INK || m_mode.getIndex() == 2)
    modeValue = 1;//LINES
  else
    modeValue = 0;//AREAS

  TImageP image    = getImage(false);
  TToonzImageP ti  = image;
  TVectorImageP vi = image;
  TXshSimpleLevel *level =
      getApplication()->getCurrentLevel()->getSimpleLevel();
  if (!ti || !level) return;

  /*--- 画面外をpickしても拾えないようにする ---*/
  if (!m_viewer->getGeometry().contains(pos)) return;

  int subsampling = level->getImageSubsampling(getCurrentFid());

  StylePicker picker(getViewer()->viewerWidget(), image);

  int styleId =
      picker.pickStyleId(TScale(1.0 / subsampling) * pos + TPointD(-0.5, -0.5),
                         getPixelSize() * getPixelSize(), 1.0, modeValue);
  
  if (styleId < 0) return;

  if (modeValue == 1)  // LINES
  {
    // pickLineモードのとき、取得Styleが0の場合はカレントStyleを変えない。
    if (styleId == 0) return;

    /*---
     * pickLineモードのとき、PurePaintの部分をクリックしてもカレントStyleを変えない
     * ---*/
    if (ti && picker.pickTone(TScale(1.0 / subsampling) * pos +
                              TPointD(-0.5, -0.5)) == 255)
      return;
  }

  /*--- Styleを選択している場合は選択を解除する ---*/
  TSelection *selection =
      TTool::getApplication()->getCurrentSelection()->getSelection();
  if (selection) {
    TStyleSelection *styleSelection =
        dynamic_cast<TStyleSelection *>(selection);
    if (styleSelection) styleSelection->selectNone();
  }

  getApplication()->setCurrentLevelStyleIndex(styleId);
}
