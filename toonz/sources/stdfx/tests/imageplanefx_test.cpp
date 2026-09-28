// Exercise the production FX registration, raster Source port and connection
// through 3D Transformer, without linking the full tnzstdfx plugin.
#include "../imageplanefx.cpp"
#include "../transform3dfx.cpp"
#include "tparamcontainer.h"

#include <QCoreApplication>

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
    TRenderSettings settings;
    auto scene = plane.get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
    check(scene && scene->triangles.size() == 2 && scene->texture &&
              scene->textureWidth == 8 && scene->textureHeight == 8,
          "Image Plane did not build a textured surface from Source");
    check((*scene->texture)[0].alpha == 0 &&
              (*scene->texture)[std::size_t(4) * 8 + 1].r == 1 &&
              (*scene->texture)[std::size_t(4) * 8 + 7].alpha == 0,
          "the Source alpha cutout was lost");
    auto nextFrame = plane.get3DRenderScene(1, nullptr, nullptr, nullptr, &settings);
    check((*nextFrame->texture)[std::size_t(4) * 8 + 1].b == 1,
          "animated Source was not evaluated at the requested frame");

    TFxP transformOwner = new Transform3DFx;
    auto &transform = *static_cast<Transform3DFx *>(transformOwner.getPointer());
    auto *port = dynamic_cast<T3DSourcePort *>(transform.getInputPort(0));
    check(port != nullptr, "3D Transformer has no 3D source port");
    port->setFx(&plane);
    auto *rotation = dynamic_cast<TDoubleParam *>(
        transform.getParams()->getParam("rotationY"));
    check(rotation != nullptr, "3D Transformer rotation is missing");
    rotation->setValue(0, 45);
    scene = transform.get3DRenderScene(0, nullptr, nullptr, nullptr, &settings);
    check(scene && scene->texture && scene->triangles.size() == 2 &&
              scene->bounds[2] - scene->bounds[0] < 8,
          "3D Transformer did not rotate the textured plane");

    TRaster32P output(16, 16);
    TTile tile(output, TPointD(-8, -8));
    transform.doCompute(tile, 0, settings);
    bool visible = false, transparent = false;
    for (int y = 0; y < 16; ++y)
      for (int x = 0; x < 16; ++x) {
        const auto &p = output->pixels(y)[x];
        visible |= p.m != 0;
        transparent |= p.m == 0;
      }
    check(visible && transparent,
          "connected FX did not rasterize an alpha-cut plane");
    TFxP clonedOwner = transform.clone(true);
    auto *cloned = dynamic_cast<Transform3DFx *>(clonedOwner.getPointer());
    check(cloned && cloned->getInputPort("Source") &&
              cloned->get3DRenderScene(0, nullptr, nullptr, nullptr, &settings),
          "recursive clone lost the Image Plane connection");
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
