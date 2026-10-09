#include "touchviewerhud.h"

#include "sceneviewer.h"
#include "menubarcommandids.h"
#include "touchpresetmanager.h"
#include "tapp.h"

#include "toonzqt/menubarcommand.h"

#include "tools/toolhandle.h"
#include "tools/tool.h"
#include "tproperty.h"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QRect>
#include <QShowEvent>
#include <QTimer>

namespace {

bool touchGestureControlOn() {
  QAction *a = CommandManager::instance()->getAction(MI_TouchGestureControl);
  return a && a->isChecked();
}

QString frameCellText(int frame) {
  return frame >= 1 ? QString::number(frame) : QString();
}

int textWidth(const QFontMetrics &fm, const QString &text) {
  if (text.isEmpty()) return 0;
  return qMax(fm.horizontalAdvance(text), fm.boundingRect(text).width());
}

int frostLuminance(const QPixmap &frost) {
  if (frost.isNull()) return 200;
  const QImage img = frost.toImage().scaled(8, 8, Qt::IgnoreAspectRatio,
                                            Qt::FastTransformation);
  if (img.isNull()) return 200;
  qint64 sum = 0;
  int n      = 0;
  for (int y = 0; y < img.height(); ++y) {
    for (int x = 0; x < img.width(); ++x) {
      const QRgb px = img.pixel(x, y);
      sum += qRed(px) + qGreen(px) + qBlue(px);
      ++n;
    }
  }
  return n > 0 ? static_cast<int>(sum / (n * 3)) : 200;
}

struct HudLayout {
  QFont textFont;
  QFont bold;
  QStringList toolParts;
  QString prev;
  QString cur;
  QString next;
  bool showFrame = false;
  int padH       = 20;
  int padV       = 10;
  int gap        = 16;
  int slotW      = 0;
  int boxW       = 0;
  int boxH       = 0;
};

HudLayout makeHudLayout(const QFont &baseFont, bool showSize,
                        const QString &sizeText, bool showHardness,
                        const QString &hardnessText, bool showFrame,
                        int prevFrame, int currentFrame, int nextFrame) {
  HudLayout L;
  L.textFont = baseFont;
  L.textFont.setPointSizeF(L.textFont.pointSizeF() + 1.0);
  L.bold = L.textFont;
  L.bold.setBold(true);
  if (showSize && !sizeText.isEmpty()) L.toolParts << sizeText;
  if (showHardness && !hardnessText.isEmpty()) L.toolParts << hardnessText;
  L.showFrame = showFrame;
  L.prev      = frameCellText(prevFrame);
  L.cur       = frameCellText(currentFrame);
  L.next      = frameCellText(nextFrame);

  QFontMetrics fm(L.textFont);
  QFontMetrics fmBold(L.bold);
  int contentW = 0;
  for (int i = 0; i < L.toolParts.size(); ++i) {
    contentW += textWidth(fm, L.toolParts[i]);
    if (i + 1 < L.toolParts.size()) contentW += L.gap;
  }
  if (L.showFrame) {
    L.slotW = textWidth(fmBold, QStringLiteral("88"));
    L.slotW = qMax(L.slotW, textWidth(fm, L.prev));
    L.slotW = qMax(L.slotW, textWidth(fmBold, L.cur));
    L.slotW = qMax(L.slotW, textWidth(fm, L.next));
    if (!L.toolParts.isEmpty()) contentW += L.gap;
    contentW += L.slotW * 3 + L.gap * 2;
  }
  L.boxW = qMax(80, contentW + L.padH * 2);
  L.boxH = fm.height() + L.padV * 2;
  return L;
}

TPropertyGroup *toolPropertyGroup(int idx) {
  TTool *tool = TApp::instance()->getCurrentTool()->getTool();
  if (!tool) return nullptr;
  return tool->getProperties(idx);
}

}  // namespace

bool touchViewerHudHostActive(const QWidget *viewer) {
  const SceneViewer *sceneViewer = qobject_cast<const SceneViewer *>(viewer);
  if (!sceneViewer || !touchGestureControlOn()) return false;
  if (!TouchPresetManager::instance()->viewerHudEnabled()) return false;
  return sceneViewer->isImmersiveForTouchViewerHud();
}

QString touchViewerHudFormatBrushSize() {
  for (int g = 0; g < 2; ++g) {
    TPropertyGroup *group = toolPropertyGroup(g);
    if (!group) continue;
    for (int i = 0; i < group->getPropertyCount(); ++i) {
      TProperty *prop = group->getProperty(i);
      if (!prop) continue;
      const std::string name = prop->getName();
      if (name != "Size:" && name != "Size" && name != "Thickness") continue;
      if (auto *ip = dynamic_cast<TIntProperty *>(prop))
        return QString::number(ip->getValue());
      if (auto *dp = dynamic_cast<TDoubleProperty *>(prop))
        return QString::number(dp->getValue(), 'g', 4);
      if (auto *pp = dynamic_cast<TIntPairProperty *>(prop)) {
        const TIntPairProperty::Value v = pp->getValue();
        return QString::number(v.second);
      }
      if (auto *dp = dynamic_cast<TDoublePairProperty *>(prop)) {
        const TDoublePairProperty::Value v = dp->getValue();
        return QString::number(v.second, 'g', 4);
      }
    }
  }
  return QString();
}

QString touchViewerHudFormatBrushHardness() {
  for (int g = 0; g < 2; ++g) {
    TPropertyGroup *group = toolPropertyGroup(g);
    if (!group) continue;
    for (int i = 0; i < group->getPropertyCount(); ++i) {
      TProperty *prop = group->getProperty(i);
      if (!prop) continue;
      const std::string name = prop->getName();
      if (name != "Hardness:" && name != "Hardness") continue;
      if (auto *dp = dynamic_cast<TDoubleProperty *>(prop))
        return QString::number(qRound(dp->getValue()));
      if (auto *ip = dynamic_cast<TIntProperty *>(prop))
        return QString::number(ip->getValue());
    }
  }
  return QString();
}

//-----------------------------------------------------------------------------

TouchViewerHud::TouchViewerHud(SceneViewer *viewer)
    : QWidget(viewer && viewer->parentWidget() ? viewer->parentWidget()
                                               : static_cast<QWidget *>(viewer)),
      m_viewer(viewer) {
  setObjectName(QStringLiteral("TouchViewerHud"));
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_AlwaysStackOnTop);
  setAutoFillBackground(false);
  setFocusPolicy(Qt::NoFocus);
  resize(120, 32);

  m_fadeTimer = new QTimer(this);
  m_fadeTimer->setInterval(40);
  connect(m_fadeTimer, &QTimer::timeout, this, &TouchViewerHud::onFadeTick);
  hide();
}

void TouchViewerHud::clearForInactiveContext() {
  m_fadeTimer->stop();
  m_holdMs       = 0;
  m_opacity      = 0.0;
  m_showSize     = false;
  m_showHardness = false;
  m_showFrame    = false;
  m_frost        = QPixmap();
  hide();
}

bool TouchViewerHud::hasContent() const {
  return m_showSize || m_showHardness || m_showFrame;
}

void TouchViewerHud::positionAtTop() {
  QWidget *host = parentWidget();
  if (!host) host = m_viewer;
  if (!host) return;
  const int margin = 16;
  const int x      = (host->width() - width()) / 2;
  move(qMax(0, x), margin);
  raise();
}

void TouchViewerHud::relayout() {
  const HudLayout L =
      makeHudLayout(font(), m_showSize, m_sizeText, m_showHardness,
                    m_hardnessText, m_showFrame, m_prevFrame, m_currentFrame,
                    m_nextFrame);
  resize(L.boxW, L.boxH);
  positionAtTop();
}

void TouchViewerHud::captureFrost() {
  m_frost = QPixmap();
  if (TouchPresetManager::instance()->viewerHudStyle() !=
      TouchViewerHudStyle::Frost)
    return;
  if (!m_viewer || width() < 2 || height() < 2) return;
  const QImage fb = m_viewer->grabFramebuffer();
  if (fb.isNull() || fb.width() < 2 || fb.height() < 2) return;

  QWidget *host = parentWidget() ? parentWidget() : static_cast<QWidget *>(m_viewer);
  const QPoint topLeft = m_viewer->mapFrom(host, pos());
  QRect src(topLeft, size());
  src = src.intersected(QRect(0, 0, m_viewer->width(), m_viewer->height()));
  if (src.isEmpty()) return;

  const qreal sx = fb.width() / qMax(1.0, double(m_viewer->width()));
  const qreal sy = fb.height() / qMax(1.0, double(m_viewer->height()));
  QRect fbRect(qRound(src.x() * sx), qRound(src.y() * sy),
               qRound(src.width() * sx), qRound(src.height() * sy));
  fbRect = fbRect.intersected(fb.rect());
  if (fbRect.width() < 2 || fbRect.height() < 2) return;

  const QImage slice = fb.copy(fbRect);
  const QSize tiny(qMax(1, slice.width() / 6), qMax(1, slice.height() / 6));
  const QImage mist = slice
                          .scaled(tiny, Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation)
                          .scaled(size(), Qt::IgnoreAspectRatio,
                                  Qt::SmoothTransformation);
  m_frost = QPixmap::fromImage(mist);
}

void TouchViewerHud::bump() {
  m_holdMs  = 2000;
  m_opacity = 1.0;
  relayout();
  captureFrost();
  show();
  raise();
  update();
  if (!m_fadeTimer->isActive()) m_fadeTimer->start();
}

void TouchViewerHud::onFadeTick() {
  if (m_holdMs > 0) {
    m_holdMs -= m_fadeTimer->interval();
    return;
  }
  m_opacity -= 0.07;
  if (m_opacity <= 0.0) {
    m_fadeTimer->stop();
    m_opacity      = 0.0;
    m_showSize     = false;
    m_showHardness = false;
    m_showFrame    = false;
    m_frost        = QPixmap();
    hide();
    return;
  }
  update();
}

void TouchViewerHud::pulseFrameContext(int prevFrame, int currentFrame,
                                       int nextFrame) {
  m_prevFrame    = prevFrame;
  m_currentFrame = currentFrame;
  m_nextFrame    = nextFrame;
  m_showSize     = false;
  m_showHardness = false;
  m_showFrame    = true;
  bump();
}

void TouchViewerHud::pulseBrushSize(const QString &text) {
  if (text.isEmpty()) return;
  m_sizeText     = tr("Size : %1").arg(text);
  m_showSize     = true;
  m_showHardness = false;
  m_showFrame    = false;
  bump();
}

void TouchViewerHud::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  positionAtTop();
  raise();
}

void TouchViewerHud::pulseBrushHardness(const QString &text) {
  if (text.isEmpty()) return;
  m_hardnessText = tr("Hardness : %1").arg(text);
  m_showSize     = false;
  m_showHardness = true;
  m_showFrame    = false;
  bump();
}

void TouchViewerHud::paintEvent(QPaintEvent *event) {
  if (!hasContent() || m_opacity <= 0.0) {
    QWidget::paintEvent(event);
    return;
  }

  const HudLayout L =
      makeHudLayout(font(), m_showSize, m_sizeText, m_showHardness,
                    m_hardnessText, m_showFrame, m_prevFrame, m_currentFrame,
                    m_nextFrame);

  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.setRenderHint(QPainter::TextAntialiasing, true);
  p.setRenderHint(QPainter::SmoothPixmapTransform, true);
  p.setOpacity(m_opacity);

  const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -1.5, -1.5);
  const qreal radius = qMin(box.height() * 0.5, 18.0);
  QPainterPath pill;
  pill.addRoundedRect(box, radius, radius);

  const bool frost = TouchPresetManager::instance()->viewerHudStyle() ==
                     TouchViewerHudStyle::Frost;

  p.setPen(Qt::NoPen);
  const bool frostOnLight =
      frost && frostLuminance(m_frost) >= 150;
  if (frost) {
    p.setClipPath(pill);
    if (!m_frost.isNull()) p.drawPixmap(rect(), m_frost);
    p.fillPath(pill, frostOnLight ? QColor(36, 36, 40, 96)
                                  : QColor(255, 255, 255, 72));
    p.setClipping(false);
  } else {
    p.setBrush(QColor(28, 28, 28, 210));
    p.drawPath(pill);
  }

  const QColor toolColor =
      frost ? QColor(0x2a, 0x2a, 0x2e) : QColor(0xf2, 0xf2, 0xf2);
  const QColor curColor =
      frost ? QColor(0x1a, 0x1a, 0x1e) : QColor(0xff, 0xff, 0xff);
  const QColor sideColor =
      frost ? QColor(0x6a, 0x6a, 0x72) : QColor(0xa8, 0xa8, 0xa8);

  int x = L.padH;
  QFontMetrics fm(L.textFont);

  for (int i = 0; i < L.toolParts.size(); ++i) {
    const int w = textWidth(fm, L.toolParts[i]);
    p.setFont(L.textFont);
    p.setPen(toolColor);
    p.drawText(QRect(x, 0, w, height()), Qt::AlignVCenter | Qt::AlignLeft,
               L.toolParts[i]);
    x += w + L.gap;
  }

  if (L.showFrame) {
    const QString cells[3] = {L.prev, L.cur, L.next};
    for (int i = 0; i < 3; ++i) {
      const QRect slot(x + i * (L.slotW + L.gap), 0, L.slotW, height());
      if (cells[i].isEmpty()) continue;
      const bool current = (i == 1);
      p.setFont(current ? L.bold : L.textFont);
      p.setPen(current ? curColor : sideColor);
      p.drawText(slot, Qt::AlignCenter, cells[i]);
    }
  }
}
