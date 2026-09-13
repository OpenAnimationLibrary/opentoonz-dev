// Exercise the production FX and CPU renderer. As with lut3dbake_test, compile
// the production source here without
// linking tnzstdfx so the same FX factory registration is tested in isolation.
#include "../glbmodelfx.cpp"
#include "tfilepath.h"
#include "tparamcontainer.h"
#include "tstream.h"
#include "trop.h"
#include "../../glb/tests/glbfixture.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template <class PARAM>
PARAM *parameter(GlbModelFx &fx, const char *name) {
  auto *param = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  require(param != nullptr, "Missing parameter or unexpected parameter type");
  return param;
}

struct DoubleCase {
  const char *name;
  double defaultValue;
  double firstValue;
  double lastValue;
};

const DoubleCase doubleCases[] = {
    {"positionX", 0.0, -2.0, 3.0},
    {"positionY", 0.0, 1.0, 4.0},
    {"positionZ", 0.0, -1.0, 2.0},
    {"rotationX", 0.0, 15.0, 30.0},
    {"rotationY", 0.0, -20.0, 40.0},
    {"rotationZ", 0.0, 10.0, 45.0},
    {"scale", 100.0, 50.0, 150.0},
    {"cameraDistance", 10.0, 5.0, 20.0},
    {"fieldOfView", 45.0, 30.0, 60.0},
    {"orthoSize", 10.0, 5.0, 15.0},
    {"nearClip", 0.1, 0.2, 0.5},
    {"farClip", 1000.0, 100.0, 500.0},
};

struct EnumCase {
  const char *name;
  const char *firstCaption;
  const char *lastCaption;
};

const EnumCase enumCases[] = {
    {"projection", "Orthographic", "Perspective"},
    {"lighting", "Unlit", "Headlight"},
    {"renderStyle", "Solid", "Wireframe"},
};

void expectValue(double actual, double expected) {
  require(std::fabs(actual - expected) < 0.000001,
          "Unexpected parameter value");
}

void testDefaults() {
  GlbModelFx fx;
  require(fx.getFxType() == "STD_glbModelFx", "Unexpected FX type");
  require(fx.isZerary(), "GLB Model is not a zerary FX");
  require(fx.getInputPortCount() == 0, "GLB Model acquired an input port");
  require(fx.getParams()->getParamCount() == 16,
          "Unexpected framework parameter count");
  require(parameter<TStringParam>(fx, "modelFile")->getValue().empty(),
          "Default model path is not empty");
  for (const DoubleCase &test : doubleCases) {
    auto *param = parameter<TDoubleParam>(fx, test.name);
    expectValue(param->getValue(0), test.defaultValue);
    require(param->getKeyframeCount() == 0, "Default parameter has keyframes");
  }
  for (const EnumCase &test : enumCases) {
    auto *param = parameter<TIntEnumParam>(fx, test.name);
    require(param->getValue() == 0, "Unexpected default mode");
    require(param->getItemCount() == 2, "Unexpected mode count");
    int value = -1;
    std::string caption;
    param->getItem(0, value, caption);
    require(value == 0 && caption == test.firstCaption,
            "Unexpected first mode");
    param->getItem(1, value, caption);
    require(value == 1 && caption == test.lastCaption,
            "Unexpected second mode");
  }
}

void populate(GlbModelFx &fx, const QString &path) {
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  for (const DoubleCase &test : doubleCases) {
    auto *param = parameter<TDoubleParam>(fx, test.name);
    param->setValue(0, test.firstValue);
    param->setValue(12, test.lastValue);
  }
  for (const EnumCase &test : enumCases)
    parameter<TIntEnumParam>(fx, test.name)->setValue(1);
}

void verifyPopulated(GlbModelFx &fx, const QString &path) {
  require(fx.isZerary() && fx.getInputPortCount() == 0,
          "Restored FX changed its port contract");
  require(parameter<TStringParam>(fx, "modelFile")->getValue() ==
              path.toStdWString(),
          "Model path did not survive cloning or persistence");
  for (const DoubleCase &test : doubleCases) {
    auto *param = parameter<TDoubleParam>(fx, test.name);
    require(param->getKeyframeCount() == 2 && param->isKeyframe(0) &&
                param->isKeyframe(12),
            "Parameter keyframes did not survive cloning or persistence");
    expectValue(param->getValue(0), test.firstValue);
    expectValue(param->getValue(12), test.lastValue);
  }
  for (const EnumCase &test : enumCases)
    require(parameter<TIntEnumParam>(fx, test.name)->getValue() == 1,
            "Mode did not survive cloning or persistence");
}

void testCloneAndPersistence(const QString &scenePath, const QString &modelPath) {
  GlbModelFx original;
  populate(original, modelPath);
  std::unique_ptr<TFx> clone(original.clone(false));
  auto *cloned = dynamic_cast<GlbModelFx *>(clone.get());
  require(cloned != nullptr, "Factory clone returned the wrong FX type");
  verifyPopulated(*cloned, modelPath);
  parameter<TStringParam>(*cloned, "modelFile")->setValue(L"changed.glb");
  parameter<TDoubleParam>(*cloned, "positionX")->setValue(0, 7.0);
  verifyPopulated(original, modelPath);

  {
    TOStream os(TFilePath(scenePath.toStdWString()));
    // Include the FX type tag so the real persistence factory reloads the node.
    os << &original;
  }
  TIStream is(TFilePath(scenePath.toStdWString()));
  TPersist *loaded = nullptr;
  is >> loaded;
  std::unique_ptr<TPersist> owner(loaded);
  auto *restored = dynamic_cast<GlbModelFx *>(loaded);
  require(restored != nullptr, "FX type tag did not reload GLB Model");
  verifyPopulated(*restored, modelPath);
}

template <class PIXEL>
void testTransparent(GlbModelFx &fx, int bpp) {
  TRasterPT<PIXEL> output(3, 2);
  TTile tile;
  tile.m_pos = TPointD(17, -23);
  tile.setRaster(output);
  TRenderSettings settings;
  settings.m_bpp = bpp;
  for (double frame : {0.0, 6.0, 12.0}) {
    TRectD bbox(1.0, 2.0, 3.0, 4.0);
    require(!fx.doGetBBox(frame, bbox, settings) && bbox.isEmpty(),
            "Framework-only FX reported visible model content");
    for (int y = 0; y < output->getLy(); ++y)
      for (int x = 0; x < output->getLx(); ++x)
        output->pixels(y)[x] = PIXEL(1, 1, 1, 1);
    fx.doCompute(tile, frame, settings);
    for (int y = 0; y < output->getLy(); ++y)
      for (int x = 0; x < output->getLx(); ++x) {
        const PIXEL &pixel = output->pixels(y)[x];
        require(pixel.r == 0 && pixel.g == 0 && pixel.b == 0 && pixel.m == 0,
                "Framework-only FX did not clear output to transparent");
      }
  }
}

void testEmptyRendering() {
  GlbModelFx fx;
  testTransparent<TPixel32>(fx, 32);
  testTransparent<TPixel64>(fx, 64);
  testTransparent<TPixelF>(fx, 128);
}

void writeModel(const QString &path, float offset = 0) {
  const auto bytes = triangleGlb(offset);
  QFile file(path);
  require(file.open(QIODevice::WriteOnly), "Cannot write GLB fixture");
  require(file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) ==
              qint64(bytes.size()), "Incomplete GLB fixture");
}

template <class PIXEL>
void testVisible(GlbModelFx &fx, int bpp) {
  TRasterPT<PIXEL> output(80, 80);
  TTile tile; tile.setRaster(output); tile.m_pos = TPointD(-40, -40);
  TRenderSettings settings; settings.m_bpp = bpp; settings.m_affine = TScale(.2);
  TRectD bbox;
  require(fx.doGetBBox(0, bbox, settings) && !bbox.isEmpty(), "Model has no bounding box");
  expectValue(bbox.x0, -100); expectValue(bbox.x1, 100);
  fx.doCompute(tile, 0, settings);
  const auto center = output->pixels(40)[40];
  require(center.m == PIXEL::maxChannelValue && center.r == center.g && center.g == center.b,
          "Model did not produce opaque grayscale pixels");
  require(std::abs(double(center.r) / PIXEL::maxChannelValue - .75) < .005,
          "Incorrect grayscale conversion for output precision");
  require(output->pixels(0)[0].m == 0, "Background is not transparent");

  // Exercise OpenToonz's actual downstream Over operation at every precision.
  TRasterPT<PIXEL> background(80, 80), composite(80, 80);
  const auto maximum = PIXEL::maxChannelValue;
  background->fill(PIXEL(0, 0, maximum, maximum));
  TRop::over(composite, background, output);
  require(composite->pixels(0)[0].b == maximum && composite->pixels(0)[0].r == 0,
          "Composite lost its background outside the model");
  require(composite->pixels(40)[40].r == center.r && composite->pixels(40)[40].b == center.b,
          "Opaque model did not cover the background");
}

void testRenderingAndReload(const QString &path) {
  writeModel(path);
  QFile file(path); require(file.open(QIODevice::ReadOnly), "Cannot read fixture");
  const auto originalBytes = file.readAll(); file.close();
  GlbModelFx fx;
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  testVisible<TPixel32>(fx, 32); testVisible<TPixel64>(fx, 64); testVisible<TPixelF>(fx, 128);
  // A direct request larger than one internal band must retain the same
  // coordinates and write rows after the first band correctly.
  {
    TRaster32P tall(80, 260); TTile tile; tile.setRaster(tall);
    tile.m_pos = TPointD(-40, -130);
    TRenderSettings settings; settings.m_affine = TScale(.2);
    fx.doCompute(tile, 0, settings);
    require(tall->pixels(130)[40].m == 255 && tall->pixels(150)[40].m == 0,
            "Banded rendering shifted or omitted the model");
    require(fx.getMemoryRequirement(TRectD(0, 0, 4096, 2160), 0, settings) > 0,
            "Renderer did not report temporary memory to the scheduler");
    parameter<TDoubleParam>(fx, "positionX")->setValue(12, 2.0);
    TRectD animated; require(fx.doGetBBox(12, animated, settings), "Animated model disappeared");
    expectValue(animated.x0, 100);
    parameter<TDoubleParam>(fx, "positionX")->setValue(0, 0.0);
  }
  std::unique_ptr<TFx> owner(fx.clone(false));
  auto *clone = dynamic_cast<GlbModelFx *>(owner.get());
  require(clone != nullptr, "Render clone has wrong type");
  testVisible<TPixel32>(*clone, 32);
  require(file.open(QIODevice::ReadOnly), "Source removed during rendering");
  require(file.readAll() == originalBytes, "Rendering modified source GLB"); file.close();
  const TRenderSettings settings;
  const auto alias = fx.getAlias(0, settings);
  writeModel(path, 100); // Changes the file size, even on coarse timestamp filesystems.
  require(alias != fx.getAlias(0, settings), "File replacement did not invalidate FX alias");
  TRectD bbox; require(clone->doGetBBox(0, bbox, settings), "Reload lost model");
  expectValue(bbox.x0, 9900);
  parameter<TDoubleParam>(fx, "positionX")->setValue(0, .00001);
  require(fx.getAlias(0, settings) != clone->getAlias(0, settings), "Small animated change lost from cache key");
  parameter<TDoubleParam>(fx, "farClip")->setValue(0, .05);
  TRaster32P output(4, 4); TTile tile; tile.setRaster(output);
  bool failed = false;
  try { fx.doCompute(tile, 0, settings); } catch (const TException &) { failed = true; }
  require(failed, "Invalid camera did not produce an explicit error");
}

void testLoadError(const QString &path) {
  GlbModelFx fx;
  parameter<TStringParam>(fx, "modelFile")->setValue(path.toStdWString());
  TRaster32P output(4, 4); TTile tile; tile.setRaster(output);
  bool failed = false;
  try { fx.doCompute(tile, 0, TRenderSettings()); } catch (const TException &) { failed = true; }
  require(failed, "Missing or invalid GLB did not report an error");
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir dir;
  try {
    require(dir.isValid(), "Temporary directory unavailable");
    const QString modelPath =
        dir.filePath(QString::fromUtf8("Model \xe8\x89\xb2.glb"));
    require(!QFile::exists(modelPath), "Missing-model fixture already exists");
    testDefaults();
    testCloneAndPersistence(dir.filePath("model.fx"), modelPath);
    testEmptyRendering();
    testLoadError(modelPath);
    require(!QFile::exists(modelPath), "Framework created a model file");

    // An intentionally invalid GLB must report failure and remain untouched.
    const QByteArray sentinel("not a GLB - framework-only fixture\n");
    QFile file(modelPath);
    require(file.open(QIODevice::WriteOnly), "Cannot create model fixture");
    require(file.write(sentinel) == sentinel.size(), "Cannot write fixture");
    file.close();
    testLoadError(modelPath);
    require(file.open(QIODevice::ReadOnly), "Model fixture was removed");
    require(file.readAll() == sentinel, "Framework modified the model file");
    file.close();
    testRenderingAndReload(modelPath);
    std::cout << "PASS: GLB FX registration, zero inputs, parameter defaults, "
                 "keyframes, clone, Unicode persistence, read-only loading, "
                 "8/16/float rendering, reload and explicit errors\n";
    return 0;
  } catch (const TException &error) {
    std::cerr << "FAIL: "
              << QString::fromStdWString(error.getMessage()).toStdString()
              << "\n";
  } catch (const std::exception &error) {
    std::cerr << "FAIL: " << error.what() << "\n";
  }
  return 1;
}
