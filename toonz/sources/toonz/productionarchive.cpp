#include "productionarchive.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>

#include "zip.h"

#include <limits>

namespace ProductionArchive {
namespace {

QString pathKey(QString path) {
#ifdef _WIN32
  return path.toCaseFolded();
#else
  return path;
#endif
}

void fail(const QString &message, const QString &path) {
  throw QObject::tr("%1\n%2").arg(message, QDir::toNativeSeparators(path));
}

void checkSource(const Entry &entry) {
  QFileInfo info(entry.source);
  if (!info.isFile() || info.size() != entry.size ||
      info.lastModified() != entry.modified)
    fail(QObject::tr("A source file changed during export. Please retry."),
         entry.source);
}

voidpf ZCALLBACK zipOpen(voidpf opaque, const void *, int) { return opaque; }
uLong ZCALLBACK zipRead(voidpf, voidpf stream, void *buffer, uLong size) {
  qint64 n =
      static_cast<QFile *>(stream)->read(static_cast<char *>(buffer), size);
  return n < 0 ? 0 : uLong(n);
}
uLong ZCALLBACK zipWrite(voidpf, voidpf stream, const void *buffer,
                         uLong size) {
  qint64 n = static_cast<QFile *>(stream)->write(
      static_cast<const char *>(buffer), size);
  return n < 0 ? 0 : uLong(n);
}
ZPOS64_T ZCALLBACK zipTell(voidpf, voidpf stream) {
  return static_cast<QFile *>(stream)->pos();
}
long ZCALLBACK zipSeek(voidpf, voidpf stream, ZPOS64_T offset, int origin) {
  QFile *file = static_cast<QFile *>(stream);
  qint64 base = origin == ZLIB_FILEFUNC_SEEK_CUR   ? file->pos()
                : origin == ZLIB_FILEFUNC_SEEK_END ? file->size()
                                                   : 0;
  if (offset > ZPOS64_T(std::numeric_limits<qint64>::max() - base)) return -1;
  return file->seek(base + qint64(offset)) ? 0 : -1;
}
int ZCALLBACK zipClose(voidpf, voidpf) { return 0; }
int ZCALLBACK zipError(voidpf, voidpf stream) {
  return static_cast<QFile *>(stream)->error() != QFile::NoError;
}

}  // namespace

QString normalizedPath(const QString &path) {
  QFileInfo info(QDir::fromNativeSeparators(path));
  QString canonical = info.canonicalFilePath();
  if (!canonical.isEmpty()) return QDir::cleanPath(canonical);
  QString absolute = info.absoluteFilePath();
  QString parent   = info.absolutePath();
  if (parent == absolute) return QDir::cleanPath(absolute);
  return QDir::cleanPath(normalizedPath(parent) + "/" + info.fileName());
}

bool containsPath(const QString &parent, const QString &path) {
  QString p = pathKey(QDir::cleanPath(parent));
  QString s = pathKey(QDir::cleanPath(path));
  return s == p || s.startsWith(p.endsWith('/') ? p : p + '/');
}

QString safeName(QString name) {
  name.replace(QRegularExpression("[\\x00-\\x1f<>:\"/\\\\|?*]"), "_");
  name = name.trimmed().left(80);
  while (name.endsWith('.') || name.endsWith(' ')) name.chop(1);
  if (name.isEmpty()) name = "Untitled";
  if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)",
                         QRegularExpression::CaseInsensitiveOption)
          .match(name)
          .hasMatch())
    name.prepend('_');
  return name;
}

void CopyPlan::exclude(const QString &path) {
  m_excluded.append(normalizedPath(path));
}

void CopyPlan::add(const QString &source, const QString &destination) {
  QString relative = QDir::cleanPath(QDir::fromNativeSeparators(destination));
  if (QDir::isAbsolutePath(relative) || relative == ".." ||
      relative.startsWith("../"))
    fail(QObject::tr("Invalid archive destination."), destination);
  scan(source, relative == "." ? QString() : relative, {});
}

void CopyPlan::scan(const QString &source, const QString &destination,
                    QSet<QString> ancestors) {
  m_progress(QObject::tr("Scanning: %1").arg(source), 0, 0);
  const QString canonical = normalizedPath(source);
  for (const QString &excluded : m_excluded)
    if (containsPath(excluded, canonical)) return;

  QFileInfo info(source);
  if (!info.exists() || (!info.isDir() && !info.isFile()) || !info.isReadable())
    fail(QObject::tr("Cannot read this source."), source);
  if (ancestors.contains(pathKey(canonical)))
    fail(QObject::tr("A folder link creates a loop."), source);

  const QString key = pathKey(destination);
  if (m_destinations.contains(key)) {
    if (m_destinations.value(key) == pathKey(canonical)) return;
    fail(QObject::tr("Two sources would overwrite the same archive entry."),
         destination);
  }
  m_destinations.insert(key, pathKey(canonical));
  m_entries.append({canonical, destination, info.isDir() ? 0 : info.size(),
                    info.lastModified(), info.permissions(), info.isDir()});
  if (!info.isDir()) {
    if (info.size() > std::numeric_limits<qint64>::max() - m_size)
      fail(QObject::tr("The export is too large."), source);
    m_size += info.size();
    return;
  }
  ancestors.insert(pathKey(canonical));
  const QFileInfoList children = QDir(source).entryInfoList(
      QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
      QDir::DirsFirst | QDir::Name);
  for (const QFileInfo &child : children)
    scan(child.absoluteFilePath(),
         destination.isEmpty() ? child.fileName()
                               : destination + '/' + child.fileName(),
         ancestors);
}

void CopyPlan::copy(const QString &root) const {
  qint64 copied = 0;
  for (const Entry &entry : m_entries) {
    const QString target = QDir(root).filePath(entry.destination);
    m_progress(QObject::tr("Copying: %1").arg(entry.source), copied, m_size);
    if (entry.directory) {
      if (!QDir().mkpath(target))
        fail(QObject::tr("Cannot create this folder."), target);
      continue;
    }
    checkSource(entry);
    if (!QDir().mkpath(QFileInfo(target).absolutePath()))
      fail(QObject::tr("Cannot create the destination folder."), target);
    QFile input(entry.source), output(target);
    if (!input.open(QIODevice::ReadOnly))
      fail(input.errorString(), entry.source);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
      fail(output.errorString(), target);
    qint64 written = 0;
    while (!input.atEnd()) {
      QByteArray block = input.read(1024 * 1024);
      if (block.isEmpty() && input.error() != QFile::NoError)
        fail(input.errorString(), entry.source);
      if (output.write(block) != block.size())
        fail(output.errorString(), target);
      copied += block.size();
      written += block.size();
      m_progress(QObject::tr("Copying: %1").arg(entry.source), copied, m_size);
    }
    if (written != entry.size || !output.flush())
      fail(QObject::tr("The file could not be copied completely."), target);
    checkSource(entry);
    output.setFileTime(entry.modified, QFileDevice::FileModificationTime);
    output.setPermissions(entry.permissions | QFile::WriteOwner);
  }
}

void writeFile(const QString &path, const QByteArray &data) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
      !file.commit())
    fail(QObject::tr("Cannot write the archive file: %1")
             .arg(file.errorString()),
         path);
}

void zipDirectory(const QString &source, const QString &destination,
                  const Progress &progress) {
  CopyPlan plan(progress);
  plan.add(source, QFileInfo(source).fileName());
  QFile output(destination);
  if (!output.open(QIODevice::ReadWrite | QIODevice::NewOnly))
    fail(output.errorString(), destination);
  zlib_filefunc64_def io = {};
  io.zopen64_file        = zipOpen;
  io.zread_file          = zipRead;
  io.zwrite_file         = zipWrite;
  io.ztell64_file        = zipTell;
  io.zseek64_file        = zipSeek;
  io.zclose_file         = zipClose;
  io.zerror_file         = zipError;
  io.opaque              = &output;
  zipFile zip = zipOpen2_64(nullptr, APPEND_STATUS_CREATE, nullptr, &io);
  if (!zip) fail(QObject::tr("Cannot start the ZIP archive."), destination);
  try {
    qint64 done = 0;
    for (const Entry &entry : plan.entries()) {
      progress(QObject::tr("Creating ZIP: %1").arg(entry.destination), done,
               plan.size());
      zip_fileinfo meta = {};
      const QDate date  = entry.modified.date();
      const QTime time  = entry.modified.time();
      meta.tmz_date     = {uInt(time.second()),    uInt(time.minute()),
                           uInt(time.hour()),      uInt(date.day()),
                           uInt(date.month() - 1), uInt(qMax(1980, date.year()))};
      quint32 mode      = entry.directory ? 0040755 : 0100644;
      if (entry.permissions & QFile::ExeOwner) mode |= 0111;
      meta.external_fa = (mode << 16) | (entry.directory ? 0x10 : 0);
      QByteArray name =
          (entry.destination + (entry.directory ? "/" : "")).toUtf8();
      if (name.size() > 65535 ||
          zipOpenNewFileInZip4_64(zip, name.constData(), &meta, nullptr, 0,
                                  nullptr, 0, nullptr, Z_DEFLATED,
                                  Z_DEFAULT_COMPRESSION, 0, -MAX_WBITS,
                                  DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY, nullptr, 0,
                                  (3 << 8) | 45, 0x0800, 1) != ZIP_OK)
        fail(QObject::tr("Cannot add this ZIP entry."), entry.source);
      if (!entry.directory) {
        checkSource(entry);
        QFile input(entry.source);
        if (!input.open(QIODevice::ReadOnly))
          fail(input.errorString(), entry.source);
        while (!input.atEnd()) {
          QByteArray block = input.read(1024 * 1024);
          if (block.isEmpty() && input.error() != QFile::NoError)
            fail(input.errorString(), entry.source);
          if (zipWriteInFileInZip(zip, block.constData(), block.size()) !=
              ZIP_OK)
            fail(QObject::tr("Cannot write the ZIP archive."), destination);
          done += block.size();
          progress(QObject::tr("Creating ZIP: %1").arg(entry.destination), done,
                   plan.size());
        }
        checkSource(entry);
      }
      if (zipCloseFileInZip(zip) != ZIP_OK)
        fail(QObject::tr("Cannot finish this ZIP entry."), entry.source);
    }
    int result = ::zipClose(zip, nullptr);
    zip        = nullptr;
    if (result != ZIP_OK || !output.flush())
      fail(QObject::tr("Cannot finish the ZIP archive."), destination);
  } catch (...) {
    if (zip) ::zipClose(zip, nullptr);
    output.close();
    output.remove();
    throw;
  }
}

QByteArray rewritePaths(
    const QByteArray &data,
    const std::function<QString(const QString &, const QString &)> &replace) {
  if (!data.trimmed().startsWith('<'))
    throw QObject::tr(
        "A scene or palette uses an unsupported compressed format.");
  QByteArray result;
  QString tag;
  bool inTag = false;
  for (int i = 0; i < data.size();) {
    const char c = data[i];
    if (c == '<') {
      inTag   = true;
      int end = i + 1;
      while (end < data.size() && data[end] != '>' && data[end] != ' ' &&
             data[end] != '\r' && data[end] != '\n')
        ++end;
      tag = QString::fromUtf8(data.mid(i + 1, end - i - 1));
    } else if (c == '>') {
      inTag = false;
    }
    if (c != '"' && c != '\'') {
      result.append(c);
      ++i;
      continue;
    }
    const int start = i++;
    QByteArray token;
    while (i < data.size() && data[i] != c) {
      if (data[i] == '\\' && i + 1 < data.size()) ++i;
      token.append(data[i++]);
    }
    if (i == data.size())
      throw QObject::tr("An archive path token is incomplete.");
    ++i;
    QString value       = QString::fromUtf8(token);
    QString replacement = inTag ? value : replace(value, tag);
    if (replacement == value) {
      result.append(data.mid(start, i - start));
    } else {
      QByteArray escaped = replacement.toUtf8();
      escaped.replace("\\", "\\\\");
      escaped.replace(QByteArray(1, c), QByteArray("\\") + c);
      result.append(c).append(escaped).append(c);
    }
  }
  return result;
}

}  // namespace ProductionArchive
