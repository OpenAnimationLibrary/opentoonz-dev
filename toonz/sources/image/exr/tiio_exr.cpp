// SPDX-License-Identifier: BSD-3-Clause

#include "tiio_exr.h"
#include "tpixel.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfCompression.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTileDescription.h>
#include <OpenEXR/ImfTiledOutputFile.h>

#include <Imath/half.h>

#include <QMap>
#include <QString>

#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;

inline unsigned char ftouc(float f, float gamma = 2.2f) {
  int i = static_cast<int>(255.0f * powf(f, 1.0f / gamma));
  if (i > 255) i = 255;
  if (i < 0) i = 0;
  return static_cast<unsigned char>(i);
}

inline float uctof(unsigned char uc, float gamma = 2.2f) {
  return powf(static_cast<float>(uc) / 255.0f, gamma);
}

inline unsigned short ftous(float f, float gamma = 2.2f) {
  int i = static_cast<int>(65535.0f * powf(f, 1.0f / gamma));
  if (i > 65535) i = 65535;
  if (i < 0) i = 0;
  return static_cast<unsigned short>(i);
}

inline float ustof(unsigned short us, float gamma = 2.2f) {
  return powf(static_cast<float>(us) / 65535.0f, gamma);
}

inline float toNonlinear(float f, float gamma = 2.2f) {
  if (f < 0.f) return f;
  return std::pow(f, 1.f / gamma);
}

const QMap<int, std::wstring> ExrCompTypeStr = {
    {static_cast<int>(Exr::NO_COMPRESSION), L"None"},
    {static_cast<int>(Exr::RLE_COMPRESSION), L"RLE"},
    {static_cast<int>(Exr::ZIPS_COMPRESSION), L"ZIPS"},
    {static_cast<int>(Exr::ZIP_COMPRESSION), L"ZIP"},
    {static_cast<int>(Exr::PIZ_COMPRESSION), L"PIZ"},
    {static_cast<int>(Exr::ZSTD_COMPRESSION), L"ZSTD"}};

// LJ2K is intentionally not a writer option until its lossy quality setting
// can be represented in ExrWriterProperties.

const std::wstring EXR_STORAGETYPE_SCANLINE = L"Store Image as Scanlines";
const std::wstring EXR_STORAGETYPE_TILE     = L"Store Image as Tiles";

std::runtime_error fileError(const char *operation) {
  const int error = errno;
  std::string message(operation);
  message += " failed";
  if (error) {
    message += ": ";
    message += std::strerror(error);
  }
  return std::runtime_error(message);
}

std::int64_t tellFile(FILE *file) {
#ifdef _WIN32
  const __int64 position = _ftelli64(file);
#else
  const off_t position = ftello(file);
#endif
  if (position < 0) throw fileError("File position query");
  return static_cast<std::int64_t>(position);
}

void seekFile(FILE *file, std::int64_t offset, int origin) {
#ifdef _WIN32
  const int result = _fseeki64(file, offset, origin);
#else
  const int result     = fseeko(file, static_cast<off_t>(offset), origin);
#endif
  if (result != 0) throw fileError("File seek");
}

class FileInputStream final : public Exr::IStream {
  FILE *m_file;
  std::int64_t m_size;

public:
  explicit FileInputStream(FILE *file)
      : Exr::IStream("OpenToonz FILE stream"), m_file(file), m_size(0) {
    if (!m_file) throw std::invalid_argument("EXR input stream is null");
    const std::int64_t position = tellFile(m_file);
    seekFile(m_file, 0, SEEK_END);
    m_size = tellFile(m_file);
    seekFile(m_file, position, SEEK_SET);
  }

  bool read(char buffer[], int size) override {
    if (size < 0) throw std::invalid_argument("Invalid EXR read size");
    if (size == 0) return tellFile(m_file) < m_size;

    errno = 0;
    const size_t actual =
        std::fread(buffer, 1, static_cast<size_t>(size), m_file);
    if (actual != static_cast<size_t>(size)) {
      if (std::ferror(m_file)) throw fileError("EXR file read");
      throw std::runtime_error("Unexpected end of EXR file");
    }
    return tellFile(m_file) < m_size;
  }

  std::uint64_t tellg() override {
    return static_cast<std::uint64_t>(tellFile(m_file));
  }

  void seekg(std::uint64_t position) override {
    if (position >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      throw std::out_of_range("EXR input seek position is too large");
    seekFile(m_file, static_cast<std::int64_t>(position), SEEK_SET);
  }

  void clear() override { std::clearerr(m_file); }
  std::int64_t size() override { return m_size; }
};

class FileOutputStream final : public Exr::OStream {
  FILE *m_file;

public:
  explicit FileOutputStream(FILE *file)
      : Exr::OStream("OpenToonz FILE stream"), m_file(file) {
    if (!m_file) throw std::invalid_argument("EXR output stream is null");
  }

  void write(const char buffer[], int size) override {
    if (size < 0) throw std::invalid_argument("Invalid EXR write size");
    if (size == 0) return;

    errno = 0;
    const size_t actual =
        std::fwrite(buffer, 1, static_cast<size_t>(size), m_file);
    if (actual != static_cast<size_t>(size)) throw fileError("EXR file write");
  }

  std::uint64_t tellp() override {
    return static_cast<std::uint64_t>(tellFile(m_file));
  }

  void seekp(std::uint64_t position) override {
    if (position >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
      throw std::out_of_range("EXR output seek position is too large");
    seekFile(m_file, static_cast<std::int64_t>(position), SEEK_SET);
  }
};

size_t checkedPixelCount(int width, int height) {
  if (width <= 0 || height <= 0)
    throw std::runtime_error("Invalid EXR image dimensions");

  const size_t w = static_cast<size_t>(width);
  const size_t h = static_cast<size_t>(height);
  if (w > std::numeric_limits<size_t>::max() / h ||
      w * h > std::numeric_limits<size_t>::max() / (4 * sizeof(float)))
    throw std::runtime_error("EXR image dimensions are too large");
  return w * h;
}

[[noreturn]] void throwAsString(const std::exception &error) {
  throw std::string(error.what());
}
}  // namespace

//**************************************************************************
//    ExrReader implementation
//**************************************************************************

class ExrReader final : public Tiio::Reader {
  std::vector<float> m_rgbaBuf;
  int m_row;
  std::unique_ptr<FileInputStream> m_stream;
  std::unique_ptr<Exr::InputFile> m_input;
  float m_colorSpaceGamma;

public:
  ExrReader();
  ~ExrReader() override = default;

  void open(FILE *file) override;
  Tiio::RowOrder getRowOrder() const override;
  bool read16BitIsEnabled() const override;
  int skipLines(int lineCount) override;
  void readLine(char *buffer, int x0, int x1, int shrink) override;
  void readLine(short *buffer, int x0, int x1, int shrink) override;
  void readLine(float *buffer, int x0, int x1, int shrink) override;
  void loadImage();
  void setColorSpaceGamma(const double gamma) override {
    assert(gamma > 0);
    m_colorSpaceGamma = static_cast<float>(gamma);
  }
};

ExrReader::ExrReader() : m_row(0), m_colorSpaceGamma(2.2f) {}

void ExrReader::open(FILE *file) {
  try {
    if (!file) throw std::invalid_argument("EXR input file is null");
    m_input.reset();
    m_stream.reset();
    m_rgbaBuf.clear();
    m_row = 0;
    seekFile(file, 0, SEEK_SET);
    m_stream = std::make_unique<FileInputStream>(file);
    m_input  = std::make_unique<Exr::InputFile>(*m_stream);

    const IMATH_NAMESPACE::Box2i &dataWindow = m_input->header().dataWindow();
    const std::int64_t width =
        static_cast<std::int64_t>(dataWindow.max.x) - dataWindow.min.x + 1;
    const std::int64_t height =
        static_cast<std::int64_t>(dataWindow.max.y) - dataWindow.min.y + 1;
    if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
        height > std::numeric_limits<int>::max())
      throw std::runtime_error("Invalid EXR data window");

    m_info.m_lx = static_cast<int>(width);
    m_info.m_ly = static_cast<int>(height);

    int channelCount                 = 0;
    const Exr::ChannelList &channels = m_input->header().channels();
    for (Exr::ChannelList::ConstIterator channel = channels.begin();
         channel != channels.end(); ++channel)
      ++channelCount;

    m_info.m_samplePerPixel = channelCount;
    // OpenToonz receives every EXR channel converted to a 32-bit float.
    m_info.m_bitsPerSample = 32;
  } catch (const std::exception &error) {
    m_input.reset();
    m_stream.reset();
    throwAsString(error);
  }
}

Tiio::RowOrder ExrReader::getRowOrder() const { return Tiio::TOP2BOTTOM; }

bool ExrReader::read16BitIsEnabled() const { return true; }

int ExrReader::skipLines(int lineCount) {
  m_row += lineCount;
  return lineCount;
}

void ExrReader::loadImage() {
  assert(m_rgbaBuf.empty());
  assert(m_input);

  try {
    const Exr::Header &header               = m_input->header();
    const IMATH_NAMESPACE::Box2i dataWindow = header.dataWindow();
    const size_t pixelCount = checkedPixelCount(m_info.m_lx, m_info.m_ly);

    std::vector<std::string> baseChannels;
    const Exr::ChannelList &channels = header.channels();
    for (Exr::ChannelList::ConstIterator channel = channels.begin();
         channel != channels.end(); ++channel) {
      const std::string name(channel.name());
      if (name.find('.') == std::string::npos) baseChannels.push_back(name);
    }

    if (baseChannels.empty())
      throw std::runtime_error("EXR base layer contains no channels");

    m_rgbaBuf.assign(pixelCount * 4, 1.0f);
    Exr::FrameBuffer frameBuffer;

    if (baseChannels.size() == 1) {
      std::vector<float> grayscale(pixelCount);
      frameBuffer.insert(
          baseChannels.front(),
          Exr::Slice::Make(Exr::FLOAT, grayscale.data(), dataWindow));
      m_input->setFrameBuffer(frameBuffer);
      m_input->readPixels(dataWindow.min.y, dataWindow.max.y);

      for (size_t i = 0; i < pixelCount; ++i) {
        m_rgbaBuf[i * 4]     = grayscale[i];
        m_rgbaBuf[i * 4 + 1] = grayscale[i];
        m_rgbaBuf[i * 4 + 2] = grayscale[i];
        m_rgbaBuf[i * 4 + 3] = grayscale[i];
      }
    } else {
      if (!channels.findChannel("R"))
        throw std::runtime_error("R channel not found in EXR base layer");
      if (!channels.findChannel("G"))
        throw std::runtime_error("G channel not found in EXR base layer");
      if (!channels.findChannel("B"))
        throw std::runtime_error("B channel not found in EXR base layer");

      const size_t xStride = 4 * sizeof(float);
      const size_t yStride = static_cast<size_t>(m_info.m_lx) * xStride;
      frameBuffer.insert("R", Exr::Slice::Make(Exr::FLOAT, m_rgbaBuf.data(),
                                               dataWindow, xStride, yStride));
      frameBuffer.insert("G", Exr::Slice::Make(Exr::FLOAT, m_rgbaBuf.data() + 1,
                                               dataWindow, xStride, yStride));
      frameBuffer.insert("B", Exr::Slice::Make(Exr::FLOAT, m_rgbaBuf.data() + 2,
                                               dataWindow, xStride, yStride));
      frameBuffer.insert(
          "A", Exr::Slice::Make(Exr::FLOAT, m_rgbaBuf.data() + 3, dataWindow,
                                xStride, yStride, 1, 1, 1.0));
      m_input->setFrameBuffer(frameBuffer);
      m_input->readPixels(dataWindow.min.y, dataWindow.max.y);
    }

    m_input.reset();
    m_stream.reset();
  } catch (const std::exception &error) {
    m_rgbaBuf.clear();
    m_input.reset();
    m_stream.reset();
    throwAsString(error);
  }
}

void ExrReader::readLine(char *buffer, int x0, int x1, int shrink) {
  const int pixelSize = 4;
  if (m_row < 0 || m_row >= m_info.m_ly) {
    memset(buffer, 0, (x1 - x0 + 1) * pixelSize);
    ++m_row;
    return;
  }

  if (m_rgbaBuf.empty()) loadImage();

  TPixel32 *pix = reinterpret_cast<TPixel32 *>(buffer) + x0;
  const float *value =
      m_rgbaBuf.data() + (static_cast<size_t>(m_row) * m_info.m_lx + x0) * 4;
  const int width =
      (x1 < x0) ? (m_info.m_lx - 1) / shrink + 1 : (x1 - x0) / shrink + 1;

  for (int i = 0; i < width; ++i) {
    pix->r = ftouc(value[0], m_colorSpaceGamma);
    pix->g = ftouc(value[1], m_colorSpaceGamma);
    pix->b = ftouc(value[2], m_colorSpaceGamma);
    pix->m = ftouc(value[3], 1.0f);
    value += shrink * 4;
    pix += shrink;
  }
  ++m_row;
}

void ExrReader::readLine(short *buffer, int x0, int x1, int shrink) {
  const int pixelSize = 8;
  if (m_row < 0 || m_row >= m_info.m_ly) {
    memset(buffer, 0, (x1 - x0 + 1) * pixelSize);
    ++m_row;
    return;
  }

  if (m_rgbaBuf.empty()) loadImage();

  TPixel64 *pix = reinterpret_cast<TPixel64 *>(buffer) + x0;
  const float *value =
      m_rgbaBuf.data() + (static_cast<size_t>(m_row) * m_info.m_lx + x0) * 4;
  const int width =
      (x1 < x0) ? (m_info.m_lx - 1) / shrink + 1 : (x1 - x0) / shrink + 1;

  for (int i = 0; i < width; ++i) {
    pix->r = ftous(value[0], m_colorSpaceGamma);
    pix->g = ftous(value[1], m_colorSpaceGamma);
    pix->b = ftous(value[2], m_colorSpaceGamma);
    pix->m = ftous(value[3], 1.0f);
    value += shrink * 4;
    pix += shrink;
  }
  ++m_row;
}

void ExrReader::readLine(float *buffer, int x0, int x1, int shrink) {
  const int pixelSize = 16;
  if (m_row < 0 || m_row >= m_info.m_ly) {
    memset(buffer, 0, (x1 - x0 + 1) * pixelSize);
    ++m_row;
    return;
  }

  if (m_rgbaBuf.empty()) loadImage();

  TPixelF *pix = reinterpret_cast<TPixelF *>(buffer) + x0;
  const float *value =
      m_rgbaBuf.data() + (static_cast<size_t>(m_row) * m_info.m_lx + x0) * 4;
  const int width =
      (x1 < x0) ? (m_info.m_lx - 1) / shrink + 1 : (x1 - x0) / shrink + 1;

  for (int i = 0; i < width; ++i) {
    pix->r = toNonlinear(value[0], m_colorSpaceGamma);
    pix->g = toNonlinear(value[1], m_colorSpaceGamma);
    pix->b = toNonlinear(value[2], m_colorSpaceGamma);
    pix->m = toNonlinear(value[3], 1.0f);
    value += shrink * 4;
    pix += shrink;
  }
  ++m_row;
}

//============================================================

Tiio::ExrWriterProperties::ExrWriterProperties()
    : m_compressionType("Compression Type")
    , m_storageType("Storage Type")
    , m_bitsPerPixel("Bits Per Pixel")
    , m_colorSpaceGamma("Color Space Gamma", 0.1, 10.0, 2.2) {
  m_bitsPerPixel.addValue(L"96(RGB)_HF");
  m_bitsPerPixel.addValue(L"128(RGBA)_HF");
  m_bitsPerPixel.addValue(L"96(RGB)_F");
  m_bitsPerPixel.addValue(L"128(RGBA)_F");
  m_bitsPerPixel.setValue(L"128(RGBA)_HF");

  m_compressionType.addValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::NO_COMPRESSION)));
  m_compressionType.addValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::RLE_COMPRESSION)));
  m_compressionType.addValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::ZIPS_COMPRESSION)));
  m_compressionType.addValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::ZIP_COMPRESSION)));
  m_compressionType.addValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::PIZ_COMPRESSION)));
  m_compressionType.addValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::ZSTD_COMPRESSION)));
  m_compressionType.setValue(
      ExrCompTypeStr.value(static_cast<int>(Exr::NO_COMPRESSION)));

  m_storageType.addValue(EXR_STORAGETYPE_SCANLINE);
  m_storageType.addValue(EXR_STORAGETYPE_TILE);
  m_storageType.setValue(EXR_STORAGETYPE_SCANLINE);

  bind(m_bitsPerPixel);
  bind(m_compressionType);
  bind(m_storageType);
  bind(m_colorSpaceGamma);
}

void Tiio::ExrWriterProperties::updateTranslation() {
  m_bitsPerPixel.setQStringName(tr("Bits Per Pixel"));
  m_bitsPerPixel.setItemUIName(L"96(RGB)_HF", tr("48(RGB Half Float)"));
  m_bitsPerPixel.setItemUIName(L"128(RGBA)_HF", tr("64(RGBA Half Float)"));
  m_bitsPerPixel.setItemUIName(L"96(RGB)_F", tr("96(RGB Float)"));
  m_bitsPerPixel.setItemUIName(L"128(RGBA)_F", tr("128(RGBA Float)"));

  m_compressionType.setQStringName(tr("Compression Type"));
  m_compressionType.setItemUIName(
      ExrCompTypeStr.value(static_cast<int>(Exr::NO_COMPRESSION)),
      tr("No compression"));
  m_compressionType.setItemUIName(
      ExrCompTypeStr.value(static_cast<int>(Exr::RLE_COMPRESSION)),
      tr("Run Length Encoding (RLE)"));
  m_compressionType.setItemUIName(
      ExrCompTypeStr.value(static_cast<int>(Exr::ZIPS_COMPRESSION)),
      tr("ZIP compression per Scanline (ZIPS)"));
  m_compressionType.setItemUIName(
      ExrCompTypeStr.value(static_cast<int>(Exr::ZIP_COMPRESSION)),
      tr("ZIP compression per scanline band (ZIP)"));
  m_compressionType.setItemUIName(
      ExrCompTypeStr.value(static_cast<int>(Exr::PIZ_COMPRESSION)),
      tr("PIZ-based wavelet compression (PIZ)"));
  m_compressionType.setItemUIName(
      ExrCompTypeStr.value(static_cast<int>(Exr::ZSTD_COMPRESSION)),
      tr("Zstandard compression per scanline (ZSTD)"));

  m_storageType.setQStringName(tr("Storage Type"));
  m_storageType.setItemUIName(EXR_STORAGETYPE_SCANLINE, tr("Scan-line based"));
  m_storageType.setItemUIName(EXR_STORAGETYPE_TILE, tr("Tile based"));
  m_colorSpaceGamma.setQStringName(tr("Color Space Gamma"));
}

//============================================================

class ExrWriter final : public Tiio::Writer {
  std::vector<float> m_imageBuf[4];
  int m_row;
  FILE *m_file;
  int m_bpp;
  Exr::Compression m_compression;
  Exr::PixelType m_pixelType;
  bool m_tiled;
  bool m_flushed;

public:
  ExrWriter();
  ~ExrWriter() override = default;

  void open(FILE *file, const TImageInfo &info) override;
  void writeLine(char *buffer) override;
  void writeLine(short *buffer) override;
  void writeLine(float *buffer) override;
  void flush() override;

  Tiio::RowOrder getRowOrder() const override { return Tiio::TOP2BOTTOM; }
  bool writeAlphaSupported() const override { return m_bpp == 128; }
  bool writeInLinearColorSpace() const override { return true; }
};

ExrWriter::ExrWriter()
    : m_row(0)
    , m_file(nullptr)
    , m_bpp(96)
    , m_compression(Exr::NO_COMPRESSION)
    , m_pixelType(Exr::HALF)
    , m_tiled(false)
    , m_flushed(false) {}

void ExrWriter::open(FILE *file, const TImageInfo &info) {
  try {
    if (!file) throw std::invalid_argument("EXR output file is null");
    checkedPixelCount(info.m_lx, info.m_ly);

    m_file    = file;
    m_info    = info;
    m_row     = 0;
    m_flushed = false;
    seekFile(m_file, 0, SEEK_SET);

    if (!m_properties) m_properties = new Tiio::ExrWriterProperties();

    TEnumProperty *bitsPerPixel = static_cast<TEnumProperty *>(
        m_properties->getProperty("Bits Per Pixel"));
    m_bpp = bitsPerPixel ? std::stoi(bitsPerPixel->getValue()) : 128;
    if (m_bpp != 96 && m_bpp != 128)
      throw std::runtime_error("Unsupported EXR bits-per-pixel setting");

    TEnumProperty *compressionProperty = static_cast<TEnumProperty *>(
        m_properties->getProperty("Compression Type"));
    const std::wstring compressionType =
        compressionProperty ? compressionProperty->getValue() : L"None";
    m_compression = static_cast<Exr::Compression>(ExrCompTypeStr.key(
        compressionType, static_cast<int>(Exr::NO_COMPRESSION)));

    TEnumProperty *storageProperty =
        static_cast<TEnumProperty *>(m_properties->getProperty("Storage Type"));
    m_tiled =
        storageProperty && storageProperty->getValue() == EXR_STORAGETYPE_TILE;

    m_pixelType =
        !bitsPerPixel || QString::fromStdWString(bitsPerPixel->getValue())
                             .endsWith("_HF")
            ? Exr::HALF
            : Exr::FLOAT;

    const int channelCount  = (m_bpp == 128) ? 4 : 3;
    const size_t pixelCount = checkedPixelCount(m_info.m_lx, m_info.m_ly);
    for (int channel = 0; channel < channelCount; ++channel)
      m_imageBuf[channel].resize(pixelCount);
  } catch (const std::exception &error) {
    throwAsString(error);
  }
}

void ExrWriter::writeLine(char *buffer) {
  if (m_row >= m_info.m_ly) throw std::string("Too many EXR scanlines");

  TPixel32 *pix          = reinterpret_cast<TPixel32 *>(buffer);
  TPixel32 *endPix       = pix + m_info.m_lx;
  const size_t rowOffset = static_cast<size_t>(m_row) * m_info.m_lx;
  float *red             = &m_imageBuf[0][rowOffset];
  float *green           = &m_imageBuf[1][rowOffset];
  float *blue            = &m_imageBuf[2][rowOffset];
  float *alpha           = m_bpp == 128 ? &m_imageBuf[3][rowOffset] : nullptr;
  while (pix < endPix) {
    *red++   = uctof(pix->r);
    *green++ = uctof(pix->g);
    *blue++  = uctof(pix->b);
    if (alpha) *alpha++ = uctof(pix->m, 1.0f);
    ++pix;
  }
  ++m_row;
}

void ExrWriter::writeLine(short *buffer) {
  if (m_row >= m_info.m_ly) throw std::string("Too many EXR scanlines");

  TPixel64 *pix          = reinterpret_cast<TPixel64 *>(buffer);
  TPixel64 *endPix       = pix + m_info.m_lx;
  const size_t rowOffset = static_cast<size_t>(m_row) * m_info.m_lx;
  float *red             = &m_imageBuf[0][rowOffset];
  float *green           = &m_imageBuf[1][rowOffset];
  float *blue            = &m_imageBuf[2][rowOffset];
  float *alpha           = m_bpp == 128 ? &m_imageBuf[3][rowOffset] : nullptr;
  while (pix < endPix) {
    *red++   = ustof(pix->r);
    *green++ = ustof(pix->g);
    *blue++  = ustof(pix->b);
    if (alpha) *alpha++ = ustof(pix->m, 1.0f);
    ++pix;
  }
  ++m_row;
}

void ExrWriter::writeLine(float *buffer) {
  if (m_row >= m_info.m_ly) throw std::string("Too many EXR scanlines");

  TPixelF *pix           = reinterpret_cast<TPixelF *>(buffer);
  TPixelF *endPix        = pix + m_info.m_lx;
  const size_t rowOffset = static_cast<size_t>(m_row) * m_info.m_lx;
  float *red             = &m_imageBuf[0][rowOffset];
  float *green           = &m_imageBuf[1][rowOffset];
  float *blue            = &m_imageBuf[2][rowOffset];
  float *alpha           = m_bpp == 128 ? &m_imageBuf[3][rowOffset] : nullptr;
  while (pix < endPix) {
    // The raster is already linearized in MovieRenderer::Imp::postProcessImage.
    *red++   = pix->r;
    *green++ = pix->g;
    *blue++  = pix->b;
    if (alpha) *alpha++ = pix->m;
    ++pix;
  }
  ++m_row;
}

void ExrWriter::flush() {
  if (m_flushed) return;

  try {
    if (!m_file) throw std::runtime_error("EXR writer is not open");
    if (m_row != m_info.m_ly)
      throw std::runtime_error("EXR image has incomplete scanline data");

    const IMATH_NAMESPACE::Box2i dataWindow(
        IMATH_NAMESPACE::V2i(0, 0),
        IMATH_NAMESPACE::V2i(m_info.m_lx - 1, m_info.m_ly - 1));
    Exr::Header header(m_info.m_lx, m_info.m_ly);
    header.compression() = m_compression;
    header.channels().insert("B", Exr::Channel(m_pixelType));
    header.channels().insert("G", Exr::Channel(m_pixelType));
    header.channels().insert("R", Exr::Channel(m_pixelType));
    if (m_bpp == 128) header.channels().insert("A", Exr::Channel(m_pixelType));

    const int channelCount     = (m_bpp == 128) ? 4 : 3;
    const size_t pixelCount    = checkedPixelCount(m_info.m_lx, m_info.m_ly);
    const char *channelNames[] = {"R", "G", "B", "A"};
    Exr::FrameBuffer frameBuffer;
    std::vector<IMATH_NAMESPACE::half> halfImageBuf[4];
    if (m_pixelType == Exr::HALF) {
      for (int channel = 0; channel < channelCount; ++channel) {
        halfImageBuf[channel].resize(pixelCount);
        for (size_t pixel = 0; pixel < pixelCount; ++pixel)
          halfImageBuf[channel][pixel] = m_imageBuf[channel][pixel];

        // Release each full-float channel as soon as it has been converted so
        // large half-float renders do not retain two complete image buffers.
        std::vector<float>().swap(m_imageBuf[channel]);
        frameBuffer.insert(
            channelNames[channel],
            Exr::Slice::Make(Exr::HALF, halfImageBuf[channel].data(),
                             dataWindow));
      }
    } else {
      for (int channel = 0; channel < channelCount; ++channel)
        frameBuffer.insert(
            channelNames[channel],
            Exr::Slice::Make(Exr::FLOAT, m_imageBuf[channel].data(),
                             dataWindow));
    }

    FileOutputStream stream(m_file);
    if (m_tiled) {
      header.setType(Exr::TILEDIMAGE);
      header.setTileDescription(Exr::TileDescription(128, 128, Exr::ONE_LEVEL));
      Exr::TiledOutputFile output(stream, header);
      output.setFrameBuffer(frameBuffer);
      output.writeTiles(0, output.numXTiles() - 1, 0, output.numYTiles() - 1);
    } else {
      Exr::OutputFile output(stream, header);
      output.setFrameBuffer(frameBuffer);
      output.writePixels(m_info.m_ly);
    }

    errno = 0;
    if (std::fflush(m_file) != 0) throw fileError("EXR file flush");
    m_flushed = true;
  } catch (const std::exception &error) {
    throwAsString(error);
  }
}

//============================================================

Tiio::Reader *Tiio::makeExrReader() { return new ExrReader(); }

//------------------------------------------------------------

Tiio::Writer *Tiio::makeExrWriter() { return new ExrWriter(); }
