#pragma once

#ifdef _WIN32

#include "floatingpanelcommand.h"
#include "svglevelloader.h"
#include "tapp.h"

#include "toonz/tscenehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"
#include "toonzqt/icongenerator.h"
#include "toonzqt/menubarcommand.h"

#include <QAction>
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSaveFile>
#include <QSize>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>

namespace SvgSourceEditor {

constexpr const char *kSourceEditorCommandId = "MI_OpenSourceEditor";
constexpr const char *kSourcePropertiesPanelType = "SvgSourceProperties";

inline QByteArray sourceHash(const TFilePath &path) {
  QFile file(path.getQString());
  if (!file.open(QIODevice::ReadOnly)) return QByteArray();
  return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
}

inline QByteArray sourceHash(const QByteArray &source) {
  return QCryptographicHash::hash(source, QCryptographicHash::Sha256);
}

inline QString visualStudioCodeExecutable() {
  const QString pathExe =
      QStandardPaths::findExecutable(QStringLiteral("Code.exe"));
  if (!pathExe.isEmpty()) return pathExe;

  QStringList candidates;
  const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
  const QString programFiles = qEnvironmentVariable("ProgramFiles");
  const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");

  if (!localAppData.isEmpty())
    candidates << QDir(localAppData).filePath(
        QStringLiteral("Programs/Microsoft VS Code/Code.exe"));
  if (!programFiles.isEmpty())
    candidates << QDir(programFiles).filePath(
        QStringLiteral("Microsoft VS Code/Code.exe"));
  if (!programFilesX86.isEmpty())
    candidates << QDir(programFilesX86).filePath(
        QStringLiteral("Microsoft VS Code/Code.exe"));

  for (const QString &candidate : candidates)
    if (QFileInfo::exists(candidate)) return QDir::cleanPath(candidate);

  return QStandardPaths::findExecutable(QStringLiteral("code"));
}

inline bool launchVisualStudioCode(const TFilePath &path, int line = -1,
                                   int column = -1) {
  const QString executable = visualStudioCodeExecutable();
  if (executable.isEmpty()) return false;

  const QString sourcePath = QDir::toNativeSeparators(path.getQString());
  QStringList arguments;
  arguments << QStringLiteral("--reuse-window");

  if (line > 0) {
    arguments << QStringLiteral("--goto");
    QString location =
        sourcePath + QStringLiteral(":") + QString::number(line);
    if (column > 0)
      location += QStringLiteral(":") + QString::number(column);
    arguments << location;
  } else {
    arguments << sourcePath;
  }

  const QString workingDirectory = path.getParentDir().getQString();
  if (!executable.endsWith(QStringLiteral(".cmd"), Qt::CaseInsensitive) &&
      !executable.endsWith(QStringLiteral(".bat"), Qt::CaseInsensitive))
    return QProcess::startDetached(executable, arguments, workingDirectory);

  QString command = QStringLiteral("\"") +
                    QDir::toNativeSeparators(executable) + QStringLiteral("\"");
  for (QString argument : arguments) {
    argument.replace(QStringLiteral("\""), QStringLiteral("\"\""));
    command += QStringLiteral(" \"") + argument + QStringLiteral("\"");
  }

  return QProcess::startDetached(
      QStringLiteral("cmd.exe"),
      QStringList() << QStringLiteral("/d") << QStringLiteral("/s")
                    << QStringLiteral("/c") << command,
      workingDirectory);
}

class SourceEditorObserver {
public:
  virtual ~SourceEditorObserver() = default;
  virtual void onSvgSourceFileChanged(const TFilePath &path,
                                      const QByteArray &source, bool usable,
                                      const QString &message, int line,
                                      int column, const QSize &naturalSize) = 0;
};

class SourceEditorBridge final : public QObject {
  QFileSystemWatcher m_watcher;
  TXshSimpleLevelP m_level;
  TFilePath m_path;
  QByteArray m_diskHash;
  bool m_reloadPending = false;
  SourceEditorObserver *m_observer = nullptr;

public:
  explicit SourceEditorBridge(QObject *parent = nullptr) : QObject(parent) {
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this,
            [this](const QString &) { scheduleReload(); });
    connect(TApp::instance()->getCurrentScene(), &TSceneHandle::sceneSwitched,
            this, [this]() { clearBinding(); });
  }

  static SourceEditorBridge *instance() {
    static SourceEditorBridge *bridge = new SourceEditorBridge(qApp);
    return bridge;
  }

  void setObserver(SourceEditorObserver *observer) { m_observer = observer; }
  void clearObserver(SourceEditorObserver *observer) {
    if (m_observer == observer) m_observer = nullptr;
  }

  void bind(TXshSimpleLevel *level, const TFilePath &path) {
    if (m_level.getPointer() == level && m_path == path) {
      m_diskHash = sourceHash(path);
      watchCurrentPath();
      return;
    }

    clearWatch();
    m_level = level;
    m_path = path;
    m_diskHash = sourceHash(path);
    watchCurrentPath();
  }

  void clearBinding() {
    clearWatch();
    m_level = TXshSimpleLevelP();
    m_path = TFilePath();
    m_diskHash.clear();
    m_reloadPending = false;
  }

  bool editLevelSource(TXshSimpleLevel *level, int line = -1,
                       int column = -1) {
    if (!level || level->getType() != SVG_XSHLEVEL) return false;

    const TFilePath sourcePath = SvgLevel::sourcePathForLevel(level);
    if (sourcePath.isEmpty() || !QFileInfo::exists(sourcePath.getQString())) {
      QMessageBox::warning(
          nullptr, QObject::tr("SVG Level Properties"),
          QObject::tr(
              "The selected SVG Level does not have an accessible source file."));
      return false;
    }

    bind(level, sourcePath);
    if (launchVisualStudioCode(sourcePath, line, column)) return true;

    QMessageBox::warning(
        nullptr, QObject::tr("Visual Studio Code Not Found"),
        QObject::tr(
            "OpenToonz could not find Visual Studio Code. Install VS Code "
            "or make Code.exe available through a standard installation or PATH."));
    return false;
  }

  bool applySource(TXshSimpleLevel *level, const QByteArray &source,
                   QString *error = nullptr, int *line = nullptr,
                   int *column = nullptr, QSize *naturalSize = nullptr) {
    if (error) error->clear();
    if (line) *line = -1;
    if (column) *column = -1;
    if (naturalSize) *naturalSize = QSize();

    if (!level || level->getType() != SVG_XSHLEVEL) {
      if (error) *error = QObject::tr("No SVG Level is selected.");
      return false;
    }

    const TFilePath path = SvgLevel::sourcePathForLevel(level);
    if (path.isEmpty()) {
      if (error) *error = QObject::tr("The SVG Level has no source path.");
      return false;
    }

    QString validationError;
    int validationLine = -1;
    int validationColumn = -1;
    QSize validatedSize;
    if (!SvgLevel::validateSource(source, &validationError, &validationLine,
                                  &validationColumn, &validatedSize)) {
      if (error) *error = validationError;
      if (line) *line = validationLine;
      if (column) *column = validationColumn;
      if (naturalSize) *naturalSize = validatedSize;
      return false;
    }

    QSaveFile file(path.getQString());
    if (!file.open(QIODevice::WriteOnly)) {
      if (error) *error = QObject::tr("Could not open the SVG source for writing.");
      return false;
    }

    if (file.write(source) != source.size()) {
      file.cancelWriting();
      if (error) *error = QObject::tr("Could not write the complete SVG source.");
      return false;
    }

    if (!file.commit()) {
      if (error) *error = QObject::tr("Could not replace the SVG source file.");
      return false;
    }

    bind(level, path);

    QString reloadError;
    if (!SvgLevel::reloadExperimentalLevel(level, &reloadError)) {
      if (error)
        *error = QObject::tr(
                     "The SVG was saved, but the level could not be refreshed.\n\n%1")
                     .arg(reloadError);
      notifyObserver(path, source, false, error ? *error : reloadError, -1, -1,
                     validatedSize);
      return false;
    }

    if (line) *line = validationLine;
    if (column) *column = validationColumn;
    if (naturalSize) *naturalSize = validatedSize;

    notifyObserver(path, source, true, QString(), validationLine,
                   validationColumn, validatedSize);
    notifySvgRefresh(level);
    return true;
  }

private:
  void clearWatch() {
    const QStringList watched = m_watcher.files();
    if (!watched.isEmpty()) m_watcher.removePaths(watched);
  }

  void watchCurrentPath() {
    clearWatch();
    if (!m_path.isEmpty() && QFileInfo::exists(m_path.getQString()))
      m_watcher.addPath(m_path.getQString());
  }

  void scheduleReload() {
    if (m_reloadPending) return;
    m_reloadPending = true;
    QTimer::singleShot(180, this, [this]() {
      m_reloadPending = false;
      reloadFromDisk();
    });
  }

  void reloadFromDisk() {
    if (!m_level || m_path.isEmpty()) return;

    if (!QFileInfo::exists(m_path.getQString())) {
      QTimer::singleShot(300, this, [this]() { watchCurrentPath(); });
      return;
    }

    const QByteArray newHash = sourceHash(m_path);
    watchCurrentPath();
    if (!newHash.isEmpty() && newHash == m_diskHash) return;

    QFile file(m_path.getQString());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QByteArray source = file.readAll();

    QString error;
    int line = -1;
    int column = -1;
    QSize naturalSize;
    if (!SvgLevel::validateSource(source, &error, &line, &column,
                                  &naturalSize)) {
      m_diskHash = newHash;
      notifyObserver(m_path, source, false, error, line, column, naturalSize);
      if (m_observer) return;

      QString message = error;
      if (line > 0)
        message = QObject::tr("Line %1, column %2: %3")
                      .arg(line)
                      .arg(column)
                      .arg(error);

      QMessageBox box(
          QMessageBox::Warning, QObject::tr("SVG Source Not Yet Valid"),
          QObject::tr(
              "The SVG source changed on disk, but OpenToonz kept the "
              "previous valid rendering.\n\n%1")
              .arg(message),
          QMessageBox::NoButton);
      QPushButton *editButton = box.addButton(
          QObject::tr("Open Error in VS Code"), QMessageBox::ActionRole);
      QPushButton *closeButton =
          box.addButton(QObject::tr("Close"), QMessageBox::RejectRole);
      box.setDefaultButton(editButton);
      box.setEscapeButton(closeButton);
      box.exec();

      if (box.clickedButton() == editButton)
        launchVisualStudioCode(m_path, line, column);
      return;
    }

    if (!SvgLevel::reloadExperimentalLevel(m_level.getPointer(), &error)) {
      m_diskHash = newHash;
      notifyObserver(m_path, source, false, error, -1, -1, naturalSize);
      if (!m_observer)
        QMessageBox::warning(
            nullptr, QObject::tr("SVG Source Reload Failed"),
            QObject::tr(
                "The source is valid SVG, but OpenToonz could not refresh the "
                "retained SVG Level.\n\n%1")
                .arg(error));
      return;
    }

    m_diskHash = newHash;
    notifyObserver(m_path, source, true, QString(), line, column, naturalSize);
    notifySvgRefresh(m_level.getPointer());
  }

  void notifyObserver(const TFilePath &path, const QByteArray &source,
                      bool usable, const QString &message, int line,
                      int column, const QSize &naturalSize) {
    if (m_observer)
      m_observer->onSvgSourceFileChanged(path, source, usable, message, line,
                                         column, naturalSize);
  }

  void notifySvgRefresh(TXshSimpleLevel *level) {
    if (!level) return;

    IconGenerator::instance()->invalidate(level, TFrameId(1));
    if (TApp::instance()->getCurrentLevel()->getSimpleLevel() == level)
      TApp::instance()->getCurrentLevel()->notifyLevelChange();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->notifySceneChanged(false);
  }
};

inline void registerSourceEditorCommand() {
  if (CommandManager::instance()->getAction(kSourceEditorCommandId, false))
    return;

  QAction *action =
      new DVAction(QObject::tr("SVG Level Properties..."), qApp);
  QObject::connect(action, &QAction::triggered, qApp, []() {
    OpenFloatingPanel::getOrOpenFloatingPanel(kSourcePropertiesPanelType);
  });

  CommandManager::instance()->define(kSourceEditorCommandId,
                                     MenuWindowsCommandType, "Ctrl+Alt+E",
                                     action, "");
}

}  // namespace SvgSourceEditor

#endif
