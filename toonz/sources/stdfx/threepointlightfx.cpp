#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tparamset.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
constexpr double Pi = 3.14159265358979323846;

template <class PIXEL>
void copy3DPixels(TRasterPT<PIXEL> raster,
                  const std::vector<otglb::ColorPixel> &pixels, int yOffset) {
  using Channel = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  const double round = std::is_floating_point<Channel>::value ? 0.0 : 0.5;
  raster->lock();
  const int rows = int(pixels.size() / raster->getLx());
  for (int y = 0; y < rows; ++y) {
    auto *row = raster->pixels(y + yOffset);
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p = pixels[std::size_t(y) * raster->getLx() + x];
      row[x] = PIXEL(Channel(p.r * maximum + round),
                     Channel(p.g * maximum + round),
                     Channel(p.b * maximum + round),
                     Channel(p.alpha * maximum + round));
    }
  }
  raster->unlock();
}

std::array<double, 3> lightDirection(double azimuth, double elevation) {
  const double a = azimuth * Pi / 180.0;
  const double e = elevation * Pi / 180.0;
  const double ce = std::cos(e);
  return {{std::sin(a) * ce, std::sin(e), std::cos(a) * ce}};
}

std::array<float, 3> lightColor(const TPixelD &color) {
  return {{float(std::clamp(color.r, 0.0, 1.0)),
           float(std::clamp(color.g, 0.0, 1.0)),
           float(std::clamp(color.b, 0.0, 1.0))}};
}
}  // namespace

// Simple camera-relative diffuse lighting for a 3D source. GLB Model is the
// first production implementation of T3DRenderSource; ordinary raster FX cannot
// be connected to this port. Shadows/specular/PBR response are intentionally
// outside this first node.
class ThreePointLightFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(ThreePointLightFx)

  T3DSourcePort m_source;
  TDoubleParamP m_masterIntensity, m_ambient;
  TDoubleParamP m_keyAzimuth, m_keyElevation, m_keyIntensity;
  TPixelParamP m_keyColor;
  TDoubleParamP m_fillAzimuth, m_fillElevation, m_fillIntensity;
  TPixelParamP m_fillColor;
  TDoubleParamP m_rimAzimuth, m_rimElevation, m_rimIntensity;
  TPixelParamP m_rimColor;

  otglb::DirectionalLight light(double frame, const TDoubleParamP &azimuth,
                                const TDoubleParamP &elevation,
                                const TDoubleParamP &intensity,
                                const TPixelParamP &color) const {
    otglb::DirectionalLight result;
    result.direction = lightDirection(azimuth->getValue(frame),
                                      elevation->getValue(frame));
    result.intensity = intensity->getValue(frame) / 100.0;
    result.color = lightColor(color->getValueD(frame));
    return result;
  }

  otglb::LightingRig lighting(double frame) const {
    otglb::LightingRig result;
    result.master = m_masterIntensity->getValue(frame) / 100.0;
    result.ambient = m_ambient->getValue(frame) / 100.0;
    result.lights.push_back(light(frame, m_keyAzimuth, m_keyElevation,
                                  m_keyIntensity, m_keyColor));
    result.lights.push_back(light(frame, m_fillAzimuth, m_fillElevation,
                                  m_fillIntensity, m_fillColor));
    result.lights.push_back(light(frame, m_rimAzimuth, m_rimElevation,
                                  m_rimIntensity, m_rimColor));
    return result;
  }

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled) const {
    if (!m_source.isConnected()) return {};
    auto *source = m_source.source();
    if (!source)
      throw std::runtime_error("Three-Point Light input is not a compatible 3D source.");
    const auto rig = lighting(frame);
    return source->get3DRenderScene(frame, canceled, &rig);
  }

public:
  ThreePointLightFx()
      : m_masterIntensity(100.0)
      , m_ambient(10.0)
      , m_keyAzimuth(-45.0)
      , m_keyElevation(35.0)
      , m_keyIntensity(100.0)
      , m_keyColor(TPixel32::White)
      , m_fillAzimuth(45.0)
      , m_fillElevation(20.0)
      , m_fillIntensity(40.0)
      , m_fillColor(TPixel32::White)
      , m_rimAzimuth(150.0)
      , m_rimElevation(35.0)
      , m_rimIntensity(65.0)
      , m_rimColor(TPixel32::White) {
    addInputPort("GLB Model", m_source);

    bindParam(this, "masterIntensity", m_masterIntensity);
    bindParam(this, "ambient", m_ambient);
    bindParam(this, "keyAzimuth", m_keyAzimuth);
    bindParam(this, "keyElevation", m_keyElevation);
    bindParam(this, "keyIntensity", m_keyIntensity);
    bindParam(this, "keyColor", m_keyColor);
    bindParam(this, "fillAzimuth", m_fillAzimuth);
    bindParam(this, "fillElevation", m_fillElevation);
    bindParam(this, "fillIntensity", m_fillIntensity);
    bindParam(this, "fillColor", m_fillColor);
    bindParam(this, "rimAzimuth", m_rimAzimuth);
    bindParam(this, "rimElevation", m_rimElevation);
    bindParam(this, "rimIntensity", m_rimIntensity);
    bindParam(this, "rimColor", m_rimColor);

    for (auto param : {m_keyAzimuth, m_keyElevation, m_fillAzimuth,
                       m_fillElevation, m_rimAzimuth, m_rimElevation})
      param->setMeasureName("angle");
    for (auto param : {m_masterIntensity, m_keyIntensity, m_fillIntensity,
                       m_rimIntensity})
      param->setValueRange(0.0, 1000.0);
    m_ambient->setValueRange(0.0, 100.0);
    for (auto param : {m_keyAzimuth, m_fillAzimuth, m_rimAzimuth})
      param->setValueRange(-360.0, 360.0);
    for (auto param : {m_keyElevation, m_fillElevation, m_rimElevation})
      param->setValueRange(-90.0, 90.0);

    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    if (!m_source.isConnected()) return false;
    try {
      const auto rendered = scene(frame, info.m_isCanceled);
      if (!rendered || rendered->triangles.empty()) return false;
      const auto &b = rendered->bounds;
      bbox = TRectD(b[0], b[1], b[2], b[3]);
      return true;
    } catch (const std::exception &) {
      // Preserve an actionable compute error instead of allowing the scheduler
      // to suppress the node after a failed bounding-box probe.
      bbox = TRectD(-500, -500, 500, 500);
      return true;
    }
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    const double megabytes = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                             112.0 / (1024.0 * 1024.0);
    return int(std::min(double(std::numeric_limits<int>::max()),
                        std::ceil(megabytes)));
  }

  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    if (!m_source.isConnected()) return;
    try {
      const auto rendered = scene(frame, info.m_isCanceled);
      if (!rendered || rendered->triangles.empty()) return;

      otglb::RenderTile request;
      request.width = tile.getRaster()->getLx();
      request.height = tile.getRaster()->getLy();
      request.x = tile.m_pos.x;
      request.y = tile.m_pos.y;
      const auto &a = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      if (!request.width || !request.height) return;

      const int band = std::max(
          1, std::min(128, 2 * 1024 * 1024 / request.width));
      const int height = request.height;
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y = tile.m_pos.y + y;
        const auto pixels =
            otglb::renderTile(*rendered, request, info.m_isCanceled);
        if (pixels.empty()) {
          tile.getRaster()->clear();
          return;
        }
        if (TRasterFP raster = tile.getRaster())
          copy3DPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copy3DPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copy3DPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("Three-Point Light [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(ThreePointLightFx, "threePointLightFx")
