from pathlib import Path

cpp_path = Path("toonz/sources/toonz/exportscenepopup.cpp")
cpp = cpp_path.read_text(encoding="utf-8")

cpp = cpp.replace(
    "#include <QStandardPaths>\n",
    "#include <QStandardPaths>\n#include <QCheckBox>\n#include <QDirIterator>\n#include <QFileInfo>\n\n#include \"simplezipwriter.h\"\n",
    1,
)

namespace_marker = """  }\n}\n}  // namespace\n//------------------------------------------------------------------------\n"""
helper = """  }\n}\n\nbool zipDirectory(const TFilePath &sourceFolder, const TFilePath &archivePath,\n                  QString &errorMessage) {\n  QDir sourceDir(sourceFolder.getQString());\n  if (!sourceDir.exists()) {\n    errorMessage = QObject::tr(\"The exported scene folder does not exist.\");\n    return false;\n  }\n\n  SimpleZipWriter writer(archivePath.getQString());\n  if (!writer.isOpen()) {\n    errorMessage = writer.errorString();\n    return false;\n  }\n\n  const QString rootName = QFileInfo(sourceDir.absolutePath()).fileName();\n  if (!writer.addDirectory(rootName)) {\n    errorMessage = writer.errorString();\n    return false;\n  }\n\n  QDirIterator iterator(sourceDir.absolutePath(),\n                        QDir::AllEntries | QDir::NoDotAndDotDot |\n                            QDir::Hidden | QDir::System,\n                        QDirIterator::Subdirectories);\n  while (iterator.hasNext()) {\n    iterator.next();\n    const QFileInfo info = iterator.fileInfo();\n    const QString relativePath =\n        sourceDir.relativeFilePath(info.absoluteFilePath());\n    const QString archiveEntry = rootName + \"/\" + relativePath;\n\n    bool added = true;\n    if (info.isDir())\n      added = writer.addDirectory(archiveEntry);\n    else if (info.isFile())\n      added = writer.addFile(archiveEntry, info.absoluteFilePath());\n\n    if (!added) {\n      errorMessage = writer.errorString();\n      return false;\n    }\n  }\n\n  if (!writer.close()) {\n    errorMessage = writer.errorString();\n    return false;\n  }\n  return true;\n}\n}  // namespace\n//------------------------------------------------------------------------\n"""
if namespace_marker not in cpp:
    raise SystemExit("namespace marker not found")
cpp = cpp.replace(namespace_marker, helper, 1)

old_ui = """  lonelyProjectLayout->addWidget(m_lonelyModePathFld, 1, 1);\n\n  lonelyProjectWidget->setLayout(lonelyProjectLayout);"""
new_ui = """  lonelyProjectLayout->addWidget(m_lonelyModePathFld, 1, 1);\n\n  m_createZipCheckBox =\n      new QCheckBox(tr(\"Create ZIP archive of exported scene folder\"),\n                    lonelyProjectWidget);\n  m_createZipCheckBox->setToolTip(\n      tr(\"Create a ZIP archive after export while keeping the exported folder.\"));\n  lonelyProjectLayout->addWidget(m_createZipCheckBox, 2, 1);\n\n  lonelyProjectWidget->setLayout(lonelyProjectLayout);"""
if old_ui not in cpp:
    raise SystemExit("standalone UI marker not found")
cpp = cpp.replace(old_ui, new_ui, 1)

old_export = """      int count = collectAssets(scene);\n      scene.save(newScenePath);\n      Preferences::instance()->setValue(PreferencesItemId::pathAliasPriority,\n                                        oldPriority, false);"""
new_export = """      int count = collectAssets(scene);\n      scene.save(newScenePath);\n      Preferences::instance()->setValue(PreferencesItemId::pathAliasPriority,\n                                        oldPriority, false);\n\n      if (m_createZipCheckBox->isChecked()) {\n        TFilePath archivePath(newSceneFolder.getWideString() + L\".zip\");\n        QString zipError;\n        if (!zipDirectory(newSceneFolder, archivePath, zipError)) {\n          DVGui::warning(\n              tr(\"The scene was exported, but the ZIP archive could not be \"\n                 \"created.\\n%1\")\n                  .arg(zipError));\n          success = false;\n        }\n      }"""
if old_export not in cpp:
    raise SystemExit("standalone export marker not found")
cpp = cpp.replace(old_export, new_export, 1)
cpp_path.write_text(cpp, encoding="utf-8")

header_path = Path("toonz/sources/toonz/exportscenepopup.h")
header = header_path.read_text(encoding="utf-8")
header = header.replace(
    "class QRadioButton;\n", "class QRadioButton;\nclass QCheckBox;\n", 1
)
old_member = """  DVGui::FileField *m_lonelyModePathFld;\n\npublic:"""
new_member = """  DVGui::FileField *m_lonelyModePathFld;\n  QCheckBox *m_createZipCheckBox;\n\npublic:"""
if old_member not in header:
    raise SystemExit("header member marker not found")
header = header.replace(old_member, new_member, 1)
header_path.write_text(header, encoding="utf-8")

zip_header = r'''#pragma once

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QVector>

#include <limits>

// Minimal streaming ZIP writer used for non-destructive scene packaging.
// Entries use the ZIP "store" method so no additional compression library is
// required. The resulting archive is a standard ZIP readable by common tools.
class SimpleZipWriter {
  struct Entry {
    QByteArray name;
    quint32 crc = 0;
    quint32 size = 0;
    quint32 offset = 0;
    bool directory = false;
  };

  QFile m_output;
  QVector<Entry> m_entries;
  QString m_error;
  bool m_closed = false;

  static void append16(QByteArray &data, quint16 value) {
    data.append(char(value & 0xff));
    data.append(char((value >> 8) & 0xff));
  }

  static void append32(QByteArray &data, quint32 value) {
    data.append(char(value & 0xff));
    data.append(char((value >> 8) & 0xff));
    data.append(char((value >> 16) & 0xff));
    data.append(char((value >> 24) & 0xff));
  }

  static quint32 updateCrc32(quint32 crc, const QByteArray &data) {
    crc = ~crc;
    for (unsigned char byte : data) {
      crc ^= byte;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  }

  bool writeBytes(const QByteArray &data) {
    if (m_output.write(data) != data.size()) {
      m_error = m_output.errorString();
      return false;
    }
    return true;
  }

  bool beginEntry(Entry &entry) {
    const qint64 offset = m_output.pos();
    if (offset < 0 || offset > std::numeric_limits<quint32>::max()) {
      m_error = QStringLiteral("The ZIP archive exceeds the supported 4 GB size.");
      return false;
    }
    entry.offset = quint32(offset);

    QByteArray header;
    append32(header, 0x04034b50u);
    append16(header, 20);       // version needed
    append16(header, 0x0808);   // UTF-8 name + data descriptor
    append16(header, 0);        // store method
    append16(header, 0);        // time
    append16(header, 0);        // date
    append32(header, 0);        // crc follows in descriptor
    append32(header, 0);        // compressed size
    append32(header, 0);        // uncompressed size
    append16(header, quint16(entry.name.size()));
    append16(header, 0);        // extra length
    header.append(entry.name);
    return writeBytes(header);
  }

  bool finishEntry(const Entry &entry) {
    QByteArray descriptor;
    append32(descriptor, 0x08074b50u);
    append32(descriptor, entry.crc);
    append32(descriptor, entry.size);
    append32(descriptor, entry.size);
    return writeBytes(descriptor);
  }

public:
  explicit SimpleZipWriter(const QString &fileName) : m_output(fileName) {
    if (!m_output.open(QIODevice::WriteOnly | QIODevice::Truncate))
      m_error = m_output.errorString();
  }

  ~SimpleZipWriter() {
    if (m_output.isOpen() && !m_closed) m_output.close();
  }

  bool isOpen() const { return m_output.isOpen(); }
  QString errorString() const { return m_error; }

  bool addDirectory(QString archiveName) {
    if (!archiveName.endsWith('/')) archiveName.append('/');
    Entry entry;
    entry.name = archiveName.toUtf8();
    entry.directory = true;
    if (entry.name.size() > std::numeric_limits<quint16>::max()) {
      m_error = QStringLiteral("A ZIP entry name is too long.");
      return false;
    }
    if (!beginEntry(entry) || !finishEntry(entry)) return false;
    m_entries.append(entry);
    return true;
  }

  bool addFile(const QString &archiveName, const QString &sourceFile) {
    QFile input(sourceFile);
    if (!input.open(QIODevice::ReadOnly)) {
      m_error = input.errorString();
      return false;
    }

    const qint64 fileSize = input.size();
    if (fileSize < 0 || fileSize > std::numeric_limits<quint32>::max()) {
      m_error = QStringLiteral("A file exceeds the supported 4 GB ZIP entry size: %1")
                    .arg(sourceFile);
      return false;
    }

    Entry entry;
    entry.name = archiveName.toUtf8();
    if (entry.name.size() > std::numeric_limits<quint16>::max()) {
      m_error = QStringLiteral("A ZIP entry name is too long.");
      return false;
    }
    if (!beginEntry(entry)) return false;

    while (!input.atEnd()) {
      const QByteArray block = input.read(1024 * 1024);
      if (block.isEmpty() && input.error() != QFile::NoError) {
        m_error = input.errorString();
        return false;
      }
      entry.crc = updateCrc32(entry.crc, block);
      entry.size += quint32(block.size());
      if (!writeBytes(block)) return false;
    }

    if (!finishEntry(entry)) return false;
    m_entries.append(entry);
    return true;
  }

  bool close() {
    if (m_closed) return m_error.isEmpty();
    if (!m_output.isOpen()) return false;

    const qint64 centralOffset64 = m_output.pos();
    if (centralOffset64 < 0 ||
        centralOffset64 > std::numeric_limits<quint32>::max()) {
      m_error = QStringLiteral("The ZIP archive exceeds the supported 4 GB size.");
      return false;
    }
    const quint32 centralOffset = quint32(centralOffset64);

    for (const Entry &entry : m_entries) {
      QByteArray record;
      append32(record, 0x02014b50u);
      append16(record, 20);      // made by
      append16(record, 20);      // version needed
      append16(record, 0x0808);  // UTF-8 + descriptor
      append16(record, 0);       // store method
      append16(record, 0);
      append16(record, 0);
      append32(record, entry.crc);
      append32(record, entry.size);
      append32(record, entry.size);
      append16(record, quint16(entry.name.size()));
      append16(record, 0);       // extra
      append16(record, 0);       // comment
      append16(record, 0);       // disk
      append16(record, 0);       // internal attributes
      append32(record, entry.directory ? 0x10u : 0u);
      append32(record, entry.offset);
      record.append(entry.name);
      if (!writeBytes(record)) return false;
    }

    const qint64 centralEnd64 = m_output.pos();
    const qint64 centralSize64 = centralEnd64 - centralOffset64;
    if (m_entries.size() > std::numeric_limits<quint16>::max() ||
        centralSize64 > std::numeric_limits<quint32>::max()) {
      m_error = QStringLiteral("The ZIP archive has too many entries or is too large.");
      return false;
    }

    QByteArray end;
    append32(end, 0x06054b50u);
    append16(end, 0);
    append16(end, 0);
    append16(end, quint16(m_entries.size()));
    append16(end, quint16(m_entries.size()));
    append32(end, quint32(centralSize64));
    append32(end, centralOffset);
    append16(end, 0);
    if (!writeBytes(end)) return false;

    m_output.close();
    m_closed = true;
    return true;
  }
};
'''
Path("toonz/sources/toonz/simplezipwriter.h").write_text(zip_header, encoding="utf-8")
