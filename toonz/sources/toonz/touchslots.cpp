#include "touchslots.h"

#include <QObject>

namespace {

struct SlotInfo {
  TouchSlot slot;
  const char *id;
  const char *label;
  bool reserved;
};

const SlotInfo kSlots[] = {
    {TouchSlot_ReservedPan, "Reserved_Pan", "1 finger drag (Pan)", true},
    {TouchSlot_ReservedPinchZoom, "Reserved_PinchZoom", "2 finger pinch (Zoom)",
     true},
    {TouchSlot_ReservedRotate, "Reserved_Rotate", "2 finger rotate", true},

    {TouchSlot_Tap2, "Tap_2", "2 finger tap", false},
    {TouchSlot_Tap3, "Tap_3", "3 finger tap", false},
    {TouchSlot_Tap4, "Tap_4", "4 finger tap", false},
    {TouchSlot_Tap5, "Tap_5", "5 finger tap", false},

    {TouchSlot_DoubleTap2, "DoubleTap_2", "2 finger double tap", false},
    {TouchSlot_DoubleTap3, "DoubleTap_3", "3 finger double tap", false},
    {TouchSlot_DoubleTap4, "DoubleTap_4", "4 finger double tap", false},
    {TouchSlot_DoubleTap5, "DoubleTap_5", "5 finger double tap", false},

    {TouchSlot_Hold2, "Hold_2", "2 finger hold", false},
    {TouchSlot_Hold3, "Hold_3", "3 finger hold", false},
    {TouchSlot_Hold4, "Hold_4", "4 finger hold", false},
    {TouchSlot_Hold5, "Hold_5", "5 finger hold", false},

    {TouchSlot_Swipe3Left, "Swipe3_Left", "3 finger swipe left", false},
    {TouchSlot_Swipe3Right, "Swipe3_Right", "3 finger swipe right", false},
    {TouchSlot_Swipe3Up, "Swipe3_Up", "3 finger swipe up", false},
    {TouchSlot_Swipe3Down, "Swipe3_Down", "3 finger swipe down", false},
    {TouchSlot_Swipe4Left, "Swipe4_Left", "4 finger swipe left", false},
    {TouchSlot_Swipe4Right, "Swipe4_Right", "4 finger swipe right", false},
    {TouchSlot_Swipe4Up, "Swipe4_Up", "4 finger swipe up", false},
    {TouchSlot_Swipe4Down, "Swipe4_Down", "4 finger swipe down", false},
    {TouchSlot_Swipe5Left, "Swipe5_Left", "5 finger swipe left", false},
    {TouchSlot_Swipe5Right, "Swipe5_Right", "5 finger swipe right", false},
    {TouchSlot_Swipe5Up, "Swipe5_Up", "5 finger swipe up", false},
    {TouchSlot_Swipe5Down, "Swipe5_Down", "5 finger swipe down", false},

    {TouchSlot_Pinch3In, "Pinch3_In", "3 finger pinch in", false},
    {TouchSlot_Pinch3Out, "Pinch3_Out", "3 finger pinch out", false},
    {TouchSlot_Pinch4In, "Pinch4_In", "4 finger pinch in", false},
    {TouchSlot_Pinch4Out, "Pinch4_Out", "4 finger pinch out", false},

    {TouchSlot_Edge3Left, "Edge3_Left", "3 finger from left edge", false},
    {TouchSlot_Edge3Right, "Edge3_Right", "3 finger from right edge", false},
    {TouchSlot_Edge3Top, "Edge3_Top", "3 finger from top edge", false},
    {TouchSlot_Edge3Bottom, "Edge3_Bottom", "3 finger from bottom edge", false},

    {TouchSlot_StylusPlus1, "StylusPlus_1", "Stylus + 1 finger hold", false},
    {TouchSlot_StylusPlus2, "StylusPlus_2", "Stylus + 2 finger hold", false},
};

const SlotInfo *findInfo(TouchSlot slot) {
  for (const SlotInfo &info : kSlots) {
    if (info.slot == slot) return &info;
  }
  return nullptr;
}

}  // namespace

const char *touchSlotId(TouchSlot slot) {
  if (const SlotInfo *info = findInfo(slot)) return info->id;
  return "";
}

QString touchSlotLabel(TouchSlot slot) {
  if (const SlotInfo *info = findInfo(slot))
    return QObject::tr(info->label);
  return QString();
}

QString touchSlotShortLabel(TouchSlot slot) {
  switch (slot) {
  case TouchSlot_ReservedPan:
    return QObject::tr("Pan");
  case TouchSlot_ReservedPinchZoom:
    return QObject::tr("Zoom");
  case TouchSlot_ReservedRotate:
    return QObject::tr("Rotate");
  case TouchSlot_Tap2:
  case TouchSlot_DoubleTap2:
  case TouchSlot_Hold2:
    return QObject::tr("2 fingers");
  case TouchSlot_Tap3:
  case TouchSlot_DoubleTap3:
  case TouchSlot_Hold3:
    return QObject::tr("3 fingers");
  case TouchSlot_Tap4:
  case TouchSlot_DoubleTap4:
  case TouchSlot_Hold4:
    return QObject::tr("4 fingers");
  case TouchSlot_Tap5:
  case TouchSlot_DoubleTap5:
  case TouchSlot_Hold5:
    return QObject::tr("5 fingers");
  case TouchSlot_Swipe3Left:
    return QObject::tr("3 left");
  case TouchSlot_Swipe3Right:
    return QObject::tr("3 right");
  case TouchSlot_Swipe3Up:
    return QObject::tr("3 up");
  case TouchSlot_Swipe3Down:
    return QObject::tr("3 down");
  case TouchSlot_Swipe4Left:
    return QObject::tr("4 left");
  case TouchSlot_Swipe4Right:
    return QObject::tr("4 right");
  case TouchSlot_Swipe4Up:
    return QObject::tr("4 up");
  case TouchSlot_Swipe4Down:
    return QObject::tr("4 down");
  case TouchSlot_Swipe5Left:
    return QObject::tr("5 left");
  case TouchSlot_Swipe5Right:
    return QObject::tr("5 right");
  case TouchSlot_Swipe5Up:
    return QObject::tr("5 up");
  case TouchSlot_Swipe5Down:
    return QObject::tr("5 down");
  case TouchSlot_Edge3Left:
    return QObject::tr("Left");
  case TouchSlot_Edge3Right:
    return QObject::tr("Right");
  case TouchSlot_Edge3Top:
    return QObject::tr("Top");
  case TouchSlot_Edge3Bottom:
    return QObject::tr("Bottom");
  case TouchSlot_Pinch3In:
    return QObject::tr("3 in");
  case TouchSlot_Pinch3Out:
    return QObject::tr("3 out");
  case TouchSlot_Pinch4In:
    return QObject::tr("4 in");
  case TouchSlot_Pinch4Out:
    return QObject::tr("4 out");
  case TouchSlot_StylusPlus1:
    return QObject::tr("+ 1 finger");
  case TouchSlot_StylusPlus2:
    return QObject::tr("+ 2 fingers");
  default:
    return touchSlotLabel(slot);
  }
}

TouchSlot touchSlotFromId(const QString &id) {
  for (const SlotInfo &info : kSlots) {
    if (id == QLatin1String(info.id)) return info.slot;
  }
  return TouchSlot_None;
}

bool touchSlotIsReserved(TouchSlot slot) {
  if (const SlotInfo *info = findInfo(slot)) return info->reserved;
  return false;
}

QList<TouchSlot> assignableTouchSlots() {
  QList<TouchSlot> out;
  for (const SlotInfo &info : kSlots) {
    if (!info.reserved) out.append(info.slot);
  }
  return out;
}

QList<TouchSlot> reservedTouchSlots() {
  QList<TouchSlot> out;
  for (const SlotInfo &info : kSlots) {
    if (info.reserved) out.append(info.slot);
  }
  return out;
}

TouchSlotFamily touchSlotFamily(TouchSlot slot) {
  if (touchSlotIsReserved(slot)) return TouchSlotFamily_Reserved;
  switch (slot) {
  case TouchSlot_Tap2:
  case TouchSlot_Tap3:
  case TouchSlot_Tap4:
  case TouchSlot_Tap5:
    return TouchSlotFamily_Tap;
  case TouchSlot_DoubleTap2:
  case TouchSlot_DoubleTap3:
  case TouchSlot_DoubleTap4:
  case TouchSlot_DoubleTap5:
    return TouchSlotFamily_DoubleTap;
  case TouchSlot_Hold2:
  case TouchSlot_Hold3:
  case TouchSlot_Hold4:
  case TouchSlot_Hold5:
    return TouchSlotFamily_Hold;
  case TouchSlot_Swipe3Left:
  case TouchSlot_Swipe3Right:
  case TouchSlot_Swipe3Up:
  case TouchSlot_Swipe3Down:
  case TouchSlot_Swipe4Left:
  case TouchSlot_Swipe4Right:
  case TouchSlot_Swipe4Up:
  case TouchSlot_Swipe4Down:
  case TouchSlot_Swipe5Left:
  case TouchSlot_Swipe5Right:
  case TouchSlot_Swipe5Up:
  case TouchSlot_Swipe5Down:
    return TouchSlotFamily_Swipe;
  case TouchSlot_Pinch3In:
  case TouchSlot_Pinch3Out:
  case TouchSlot_Pinch4In:
  case TouchSlot_Pinch4Out:
    return TouchSlotFamily_Pinch;
  case TouchSlot_Edge3Left:
  case TouchSlot_Edge3Right:
  case TouchSlot_Edge3Top:
  case TouchSlot_Edge3Bottom:
    return TouchSlotFamily_Edge;
  case TouchSlot_StylusPlus1:
  case TouchSlot_StylusPlus2:
    return TouchSlotFamily_Stylus;
  default:
    return TouchSlotFamily_Tap;
  }
}

QString touchSlotFamilyLabel(TouchSlotFamily family) {
  switch (family) {
  case TouchSlotFamily_Reserved:
    return QObject::tr("Reserved");
  case TouchSlotFamily_Tap:
    return QObject::tr("Tap");
  case TouchSlotFamily_DoubleTap:
    return QObject::tr("Double tap");
  case TouchSlotFamily_Hold:
    return QObject::tr("Hold");
  case TouchSlotFamily_Swipe:
    return QObject::tr("Swipe");
  case TouchSlotFamily_Pinch:
    return QObject::tr("Pinch");
  case TouchSlotFamily_Edge:
    return QObject::tr("From edge");
  case TouchSlotFamily_Stylus:
    return QObject::tr("Stylus + finger");
  }
  return QString();
}

QList<TouchSlotFamily> assignableTouchFamilies() {
  return {TouchSlotFamily_Tap,       TouchSlotFamily_DoubleTap,
          TouchSlotFamily_Hold,      TouchSlotFamily_Swipe,
          TouchSlotFamily_Pinch,     TouchSlotFamily_Edge,
          TouchSlotFamily_Stylus};
}

QList<TouchSlot> touchSlotsInFamily(TouchSlotFamily family) {
  QList<TouchSlot> out;
  if (family == TouchSlotFamily_Reserved) return reservedTouchSlots();
  for (TouchSlot slot : assignableTouchSlots()) {
    if (touchSlotFamily(slot) == family) out.append(slot);
  }
  return out;
}
