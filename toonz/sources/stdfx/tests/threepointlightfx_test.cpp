// Exercise the production 3D source contract and Three-Point Light node without
// linking tnzstdfx, matching the GLB Model framework-test pattern.
#include "../glbmodelfx.cpp"
#include "../threepointlightfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "tparamcontainer.h"
#include "toonz/txshzeraryfxcolumn.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

// A concrete, registered raster-only FX is needed to test port rejection.
class OrdinaryRasterFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(OrdinaryRasterFx)

public:
  bool doGetBBox(double, TRectD &box, const TRenderSettings &) override {
    box = TRectD();
    return false;
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  void doCompute(TTile &tile, double, const TRenderSettings &) override {
    tile.getRaster()->clear();
  }
};
FX_PLUGIN_IDENTIFIER(OrdinaryRasterFx, "ordinaryRasterTestFx")

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

void writeModel(const QString &path) {
  // Use the default white material so output color comes from the light.
  // The explicit-material fixture is red and cannot satisfy the white test.
  const auto bytes = triangleGlb();
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

    // FX ports add/release references. Never connect stack-allocated FX.
    TFxP modelOwner = new GlbModelFx;
    auto &model = *static_cast<GlbModelFx *>(modelOwner.getPointer());
    param<TStringParam>(model, "modelFile")->setValue(path.toStdWString());
    param<TIntEnumParam>(model, "colorMode")->setValue(1);

    // The application inserts a zerary column, not the bare model used by the
    // original tests. Keep it alive until after all connected FX are destroyed.
    TXshZeraryFxColumnP modelColumn = new TXshZeraryFxColumn(1);
    auto *columnFx = modelColumn->getZeraryColumnFx();
    columnFx->setZeraryFx(&model);

    TFxP lightOwner = new ThreePointLightFx;
    auto &light = *static_cast<ThreePointLightFx *>(lightOwner.getPointer());
    check(light.getFxType() == "STD_threePointLightFx",
          "unexpected Three-Point Light FX type");
    check(light.getInputPortCount() == 1 &&
              light.getInputPortName(0) == "GLB Model",
          "unexpected Three-Point Light input contract");
    auto *port = dynamic_cast<T3DSourcePort *>(light.getInputPort(0));
    check(port && !port->source() && center(light).m == 0,
          "unconnected lighting node did not render empty");

    bool rejected = false;
    TFxP ordinary = new OrdinaryRasterFx;
    try {
      port->setFx(ordinary.getPointer());
    } catch (const TException &) {
      rejected = true;
    }
    check(rejected && !port->isConnected(),
          "ordinary raster FX was accepted as a 3D source");

    port->setFx(&model);
    check(port->source() == static_cast<T3DRenderSource *>(&model),
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

    // Reproduce the actual schematic connection. The former bare-object type
    // check threw here during FX insertion, before any raster rendering.
    port->setFx(columnFx);
    check(port->getFx() == columnFx &&
              port->source() == static_cast<T3DRenderSource *>(&model),
          "GLB column was rejected or replaced by an untracked raw FX link");
    check(columnFx->getOutputConnectionCount() == 1 &&
              columnFx->getOutputConnection(0) == port && center(light) == white,
          "column ownership or wrapped-source lighting is incorrect");

    // FX Settings and rendering clone the graph. The cloned source must also
    // resolve to 3D without changing the original schematic column connection.
    TFxP clonedOwner = light.clone(true);
    auto *cloned = dynamic_cast<ThreePointLightFx *>(clonedOwner.getPointer());
    check(cloned && center(*cloned) == white && port->getFx() == columnFx,
          "recursive lighting clone lost its GLB source");

    // Rejection must leave the existing valid connection intact.
    TFxP emptyColumn = new TZeraryColumnFx;
    for (TFx *invalid : {ordinary.getPointer(), emptyColumn.getPointer()}) {
      rejected = false;
      try {
        port->setFx(invalid);
      } catch (const TException &) {
        rejected = true;
      }
      check(rejected && port->getFx() == columnFx &&
                columnFx->getOutputConnectionCount() == 1 &&
                columnFx->getOutputConnection(0) == port,
            "invalid input damaged the existing GLB column connection");
    }

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

    port->setFx(nullptr);
    check(!port->source() && columnFx->getOutputConnectionCount() == 0 &&
              center(light).m == 0,
          "disconnect did not release the column or clear the output");

    std::cout << "PASS: Three-Point Light, raw/column sources, recursive clone, "
                 "connection preservation, colored lighting and disconnect\n";
    return 0;
  } catch (const TException &) {
    std::cerr << "FAIL threepointlightfx: unexpected FX exception\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "FAIL threepointlightfx: " << error.what() << '\n';
    return 1;
  }
}
