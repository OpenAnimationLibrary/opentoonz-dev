#include "productionarchive.h"

#include "iocommand.h"
#include "toonzqt/menubarcommand.h"
#include "menubarcommandids.h"
#include "tapp.h"
#include "tenv.h"
#include "tsystem.h"
#include "tpalette.h"
#include "toonz/preferences.h"
#include "toonz/sceneresources.h"
#include "toonz/toonzfolders.h"
#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/tproject.h"
#include "toonz/txshpalettelevel.h"
#include "toonz/txshsimplelevel.h"
#include "toonz/txshsoundlevel.h"

#include <QApplication>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStorageInfo>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

namespace {

using namespace ProductionArchive;

QString tr(const char *text) {
  return QCoreApplication::translate("ExportProductionArchivePopup", text);
}

struct Cancelled {};

struct ExportGuard {
  ExportGuard() {
    TApp::instance()->setSaveInProgress(true);
    qApp->setProperty("productionArchiveBusy", true);
  }
  ~ExportGuard() {
    qApp->setProperty("productionArchiveBusy", false);
    TApp::instance()->setSaveInProgress(false);
  }
};

struct Root {
  QString source, destination;
};

class ArchiveBuilder {
  CopyPlan m_plan;
  QVector<Root> m_roots;
  Progress m_progress;
  QString m_projectDestination;
  QString m_sceneDestination;
  QString m_stuff;
  QString m_activeScene;
  std::shared_ptr<TProject> m_project;
  QHash<QString, QByteArray> m_rewritten;
  QHash<QString, QString> m_palettes;

  QString mapPath(const QString &source) const {
    const QString path = normalizedPath(source);
    const Root *best   = nullptr;
    for (const Root &root : m_roots)
      if (containsPath(root.source, path) &&
          (!best || root.source.size() > best->source.size()))
        best = &root;
    if (!best) return QString();
    QString relative = QDir(best->source).relativeFilePath(path);
    return QDir::cleanPath(best->destination + '/' + relative);
  }

  void addRoot(const QString &source, const QString &destination) {
    m_roots.append({normalizedPath(source), destination});
    m_plan.add(source, destination);
  }

  QString externalDestination(const QString &source) const {
    QString hash = QString::fromLatin1(
        QCryptographicHash::hash(normalizedPath(source).toUtf8(),
                                 QCryptographicHash::Sha256)
            .toHex()
            .left(16));
    return m_projectDestination + "/_external/" + hash + '/' +
           QFileInfo(source).fileName();
  }

  QString collectPath(TFilePath source, bool required) {
    std::string suffix = ResourceImporter::extractPsdSuffix(source);
    QString mapped     = mapPath(source.getQString());
    if (mapped.isEmpty()) mapped = externalDestination(source.getQString());
    if (TSystem::doesExistFileOrLevel(source)) {
      TFilePathSet files;
      if (source.isLevelName()) {
        for (const TFilePath &file :
             TSystem::readDirectory(source.getParentDir(), false, true))
          if (file.getLevelName() == source.getLevelName())
            files.push_back(file);
      } else {
        files.push_back(source);
      }
      TXshSimpleLevel::getFiles(source, files);
      if ((source.getType() == "tzp" || source.getType() == "tzu") &&
          TFileStatus(source.withType("plt")).doesExist())
        files.push_back(source.withType("plt"));
      TFilePath auxiliary =
          source.getParentDir() + (source.getName() + "_files");
      if (QFileInfo(auxiliary.getQString()).isDir()) files.push_back(auxiliary);
      for (const TFilePath &file : files) {
        QString target = QDir(QFileInfo(mapped).path())
                             .filePath(file.withoutParentDir().getQString());
        m_plan.add(file.getQString(), target);
        if (file.getType() == "tpl")
          m_palettes.insert(target, file.getQString());
      }
    } else if (required) {
      throw tr("A scene resource is missing:\n%1").arg(source.getQString());
    }
    TFilePath target(mapped);
    if (!suffix.empty()) target = ResourceImporter::buildPsd(target, suffix);
    return target.getQString();
  }

  class Resources final : public ResourceProcessor {
    ArchiveBuilder &m_builder;
    ToonzScene &m_scene;

  public:
    Resources(ArchiveBuilder &builder, ToonzScene &scene)
        : m_builder(builder), m_scene(scene) {}
    void collect(const TFilePath &path) {
      if (!path.isEmpty())
        m_builder.collectPath(m_scene.decodeFilePath(path), true);
    }
    void process(TXshSimpleLevel *level) override {
      collect(level->getPath());
      collect(level->getScannedPath());
      if (level->getPalette()) collect(level->getPalette()->getRefImgPath());
    }
    void process(TXshSoundLevel *level) override { collect(level->getPath()); }
    void process(TXshPaletteLevel *level) override {
      collect(level->getPath());
    }
  };

  QByteArray read(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
      throw tr("Cannot read:\n%1\n%2").arg(path, file.errorString());
    QByteArray data = file.readAll();
    if (file.error() != QFile::NoError) throw file.errorString();
    return data;
  }

  void prepareScene(const QString &source, const QString &destination,
                    ToonzScene *active) {
    m_progress(tr("Collecting scene resources: %1").arg(source), 0, 0);
    ToonzScene loaded;
    ToonzScene *scene = active;
    if (!scene) {
      loaded.loadNoResources(TFilePath(source));
      if (loaded.getScenePath() != TFilePath(source))
        throw tr("Cannot load the archived scene:\n%1").arg(source);
      scene = &loaded;
    }
    Resources collector(*this, *scene);
    SceneResources resources(scene, scene->getXsheet());
    resources.accept(&collector);
    const QString projectRoot =
        scene->getProject()->getProjectFolder().getQString();
    const QString portableProject = mapPath(projectRoot);
    m_rewritten.insert(
        destination, rewritePaths(read(source), [&](const QString &value,
                                                    const QString &tag) {
          TFilePath path(value);
          bool resource =
              tag == "path" || tag == "scannedPath" || tag == "refImgPath";
          if (value.isEmpty() ||
              (!resource && !path.isAbsolute() && !value.startsWith('+') &&
               !value.startsWith("$scenefolder/")))
            return value;
          TFilePath decoded = scene->decodeFilePath(path);
          QString mapped    = mapPath(decoded.getQString());
          if (TSystem::doesExistFileOrLevel(decoded))
            mapped = collectPath(decoded, true);
          else if (resource && tag != "path")
            throw tr("A scene resource is missing:\n%1")
                .arg(decoded.getQString());
          else if (mapped.isEmpty())
            return value;
          if (portableProject.isEmpty())
            throw tr("Cannot locate the exported project for:\n%1").arg(source);
          return QDir(portableProject).relativeFilePath(mapped);
        }));
  }

public:
  ArchiveBuilder(const Progress &progress, ToonzScene *scene)
      : m_plan(progress)
      , m_progress(progress)
      , m_stuff(normalizedPath(TEnv::getStuffDir().getQString()))
      , m_activeScene(normalizedPath(scene->getScenePath().getQString()))
      , m_project(scene->getProject()) {}

  void prepare(const QString &program, const QString &stage,
               const QString &final, bool includeProgram, bool includeStuff,
               bool includeProject, ToonzScene *scene) {
    m_plan = dataPlanWithProgram(program, stage, final, includeProgram);
    if (includeStuff) addRoot(m_stuff, "portablestuff");
    if (!includeProject) return;
    const QString projectSource = m_project->getProjectFolder().getQString();
    m_projectDestination        = mapPath(projectSource);
    if (m_projectDestination.isEmpty()) {
      m_projectDestination = "portablestuff/projects/" +
                             safeName(m_project->getName().getQString());
      if (includeStuff &&
          QFileInfo(
              QDir(m_stuff).filePath(
                  "projects/" + safeName(m_project->getName().getQString())))
              .exists())
        m_projectDestination +=
            "_" + QString::fromLatin1(
                      QCryptographicHash::hash(projectSource.toUtf8(),
                                               QCryptographicHash::Sha256)
                          .toHex()
                          .left(8));
      addRoot(projectSource, m_projectDestination);
    }
    for (int i = 0; i < m_project->getFolderCount(); ++i) {
      QString folder = m_project->getFolder(i).getQString();
      if (folder.isEmpty()) continue;
      QString source = QDir::isAbsolutePath(folder)
                           ? folder
                           : QDir(projectSource).filePath(folder);
      int variable   = source.indexOf('$');
      if (variable >= 0)
        source = source.left(source.lastIndexOf('/', variable));
      if (mapPath(source).isEmpty() && QFileInfo(source).isDir())
        addRoot(source, externalDestination(source));
    }
    m_sceneDestination = mapPath(m_activeScene);
    if (m_sceneDestination.isEmpty())
      m_sceneDestination = m_projectDestination + "/scenes/" +
                           QFileInfo(m_activeScene).fileName();
    m_plan.add(m_activeScene, m_sceneDestination);
    prepareScene(m_activeScene, m_sceneDestination, scene);
    // Process the project's other scenes without loading their drawings into
    // memory.
    QVector<Entry> entries = m_plan.entries();
    for (const Entry &entry : entries)
      if (!entry.directory && entry.source != m_activeScene &&
          entry.source.endsWith(".tnz", Qt::CaseInsensitive) &&
          containsPath(m_projectDestination, entry.destination))
        prepareScene(entry.source, entry.destination, nullptr);
    QSet<QString> processed;
    while (processed.size() < m_palettes.size()) {
      const auto palettes = m_palettes;
      for (auto it = palettes.cbegin(); it != palettes.cend(); ++it) {
        if (processed.contains(it.key())) continue;
        processed.insert(it.key());
        m_rewritten.insert(
            it.key(), rewritePaths(read(it.value()), [&](const QString &value,
                                                         const QString &tag) {
              if (tag != "refImgPath" || value.isEmpty()) return value;
              QString mapped =
                  collectPath(scene->decodeFilePath(TFilePath(value)), true);
              return QDir(m_projectDestination).relativeFilePath(mapped);
            }));
      }
    }
  }

  CopyPlan dataPlanWithProgram(const QString &program, const QString &stage,
                               const QString &final, bool includeProgram) {
    CopyPlan plan(m_progress);
    plan.exclude(stage);
    plan.exclude(final);
    plan.exclude(final + ".zip");
    plan.exclude(final + ".zip.partial");
    if (includeProgram) {
      if (QFileInfo(program).isFile()) {
        plan.add(program, QFileInfo(program).fileName());
      } else {
        QString prefix;
#ifdef MACOSX
        prefix = QFileInfo(program).fileName() + '/';
#endif
        for (const QFileInfo &file : QDir(program).entryInfoList(
                 QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden |
                 QDir::System)) {
          if (file.fileName().compare("portablestuff", Qt::CaseInsensitive) ==
              0)
            continue;
          plan.add(file.absoluteFilePath(), prefix + file.fileName());
        }
      }
    }
    return plan;
  }

  qint64 size() const { return m_plan.size(); }
  QString sceneDestination() const { return m_sceneDestination; }

  void copy(const QString &stage) {
    m_plan.copy(stage);
    if (m_projectDestination.isEmpty()) return;
    const QString projectSource = m_project->getProjectFolder().getQString();
    auto project                = std::make_shared<TProject>();
    project->load(m_project->getProjectPath());
    for (int i = 0; i < project->getFolderCount(); ++i) {
      QString folder = project->getFolder(i).getQString();
      if (folder.isEmpty()) continue;
      QString source = QDir::isAbsolutePath(folder)
                           ? folder
                           : QDir(projectSource).filePath(folder);
      QString mapped = mapPath(source);
      if (mapped.isEmpty()) mapped = externalDestination(source);
      project->setFolder(
          project->getFolderName(i),
          TFilePath(QDir(m_projectDestination).relativeFilePath(mapped)));
    }
    QString projectFile = QDir(stage).filePath(
        m_projectDestination + '/' +
        m_project->getProjectPath().withoutParentDir().getQString());
    if (!project->save(TFilePath(projectFile)))
      throw tr("Cannot save the exported project settings.");
    for (auto it = m_rewritten.cbegin(); it != m_rewritten.cend(); ++it)
      writeFile(QDir(stage).filePath(it.key()), it.value());
    // A scene saved outside the project's configured scene folder needs its own
    // link.
    QString sceneFolder =
        QFileInfo(QDir(stage).filePath(m_sceneDestination)).path();
    QString relativeProject =
        QDir(sceneFolder).relativeFilePath(QFileInfo(projectFile).path());
    relativeProject.replace("\\", "\\\\").replace("\"", "\\\"");
    writeFile(QDir(sceneFolder).filePath("scenes.xml"),
              ("<parentProject type=\"projectFolder\">\"" + relativeProject +
               "\"</parentProject>\n")
                  .toUtf8());
  }
};

QString programPath() {
#ifdef MACOSX
  QDir bundle(QCoreApplication::applicationDirPath());
  bundle.cdUp();
  bundle.cdUp();
  return bundle.absolutePath();
#elif defined(_WIN32)
  return QCoreApplication::applicationDirPath();
#else
  return QString::fromLocal8Bit(qgetenv("APPIMAGE"));
#endif
}

class ExportProductionArchivePopup final : public QDialog {
  Q_DECLARE_TR_FUNCTIONS(ExportProductionArchivePopup)
  QSettings m_settings;
  QLineEdit *m_destination;
  QLabel *m_preview;
  QCheckBox *m_program, *m_stuff, *m_project, *m_zip;
  QString m_defaultDestination, m_name, m_programPath;

  QString destination() const {
    QString value = m_destination->text().trimmed();
    return normalizedPath(value.isEmpty() ? m_defaultDestination : value);
  }
  void remember() {
    m_settings.setValue("productionArchiveDestination",
                        m_destination->text().trimmed());
    m_settings.setValue("productionArchiveProgram", m_program->isChecked());
    m_settings.setValue("productionArchiveStuff", m_stuff->isChecked());
    m_settings.setValue("productionArchiveProject", m_project->isChecked());
    m_settings.setValue("productionArchiveZip", m_zip->isChecked());
    m_settings.sync();
  }
  void exportArchive() {
    remember();
    if (!m_program->isChecked() && !m_stuff->isChecked() &&
        !m_project->isChecked()) {
      QMessageBox::warning(this, tr("Full Production Archive"),
                           tr("Select content to export."));
      return;
    }
    if (m_program->isChecked() &&
        (m_programPath.isEmpty() || !QFileInfo(m_programPath).exists())) {
      QMessageBox::warning(
          this, tr("Full Production Archive"),
          tr("Program export requires a Windows installation, a macOS app "
             "bundle, or a Linux AppImage.\nUncheck program files to export "
             "data from this build."));
      return;
    }
    const QString parent = destination();
    QString name         = m_name;
    QString final        = QDir(parent).filePath(name);
    int sequence         = 2;
    while (QFileInfo::exists(final) || QFileInfo::exists(final + ".partial") ||
           QFileInfo::exists(final + ".zip") ||
           QFileInfo::exists(final + ".zip.partial")) {
      name  = m_name + '_' + QString::number(sequence++);
      final = QDir(parent).filePath(name);
    }
    const QString stage = final + ".partial";
    bool complete = false, created = false;
    QProgressDialog progress(this);
    progress.setWindowTitle(tr("Full Production Archive"));
    progress.setCancelButtonText(tr("Cancel"));
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    QElapsedTimer elapsed;
    elapsed.start();
    Progress update = [&](const QString &label, qint64 done, qint64 total) {
      if (elapsed.elapsed() >= 80 || total == 0) {
        progress.setLabelText(total > 0 ? tr("%1\n%2 / %3 MiB")
                                              .arg(label)
                                              .arg(done / (1024 * 1024))
                                              .arg(total / (1024 * 1024))
                                        : label);
        progress.setRange(0, total > 0 ? 1000 : 0);
        if (total > 0) progress.setValue(int(1000.0 * done / total));
        QApplication::processEvents();
        elapsed.restart();
      }
      if (progress.wasCanceled()) throw Cancelled();
    };
    ExportGuard guard;
    try {
      QElapsedTimer finishRecording;
      finishRecording.start();
      while (qApp->property("productionArchiveRecorderPending").toBool()) {
        update(tr("Finishing the current session recording..."), 0, 0);
        if (finishRecording.elapsed() > 10000)
          throw tr(
              "The session recording could not be finalized. Stop recording "
              "and retry the export.");
        QThread::msleep(10);
      }
      if (!QDir().mkpath(parent))
        throw tr("Cannot create destination:\n%1").arg(parent);
      if (!QDir(parent).mkdir(QFileInfo(stage).fileName()))
        throw tr("Cannot create export folder:\n%1").arg(stage);
      created = true;
      writeFile(QDir(stage).filePath("archive-incomplete.txt"),
                "This production archive did not finish. Do not use it as a "
                "complete backup.\n");
      ToonzScene *scene = TApp::instance()->getCurrentScene()->getScene();
      ArchiveBuilder builder(update, scene);
      builder.prepare(m_programPath, stage, final, m_program->isChecked(),
                      m_stuff->isChecked(), m_project->isChecked(), scene);
      QStorageInfo storage(parent);
      if (storage.isValid() && storage.bytesAvailable() >= 0 &&
          builder.size() > storage.bytesAvailable())
        throw tr("There is not enough free space for the export.");
      builder.copy(stage);
      QJsonObject manifest{
          {"format", "OpenToonz Production Archive"},
          {"version", 1},
          {"created", QDateTime::currentDateTime().toString(Qt::ISODate)},
          {"build", QString::fromStdString(TEnv::getApplicationFullName())},
          {"scene", builder.sceneDestination()},
          {"program", m_program->isChecked()},
          {"stuff", m_stuff->isChecked()},
          {"project", m_project->isChecked()}};
      writeFile(QDir(stage).filePath("production-archive.json"),
                QJsonDocument(manifest).toJson());
      update(tr("Finishing archive..."), builder.size(), builder.size());
      if (!QFile::remove(QDir(stage).filePath("archive-incomplete.txt")) ||
          !QDir(parent).rename(QFileInfo(stage).fileName(), name))
        throw tr("Cannot finalize the archive folder:\n%1").arg(stage);
      complete = true;
      if (m_zip->isChecked()) {
        zipDirectory(final, final + ".zip.partial", update);
        if (!QFile::rename(final + ".zip.partial", final + ".zip"))
          throw tr("Cannot finalize the ZIP archive.");
      }
      progress.hide();
      QMessageBox message(QMessageBox::Information,
                          tr("Full Production Archive"),
                          tr("Production archive created:\n%1")
                              .arg(QDir::toNativeSeparators(final)),
                          QMessageBox::Ok, this);
      QPushButton *open =
          message.addButton(tr("Open Folder"), QMessageBox::ActionRole);
      message.exec();
      if (message.clickedButton() == open)
        QDesktopServices::openUrl(QUrl::fromLocalFile(final));
      accept();
    } catch (const Cancelled &) {
      progress.hide();
      QMessageBox::information(
          this, tr("Full Production Archive"),
          complete ? tr("ZIP creation was cancelled. The completed archive "
                        "folder is available at:\n%1")
                         .arg(final)
                   : tr("Export cancelled. The incomplete folder is at:\n%1")
                         .arg(stage));
    } catch (const QString &error) {
      progress.hide();
      QMessageBox::warning(
          this, tr("Full Production Archive"),
          error + "\n\n" +
              (complete
                   ? tr("The completed archive folder is available at:\n%1")
                         .arg(final)
               : created ? tr("The incomplete export is at:\n%1").arg(stage)
                         : QString()));
    } catch (const TException &error) {
      progress.hide();
      QMessageBox::warning(
          this, tr("Full Production Archive"),
          QString::fromStdWString(error.getMessage()) + "\n" + stage);
    } catch (const std::exception &error) {
      progress.hide();
      QMessageBox::warning(this, tr("Full Production Archive"),
                           QString::fromUtf8(error.what()) + "\n" + stage);
    }
  }

public:
  explicit ExportProductionArchivePopup(ToonzScene *scene)
      : QDialog(TApp::instance()->getMainWindow())
      , m_settings(
            (ToonzFolder::getMyModuleDir() + "preferences.ini").getQString(),
            QSettings::IniFormat)
      , m_defaultDestination(
            scene->getProject()->getProjectFolder().getQString())
      , m_name(safeName(scene->getProject()->getName().getQString()) + '_' +
               safeName(QString::fromStdWString(scene->getSceneName())) + '_' +
               QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))
      , m_programPath(programPath()) {
    setWindowTitle(tr("Full Production Archive"));
    setMinimumWidth(660);
    auto *layout         = new QVBoxLayout(this);
    auto *form           = new QFormLayout;
    auto *destinationRow = new QHBoxLayout;
    m_destination        = new QLineEdit(
               m_settings.value("productionArchiveDestination").toString());
    m_destination->setPlaceholderText(
        QDir::toNativeSeparators(m_defaultDestination));
    auto *browse = new QPushButton(tr("Browse..."));
    destinationRow->addWidget(m_destination);
    destinationRow->addWidget(browse);
    form->addRow(tr("Destination:"), destinationRow);
    auto *program = new QLabel(QDir::toNativeSeparators(m_programPath));
    auto *stuff =
        new QLabel(QDir::toNativeSeparators(TEnv::getStuffDir().getQString()));
    program->setTextInteractionFlags(Qt::TextSelectableByMouse);
    stuff->setTextInteractionFlags(Qt::TextSelectableByMouse);
    program->setWordWrap(true);
    stuff->setWordWrap(true);
    form->addRow(tr("Program:"), program);
    form->addRow(tr("Stuff:"), stuff);
    m_preview = new QLabel;
    m_preview->setWordWrap(true);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(tr("Export folder:"), m_preview);
    layout->addLayout(form);
    m_program = new QCheckBox(tr("Include OpenToonz program files"));
    m_stuff   = new QCheckBox(tr("Include the entire active stuff directory"));
    m_project = new QCheckBox(
        tr("Include the current project and external scene resources"));
    m_zip = new QCheckBox(
        tr("Also create a ZIP archive (keep the exported folder)"));
    m_program->setChecked(
        m_settings.value("productionArchiveProgram", true).toBool());
    m_stuff->setChecked(
        m_settings.value("productionArchiveStuff", true).toBool());
    m_project->setChecked(
        m_settings.value("productionArchiveProject", true).toBool());
    m_zip->setChecked(m_settings.value("productionArchiveZip", false).toBool());
    for (QCheckBox *check : {m_program, m_stuff, m_project, m_zip})
      layout->addWidget(check);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *exportButton =
        buttons->addButton(tr("Export"), QDialogButtonBox::AcceptRole);
    exportButton->setDefault(true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(exportButton, &QPushButton::clicked, this,
            [this] { exportArchive(); });
    auto preview = [this] {
      m_preview->setText(
          QDir::toNativeSeparators(QDir(destination()).filePath(m_name)));
    };
    connect(m_destination, &QLineEdit::textChanged, this, preview);
    connect(browse, &QPushButton::clicked, this, [this] {
      QString path = QFileDialog::getExistingDirectory(
          this, tr("Archive Destination"), destination());
      if (!path.isEmpty())
        m_destination->setText(QDir::toNativeSeparators(path));
    });
    preview();
  }
  ~ExportProductionArchivePopup() override { remember(); }
};

class ExportProductionArchiveCommand final : public MenuItemHandler {
public:
  ExportProductionArchiveCommand()
      : MenuItemHandler(MI_ExportProductionArchive) {}
  void execute() override {
    TApp *app = TApp::instance();
    if (app->isSaveInProgress()) return;
    ToonzScene *scene = app->getCurrentScene()->getScene();
    if (!scene) return;
    QStringList dirty;
    {
      SceneResources resources(scene, scene->getXsheet());
      resources.getDirtyResources(dirty);
    }
    if (app->getCurrentScene()->getDirtyFlag() || !dirty.isEmpty() ||
        scene->isUntitled()) {
      QMessageBox prompt(QMessageBox::Question, tr("Full Production Archive"),
                         tr("Save the current scene and all modified resources "
                            "before exporting?"),
                         QMessageBox::Cancel, app->getMainWindow());
      QPushButton *save = prompt.addButton(tr("Save All and Continue"),
                                           QMessageBox::AcceptRole);
      prompt.setDefaultButton(save);
      prompt.exec();
      if (prompt.clickedButton() != save) return;
    }
    if (!IoCmd::saveAll()) return;
    scene = app->getCurrentScene()->getScene();
    dirty.clear();
    {
      SceneResources resources(scene, scene->getXsheet());
      resources.getDirtyResources(dirty);
    }
    if (scene->isUntitled() || app->getCurrentScene()->getDirtyFlag() ||
        !dirty.isEmpty()) {
      QMessageBox::warning(
          app->getMainWindow(), tr("Full Production Archive"),
          tr("The scene or its resources could not be fully saved."));
      return;
    }
    ExportProductionArchivePopup popup(scene);
    popup.exec();
  }
} exportProductionArchiveCommand;

}  // namespace
