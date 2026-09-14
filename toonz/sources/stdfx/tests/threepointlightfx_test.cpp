// Exercise the production 3D source contract and Three-Point Light node without
// linking tnzstdfx, matching the GLB Model framework-test pattern.
#include "../glbmodelfx.cpp"
#include "../threepointlightfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "tparamcontainer.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template <class PARAM, class FX>
PARAM *param(FX &fx, const char *name) {
  auto *value = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  check(value != nullptr, "missing Three-Point Light parameter");
  return value;
}

class OrdinaryRasterFx final : public TStandardRasterFx {
public:
  std::string getPluginId() const override { return "ordinaryRasterTestFx"; }
  bool doGetBBox(double, TRectD &box, const TRenderSettings &) override {
    box = TRectD();
    return false;
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  void doCompute(TTile &tile, double, const TRenderSettings &) override {
    tile.getRaster()->clear();
  }
};

void writeModel(const QString &path) {
  const auto bytes = triangleGlb(0, true);
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "cannot create GLB fixture");
  check(file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) ==
            qint64(bytes.size()),
        "incomplete GLB fixture write");
}

TPixel32 center(ThreePointLightFx &fx) {
  TRaster32P raster(80, 80);
  TTile tile;
  tile.setRaster(raster);
  tile.m_pos = TPointD(-40, -40);
  TRenderSettings settings;
  settings.m_affine = TScale(.2);
  fx.doCompute(tile, 0, settings);
  return raster->pixels(40)[40];
}

void zeroSecondaryLights(ThreePointLightFx &fx) {
  param<TDoubleParam>(fx, "ambient")->setValue(0, 0.0);
  param<TDoubleParam>(fx, "fillIntensity")->setValue(0, 0.0);
  param<TDoubleParam>(fx, "rimIntensity")->setValue(0, 0.0);
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  try {
    check(directory.isValid(), "temporary directory unavailable");
    const QString path = directory.filePath("three-point.glb");
    writeModel(path);

    GlbModelFx model;
    param<TStringParam>(model, "modelFile")->setValue(path.toStdWString());
    param<TIntEnumParam>(model, "colorMode")->setValue(1);

    ThreePointLightFx light;
    check(light.getFxType() == "STD_threePointLightFx",
          "unexpected Three-Point Light FX type");
    check(light.getInputPortCount() == 1 &&
              light.getInputPortName(0) == "GLB Model",
          "unexpected Three-Point Light input contract");

    bool rejected = false;
    OrdinaryRasterFx ordinary;
    try {
      light.getInputPort("GLB Model")->setFx(&ordinary);
    } catch (const TException &) {
      rejected = true;
    }
    check(rejected && !light.getInputPort("GLB Model")->isConnected(),
          "ordinary raster FX was accepted as a 3D source");

    light.getInputPort("GLB Model")->setFx(&model);
    check(light.getInputPort("GLB Model")->isConnected(),
          "GLB Model was rejected as a 3D source");

    zeroSecondaryLights(light);
    param<TDoubleParam>(light, "keyAzimuth")->setValue(0, 0.0);
    param<TDoubleParam>(light, "keyElevation")->setValue(0, 0.0);
    param<TDoubleParam>(light, "keyIntensity")->setValue(0, 100.0);
    param<TPixelParam>(light, "keyColor")->setValue(0, TPixel32::White);

    TRectD box;
    TRenderSettings settings;
    check(light.doGetBBox(0, box, settings) && !box.isEmpty(),
          "lit model has no bounding box");

    const auto white = center(light);
    check(white.m == 255 && white.r > 245 && white.g > 245 && white.b > 245,
          "front white key did not illuminate the model");

    param<TPixelParam>(light, "keyColor")->setValue(
        0, TPixel32(255, 0, 0, 255));
    const auto red = center(light);
    check(red.m == 255 && red.r > 245 && red.g < 5 && red.b < 5,
          "colored key light did not reach the raster output");

    param<TDoubleParam>(light, "keyAzimuth")->setValue(0, 180.0);
    const auto dark = center(light);
    check(dark.m == 255 && dark.r < 5 && dark.g < 5 && dark.b < 5,
          "rear-facing key should not light the front-facing triangle");

    // Source-side embedded settings remain independent. A downstream rig must
    // not mutate GLB Model's own headlight/color/animation parameters.
    check(param<TIntEnumParam>(model, "lighting")->getValue() == 0 &&
              param<TIntEnumParam>(model, "colorMode")->getValue() == 1,
          "downstream light mutated GLB Model settings");

    std::cout << "6 passed, 0 failed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL threepointlightfx: " << error.what() << '\n';
    return 1;
  }
}
