// Exercise the framework directly without loading GLB data or starting a GPU
// renderer. As with lut3dbake_test, compile the production source here without
// linking tnzstdfx so the same FX factory registration is tested in isolation.
#include "../glbmodelfx.cpp"
#include "tfilepath.h"
#include "tparamcontainer.h"
#include "tstream.h"

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

void testNoRendering(const QString &modelPath) {
  GlbModelFx fx;
  testTransparent<TPixel32>(fx, 32);
  testTransparent<TPixel64>(fx, 64);
  testTransparent<TPixelF>(fx, 128);
  populate(fx, modelPath);
  testTransparent<TPixel32>(fx, 32);
  testTransparent<TPixel64>(fx, 64);
  testTransparent<TPixelF>(fx, 128);
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
    testNoRendering(modelPath);
    require(!QFile::exists(modelPath), "Framework created a model file");

    // An intentionally invalid GLB must also remain untouched and unparsed.
    const QByteArray sentinel("not a GLB - framework-only fixture\n");
    QFile file(modelPath);
    require(file.open(QIODevice::WriteOnly), "Cannot create model fixture");
    require(file.write(sentinel) == sentinel.size(), "Cannot write fixture");
    file.close();
    testNoRendering(modelPath);
    require(file.open(QIODevice::ReadOnly), "Model fixture was removed");
    require(file.readAll() == sentinel, "Framework modified the model file");
    std::cout << "PASS: GLB FX registration, zero inputs, parameter defaults, "
                 "keyframes, clone, Unicode persistence and transparent "
                 "8/16/float output without GLB processing\n";
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
