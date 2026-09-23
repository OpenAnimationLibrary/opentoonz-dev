// SPDX-License-Identifier: BSD-3-Clause

#include "../../toonzlib/exrlevelsource.h"
#include "../../toonzlib/exrlevelloader.h"

#include "toonz/imagemanager.h"
#include "toonz/levelproperties.h"
#include "toonz/txshexrlevel.h"
#include "toonz/txshleveltypes.h"

#include "tfiletype.h"
#include "tstream.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTiledOutputPart.h>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void expect(float actual, float expected, const char *message) {
  if (std::fabs(actual - expected) > 0.00001f)
    throw std::runtime_error(std::string(message) + ": expected " +
                             std::to_string(expected) + ", got " +
                             std::to_string(actual));
}

struct Plane {
  std::string name;
  std::vector<float> values;
  int xSampling = 1;
  int ySampling = 1;
};

void addChannels(Exr::Header &header, const std::vector<Plane> &planes) {
  for (const Plane &plane : planes)
    header.channels().insert(
        plane.name, Exr::Channel(Exr::FLOAT, plane.xSampling, plane.ySampling));
}

Exr::FrameBuffer frameBuffer(std::vector<Plane> &planes,
                             const IMATH_NAMESPACE::Box2i &dataWindow) {
  Exr::FrameBuffer result;
  for (Plane &plane : planes)
    result.insert(
        plane.name,
        Exr::Slice::Make(Exr::FLOAT, plane.values.data(), dataWindow,
                         sizeof(float), 0, plane.xSampling, plane.ySampling));
  return result;
}

void writeFixture(const QString &path) {
  using IMATH_NAMESPACE::Box2i;
  using IMATH_NAMESPACE::V2i;
  const Box2i display(V2i(-2, 3), V2i(3, 6));
  const Box2i data(V2i(-1, 4), V2i(2, 5));
  constexpr int count = 8;

  std::vector<Plane> scanline = {
      {"R", std::vector<float>(count)},
      {"G", std::vector<float>(count, 0.25f)},
      {"B", std::vector<float>(count, 4.0f)},
      {"A", std::vector<float>(count, 0.5f)},
      {"normal.X", std::vector<float>(count, -1.0f)},
      {"normal.Y", std::vector<float>(count, 0.2f)},
      {"normal.Z", std::vector<float>(count, 0.8f)},
      {"depth.Y", std::vector<float>(count)},
      {"subsampled.Y", std::vector<float>(4, 0.5f), 1, 2}};
  for (int index = 0; index < count; ++index) {
    scanline[0].values[index] = 0.1f + static_cast<float>(index) * 0.1f;
    scanline[7].values[index] = static_cast<float>(index);
  }
  std::vector<Plane> tiled          = {{"R", std::vector<float>(count, 0.9f)},
                                       {"G", std::vector<float>(count, 0.8f)},
                                       {"B", std::vector<float>(count, 0.7f)}};
  std::vector<Plane> multipartDepth = {{"Y", std::vector<float>(count, 2.0f)}};

  std::vector<Exr::Header> headers;
  headers.emplace_back(display, data);
  headers.back().setName("beauty");
  headers.back().setType(Exr::SCANLINEIMAGE);
  addChannels(headers.back(), scanline);
  headers.emplace_back(display, data);
  headers.back().setName("normal");
  headers.back().setType(Exr::TILEDIMAGE);
  headers.back().setTileDescription(Exr::TileDescription(2, 2, Exr::ONE_LEVEL));
  addChannels(headers.back(), tiled);
  headers.emplace_back(display, data);
  headers.back().setName("depth");
  headers.back().setType(Exr::SCANLINEIMAGE);
  addChannels(headers.back(), multipartDepth);

  Exr::MultiPartOutputFile output(path.toUtf8().constData(), headers.data(),
                                  static_cast<int>(headers.size()));
  Exr::OutputPart scanlinePart(output, 0);
  const Exr::FrameBuffer scanlineBuffer = frameBuffer(scanline, data);
  scanlinePart.setFrameBuffer(scanlineBuffer);
  scanlinePart.writePixels(data.max.y - data.min.y + 1);

  Exr::TiledOutputPart tiledPart(output, 1);
  const Exr::FrameBuffer tiledBuffer = frameBuffer(tiled, data);
  tiledPart.setFrameBuffer(tiledBuffer);
  tiledPart.writeTiles(0, tiledPart.numXTiles() - 1, 0,
                       tiledPart.numYTiles() - 1);

  Exr::OutputPart depthPart(output, 2);
  const Exr::FrameBuffer depthBuffer = frameBuffer(multipartDepth, data);
  depthPart.setFrameBuffer(depthBuffer);
  depthPart.writePixels(data.max.y - data.min.y + 1);
}

void writeFixtureWithoutNormalPart(const QString &path) {
  using IMATH_NAMESPACE::Box2i;
  using IMATH_NAMESPACE::V2i;
  const Box2i display(V2i(-2, 3), V2i(3, 6));
  const Box2i data(V2i(-1, 4), V2i(2, 5));
  constexpr int count = 8;

  std::vector<Plane> beauty = {{"R", std::vector<float>(count, 0.1f)},
                               {"G", std::vector<float>(count, 0.2f)},
                               {"B", std::vector<float>(count, 0.3f)}};
  std::vector<Plane> depth  = {{"Y", std::vector<float>(count, 2.0f)}};
  std::vector<Exr::Header> headers;
  headers.emplace_back(display, data);
  headers.back().setName("beauty");
  headers.back().setType(Exr::SCANLINEIMAGE);
  addChannels(headers.back(), beauty);
  headers.emplace_back(display, data);
  headers.back().setName("depth");
  headers.back().setType(Exr::SCANLINEIMAGE);
  addChannels(headers.back(), depth);

  Exr::MultiPartOutputFile output(path.toUtf8().constData(), headers.data(),
                                  static_cast<int>(headers.size()));
  Exr::OutputPart beautyPart(output, 0);
  const Exr::FrameBuffer beautyBuffer = frameBuffer(beauty, data);
  beautyPart.setFrameBuffer(beautyBuffer);
  beautyPart.writePixels(data.max.y - data.min.y + 1);

  Exr::OutputPart depthPart(output, 1);
  const Exr::FrameBuffer depthBuffer = frameBuffer(depth, data);
  depthPart.setFrameBuffer(depthBuffer);
  depthPart.writePixels(data.max.y - data.min.y + 1);
}

void writeFixtureWithoutBaseLayer(const QString &path) {
  using IMATH_NAMESPACE::Box2i;
  using IMATH_NAMESPACE::V2i;
  const Box2i display(V2i(-2, 3), V2i(3, 6));
  const Box2i data(V2i(-1, 4), V2i(2, 5));
  constexpr int count = 8;

  std::vector<Plane> diffuse = {{"diffuse.R", std::vector<float>(count, 0.7f)},
                                {"diffuse.G", std::vector<float>(count, 0.6f)},
                                {"diffuse.B", std::vector<float>(count, 0.5f)}};
  Exr::Header header(display, data);
  header.setName("beauty");
  header.setType(Exr::SCANLINEIMAGE);
  addChannels(header, diffuse);

  Exr::MultiPartOutputFile output(path.toUtf8().constData(), &header, 1);
  Exr::OutputPart part(output, 0);
  const Exr::FrameBuffer buffer = frameBuffer(diffuse, data);
  part.setFrameBuffer(buffer);
  part.writePixels(data.max.y - data.min.y + 1);
}

TRasterFP raster(const TRasterImageP &image) {
  require(image, "EXR load returned no image");
  TRasterFP result = image->getRaster();
  require(result, "EXR level decoder did not return a float raster");
  return result;
}

void testInspection(const TFilePath &path) {
  const std::vector<ExrLevelSource::PartInfo> parts =
      ExrLevelSource::inspect(path);
  require(parts.size() == 3, "Multipart EXR part count is wrong");
  require(parts[0].stableId == "name:beauty" &&
              parts[0].type == Exr::SCANLINEIMAGE && parts[0].supported,
          "Scanline part metadata is wrong");
  require(parts[1].stableId == "name:normal" && parts[1].tiled &&
              parts[1].supported,
          "Tiled part metadata is wrong");
  require(parts[0].displayWindow.xMin == -2 &&
              parts[0].displayWindow.yMax == 6 &&
              parts[0].dataWindow.xMin == -1 && parts[0].dataWindow.yMax == 5,
          "EXR windows were not preserved");
  require(parts[0].hasBaseLayer && parts[0].layers.size() == 3,
          "Base/named layer discovery is wrong");
  require(parts[0].channels.size() == 9, "EXR channel discovery lost channels");

  ExrLevelSource::Selection named;
  named.part = "name:normal";
  require(ExrLevelSource::resolve(parts, named).index == 1,
          "Stable part ID did not resolve");
  named.part              = "missing";
  named.partIndexFallback = 1;
  require(ExrLevelSource::resolve(parts, named).name == "normal",
          "Ordinal part fallback did not resolve");
}

void testScanlineAndInfo(const TFilePath &path) {
  ExrLevelSource::Selection selection;
  selection.part            = "beauty";
  const TRasterImageP image = ExrLevelSource::load(path, selection, 1.0);
  const TRasterFP decoded   = raster(image);
  require(decoded->getLx() == 6 && decoded->getLy() == 4,
          "Display-window raster dimensions are wrong");
  const TPixelF outside = decoded->pixels(0)[0];
  expect(outside.r, 0.0f, "Display padding is not black");
  expect(outside.m, 0.0f, "Display padding is not transparent");
  // OpenToonz row 2 maps to the first/top EXR data scanline (EXR y=4).
  const TPixelF top = decoded->pixels(2)[1];
  expect(top.r, 0.1f, "EXR top/bottom orientation is wrong");
  expect(top.g, 0.25f, "EXR G channel is wrong");
  expect(top.b, 4.0f, "Float HDR channel was clamped");
  expect(top.m, 0.5f, "EXR alpha channel is wrong");
  const TPixelF bottom = decoded->pixels(1)[1];
  expect(bottom.r, 0.5f, "EXR lower scanline orientation is wrong");

  TImageInfo info;
  std::string error;
  require(ExrLevelSource::getInfo(path, info, selection, &error),
          "EXR image info failed");
  require(error.empty() && info.m_lx == 6 && info.m_ly == 4 && info.m_x0 == 1 &&
              info.m_y0 == 1 && info.m_x1 == 4 && info.m_y1 == 2 &&
              info.m_bitsPerSample == 32 && info.m_valid,
          "EXR image info did not preserve display/data windows");
  require(image->getSavebox() == TRect(1, 1, 4, 2),
          "EXR raster savebox does not match the data window");
}

void testLayersAndTiled(const TFilePath &path) {
  ExrLevelSource::Selection selection;
  selection.part       = "name:beauty";
  selection.layer      = "normal";
  TRasterFP decoded    = raster(ExrLevelSource::load(path, selection, 1.0));
  const TPixelF normal = decoded->pixels(2)[1];
  expect(normal.r, -1.0f, "Negative normal X was not preserved");
  expect(normal.g, 0.2f, "Normal Y was not mapped to green");
  expect(normal.b, 0.8f, "Normal Z was not mapped to blue");
  expect(normal.m, 1.0f, "Missing normal alpha did not default to one");

  selection.layer    = "depth";
  decoded            = raster(ExrLevelSource::load(path, selection, 1.0));
  const TPixelF gray = decoded->pixels(1)[1];
  expect(gray.r, 4.0f, "Y channel grayscale mapping is wrong");
  expect(gray.g, gray.r, "Y channel did not map to green");
  expect(gray.b, gray.r, "Y channel did not map to blue");

  selection.part      = "name:normal";
  selection.layer     = "";
  decoded             = raster(ExrLevelSource::load(path, selection, 1.0));
  const TPixelF tiled = decoded->pixels(2)[1];
  expect(tiled.r, 0.9f, "Tiled EXR R decode failed");
  expect(tiled.g, 0.8f, "Tiled EXR G decode failed");
  expect(tiled.b, 0.7f, "Tiled EXR B decode failed");

  selection.part  = "name:beauty";
  selection.layer = "subsampled";
  TImageInfo info;
  std::string validationError;
  require(
      !ExrLevelSource::getInfo(path, info, selection, &validationError) &&
          validationError.find("Subsampled EXR channels") != std::string::npos,
      "EXR info did not reject a subsampled selection early");
  try {
    ExrLevelSource::load(path, selection, 1.0);
    throw std::runtime_error("Subsampled EXR layer was unexpectedly decoded");
  } catch (const std::runtime_error &error) {
    require(std::string(error.what()).find("Subsampled EXR channels") !=
                std::string::npos,
            "Subsampled EXR rejection is not actionable");
  }
}

const ExrLevel::Binding &binding(const std::vector<ExrLevel::Binding> &bindings,
                                 const std::string &part,
                                 const std::string &layer) {
  const auto found = std::find_if(
      bindings.begin(), bindings.end(), [&](const ExrLevel::Binding &item) {
        return item.part == part && item.layer == layer;
      });
  if (found == bindings.end())
    throw std::runtime_error("Expected EXR binding was not enumerated");
  return *found;
}

void testBindings(const TFilePath &path) {
  std::string error;
  const std::vector<ExrLevel::Binding> bindings =
      ExrLevel::inspectBindings(path, &error);
  require(error.empty(), "EXR binding inspection reported an error");
  require(bindings.size() == 5,
          "Unsupported subsampled layer was not filtered from bindings");

  const ExrLevel::Binding &beauty = binding(bindings, "name:beauty", "");
  require(beauty.primary && !beauty.raw,
          "Beauty binding primary/raw defaults are wrong");
  require(binding(bindings, "name:beauty", "normal").raw,
          "Normal binding was not classified as raw data");
  require(binding(bindings, "name:beauty", "depth").raw,
          "Depth binding was not classified as raw data");
  require(binding(bindings, "name:normal", "").raw,
          "Multipart normal RGB binding was not classified as raw data");
  require(binding(bindings, "name:depth", "").raw,
          "Multipart depth Y binding was not classified as raw data");
}

void testRetainedBuilder(const TFilePath &path) {
  TXshExrLevel level(L"EXR Normal Builder");
  level.setPath(path, true);
  TXshExrLevel::Selection selection;
  selection.part  = "name:beauty";
  selection.layer = "normal";
  selection.raw   = true;
  level.setSelection(selection, true);
  level.load();

  std::vector<TFrameId> fids;
  level.getFids(fids);
  require(fids.size() == 1, "Retained EXR builder lost its source frame");

  TRasterImageP image =
      level.getFrame(fids.front(), ImageManager::isFloatEnabled, 2);
  require(image && (TRasterFP)image->getRaster(),
          "Float-enabled EXR request did not return a float raster");
  require(image->getRaster()->getLx() == 3 &&
              image->getRaster()->getLy() == 2 && image->getSubsampling() == 2,
          "Retained EXR builder ignored explicit subsampling");
  require(image->getSavebox() == TRect(1, 1, 2, 1),
          "Subsampled EXR savebox is wrong");
  expect(((TRasterFP)image->getRaster())->pixels(1)[1].r, -1.0f,
         "Subsampled raw normal data was gamma-adjusted or clamped");

  image = level.getFrame(fids.front(), ImageManager::none, 1);
  require(
      image && (TRaster32P)image->getRaster() && image->getSubsampling() == 1,
      "Default EXR request did not rebuild as a full-size 32-bit raster");

  image = level.getFrame(fids.front(), ImageManager::is64bitEnabled, 1);
  require(image && (TRaster64P)image->getRaster(),
          "64-bit-enabled EXR request did not return a 64-bit raster");

  image = level.getFrame(fids.front(), ImageManager::isFloatEnabled, 1);
  require(image && (TRasterFP)image->getRaster(),
          "Float EXR request did not rebuild after an integer request");
}

void testSequenceLoadingRange(const QString &sourcePath,
                              const QTemporaryDir &directory) {
  const QString firstPath = directory.filePath("sequence.0001.exr");
  const QString thirdPath = directory.filePath("sequence.0003.exr");
  require(QFile::copy(sourcePath, firstPath),
          "Cannot create first EXR sequence frame");
  writeFixtureWithoutNormalPart(thirdPath);

  const TFilePath firstFrame(firstPath.toStdWString());
  const TFilePath levelPath =
      firstFrame.getParentDir() + TFilePath(firstFrame.getLevelNameW());
  TXshExrLevel probe(L"EXR Sequence Probe");
  probe.setPath(levelPath, true);
  probe.load();
  std::vector<TFrameId> availableFids;
  probe.getFids(availableFids);
  if (availableFids.size() != 2) {
    std::string detail = "EXR sequence fixture produced " +
                         std::to_string(availableFids.size()) + " frame(s):";
    for (const TFrameId &fid : availableFids)
      detail +=
          " " + std::to_string(fid.getNumber()) + fid.getLetter().toStdString();
    detail += " from " + levelPath.getQString().toStdString();
    throw std::runtime_error(detail);
  }

  TXshExrLevel level(L"EXR Subsequence");
  level.setPath(levelPath, true);

  setLoadingLevelRange(TFrameId(2), TFrameId(3));
  level.load(availableFids);

  std::vector<TFrameId> fids;
  level.getFids(fids);
  if (fids.size() != 1 || fids.front() != TFrameId(3)) {
    std::string detail = "EXR loading range returned " +
                         std::to_string(fids.size()) + " frame(s):";
    for (const TFrameId &fid : fids)
      detail +=
          " " + std::to_string(fid.getNumber()) + fid.getLetter().toStdString();
    throw std::runtime_error(detail);
  }
  require(level.isSubsequence(),
          "Dedicated EXR level did not retain subsequence identity");

  TFrameId rangeFrom, rangeTo;
  getLoadingLevelRange(rangeFrom, rangeTo);
  require(rangeTo < rangeFrom,
          "Dedicated EXR load leaked its one-shot loading range");

  TXshExrLevel normalLevel(L"EXR Named Part Sequence");
  normalLevel.setPath(levelPath, true);
  TXshExrLevel::Selection selection;
  selection.part              = "name:normal";
  selection.partIndexFallback = 1;
  selection.raw               = true;
  normalLevel.setSelection(selection, false);
  normalLevel.load();
  require(normalLevel.getFrame(TFrameId(1), ImageManager::isFloatEnabled, 1),
          "Named normal part was not loaded from the matching frame");
  require(!normalLevel.getFrame(TFrameId(3), ImageManager::isFloatEnabled, 1),
          "Missing named normal part silently fell back to another part");

  const QString lateFirstPath = directory.filePath("late.0001.exr");
  const QString lateThirdPath = directory.filePath("late.0003.exr");
  writeFixtureWithoutNormalPart(lateFirstPath);
  require(QFile::copy(sourcePath, lateThirdPath),
          "Cannot create later matching EXR sequence frame");
  const TFilePath lateFirstFrame(lateFirstPath.toStdWString());
  const TFilePath lateLevelPath =
      lateFirstFrame.getParentDir() + TFilePath(lateFirstFrame.getLevelNameW());
  TXshExrLevel lateNormalLevel(L"EXR Late Named Part Sequence");
  lateNormalLevel.setPath(lateLevelPath, true);
  lateNormalLevel.setSelection(selection, false);
  lateNormalLevel.load();
  require(lateNormalLevel.getSelection().part == "name:normal",
          "Missing first-frame part rewrote the stable named selector");
  require(
      !lateNormalLevel.getFrame(TFrameId(1), ImageManager::isFloatEnabled, 1),
      "Missing first-frame normal part fell back by index");
  require(
      lateNormalLevel.getFrame(TFrameId(3), ImageManager::isFloatEnabled, 1),
      "Later matching normal part was lost during canonicalization");

  const QString noBaseFirstPath = directory.filePath("nobase.0001.exr");
  const QString noBaseThirdPath = directory.filePath("nobase.0003.exr");
  require(QFile::copy(sourcePath, noBaseFirstPath),
          "Cannot create base-layer EXR sequence frame");
  writeFixtureWithoutBaseLayer(noBaseThirdPath);
  const TFilePath noBaseFirstFrame(noBaseFirstPath.toStdWString());
  const TFilePath noBaseLevelPath = noBaseFirstFrame.getParentDir() +
                                    TFilePath(noBaseFirstFrame.getLevelNameW());
  TXshExrLevel baseLevel(L"EXR Base Layer Sequence");
  baseLevel.setPath(noBaseLevelPath, true);
  selection.part              = "name:beauty";
  selection.partIndexFallback = 0;
  selection.layer.clear();
  selection.raw = false;
  baseLevel.setSelection(selection, true);
  baseLevel.load();
  require(baseLevel.getFrame(TFrameId(1), ImageManager::isFloatEnabled, 1),
          "Matching sequence base layer did not load");
  require(!baseLevel.getFrame(TFrameId(3), ImageManager::isFloatEnabled, 1),
          "Missing base layer silently switched to a named AOV");

  const QString lateBaseFirstPath = directory.filePath("latebase.0001.exr");
  const QString lateBaseThirdPath = directory.filePath("latebase.0003.exr");
  writeFixtureWithoutBaseLayer(lateBaseFirstPath);
  require(QFile::copy(sourcePath, lateBaseThirdPath),
          "Cannot create later base-layer EXR sequence frame");
  const TFilePath lateBaseFirstFrame(lateBaseFirstPath.toStdWString());
  const TFilePath lateBaseLevelPath =
      lateBaseFirstFrame.getParentDir() +
      TFilePath(lateBaseFirstFrame.getLevelNameW());
  TXshExrLevel lateBaseLevel(L"EXR Late Base Layer Sequence");
  lateBaseLevel.setPath(lateBaseLevelPath, true);
  lateBaseLevel.setSelection(selection, true);
  lateBaseLevel.load();
  require(lateBaseLevel.getSelection().layer.empty(),
          "Missing first-frame base rewrote the stable layer selector");
  require(!lateBaseLevel.getFrame(TFrameId(1), ImageManager::isFloatEnabled, 1),
          "Missing first-frame base silently selected a named AOV");
  require(lateBaseLevel.getFrame(TFrameId(3), ImageManager::isFloatEnabled, 1),
          "Later matching base layer was lost during canonicalization");
}

void testLevelPersistence(const TFilePath &sourcePath,
                          const TFilePath &scenePath) {
  TXshExrLevel original(L"EXR Normal");
  original.setPath(sourcePath, true);
  TXshExrLevel::Selection selection;
  selection.part              = "name:beauty";
  selection.partIndexFallback = 1;
  selection.layer             = "normal";
  selection.raw               = true;
  original.setSelection(selection, false);

  require(original.getType() == OVL_XSHLEVEL,
          "Dedicated EXR level is not an OVL level");
  require(original.isFloatChannelLevel(),
          "Dedicated EXR level is not float-channel enabled");
  require(original.isReadOnly(), "Dedicated EXR level is not read-only");
  original.setIsReadOnly(false);
  require(original.isFrameReadOnly(TFrameId(1)),
          "Dedicated EXR frame became editable through the OVL file check");
  original.updateReadOnly();
  require(original.isReadOnly(),
          "Dedicated EXR level became editable after read-only refresh");

  {
    TOStream stream(scenePath);
    stream << &original;
  }
  TIStream stream(scenePath);
  TPersist *persist = nullptr;
  stream >> persist;
  std::unique_ptr<TPersist> owner(persist);
  auto *restored = dynamic_cast<TXshExrLevel *>(persist);
  require(restored != nullptr,
          "Dedicated EXR level persistence registration failed");
  require(restored->getName() == L"EXR Normal",
          "EXR level name did not round-trip");
  require(restored->getPath() == sourcePath,
          "EXR source path did not round-trip");
  require(restored->getSelection() == selection,
          "EXR part/layer/raw selection did not round-trip");
  require(!restored->isPrimaryBinding(),
          "EXR primary-binding flag did not round-trip");
  require(restored->getType() == OVL_XSHLEVEL &&
              restored->isFloatChannelLevel() && restored->isReadOnly(),
          "Reloaded EXR level lost its OVL/float/read-only invariants");
}
}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  try {
    // OpenToonz normally registers this in initImageIo(). The focused test
    // initializes only the file-type contract it needs for sequence parsing.
    TFileType::declare("exr", TFileType::RASTER_IMAGE);
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create EXR test directory");
    const QString asciiPath = directory.filePath("fixture.exr");
    const QString path =
        directory.filePath(QString::fromUtf8("m\xC3\xBClti.exr"));
    writeFixture(asciiPath);
    require(QFile::rename(asciiPath, path),
            "Cannot create Unicode EXR test path");
    const TFilePath filePath(path.toStdWString());
    testInspection(filePath);
    testScanlineAndInfo(filePath);
    testLayersAndTiled(filePath);
    testBindings(filePath);
    testRetainedBuilder(filePath);
    testSequenceLoadingRange(path, directory);
    testLevelPersistence(
        filePath,
        TFilePath(directory.filePath("exr-level.tnz").toStdWString()));
    std::cout << "Dedicated OpenEXR level-source tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Dedicated OpenEXR level-source test failed: " << error.what()
              << '\n';
    return 1;
  }
}
