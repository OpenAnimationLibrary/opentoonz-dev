// Exercise the production composable 3D source contract without linking tnzstdfx.
#include "../glbmodelfx.cpp"
#include "../material3dfx.cpp"
#include "../threepointlightfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "tparamcontainer.h"
#include "toonz/scenefx.h"
#include "toonz/toonzscene.h"
#include "toonz/txsheet.h"
#include "toonz/txshzeraryfxcolumn.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

class OrdinaryRasterFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(OrdinaryRasterFx)
public:
  bool doGetBBox(double, TRectD &box, const TRenderSettings &) override {
    box = TRectD(); return false;
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
  check(value != nullptr, "missing 3D FX parameter");
  return value;
}

void writeModel(const QString &path) {
  const auto bytes = triangleGlb(0, true);
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "cannot create GLB fixture");
  check(file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size()) ==
            qint64(bytes.size()), "incomplete GLB fixture write");
}

template <class FX>
TPixel32 center(FX &fx) {
  TRaster32P raster(80, 80);
  TTile tile; tile.setRaster(raster); tile.m_pos = TPointD(-40, -40);
  TRenderSettings settings; settings.m_affine = TScale(.2);
  fx.doCompute(tile, 0, settings);
  return raster->pixels(40)[40];
}

void frontLight(ThreePointLightFx &light) {
  param<TDoubleParam>(light, "ambient")->setValue(0, 0.0);
  param<TDoubleParam>(light, "fillIntensity")->setValue(0, 0.0);
  param<TDoubleParam>(light, "rimIntensity")->setValue(0, 0.0);
  param<TDoubleParam>(light, "keyAzimuth")->setValue(0, 0.0);
  param<TDoubleParam>(light, "keyElevation")->setValue(0, 0.0);
  param<TDoubleParam>(light, "keyIntensity")->setValue(0, 100.0);
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  try {
    check(directory.isValid(), "temporary directory unavailable");
    const QString path = directory.filePath("material-light.glb");
    writeModel(path);
    ToonzScene scene;

    TFxP modelOwner = new GlbModelFx;
    auto &model = *static_cast<GlbModelFx *>(modelOwner.getPointer());
    param<TStringParam>(model, "modelFile")->setValue(path.toStdWString());
    param<TIntEnumParam>(model, "colorMode")->setValue(1);

    TXshZeraryFxColumnP modelColumn = new TXshZeraryFxColumn(1);
    auto *columnFx = modelColumn->getZeraryColumnFx();
    columnFx->setZeraryFx(&model);
    scene.getXsheet()->insertColumn(0, modelColumn.getPointer());

    TFxP materialOwner = new Material3DFx;
    TFxP lightOwner = new ThreePointLightFx;
    auto &material = *static_cast<Material3DFx *>(materialOwner.getPointer());
    auto &light = *static_cast<ThreePointLightFx *>(lightOwner.getPointer());
    check(material.getInputPortName(0) == "Source" &&
              light.getInputPortName(0) == "Source",
          "3D modifier persistence ports are not generic/safe");

    // Existing #184/#186 scenes wrote GLB. Material-era generic lighting must
    // translate that saved token rather than strand existing scenes.
    std::string oldPort = "GLB";
    light.compatibilityTranslatePort(1, 8, oldPort);
    check(oldPort == "Source", "legacy Three-Point Light GLB port was not translated");
    oldPort = "GLB Model";
    light.compatibilityTranslatePort(1, 8, oldPort);
    check(oldPort == "Source", "legacy display-style GLB port was not translated");

    TFxP ordinary = new OrdinaryRasterFx;
    bool rejected = false;
    try { material.getInputPort("Source")->setFx(ordinary.getPointer()); }
    catch (const TException &) { rejected = true; }
    check(rejected && !material.getInputPort("Source")->isConnected(),
          "ordinary raster FX was accepted as a 3D source");

    material.getInputPort("Source")->setFx(&model);
    light.getInputPort("Source")->setFx(&material);
    check(light.getInputPort("Source")->isConnected(),
          "Material -> Light 3D chain was rejected");

    frontLight(light);
    const auto source = center(light);
    check(source.m == 255 && source.r > 245, "source material lighting failed");

    param<TIntEnumParam>(material, "materialMode")->setValue(3);
    const auto normal = center(light);
    check(normal.m == 255 && normal.b > normal.r,
          "Surface Normal material mode was not applied through lighting node");

    TFxP light2Owner = new ThreePointLightFx;
    TFxP material2Owner = new Material3DFx;
    auto &light2 = *static_cast<ThreePointLightFx *>(light2Owner.getPointer());
    auto &material2 = *static_cast<Material3DFx *>(material2Owner.getPointer());
    light2.getInputPort("Source")->setFx(&model);
    material2.getInputPort("Source")->setFx(&light2);
    param<TIntEnumParam>(material2, "materialMode")->setValue(2);
    const auto matcap = center(material2);
    check(matcap.m == 255, "Light -> Material 3D chain failed");

    frontLight(light2);
    param<TIntEnumParam>(material, "materialMode")->setValue(1);
    param<TIntEnumParam>(material2, "materialMode")->setValue(1);
    const auto pbr = center(light);
    const auto reversedPbr = center(material2);
    check(pbr.m == 255 && pbr.r > 0 && pbr == reversedPbr,
          "PBR did not preserve the light rig in both chain orders");

    auto *materialPort = dynamic_cast<T3DSourcePort *>(material.getInputPort(0));
    auto *lightPort = dynamic_cast<T3DSourcePort *>(light2.getInputPort(0));
    check(materialPort && lightPort, "missing typed modifier ports");
    for (auto *port : {materialPort, lightPort}) {
      port->setFx(columnFx);
      check(port->getFx() == columnFx &&
                port->source() == static_cast<T3DRenderSource *>(&model),
            "3D modifier lost the schematic GLB column connection");
      rejected = false;
      try { port->setFx(ordinary.getPointer()); }
      catch (const TException &) { rejected = true; }
      check(rejected && port->getFx() == columnFx,
            "invalid replacement damaged the GLB column connection");
    }
    check(columnFx->getOutputConnectionCount() == 2 &&
              center(light) == pbr && center(material2) == pbr,
          "wrapped GLB source changed PBR output or connection ownership");

    TFxP builtLight = buildSceneFx(&scene, 0.0, lightOwner, false);
    TFxP builtMaterial = buildSceneFx(&scene, 0.0, material2Owner, false);
    check(builtLight && builtMaterial, "FX Settings lost a 3D chain");
    TFxP clonedLightOwner = builtLight->clone(true);
    TFxP clonedMaterialOwner = builtMaterial->clone(true);
    auto *clonedLight = dynamic_cast<ThreePointLightFx *>(clonedLightOwner.getPointer());
    auto *clonedMaterial = dynamic_cast<Material3DFx *>(clonedMaterialOwner.getPointer());
    check(clonedLight && clonedMaterial &&
              clonedLight->getInputPortName(0) == "Source" &&
              clonedMaterial->getInputPortName(0) == "Source" &&
              center(*clonedLight) == pbr && center(*clonedMaterial) == pbr,
          "scene expansion or recursive clone lost the persistence-safe 3D chain");
    check(!buildSceneFx(&scene, 1.0, lightOwner, false) &&
              !buildSceneFx(&scene, 1.0, material2Owner, false),
          "3D modifier chain ignored the GLB column's exposure range");

    materialPort->setFx(nullptr);
    lightPort->setFx(nullptr);
    check(!materialPort->source() && !lightPort->source() &&
              columnFx->getOutputConnectionCount() == 0 &&
              center(light).m == 0 && center(material2).m == 0,
          "3D chain disconnect did not release the column or clear output");

    std::cout << "PASS: persistence-safe Material/Light chains, legacy light "
                 "port translation, raw/column sources, FX Settings expansion, "
                 "render clones and exposure\n";
    return 0;
  } catch (const TException &) {
    std::cerr << "FAIL 3D material/light chain: unexpected FX exception\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "FAIL 3D material/light chain: " << error.what() << '\n';
    return 1;
  }
}
