#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QIODevice>
#include <QString>

#include <limits>

// Reads the stored, UTF-8 ZIP entries produced by SimpleZipWriter. No
// extraction path is ever derived directly from an entry in the archive.
class SimpleZipReader {
public:
  struct Entry {
    QString name;
    quint32 size   = 0;
    quint32 crc    = 0;
    quint32 offset = 0;
    bool directory = false;
  };

private:
  QFile m_file;
  QHash<QString, Entry> m_entries;
  QString m_error;

  static quint16 u16(const QByteArray &b, int p) {
    return quint16(quint8(b[p])) | (quint16(quint8(b[p + 1])) << 8);
  }
  static quint32 u32(const QByteArray &b, int p) {
    return quint32(u16(b, p)) | (quint32(u16(b, p + 2)) << 16);
  }
  static quint32 crc32(quint32 crc, const QByteArray &data) {
    crc = ~crc;
    for (unsigned char byte : data) {
      crc ^= byte;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
  }
  bool readAt(qint64 offset, qint64 size, QByteArray &out) {
    if (offset < 0 || size < 0 || offset > m_file.size() - size ||
        !m_file.seek(offset))
      return false;
    out = m_file.read(size);
    return out.size() == size;
  }
  static bool safeName(const QString &name) {
    if (name.isEmpty() || name.startsWith('/') || name.contains('\\') ||
        name.contains(':') || name.contains(QChar(0)))
      return false;
    for (const QString &part : name.split('/'))
      if (part == "." || part == "..") return false;
    return true;
  }

public:
  explicit SimpleZipReader(const QString &path) : m_file(path) {}
  QString errorString() const { return m_error; }
  const QHash<QString, Entry> &entries() const { return m_entries; }

  bool open() {
    if (!m_file.open(QIODevice::ReadOnly)) {
      m_error = m_file.errorString();
      return false;
    }
    const qint64 length = m_file.size();
    if (length < 22 || length > std::numeric_limits<quint32>::max()) {
      m_error = QStringLiteral("Unsupported ZIP size.");
      return false;
    }
    QByteArray tail;
    const qint64 tailSize = qMin<qint64>(length, 65557);
    if (!readAt(length - tailSize, tailSize, tail)) return false;
    int end = -1;
    for (int p = tail.size() - 22; p >= 0; --p) {
      if (u32(tail, p) == 0x06054b50u &&
          p + 22 + u16(tail, p + 20) == tail.size()) {
        end = p;
        break;
      }
    }
    if (end < 0 || u16(tail, end + 4) != 0 || u16(tail, end + 6) != 0 ||
        u16(tail, end + 8) != u16(tail, end + 10)) {
      m_error = QStringLiteral("Invalid ZIP directory.");
      return false;
    }
    const quint32 count = u16(tail, end + 10);
    const quint32 bytes = u32(tail, end + 12);
    const quint32 start = u32(tail, end + 16);
    if (count == 0xffff || bytes == 0xffffffffu || start == 0xffffffffu ||
        quint64(start) + bytes != quint64(length - tailSize + end) ||
        bytes > 32 * 1024 * 1024) {
      m_error = QStringLiteral("Unsupported or invalid ZIP directory.");
      return false;
    }
    QByteArray directory;
    if (!readAt(start, bytes, directory)) return false;
    int p         = 0;
    quint64 total = 0;
    for (quint32 i = 0; i < count; ++i) {
      if (p + 46 > directory.size() || u32(directory, p) != 0x02014b50u) break;
      const quint16 flags    = u16(directory, p + 8);
      const quint16 method   = u16(directory, p + 10);
      const quint32 size     = u32(directory, p + 24);
      const quint16 nameSize = u16(directory, p + 28);
      const quint16 extra    = u16(directory, p + 30);
      const quint16 comment  = u16(directory, p + 32);
      if (method != 0 || (flags & ~quint16(0x0808)) ||
          u32(directory, p + 20) != size || u16(directory, p + 34) != 0 ||
          p + 46 + nameSize + extra + comment > directory.size())
        break;
      const QByteArray utf8 = directory.mid(p + 46, nameSize);
      const QString name    = QString::fromUtf8(utf8);
      if (name.toUtf8() != utf8 || !safeName(name) || m_entries.contains(name))
        break;
      Entry e;
      e.name      = name;
      e.size      = size;
      e.crc       = u32(directory, p + 16);
      e.offset    = u32(directory, p + 42);
      e.directory = name.endsWith('/');
      QByteArray local;
      if (!readAt(e.offset, 30, local) || u32(local, 0) != 0x04034b50u ||
          u16(local, 6) != flags || u16(local, 8) != method ||
          u16(local, 26) != nameSize ||
          quint64(e.offset) + 30 + nameSize + u16(local, 28) + size > start)
        break;
      // With a data descriptor the local values may be zero. Otherwise they
      // must match the central directory before any bytes are extracted.
      if (flags & 0x0008) {
        if ((u32(local, 14) && u32(local, 14) != e.crc) ||
            (u32(local, 18) && u32(local, 18) != size) ||
            (u32(local, 22) && u32(local, 22) != size))
          break;
      } else if (u32(local, 14) != e.crc || u32(local, 18) != size ||
                 u32(local, 22) != size)
        break;
      QByteArray localName;
      if (!readAt(quint64(e.offset) + 30, nameSize, localName) ||
          localName != utf8)
        break;
      total += size;
      if (total > std::numeric_limits<quint32>::max()) break;
      m_entries.insert(name, e);
      p += 46 + nameSize + extra + comment;
    }
    if (m_entries.size() != int(count) || p != directory.size()) {
      m_entries.clear();
      m_error = QStringLiteral("Invalid, duplicate, or unsupported ZIP entry.");
      return false;
    }
    return true;
  }

  bool extract(const QString &name, QIODevice *output, quint64 maxBytes) {
    if (!m_entries.contains(name) || !output || !output->isWritable())
      return false;
    const Entry e = m_entries.value(name);
    if (e.directory || e.size > maxBytes) return false;
    QByteArray header;
    if (!readAt(e.offset, 30, header)) return false;
    qint64 pos  = quint64(e.offset) + 30 + u16(header, 26) + u16(header, 28);
    quint32 crc = 0;
    quint32 remaining = e.size;
    while (remaining) {
      const int chunk = qMin<quint32>(remaining, 1024 * 1024);
      QByteArray bytes;
      if (!readAt(pos, chunk, bytes) || output->write(bytes) != bytes.size())
        return false;
      crc = crc32(crc, bytes);
      remaining -= chunk;
      pos += chunk;
    }
    if (crc != e.crc) {
      m_error = QStringLiteral("ZIP entry CRC mismatch: %1").arg(name);
      return false;
    }
    return true;
  }
};
