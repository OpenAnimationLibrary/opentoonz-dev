#include "../simplezipreader.h"
#include "../simplezipwriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void writeArchive(const QString &file, const QString &name,
                  const QByteArray &payload) {
  SimpleZipWriter writer(file);
  check(writer.isOpen(), "cannot create fixture ZIP");
  check(writer.addData(name, payload), "cannot add fixture ZIP entry");
  check(writer.close(), "cannot close fixture ZIP");
}

QByteArray readFile(const QString &file) {
  QFile input(file);
  check(input.open(QIODevice::ReadOnly), "cannot read fixture ZIP");
  return input.readAll();
}

void writeFile(const QString &file, const QByteArray &bytes) {
  QFile output(file);
  check(output.open(QIODevice::WriteOnly | QIODevice::Truncate),
        "cannot modify fixture ZIP");
  check(output.write(bytes) == bytes.size(), "incomplete fixture ZIP write");
}

void testRoundTrip(const QString &file) {
  const QByteArray payload("payload-for-config-test");
  writeArchive(file, "files/settings/preferences.ini", payload);
  SimpleZipReader reader(file);
  check(reader.open(), "stored ZIP reader rejected its writer's output");
  QByteArray result;
  QBuffer buffer(&result);
  check(buffer.open(QIODevice::WriteOnly), "cannot open output buffer");
  check(!reader.extract("files/settings/preferences.ini", &buffer,
                        payload.size() - 1),
        "entry size limit was ignored");
  check(
      reader.extract("files/settings/preferences.ini", &buffer, payload.size()),
      "cannot extract stored entry");
  check(result == payload, "stored entry changed during extraction");
}

void testCorruption(const QString &file) {
  const QByteArray payload("payload-for-config-test");
  writeArchive(file, "files/settings/preferences.ini", payload);
  QByteArray bytes = readFile(file);
  const int pos    = bytes.indexOf(payload);
  check(pos >= 0, "fixture payload missing");
  bytes[pos] = char(bytes.at(pos) ^ 1);
  writeFile(file, bytes);
  SimpleZipReader reader(file);
  check(reader.open(), "cannot inspect corrupted entry metadata");
  QByteArray result;
  QBuffer buffer(&result);
  buffer.open(QIODevice::WriteOnly);
  check(!reader.extract("files/settings/preferences.ini", &buffer,
                        payload.size()),
        "CRC mismatch was accepted");
}

void testHeaders(const QString &file) {
  writeArchive(file, "files/test.txt", "content");
  QByteArray bytes  = readFile(file);
  const int central = bytes.indexOf(QByteArray::fromHex("504b0102"));
  check(central > 0, "central ZIP record missing");

  QByteArray method    = bytes;
  method[8]            = char(8);
  method[central + 10] = char(8);
  writeFile(file, method);
  {
    SimpleZipReader unsupported(file);
    check(!unsupported.open(), "compressed ZIP method was accepted");
  }

  QByteArray mismatched = bytes;
  mismatched[30]        = 'x';
  writeFile(file, mismatched);
  {
    SimpleZipReader wrongName(file);
    check(!wrongName.open(), "local and central names disagreed");
  }

  QByteArray ascii   = bytes;
  ascii[6]           = char(8);
  ascii[7]           = char(0);
  ascii[central + 8] = char(8);
  ascii[central + 9] = char(0);
  writeFile(file, ascii);
  SimpleZipReader validAscii(file);
  check(validAscii.open(), "valid ASCII name without UTF-8 flag was rejected");
}

void testNames(const QString &file) {
  writeArchive(file, "../outside.txt", "bad");
  {
    SimpleZipReader traversal(file);
    check(!traversal.open(), "traversal entry was accepted");
  }

  SimpleZipWriter writer(file);
  check(writer.isOpen() && writer.addData("duplicate.txt", "one") &&
            writer.addData("duplicate.txt", "two") && writer.close(),
        "cannot create duplicate entry fixture");
  SimpleZipReader duplicate(file);
  check(!duplicate.open(), "duplicate ZIP name was accepted");
}

}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    QTemporaryDir temp;
    check(temp.isValid(), "cannot create temporary directory");
    const QString file = QDir(temp.path()).filePath("test.zip");
    testRoundTrip(file);
    testCorruption(file);
    testHeaders(file);
    testNames(file);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
