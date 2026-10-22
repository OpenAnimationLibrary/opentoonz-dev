// Exercise the production 3D source contract and 3D Transformer without
// linking tnzstdfx, matching the other GLB FX framework-test patterns.
#include "../glbmodelfx.cpp"
#include "../transform3dfx.cpp"
#include "../../glb/tests/glbfixture.h"
#include "tparamcontainer.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

class TransformOrdinaryRasterFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(TransformOrdinaryRasterFx)

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
FX_PLUGIN_IDENTIFIER(TransformOrdinaryRasterFx, "transformOrdinaryRasterTestFx")

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void checkNear(double actual, double expected) {
  check(std::abs(actual - expected) < 0.00001,
        "unexpected transformed coordinate");
}

template <class PARAM, class FX>
PARAM *param(FX &fx, const char *name) {
  auto *value = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  check(value != nullptr, "missing 3D Transformer parameter");
  return value;
}

void writeModel(const QString &path) {
  const auto bytes = triangleGlb();
  QFile file(path);
  check(file.open(QIODevice::WriteOnly), "cannot create GLB fixture");
  check(file.write(reinterpret_cast<const char *>(bytes.data()),
                   bytes.size()) == qint64(bytes.size()),
        "incomplete GLB fixture write");
}

std::shared_ptr<const otglb::RenderScene> rendered(Transform3DFx &fx) {
  return fx.get3DRenderScene(0.0, nullptr);
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  try {
    check(directory.isValid(), "temporary directory unavailable");
    const QString path = directory.filePath("transform.glb");
    writeModel(path);

    TFxP modelOwner = new GlbModelFx;
    auto &model     = *static_cast<GlbModelFx *>(modelOwner.getPointer());
    param<TStringParam>(model, "modelFile")->setValue(path.toStdWString());

    TFxP transformOwner = new Transform3DFx;
    auto &transform =
        *static_cast<Transform3DFx *>(transformOwner.getPointer());
    check(transform.getFxType() == "STD_transform3DFx",
          "unexpected 3D Transformer FX type");
    check(transform.getInputPortCount() == 1 &&
              transform.getInputPortName(0) == "Source",
          "3D Transformer persistence port must be Source");
    check(transform.getParams()->getParamCount() == 10,
          "unexpected 3D Transformer parameter count");
    for (const char *name : {"positionX", "positionY", "positionZ", "rotationX",
                             "rotationY", "rotationZ"})
      checkNear(param<TDoubleParam>(transform, name)->getValue(0), 0.0);
    for (const char *name : {"scaleX", "scaleY", "scaleZ"})
      checkNear(param<TDoubleParam>(transform, name)->getValue(0), 100.0);
    check(param<TIntEnumParam>(transform, "gizmoMode")->getItemCount() == 3,
          "gizmo mode does not expose translate, rotate and scale");

    TParamUIConcept *concepts = nullptr;
    int conceptCount          = 0;
    transform.getParamUIs(concepts, conceptCount);
    check(conceptCount == 1 &&
              concepts[0].m_type == TParamUIConcept::TRANSFORM_3D &&
              concepts[0].m_params.size() == 10,
          "3D Transformer did not publish its canvas gizmo");
    delete[] concepts;

    auto *port = dynamic_cast<T3DSourcePort *>(transform.getInputPort(0));
    check(port && !port->source(), "unconnected transformer source is invalid");
    TFxP ordinary = new TransformOrdinaryRasterFx;
    bool rejected = false;
    try {
      port->setFx(ordinary.getPointer());
    } catch (const TException &) {
      rejected = true;
    }
    check(rejected && !port->isConnected(),
          "ordinary raster FX was accepted as a 3D source");

    port->setFx(&model);
    auto scene = rendered(transform);
    check(scene && scene->triangles.size() == 1,
          "connected transformer did not preserve source geometry");
    checkNear(scene->bounds[0], -100.0);
    checkNear(scene->bounds[2], 100.0);

    param<TDoubleParam>(transform, "positionX")->setValue(0, 2.0);
    param<TDoubleParam>(transform, "scaleX")->setValue(0, 200.0);
    scene = rendered(transform);
    checkNear(scene->bounds[0], 0.0);
    checkNear(scene->bounds[2], 400.0);
    checkNear(scene->bounds[1], -100.0);
    checkNear(scene->bounds[3], 100.0);

    param<TDoubleParam>(transform, "rotationZ")->setValue(0, 90.0);
    scene = rendered(transform);
    checkNear(scene->bounds[0], 100.0);
    checkNear(scene->bounds[2], 300.0);
    checkNear(scene->bounds[1], -200.0);
    checkNear(scene->bounds[3], 200.0);

    // Downstream nodes are applied after upstream nodes. This makes multiple
    // transformer nodes composable without flattening intermediate results.
    param<TDoubleParam>(transform, "positionX")->setValue(0, 1.0);
    param<TDoubleParam>(transform, "scaleX")->setValue(0, 100.0);
    param<TDoubleParam>(transform, "rotationZ")->setValue(0, 0.0);
    TFxP downstreamOwner = new Transform3DFx;
    auto &downstream =
        *static_cast<Transform3DFx *>(downstreamOwner.getPointer());
    auto *downstreamPort =
        dynamic_cast<T3DSourcePort *>(downstream.getInputPort(0));
    downstreamPort->setFx(&transform);
    param<TDoubleParam>(downstream, "scaleX")->setValue(0, 200.0);
    scene = rendered(downstream);
    checkNear(scene->bounds[0], 0.0);
    checkNear(scene->bounds[2], 400.0);

    TRectD bbox;
    TRenderSettings settings;
    check(downstream.doGetBBox(0.0, bbox, settings) && !bbox.isEmpty(),
          "transformed 3D source has no bounding box");
    TRaster32P raster(80, 80);
    TTile tile;
    tile.setRaster(raster);
    tile.m_pos        = TPointD(-40, -40);
    settings.m_affine = TScale(0.2);
    downstream.doCompute(tile, 0.0, settings);
    bool visible = false;
    for (int y = 0; y < raster->getLy(); ++y)
      for (int x = 0; x < raster->getLx(); ++x)
        visible |= raster->pixels(y)[x].m != 0;
    check(visible, "3D Transformer raster output is transparent");

    TFxP clonedOwner = downstream.clone(true);
    auto *cloned     = dynamic_cast<Transform3DFx *>(clonedOwner.getPointer());
    check(cloned && cloned->getInputPort("Source") && rendered(*cloned),
          "recursive clone lost the 3D source or transform");

    std::cout
        << "PASS: 3D Transformer parameters, canvas concept, source "
           "validation, nonuniform transforms, chaining, clone and render\n";
    return 0;
  } catch (const TException &) {
    std::cerr << "FAIL transform3dfx: unexpected FX exception\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "FAIL transform3dfx: " << error.what() << '\n';
    return 1;
  }
}
