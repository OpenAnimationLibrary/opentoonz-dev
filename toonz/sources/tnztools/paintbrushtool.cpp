

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
#include "tpixelutils.h"

#include <algorithm>
#include <cmath>
#include <QColor>

#include "tinbetween.h"
#include "ttile.h"

#include "toonz/tpalettehandle.h"
#include "toonz/paintbrushgizmosettings.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/tframehandle.h"
#include "tools/toolhandle.h"

// For Qt translation support
#include <QCoreApplication>

#include "stylepicker.h"

using namespace ToolUtils;

#define LINES L"Lines"
#define AREAS L"Areas"
#define ALL L"Lines & Areas"

TEnv::StringVar PaintBrushColorType("InknpaintPaintBrushColorType", "Areas");
TEnv::IntVar PaintBrushSelective("InknpaintPaintBrushSelective", 0);
TEnv::IntVar PaintBrushEmptyOnly("InknpaintPaintBrushEmptyOnly", 1);
TEnv::DoubleVar PaintBrushSize("InknpaintPaintBrushSize", 10);
TEnv::IntVar PaintBrushModifierLockAlpha("PaintBrushModifierLockAlpha", 0);
TEnv::IntVar PaintBrushPick("PaintBrushPick", 0);
TEnv::IntVar PaintBrushFill("PaintBrushFill", 0);

//-----------------------------------------------------------------------------

const int BackgroundStyle = 0;

// Companion styles are ordinary palette styles. The name embeds the source ID
// so variants can be found again after the palette has been saved and reopened.
// Existing companions are never recolored when they are reused.
namespace {
int paintBrushCompanionStyle(TPalette *palette, int sourceId, bool highlight) {
  if (!palette || palette->isLocked() || sourceId <= BackgroundStyle)
    return sourceId;

  TColorStyle *source = palette->getStyle(sourceId);
  if (!source || !source->hasMainColor() || source->getColorParamCount() != 1)
    return sourceId;

  // A short visible name includes the stable numeric ID of the source style.
  // Recognize old Paint Brush names so existing painted art is not duplicated.
  std::wstring label = highlight ? L"Highlight " : L"Shadow ";
  label += std::to_wstring(sourceId);

  std::wstring legacyLabel =
      highlight ? L"[Paint Brush Highlight " : L"[Paint Brush Shadow ";
  legacyLabel += std::to_wstring(sourceId);
  legacyLabel += L"]";

  // Find the original's page. Palette indices are NOT changed when new
  // companions are inserted into a page.
  TPalette::Page *page = nullptr;
  int sourcePosition   = -1;
  for (int i = 0; i < palette->getPageCount(); ++i) {
    TPalette::Page *candidate = palette->getPage(i);
    const int pos             = candidate->search(sourceId);
    if (pos >= 0) {
      page           = candidate;
      sourcePosition = pos;
      break;
    }
  }
  if (!page) return sourceId;

  for (int i = 0; i < palette->getStyleCount(); ++i) {
    TColorStyle *style = palette->getStyle(i);
    if (!style) continue;
    if (style->getName() == label || style->getName() == legacyLabel) return i;
  }

  // Avoid generating solid-color approximations from animated or multi-color
  // styles. A clone retains all original solid style properties.
  TColorStyle *companion = source->clone();
  if (!companion) return sourceId;
  TPixel32 color = source->getMainColor();

  // The Gizmo Fade field controls strength. When it has not been published
  // (e.g. the Gizmo was never opened), use the conservative 30% fallback.
  const double fadeValue =
      PaintBrushGizmoSettings::number(PaintBrushGizmoSettings::Fade, 30.0);
  const int roundedFade = static_cast<int>(std::lround(fadeValue));
  const int fade        = std::max(0, std::min(100, roundedFade));

  // The Gizmo's fade target, when edited, tints the shadow. Never let a
  // shadow target brighten a source channel. Highlights fade toward white.
  QColor shadowColor =
      PaintBrushGizmoSettings::read(PaintBrushGizmoSettings::ShadowColor)
          .value<QColor>();
  if (!shadowColor.isValid()) shadowColor = QColor(0, 0, 0);

  const auto adjusted = [highlight, fade](int channel, int shadowChannel) {
    const int target = highlight ? 255 : std::min(channel, shadowChannel);
    return (channel * (100 - fade) + target * fade + 50) / 100;
  };
  color.r = adjusted(color.r, shadowColor.red());
  color.g = adjusted(color.g, shadowColor.green());
  color.b = adjusted(color.b, shadowColor.blue());

  // Unlike the Gizmo's action buttons, the following adjustments are used
  // only if their numeric fields have explicitly been edited or activated.
  const double sign = highlight ? 1.0 : -1.0;
  const auto preset = [](const char *key) {
    return PaintBrushGizmoSettings::number(key);
  };
  const double valueScale = preset(PaintBrushGizmoSettings::ValueScale);
  const double valueShift = preset(PaintBrushGizmoSettings::ValueShift);
  const double saturationScale =
      preset(PaintBrushGizmoSettings::SaturationScale);
  const double saturationShift =
      preset(PaintBrushGizmoSettings::SaturationShift);
  const double hueShift = preset(PaintBrushGizmoSettings::HueShift);

  const auto clamp = [](double number, int maximum) {
    const int rounded = static_cast<int>(std::lround(number));
    return std::max(0, std::min(maximum, rounded));
  };
  if (valueScale || valueShift || saturationScale || saturationShift ||
      hueShift) {
    const int alpha = color.m;
    int hsv[3];
    rgb2hsv(hsv, color, 360);
    const int hueDelta = static_cast<int>(std::lround(sign * hueShift));
    hsv[0] = (hsv[0] + hueDelta % 360 + 360) % 360;

    const double saturation =
        hsv[1] * (1.0 + sign * saturationScale / 100.0) +
        sign * saturationShift * 3.6;
    hsv[1] = clamp(saturation, 360);

    const double value = hsv[2] * (1.0 + sign * valueScale / 100.0) +
                         sign * valueShift * 3.6;
    hsv[2] = clamp(value, 360);
    hsv2rgb(color, hsv, 360);
    color.m = alpha;
  }

  const double alphaScale = preset(PaintBrushGizmoSettings::AlphaScale);
  const double alphaShift = preset(PaintBrushGizmoSettings::AlphaShift);
  if (alphaScale || alphaShift) {
    const double adjustedAlpha =
        color.m * (1.0 + sign * alphaScale / 100.0) + sign * alphaShift;
    color.m = clamp(adjustedAlpha, 255);
  }
  companion->setMainColor(color);
  companion->setName(label);

  // Page insertion creates a new palette style ID without renumbering any
  // existing CM32 pixels. Highlight precedes source; shadow follows source.
  page->insertStyle(sourcePosition + (highlight ? 0 : 1), companion);
  palette->setDirtyFlag(true);
  for (int i = 0; i < page->getStyleCount(); ++i) {
    TColorStyle *style = page->getStyle(i);
    if (style && style->getName() == label) return page->getStyleId(i);
  }
  return sourceId;
}
}  // namespace

//-----------------------------------------------------------------------------

namespace {

class BrushUndo final : public TRasterUndo {
  std::vector<TThickPoint> m_points;
  int m_styleId;
  int m_pickStyleId;
  bool m_emptyOnly;
  ColorType m_colorType;
  bool m_modifierLockAlpha;
  bool m_selective;
  bool m_fillMode;

public:
  BrushUndo(TTileSetCM32 *tileSet, const std::vector<TThickPoint> &points,
            ColorType colorType, int styleId, int pickStyleId, bool selective, bool emptyOnly,
            TXshSimpleLevel *level, const TFrameId &frameId, bool lockAlpha, bool fillMode)
      : TRasterUndo(tileSet, level, frameId, false, false, 0)
      , m_points(points)
      , m_styleId(styleId)
      , m_pickStyleId(pickStyleId)
      , m_selective(selective)
      , m_emptyOnly(emptyOnly)
      , m_colorType(colorType)
      , m_modifierLockAlpha(lockAlpha),
      m_fillMode(fillMode){}

  void redo() const override {
    TToonzImageP image = m_level->getFrame(m_frameId, true);
    TRasterCM32P ras   = image->getRaster();
    RasterStrokeGenerator m_rasterTrack(ras, PAINTBRUSH, m_colorType, m_styleId,
                                        m_points[0], m_selective, m_pickStyleId,
                                        m_modifierLockAlpha, m_fillMode, m_emptyOnly);
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

  QString getToolName() override { return QString("Paint Brush Tool"); }
  int getHistoryType() override { return HistoryType::PaintBrushTool; }
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

class PaintBrushTool final : public TTool {
  Q_DECLARE_TR_FUNCTIONS(PaintBrushTool)

  RasterStrokeGenerator *m_rasterTrack;

  bool m_firstTime;

  double m_pointSize, m_distance2;

  bool m_selecting;
  TTileSaverCM32 *m_tileSaver;

  TPointD m_mousePos;

  TIntProperty m_toolSize;
  TBoolProperty m_emptyOnly;
  TEnumProperty m_colorType;
  TBoolProperty m_pick;
  TBoolProperty m_fillingMode;
  TBoolProperty m_selective;

  TPropertyGroup m_prop;
  int m_cursor;
  ColorType m_colorTypeBrush;
  int m_orignalStyle;
  /*--
     描画開始時のFrameIdを保存し、マウスリリース時（Undoの登録時）に別のフレームに
          移動している場合に備える --*/
  TFrameId m_workingFrameId;

  TBoolProperty m_modifierLockAlpha;

public:
  PaintBrushTool();

  ToolType getToolType() const override { return TTool::LevelWriteTool; }

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

  int getCursorId() const override { return m_cursor; }
  TPointD getCenteredCursorPos(const TPointD &originalCursorPos);
  void fixMousePos(TPointD pos, bool precise = false);

  int getColorClass() const { return 2; }

  /*---
   * 描画中にツールが切り替わった場合に備え、onDeactivateにもMouseReleaseと同じ終了処理を行う
   * ---*/
  void finishBrush();
  /*--- Brush、PaintBrush、EraserToolがPencilModeのときにTrueを返す。
  　　　PaintBrushはピクセルのStyleIndexを入れ替えるツールのため、
     　 アンチエイリアスは存在しない、いわば常にPencilMode ---*/
  bool isPencilModeActive() override { return true; }

  int pick(const TImageP &image, const TPointD &pos, const int frame);
};

PaintBrushTool paintBrushTool;

//=============================================================================
//
//  InkPaintTool implementation
//
//-----------------------------------------------------------------------------

PaintBrushTool::PaintBrushTool()
    : TTool("T_PaintBrush")
    , m_rasterTrack(0)
    , m_pointSize(-1)
    , m_selecting(false)
    , m_tileSaver(0)
    , m_cursor(ToolCursor::EraserCursor)
    // sostituire i nomi con quelli del current, tipo W_ToolOptions...
    , m_toolSize("Size:", 1, 1000, 10, false)  // W_ToolOptions_BrushToolSize
    , m_colorType("Mode:")                     // W_ToolOptions_InkOrPaint
    , m_selective("Selective", false)          // W_ToolOptions_Selective
    , m_emptyOnly("Empty Only", false)
    , m_firstTime(true)
    , m_workingFrameId(TFrameId())
    , m_modifierLockAlpha("Lock Alpha", false)
    , m_pick("Pick", false)
    , m_fillingMode("Paint by Filling", true) {
  m_toolSize.setNonLinearSlider();

  m_colorType.addValue(LINES);
  m_colorType.addValue(AREAS);
  m_colorType.addValue(ALL);

  bind(TTool::ToonzImage);

  m_prop.bind(m_toolSize);
  m_prop.bind(m_colorType);
  m_prop.bind(m_emptyOnly);
  m_prop.bind(m_modifierLockAlpha);
  m_prop.bind(m_pick);
  m_prop.bind(m_fillingMode);
  m_prop.bind(m_selective);

  m_emptyOnly.setId("EmptyOnly");
  m_colorType.setId("Mode");
  m_modifierLockAlpha.setId("LockAlpha");
  m_pick.setId("Pick");
  m_fillingMode.setId("FillingMode");
  m_selective.setId("Selective");
}

//-----------------------------------------------------------------------------

void PaintBrushTool::updateTranslation() {
  m_toolSize.setQStringName(tr("Size:"));

  m_colorType.setQStringName(tr("Mode:"));
  m_colorType.setItemUIName(LINES, tr("Lines"));
  m_colorType.setItemUIName(AREAS, tr("Areas"));
  m_colorType.setItemUIName(ALL, tr("Lines & Areas"));

  m_selective.setQStringName(tr("Selective", NULL));
  m_emptyOnly.setQStringName(tr("Empty Only", NULL));
  m_modifierLockAlpha.setQStringName(tr("Lock Alpha"));
  m_pick.setQStringName(tr("Pick"));
  m_fillingMode.setQStringName(tr("Paint by Filling"));
}

//-------------------------------------------------------------------------------------------------------

TPointD PaintBrushTool::getCenteredCursorPos(const TPointD &originalCursorPos) {
  TXshLevelHandle *levelHandle = m_application->getCurrentLevel();
  TXshSimpleLevel *level = levelHandle ? levelHandle->getSimpleLevel() : 0;
  TDimension resolution =
      level ? level->getProperties()->getImageRes() : TDimension(0, 0);

  bool xEven = (resolution.lx % 2 == 0);
  bool yEven = (resolution.ly % 2 == 0);

  TPointD centeredCursorPos = originalCursorPos;

  if (xEven) centeredCursorPos.x -= 0.5;
  if (yEven) centeredCursorPos.y -= 0.5;

  return centeredCursorPos;
}

//-----------------------------------------------------------------------------

void PaintBrushTool::fixMousePos(TPointD pos, bool precise) {
  m_mousePos = getCenteredCursorPos(pos);
  if (precise) {
    TPointD pp(tround(m_mousePos.x), tround(m_mousePos.y));
    m_mousePos = pp;
  }
}

//-----------------------------------------------------------------------------

void PaintBrushTool::draw() {
  /*-- MouseLeave時にBrushTipが描かれるのを防止する --*/
  if (m_pointSize == -1) return;

  // If toggled off, don't draw brush outline
  if (!Preferences::instance()->isCursorOutlineEnabled()) return;

  TToonzImageP ti = (TToonzImageP)getImage(false);
  if (!ti) return;
  TRasterP ras = ti->getRaster();
  int lx       = ras->getLx();
  int ly       = ras->getLy();

  if ((ToonzCheck::instance()->getChecks() & ToonzCheck::eInk) ||
      (ToonzCheck::instance()->getChecks() & ToonzCheck::ePaint) ||
      (ToonzCheck::instance()->getChecks() & ToonzCheck::eInk1))
    glColor3d(0.5, 0.8, 0.8);
  else
    glColor3d(1.0, 0.0, 0.0);

  drawEmptyCircle(m_toolSize.getValue(), m_mousePos, true, lx % 2 == 0,
                  ly % 2 == 0);
}

//-----------------------------------------------------------------------------

const UINT pointCount = 20;

//-----------------------------------------------------------------------------

bool PaintBrushTool::onPropertyChanged(std::string propertyName) {
  /*-- Size ---*/
  if (propertyName == m_toolSize.getName()) {
    PaintBrushSize = m_toolSize.getValue();
    double x       = m_toolSize.getValue();

    double minRange = 1;
    double maxRange = 100;

    double minSize = 0.01;
    double maxSize = 100;

    m_pointSize =
        (x - minRange) / (maxRange - minRange) * (maxSize - minSize) + minSize;
    invalidate();
  }

  // Selective
  else if (propertyName == m_selective.getName()) {
    PaintBrushSelective = (int)(m_selective.getValue());
    if (m_selective.getValue() && m_modifierLockAlpha.getValue()) {
      m_modifierLockAlpha.setValue(false);
      PaintBrushModifierLockAlpha = 0;
    }
    if (m_selective.getValue() && m_pick.getValue()) {
      m_pick.setValue(false);
      PaintBrushPick = 0;
    }
    if (m_selective.getValue() && m_emptyOnly.getValue()) {
        m_emptyOnly.setValue(false);
        PaintBrushEmptyOnly = 0;
    }
  }

  // EmptyOnly
  else if (propertyName == m_emptyOnly.getName()) {
      PaintBrushEmptyOnly = (int)(m_emptyOnly.getValue());
      if (m_emptyOnly.getValue() && m_modifierLockAlpha.getValue()) {
          m_modifierLockAlpha.setValue(false);
          PaintBrushModifierLockAlpha = 0;
      }
      if (m_emptyOnly.getValue() && m_selective.getValue()) {
          m_selective.setValue(false);
          PaintBrushSelective = 0;
      }
  }

  // Areas, Lines etc.
  else if (propertyName == m_colorType.getName()) {
    PaintBrushColorType = ::to_string(m_colorType.getValue());
    /*--- ColorModelのCursor更新のためにSIGNALを出す ---*/
    TTool::getApplication()->getCurrentTool()->notifyToolChanged();
  }

  // Lock Alpha
  else if (propertyName == m_modifierLockAlpha.getName()) {
    PaintBrushModifierLockAlpha = (int)(m_modifierLockAlpha.getValue());
    if (m_modifierLockAlpha.getValue() && m_selective.getValue()) {
      m_selective.setValue(false);
      PaintBrushSelective = 0;
    }
    if (m_modifierLockAlpha.getValue() && m_emptyOnly.getValue()) {
        m_emptyOnly.setValue(false);
        PaintBrushEmptyOnly = 0;
    }
  }

  // Pick
  else if (propertyName == m_pick.getName()) {
    PaintBrushPick = (int)m_pick.getValue();
    if (m_pick.getValue() && m_selective.getValue()) {
      m_selective.setValue(false);
      PaintBrushSelective = 0;
    }
  }

  // Filling Mode
  else if (propertyName == m_fillingMode.getName()) {
    PaintBrushFill = (int)(m_fillingMode.getValue());
  }
  return true;
}

//-----------------------------------------------------------------------------

void PaintBrushTool::leftButtonDown(const TPointD &pos, const TMouseEvent &e) {
  fixMousePos(pos);
  m_selecting = true;
  TImageP image(getImage(true));
  if (m_colorType.getValue() == LINES) m_colorTypeBrush = INK;
  if (m_colorType.getValue() == AREAS) m_colorTypeBrush = PAINT;
  if (m_colorType.getValue() == ALL) m_colorTypeBrush = INKNPAINT;

  if (TToonzImageP ti = image) {
    TRasterCM32P ras = ti->getRaster();
    if (ras) {
      int pickedStyle = 0;
      if (m_pick.getValue() || m_selective.getValue()) {
        TApplication *app = getApplication();
        pickedStyle       = pick(ti, pos,
                           app->getCurrentFrame()->isEditingLevel()
                                     ? -1
                                     : app->getCurrentFrame()->getFrame());
        if (m_pick.getValue()) {
          m_orignalStyle = app->getCurrentLevelStyleIndex();
          app->setCurrentLevelStyleIndex(pickedStyle);
        }
      }
      int thickness = m_toolSize.getValue();
      int styleId   = TTool::getApplication()->getCurrentLevelStyleIndex();
      // Modifier selection is captured at stroke start; normal drawing is
      // unchanged, and the selected source style remains selected in the UI.
      if (e.isShiftPressed() != e.isCtrlPressed()) {
        TPalette *palette = ti->getPalette();

        const int previousCount = palette ? palette->getStyleCount() : 0;
        const bool highlight    = e.isShiftPressed();
        styleId = paintBrushCompanionStyle(palette, styleId, highlight);
        if (palette && palette->getStyleCount() != previousCount)
          getApplication()->getCurrentPalette()->notifyPaletteChanged();
      }
      TTileSetCM32 *tileSet = new TTileSetCM32(ras->getSize());
      m_tileSaver           = new TTileSaverCM32(ras, tileSet);
      m_rasterTrack         = new RasterStrokeGenerator(
          ras, PAINTBRUSH, m_colorTypeBrush, styleId,
          TThickPoint(m_mousePos + convert(ras->getCenter()), thickness),
          m_selective.getValue(), pickedStyle, m_modifierLockAlpha.getValue(),
          m_fillingMode.getValue(), m_emptyOnly.getValue());
      /*-- 現在のFidを記憶 --*/
      m_workingFrameId = getFrameId();
      m_tileSaver->save(m_rasterTrack->getLastRect());
      m_rasterTrack->generateLastPieceOfStroke(true);
      invalidate();
    }
  }
}

//-----------------------------------------------------------------------------

void PaintBrushTool::leftButtonDrag(const TPointD &pos, const TMouseEvent &e) {
  if (!m_selecting) return;

  fixMousePos(pos);
  if (TToonzImageP ri = TImageP(getImage(true))) {
    /*--- マウスを動かしながらショートカットでこのツールに切り替わった場合、
            いきなりleftButtonDragから呼ばれることがあり、m_rasterTrackが無い可能性がある
　---*/
    if (m_rasterTrack) {
      int thickness = m_toolSize.getValue();
      m_rasterTrack->add(TThickPoint(
          m_mousePos + convert(ri->getRaster()->getCenter()), thickness));
      m_tileSaver->save(m_rasterTrack->getLastRect());
      TRect modifiedBbox = m_rasterTrack->generateLastPieceOfStroke(true);
      invalidate();
    }
  }
}

//-----------------------------------------------------------------------------

void PaintBrushTool::leftButtonUp(const TPointD &pos, const TMouseEvent &) {
  if (!m_selecting) return;

  fixMousePos(pos);

  finishBrush();

  if (m_pick.getValue()) {
    TApplication *app = getApplication();
    app->setCurrentLevelStyleIndex(m_orignalStyle);
  }
}

//-----------------------------------------------------------------------------

void PaintBrushTool::mouseMove(const TPointD &pos, const TMouseEvent &e) {
  fixMousePos(pos, true);
  invalidate();
}

//-----------------------------------------------------------------------------

void PaintBrushTool::onEnter() {
  if (m_firstTime) {
    m_emptyOnly.setValue(PaintBrushEmptyOnly ? 1 : 0);
    m_selective.setValue(PaintBrushSelective ? 1 : 0);
    m_colorType.setValue(::to_wstring(PaintBrushColorType.getValue()));
    m_toolSize.setValue(PaintBrushSize);
    m_modifierLockAlpha.setValue(PaintBrushModifierLockAlpha ? 1 : 0);
    m_pick.setValue(PaintBrushPick ? 1 : 0);
    m_fillingMode.setValue(PaintBrushFill ? 1 : 0);
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

void PaintBrushTool::onLeave() { m_pointSize = -1; }

//-----------------------------------------------------------------------------

void PaintBrushTool::onActivate() { onEnter(); }

//-----------------------------------------------------------------------------

void PaintBrushTool::onDeactivate() {
  /*--マウスドラッグ中(m_selecting=true)にツールが切り替わったときに描画の終了処理を行う---*/
  if (m_selecting) finishBrush();
}

//-----------------------------------------------------------------------------
/*!
 * 描画中にツールが切り替わった場合に備え、onDeactivateでもMouseReleaseと同じ終了処理を行う
 */
void PaintBrushTool::finishBrush() {
  if (TToonzImageP ti = (TToonzImageP)getImage(true)) {
    if (m_rasterTrack) {
      int thickness = m_toolSize.getValue();
      m_rasterTrack->add(TThickPoint(
          m_mousePos + convert(ti->getRaster()->getCenter()), thickness));
      m_tileSaver->save(m_rasterTrack->getLastRect());
      m_rasterTrack->generateLastPieceOfStroke(true, true);

      TTool::Application *app   = TTool::getApplication();
      TXshLevel *level          = app->getCurrentLevel()->getLevel();
      TXshSimpleLevelP simLevel = level->getSimpleLevel();

      /*-- 描画中にフレームが動いても、描画開始時のFidに対してUndoを記録する
       * --*/
      TFrameId frameId =
          m_workingFrameId.isEmptyFrame() ? getCurrentFid() : m_workingFrameId;

      TUndoManager::manager()->add(new BrushUndo(
          m_tileSaver->getTileSet(), m_rasterTrack->getPointsSequence(),
          m_colorTypeBrush, m_rasterTrack->getStyleId(), m_rasterTrack->getSelectedStyleId(),
          m_rasterTrack->isSelective(), m_rasterTrack->isPaletteOrder(),
          simLevel.getPointer(), frameId,
          m_rasterTrack->isAlphaLocked(),m_rasterTrack->isKeepAntiAlias()));
      ToolUtils::updateSaveBox();

      /*--- FIdを指定して、描画中にフレームが変わっても、
              クリック時のFidのサムネイルが更新されるようにする。---*/
      notifyImageChanged(frameId);

      invalidate();
      delete m_rasterTrack;
      m_rasterTrack = 0;
      delete m_tileSaver;

      /*--- 作業中のフレームをリセット ---*/
      m_workingFrameId = TFrameId();
    }

    ToolUtils::updateSaveBox();
  }

  m_selecting = false;
}

int PaintBrushTool::pick(const TImageP &image, const TPointD &pos,
                         const int frame) {
  TToonzImageP ti = image;
  if (!ti) return 0;

  StylePicker picker(getViewer()->viewerWidget(), image);
  double scale2   = 1.0;
  TPointD pickPos = pos;
  // in case that the column is animated in scene-editing mode
  if (frame > 0) {
    TPointD dpiScale = getViewer()->getDpiScale();
    pickPos.x *= dpiScale.x;
    pickPos.y *= dpiScale.y;
    TPointD worldPos = getCurrentColumnMatrix() * pickPos;
    pickPos          = getCurrentColumnMatrix(frame).inv() * worldPos;
    pickPos.x /= dpiScale.x;
    pickPos.y /= dpiScale.y;
  }
  // thin stroke can be picked with 10 pixel range
  return picker.pickStyleId(pickPos, 10.0, scale2);
}
