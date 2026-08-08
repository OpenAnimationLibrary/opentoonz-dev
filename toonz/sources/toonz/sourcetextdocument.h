#pragma once

#ifdef _WIN32

#include "svglevelloader.h"

#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTextCodec>
#include <QTimer>
#include <QXmlStreamReader>

#include <functional>
#include <utility>

namespace SvgSourceEditor {

//=============================================================================
// SourceTextDocument
//-----------------------------------------------------------------------------

class SourceTextDocument final {
public:
  enum class Mode { Untitled, StandaloneFile, SvgLevelSource };
  enum class LineEnding { LF, CRLF, CR };

private:
  struct TextFormat {
    QByteArray codecName = QByteArrayLiteral("UTF-8");
    bool writeBom = false;
    LineEnding lineEnding = LineEnding::CRLF;
  };

  Mode m_mode = Mode::Untitled;
  TFilePath m_path;
  TXshSimpleLevelP m_boundLevel;
  TextFormat m_format;
  QFileSystemWatcher m_watcher;
  QByteArray m_diskHash;
  bool m_modified = false;
  std::function<void(bool)> m_externalChangeCallback;

public:
  SourceTextDocument() {
    QObject::connect(&m_watcher, &QFileSystemWatcher::fileChanged,
                     [this]() { scheduleExternalChangeCheck(); });
  }

  Mode mode() const { return m_mode; }
  TFilePath path() const { return m_path; }
  TXshSimpleLevel *boundLevel() const { return m_boundLevel.getPointer(); }
  bool isModified() const { return m_modified; }
  bool isSvgBound() const {
    return m_mode == Mode::SvgLevelSource && boundLevel();
  }

  QString encodingLabel() const {
    return QString::fromLatin1(m_format.codecName) +
           (m_format.writeBom ? QObject::tr(" with BOM") : QString());
  }

  QString lineEndingLabel() const {
    switch (m_format.lineEnding) {
    case LineEnding::CRLF:
      return QStringLiteral("CRLF");
    case LineEnding::CR:
      return QStringLiteral("CR");
    case LineEnding::LF:
      return QStringLiteral("LF");
    }
    return QString();
  }

  QString displayName() const {
    if (!m_path.isEmpty())
      return QFileInfo(m_path.getQString()).fileName();
    return QObject::tr("Untitled");
  }

  void setModified(bool modified) { m_modified = modified; }

  void setExternalChangeCallback(std::function<void(bool)> callback) {
    m_externalChangeCallback = std::move(callback);
  }

  bool newUntitled(QString &text) {
    clearWatch();
    m_mode = Mode::Untitled;
    m_path = TFilePath();
    m_boundLevel = TXshSimpleLevelP();
    m_format = TextFormat();
    m_diskHash.clear();
    m_modified = false;
    text.clear();
    return true;
  }

  bool openStandalone(const TFilePath &path, QString &text, QString &error) {
    QString loadedText;
    TextFormat loadedFormat;
    QByteArray raw;
    if (!readTextFile(path, loadedText, loadedFormat, raw, error)) return false;

    clearWatch();
    m_mode = Mode::StandaloneFile;
    m_path = path;
    m_boundLevel = TXshSimpleLevelP();
    m_format = loadedFormat;
    m_diskHash = hashBytes(raw);
    m_modified = false;
    text = loadedText;
    watchCurrentPath();
    return true;
  }

  bool bindSvgLevel(TXshSimpleLevel *level, QString &text, QString &error) {
    if (!level || level->getType() != SVG_XSHLEVEL) {
      error = QObject::tr("The current level is not an SVG Level.");
      return false;
    }

    const TFilePath sourcePath = SvgLevel::sourcePathForLevel(level);
    QString loadedText;
    TextFormat loadedFormat;
    QByteArray raw;
    if (!readTextFile(sourcePath, loadedText, loadedFormat, raw, error))
      return false;

    clearWatch();
    m_mode = Mode::SvgLevelSource;
    m_path = sourcePath;
    m_boundLevel = level;
    m_format = loadedFormat;
    m_diskHash = hashBytes(raw);
    m_modified = false;
    text = loadedText;
    watchCurrentPath();
    return true;
  }

  bool reload(QString &text, QString &error) {
    if (m_path.isEmpty()) {
      error = QObject::tr("There is no source file to reload.");
      return false;
    }

    QString loadedText;
    TextFormat loadedFormat;
    QByteArray raw;
    if (!readTextFile(m_path, loadedText, loadedFormat, raw, error))
      return false;

    // Always expose the actual text on disk to the editor. If the SVG is
    // temporarily invalid, the SVG service keeps the previous valid raster
    // visible while the user repairs this source.
    m_format = loadedFormat;
    m_diskHash = hashBytes(raw);
    m_modified = false;
    text = loadedText;
    watchCurrentPath();

    if (isSvgBound() &&
        !SvgLevel::reloadExperimentalLevel(boundLevel(), &error))
      return false;

    return true;
  }

  bool validate(const QString &text, QString &error, int *line = nullptr,
                int *column = nullptr) const {
    if (line) *line = -1;
    if (column) *column = -1;

    const QByteArray encoded = encodeText(text);
    const QString extension =
        QString::fromStdString(m_path.getType()).toLower();

    if (isSvgBound() || extension == QStringLiteral("svg")) {
      return SvgLevel::validateSource(encoded, &error, line, column, nullptr);
    }

    if (extension == QStringLiteral("xml")) {
      QXmlStreamReader reader(encoded);
      while (!reader.atEnd()) reader.readNext();
      if (reader.hasError()) {
        error = reader.errorString();
        if (line) *line = static_cast<int>(reader.lineNumber());
        if (column) *column = static_cast<int>(reader.columnNumber());
        return false;
      }
    }

    error.clear();
    return true;
  }

  bool save(const QString &text, QString &error) {
    if (m_path.isEmpty()) {
      error = QObject::tr("Choose a file name before saving.");
      return false;
    }

    QByteArray encoded;
    if (!prepareSaveBytes(text, encoded, error)) return false;
    if (!writeAtomic(m_path, encoded, error)) return false;

    if (isSvgBound() &&
        !SvgLevel::reloadExperimentalLevel(boundLevel(), &error))
      return false;

    m_diskHash = hashBytes(encoded);
    m_modified = false;
    watchCurrentPath();
    return true;
  }

  bool saveAs(const QString &text, const TFilePath &newPath,
              bool relinkBoundSvg, QString &error) {
    QByteArray encoded;
    if (!prepareSaveBytesForPath(text, newPath, encoded, error)) return false;
    if (!writeAtomic(newPath, encoded, error)) return false;

    if (isSvgBound()) {
      if (!relinkBoundSvg) return true;

      if (!SvgLevel::relinkExperimentalLevel(boundLevel(), newPath, &error))
        return false;

      m_path = SvgLevel::sourcePathForLevel(boundLevel());
    } else {
      m_mode = Mode::StandaloneFile;
      m_path = newPath;
    }

    m_diskHash = hashBytes(encoded);
    m_modified = false;
    watchCurrentPath();
    return true;
  }

private:
  static QByteArray hashBytes(const QByteArray &bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
  }

  void clearWatch() {
    const QStringList watched = m_watcher.files();
    if (!watched.isEmpty()) m_watcher.removePaths(watched);
  }

  void watchCurrentPath() {
    clearWatch();
    if (!m_path.isEmpty() && QFileInfo::exists(m_path.getQString()))
      m_watcher.addPath(m_path.getQString());
  }

  void scheduleExternalChangeCheck() {
    QTimer::singleShot(150, &m_watcher, [this]() {
      if (m_path.isEmpty()) return;

      QFile file(m_path.getQString());
      QByteArray raw;
      if (file.open(QIODevice::ReadOnly)) raw = file.readAll();

      watchCurrentPath();
      if (raw.isEmpty() && QFileInfo(m_path.getQString()).size() > 0) return;

      const QByteArray newHash = hashBytes(raw);
      if (newHash == m_diskHash) return;
      if (m_externalChangeCallback)
        m_externalChangeCallback(m_modified);
    });
  }

  static LineEnding detectLineEnding(const QString &text) {
    if (text.contains(QStringLiteral("\r\n"))) return LineEnding::CRLF;
    if (text.contains(QChar('\r'))) return LineEnding::CR;
    return LineEnding::LF;
  }

  static QByteArray detectCodecName(const QByteArray &raw, bool &writeBom,
                                    int &bomLength) {
    writeBom = false;
    bomLength = 0;

    if (raw.startsWith("\xEF\xBB\xBF")) {
      writeBom = true;
      bomLength = 3;
      return QByteArrayLiteral("UTF-8");
    }
    if (raw.startsWith("\xFF\xFE")) {
      writeBom = true;
      bomLength = 2;
      return QByteArrayLiteral("UTF-16LE");
    }
    if (raw.startsWith("\xFE\xFF")) {
      writeBom = true;
      bomLength = 2;
      return QByteArrayLiteral("UTF-16BE");
    }

    const QByteArray prefix = raw.left(512);
    const QRegularExpression expression(
        QStringLiteral(R"(encoding\s*=\s*["']([^"']+)["'])"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match =
        expression.match(QString::fromLatin1(prefix));
    if (match.hasMatch()) return match.captured(1).toLatin1();

    return QByteArrayLiteral("UTF-8");
  }

  static bool readTextFile(const TFilePath &path, QString &text,
                           TextFormat &format, QByteArray &raw,
                           QString &error) {
    QFile file(path.getQString());
    if (!file.open(QIODevice::ReadOnly)) {
      error = QObject::tr("Unable to open text file:\n%1")
                  .arg(QDir::toNativeSeparators(path.getQString()));
      return false;
    }

    raw = file.readAll();
    file.close();

    bool writeBom = false;
    int bomLength = 0;
    const QByteArray codecName = detectCodecName(raw, writeBom, bomLength);
    QTextCodec *codec = QTextCodec::codecForName(codecName);
    if (!codec) {
      error = QObject::tr("Unsupported text encoding: %1")
                  .arg(QString::fromLatin1(codecName));
      return false;
    }

    const QByteArray body = raw.mid(bomLength);
    if (codecName == QByteArrayLiteral("UTF-8") && body.contains('\0')) {
      error = QObject::tr("The selected file appears to contain binary data.");
      return false;
    }

    text = codec->toUnicode(body);
    format.lineEnding = detectLineEnding(text);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QChar('\r'), QChar('\n'));

    format.codecName = codecName;
    format.writeBom = writeBom;
    error.clear();
    return true;
  }

  QByteArray encodeText(const QString &text) const {
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    normalized.replace(QChar('\r'), QChar('\n'));

    switch (m_format.lineEnding) {
    case LineEnding::CRLF:
      normalized.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
      break;
    case LineEnding::CR:
      normalized.replace(QStringLiteral("\n"), QStringLiteral("\r"));
      break;
    case LineEnding::LF:
      break;
    }

    QTextCodec *codec = QTextCodec::codecForName(m_format.codecName);
    if (!codec) codec = QTextCodec::codecForName("UTF-8");
    QByteArray encoded = codec->fromUnicode(normalized);

    if (m_format.writeBom) {
      if (m_format.codecName.compare("UTF-8", Qt::CaseInsensitive) == 0)
        encoded.prepend("\xEF\xBB\xBF");
      else if (m_format.codecName.compare("UTF-16LE",
                                          Qt::CaseInsensitive) == 0)
        encoded.prepend("\xFF\xFE");
      else if (m_format.codecName.compare("UTF-16BE",
                                          Qt::CaseInsensitive) == 0)
        encoded.prepend("\xFE\xFF");
    }
    return encoded;
  }

  bool prepareSaveBytes(const QString &text, QByteArray &encoded,
                        QString &error) const {
    encoded = encodeText(text);
    int line = -1;
    int column = -1;
    if (!validate(text, error, &line, &column)) {
      if (line >= 0)
        error = QObject::tr("Line %1, column %2: %3")
                    .arg(line)
                    .arg(column)
                    .arg(error);
      return false;
    }
    return true;
  }

  bool prepareSaveBytesForPath(const QString &text, const TFilePath &path,
                               QByteArray &encoded, QString &error) const {
    encoded = encodeText(text);
    const QString extension =
        QString::fromStdString(path.getType()).toLower();

    if (isSvgBound() || extension == QStringLiteral("svg")) {
      int line = -1;
      int column = -1;
      if (!SvgLevel::validateSource(encoded, &error, &line, &column, nullptr)) {
        if (line >= 0)
          error = QObject::tr("Line %1, column %2: %3")
                      .arg(line)
                      .arg(column)
                      .arg(error);
        return false;
      }
    } else if (extension == QStringLiteral("xml")) {
      QXmlStreamReader reader(encoded);
      while (!reader.atEnd()) reader.readNext();
      if (reader.hasError()) {
        error = QObject::tr("Line %1, column %2: %3")
                    .arg(reader.lineNumber())
                    .arg(reader.columnNumber())
                    .arg(reader.errorString());
        return false;
      }
    }

    return true;
  }

  static bool writeAtomic(const TFilePath &path, const QByteArray &bytes,
                          QString &error) {
    QSaveFile file(path.getQString());
    if (!file.open(QIODevice::WriteOnly)) {
      error = QObject::tr("Unable to write text file:\n%1")
                  .arg(QDir::toNativeSeparators(path.getQString()));
      return false;
    }

    if (file.write(bytes) != bytes.size()) {
      file.cancelWriting();
      error = QObject::tr("The complete text document could not be written.");
      return false;
    }

    if (!file.commit()) {
      error = QObject::tr("Unable to replace the destination file safely.");
      return false;
    }

    error.clear();
    return true;
  }
};


}  // namespace SvgSourceEditor

#endif  // _WIN32
