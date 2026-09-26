#pragma once

#include <QMap>
#include <QMetaType>
#include <QString>
#include <QVariant>

struct PreferencesItem {
  QString idString;
  QMetaType::Type type;
  QVariant min;
  QVariant max;
};

class Preferences {
public:
  QMap<int, PreferencesItem> m_items;
  static Preferences *instance() {
    static Preferences value;
    return &value;
  }
};
