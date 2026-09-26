#pragma once

#include <QList>
#include <QString>

namespace ConfigTransfer {

struct Options {
  bool recentFiles = false;
  bool library     = false;
  bool plugins     = false;
};

struct Item {
  QString archivePath;
  QString category;
  QString destination;
  QString status;
  QString detail;
  bool selected   = false;
  bool existing   = false;
  bool compatible = true;
};

bool save(const QString &archivePath, const Options &options, QString &error);
bool inspect(const QString &archivePath, QList<Item> &items, QString &error);
bool queueRestore(const QString &archivePath, const QList<Item> &items,
                  QString &error);
// Called after TEnv is initialized, before Preferences and plugins are loaded.
bool applyPending(QString &error);

}  // namespace ConfigTransfer
