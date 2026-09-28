// Exercise the production FX registration, raster Source port and connection
// through 3D Transformer, without linking the full tnzstdfx plugin.
#include "../imageplanefx.cpp"
#include "../transform3dfx.cpp"
#include "tparamcontainer.h"
#include "trenderer.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>

#include <cmath>
#include <iostream>
#include <stdexcept>

class PlaneRasterFixture final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(PlaneRasterFixture)

public:
  bool doGetBBox(double, TRectD &box, const TRenderSettings &) override {
    box = TRectD(-4, -4, 4, 4);
    return true;
  }
  bool canHandle(const TRenderSettings &, double) override { return true; }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &) override {
    tile.getRaster()->clear();
    TRaster32P raster = tile.getRaster();
    if (!raster) throw std::runtime_error("expected a 32-bit input raster");
    raster->lock();
    for (int y = 0; y < raster->getLy(); ++y) {
      auto *row = raster->pixels(y);
      for (int x = 0; x < raster->getLx(); ++x) {
        const double px = tile.m_pos.x + x + 0.5;
        const double py = tile.m_pos.y + y + 0.5;
        if (px < 0 && py > -3 && py < 3)
          row[x] = frame > 0 ? TPixel32(0, 0, 255, 255)
                             : TPixel32(255, 0, 0, 255);
      }
    }
    raster->unlock();
  }
};
FX_PLUGIN_IDENTIFIER(PlaneRasterFixture, "planeRasterFixtureTestFx")

namespace {
void check(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

class PlaneRenderPort final : public TRenderPort {
public:
  bool finished = false, failed = false;
  TRasterP result;

  PlaneRenderPort() { setRenderArea(TRectD(-8, -8, 8, 8)); }
  void onRenderRasterCompleted(const RenderData &data) override {
    if (data.m_rasA) result = data.m_rasA->clone();
  }
  void onRenderFailure(const RenderData &, TException &) override {
    failed = true;
  }
  void onRenderFinished(bool canceled = false) override {
    failed = failed || canceled;
    finished = true;
  }
};

TRaster32P render(TRasterFxP fx, double frame) {
  PlaneRenderPort port;
  TRenderer renderer(1);
  renderer.enablePrecomputing(false);
  renderer.addPort(&port);
  TRenderSettings settings;
  TFxPair pair;
  pair.m_frameA = fx;
  renderer.startRendering(frame, settings, pair);
  QElapsedTimer timer;
  timer.start();
  while (!port.finished && timer.elapsed() < 30000) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  renderer.stopRendering(true);
  renderer.removePort(&port);
  check(port.finished && !port.failed && port.result,
        "Image Plane render failed or timed out");
  TRaster32P raster = port.result;
  check(raster != nullptr, "Image Plane did not produce a 32-bit raster");
  return raster;
}

void checkCutout(const TRaster32P &raster, bool blue) {
  bool visible = false, transparent = false;
  for (int y = 0; y < raster->getLy(); ++y)
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p = raster->pixels(y)[x];
      visible |= p.m != 0 && (blue ? p.b != 0 : p.r != 0);
      transparent |= p.m == 0;
    }
  check(visible && transparent,
        "3D Plane render lost the input image or its alpha cutout");
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  try {
    TFxP rasterOwner = new PlaneRasterFixture;
    TFxP planeOwner = new ImagePlaneFx;
    auto &plane = *static_cast<ImagePlaneFx *>(planeOwner.getPointer());
    check(plane.getFxType() == "STD_imagePlaneFx", "FX is not registered as 3D Plane");
    check(plane.getInputPortCount() == 1 &&
              plane.getInputPortName(0) == "Source",
          "Image Plane does not expose a persistent raster Source port");
    plane.getInputPort(0)->setFx(rasterOwner.getPointer());
    TRectD bounds;
    TRenderSettings settings;
    check(plane.doGetBBox(0, bounds, settings) &&
              bounds == TRectD(-4, -4, 4, 4),
          "Image Plane did not publish the input extent");
    checkCutout(render(planeOwner, 0), false);
    checkCutout(render(planeOwner, 1), true);

    TFxP transformOwner = new Transform3DFx;
    auto &transform = *static_cast<Transform3DFx *>(transformOwner.getPointer());
    auto *port = dynamic_cast<T3DSourcePort *>(transform.getInputPort(0));
    check(port != nullptr, "3D Transformer has no 3D source port");
    port->setFx(&plane);
    auto *rotation = dynamic_cast<TDoubleParam *>(
        transform.getParams()->getParam("rotationY"));
    check(rotation != nullptr, "3D Transformer rotation is missing");
    rotation->setValue(0, 45);
    checkCutout(render(transformOwner, 0), false);

    TFxP clonedOwner = transform.clone(true);
    auto *cloned = dynamic_cast<Transform3DFx *>(clonedOwner.getPointer());
    check(cloned && cloned->getInputPort("Source") &&
              cloned->getInputPort("Source")->isConnected(),
          "recursive clone lost the Image Plane connection");
    checkCutout(render(TRasterFxP(clonedOwner), 0), false);
    std::cout << "PASS: Image Plane registration, alpha, animation, 3D connection, render and clone\n";
    return 0;
  } catch (const TException &) {
    std::cerr << "FAIL imageplanefx: unexpected FX exception\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "FAIL imageplanefx: " << error.what() << '\n';
    return 1;
  }
}
