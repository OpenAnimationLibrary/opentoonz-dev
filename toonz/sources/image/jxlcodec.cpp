#include "jxlcodec.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTemporaryDir>
#include <QtEndian>

#include <climits>

namespace JxlCodec {
namespace {

Result fail(Result result, const QString &message, QString *error) {
  if (error) *error = message;
  return result;
}

QString format(QString value) {
  value = value.toLower();
  if (value.startsWith('.')) value.remove(0, 1);
  return value == "jpeg" ? QStringLiteral("jpg") : value;
}

bool candidate(const QString &value, bool output) {
  const QStringList formats = {"jxl", "png", "ppm", "pgm",
                               "pnm", "pam", "pgx", "jpg"};
  return formats.contains(value) || (!output && value == "gif");
}

struct ProcessResult {
  bool ok = false;
  QByteArray out, err;
};

ProcessResult run(const QString &program, const QStringList &arguments,
                  const QString &directory, int timeoutSeconds) {
  ProcessResult result;
  QProcess process;
  process.setWorkingDirectory(directory);
  QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
  environment.insert("LC_ALL", "C");
  process.setProcessEnvironment(environment);
  QElapsedTimer timer;
  timer.start();
  const qint64 timeout = qint64(qBound(1, timeoutSeconds, 86400)) * 1000;
  process.start(program, arguments, QIODevice::ReadOnly);
  if (!process.waitForStarted(int(qMin<qint64>(timeout, 5000)))) {
    result.err = ("Cannot start " + QFileInfo(program).fileName() + ": " +
                  process.errorString())
                     .toUtf8();
    if (process.state() != QProcess::NotRunning) {
      process.kill();
      process.waitForFinished(5000);
    }
    return result;
  }
  // Drain both pipes while waiting. Untrusted filenames and metadata can occur
  // in diagnostic output; cap it instead of allowing unbounded accumulation.
  while (true) {
    process.waitForFinished(50);
    result.out += process.readAllStandardOutput();
    result.err += process.readAllStandardError();
    if (result.out.size() + result.err.size() > 256 * 1024 ||
        timer.elapsed() > timeout) {
      const bool timedOut = timer.elapsed() > timeout;
      process.kill();
      process.waitForFinished(5000);
      result.out.clear();
      result.err = QFileInfo(program).fileName().toUtf8() +
                   (timedOut ? " timed out." : " produced too much output.");
      return result;
    }
    if (process.state() == QProcess::NotRunning) break;
  }
  result.ok =
      process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
  if (!result.ok && result.err.isEmpty())
    result.err = QFileInfo(program).fileName().toUtf8() + " failed.";
  return result;
}

QString diagnostic(const ProcessResult &result) {
  return QString::fromLocal8Bit(result.err.right(1024)).simplified();
}

struct Tools {
  QString cjxl, djxl, jxlinfo;
  QSet<QString> reads, writes;
  bool reconstruct = false;
};

QSet<QString> codecList(const QByteArray &help, const QString &prefix) {
  const QRegularExpression expression("^\\s*" + prefix + "([^\\r\\n]+)",
                                      QRegularExpression::MultilineOption);
  const auto match = expression.match(QString::fromLocal8Bit(help));
  QSet<QString> codecs;
  for (QString item : match.captured(1).split(',')) {
    item = item.section('(', 0, 0).trimmed();
    if (!item.isEmpty()) codecs.insert(format(item));
  }
  if (codecs.contains("pnm")) codecs.insert("pgm");
  return codecs;
}

bool getTools(const Settings &settings, Tools *tools, QString *error) {
  QString path = settings.toolsPath;
  if (!path.isEmpty() && QDir::isRelativePath(path))
    path = QDir(QCoreApplication::applicationDirPath()).filePath(path);
  if (path.isEmpty() || !QFileInfo(path).isDir()) {
    fail(Result::Unsupported, "Set the JPEG XL tools folder in Preferences.",
         error);
    return false;
  }
  const QDir directory(QFileInfo(path).absoluteFilePath());
  QString key;
  QStringList executables;
  for (const QString &name :
       {QString("cjxl"), QString("djxl"), QString("jxlinfo")}) {
    QString filename = name;
#ifdef Q_OS_WIN
    filename += ".exe";
#endif
    const QFileInfo file(directory.filePath(filename));
    if (!file.isFile()
#ifndef Q_OS_WIN
        || !file.isExecutable()
#endif
    ) {
      fail(Result::Unsupported,
           "The JPEG XL tools folder must contain " + filename + ".", error);
      return false;
    }
    executables.append(file.absoluteFilePath());
    key += file.absoluteFilePath() + '\n' + QString::number(file.size()) + ':' +
           QString::number(file.lastModified().toMSecsSinceEpoch()) + '\n';
  }
  static QMutex mutex;
  static QHash<QString, Tools> cache;
  {
    QMutexLocker lock(&mutex);
    const auto found = cache.constFind(key);
    if (found != cache.cend()) {
      *tools = found.value();
      return true;
    }
  }
  Tools detected;
  detected.cjxl                   = executables[0];
  detected.djxl                   = executables[1];
  detected.jxlinfo                = executables[2];
  const QStringList helpArguments = {"-v", "-v", "--help"};
  const int timeout               = qMin(10, settings.timeoutSeconds);
  const auto encoder =
      run(detected.cjxl, helpArguments, directory.path(), timeout);
  const auto decoder =
      run(detected.djxl, helpArguments, directory.path(), timeout);
  if (!encoder.ok || !decoder.ok || !encoder.out.contains("--keep_invisible") ||
      !encoder.out.contains("--num_threads") ||
      !decoder.out.contains("--bits_per_sample")) {
    fail(Result::Unsupported,
         "Cannot identify compatible cjxl and djxl tools in the selected "
         "folder.",
         error);
    return false;
  }
  detected.reads       = codecList(encoder.out, "the input can be ");
  detected.writes      = codecList(decoder.out, "The output format can be ");
  detected.reconstruct = decoder.out.contains("--reconstruct_jpeg");
  if (detected.reads.isEmpty() || detected.writes.isEmpty()) {
    fail(Result::Unsupported,
         "Cannot read the JPEG XL tools' format capabilities.", error);
    return false;
  }
  {
    QMutexLocker lock(&mutex);
    if (cache.size() >= 8) cache.clear();
    cache.insert(key, detected);
  }
  *tools = detected;
  return true;
}

bool available(const Tools &tools, const QString &input,
               const QString &output) {
  if (!candidate(input, false) || !candidate(output, true)) return false;
  if (input == "jxl" && output == "jxl") return false;
  return (input == "jxl" || tools.reads.contains(input)) &&
         (output == "jxl" || tools.writes.contains(output)) &&
         (output != "jpg" || tools.reconstruct);
}

bool jxlSignature(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QByteArray signature = file.read(12);
  return signature.startsWith(QByteArray::fromHex("ff0a")) ||
         signature == QByteArray::fromHex("0000000c4a584c200d0a870a");
}

Result inspectLocal(const Settings &settings, const Tools &tools,
                    const QString &directory, const QString &name,
                    ImageInfo *info, QString *error) {
  if (!jxlSignature(QDir(directory).filePath(name)))
    return fail(Result::Failed, "The file does not have a JPEG XL signature.",
                error);
  const auto process =
      run(tools.jxlinfo, {"-v", name}, directory, settings.timeoutSeconds);
  if (!process.ok || !process.err.trimmed().isEmpty())
    return fail(Result::Failed, diagnostic(process), error);
  QString output = QString::fromLocal8Bit(process.out);
  output.replace("\r\n", "\n");  // Windows tools write text-mode stdout.
  const auto unsupported = [&]() {
    return fail(Result::Unsupported,
                "This bridge supports single sRGB images with integer samples "
                "up to 16 bits and optional straight alpha.",
                error);
  };
  // jxlinfo has no JSON mode. Match its fixed, anchored fields conservatively.
  // In particular, reject names before interpreting further text: channel and
  // frame names are user-controlled and may contain newlines.
  const QStringList lines = output.split('\n');
  for (const QString &line : lines) {
    if (line.startsWith("  name:") || line.startsWith("Layer:") ||
        line.startsWith("Frame:") || line.startsWith("Intensity target:") ||
        line.contains("jhgm") ||  // HDR gain-map container box.
        line.contains("float, with exponent_bits_per_sample") ||
        line.startsWith("  upsampled "))
      return unsupported();
  }
  const QRegularExpression summary(
      "^JPEG XL image, ([0-9]+)x([0-9]+), (?:\\(possibly\\) lossless|lossy), "
      "([0-9]+)-bit (RGB|Grayscale)(\\+Alpha)?$",
      QRegularExpression::MultilineOption);
  const auto match = summary.match(output);
  if (!match.hasMatch()) return unsupported();
  bool widthOk = false, heightOk = false;
  const qulonglong width  = match.captured(1).toULongLong(&widthOk);
  const qulonglong height = match.captured(2).toULongLong(&heightOk);
  if (!widthOk || !heightOk || !width || !height || width > INT_MAX ||
      height > INT_MAX || width * height > INT_MAX / 8)
    return unsupported();
  ImageInfo found;
  found.width    = int(width);
  found.height   = int(height);
  found.bits     = match.captured(3).toInt();
  found.alpha    = !match.captured(5).isEmpty();
  const bool rgb = match.captured(4) == "RGB";
  found.channels = (rgb ? 3 : 1) + int(found.alpha);
  if (found.bits < 1 || found.bits > 16 ||
      !lines.contains(
          QString("Number of color channels: %1").arg(rgb ? 3 : 1)) ||
      !lines.contains(
          QString("Number of extra channels: %1").arg(int(found.alpha))) ||
      !lines.contains("Have animation: 0") ||
      !lines.contains("Color space: " + match.captured(4)) ||
      !lines.contains("White point: D65") ||
      (rgb && !lines.contains("Primaries: sRGB")) ||
      !lines.contains("Transfer function: sRGB"))
    return unsupported();
  if (found.alpha) {
    const QRegularExpression alpha(
        "^Extra channel 0:\\n  type: Alpha\\n  bits per sample: ([0-9]+)\\n"
        "  alpha premultiplied: 0 \\(Non-premultiplied\\)$",
        QRegularExpression::MultilineOption);
    const auto alphaMatch = alpha.match(output);
    const int alphaBits   = alphaMatch.captured(1).toInt();
    if (!alphaMatch.hasMatch() || alphaBits < 1 || alphaBits > 16)
      return unsupported();
    found.bits = qMax(found.bits, alphaBits);
  }
  const QRegularExpression orientation("^Orientation: ([1-8]) ",
                                       QRegularExpression::MultilineOption);
  const auto orientationMatch = orientation.match(output);
  if (!orientationMatch.hasMatch()) return unsupported();
  found.orientation = orientationMatch.captured(1).toInt();
  if (found.orientation >= 5)
    qSwap(found.width, found.height);  // djxl applies the stored orientation.
  if (info) *info = found;
  return Result::Success;
}

bool copyInput(const QString &input, const QString &target, QString *error) {
  QFile source(input);
  if (source.copy(target)) return true;
  fail(Result::Failed, "Cannot stage image: " + source.errorString(), error);
  return false;
}

bool singleNetpbm(const QString &path, const ImageInfo &info) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return false;
  const QByteArray header = file.read(64 * 1024);
  int dataStart           = -1;
  if (header.startsWith("P5") || header.startsWith("P6")) {
    int cursor            = 2;
    const auto whitespace = [](char c) {
      return QByteArray(" \t\r\n\v\f").contains(c);
    };
    // Skip width, height, and MAXVAL, including ordinary header comments.
    for (int token = 0; token != 3; ++token) {
      while (cursor < header.size()) {
        if (whitespace(header[cursor]))
          ++cursor;
        else if (header[cursor] == '#') {
          cursor = header.indexOf('\n', cursor);
          if (cursor < 0) return false;
        } else
          break;
      }
      const int start = cursor;
      while (cursor < header.size() && header[cursor] >= '0' &&
             header[cursor] <= '9')
        ++cursor;
      if (cursor == start) return false;
    }
    // libjxl consumes one whitespace byte after MAXVAL, then raw samples.
    if (cursor >= header.size() || !whitespace(header[cursor])) return false;
    dataStart = cursor + 1;
  } else if (header.startsWith("P7")) {
    const QRegularExpression end("^[ \\t]*ENDHDR[ \\t]*\\r?\\n",
                                 QRegularExpression::MultilineOption);
    const auto match = end.match(QString::fromLatin1(header));
    if (match.hasMatch()) dataStart = match.capturedEnd();
  } else if (header.startsWith("PG ")) {
    const int newline = header.indexOf('\n');
    if (newline >= 0) dataStart = newline + 1;
  }
  // cjxl has already validated the header and supplied the sample dimensions.
  // Its PNM decoder permits trailing images; this still-image bridge must not.
  return dataStart >= 0 &&
         file.size() == dataStart + qint64(info.width) * info.height *
                                        info.channels * ((info.bits + 7) / 8);
}

Result publish(const QString &source, const QString &target, QString *error) {
  QFile input(source);
  QSaveFile output(target);
  if (!input.open(QIODevice::ReadOnly) || input.size() == 0 ||
      !output.open(QIODevice::WriteOnly))
    return fail(Result::Failed, "Cannot open the converted image for saving.",
                error);
  while (!input.atEnd()) {
    const QByteArray block = input.read(64 * 1024);
    if (block.isEmpty() || output.write(block) != block.size())
      return fail(Result::Failed, "Cannot save the converted image.", error);
  }
  if (!output.commit())
    return fail(Result::Failed,
                "Cannot commit the converted image: " + output.errorString(),
                error);
  return Result::Success;
}

bool validateOutput(const QString &path, const QString &outputFormat,
                    const ImageInfo &info) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() < 8) return false;
  const QByteArray header = file.peek(8192);
  if (outputFormat == "png") {
    if (header.size() < 33 ||
        !header.startsWith(QByteArray::fromHex("89504e470d0a1a0a")) ||
        header.mid(12, 4) != "IHDR")
      return false;
    const auto *bytes  = reinterpret_cast<const uchar *>(header.constData());
    const int channels = bytes[25] == 0   ? 1
                         : bytes[25] == 2 ? 3
                         : bytes[25] == 4 ? 2
                         : bytes[25] == 6 ? 4
                                          : 0;
    if (qFromBigEndian<quint32>(bytes + 16) != quint32(info.width) ||
        qFromBigEndian<quint32>(bytes + 20) != quint32(info.height) ||
        bytes[24] != (info.bits > 8 ? 16 : 8) || channels != info.channels)
      return false;
    file.seek(8);
    bool pixels = false;
    while (file.pos() + 12 <= file.size()) {
      const QByteArray chunk = file.read(8);
      const quint32 length   = qFromBigEndian<quint32>(
          reinterpret_cast<const uchar *>(chunk.constData()));
      const QByteArray type = chunk.mid(4, 4);
      if (type == "acTL" || length > quint64(file.size() - file.pos() - 4))
        return false;
      if (type == "IDAT") pixels = true;
      if (!file.seek(file.pos() + qint64(length) + 4)) return false;
      if (type == "IEND") {
        if (length != 0 || !pixels || !file.atEnd()) return false;
        // A native PNG reader may display a placeholder after a damaged IDAT.
        // Validate the full stream before publishing it or choosing this route.
        // This image is discarded; the original 8/16-bit PNG supplies pixels.
        QImageReader reader(path, "png");
        reader.setAutoTransform(false);
        const QImage decoded = reader.read();
        return !decoded.isNull() && decoded.width() == info.width &&
               decoded.height() == info.height;
      }
    }
    return false;
  }
  if (outputFormat == "jpg") {
    if (!header.startsWith(QByteArray::fromHex("ffd8ff"))) return false;
    return file.seek(file.size() - 2) &&
           file.read(2) == QByteArray::fromHex("ffd9");
  }
  const QString text = QString::fromLatin1(header);
  QString expected;
  if (outputFormat == "pam") {
    expected = QString(
                   "^P7\\nWIDTH %1\\nHEIGHT %2\\nDEPTH %3\\nMAXVAL %4\\n"
                   "TUPLTYPE [A-Z_]+\\nENDHDR\\n")
                   .arg(info.width)
                   .arg(info.height)
                   .arg(info.channels)
                   .arg((1u << info.bits) - 1);
  } else if (outputFormat == "pgx") {
    expected = QString("^PG (?:ML|LM) \\+ %1 %2 %3\\n")
                   .arg(info.bits)
                   .arg(info.width)
                   .arg(info.height);
  } else {
    expected = QString("^P%1\\n%2 %3\\n%4\\n")
                   .arg(info.channels == 1 ? 5 : 6)
                   .arg(info.width)
                   .arg(info.height)
                   .arg((1u << info.bits) - 1);
  }
  const auto match = QRegularExpression(expected).match(text);
  return match.hasMatch() &&
         file.size() ==
             match.capturedLength() + qint64(info.width) * info.height *
                                          info.channels * ((info.bits + 7) / 8);
}

bool sameFileBytes(const QString &a, const QString &b) {
  QFile first(a), second(b);
  if (!first.open(QIODevice::ReadOnly) || !second.open(QIODevice::ReadOnly) ||
      first.size() != second.size())
    return false;
  while (!first.atEnd()) {
    const QByteArray block = first.read(64 * 1024);
    if (block.isEmpty() || block != second.read(block.size())) return false;
  }
  return true;
}

}  // namespace

bool supports(const Settings &settings, const QString &inputFormat,
              const QString &outputFormat) {
  const QString input = format(inputFormat), output = format(outputFormat);
  if (!candidate(input, false) || !candidate(output, true)) return false;
  Tools tools;
  return getTools(settings, &tools, nullptr) && available(tools, input, output);
}

Result inspect(const Settings &settings, const QString &input, ImageInfo *info,
               QString *error) {
  if (error) error->clear();
  if (info) *info = ImageInfo();
  Tools tools;
  if (!getTools(settings, &tools, error)) return Result::Unsupported;
  QTemporaryDir directory;
  if (!directory.isValid() ||
      !copyInput(input, directory.filePath("input.jxl"), error))
    return fail(Result::Failed, "Cannot stage the JPEG XL image.", error);
  return inspectLocal(settings, tools, directory.path(), "input.jxl", info,
                      error);
}

Result encode(const Settings &settings, const QString &input,
              const QString &stagedOutput, QString *error) {
  if (error) error->clear();
  Tools tools;
  const QString inputFormat = format(QFileInfo(input).suffix());
  if (!getTools(settings, &tools, error)) return Result::Unsupported;
  if (!available(tools, inputFormat, "jxl"))
    return fail(Result::Unsupported, "cjxl cannot read this image format.",
                error);
  QTemporaryDir directory;
  const QString name = "input." + inputFormat;
  if (!directory.isValid() ||
      !copyInput(input, directory.filePath(name), error))
    return fail(Result::Failed, "Cannot stage the image for cjxl.", error);
  const auto result =
      run(tools.cjxl,
          {name, "output.jxl", "--distance=0", "--keep_invisible=1",
           "--container=1", "--num_threads=0",
           "--effort=" + QString::number(qBound(1, settings.effort, 10))},
          directory.path(), settings.timeoutSeconds);
  if (!result.ok) return fail(Result::Failed, diagnostic(result), error);
  ImageInfo info;
  const Result probed = inspectLocal(settings, tools, directory.path(),
                                     "output.jxl", &info, error);
  if (probed != Result::Success) return probed;
  if ((inputFormat == "ppm" || inputFormat == "pgm" || inputFormat == "pnm" ||
       inputFormat == "pam" || inputFormat == "pgx") &&
      !singleNetpbm(directory.filePath(name), info))
    return fail(
        Result::Unsupported,
        "Only one binary Netpbm image without trailing data is supported.",
        error);
  return publish(directory.filePath("output.jxl"), stagedOutput, error);
}

Result decode(const Settings &settings, const QString &input,
              const QString &stagedOutput, const QString &outputFormat,
              ImageInfo *info, QString *error) {
  if (error) error->clear();
  if (info) *info = ImageInfo();
  const QString output = format(outputFormat);
  Tools tools;
  if (!getTools(settings, &tools, error)) return Result::Unsupported;
  if (!available(tools, "jxl", output))
    return fail(Result::Unsupported, "djxl cannot write this image format.",
                error);
  QTemporaryDir directory;
  if (!directory.isValid() ||
      !copyInput(input, directory.filePath("input.jxl"), error))
    return fail(Result::Failed, "Cannot stage the JPEG XL image.", error);
  ImageInfo found;
  const Result probed = inspectLocal(settings, tools, directory.path(),
                                     "input.jxl", &found, error);
  if (probed != Result::Success) return probed;
  if ((found.alpha && output != "png" && output != "pam") ||
      ((output == "pgm" || output == "pgx") && found.channels != 1) ||
      (output == "ppm" && found.channels != 3))
    return fail(
        Result::Unsupported,
        "The requested format cannot preserve these color/alpha channels.",
        error);
  const QString name    = "output." + output;
  QStringList arguments = {"input.jxl", name, "--num_threads=0"};
  if (output == "jpg")
    arguments.append("--reconstruct_jpeg");
  else
    arguments.append(
        "--bits_per_sample=" +
        QString::number(output == "png" ? (found.bits > 8 ? 16 : 8) : 0));
  const auto result =
      run(tools.djxl, arguments, directory.path(), settings.timeoutSeconds);
  if (!result.ok) return fail(Result::Failed, diagnostic(result), error);
  if (!validateOutput(directory.filePath(name), output, found))
    return fail(Result::Failed, "djxl produced an invalid or unexpected image.",
                error);
  const Result saved = publish(directory.filePath(name), stagedOutput, error);
  if (saved == Result::Success && info) *info = found;
  return saved;
}

Result roundTrip(const Settings &settings, const QString &input,
                 const QString &stagedOutput, const QString &outputFormat,
                 ImageInfo *info, QString *error) {
  if (error) error->clear();
  if (info) *info = ImageInfo();
  const QString inputFormat = format(QFileInfo(input).suffix());
  const QString output      = format(outputFormat);
  if (!supports(settings, inputFormat, output))
    return fail(Result::Unsupported,
                "The selected JPEG XL tools do not support this conversion.",
                error);
  QTemporaryDir directory;
  if (!directory.isValid())
    return fail(Result::Failed, "Cannot create a temporary conversion folder.",
                error);
  const QString intermediate = directory.filePath("intermediate.jxl");
  const Result encoded       = encode(settings, input, intermediate, error);
  if (encoded != Result::Success) return encoded;
  ImageInfo found;
  const QString candidate = directory.filePath("candidate." + output);
  const Result decoded =
      decode(settings, intermediate, candidate, output, &found, error);
  if (decoded != Result::Success) return decoded;
  if (found.orientation != 1)
    return fail(Result::Unsupported,
                "This image's orientation requires the native format reader.",
                error);
  if (output == "jpg" &&
      (inputFormat != "jpg" || !sameFileBytes(input, candidate)))
    return fail(Result::Failed,
                "JPEG reconstruction did not preserve the original file.",
                error);
  const Result saved = publish(candidate, stagedOutput, error);
  if (saved == Result::Success && info) *info = found;
  return saved;
}

}  // namespace JxlCodec
