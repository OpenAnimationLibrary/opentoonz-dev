#pragma once

#ifndef NOTEINK_INCLUDED
#define NOTEINK_INCLUDED

#include <QColor>
#include <QList>
#include <QPointF>
#include <QRect>
#include <QVector>

enum NoteInkToolKind {
  NoteInkPencil0 = 0,
  NoteInkPencil1 = 1,
  NoteInkPencil2 = 2,
  NoteInkEraser  = 3
};

const int NoteInkPencilCount = 3;
const int NoteInkSliderInset = 4;
const int NoteInkChipMargin  = 1;

inline QRect noteInkIconSlotRect(int slotIndex, int nSlots, int slotGap,
                                 const QRect &box, int iconRowH) {
  int slotW = (box.width() - slotGap * (nSlots - 1)) / nSlots;
  if (slotW < 4) slotW = 4;
  int x = box.left() + slotIndex * (slotW + slotGap);
  return QRect(x, box.top(), slotW, iconRowH);
}

inline QRect noteInkSlotChipSquare(const QRect &slot) {
  QRect inner = slot.adjusted(1, 1, -1, -1);
  int s       = qMin(inner.width(), inner.height());
  if (s < 2) return inner;
  return QRect(inner.x() + (inner.width() - s) / 2,
               inner.y() + (inner.height() - s) / 2, s, s);
}

// Timeline step control aligned under the gap between two icon slots (wide
// zone).
inline QRect noteInkTimelineStepZone(int gapIndex, int nSlots, int slotGap,
                                     const QRect &iconBox, int iconRowH,
                                     const QRect &sliderBox) {
  const int nZones = nSlots - 1;
  if (nZones < 1 || gapIndex < 0 || gapIndex >= nZones) return sliderBox;

  int zoneW = qMax(8, sliderBox.width() / nZones);
  int x     = sliderBox.left() + gapIndex * zoneW;
  if (gapIndex == nZones - 1) zoneW = sliderBox.right() - x + 1;

  QRect zone(x, sliderBox.top(), zoneW, sliderBox.height());

  QRect slot0 =
      noteInkIconSlotRect(gapIndex, nSlots, slotGap, iconBox, iconRowH);
  QRect slot1 =
      noteInkIconSlotRect(gapIndex + 1, nSlots, slotGap, iconBox, iconRowH);
  const int dx  = sliderBox.left() - iconBox.left();
  int gapCenter = (slot0.center().x() + slot1.center().x()) / 2 + dx;
  gapCenter     = qBound(sliderBox.left() + zoneW / 2, gapCenter,
                         sliderBox.right() - zoneW / 2);
  zone.moveLeft(gapCenter - zoneW / 2);
  return zone.intersected(sliderBox);
}

struct NoteInkStroke {
  QVector<QPointF> points;
  QColor color = QColor(40, 40, 40);
  double width = 1.6;
};

typedef QList<NoteInkStroke> NoteInkStrokeList;

inline QColor defaultNoteInkPencil(int index) {
  switch (index) {
  case 1:
    return QColor(180, 40, 40);
  case 2:
    return QColor(30, 70, 170);
  default:
    return QColor(40, 40, 40);
  }
}

#endif  // NOTEINK_INCLUDED
