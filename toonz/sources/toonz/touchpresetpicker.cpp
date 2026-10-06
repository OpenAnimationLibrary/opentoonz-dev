#include "touchpresetpicker.h"

#include "menubarcommandids.h"
#include "sceneviewer.h"
#include "touchpresetmanager.h"
#include "touchworkspacesettingspopup.h"

#include "tapp.h"
#include "toonzqt/menubarcommand.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QVector>
#include <QtMath>

#include <cmath>

#include <QDateTime>
#include <QApplication>
#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRegularExpression>

namespace {

const int kVisiblePresets = 4;
const int kWheelPageSize  = 6;
const int kLinearSpacing  = 10;
const int kOuterPad       = 4;
const int kGhostItemGap   = 6;
const int kGhostBodyPad   = 6;
const int kWheelPetalGap  = 8;

const int kBaseWheelDiam  = 88;
const int kBaseWheelRing  = 62;
const int kBaseWheelDead  = 24;
const int kBaseStripW     = 132;
const int kBaseStripH     = 34;
const int kBaseEdgeCircle = 26;

int scaled(int base, int sizePercent) {
  return qMax(1, base * sizePercent / 100);
}

int alphaFromOpacity(int baseAlpha, int opacityPercent) {
  return qBound(0, baseAlpha * opacityPercent / 100, 255);
}

QPainterPath roundRectPath(const QRectF &rect, qreal radius) {
  radius = qMin(radius, qMin(rect.width(), rect.height()) * 0.5);
  const qreal d = radius * 2.0;
  QPainterPath path;
  path.moveTo(rect.left() + radius, rect.top());
  path.lineTo(rect.right() - radius, rect.top());
  path.arcTo(rect.right() - d, rect.top(), d, d, 90, -90);
  path.lineTo(rect.right(), rect.bottom() - radius);
  path.arcTo(rect.right() - d, rect.bottom() - d, d, d, 0, -90);
  path.lineTo(rect.left() + radius, rect.bottom());
  path.arcTo(rect.left(), rect.bottom() - d, d, d, 270, -90);
  path.lineTo(rect.left(), rect.top() + radius);
  path.arcTo(rect.left(), rect.top(), d, d, 180, -90);
  path.closeSubpath();
  return path;
}

QPointF offsetRadialPoint(const QPointF &c, qreal radius, qreal angleDeg,
                          qreal offset, int intoClockwise) {
  const qreal th = angleDeg * M_PI / 180.0;
  const qreal ct = std::cos(th);
  const qreal st = std::sin(th);
  const qreal t =
      std::sqrt(qMax(0.0, radius * radius - offset * offset));
  const qreal d = intoClockwise * offset;
  return QPointF(c.x() + t * ct + d * st, c.y() - t * st + d * ct);
}

qreal qtAngleAt(const QPointF &c, const QPointF &p) {
  return std::atan2(c.y() - p.y(), p.x() - c.x()) * 180.0 / M_PI;
}

qreal clockwiseSweep(qreal fromDeg, qreal toDeg) {
  qreal s = toDeg - fromDeg;
  while (s > 0) s -= 360.0;
  while (s <= -360.0) s += 360.0;
  return s;
}

qreal counterClockwiseSweep(qreal fromDeg, qreal toDeg) {
  qreal s = toDeg - fromDeg;
  while (s < 0) s += 360.0;
  while (s >= 360.0) s -= 360.0;
  return s;
}

QPainterPath equalGapSlicePath(const QPointF &c, qreal outerR, qreal innerR,
                               qreal startDeg, qreal spanDeg, qreal gapPx) {
  const qreal d = gapPx * 0.5;
  if (d >= innerR - 1.0 || outerR <= innerR + 1.0) return QPainterPath();
  const qreal endDeg = startDeg - spanDeg;
  const QPointF o0 = offsetRadialPoint(c, outerR, startDeg, d, 1);
  const QPointF o1 = offsetRadialPoint(c, outerR, endDeg, d, -1);
  const QPointF i1 = offsetRadialPoint(c, innerR, endDeg, d, -1);
  const QPointF i0 = offsetRadialPoint(c, innerR, startDeg, d, 1);
  const QRectF outerBox(c.x() - outerR, c.y() - outerR, outerR * 2.0,
                        outerR * 2.0);
  const QRectF innerBox(c.x() - innerR, c.y() - innerR, innerR * 2.0,
                        innerR * 2.0);
  const qreal a0 = qtAngleAt(c, o0);
  const qreal a1 = qtAngleAt(c, o1);
  const qreal b1 = qtAngleAt(c, i1);
  const qreal b0 = qtAngleAt(c, i0);
  QPainterPath path;
  path.moveTo(o0);
  path.arcTo(outerBox, a0, clockwiseSweep(a0, a1));
  path.lineTo(i1);
  path.arcTo(innerBox, b1, counterClockwiseSweep(b1, b0));
  path.closeSubpath();
  return path;
}

struct PickerThemeTokens {
  int g0r, g0g, g0b;
  int g1r, g1g, g1b;
  int g2r, g2g, g2b;
  int h0r, h0g, h0b;
  int sr, sg, sb;
  int br, bg, bb;
  int bhr, bhg, bhb;
  QString text;
  QString textHover;
  QString textChecked;
};

QString colorHex(const QColor &c) { return c.name(QColor::HexRgb); }

QColor parseCssColor(QString value) {
  value = value.trimmed();
  QRegularExpression rgbRe(QStringLiteral(
      R"(rgba?\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+))"));
  const auto rgb = rgbRe.match(value);
  if (rgb.hasMatch())
    return QColor(rgb.captured(1).toInt(), rgb.captured(2).toInt(),
                  rgb.captured(3).toInt());
  const QColor named(value);
  return named.isValid() ? named : QColor();
}

QColor qssRuleColor(const QString &css, const QString &selector,
                    const QString &property, const QColor &fallback) {
  QRegularExpression blockRe(QRegularExpression::escape(selector) +
                             QStringLiteral("\\s*\\{([^}]*)\\}"));
  const auto blockMatch = blockRe.match(css);
  if (!blockMatch.hasMatch()) return fallback;
  QRegularExpression propRe(QStringLiteral("(^|;)\\s*") +
                            QRegularExpression::escape(property) +
                            QStringLiteral("\\s*:\\s*([^;]+)"));
  const auto propMatch = propRe.match(blockMatch.captured(1));
  if (!propMatch.hasMatch()) return fallback;
  const QColor c = parseCssColor(propMatch.captured(2));
  return c.isValid() ? c : fallback;
}

QColor visibleOutline(const QColor &fill) {
  return fill.lightness() >= 140 ? fill.darker(135) : fill.lighter(155);
}

PickerThemeTokens tokensForTheme(TouchPresetPickerTheme theme) {
  if (theme == TouchPresetPickerTheme::OpenToonz) {
    const QString css = qApp ? qApp->styleSheet() : QString();
    const QColor panel =
        qssRuleColor(css, QStringLiteral("QWidget"),
                     QStringLiteral("background-color"), QColor(48, 48, 48));
    const QColor item =
        qssRuleColor(css, QStringLiteral("QPushButton"),
                     QStringLiteral("background-color"), QColor(74, 74, 74));
    const QColor hover = qssRuleColor(
        css, QStringLiteral("QPushButton:hover"),
        QStringLiteral("background-color"), item.lighter(120));
    const QColor pressed = qssRuleColor(
        css, QStringLiteral("QPushButton:pressed"),
        QStringLiteral("background-color"), item.darker(120));
    const QColor sel = qssRuleColor(
        css, QStringLiteral("QTreeView::item:selected"),
        QStringLiteral("background-color"), QColor(69, 106, 169));
    const QColor selHover = qssRuleColor(
        css, QStringLiteral("QTreeView::item:selected:hover"),
        QStringLiteral("background-color"), sel.lighter(115));
    const QColor text =
        qssRuleColor(css, QStringLiteral("QPushButton"),
                     QStringLiteral("color"), QColor(211, 211, 211));
    const QColor textHover =
        qssRuleColor(css, QStringLiteral("QPushButton:hover"),
                     QStringLiteral("color"), text);
    const QColor textSel = qssRuleColor(
        css, QStringLiteral("QTreeView::item:selected"),
        QStringLiteral("color"), QColor(255, 255, 255));
    const QColor outline = visibleOutline(item);
    return {panel.red(),   panel.green(),   panel.blue(),
            item.red(),    item.green(),    item.blue(),
            pressed.red(), pressed.green(), pressed.blue(),
            hover.red(),   hover.green(),   hover.blue(),
            sel.red(),     sel.green(),     sel.blue(),
            outline.red(), outline.green(), outline.blue(),
            selHover.red(), selHover.green(), selHover.blue(),
            colorHex(text), colorHex(textHover), colorHex(textSel)};
  }
  switch (theme) {
  case TouchPresetPickerTheme::WhiteGreen:
    return {232, 236, 240, 248, 249, 251, 236, 238, 242, 242, 244, 246,
            220, 224, 228, 198, 202, 208, 39,  141, 68,  QStringLiteral("#1c1c1c"),
            QStringLiteral("#111111"), QStringLiteral("#111111")};
  case TouchPresetPickerTheme::CreamSage:
    return {248, 246, 236, 236, 240, 224, 218, 224, 206, 246, 252, 238,
            118, 148, 108, 108, 138, 98,  138, 168, 118, QStringLiteral("#353830"),
            QStringLiteral("#2a2e26"), QStringLiteral("#1c2018")};
  default:
    return {16,  36,  56,  10,  24,  42,  6,   14,  28,  22,  48,  72,
            20,  52,  78,  36,  72,  98,  0,   212, 255, QStringLiteral("#c8e8f4"),
            QStringLiteral("#e8f7ff"), QStringLiteral("#ffffff")};
  }
}

QColor hologramLitFill(const PickerThemeTokens &tok) {
  return QColor(qBound(0, (tok.sr * 3 + tok.bhr) / 4, 255),
                qBound(0, (tok.sg * 3 + tok.bhg) / 4, 255),
                qBound(0, (tok.sb * 3 + tok.bhb) / 4, 255));
}

QString rgba(int r, int g, int b, int a) {
  return QStringLiteral("rgba(%1, %2, %3, %4)").arg(r).arg(g).arg(b).arg(a);
}

QString pickerStyleSheet(int wheelDiam, int edgeCircle, int opacityPercent,
                         TouchPresetPickerTheme theme,
                         TouchPresetPickerStyle style, int stripH) {
  const PickerThemeTokens tok = tokensForTheme(theme);
  const int wheelR = wheelDiam / 2;
  const int edgeR  = edgeCircle / 2;
  const int a       = alphaFromOpacity(255, opacityPercent);
  const int aLine   = alphaFromOpacity(110, opacityPercent);
  const int aLineHi = alphaFromOpacity(255, opacityPercent);
  const int aHintLine = alphaFromOpacity(90, opacityPercent);
  const int aGhostLine = alphaFromOpacity(120, opacityPercent);
  const QString c0  = rgba(tok.g0r, tok.g0g, tok.g0b, a);
  const QString c1  = rgba(tok.g1r, tok.g1g, tok.g1b, a);
  const QString c2  = rgba(tok.g2r, tok.g2g, tok.g2b, a);
  const QString ch  = rgba(tok.h0r, tok.h0g, tok.h0b, a);
  const QString cs  = rgba(tok.sr, tok.sg, tok.sb, a);
  const QString cb  = rgba(tok.br, tok.bg, tok.bb, aLine);
  const QString cbh = rgba(tok.bhr, tok.bhg, tok.bhb, aLineHi);
  const QString hintBg = rgba(tok.g1r, tok.g1g, tok.g1b, a);
  const QString hintBd = rgba(tok.br, tok.bg, tok.bb, aHintLine);
  const QColor holoLit = hologramLitFill(tok);
  const QString ghostH = rgba(tok.h0r, tok.h0g, tok.h0b, a);
  const QString ghostS = rgba(tok.sr, tok.sg, tok.sb, a);
  const QString ghostIdle = rgba(tok.g1r, tok.g1g, tok.g1b, a);
  const QString ghostLine = rgba(tok.br, tok.bg, tok.bb, aGhostLine);
  const bool hologram = theme == TouchPresetPickerTheme::HologramCyan;

  if (style == TouchPresetPickerStyle::Ghost) {
    const QString idleFill = hologram ? cs : ghostIdle;
    const QString idleLine =
        hologram ? rgba(tok.bhr, tok.bhg, tok.bhb, alphaFromOpacity(180, opacityPercent))
                 : ghostLine;
    const QString selFill =
        hologram ? rgba(holoLit.red(), holoLit.green(), holoLit.blue(), a)
                 : ghostS;
    QString qss = QStringLiteral(
               "QPushButton#TouchPresetPickerWheel {"
               " background: transparent; border: none; }"
               " QPushButton#TouchPresetPickerStrip {"
               " color: %1;"
               " background-color: %2;"
               " border: __BW__ solid %14;"
               " border-radius: %15px; font-size: 11px; font-weight: 500;"
               " padding: 4px 12px; }"
               " QPushButton#TouchPresetPickerStrip:hover {"
               " color: %3;"
               " background-color: %4;"
               " border-color: %12; }"
               " QPushButton#TouchPresetPickerStrip:checked {"
               " color: %5;"
               " background-color: %6; font-weight: 600;"
               " border: 2px solid %12; }"
               " QPushButton#TouchPresetPickerEdge {"
               " color: %3;"
               " background-color: qradialgradient(cx:0.5, cy:0.5, radius:0.8,"
               " stop:0 %7, stop:1 %8);"
               " border: 1px solid %9;"
               " border-radius: %10px; min-width: %11px; max-width: %11px;"
               " min-height: %11px; max-height: %11px; font-size: 12px;"
               " font-weight: 600; padding: 0; }"
               " QPushButton#TouchPresetPickerEdge:hover {"
               " border-color: %12; }"
               " QPushButton#TouchPresetPickerEdge:disabled {"
               " background: transparent; border-color: %9;"
               " color: %1; }"
               " QLabel#TouchPresetPickerHint {"
               " color: %3;"
               " background-color: %7;"
               " border: 1px solid %13;"
               " border-radius: 14px; padding: 8px 12px; }");
    qss = qss.arg(tok.text)
              .arg(idleFill)
              .arg(tok.textHover)
              .arg(hologram ? selFill : ghostH)
              .arg(tok.textChecked)
              .arg(selFill)
              .arg(c0)
              .arg(c2)
              .arg(cb)
              .arg(edgeR)
              .arg(edgeCircle)
              .arg(cbh)
              .arg(hintBd)
              .arg(idleLine)
              .arg(qMax(1, stripH / 2));
    qss.replace(QStringLiteral("__BW__"),
                hologram ? QStringLiteral("2px") : QStringLiteral("1px"));
    return qss;
  }

  if (theme == TouchPresetPickerTheme::OpenToonz) {
    return QStringLiteral(
               "QPushButton#TouchPresetPickerWheel,"
               " QPushButton#TouchPresetPickerStrip {"
               " color: %1; background: transparent; border: none;"
               " font-size: 11px; font-weight: 500; padding: 0; }"
               " QPushButton#TouchPresetPickerWheel:hover,"
               " QPushButton#TouchPresetPickerStrip:hover {"
               " color: %2; }"
               " QPushButton#TouchPresetPickerWheel:checked,"
               " QPushButton#TouchPresetPickerStrip:checked {"
               " color: %3; font-weight: 600; }"
               " QPushButton#TouchPresetPickerEdge {"
               " color: %2; background: transparent; border: none;"
               " border-radius: %4px; min-width: %5px; max-width: %5px;"
               " min-height: %5px; max-height: %5px; font-size: 12px;"
               " font-weight: 600; padding: 0; }"
               " QPushButton#TouchPresetPickerEdge:disabled { color: %1; }"
               " QLabel#TouchPresetPickerHint {"
               " color: %2; background-color: %6;"
               " border: 1px solid %7; border-radius: 6px; padding: 6px 10px; }")
        .arg(tok.text)
        .arg(tok.textHover)
        .arg(tok.textChecked)
        .arg(edgeR)
        .arg(edgeCircle)
        .arg(hintBg)
        .arg(hintBd);
  }

  return QStringLiteral(
             "QPushButton#TouchPresetPickerWheel {"
             " color: %13;"
             " background-color: qradialgradient(cx:0.5, cy:0.4, radius:0.85,"
             " stop:0 %1, stop:0.45 %2, stop:1 %3);"
             " border: 2px solid %9;"
             " border-radius: %4px; font-size: 11px; font-weight: 600;"
             " padding: 4px; }"
             " QPushButton#TouchPresetPickerWheel:hover {"
             " color: %14;"
             " background-color: qradialgradient(cx:0.5, cy:0.35, radius:0.9,"
             " stop:0 %5, stop:0.5 %2, stop:1 %3);"
             " border-color: %10; }"
             " QPushButton#TouchPresetPickerWheel:checked {"
             " color: %15;"
             " background-color: qradialgradient(cx:0.5, cy:0.35, radius:0.95,"
             " stop:0 %6, stop:0.55 %2, stop:1 %3);"
             " border: 3px solid %10; }"
             " QPushButton#TouchPresetPickerStrip {"
             " color: %13;"
             " background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
             " stop:0 %1, stop:1 %3);"
             " border: 1px solid %9;"
             " border-radius: 10px; font-size: 11px; font-weight: 500;"
             " padding: 4px 12px; }"
             " QPushButton#TouchPresetPickerStrip:hover {"
             " color: %14;"
             " background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,"
             " stop:0 %5, stop:1 %3);"
             " border-color: %10; }"
             " QPushButton#TouchPresetPickerStrip:checked {"
             " color: %15;"
             " background-color: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
             " stop:0 %6, stop:0.45 %2, stop:1 %3);"
             " border: 2px solid %10; font-weight: 600; }"
             " QPushButton#TouchPresetPickerEdge {"
             " color: %14;"
             " background-color: qradialgradient(cx:0.5, cy:0.5, radius:0.8,"
             " stop:0 %1, stop:1 %3);"
             " border: 1px solid %9;"
             " border-radius: %7px; min-width: %8px; max-width: %8px;"
             " min-height: %8px; max-height: %8px; font-size: 12px;"
             " font-weight: 600; padding: 0; }"
             " QPushButton#TouchPresetPickerEdge:hover {"
             " border-color: %10; }"
             " QPushButton#TouchPresetPickerEdge:disabled {"
             " background: transparent; border-color: %9;"
             " color: %13; }"
             " QLabel#TouchPresetPickerHint {"
             " color: %14;"
             " background-color: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
             " stop:0 %11, stop:1 %3);"
             " border: 1px solid %12;"
             " border-radius: 6px; padding: 6px 10px; }")
      .arg(c0)
      .arg(c1)
      .arg(c2)
      .arg(wheelR)
      .arg(ch)
      .arg(cs)
      .arg(edgeR)
      .arg(edgeCircle)
      .arg(cb)
      .arg(cbh)
      .arg(hintBg)
      .arg(hintBd)
      .arg(tok.text)
      .arg(tok.textHover)
      .arg(tok.textChecked);
}

QString elidedName(const QFont &font, const QString &name, int maxWidth) {
  return QFontMetrics(font).elidedText(name, Qt::ElideRight, maxWidth);
}

QPushButton *makeEdgeButton(QWidget *parent, const QString &glyph, int edgeCircle) {
  auto *btn = new QPushButton(glyph, parent);
  btn->setObjectName(QStringLiteral("TouchPresetPickerEdge"));
  btn->setFlat(true);
  btn->setFocusPolicy(Qt::NoFocus);
  btn->setCursor(Qt::PointingHandCursor);
  btn->setFixedSize(edgeCircle, edgeCircle);
  btn->setAttribute(Qt::WA_TranslucentBackground, false);
  return btn;
}

QPushButton *makeWheelButton(QWidget *parent, const QString &name,
                             const QString &active, int wheelDiam) {
  const QString label = elidedName(parent->font(), name, wheelDiam - 16);
  auto *btn           = new QPushButton(label, parent);
  btn->setObjectName(QStringLiteral("TouchPresetPickerWheel"));
  btn->setCheckable(true);
  btn->setChecked(name == active);
  btn->setProperty("presetName", name);
  btn->setToolTip(name);
  btn->setFixedSize(wheelDiam, wheelDiam);
  btn->setAttribute(Qt::WA_TranslucentBackground, false);
  return btn;
}

QPushButton *makeStripButton(QWidget *parent, const QString &name,
                             const QString &active, int stripW, int stripH) {
  const QString label = elidedName(parent->font(), name, stripW - 16);
  auto *btn           = new QPushButton(label, parent);
  btn->setObjectName(QStringLiteral("TouchPresetPickerStrip"));
  btn->setCheckable(true);
  btn->setChecked(name == active);
  btn->setProperty("presetName", name);
  btn->setToolTip(name);
  btn->setFixedSize(stripW, stripH);
  btn->setAttribute(Qt::WA_TranslucentBackground, false);
  return btn;
}

QStringList pageSlice(const QStringList &names, int start, int pageSize) {
  QStringList out;
  const int end = qMin(start + pageSize, names.size());
  for (int i = start; i < end; ++i) out.append(names[i]);
  return out;
}

}  // namespace

//-----------------------------------------------------------------------------

TouchPresetPickerPanel::TouchPresetPickerPanel(SceneViewer *viewer)
    : QWidget(viewer), m_viewer(viewer) {
  setObjectName(QStringLiteral("TouchPresetPickerPanel"));
  setAttribute(Qt::WA_TranslucentBackground);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::StrongFocus);
  applyPickerMetricsFromSettings();
  hide();
}

void TouchPresetPickerPanel::applyPickerMetricsFromSettings() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const int sizePct       = mgr->pickerSizePercent();
  const int opacityPct    = mgr->pickerOpacityPercent();
  m_wheelDiam             = scaled(kBaseWheelDiam, sizePct);
  m_wheelRing             = scaled(kBaseWheelRing, sizePct);
  m_wheelDead             = scaled(kBaseWheelDead, sizePct);
  m_stripW                = scaled(kBaseStripW, sizePct);
  m_stripH                = scaled(kBaseStripH, sizePct);
  m_edgeCircle            = scaled(kBaseEdgeCircle, sizePct);
  const QString css = pickerStyleSheet(m_wheelDiam, m_edgeCircle, opacityPct,
                                       mgr->pickerTheme(), mgr->pickerStyle(),
                                       m_stripH);
  if (styleSheet() != css) {
    setStyleSheet(css);
    for (QPushButton *btn : findChildren<QPushButton *>()) {
      btn->style()->unpolish(btn);
      btn->style()->polish(btn);
      btn->update();
    }
  }
}

int TouchPresetPickerPanel::visibleSlotCount() const { return kVisiblePresets; }

int TouchPresetPickerPanel::wheelPageSize() const { return kWheelPageSize; }

int TouchPresetPickerPanel::pageSizeForCurrentLayout() const {
  return TouchPresetManager::instance()->pickerLayout() ==
                 TouchPresetPickerLayout::Wheel
             ? wheelPageSize()
             : visibleSlotCount();
}

QPushButton *TouchPresetPickerPanel::presetButton(
    const QString &objectName, const QString &presetName) const {
  for (QPushButton *btn : findChildren<QPushButton *>()) {
    if (btn->objectName() != objectName) continue;
    if (btn->property("presetName").toString() == presetName) return btn;
  }
  return nullptr;
}

QPushButton *TouchPresetPickerPanel::slotAt(int index) const {
  for (QPushButton *btn : findChildren<QPushButton *>()) {
    const QString role = btn->objectName();
    if (role != QLatin1String("TouchPresetPickerWheel") &&
        role != QLatin1String("TouchPresetPickerStrip"))
      continue;
    if (btn->property("slotIndex").toInt() == index) return btn;
  }
  return nullptr;
}

bool TouchPresetPickerPanel::relabelSlots(const QStringList &pageNames) {
  const QString active = TouchPresetManager::instance()->activePreset();
  for (int i = 0; i < pageNames.size(); ++i) {
    QPushButton *btn = slotAt(i);
    if (!btn) return false;
    btn->setProperty("presetName", pageNames[i]);
    btn->setToolTip(pageNames[i]);
    btn->setChecked(pageNames[i] == active);
    const bool pieWheel =
        TouchPresetManager::instance()->pickerLayout() ==
        TouchPresetPickerLayout::Wheel;
    btn->setVisible(!pieWheel);
  }
  for (int i = pageNames.size();; ++i) {
    QPushButton *btn = slotAt(i);
    if (!btn) break;
    btn->hide();
  }
  return true;
}

bool TouchPresetPickerPanel::showCurrentPage() {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QStringList names = mgr->pinnedPresetsForPicker();
  if (names.isEmpty()) return false;

  const int pageSize = pageSizeForCurrentLayout();
  const int maxStart = qMax(0, names.size() - pageSize);
  m_pageStart        = qBound(0, m_pageStart, maxStart);
  const QStringList pageNames = pageSlice(names, m_pageStart, pageSize);
  if (pageNames.isEmpty() || !relabelSlots(pageNames)) return false;

  const bool paging  = names.size() > pageSize;
  const bool canPrev = paging && m_pageStart > 0;
  const bool canNext = paging && m_pageStart < maxStart;
  const TouchPresetPickerLayout mode = mgr->pickerLayout();

  if (mode == TouchPresetPickerLayout::Wheel) {
    if (paging && (!edgeButton(true) || !edgeButton(false))) return false;
    applyWheelGeometry(pageNames, paging, canPrev, canNext);
    return true;
  }
  if (!edgeButton(true) || !edgeButton(false)) return false;
  applyLinearGeometry(mode == TouchPresetPickerLayout::Vertical, pageNames,
                      paging, canPrev, canNext);
  return true;
}

QPushButton *TouchPresetPickerPanel::edgeButton(bool prev) const {
  for (QPushButton *btn : findChildren<QPushButton *>()) {
    if (btn->objectName() != QLatin1String("TouchPresetPickerEdge")) continue;
    const int delta = btn->property("pageDelta").toInt();
    if (prev ? delta < 0 : delta > 0) return btn;
  }
  return nullptr;
}

QPushButton *TouchPresetPickerPanel::buttonAt(const QPoint &local) const {
  QPushButton *edge  = nullptr;
  QPushButton *other = nullptr;
  for (QPushButton *btn : findChildren<QPushButton *>()) {
    if (!btn->isVisible() || !btn->isEnabled()) continue;
    const QPoint topLeft = btn->mapTo(this, QPoint(0, 0));
    if (!QRect(topLeft, btn->size()).contains(local)) continue;
    if (btn->objectName() == QLatin1String("TouchPresetPickerEdge"))
      edge = btn;
    else
      other = btn;
  }
  return edge ? edge : other;
}

void TouchPresetPickerPanel::alignPageToActive(const QStringList &names,
                                               const QString &active) {
  m_pageStart   = 0;
  const int idx = names.indexOf(active);
  if (idx < 0) return;
  const int pageSize = pageSizeForCurrentLayout();
  m_pageStart        = (idx / pageSize) * pageSize;
  const int maxStart = qMax(0, names.size() - pageSize);
  if (m_pageStart > maxStart) m_pageStart = maxStart;
}

void TouchPresetPickerPanel::clearPickerContent() {
  if (QLayout *old = layout()) {
    QLayoutItem *item = nullptr;
    while ((item = old->takeAt(0)) != nullptr) {
      if (QWidget *w = item->widget()) {
        w->setParent(nullptr);
        delete w;
      }
      delete item;
    }
    delete old;
  }
  const QList<QWidget *> kids =
      findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
  for (QWidget *w : kids) delete w;
}

void TouchPresetPickerPanel::applyWheelGeometry(const QStringList &pageNames,
                                                bool paging, bool canPrev,
                                                bool canNext) {
  const int count = pageNames.size();
  const int pad =
      paging ? m_edgeCircle + kLinearSpacing + kOuterPad : kOuterPad;

  const int pie = qMax(160, m_wheelDiam * 2 + m_wheelRing);
  setFixedSize(pie + pad * 2, pie + kOuterPad * 2);
  m_bodyRect  = QRect(pad, (height() - pie) / 2, pie, pie);
  m_wheelDead = qMax(22, pie * 16 / 100);
  for (int i = 0; i < count; ++i) {
    QPushButton *btn = slotAt(i);
    if (!btn) continue;
    btn->setProperty("presetName", pageNames[i]);
    btn->hide();
  }

  QPushButton *prev = edgeButton(true);
  QPushButton *next = edgeButton(false);
  if (!paging || !prev || !next) {
    update();
    return;
  }
  const int cy = height() / 2 - m_edgeCircle / 2;
  prev->setFixedSize(m_edgeCircle, m_edgeCircle);
  next->setFixedSize(m_edgeCircle, m_edgeCircle);
  prev->setText(QStringLiteral("<"));
  next->setText(QStringLiteral(">"));
  prev->setEnabled(canPrev);
  next->setEnabled(canNext);
  prev->move(kOuterPad, cy);
  next->move(width() - m_edgeCircle - kOuterPad, cy);
  prev->show();
  next->show();
  prev->raise();
  next->raise();
  update();
}

void TouchPresetPickerPanel::applyLinearGeometry(bool vertical,
                                                 const QStringList &pageNames,
                                                 bool paging, bool canPrev,
                                                 bool canNext) {
  const int n = pageNames.size();
  const bool classic = !isGhostLook();
  const bool flatOt =
      classic && TouchPresetManager::instance()->pickerTheme() ==
                     TouchPresetPickerTheme::OpenToonz;
  const int itemGap  = classic ? kLinearSpacing : kGhostItemGap;
  const int arrowGap = classic ? kLinearSpacing : 16;
  int w = 0;
  int h = 0;
  if (vertical) {
    w = qMax(m_stripW, paging ? m_edgeCircle : 0) + kOuterPad * 2;
    h = n * m_stripH + qMax(0, n - 1) * itemGap + kOuterPad * 2;
    if (paging) h += 2 * (m_edgeCircle + arrowGap);
  } else {
    h = qMax(m_stripH, paging ? m_edgeCircle : 0) + kOuterPad * 2;
    w = n * m_stripW + qMax(0, n - 1) * itemGap + kOuterPad * 2;
    if (paging) w += 2 * (m_edgeCircle + arrowGap);
  }
  setFixedSize(qMax(1, w), qMax(1, h));

  QPushButton *prev = edgeButton(true);
  QPushButton *next = edgeButton(false);
  if (prev) {
    prev->setFixedSize(m_edgeCircle, m_edgeCircle);
    prev->setEnabled(paging && canPrev);
    prev->setVisible(paging);
    prev->setFlat(flatOt);
    prev->setAttribute(Qt::WA_TranslucentBackground, flatOt);
  }
  if (next) {
    next->setFixedSize(m_edgeCircle, m_edgeCircle);
    next->setEnabled(paging && canNext);
    next->setVisible(paging);
    next->setFlat(flatOt);
    next->setAttribute(Qt::WA_TranslucentBackground, flatOt);
  }

  int x = kOuterPad;
  int y = kOuterPad;
  if (vertical) {
    if (paging && prev) {
      prev->move((width() - m_edgeCircle) / 2, kOuterPad);
      y = kOuterPad + m_edgeCircle + arrowGap;
    }
    for (const QString &name : pageNames) {
      QPushButton *btn =
          presetButton(QStringLiteral("TouchPresetPickerStrip"), name);
      if (!btn) continue;
      btn->setFixedSize(m_stripW, m_stripH);
      btn->setText(elidedName(font(), name, m_stripW - 16));
      btn->setFlat(flatOt);
      btn->setAttribute(Qt::WA_TranslucentBackground, flatOt);
      btn->move((width() - m_stripW) / 2, y);
      y += m_stripH + itemGap;
    }
    if (paging && next)
      next->move((width() - m_edgeCircle) / 2,
                 height() - m_edgeCircle - kOuterPad);
  } else {
    if (paging && prev) {
      prev->move(kOuterPad, (height() - m_edgeCircle) / 2);
      x = kOuterPad + m_edgeCircle + arrowGap;
    }
    for (const QString &name : pageNames) {
      QPushButton *btn =
          presetButton(QStringLiteral("TouchPresetPickerStrip"), name);
      if (!btn) continue;
      btn->setFixedSize(m_stripW, m_stripH);
      btn->setText(elidedName(font(), name, m_stripW - 16));
      btn->setFlat(flatOt);
      btn->setAttribute(Qt::WA_TranslucentBackground, flatOt);
      btn->move(x, (height() - m_stripH) / 2);
      x += m_stripW + itemGap;
    }
    if (paging && next)
      next->move(width() - m_edgeCircle - kOuterPad,
                 (height() - m_edgeCircle) / 2);
  }
  if (prev) prev->raise();
  if (next) next->raise();

  QRect body;
  for (const QString &name : pageNames) {
    QPushButton *btn =
        presetButton(QStringLiteral("TouchPresetPickerStrip"), name);
    if (btn) body = body.isNull() ? btn->geometry() : body.united(btn->geometry());
  }
  if (!body.isNull())
    body.adjust(-kGhostBodyPad, -kGhostBodyPad, kGhostBodyPad, kGhostBodyPad);
  m_bodyRect = isGhostLook() ? body : QRect();
  update();
}

bool TouchPresetPickerPanel::isGhostLook() const {
  return TouchPresetManager::instance()->pickerStyle() ==
         TouchPresetPickerStyle::Ghost;
}

int TouchPresetPickerPanel::ghostPieSlotIndex(const QPoint &local) const {
  if (m_bodyRect.isEmpty()) return -2;
  const QPointF c(m_bodyRect.center());
  const double dist  = QLineF(c, local).length();
  const double outer = m_bodyRect.width() / 2.0;
  if (dist > outer + 1.0) return -2;
  if (dist < m_wheelDead) return -1;

  TouchPresetManager *mgr = TouchPresetManager::instance();
  const int n =
      pageSlice(mgr->pinnedPresetsForPicker(), m_pageStart,
                pageSizeForCurrentLayout())
          .size();
  if (n <= 0) return -2;
  double a = std::atan2(local.y() - c.y(), local.x() - c.x()) + M_PI / 2.0;
  if (a < 0) a += 2.0 * M_PI;
  const int idx = qBound(0, static_cast<int>(a / (2.0 * M_PI) * n), n - 1);
  if (!isGhostLook()) {
    const double gap = kWheelPetalGap;
    if (dist < m_wheelDead + gap) return dist < m_wheelDead ? -1 : -2;
    const double span       = 360.0 / n;
    const double halfGapDeg = (gap * 0.5 / dist) * 180.0 / M_PI;
    double inSlice          = a * 180.0 / M_PI - idx * span;
    while (inSlice < 0) inSlice += 360.0;
    inSlice = std::fmod(inSlice, span);
    if (inSlice < halfGapDeg || inSlice > span - halfGapDeg) return -2;
  }
  return idx;
}

void TouchPresetPickerPanel::paintGhostLook(QPainter &p) {
  TouchPresetManager *mgr = TouchPresetManager::instance();
  const PickerThemeTokens tok = tokensForTheme(mgr->pickerTheme());
  const int op = mgr->pickerOpacityPercent();
  p.setRenderHint(QPainter::Antialiasing, true);

  if (mgr->pickerLayout() != TouchPresetPickerLayout::Wheel) {
    if (isGhostLook()) {
      if (m_bodyRect.isEmpty()) return;
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(tok.g0r, tok.g0g, tok.g0b, alphaFromOpacity(255, op)));
      const qreal rad = m_stripH * 0.5 + kGhostBodyPad;
      p.drawPath(roundRectPath(QRectF(m_bodyRect), rad));
      return;
    }
    if (mgr->pickerTheme() != TouchPresetPickerTheme::OpenToonz) return;
    const qreal rad = 10.0;
    for (QPushButton *btn : findChildren<QPushButton *>()) {
      if (!btn->isVisible()) continue;
      const QString role = btn->objectName();
      const bool edge =
          role == QLatin1String("TouchPresetPickerEdge");
      const bool strip =
          role == QLatin1String("TouchPresetPickerStrip");
      if (!edge && !strip) continue;
      const bool sel = btn->isChecked();
      const bool en  = btn->isEnabled();
      QColor fill =
          sel ? QColor(tok.sr, tok.sg, tok.sb, alphaFromOpacity(255, op))
              : QColor(tok.g1r, tok.g1g, tok.g1b, alphaFromOpacity(255, op));
      if (!en) fill.setAlpha(alphaFromOpacity(90, op));
      const QColor line =
          (sel || (edge && en))
              ? QColor(tok.bhr, tok.bhg, tok.bhb, alphaFromOpacity(220, op))
              : QColor(tok.br, tok.bg, tok.bb, alphaFromOpacity(110, op));
      p.setPen(QPen(line, sel && strip ? 2 : 1));
      p.setBrush(fill);
      if (edge)
        p.drawEllipse(QRectF(btn->geometry()));
      else
        p.drawPath(roundRectPath(QRectF(btn->geometry()), rad));
    }
    return;
  }

  if (m_bodyRect.isEmpty()) return;

  const bool gapped = !isGhostLook();
  const bool hologram =
      mgr->pickerTheme() == TouchPresetPickerTheme::HologramCyan;
  const QRectF disk = m_bodyRect;
  const QPointF c   = disk.center();
  const int n =
      pageSlice(mgr->pinnedPresetsForPicker(), m_pageStart,
                pageSizeForCurrentLayout())
          .size();
  if (n <= 0) return;

  const QString active = mgr->activePreset();
  const double spanDeg = 360.0 / n;
  const qreal gapPx    = gapped ? kWheelPetalGap : 0.0;
  const int inner      = m_wheelDead * 2;
  QRect hole(0, 0, inner, inner);
  hole.moveCenter(m_bodyRect.center());
  const qreal outerR = disk.width() * 0.5;
  const qreal holeR  = inner * 0.5;
  const qreal innerR = holeR + gapPx;
  const QRectF innerDisk = gapped ? QRectF()
                                  : QRectF(hole).adjusted(1.0, 1.0, -1.0, -1.0);
  int selIndex = -1;
  QVector<QPainterPath> slices;
  slices.reserve(n);
  for (int i = 0; i < n; ++i) {
    QPushButton *btn = slotAt(i);
    const QString name =
        btn ? btn->property("presetName").toString() : QString();
    const bool sel = (name == active);
    if (sel) selIndex = i;
    const double startDeg = 90.0 - i * spanDeg;
    QPainterPath slice;
    if (gapped) {
      slice = equalGapSlicePath(c, outerR, innerR, startDeg, spanDeg, gapPx);
    } else {
      slice.arcMoveTo(disk, startDeg);
      slice.arcTo(disk, startDeg, -spanDeg);
      slice.arcTo(innerDisk, startDeg - spanDeg, spanDeg);
      slice.closeSubpath();
    }
    slices.append(slice);
    if (slice.isEmpty()) continue;
    QColor fill;
    if (hologram) {
      const QColor lit = hologramLitFill(tok);
      fill = sel ? QColor(lit.red(), lit.green(), lit.blue(),
                          alphaFromOpacity(255, op))
                 : QColor(tok.sr, tok.sg, tok.sb, alphaFromOpacity(255, op));
    } else {
      fill = sel ? QColor(tok.sr, tok.sg, tok.sb, alphaFromOpacity(255, op))
                 : QColor(tok.g1r, tok.g1g, tok.g1b, alphaFromOpacity(255, op));
    }
    p.fillPath(slice, fill);
    const QColor edge =
        hologram ? QColor(tok.bhr, tok.bhg, tok.bhb,
                          alphaFromOpacity(sel ? 90 : 70, op))
                 : QColor(tok.br, tok.bg, tok.bb, alphaFromOpacity(80, op));
    p.strokePath(slice, QPen(edge, 1));

    const double mid = (90.0 - (i + 0.5) * spanDeg) * M_PI / 180.0;
    const double lr  = disk.width() * 0.34;
    const QPointF lp(c.x() + std::cos(mid) * lr, c.y() - std::sin(mid) * lr);
    p.setPen(QColor(sel ? tok.textChecked : tok.text));
    QFont f = font();
    f.setPixelSize(qMax(9, m_bodyRect.width() / 18));
    f.setBold(sel);
    p.setFont(f);
    const QString label = elidedName(f, name, int(disk.width() * 0.28));
    const QRect tr(lp.toPoint() - QPoint(60, 12), QSize(120, 24));
    p.drawText(tr, Qt::AlignCenter, label);
  }

  p.setPen(QPen(QColor(tok.br, tok.bg, tok.bb, alphaFromOpacity(90, op)), 1));
  p.setBrush(QColor(tok.g0r, tok.g0g, tok.g0b, alphaFromOpacity(255, op)));
  p.drawEllipse(hole);
  if (hologram) {
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(tok.bhr, tok.bhg, tok.bhb, alphaFromOpacity(180, op)),
                  1.5));
    p.drawEllipse(QRectF(hole).adjusted(-1.0, -1.0, 1.0, 1.0));
  }

  auto paintSliceRings = [&](int i, bool bright) {
    if (i < 0 || i >= n || slices[i].isEmpty()) return;
    const double startDeg = 90.0 - i * spanDeg;
    const QColor accent(tok.bhr, tok.bhg, tok.bhb,
                        alphaFromOpacity(bright ? 255 : (hologram ? 200 : 255), op));
    const QColor glow(tok.bhr, tok.bhg, tok.bhb,
                      alphaFromOpacity(bright ? (hologram ? 120 : 80)
                                              : (hologram ? 90 : 0),
                                       op));
    const qreal glowW = bright ? (hologram ? 7.0 : 5.0) : 6.0;
    const qreal accW  = bright ? 2.0 : 2.0;
    p.save();
    if (gapped) p.setClipPath(slices[i]);
    p.setBrush(Qt::NoBrush);
    if (gapped) {
      const qreal d         = gapPx * 0.5;
      const qreal endDeg    = startDeg - spanDeg;
      const qreal outerArcR = qMax(innerR + 4.0, outerR - 2.5);
      const qreal innerArcR = innerR + 2.5;
      const QPointF o0 = offsetRadialPoint(c, outerArcR, startDeg, d, 1);
      const QPointF o1 = offsetRadialPoint(c, outerArcR, endDeg, d, -1);
      const QPointF i0 = offsetRadialPoint(c, innerArcR, startDeg, d, 1);
      const QPointF i1 = offsetRadialPoint(c, innerArcR, endDeg, d, -1);
      const QRectF outerArc(c.x() - outerArcR, c.y() - outerArcR,
                            outerArcR * 2.0, outerArcR * 2.0);
      const QRectF innerArc(c.x() - innerArcR, c.y() - innerArcR,
                            innerArcR * 2.0, innerArcR * 2.0);
      const qreal a0  = qtAngleAt(c, o0);
      const qreal a1  = qtAngleAt(c, o1);
      const qreal b0  = qtAngleAt(c, i0);
      const qreal b1  = qtAngleAt(c, i1);
      const qreal swO = clockwiseSweep(a0, a1);
      const qreal swI = clockwiseSweep(b0, b1);
      p.setPen(QPen(glow, glowW, Qt::SolidLine, Qt::FlatCap));
      p.drawArc(outerArc, qRound(a0 * 16), qRound(swO * 16));
      p.drawArc(innerArc, qRound(b0 * 16), qRound(swI * 16));
      p.setPen(QPen(accent, accW, Qt::SolidLine, Qt::FlatCap));
      p.drawArc(outerArc, qRound(a0 * 16), qRound(swO * 16));
      p.drawArc(innerArc, qRound(b0 * 16), qRound(swI * 16));
    } else {
      const double span     = -spanDeg;
      const QRectF outerArc = disk.adjusted(2.5, 2.5, -2.5, -2.5);
      const QRectF innerArc = QRectF(hole).adjusted(-1.5, -1.5, 1.5, 1.5);
      p.setPen(QPen(glow, glowW, Qt::SolidLine, Qt::FlatCap));
      p.drawArc(outerArc, qRound(startDeg * 16), qRound(span * 16));
      p.drawArc(innerArc, qRound(startDeg * 16), qRound(span * 16));
      p.setPen(QPen(accent, accW, Qt::SolidLine, Qt::FlatCap));
      p.drawArc(outerArc, qRound(startDeg * 16), qRound(span * 16));
      p.drawArc(innerArc, qRound(startDeg * 16), qRound(span * 16));
    }
    p.restore();
  };

  if (hologram) {
    for (int i = 0; i < n; ++i) paintSliceRings(i, false);
    if (selIndex >= 0) paintSliceRings(selIndex, true);
  } else if (selIndex >= 0) {
    paintSliceRings(selIndex, true);
  }

  // Close control for now; the center may later host a logo or command.
  const int arm    = qMax(4, inner / 16);
  const QPoint mid = hole.center();
  p.setPen(QPen(QColor(tok.textHover), 1.5, Qt::SolidLine, Qt::RoundCap));
  p.drawLine(mid + QPoint(-arm, -arm), mid + QPoint(arm, arm));
  p.drawLine(mid + QPoint(arm, -arm), mid + QPoint(-arm, arm));
}

void TouchPresetPickerPanel::paintEvent(QPaintEvent *event) {
  QPainter p(this);
  paintGhostLook(p);
  QWidget::paintEvent(event);
}

void TouchPresetPickerPanel::finishPickerLayout() {
  positionPanel();
  show();
  raise();
  update();
}

void TouchPresetPickerPanel::applyPageDelta(int delta) {
  if (delta == 0) return;
  const QStringList names =
      TouchPresetManager::instance()->pinnedPresetsForPicker();
  const int pageSize = pageSizeForCurrentLayout();
  const int maxStart = qMax(0, names.size() - pageSize);
  const int next     = qBound(0, m_pageStart + delta, maxStart);
  if (next == m_pageStart) return;
  m_pageStart = next;
  if (!showCurrentPage()) return;
  finishPickerLayout();
}

bool TouchPresetPickerPanel::tryPageBySwipe(const QPoint &local) {
  if (!m_touchPagingCandidate) return false;
  const TouchPresetPickerLayout mode =
      TouchPresetManager::instance()->pickerLayout();
  const QPoint delta  = local - m_touchStartLocal;
  const int threshold = qMax(28, QApplication::startDragDistance());
  const bool verticalSwipe = mode == TouchPresetPickerLayout::Vertical;
  int stepPx               = m_stripW + kLinearSpacing;
  if (mode == TouchPresetPickerLayout::Wheel)
    stepPx = m_wheelDiam / 2;
  else if (verticalSwipe)
    stepPx = m_stripH + kLinearSpacing;

  int steps = 1;
  if (verticalSwipe) {
    if (qAbs(delta.y()) < threshold || qAbs(delta.y()) < qAbs(delta.x()))
      return false;
    steps = qMax(1, qAbs(delta.y()) / qMax(1, stepPx));
    applyPageDelta(delta.y() < 0 ? steps : -steps);
    return true;
  }
  if (qAbs(delta.x()) < threshold || qAbs(delta.x()) < qAbs(delta.y()))
    return false;
  steps = qMax(1, qAbs(delta.x()) / qMax(1, stepPx));
  applyPageDelta(delta.x() < 0 ? steps : -steps);
  return true;
}

void TouchPresetPickerPanel::rebuild() {
  // Keep each layout mode at its own computed size.
  const bool shown = isVisible();
  if (shown) hide();
  clearPickerContent();
  applyPickerMetricsFromSettings();
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

  TouchPresetManager *mgr = TouchPresetManager::instance();
  const QStringList names = mgr->pinnedPresetsForPicker();
  const QString active    = mgr->activePreset();
  const TouchPresetPickerLayout layoutMode = mgr->pickerLayout();

  if (names.isEmpty()) {
    auto *label = new QLabel(tr("No pinned presets"), this);
    label->setObjectName(QStringLiteral("TouchPresetPickerHint"));
    label->setAlignment(Qt::AlignCenter);
    label->adjustSize();
    const QSize hint = label->size() + QSize(8, 8);
    label->setFixedSize(hint);
    label->move(0, 0);
    setFixedSize(hint);
    m_bodyRect = QRect();
    return;
  }

  const int pageSize = pageSizeForCurrentLayout();
  const int maxStart = qMax(0, names.size() - pageSize);
  m_pageStart        = qBound(0, m_pageStart, maxStart);
  const QStringList pageNames = pageSlice(names, m_pageStart, pageSize);
  const bool paging           = names.size() > pageSize;
  const bool canPrev          = paging && m_pageStart > 0;
  const bool canNext          = paging && m_pageStart < maxStart;

  if (layoutMode == TouchPresetPickerLayout::Wheel) {
    for (int i = 0; i < pageNames.size(); ++i) {
      auto *btn = makeWheelButton(this, pageNames[i], active, m_wheelDiam);
      btn->setProperty("slotIndex", i);
      connect(btn, &QPushButton::clicked, this,
              &TouchPresetPickerPanel::onPresetClicked);
    }
    if (paging) {
      auto *prev = makeEdgeButton(this, QStringLiteral("<"), m_edgeCircle);
      prev->setProperty("pageDelta", -1);
      connect(prev, &QPushButton::clicked, this,
              &TouchPresetPickerPanel::onPageEdgeClicked);
      auto *next = makeEdgeButton(this, QStringLiteral(">"), m_edgeCircle);
      next->setProperty("pageDelta", 1);
      connect(next, &QPushButton::clicked, this,
              &TouchPresetPickerPanel::onPageEdgeClicked);
    }
    applyWheelGeometry(pageNames, paging, canPrev, canNext);
    return;
  }

  const bool vertical = layoutMode == TouchPresetPickerLayout::Vertical;
  auto *edgePrev =
      makeEdgeButton(this, vertical ? QStringLiteral("^") : QStringLiteral("<"),
                     m_edgeCircle);
  edgePrev->setProperty("pageDelta", -1);
  connect(edgePrev, &QPushButton::clicked, this,
          &TouchPresetPickerPanel::onPageEdgeClicked);

  for (int i = 0; i < pageNames.size(); ++i) {
    auto *btn =
        makeStripButton(this, pageNames[i], active, m_stripW, m_stripH);
    btn->setProperty("slotIndex", i);
    connect(btn, &QPushButton::clicked, this,
            &TouchPresetPickerPanel::onPresetClicked);
  }

  auto *edgeNext =
      makeEdgeButton(this, vertical ? QStringLiteral("v") : QStringLiteral(">"),
                     m_edgeCircle);
  edgeNext->setProperty("pageDelta", 1);
  connect(edgeNext, &QPushButton::clicked, this,
          &TouchPresetPickerPanel::onPageEdgeClicked);

  applyLinearGeometry(vertical, pageNames, paging, canPrev, canNext);
}

void TouchPresetPickerPanel::refreshAppearanceLive() {
  refreshFromSettings(false);
}

void TouchPresetPickerPanel::refreshFromSettings(bool rebuildLayout) {
  if (!isOpen()) return;
  if (rebuildLayout) {
    TouchPresetManager *mgr = TouchPresetManager::instance();
    alignPageToActive(mgr->pinnedPresetsForPicker(), mgr->activePreset());
    rebuild();
  } else {
    applyPickerMetricsFromSettings();
    if (!showCurrentPage()) rebuild();
  }
  finishPickerLayout();
}

void TouchPresetPickerPanel::onPageEdgeClicked() {
  if (!pickerInputAllowed()) return;
  auto *btn = qobject_cast<QPushButton *>(sender());
  if (!btn || !btn->isEnabled()) return;
  const int delta = btn->property("pageDelta").toInt();
  QTimer::singleShot(0, this, [this, delta]() { applyPageDelta(delta); });
}

QPoint TouchPresetPickerPanel::clampTopLeft(QPoint topLeft) const {
  if (!m_viewer) return topLeft;
  const QRect bounds = m_viewer->rect();
  const QSize sz     = size();
  if (topLeft.x() < 0) topLeft.setX(0);
  if (topLeft.y() < 0) topLeft.setY(0);
  if (topLeft.x() + sz.width() > bounds.width())
    topLeft.setX(qMax(0, bounds.width() - sz.width()));
  if (topLeft.y() + sz.height() > bounds.height())
    topLeft.setY(qMax(0, bounds.height() - sz.height()));
  return topLeft;
}

bool TouchPresetPickerPanel::pickerInputAllowed() const {
  return QDateTime::currentMSecsSinceEpoch() >= m_blockInputUntilMs;
}

QPoint TouchPresetPickerPanel::anchorTopLeft() const {
  const int gap = qMax(32, m_edgeCircle + 10);
  const QPoint touch = m_anchor.toPoint();
  // Keep the touch point below the panel so presets are not under the finger.
  return touch - QPoint(width() / 2, height() + gap);
}

void TouchPresetPickerPanel::positionPanel() {
  move(clampTopLeft(anchorTopLeft()));
}

void TouchPresetPickerPanel::openAt(const QPointF &anchorWidgetPos) {
  m_anchor = anchorWidgetPos;
  const QStringList names =
      TouchPresetManager::instance()->pinnedPresetsForPicker();
  alignPageToActive(names, TouchPresetManager::instance()->activePreset());
  rebuild();
  finishPickerLayout();
  m_blockInputUntilMs = QDateTime::currentMSecsSinceEpoch() + 320;
  m_touchActive          = false;
  m_touchPagingCandidate = false;
  show();
  raise();
  setFocus(Qt::PopupFocusReason);
}

void TouchPresetPickerPanel::closePicker() { hide(); }

void TouchPresetPickerPanel::toggleAt(const QPointF &anchorWidgetPos) {
  if (isVisible())
    closePicker();
  else
    openAt(anchorWidgetPos);
}

bool TouchPresetPickerPanel::event(QEvent *event) {
  if (event->type() == QEvent::ShortcutOverride) {
    auto *ke = static_cast<QKeyEvent *>(event);
    if (ke->key() == Qt::Key_Escape) {
      closePicker();
      event->accept();
      return true;
    }
  }
  if (event->type() == QEvent::StyleChange && isVisible()) {
    applyPickerMetricsFromSettings();
    update();
  }
  return QWidget::event(event);
}

void TouchPresetPickerPanel::keyPressEvent(QKeyEvent *event) {
  if (event->key() == Qt::Key_Escape) {
    closePicker();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void TouchPresetPickerPanel::mousePressEvent(QMouseEvent *event) {
  if (TouchWorkspaceSettingsPopup::isPanelOpen() ||
      TouchWorkspaceSettingsPopup::isLivePickerPreviewActive()) {
    QWidget::mousePressEvent(event);
    return;
  }
  if (!pickerInputAllowed()) {
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void TouchPresetPickerPanel::mouseReleaseEvent(QMouseEvent *event) {
  if (!pickerInputAllowed() ||
      TouchWorkspaceSettingsPopup::isPanelOpen() ||
      TouchWorkspaceSettingsPopup::isLivePickerPreviewActive()) {
    QWidget::mouseReleaseEvent(event);
    return;
  }
  if (TouchPresetManager::instance()->pickerLayout() ==
      TouchPresetPickerLayout::Wheel) {
    const int idx = ghostPieSlotIndex(event->pos());
    if (idx == -1) {
      closePicker();
      event->accept();
      return;
    }
    if (idx >= 0) {
      if (QPushButton *btn = slotAt(idx)) btn->click();
      event->accept();
      return;
    }
  }
  QWidget::mouseReleaseEvent(event);
}

void TouchPresetPickerPanel::onPresetClicked() {
  if (!pickerInputAllowed()) return;
  auto *btn = qobject_cast<QPushButton *>(sender());
  if (!btn) return;
  const QString name = btn->property("presetName").toString();
  if (name.isEmpty()) return;
  if (name != TouchPresetManager::instance()->activePreset())
    TouchPresetManager::instance()->setActivePreset(name);
  closePicker();
}

bool TouchPresetPickerPanel::handleViewerTouch(const QPoint &viewerPos,
                                               QEvent::Type type) {
  if (!isVisible()) return false;

  const QPoint local = mapFromParent(viewerPos);
  const bool inside  = geometry().contains(viewerPos);

  if (!inside) return false;

  if (type == QEvent::TouchBegin) {
    m_touchStartLocal      = local;
    m_touchActive          = true;
    m_touchPagingCandidate = false;
    return true;
  }

  if (type == QEvent::TouchUpdate) {
    if (!m_touchActive) return true;
    const int dragDist = QApplication::startDragDistance();
    if (QLineF(m_touchStartLocal, local).length() > qMax(12, dragDist / 2))
      m_touchPagingCandidate = true;
    return true;
  }

  if (type != QEvent::TouchEnd && type != QEvent::TouchCancel) return true;

  m_touchActive = false;
  if (!pickerInputAllowed()) return true;

  if (tryPageBySwipe(local)) return true;
  if (m_touchPagingCandidate) return true;

  if (TouchPresetManager::instance()->pickerLayout() ==
      TouchPresetPickerLayout::Wheel) {
    const int idx = ghostPieSlotIndex(m_touchStartLocal);
    if (idx == -1) {
      closePicker();
      return true;
    }
    if (idx >= 0) {
      if (QPushButton *btn = slotAt(idx)) btn->click();
      return true;
    }
  }

  if (QPushButton *btn = buttonAt(m_touchStartLocal)) {
    btn->click();
    return true;
  }

  return true;
}

//-----------------------------------------------------------------------------

class TouchPresetPickerCommand final : public MenuItemHandler {
public:
  TouchPresetPickerCommand() : MenuItemHandler(MI_TouchPresetPicker) {}
  void execute() override {
    SceneViewer *viewer = TApp::instance()->getActiveViewer();
    if (viewer) viewer->toggleTouchPresetPicker();
  }
} touchPresetPickerCommand;
