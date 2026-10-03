#pragma once

#include <QDir>
#include <QString>

namespace TestEnvironment {
inline QString &root() {
  static QString value;
  return value;
}
inline QString &user() {
  static QString value;
  return value;
}
inline QString under(const QString &relative) {
  return QDir(root()).filePath(relative);
}
}  // namespace TestEnvironment

class TFilePath {
  QString m_path;

public:
  TFilePath() = default;
  explicit TFilePath(const QString &path) : m_path(path) {}
  bool isEmpty() const { return m_path.isEmpty(); }
  QString getQString() const { return m_path; }
};
