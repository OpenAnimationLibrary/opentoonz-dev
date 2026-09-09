#pragma once

#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>

#include <functional>

namespace ProductionArchive {

using Progress = std::function<void(const QString &, qint64, qint64)>;

struct Entry {
  QString source, destination;
  qint64 size;
  QDateTime modified;
  QFile::Permissions permissions;
  bool directory;
  QString linkTarget;
};

QString normalizedPath(const QString &path);
QString relativePath(const QString &from, const QString &to);
bool containsPath(const QString &parent, const QString &path);
QString safeName(QString name);

class CopyPlan {
  QVector<Entry> m_entries;
  QHash<QString, QString> m_destinations;
  QStringList m_excluded;
  qint64 m_size = 0;
  Progress m_progress;
  QString m_linkRoot;

  void scan(const QString &source, const QString &destination,
            QSet<QString> ancestors);

public:
  explicit CopyPlan(const Progress &progress) : m_progress(progress) {}
  void exclude(const QString &path);
  void preserveLinksInside(const QString &path);
  void add(const QString &source, const QString &destination);
  const QVector<Entry> &entries() const { return m_entries; }
  qint64 size() const { return m_size; }
  void copy(const QString &root) const;
};

void writeFile(const QString &path, const QByteArray &data);
void zipDirectory(const QString &source, const QString &destination,
                  const Progress &progress);

// Preserve unknown scene data and formatting while changing path tokens only.
QByteArray rewritePaths(
    const QByteArray &data,
    const std::function<QString(const QString &, const QString &)> &replace);

}  // namespace ProductionArchive
