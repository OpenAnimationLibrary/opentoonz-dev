#include "configtransfer.h"

#include "simplezipreader.h"
#include "simplezipwriter.h"

#include "toonz/preferences.h"
#include "toonz/toonzfolders.h"
#include "toonz/mypaintbrushstyle.h"
#include "toonzqt/menubarcommand.h"
#include "tenv.h"
#include "tsystem.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QVersionNumber>

namespace ConfigTransfer {
namespace {

const int kSchema       = 1;
const quint64 kMaxFile  = 512ull * 1024 * 1024;
const quint64 kMaxTotal = 4ull * 1024 * 1024 * 1024 - 64 * 1024 * 1024;
const int kMaxFiles     = 60000;
const int kMaxManifest  = 32 * 1024 * 1024;

QString path(const TFilePath &fp) { return QDir::cleanPath(fp.getQString()); }

QString userName() {
  return QString::fromStdWString(TSystem::getUserName().toStdWString());
}

QString pendingDir() {
  return QDir(path(TEnv::getConfigDir())).filePath("config-restore-transfer");
}

QString hashFile(const QString &name) {
  QFile input(name);
  if (!input.open(QIODevice::ReadOnly)) return QString();
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!input.atEnd()) {
    QByteArray block = input.read(1024 * 1024);
    if (block.isEmpty() && input.error() != QFile::NoError) return QString();
    hash.addData(block);
  }
  return QString::fromLatin1(hash.result().toHex());
}

bool safePart(const QString &part) {
  return !part.isEmpty() && part != "." && part != ".." &&
         !part.contains('/') && !part.contains('\\') && !part.contains(':') &&
         !part.contains(QChar(0));
}

bool safeRelative(const QString &relative) {
  if (relative.isEmpty() || relative.startsWith('/')) return false;
  for (const QString &part : relative.split('/'))
    if (!safePart(part)) return false;
  return true;
}

// Never follow links in a user-selected source tree or destination directory.
bool noLinks(const QString &root, const QString &relative) {
  QString cursor = root;
  if (QFileInfo(cursor).isSymLink()) return false;
  for (const QString &part : relative.split('/')) {
    cursor = QDir(cursor).filePath(part);
    if (QFileInfo(cursor).isSymLink()) return false;
  }
  return true;
}

QString rootPath(const QString &root) {
  if (root == "settings") return path(ToonzFolder::getMyModuleDir());
  if (root == "rooms")
    return QDir(path(ToonzFolder::getProfileFolder()))
        .filePath("layouts/personal");
  if (root == "env")
    return QDir(path(ToonzFolder::getProfileFolder())).filePath("env");
  if (root == "config") return path(TEnv::getConfigDir());
  if (root == "fxs") return path(ToonzFolder::getFxPresetFolder());
  if (root == "library") return path(ToonzFolder::getLibraryFolder());
  if (root == "plugins")
    return QDir(path(TEnv::getStuffDir())).filePath("plugins");
  return QString();
}

QString destination(const QString &root, const QString &relative) {
  const QString base = rootPath(root);
  if (base.isEmpty() || !safeRelative(relative)) return QString();
  QString translated = relative;
  if (root == "env") {
    if (relative != "user.env") return QString();
    translated = userName() + ".env";
  } else if (root == "rooms") {
    const QStringList parts = relative.split('/');
    if (parts.size() < 2 || !safePart(parts.first())) return QString();
    translated =
        parts.first() + "." + userName() + "/" + parts.mid(1).join('/');
  }
  if (!noLinks(base, translated)) return QString();
  return QDir(base).filePath(translated);
}

bool allowedConfig(const QString &relative) {
  static const QStringList files = {"brush_vector.txt",
                                    "brush_toonzraster.txt",
                                    "brush_raster.txt",
                                    "brush_vector_order.txt",
                                    "brush_toonzraster_order.txt",
                                    "brush_raster_order.txt",
                                    "reslist.txt",
                                    "cleanupreslist.txt",
                                    "exportsettings.txt",
                                    "colornames.txt",
                                    "safearea.ini"};
  if (files.contains(relative)) return true;
  for (const QString &dir : {"outputpresets/", "qss/", "fdg/"})
    if (relative.startsWith(dir)) return true;
  return false;
}

bool allowed(const QString &root, const QString &relative) {
  if (!safeRelative(relative)) return false;
  if (root == "config") return allowedConfig(relative);
  if (root == "env") return relative == "user.env";
  if (root == "rooms") {
    const QStringList parts = relative.split('/');
    if (parts.size() < 2 || !safePart(parts.first())) return false;
    const QString file = parts.last();
    return parts.size() == 2 &&
           (file.endsWith(".ini") || file.endsWith(".xml") ||
            file == "layouts.txt" || file == "currentRoom.txt");
  }
  if (root == "settings") {
    return !relative.startsWith("layouts/") &&
           !relative.contains("config-restore-transfer");
  }
  return root == "fxs" || root == "library" || root == "plugins";
}

bool optionalRoot(const QString &root) {
  return root == "library" || root == "plugins";
}

QString versionFor(const QString &file) {
  QFile manifest(QFileInfo(file).dir().filePath("manifest.json"));
  if (!manifest.open(QIODevice::ReadOnly) || manifest.size() > 1024 * 1024)
    return QString();
  const QJsonDocument json = QJsonDocument::fromJson(manifest.readAll());
  return json.isObject() ? json.object().value("version").toString()
                         : QString();
}

struct Source {
  QString root;
  QString relative;
  QString file;
};

void addTree(QList<Source> &sources, const QString &root,
             const QString &sourceDir, const QString &prefix,
             const Options &options) {
  if (!QFileInfo(sourceDir).isDir()) return;
  QDirIterator it(sourceDir, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    const QString file = it.next();
    QString relative =
        QDir(sourceDir).relativeFilePath(file).replace('\\', '/');
    if (!prefix.isEmpty()) relative = prefix + "/" + relative;
    if (!allowed(root, relative) || QFileInfo(file).isSymLink() ||
        !noLinks(sourceDir,
                 QDir(sourceDir).relativeFilePath(file).replace('\\', '/')))
      continue;
    if (!options.recentFiles &&
        (relative == "RecentFiles.ini" || relative == "fliphistory.ini"))
      continue;
    sources.append({root, relative, file});
  }
}

bool collect(QList<Source> &sources, const Options &options) {
  addTree(sources, "settings", rootPath("settings"), "", options);
  addTree(sources, "config", rootPath("config"), "", options);
  addTree(sources, "fxs", rootPath("fxs"), "", options);
  const QString rooms = rootPath("rooms");
  QDir personal(rooms);
  for (const QFileInfo &set :
       personal.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
    const QString suffix = "." + userName();
    if (set.isSymLink() || !set.fileName().endsWith(suffix)) continue;
    const QString name =
        set.fileName().left(set.fileName().size() - suffix.size());
    if (safePart(name))
      addTree(sources, "rooms", set.absoluteFilePath(), name, options);
  }
  const QString env = QDir(rootPath("env")).filePath(userName() + ".env");
  if (QFileInfo(env).isFile() && !QFileInfo(env).isSymLink())
    sources.append({"env", "user.env", env});
  if (options.library)
    addTree(sources, "library", rootPath("library"), "", options);
  if (options.library) {
    // Bring external MyPaint brushes into the portable OpenToonz library path.
    const auto brushDirs = TMyPaintBrushStyle::getBrushesDirs();
    for (const TFilePath &dir : brushDirs) {
      const QString location = path(dir);
      if (location == QDir(rootPath("library")).filePath("mypaint brushes"))
        continue;
      addTree(sources, "library", location, "mypaint brushes", options);
    }
  }
  if (options.plugins)
    addTree(sources, "plugins", rootPath("plugins"), "", options);
  return true;
}

bool manifest(SimpleZipReader &reader, QJsonArray &files, QString &error) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  buffer.open(QIODevice::WriteOnly);
  if (!reader.extract("manifest.json", &buffer, kMaxManifest)) {
    error = QObject::tr("Cannot read the configuration manifest.");
    return false;
  }
  QJsonParseError parse;
  const QJsonDocument json = QJsonDocument::fromJson(bytes, &parse);
  if (parse.error != QJsonParseError::NoError || !json.isObject() ||
      json.object().value("schema").toInt() != kSchema ||
      !json.object().value("files").isArray()) {
    error = QObject::tr("Unsupported or invalid configuration manifest.");
    return false;
  }
  files = json.object().value("files").toArray();
  if (files.size() > kMaxFiles || files.size() + 1 != reader.entries().size()) {
    error = QObject::tr("The archive file list is invalid.");
    return false;
  }
  return true;
}

bool writeJson(const QString &file, const QJsonObject &object) {
  QSaveFile out(file);
  if (!out.open(QIODevice::WriteOnly)) return false;
  out.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  return out.commit();
}

bool validType(const PreferencesItem &item, const QVariant &value) {
  if (item.type == QMetaType::QColor) {
    const QStringList rgba = value.toString().split(' ', Qt::SkipEmptyParts);
    if (rgba.size() != 4) return false;
    for (const QString &component : rgba) {
      bool ok = false;
      int n   = component.toInt(&ok);
      if (!ok || n < 0 || n > 255) return false;
    }
    return true;
  }
  if (item.type == QMetaType::QVariantMap)
    return false;  // Maps may contain paths to external files.
  if (item.type == QMetaType::QSize) {
    if (!value.canConvert(QMetaType::QSize)) return false;
    const QSize size = value.toSize();
    return size.width() > 0 && size.height() > 0 && size.width() <= 4096 &&
           size.height() <= 4096;
  }
  if (!value.canConvert(item.type)) return false;
  if (item.type == QMetaType::Int || item.type == QMetaType::Double) {
    bool ok        = false;
    const double n = value.toDouble(&ok);
    if (!ok || (item.min.isValid() && n < item.min.toDouble())) return false;
    if (item.max.isValid() && item.max.toDouble() >= 0 &&
        n > item.max.toDouble())
      return false;
  }
  return true;
}

// Preferences are merged by registered key, not replaced by a foreign INI.
bool filterPreferences(const QString &sourcePath, const QString &targetPath,
                       const QString &outputPath, QString &error) {
  QSettings source(sourcePath, QSettings::IniFormat);
  QSettings target(targetPath, QSettings::IniFormat);
  QSettings output(outputPath, QSettings::IniFormat);
  for (const QString &key : target.allKeys())
    output.setValue(key, target.value(key));
  const auto &known = Preferences::instance()->m_items;
  for (auto it = known.cbegin(); it != known.cend(); ++it) {
    const PreferencesItem &item = it.value();
    if (!source.contains(item.idString)) continue;
    const QString key = item.idString;
    // Machine-specific paths stay local unless a later UI explicitly remaps
    // them.
    if (key.contains("path", Qt::CaseInsensitive) ||
        key.contains("folder", Qt::CaseInsensitive) ||
        key.contains("root", Qt::CaseInsensitive) ||
        key.contains("directory", Qt::CaseInsensitive))
      continue;
    const QVariant value = source.value(key);
    if (validType(item, value)) output.setValue(key, value);
  }
  output.sync();
  if (source.status() != QSettings::NoError ||
      output.status() != QSettings::NoError) {
    error = QObject::tr("Could not migrate preferences.ini.");
    return false;
  }
  return true;
}

bool filterShortcuts(const QString &sourcePath, const QString &targetPath,
                     const QString &outputPath, QString &error) {
  QSettings source(sourcePath, QSettings::IniFormat);
  QSettings target(targetPath, QSettings::IniFormat);
  QSettings output(outputPath, QSettings::IniFormat);
  for (const QString &key : target.allKeys())
    output.setValue(key, target.value(key));
  source.beginGroup("shortcuts");
  for (const QString &id : source.allKeys()) {
    if (!CommandManager::instance()->getAction(id.toUtf8().constData(), false))
      continue;
    const QString shortcut = source.value(id).toString();
    if (shortcut.isEmpty() || !QKeySequence(shortcut).isEmpty())
      output.setValue("shortcuts/" + id, shortcut);
  }
  source.endGroup();
  output.sync();
  if (source.status() != QSettings::NoError ||
      output.status() != QSettings::NoError) {
    error = QObject::tr("Could not migrate shortcuts.ini.");
    return false;
  }
  return true;
}

bool filterEnvironment(const QString &sourcePath, const QString &targetPath,
                       const QString &outputPath, QString &error) {
  QFile source(sourcePath);
  QFile target(targetPath);
  if (!source.open(QIODevice::ReadOnly)) {
    error = QObject::tr("Cannot read the environment configuration.");
    return false;
  }
  QMap<QString, QString> lines;
  const QRegularExpression entry("^([A-Za-z_][A-Za-z_0-9]*) +\"(.*)\"$");
  if (target.open(QIODevice::ReadOnly)) {
    for (const QByteArray &raw : target.readAll().split('\n')) {
      const QString line = QString::fromUtf8(raw).trimmed();
      const auto match   = entry.match(line);
      if (match.hasMatch()) lines.insert(match.captured(1), line);
    }
  }
  const auto registered = TEnv::getRegisteredVariableNames();
  for (const QByteArray &raw : source.readAll().split('\n')) {
    const QString line = QString::fromUtf8(raw).trimmed();
    const auto match   = entry.match(line);
    if (!match.hasMatch() || !registered.count(match.captured(1).toStdString()))
      continue;
    const QString value = match.captured(2);
    // Machine-specific locations stay in the target environment.
    if (value.contains('/') || value.contains('\\') || value.contains(':'))
      continue;
    lines.insert(match.captured(1), line);
  }
  QSaveFile output(outputPath);
  if (!output.open(QIODevice::WriteOnly)) return false;
  for (const QString &line : lines) {
    const QByteArray bytes = line.toUtf8() + '\n';
    if (output.write(bytes) != bytes.size()) return false;
  }
  if (!output.commit()) {
    error = QObject::tr("Cannot stage the environment configuration.");
    return false;
  }
  return true;
}

bool validRoom(SimpleZipReader &reader, const QString &entry) {
  QTemporaryFile temp;
  if (!temp.open() || !reader.extract(entry, &temp, 4 * 1024 * 1024))
    return false;
  temp.flush();
  temp.close();
  QSettings room(temp.fileName(), QSettings::IniFormat);
  room.beginGroup("room");
  const QString name      = room.value("name").toString();
  const QString hierarchy = room.value("hierarchy").toString();
  const QStringList panes = room.childGroups();
  if (name.isEmpty() || hierarchy.isEmpty() || hierarchy.size() > 8192 ||
      room.status() != QSettings::NoError || panes.size() > 256)
    return false;
  for (int i = 0; i < panes.size(); ++i) {
    room.beginGroup("pane_" + QString::number(i));
    const bool present = !room.value("name").toString().isEmpty();
    room.endGroup();
    if (!present) return false;
  }
  return true;
}

QString preferenceSummary(SimpleZipReader &reader, const QString &entry) {
  QTemporaryFile temp;
  if (!temp.open() || !reader.extract(entry, &temp, 4 * 1024 * 1024))
    return QObject::tr("Preferences could not be inspected.");
  temp.close();
  QSettings source(temp.fileName(), QSettings::IniFormat);
  QSet<QString> known;
  int applicable = 0;
  for (const PreferencesItem &item : Preferences::instance()->m_items) {
    known.insert(item.idString);
    if (source.contains(item.idString) &&
        validType(item, source.value(item.idString)) &&
        !item.idString.contains("path", Qt::CaseInsensitive) &&
        !item.idString.contains("folder", Qt::CaseInsensitive) &&
        !item.idString.contains("root", Qt::CaseInsensitive) &&
        !item.idString.contains("directory", Qt::CaseInsensitive))
      ++applicable;
  }
  int skipped = 0;
  for (const QString &key : source.allKeys())
    if (!known.contains(key)) ++skipped;
  return QObject::tr(
             "%1 recognized values; %2 unknown keys skipped. "
             "Machine paths stay local.")
      .arg(applicable)
      .arg(skipped);
}

}  // namespace

bool save(const QString &archivePath, const Options &options, QString &error) {
  QList<Source> sources;
  collect(sources, options);
  const QString target =
      QDir::cleanPath(QFileInfo(archivePath).absoluteFilePath());
  const QStringList roots = {"settings", "config",  "fxs",    "rooms",
                             "env",      "library", "plugins"};
  for (const QString &root : roots) {
    const QString directory = QDir::cleanPath(rootPath(root));
    if (target.startsWith(directory + "/")) {
      error = QObject::tr(
          "Save the configuration archive outside the captured folders.");
      return false;
    }
  }
  QSet<QString> seen;
  for (auto it = sources.begin(); it != sources.end();) {
    const QString key = it->root + "/" + it->relative;
    if (seen.contains(key))
      it = sources.erase(it);
    else {
      seen.insert(key);
      ++it;
    }
  }
  if (sources.size() > kMaxFiles) {
    error = QObject::tr("Too many configuration files for this ZIP format.");
    return false;
  }
  quint64 total = 0;
  for (const Source &source : sources) {
    const qint64 size = QFileInfo(source.file).size();
    if (size < 0 || quint64(size) > kMaxFile ||
        (total += quint64(size)) > kMaxTotal) {
      error = QObject::tr(
          "The configuration exceeds this ZIP format's size limit.");
      return false;
    }
  }
  QTemporaryDir temp;
  if (!temp.isValid()) return false;
  const QString tempZip = QDir(temp.path()).filePath("backup.zip");
  SimpleZipWriter writer(tempZip);
  if (!writer.isOpen()) {
    error = writer.errorString();
    return false;
  }
  QJsonArray entries;
  for (const Source &source : sources) {
    const QString archiveName = "files/" + source.root + "/" + source.relative;
    const QString digest      = hashFile(source.file);
    if (digest.isEmpty() || !writer.addFile(archiveName, source.file)) {
      error = writer.errorString().isEmpty()
                  ? QObject::tr("Cannot read %1").arg(source.file)
                  : writer.errorString();
      return false;
    }
    QJsonObject entry{{"path", archiveName},
                      {"root", source.root},
                      {"relative", source.relative},
                      {"sha256", digest},
                      {"size", double(QFileInfo(source.file).size())}};
    const QString version = versionFor(source.file);
    if (!version.isEmpty()) entry["version"] = version;
    entries.append(entry);
  }
  const QJsonObject meta{
      {"schema", kSchema},
      {"application", "OpenToonz"},
      {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
      {"platform", QSysInfo::productType()},
      {"architecture", QSysInfo::currentCpuArchitecture()},
      {"files", entries}};
  const QByteArray manifestBytes = QJsonDocument(meta).toJson();
  if (manifestBytes.size() > kMaxManifest) {
    error = QObject::tr("The configuration manifest is too large.");
    return false;
  }
  if (!writer.addData("manifest.json", manifestBytes) || !writer.close()) {
    error = writer.errorString();
    return false;
  }
  QFile source(tempZip);
  QSaveFile output(archivePath);
  if (!source.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) {
    error = QObject::tr("Cannot create the configuration archive.");
    return false;
  }
  bool copied = true;
  while (!source.atEnd()) {
    const QByteArray block = source.read(1024 * 1024);
    if ((block.isEmpty() && source.error() != QFile::NoError) ||
        output.write(block) != block.size()) {
      copied = false;
      break;
    }
  }
  if (!copied || !output.commit()) {
    error = QObject::tr("Could not finish the configuration archive.");
    return false;
  }
  return true;
}

bool inspect(const QString &archivePath, QList<Item> &items, QString &error) {
  SimpleZipReader reader(archivePath);
  if (!reader.open()) {
    error = reader.errorString();
    return false;
  }
  QJsonArray entries;
  if (!manifest(reader, entries, error)) return false;
  QSet<QString> seen;
  // Read the producer platform for optional binary plugin compatibility.
  QByteArray manifestBytes;
  QBuffer manifestBuffer(&manifestBytes);
  manifestBuffer.open(QIODevice::WriteOnly);
  if (!reader.extract("manifest.json", &manifestBuffer, kMaxManifest)) {
    error = QObject::tr("Cannot verify the configuration manifest.");
    return false;
  }
  const QJsonObject producer = QJsonDocument::fromJson(manifestBytes).object();
  const bool samePlatform =
      producer.value("platform").toString() == QSysInfo::productType() &&
      producer.value("architecture").toString() ==
          QSysInfo::currentCpuArchitecture();
  QSet<QString> invalidRoomSets;
  for (const QJsonValue &value : entries) {
    const QJsonObject object = value.toObject();
    if (object.value("root").toString() != "rooms") continue;
    const QString relative = object.value("relative").toString();
    const QString set      = relative.section('/', 0, 0);
    const QString entry    = object.value("path").toString();
    if (relative.endsWith(".ini") && !validRoom(reader, entry))
      invalidRoomSets.insert(set);
    if (relative.endsWith("/layouts.txt")) {
      QByteArray bytes;
      QBuffer buffer(&bytes);
      buffer.open(QIODevice::WriteOnly);
      if (!reader.extract(entry, &buffer, 1024 * 1024)) {
        invalidRoomSets.insert(set);
        continue;
      }
      for (const QByteArray &raw : bytes.split('\n')) {
        const QString room = QString::fromUtf8(raw).trimmed();
        if (!room.isEmpty() &&
            (!safePart(room) || !room.endsWith(".ini") ||
             !reader.entries().contains("files/rooms/" + set + "/" + room)))
          invalidRoomSets.insert(set);
      }
    }
  }
  quint64 total = 0;
  QSet<QString> targets;
  for (const QJsonValue &value : entries) {
    if (!value.isObject()) {
      error = QObject::tr("Invalid file entry.");
      return false;
    }
    const QJsonObject object  = value.toObject();
    const QString root        = object.value("root").toString();
    const QString relative    = object.value("relative").toString();
    const QString archiveName = object.value("path").toString();
    const QString sha         = object.value("sha256").toString();
    if (!allowed(root, relative) ||
        archiveName != "files/" + root + "/" + relative ||
        seen.contains(archiveName) || !reader.entries().contains(archiveName) ||
        reader.entries().value(archiveName).directory || sha.size() != 64 ||
        object.value("size").toDouble(-1) !=
            reader.entries().value(archiveName).size) {
      error = QObject::tr("Invalid configuration entry: %1").arg(archiveName);
      return false;
    }
    seen.insert(archiveName);
    total += reader.entries().value(archiveName).size;
    if (total > kMaxTotal ||
        reader.entries().value(archiveName).size > kMaxFile) {
      error = QObject::tr("The archive exceeds restore limits.");
      return false;
    }
    Item item;
    item.archivePath  = archiveName;
    item.category     = root;
    item.destination  = destination(root, relative);
    item.compatible   = !item.destination.isEmpty();
    QString targetKey = QDir::cleanPath(item.destination);
#ifdef Q_OS_WIN
    targetKey = targetKey.toCaseFolded();
#endif
    if (targets.contains(targetKey)) {
      error = QObject::tr("The archive maps two files to %1.")
                  .arg(item.destination);
      return false;
    }
    targets.insert(targetKey);
    if (root == "rooms" &&
        invalidRoomSets.contains(relative.section('/', 0, 0))) {
      item.compatible = false;
      item.detail =
          QObject::tr("This room set contains an invalid or missing layout.");
    }
    if (root == "plugins") {
      item.compatible = item.compatible && samePlatform;
      item.detail     = samePlatform ? QObject::tr(
                                           "Check this plugin's OpenToonz API "
                                               "compatibility before enabling it.")
                                     : QObject::tr(
                                           "This plugin was backed up on another "
                                               "platform or architecture.");
    }
    item.existing = QFileInfo::exists(item.destination);
    const QString localHash =
        item.existing ? hashFile(item.destination) : QString();
    if (!item.compatible)
      item.status = QObject::tr("Unsupported");
    else if (localHash == sha)
      item.status = QObject::tr("Identical");
    else if (item.existing) {
      const QVersionNumber archived =
          QVersionNumber::fromString(object.value("version").toString());
      const QVersionNumber installed =
          QVersionNumber::fromString(versionFor(item.destination));
      const int order = archived.isNull() || installed.isNull()
                            ? 0
                            : QVersionNumber::compare(archived, installed);
      item.status = order > 0 ? QObject::tr("Archive newer; review replacement")
                    : order < 0 ? QObject::tr("Installed newer; keep installed")
                                : QObject::tr("Different; version unknown");
    } else
      item.status = QObject::tr("Missing");
    item.selected = item.compatible && (!item.existing || !optionalRoot(root));
    if (root == "env") {
      item.selected = false;
      item.detail   = QObject::tr(
            "Environment variables may contain machine paths; review before "
              "transfer.");
    }
    if (relative == "mainwindow.ini" || relative == "popups.ini")
      item.selected = false;  // Screen geometry is machine-specific.
    if (root == "settings" && relative == "preferences.ini") {
      item.selected =
          item.compatible && item.status != QObject::tr("Identical");
      item.detail = preferenceSummary(reader, archiveName);
    }
    items.append(item);
  }
  return true;
}

bool queueRestore(const QString &archivePath, const QList<Item> &items,
                  QString &error) {
  QList<Item> verified;
  if (!inspect(archivePath, verified, error)) return false;
  QHash<QString, Item> choices;
  for (const Item &item : items)
    if (item.selected) choices.insert(item.archivePath, item);
  const QString base = pendingDir();
  if (QFileInfo::exists(base)) {
    error = QObject::tr("A configuration restore is already pending.");
    return false;
  }
  if (!QDir().mkpath(QDir(base).filePath("files"))) {
    error = QObject::tr("Cannot stage the restore.");
    return false;
  }
  SimpleZipReader reader(archivePath);
  if (!reader.open()) {
    error = reader.errorString();
    QDir(base).removeRecursively();
    return false;
  }
  QJsonArray manifests;
  if (!manifest(reader, manifests, error)) {
    QDir(base).removeRecursively();
    return false;
  }
  QHash<QString, QString> digests;
  for (const QJsonValue &v : manifests)
    digests.insert(v.toObject().value("path").toString(),
                   v.toObject().value("sha256").toString());
  QJsonArray selected;
  bool failed = false;
  for (const Item &item : verified) {
    if (!choices.contains(item.archivePath) || !item.compatible ||
        item.status == QObject::tr("Identical"))
      continue;
    const Item choice = choices.value(item.archivePath);
    if (choice.destination != item.destination ||
        (item.existing && !choice.existing)) {
      error = QObject::tr("The restore preview changed. Please reopen it.");
      QDir(base).removeRecursively();
      return false;
    }
    const QString staged = QDir(base).filePath("files/" + item.archivePath);
    if (!QDir().mkpath(QFileInfo(staged).absolutePath())) {
      failed = true;
      break;
    }
    QFile out(staged);
    if (!out.open(QIODevice::WriteOnly) ||
        !reader.extract(item.archivePath, &out, kMaxFile)) {
      failed = true;
      break;
    }
    out.close();
    if (hashFile(staged) != digests.value(item.archivePath)) {
      error =
          QObject::tr("Archive checksum mismatch: %1").arg(item.archivePath);
      failed = true;
      break;
    }
    QJsonObject record{
        {"destination", item.destination},
        {"staged", staged},
        {"root", item.category},
        {"relative", item.archivePath.mid(7 + item.category.size())}};
    if (item.category == "settings" &&
        item.archivePath == "files/settings/preferences.ini") {
      const QString merged = staged + ".merged";
      if (!filterPreferences(staged, item.destination, merged, error)) {
        failed = true;
        break;
      }
      record["staged"] = merged;
    } else if (item.category == "settings" &&
               item.archivePath == "files/settings/shortcuts.ini") {
      const QString merged = staged + ".merged";
      if (!filterShortcuts(staged, item.destination, merged, error)) {
        failed = true;
        break;
      }
      record["staged"] = merged;
    } else if (item.category == "env") {
      const QString merged = staged + ".merged";
      if (!filterEnvironment(staged, item.destination, merged, error)) {
        failed = true;
        break;
      }
      record["staged"] = merged;
    }
    record["sha256"] = hashFile(record.value("staged").toString());
    if (record.value("sha256").toString().isEmpty()) {
      error  = QObject::tr("Cannot verify the staged configuration.");
      failed = true;
      break;
    }
    selected.append(record);
    continue;
  }
  if (failed || (selected.isEmpty() && !choices.isEmpty())) {
    error = error.isEmpty()
                ? QObject::tr("No compatible changes were selected.")
                : error;
  }
  if (!error.isEmpty() ||
      !writeJson(QDir(base).filePath("pending.json"),
                 QJsonObject{{"schema", kSchema}, {"files", selected}})) {
    if (error.isEmpty()) error = QObject::tr("Could not stage the restore.");
    QDir(base).removeRecursively();
    return false;
  }
  return true;
}

bool applyPending(QString &error) {
  const QString base = pendingDir();
  const QString plan = QDir(base).filePath("pending.json");
  if (!QFileInfo::exists(plan)) return true;
  const QString marker = QDir(base).filePath("applying.json");
  if (QFileInfo::exists(marker)) {
    QFile interrupted(marker);
    if (!interrupted.open(QIODevice::ReadOnly)) {
      error = QObject::tr("Cannot read the interrupted restore record.");
      return false;
    }
    const QJsonArray records = QJsonDocument::fromJson(interrupted.readAll())
                                   .object()
                                   .value("files")
                                   .toArray();
    for (const QJsonValue &value : records) {
      const QJsonObject record = value.toObject();
      const QString root       = record.value("root").toString();
      const QString relative   = record.value("relative").toString();
      const QString target     = destination(root, relative);
      const QString backup     = record.value("backup").toString();
      if (target.isEmpty() ||
          target != record.value("destination").toString() ||
          (!backup.isEmpty() && !backup.startsWith(base + "/backup/"))) {
        error = QObject::tr("Cannot safely roll back the interrupted restore.");
        return false;
      }
      if (!backup.isEmpty() && !QFileInfo::exists(backup)) {
        error = QObject::tr("A configuration rollback copy is missing.");
        return false;
      }
      if (QFileInfo::exists(target) && !QFile::remove(target)) {
        error = QObject::tr("Cannot roll back %1.").arg(target);
        return false;
      }
      if (!backup.isEmpty() && !QFile::copy(backup, target)) {
        error = QObject::tr("Cannot restore %1.").arg(target);
        return false;
      }
    }
    QDir().rename(
        base, base + ".interrupted-" +
                  QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmss"));
    error =
        QObject::tr("An interrupted configuration restore was rolled back.");
    return false;
  }
  QFile file(plan);
  if (!file.open(QIODevice::ReadOnly)) {
    error = QObject::tr("Cannot read the pending restore.");
    return false;
  }
  const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
  if (!json.isObject() || json.object().value("schema").toInt() != kSchema) {
    error = QObject::tr("Invalid pending configuration restore.");
    return false;
  }
  const QJsonArray entries = json.object().value("files").toArray();
  if (entries.size() > kMaxFiles) {
    error = QObject::tr("Too many staged restore files.");
    return false;
  }
  QJsonArray records;
  // Validate and back up every destination before touching any of them.
  for (int i = 0; i < entries.size(); ++i) {
    const QJsonObject entry = entries.at(i).toObject();
    const QString root      = entry.value("root").toString();
    const QString relative  = entry.value("relative").toString();
    const QString target    = destination(root, relative);
    const QString staged    = entry.value("staged").toString();
    if (!allowed(root, relative) || target.isEmpty() ||
        target != entry.value("destination").toString() ||
        !staged.startsWith(base + "/") || !QFileInfo(staged).isFile() ||
        QFileInfo(staged).isSymLink() ||
        hashFile(staged) != entry.value("sha256").toString()) {
      error = QObject::tr("Invalid staged configuration path.");
      break;
    }
    if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
      error = QObject::tr("Cannot create the destination folder.");
      break;
    }
    const QString backup = QDir(base).filePath("backup/" + QString::number(i));
    const bool existed   = QFileInfo::exists(target);
    if (existed && (!QDir().mkpath(QFileInfo(backup).absolutePath()) ||
                    !QFile::copy(target, backup))) {
      error = QObject::tr("Cannot back up %1.").arg(target);
      break;
    }
    records.append(QJsonObject{{"root", root},
                               {"relative", relative},
                               {"destination", target},
                               {"staged", staged},
                               {"backup", existed ? backup : QString()}});
  }
  if (error.isEmpty() &&
      !writeJson(marker, QJsonObject{{"schema", kSchema}, {"files", records}}))
    error = QObject::tr("Cannot record the restore transaction.");
  if (!error.isEmpty()) {
    QDir().rename(
        base, base + ".failed-" +
                  QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmss"));
    return false;
  }
  for (const QJsonValue &value : records) {
    const QJsonObject record = value.toObject();
    const QString target     = record.value("destination").toString();
    const QString staged     = record.value("staged").toString();
    QFile input(staged);
    QSaveFile output(target);
    if (!input.open(QIODevice::ReadOnly) ||
        !output.open(QIODevice::WriteOnly)) {
      error = QObject::tr("Cannot install %1.").arg(target);
      break;
    }
    bool copied = true;
    while (!input.atEnd()) {
      const QByteArray block = input.read(1024 * 1024);
      if ((block.isEmpty() && input.error() != QFile::NoError) ||
          output.write(block) != block.size()) {
        copied = false;
        break;
      }
    }
    if (!copied || !output.commit()) {
      error = QObject::tr("Cannot install %1.").arg(target);
      break;
    }
  }
  if (!error.isEmpty()) {
    QString rollbackError;
    for (const QJsonValue &value : records) {
      const QJsonObject record = value.toObject();
      const QString target     = record.value("destination").toString();
      const QString backup     = record.value("backup").toString();
      if ((QFileInfo::exists(target) && !QFile::remove(target)) ||
          (!backup.isEmpty() && !QFile::copy(backup, target)))
        rollbackError = QObject::tr("Rollback failed for %1.").arg(target);
    }
    if (!rollbackError.isEmpty()) {
      error += " " + rollbackError;
      return false;  // Keep the marker and backups for another recovery
                     // attempt.
    }
    QFile::remove(marker);
    QDir().rename(
        base, base + ".failed-" +
                  QDateTime::currentDateTimeUtc().toString("yyyyMMddhhmmss"));
    return false;
  }
  // Even if cleanup fails, an applied transaction must never run again.
  if (!QFile::remove(plan)) {
    error = QObject::tr(
                "Configuration was restored, but its pending record "
                "could not be removed. Remove %1 before relaunching.")
                .arg(plan);
    return false;
  }
  QFile::remove(marker);
  QDir(base).removeRecursively();
  return true;
}

}  // namespace ConfigTransfer
