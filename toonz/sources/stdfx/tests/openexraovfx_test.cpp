// Exercise the production FX directly, including OpenEXR multipart and tiled
// input.  The source is included so the production FX registration is tested
// without linking the tnzstdfx plugin into this focused executable.
#include "../openexraovfx.cpp"

#include <OpenEXR/ImfMultiPartOutputFile.h>
#include <OpenEXR/ImfOutputPart.h>
#include <OpenEXR/ImfTiledOutputPart.h>
#include <Imath/half.h>

#include "tfilepath.h"
#include "tstream.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace ExrTest = OPENEXR_IMF_NAMESPACE;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void expect(float actual, float expected, const char *message) {
  if (std::fabs(actual - expected) > 0.00001f)
    throw std::runtime_error(message);
}

template <class PARAM>
PARAM *parameter(OpenExrAovFx &fx, const char *name) {
  auto *param = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  require(param != nullptr, "Missing parameter or unexpected parameter type");
  return param;
}

struct PlaneData {
  std::string name;
  std::vector<float> values;
  ExrTest::PixelType type = ExrTest::FLOAT;
  std::vector<IMATH_NAMESPACE::half> halfValues;
  std::vector<std::uint32_t> uintValues;
};

void addChannels(ExrTest::Header &header,
                 const std::vector<PlaneData> &planes) {
  for (const PlaneData &plane : planes)
    header.channels().insert(plane.name, ExrTest::Channel(plane.type));
}

ExrTest::FrameBuffer frameBuffer(std::vector<PlaneData> &planes,
                                 const IMATH_NAMESPACE::Box2i &dataWindow) {
  const std::size_t width =
      std::size_t(dataWindow.max.x - dataWindow.min.x + 1);
  ExrTest::FrameBuffer result;
  for (PlaneData &plane : planes) {
    void *data              = plane.values.data();
    std::size_t pixelStride = sizeof(float);
    if (plane.type == ExrTest::HALF) {
      plane.halfValues.assign(plane.values.begin(), plane.values.end());
      data        = plane.halfValues.data();
      pixelStride = sizeof(IMATH_NAMESPACE::half);
    } else if (plane.type == ExrTest::UINT) {
      plane.uintValues.clear();
      for (float value : plane.values)
        plane.uintValues.push_back(std::uint32_t(std::max(0.0f, value)));
      data        = plane.uintValues.data();
      pixelStride = sizeof(std::uint32_t);
    }
    result.insert(plane.name,
                  ExrTest::Slice::Make(plane.type, data, dataWindow,
                                       pixelStride, width * pixelStride));
  }
  return result;
}

void writeFixture(const QString &path) {
  using IMATH_NAMESPACE::Box2i;
  using IMATH_NAMESPACE::V2i;
  const Box2i display(V2i(0, 0), V2i(5, 3));
  const Box2i data(V2i(1, 1), V2i(4, 2));
  constexpr int count = 8;

  std::vector<PlaneData> scanline = {
      {"R", std::vector<float>(count)},
      {"G", std::vector<float>(count, 0.2f)},
      {"B", std::vector<float>(count, 0.3f)},
      {"A", std::vector<float>(count, 0.5f), ExrTest::HALF},
      {"diffuse.R", std::vector<float>(count, 1.0f), ExrTest::HALF},
      {"diffuse.G", std::vector<float>(count, 2.0f)},
      {"diffuse.B", std::vector<float>(count, 3.0f), ExrTest::UINT},
      {"normal.X", std::vector<float>(count, -1.0f), ExrTest::HALF},
      {"normal.Y", std::vector<float>(count, 0.25f), ExrTest::HALF},
      {"normal.Z", std::vector<float>(count, 0.75f), ExrTest::HALF},
      {"Z", std::vector<float>(count)}};
  for (int i = 0; i < count; ++i) {
    scanline[0].values[i]  = 0.1f + float(i) * 0.01f;
    scanline[10].values[i] = float(i) / float(count - 1);
  }
  std::vector<PlaneData> tiled = {{"R", std::vector<float>(count, 0.9f)},
                                  {"G", std::vector<float>(count, 0.8f)},
                                  {"B", std::vector<float>(count, 0.7f)},
                                  {"A", std::vector<float>(count, 1.0f)}};

  std::vector<ExrTest::Header> headers;
  headers.emplace_back(display, data);
  headers.back().setName("beauty");
  headers.back().setType(ExrTest::SCANLINEIMAGE);
  addChannels(headers.back(), scanline);
  headers.emplace_back(display, data);
  headers.back().setName("tiles");
  headers.back().setType(ExrTest::TILEDIMAGE);
  headers.back().setTileDescription(
      ExrTest::TileDescription(2, 2, ExrTest::ONE_LEVEL));
  addChannels(headers.back(), tiled);

  ExrTest::MultiPartOutputFile output(path.toUtf8().constData(), headers.data(),
                                      int(headers.size()));
  ExrTest::OutputPart scanlinePart(output, 0);
  const ExrTest::FrameBuffer scanlineBuffer = frameBuffer(scanline, data);
  scanlinePart.setFrameBuffer(scanlineBuffer);
  scanlinePart.writePixels(data.max.y - data.min.y + 1);

  ExrTest::TiledOutputPart tiledPart(output, 1);
  const ExrTest::FrameBuffer tiledBuffer = frameBuffer(tiled, data);
  tiledPart.setFrameBuffer(tiledBuffer);
  tiledPart.writeTiles(0, tiledPart.numXTiles() - 1, 0,
                       tiledPart.numYTiles() - 1);
}

TRasterFP renderFloat(OpenExrAovFx &fx, double frame = 0.0) {
  TRasterFP raster(6, 4);
  raster->setLinear(false);
  TTile tile(raster, TPointD(-3.0, -2.0));
  TRenderSettings settings;
  settings.m_bpp              = 128;
  settings.m_linearColorSpace = false;
  fx.doCompute(tile, frame, settings);
  return raster;
}

TRaster32P render32(OpenExrAovFx &fx, double frame = 0.0) {
  TRaster32P raster(6, 4);
  TTile tile(raster, TPointD(-3.0, -2.0));
  TRenderSettings settings;
  settings.m_bpp = 32;
  fx.doCompute(tile, frame, settings);
  return raster;
}

TPixelF pixel(const TRasterFP &raster, int x, int y) {
  return raster->pixels(y)[x];
}

void testDefaults() {
  OpenExrAovFx fx;
  require(fx.getFxType() == "STD_openExrAovFx", "Unexpected FX type");
  require(fx.isZerary() && fx.getInputPortCount() == 0,
          "EXR AOV must be a zero-input source");
  require(fx.getParams()->getParamCount() == 30,
          "Unexpected EXR AOV parameter count");
  require(parameter<TIntEnumParam>(fx, "outputMode")->getItemCount() == 5,
          "Output mode list is incomplete");
  require(parameter<TIntEnumParam>(fx, "interpretation")->getValue() == 0,
          "Scene-linear color should be the default interpretation");
  require(fx.toBeComputedInLinearColorSpace(false, false),
          "Color interpretation did not request scene-linear computation");
  parameter<TIntEnumParam>(fx, "interpretation")->setValue(1);
  require(!fx.toBeComputedInLinearColorSpace(false, false) &&
              fx.toBeComputedInLinearColorSpace(false, true),
          "Raw interpretation did not preserve the tile color-space state");
  require(parameter<TBoolParam>(fx, "slot1Enabled")->getValue(),
          "The first composite slot should be enabled by default");
}

void configure(OpenExrAovFx &fx, const QString &pattern) {
  parameter<TStringParam>(fx, "exrFile")->setValue(pattern.toStdWString());
  parameter<TIntEnumParam>(fx, "interpretation")->setValue(1);
}

void testDiscovery(OpenExrAovFx &fx) {
  const auto parts = fx.getAovChoices(TFxAovChoiceKind::Part, 0.0);
  require(parts.size() == 2 && parts[0].value == L"beauty" &&
              parts[1].value == L"tiles",
          "Multipart choices are not stable names");
  const auto layers = fx.getAovChoices(TFxAovChoiceKind::Layer, 0.0);
  require(layers.size() == 3 && layers[0].value.empty(),
          "Base and named layer discovery failed");
  bool normalFound = false;
  for (const auto &choice : layers) normalFound |= choice.value == L"normal";
  require(normalFound, "Normal layer was not discovered");
  const auto channels = fx.getAovChoices(TFxAovChoiceKind::Channel, 0.0);
  require(channels.size() == 11, "Channel discovery lost AOVs");
  const QString summary = QString::fromStdWString(fx.getAovSummary(0.0));
  require(summary.contains("2. tiles") && summary.contains("tiled"),
          "EXR inspection summary omitted tiled multipart data");
}

void testLayerAndWindows(OpenExrAovFx &fx) {
  TRasterFP raster      = renderFloat(fx);
  const TPixelF outside = pixel(raster, 0, 0);
  expect(outside.r, 0.0f, "Display-window padding is not black");
  expect(outside.m, 0.0f, "Display-window padding is not transparent");
  // OT row 2 maps to the first (top) EXR data scanline.
  const TPixelF base = pixel(raster, 1, 2);
  expect(base.r, 0.1f, "Scanline orientation or R mapping is wrong");
  expect(base.g, 0.2f, "Base G mapping is wrong");
  expect(base.b, 0.3f, "Base B mapping is wrong");
  expect(base.m, 0.5f, "Base A mapping is wrong");

  parameter<TStringParam>(fx, "layer")->setValue(L"normal");
  raster               = renderFloat(fx);
  const TPixelF normal = pixel(raster, 1, 2);
  expect(normal.r, -1.0f, "Float output did not preserve negative normals");
  expect(normal.g, 0.25f, "Normal Y mapping is wrong");
  expect(normal.b, 0.75f, "Normal Z mapping is wrong");
  expect(normal.m, 1.0f, "AOV layer default alpha is not one");
}

void testRoutingAndComposite(OpenExrAovFx &fx) {
  auto *mode = parameter<TIntEnumParam>(fx, "outputMode");
  mode->setValue(1);
  parameter<TStringParam>(fx, "channel")->setValue(L"Z");
  TRasterFP raster    = renderFloat(fx);
  const TPixelF depth = pixel(raster, 1, 2);
  expect(depth.r, 0.0f, "Single-channel routing did not select Z");
  expect(depth.g, depth.r, "Single-channel output is not grayscale");
  expect(depth.m, 1.0f, "Single-channel alpha is not one");

  mode->setValue(2);
  raster                        = renderFloat(fx);
  const TPixelF falseColorPixel = pixel(raster, 1, 2);
  expect(falseColorPixel.r, 0.0f, "False-color low endpoint is not blue");
  expect(falseColorPixel.b, 1.0f, "False-color low endpoint is not blue");

  mode->setValue(3);
  parameter<TStringParam>(fx, "redChannel")->setValue(L"diffuse.R");
  parameter<TStringParam>(fx, "greenChannel")->setValue(L"normal.X");
  parameter<TStringParam>(fx, "blueChannel")->setValue(L"Z");
  parameter<TStringParam>(fx, "alphaChannel")->setValue(L"");
  raster               = renderFloat(fx);
  const TPixelF custom = pixel(raster, 1, 2);
  expect(custom.r, 1.0f, "Custom red routing failed");
  expect(custom.g, -1.0f, "Custom data routing was clamped");
  expect(custom.b, 0.0f, "Custom blue routing failed");
  expect(custom.m, 1.0f, "Blank custom alpha did not default to one");
  const TPixel32 custom8 = render32(fx)->pixels(2)[1];
  require(
      custom8.r == 255 && custom8.g == 0 && custom8.b == 0 && custom8.m == 255,
      "Integer output did not clamp HDR/data channels safely");

  mode->setValue(4);
  parameter<TStringParam>(fx, "slot1Layer")->setValue(L"diffuse");
  parameter<TDoubleParam>(fx, "slot1Gain")->setValue(0, 0.5);
  raster                  = renderFloat(fx);
  const TPixelF composite = pixel(raster, 1, 2);
  expect(composite.r, 0.5f, "Composite gain failed for R");
  expect(composite.g, 1.0f, "Composite gain failed for G");
  expect(composite.b, 1.5f, "Float composite highlights were clamped");
  expect(composite.m, 1.0f, "Composite alpha failed");
}

void testTiledAndClone(OpenExrAovFx &fx) {
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(0);
  parameter<TStringParam>(fx, "part")->setValue(L"tiles");
  parameter<TStringParam>(fx, "layer")->setValue(L"");
  TRasterFP raster    = renderFloat(fx);
  const TPixelF tiled = pixel(raster, 1, 2);
  expect(tiled.r, 0.9f, "Tiled part R decoding failed");
  expect(tiled.g, 0.8f, "Tiled part G decoding failed");
  expect(tiled.b, 0.7f, "Tiled part B decoding failed");

  TFxP cloneOwner(fx.clone());
  auto *clone = dynamic_cast<OpenExrAovFx *>(cloneOwner.getPointer());
  require(clone != nullptr, "FX clone type changed");
  raster = renderFloat(*clone);
  expect(pixel(raster, 1, 2).r, 0.9f, "Cloned FX could not reuse EXR data");
}

void testPersistence(OpenExrAovFx &fx, const QString &saved,
                     const QString &pattern) {
  {
    TOStream stream(TFilePath(saved.toStdWString()));
    stream << &fx;
  }
  TIStream stream(TFilePath(saved.toStdWString()));
  TPersist *persist = nullptr;
  stream >> persist;
  std::unique_ptr<TPersist> owner(persist);
  auto *restored = dynamic_cast<OpenExrAovFx *>(persist);
  require(restored != nullptr, "FX registration did not reload EXR AOV");
  require(parameter<TStringParam>(*restored, "exrFile")->getValue() ==
              pattern.toStdWString(),
          "EXR sequence path did not survive persistence");
  require(parameter<TStringParam>(*restored, "part")->getValue() == L"tiles",
          "EXR part selection did not survive persistence");
  const TRasterFP raster = renderFloat(*restored);
  expect(pixel(raster, 1, 2).r, 0.9f,
         "Reloaded EXR AOV FX did not render its tiled part");
}
}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  try {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create a temporary test directory");
    const QString concrete = directory.filePath("shot.0001.exr");
    writeFixture(concrete);

    OpenExrAovFx fx;
    testDefaults();
    configure(fx, directory.filePath("shot.####.exr"));
    testDiscovery(fx);
    testLayerAndWindows(fx);
    testRoutingAndComposite(fx);
    testTiledAndClone(fx);
    testPersistence(fx, directory.filePath("aov.fx"),
                    directory.filePath("shot.####.exr"));
    std::cout << "OpenEXR AOV FX regression tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "OpenEXR AOV FX regression test failed: " << error.what()
              << '\n';
    return 1;
  }
}
