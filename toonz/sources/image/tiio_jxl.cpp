#include "tiio_jxl.h"

#include "jxlcodec.h"
#include "png/tiio_png.h"
#include "texception.h"
#include "tfilepath.h"
#include "tfilepath_io.h"
#include "tpixel.h"
#include "toonz/preferences.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QXmlStreamReader>
#include <QtEndian>

#include <cmath>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace {

using File = std::unique_ptr<FILE, int (*)(FILE *)>;

JxlCodec::Settings settings() {
  Preferences *preferences = Preferences::instance();
  JxlCodec::Settings result;
  result.toolsPath      = preferences->getJxlPath();
  result.timeoutSeconds = preferences->getJxlTimeout();
  return result;
}

bool extraFormat(const QString &ext) {
  return ext == "ppm" || ext == "pgm" || ext == "pnm" || ext == "pam" ||
         ext == "pgx";
}

bool ordinaryFormat(const QString &ext) {
  // Only ordinary raster factories belong here. Colormaps, vectors and level
  // formats have additional semantics which a still-image round trip loses.
  return ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" ||
         ext == "tga" || ext == "tif" || ext == "tiff" || ext == "sgi" ||
         ext == "rgb" || ext == "exr" || extraFormat(ext);
}

void fail(const QString &message) {
  throw TException(QString("JPEG XL: %1").arg(message).toStdString());
}

void fallback(const QString &ext, const QString &reason) {
  qWarning().noquote() << "JPEG XL experimental route:" << ext
                       << "using native codec:" << reason;
}

void requireExperimental() {
  if (!Preferences::instance()->isJxlExperimentalEnabled())
    fail(
        "Enable the experimental JPEG XL route in Preferences > "
        "Decoder/Encoder to "
        "use this format.");
}

File openFile(const QString &path, const char *mode) {
  return File(fopen(TFilePath(path), mode), fclose);
}

void closeFile(File &file) {
  if (file && fclose(file.release()) != 0)
    fail("Cannot finish writing temporary image.");
}

// Copy without disturbing a native reader's starting position. A failed seek
// cannot safely fall back and is consequently reported instead of ignored.
bool stageSource(FILE *source, const QString &path) {
  fpos_t position;
  if (fgetpos(source, &position) != 0) return false;
  QFile destination(path);
  if (!destination.open(QIODevice::WriteOnly)) return false;
  bool ok = true;
  char data[65536];
  size_t size;
  while ((size = fread(data, 1, sizeof(data), source)) != 0) {
    if (destination.write(data, static_cast<qint64>(size)) !=
        static_cast<qint64>(size)) {
      ok = false;
      break;
    }
  }
  ok = ok && !ferror(source) && destination.flush();
  clearerr(source);
  if (fsetpos(source, &position) != 0)
    fail("Cannot restore the source image position.");
  return ok;
}

void copyToFile(const QString &path, FILE *destination) {
  QFile source(path);
  if (!source.open(QIODevice::ReadOnly) || source.size() == 0)
    fail("The converter did not produce an image.");
  while (!source.atEnd()) {
    const QByteArray data = source.read(65536);
    if (data.isEmpty() ||
        fwrite(data.constData(), 1, data.size(), destination) !=
            static_cast<size_t>(data.size()))
      fail("Cannot copy the completed image.");
  }
  if (source.error() != QFile::NoError || fflush(destination) != 0)
    fail("Cannot finish copying the completed image.");
}

bool copyBytes(QIODevice &source, QIODevice &destination, qint64 count) {
  while (count > 0) {
    const QByteArray data = source.read(qMin<qint64>(count, 65536));
    if (data.isEmpty() || destination.write(data) != data.size()) return false;
    count -= data.size();
  }
  return true;
}

const QByteArray &containerSignature() {
  static const QByteArray signature =
      QByteArray::fromHex("0000000c4a584c200d0a870a");
  return signature;
}

struct Box {
  qint64 start = 0;
  qint64 size  = 0;
  int header   = 8;
  QByteArray type;
};

// Only the standard container framing is parsed; the JXL codestream remains
// entirely the responsibility of libjxl. Length checks precede every seek.
bool nextBox(QFile &file, Box &box) {
  box.start               = file.pos();
  const QByteArray header = file.read(8);
  if (header.size() != 8) return false;
  quint64 size = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar *>(header.constData()));
  box.type   = header.mid(4, 4);
  box.header = 8;
  if (size == 1) {
    const QByteArray large = file.read(8);
    if (large.size() != 8) return false;
    size = qFromBigEndian<quint64>(
        reinterpret_cast<const uchar *>(large.constData()));
    box.header = 16;
  } else if (size == 0) {
    size = static_cast<quint64>(file.size() - box.start);
  }
  if (size < static_cast<quint64>(box.header) ||
      size > static_cast<quint64>(file.size() - box.start))
    return false;
  box.size = static_cast<qint64>(size);
  return true;
}

double resolution(const QString &value) {
  const QStringList parts = value.trimmed().split('/');
  if (parts.size() < 1 || parts.size() > 2) return 0;
  bool valid    = false;
  double result = parts[0].toDouble(&valid);
  if (!valid) return 0;
  if (parts.size() == 2) {
    const double divisor = parts[1].toDouble(&valid);
    if (!valid || divisor <= 0) return 0;
    result /= divisor;
  }
  return std::isfinite(result) && result > 0 && result <= 1000000 ? result : 0;
}

bool readDpiXml(const QByteArray &data, double &x, double &y) {
  const QString ns = "http://ns.adobe.com/tiff/1.0/";
  QXmlStreamReader xml(data);
  QString xValue, yValue, unit;
  auto assign = [&](const QString &name, const QString &value) {
    if (name == "XResolution") xValue = value;
    if (name == "YResolution") yValue = value;
    if (name == "ResolutionUnit") unit = value;
  };
  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.tokenType() == QXmlStreamReader::DTD) return false;
    if (!xml.isStartElement()) continue;
    for (const QXmlStreamAttribute &attribute : xml.attributes())
      if (attribute.namespaceUri() == ns)
        assign(attribute.name().toString(), attribute.value().toString());
    if (xml.namespaceUri() == ns) {
      const QString name = xml.name().toString();
      assign(name, xml.readElementText(QXmlStreamReader::SkipChildElements));
    }
  }
  unit = unit.trimmed();
  if (xml.hasError() || (unit != "2" && unit != "3")) return false;
  x = resolution(xValue);
  y = resolution(yValue);
  if (unit == "3") {
    x *= 2.54;
    y *= 2.54;
  }
  return x > 0 && y > 0;
}

void readDpi(const QString &path, TImageInfo &info) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.read(12) != containerSignature())
    return;
  qint64 xmlBudget = 4 * 1024 * 1024;
  for (int count = 0; count < 4096 && file.pos() < file.size(); ++count) {
    Box box;
    if (!nextBox(file, box)) return;
    const qint64 payload = box.size - box.header;
    if (box.type == "xml " && payload <= 1024 * 1024 && payload <= xmlBudget) {
      xmlBudget -= payload;
      double x = 0, y = 0;
      if (readDpiXml(file.read(payload), x, y)) {
        info.m_dpix = x;
        info.m_dpiy = y;
        return;
      }
    }
    if (!file.seek(box.start + box.size)) return;
  }
}

void writeDpi(const QString &path, const TImageInfo &info) {
  if (!std::isfinite(info.m_dpix) || !std::isfinite(info.m_dpiy) ||
      info.m_dpix <= 0 || info.m_dpiy <= 0 || info.m_dpix > 1000000 ||
      info.m_dpiy > 1000000)
    return;
  const QString xml =
      QString(
          "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"><rdf:RDF "
          "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
          "<rdf:Description rdf:about=\"\" "
          "xmlns:tiff=\"http://ns.adobe.com/tiff/1.0/\" "
          "tiff:XResolution=\"%1/1000000\" "
          "tiff:YResolution=\"%2/1000000\" tiff:ResolutionUnit=\"2\"/>"
          "</rdf:RDF></x:xmpmeta>")
          .arg(qRound64(info.m_dpix * 1000000))
          .arg(qRound64(info.m_dpiy * 1000000));
  const QByteArray payload = xml.toUtf8();
  QByteArray xmlBox(8, '\0');
  qToBigEndian<quint32>(static_cast<quint32>(payload.size() + 8),
                        reinterpret_cast<uchar *>(xmlBox.data()));
  xmlBox.replace(4, 4, "xml ");
  xmlBox += payload;

  QFile source(path);
  if (!source.open(QIODevice::ReadOnly) ||
      source.read(12) != containerSignature())
    fail("The encoder did not produce the required JXL container.");
  Box type;
  if (!nextBox(source, type) || type.type != "ftyp" || type.size > 1024 * 1024)
    fail("The encoder produced an invalid JXL file-type box.");
  const qint64 prefix = type.start + type.size;
  source.seek(0);
  QSaveFile output(path);
  output.setDirectWriteFallback(false);
  if (!output.open(QIODevice::WriteOnly) ||
      !copyBytes(source, output, prefix) ||
      output.write(xmlBox) != xmlBox.size() ||
      !copyBytes(source, output, source.size() - prefix))
    fail("Cannot preserve image resolution in the JXL container.");
  source.close();
  if (!output.commit()) fail("Cannot save JXL image resolution.");
}

// Preserve the original PNG resolution chunk after an experimental round trip.
// Copying its existing CRC avoids interpreting or recompressing any pixel data.
bool preservePngDpi(const QString &original, const QString &converted) {
  const QByteArray signature = QByteArray::fromHex("89504e470d0a1a0a");
  QFile source(original);
  if (!source.open(QIODevice::ReadOnly) || source.read(8) != signature)
    return false;
  QByteArray physical;
  for (int count = 0; count < 4096 && source.pos() < source.size(); ++count) {
    const QByteArray header = source.read(8);
    if (header.size() != 8) return false;
    const quint32 size = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(header.constData()));
    if (static_cast<qint64>(size) + 4 > source.size() - source.pos())
      return false;
    if (header.mid(4) == "pHYs" && size == 9) {
      physical = header + source.read(13);
      break;
    }
    if (header.mid(4) == "IDAT" || header.mid(4) == "IEND") break;
    if (!source.seek(source.pos() + size + 4)) return false;
  }
  if (physical.isEmpty()) return true;
  source.close();
  QFile input(converted);
  QSaveFile output(converted);
  output.setDirectWriteFallback(false);
  if (!input.open(QIODevice::ReadOnly) || input.read(8) != signature ||
      !output.open(QIODevice::WriteOnly) || output.write(signature) != 8)
    return false;
  bool inserted = false;
  while (input.pos() < input.size()) {
    const QByteArray header = input.read(8);
    if (header.size() != 8) return false;
    const quint32 size = qFromBigEndian<quint32>(
        reinterpret_cast<const uchar *>(header.constData()));
    if (static_cast<qint64>(size) + 4 > input.size() - input.pos())
      return false;
    if (header.mid(4) == "pHYs") {
      if (!input.seek(input.pos() + size + 4)) return false;
      continue;
    }
    if (output.write(header) != 8 ||
        !copyBytes(input, output, qint64(size) + 4))
      return false;
    if (!inserted && header.mid(4) == "IHDR") {
      if (output.write(physical) != physical.size()) return false;
      inserted = true;
    }
  }
  input.close();
  return inserted && output.commit();
}

class JxlReader final : public Tiio::Reader {
  QString m_ext;
  Tiio::ReaderMaker *m_nativeMaker;
  JxlCodec::Settings m_settings;
  QTemporaryDir m_directory;
  File m_decodedFile;
  std::unique_ptr<Tiio::Reader> m_reader;

public:
  JxlReader(const QString &ext, Tiio::ReaderMaker *native,
            const JxlCodec::Settings &configuration)
      : m_ext(ext)
      , m_nativeMaker(native)
      , m_settings(configuration)
      , m_decodedFile(nullptr, fclose) {}

  void open(FILE *file) override {
    if (extraFormat(m_ext)) requireExperimental();
    const QString source = m_directory.filePath("source." + m_ext);
    const QString png    = m_directory.filePath("decoded.png");
    if (!m_directory.isValid() || !stageSource(file, source)) {
      if (!m_nativeMaker) fail("Cannot stage the source image.");
      fallback(m_ext, "Cannot create a temporary input file.");
      m_reader.reset(m_nativeMaker());
      m_reader->open(file);
      m_info = m_reader->getImageInfo();
      return;
    }

    std::unique_ptr<Tiio::Reader> native;
    std::exception_ptr nativeError;
    TImageInfo nativeInfo;
    if (m_nativeMaker) {
      try {
        native.reset(m_nativeMaker());
        native->open(file);
        nativeInfo = native->getImageInfo();
      } catch (...) {
        native.reset();
        nativeError = std::current_exception();
      }
      if (native && (nativeInfo.m_bitsPerSample > 16 ||
                     nativeInfo.m_samplePerPixel > 4)) {
        fallback(m_ext,
                 "Floating point or additional channels are unsupported.");
        m_reader = std::move(native);
        m_info   = m_reader->getImageInfo();
        return;
      }
    }

    QString error;
    JxlCodec::ImageInfo imageInfo;
    const JxlCodec::Result result =
        m_ext == "jxl" ? JxlCodec::decode(m_settings, source, png, "png",
                                          &imageInfo, &error)
                       : JxlCodec::roundTrip(m_settings, source, png, "png",
                                             &imageInfo, &error);
    if (result == JxlCodec::Result::Success) {
      try {
        m_decodedFile = openFile(png, "rb");
        if (!m_decodedFile) fail("Cannot open the decoded PNG.");
        m_reader.reset(Tiio::makePngReader());  // Leaf factory: never recurse.
        m_reader->open(m_decodedFile.get());
        m_info = m_reader->getImageInfo();
        if (m_info.m_lx != imageInfo.width || m_info.m_ly != imageInfo.height ||
            (m_info.m_bitsPerSample != 8 && m_info.m_bitsPerSample != 16))
          fail("The decoded image has unexpected dimensions or depth.");
        if (native &&
            (nativeInfo.m_lx != m_info.m_lx || nativeInfo.m_ly != m_info.m_ly))
          fail("The converted image changed dimensions.");
        if (m_ext == "jxl") {
          readDpi(source, m_info);
          if (imageInfo.orientation >= 5 && imageInfo.orientation <= 8)
            std::swap(m_info.m_dpix, m_info.m_dpiy);
        }
        if (native) {
          m_info.m_dpix = nativeInfo.m_dpix;
          m_info.m_dpiy = nativeInfo.m_dpiy;
        }
        return;
      } catch (const TException &e) {
        error = QString::fromStdWString(e.getMessage());
        m_reader.reset();
        m_decodedFile.reset();
      }
    }
    if (native) {
      fallback(m_ext, error);
      m_reader = std::move(native);
      m_info   = m_reader->getImageInfo();
      return;
    }
    if (nativeError) std::rethrow_exception(nativeError);
    fail(error.isEmpty() ? QString("The selected tools cannot read this image.")
                         : error);
  }

  TPropertyGroup *getProperties() const override {
    return m_reader ? m_reader->getProperties() : nullptr;
  }
  void readLine(char *line, int x0, int x1, int shrink) override {
    m_reader->readLine(line, x0, x1, shrink);
  }
  void readLine(short *line, int x0, int x1, int shrink) override {
    m_reader->readLine(line, x0, x1, shrink);
  }
  void readLine(float *line, int x0, int x1, int shrink) override {
    m_reader->readLine(line, x0, x1, shrink);
  }
  int skipLines(int count) override { return m_reader->skipLines(count); }
  Tiio::RowOrder getRowOrder() const override {
    return m_reader ? m_reader->getRowOrder() : Tiio::TOP2BOTTOM;
  }
  bool read16BitIsEnabled() const override {
    return m_reader && m_reader->read16BitIsEnabled();
  }
  void enable16BitRead(bool enabled) override {
    if (m_reader) m_reader->enable16BitRead(enabled);
  }
  void setColorSpaceGamma(double gamma) override {
    if (m_reader) m_reader->setColorSpaceGamma(gamma);
  }
};

// PGM is a small, lossless staging format for explicit grayscale export. The
// native PNG writer only emits RGB(A). Netpbm stores 16-bit samples big endian.
class GrayStageWriter final : public Tiio::Writer {
  FILE *m_file = nullptr;

  template <class Pixel>
  void writePixels(Pixel *pixels, bool sixteen) {
    std::vector<uchar> row(static_cast<size_t>(m_info.m_lx) *
                           (sixteen ? 2 : 1));
    for (int x = 0; x < m_info.m_lx; ++x) {
      const Pixel &pixel = pixels[x];
      const quint32 gray = (quint32(pixel.r) * 299 + quint32(pixel.g) * 587 +
                            quint32(pixel.b) * 114 + 500) /
                           1000;
      if (sixteen)
        qToBigEndian<quint16>(static_cast<quint16>(gray), row.data() + x * 2);
      else
        row[x] = static_cast<uchar>(gray);
    }
    if (fwrite(row.data(), 1, row.size(), m_file) != row.size())
      fail("Cannot write the grayscale staging image.");
  }

public:
  void open(FILE *file, const TImageInfo &info) override {
    m_file = file;
    m_info = info;
    if (info.m_bitsPerSample != 8 && info.m_bitsPerSample != 16)
      fail("Grayscale export supports 8 or 16 bits per channel.");
    if (fprintf(file, "P5\n%d %d\n%d\n", info.m_lx, info.m_ly,
                info.m_bitsPerSample == 16 ? 65535 : 255) < 0)
      fail("Cannot write the grayscale image header.");
  }
  void writeLine(char *line) override {
    writePixels(reinterpret_cast<TPixel32 *>(line), false);
  }
  void writeLine(short *line) override {
    writePixels(reinterpret_cast<TPixel64 *>(line), true);
  }
  Tiio::RowOrder getRowOrder() const override { return Tiio::TOP2BOTTOM; }
  bool write64bitSupported() const override { return true; }
  bool writeAlphaSupported() const override { return false; }
};

class JxlWriter final : public Tiio::Writer {
  QString m_ext;
  Tiio::WriterMaker *m_nativeMaker;
  JxlCodec::Settings m_settings;
  QTemporaryDir m_directory;
  File m_stageFile;
  std::unique_ptr<Tiio::Writer> m_writer;
  QString m_source;
  FILE *m_destination = nullptr;
  bool m_finished     = false;

public:
  JxlWriter(const QString &ext, Tiio::WriterMaker *native,
            const JxlCodec::Settings &configuration)
      : m_ext(ext)
      , m_nativeMaker(native)
      , m_settings(configuration)
      , m_stageFile(nullptr, fclose) {}

  ~JxlWriter() override {
    // Writers may emit trailers in their destructor, before the FILE closes.
    m_writer.reset();
    delete m_properties;
    m_properties = nullptr;
  }

  void open(FILE *file, const TImageInfo &info) override {
    if (extraFormat(m_ext)) requireExperimental();
    m_info        = info;
    m_destination = file;
    if (!m_directory.isValid())
      fail("Cannot create a temporary image directory.");
    if (!m_properties) m_properties = new Tiio::JxlWriterProperties();
    if (const TIntProperty *effort =
            dynamic_cast<TIntProperty *>(m_properties->getProperty("Effort")))
      m_settings.effort = effort->getValue();
    const bool gray = !m_nativeMaker && (m_ext == "pgm" || m_ext == "pgx");
    const QString stageFormat = m_nativeMaker ? m_ext : gray ? "pgm" : "png";
    m_source                  = m_directory.filePath("source." + stageFormat);
    m_stageFile               = openFile(m_source, "wb");
    if (!m_stageFile) fail("Cannot create the staging image.");

    if (m_nativeMaker) {
      m_writer.reset(m_nativeMaker());
      m_writer->setProperties(m_properties);
    } else if (gray) {
      m_writer.reset(new GrayStageWriter());
    } else {
      m_writer.reset(Tiio::makePngWriter());  // Leaf factory: never recurse.
      Tiio::PngWriterProperties pngProperties;
      bool alpha = m_ext == "jxl" || m_ext == "pam";
      if (const TBoolProperty *property = dynamic_cast<TBoolProperty *>(
              m_properties->getProperty("Alpha Channel")))
        alpha = alpha && property->getValue();
      pngProperties.m_matte.setValue(alpha);
      m_writer->setProperties(&pngProperties);
    }
    m_writer->open(m_stageFile.get(), info);
  }

  void writeLine(char *line) override { m_writer->writeLine(line); }
  void writeLine(short *line) override { m_writer->writeLine(line); }
  void writeLine(float *line) override { m_writer->writeLine(line); }
  Tiio::RowOrder getRowOrder() const override {
    return m_writer ? m_writer->getRowOrder() : Tiio::TOP2BOTTOM;
  }
  bool write64bitSupported() const override { return true; }
  bool writeAlphaSupported() const override {
    return m_writer ? m_writer->writeAlphaSupported() : true;
  }
  bool writeInLinearColorSpace() const override {
    return m_writer && m_writer->writeInLinearColorSpace();
  }
  bool requiresAtomicSave() const override { return true; }

  void flush() override {
    if (m_finished) return;
    if (!m_writer) fail("The image writer was not opened.");
    m_writer->flush();
    m_writer.reset();
    closeFile(m_stageFile);

    const QString converted = m_directory.filePath("converted." + m_ext);
    QString error;
    JxlCodec::ImageInfo imageInfo;
    JxlCodec::Result result =
        m_ext == "jxl"
            ? JxlCodec::encode(m_settings, m_source, converted, &error)
            : JxlCodec::roundTrip(m_settings, m_source, converted, m_ext,
                                  &imageInfo, &error);
    if (result == JxlCodec::Result::Success && m_ext == "jxl")
      writeDpi(converted, m_info);
    if (result == JxlCodec::Result::Success && m_nativeMaker &&
        m_ext == "png" && !preservePngDpi(m_source, converted)) {
      result = JxlCodec::Result::Failed;
      error  = "Cannot preserve PNG image resolution.";
    }
    if (result != JxlCodec::Result::Success) {
      if (!m_nativeMaker)
        fail(error.isEmpty()
                 ? QString("The selected tools cannot write this image.")
                 : error);
      fallback(m_ext, error);
      copyToFile(m_source, m_destination);
    } else {
      copyToFile(converted, m_destination);
    }
    m_finished = true;
  }
};

}  // namespace

Tiio::JxlWriterProperties::JxlWriterProperties(bool alphaSupported)
    : m_bitsPerPixel("Bits Per Pixel")
    , m_matte("Alpha Channel", alphaSupported)
    , m_effort("Effort", 1, 9, 7) {
  m_bitsPerPixel.addValue(L"32(RGBM)");
  m_bitsPerPixel.addValue(L"64(RGBM)");
  m_bitsPerPixel.setValue(L"32(RGBM)");
  bind(m_bitsPerPixel);
  if (alphaSupported) bind(m_matte);
  bind(m_effort);
}

void Tiio::JxlWriterProperties::updateTranslation() {
  m_bitsPerPixel.setQStringName(tr("Bit Depth"));
  m_bitsPerPixel.setItemUIName(L"32(RGBM)", tr("8 bits per channel"));
  m_bitsPerPixel.setItemUIName(L"64(RGBM)", tr("16 bits per channel"));
  m_matte.setQStringName(tr("Alpha Channel"));
  m_effort.setQStringName(tr("Effort"));
}

Tiio::Reader *Tiio::makeJxlReader() {
  return new JxlReader("jxl", nullptr, settings());
}

Tiio::Writer *Tiio::makeJxlWriter() {
  return new JxlWriter("jxl", nullptr, settings());
}

Tiio::Reader *Tiio::makeJxlRoutedReader(const std::string &extension,
                                        Tiio::ReaderMaker *native) {
  const QString ext = QString::fromStdString(extension).toLower();
  if (extraFormat(ext)) {
    requireExperimental();
    return new JxlReader(ext, native, settings());
  }
  if (native && ordinaryFormat(ext) &&
      Preferences::instance()->isJxlExperimentalEnabled()) {
    const JxlCodec::Settings configuration = settings();
    if (JxlCodec::supports(configuration, ext, "png"))
      return new JxlReader(ext, native, configuration);
    fallback(ext, "The selected tools do not support this input format.");
  }
  return native ? native() : nullptr;
}

Tiio::Writer *Tiio::makeJxlRoutedWriter(const std::string &extension,
                                        Tiio::WriterMaker *native) {
  const QString ext = QString::fromStdString(extension).toLower();
  if (extraFormat(ext)) {
    requireExperimental();
    return new JxlWriter(ext, native, settings());
  }
  if (native && ordinaryFormat(ext) &&
      Preferences::instance()->isJxlExperimentalEnabled()) {
    const JxlCodec::Settings configuration = settings();
    if (JxlCodec::supports(configuration, ext, ext))
      return new JxlWriter(ext, native, configuration);
    fallback(ext, "The selected tools do not support this output format.");
  }
  return native ? native() : nullptr;
}
