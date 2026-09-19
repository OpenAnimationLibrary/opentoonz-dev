#pragma once

#ifndef TOUCH_SLOTS_H
#define TOUCH_SLOTS_H

#include <QList>
#include <QString>

enum TouchSlot {
  TouchSlot_None = 0,

  TouchSlot_ReservedPan,
  TouchSlot_ReservedPinchZoom,
  TouchSlot_ReservedRotate,

  TouchSlot_Tap1,
  TouchSlot_Tap2,
  TouchSlot_Tap3,
  TouchSlot_Tap4,
  TouchSlot_Tap5,

  TouchSlot_DoubleTap2,
  TouchSlot_DoubleTap3,
  TouchSlot_DoubleTap4,
  TouchSlot_DoubleTap5,

  TouchSlot_Hold1,
  TouchSlot_Hold2,
  TouchSlot_Hold3,
  TouchSlot_Hold4,
  TouchSlot_Hold5,

  TouchSlot_Swipe3Left,
  TouchSlot_Swipe3Right,
  TouchSlot_Swipe3Up,
  TouchSlot_Swipe3Down,
  TouchSlot_Swipe4Left,
  TouchSlot_Swipe4Right,
  TouchSlot_Swipe4Up,
  TouchSlot_Swipe4Down,
  TouchSlot_Swipe5Left,
  TouchSlot_Swipe5Right,
  TouchSlot_Swipe5Up,
  TouchSlot_Swipe5Down,

  TouchSlot_Pinch3In,
  TouchSlot_Pinch3Out,
  TouchSlot_Pinch4In,
  TouchSlot_Pinch4Out,

  TouchSlot_Edge3Left,
  TouchSlot_Edge3Right,
  TouchSlot_Edge3Top,
  TouchSlot_Edge3Bottom,

  TouchSlot_StylusPlus1,
  TouchSlot_StylusPlus2,

  TouchSlot_Count
};

const char *touchSlotId(TouchSlot slot);
QString touchSlotLabel(TouchSlot slot);
QString touchSlotShortLabel(TouchSlot slot);
TouchSlot touchSlotFromId(const QString &id);
bool touchSlotIsReserved(TouchSlot slot);
QList<TouchSlot> assignableTouchSlots();
QList<TouchSlot> reservedTouchSlots();

enum TouchSlotFamily {
  TouchSlotFamily_Reserved = 0,
  TouchSlotFamily_Tap,
  TouchSlotFamily_DoubleTap,
  TouchSlotFamily_Hold,
  TouchSlotFamily_Swipe,
  TouchSlotFamily_Pinch,
  TouchSlotFamily_Edge,
  TouchSlotFamily_Stylus
};

TouchSlotFamily touchSlotFamily(TouchSlot slot);
QString touchSlotFamilyLabel(TouchSlotFamily family);
QList<TouchSlotFamily> assignableTouchFamilies();
QList<TouchSlot> touchSlotsInFamily(TouchSlotFamily family);

#endif
