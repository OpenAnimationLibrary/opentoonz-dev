// SPDX-License-Identifier: BSD-3-Clause

#include "exr/tiio_exr.h"
#include "tpixel.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfCompression.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfInputFile.h>
#include <OpenEXR/ImfOutputFile.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;

class TemporaryExr {
  std::filesystem::path m_path;

public:
  TemporaryExr() {
    const auto stamp =
        std::chrono::steady_clock::now().time_since_epoch().count();
    m_path = std::filesystem::temp_directory_path() /
             ("opentoonz-exr-test-" + std::to_string(stamp) + ".exr");
  }

  ~TemporaryExr() {
    std::error_code error;
    std::filesystem::remove(m_path, error);
  }

  const std::filesystem::path &path() const { return m_path; }
};

void require(bool condition, const std::string &message) {
  if (!condition) throw std::runtime_error(message);
}

void requireNear(float actual, float expected, float tolerance,
                 const std::string &message) {
  if (std::fabs(actual - expected) > tolerance)
    throw std::runtime_error(message + ": expected " +
                             std::to_string(expected) + ", got " +
                             std::to_string(actual));
}

Exr::Compression compressionForName(const std::wstring &compression) {
  if (compression == L"PIZ") return Exr::PIZ_COMPRESSION;
  if (compression == L"ZIP") return Exr::ZIP_COMPRESSION;
  if (compression == L"ZSTD") return Exr::ZSTD_COMPRESSION;
  throw std::runtime_error("Unexpected compression in EXR test");
}

void roundTrip(const std::wstring &bitsPerPixel,
               const std::wstring &compression, const std::wstring &storage,
               float tolerance, bool expectAlpha) {
  TemporaryExr temporary;
  const std::string path = temporary.path().string();
  FILE *file             = std::fopen(path.c_str(), "w+b");
  require(file != nullptr, "Unable to create temporary EXR file");

  try {
    TImageInfo info(2, 2);
    Tiio::ExrWriterProperties properties;
    properties.m_bitsPerPixel.setValue(bitsPerPixel);
    properties.m_compressionType.setValue(compression);
    properties.m_storageType.setValue(storage);

    std::unique_ptr<Tiio::Writer> writer(Tiio::makeExrWriter());
    writer->setProperties(&properties);
    writer->open(file, info);

    TPixelF row0[2] = {{0.1f, 0.2f, 0.3f, 0.4f}, {1.0f, 0.5f, 0.25f, 0.75f}};
    TPixelF row1[2] = {{2.0f, 1.5f, 1.0f, 0.5f}, {-0.25f, 0.0f, 0.25f, 1.0f}};
    writer->writeLine(reinterpret_cast<float *>(row0));
    writer->writeLine(reinterpret_cast<float *>(row1));
    writer->flush();
    writer.reset();

    std::rewind(file);
    std::unique_ptr<Tiio::Reader> reader(Tiio::makeExrReader());
    reader->setColorSpaceGamma(1.0);
    reader->open(file);
    require(reader->getImageInfo().m_lx == 2, "EXR width mismatch");
    require(reader->getImageInfo().m_ly == 2, "EXR height mismatch");

    TPixelF decoded0[2] = {};
    TPixelF decoded1[2] = {};
    reader->readLine(reinterpret_cast<float *>(decoded0), 0, 1, 1);
    reader->readLine(reinterpret_cast<float *>(decoded1), 0, 1, 1);

    requireNear(decoded0[0].r, row0[0].r, tolerance, "First-row red mismatch");
    requireNear(decoded0[1].b, row0[1].b, tolerance, "First-row blue mismatch");
    requireNear(decoded1[0].g, row1[0].g, tolerance,
                "Second-row green mismatch");
    requireNear(decoded1[1].r, row1[1].r, tolerance, "Negative red mismatch");
    requireNear(decoded0[0].m, expectAlpha ? row0[0].m : 1.0f, tolerance,
                "Alpha mismatch");

    reader.reset();
    std::fclose(file);
    file = nullptr;

    Exr::InputFile inspection(path.c_str());
    require(
        inspection.header().compression() == compressionForName(compression),
        "EXR compression metadata mismatch");
    require(inspection.header().hasTileDescription() ==
                (storage == L"Store Image as Tiles"),
            "EXR storage metadata mismatch");
    require(inspection.header().channels()["R"].type ==
                (bitsPerPixel.find(L"_HF") != std::wstring::npos ? Exr::HALF
                                                                 : Exr::FLOAT),
            "EXR pixel type mismatch");
  } catch (...) {
    if (file) std::fclose(file);
    throw;
  }
}

void readZstdGrayscale() {
  TemporaryExr temporary;
  const std::string path = temporary.path().string();
  const int width        = 3;
  const int height       = 2;
  const IMATH_NAMESPACE::Box2i dataWindow(
      IMATH_NAMESPACE::V2i(-2, 3),
      IMATH_NAMESPACE::V2i(-2 + width - 1, 3 + height - 1));
  std::vector<float> grayscale = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f};

  Exr::Header header(width, height);
  header.dataWindow()    = dataWindow;
  header.displayWindow() = dataWindow;
  header.compression()   = Exr::ZSTD_COMPRESSION;
  header.channels().insert("Y", Exr::Channel(Exr::FLOAT));
  Exr::FrameBuffer frameBuffer;
  frameBuffer.insert(
      "Y", Exr::Slice::Make(Exr::FLOAT, grayscale.data(), dataWindow));
  {
    Exr::OutputFile output(path.c_str(), header);
    output.setFrameBuffer(frameBuffer);
    output.writePixels(height);
  }

  FILE *file = std::fopen(path.c_str(), "rb");
  require(file != nullptr, "Unable to reopen ZSTD EXR file");
  try {
    std::unique_ptr<Tiio::Reader> reader(Tiio::makeExrReader());
    reader->setColorSpaceGamma(1.0);
    reader->open(file);
    require(reader->getImageInfo().m_samplePerPixel == 1,
            "Grayscale EXR channel count mismatch");

    TPixelF row[width] = {};
    reader->readLine(reinterpret_cast<float *>(row), 0, width - 1, 1);
    for (int x = 0; x < width; ++x) {
      requireNear(row[x].r, grayscale[x], 1e-6f, "ZSTD grayscale red mismatch");
      requireNear(row[x].g, grayscale[x], 1e-6f,
                  "ZSTD grayscale green mismatch");
      requireNear(row[x].b, grayscale[x], 1e-6f,
                  "ZSTD grayscale blue mismatch");
      requireNear(row[x].m, grayscale[x], 1e-6f,
                  "ZSTD grayscale alpha mismatch");
    }
    reader.reset();
    std::fclose(file);
    file = nullptr;
  } catch (...) {
    if (file) std::fclose(file);
    throw;
  }
}
}  // namespace

int main() {
  try {
    roundTrip(L"128(RGBA)_F", L"PIZ", L"Store Image as Tiles", 1e-6f, true);
    roundTrip(L"96(RGB)_HF", L"ZIP", L"Store Image as Scanlines", 1e-3f, false);
    roundTrip(L"128(RGBA)_HF", L"ZIP", L"Store Image as Scanlines", 1e-3f,
              true);
    roundTrip(L"128(RGBA)_F", L"ZSTD", L"Store Image as Scanlines", 1e-6f,
              true);
    readZstdGrayscale();
    std::cout << "OpenEXR image I/O tests passed\n";
    return 0;
  } catch (const std::string &error) {
    std::cerr << error << '\n';
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
  }
  return 1;
}
