#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "tparamuiconcept.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
template <class PIXEL>
void copy3DTransformPixels(TRasterPT<PIXEL> raster,
                           const std::vector<otglb::ColorPixel> &pixels,
                           int yOffset) {
  using Channel        = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  const double round   = std::is_floating_point<Channel>::value ? 0.0 : 0.5;
  raster->lock();
  const int rows = int(pixels.size() / raster->getLx());
  for (int y = 0; y < rows; ++y) {
    auto *row = raster->pixels(y + yOffset);
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p = pixels[std::size_t(y) * raster->getLx() + x];
      row[x]        = PIXEL(
                 Channel(p.r * maximum + round), Channel(p.g * maximum + round),
                 Channel(p.b * maximum + round), Channel(p.alpha * maximum + round));
    }
  }
  raster->unlock();
}
}  // namespace

// Adds an animated model-space transform to a compatible 3D source. The node
// stays in the 3D render path so transforms are applied before projection,
// depth testing and downstream lighting.
class Transform3DFx final : public TStandardRasterFx, public T3DRenderSource {
  FX_PLUGIN_DECLARATION(Transform3DFx)

  T3DSourcePort m_source;
  TDoubleParamP m_positionX, m_positionY, m_positionZ;
  TDoubleParamP m_rotationX, m_rotationY, m_rotationZ;
  TDoubleParamP m_scaleX, m_scaleY, m_scaleZ;
  TIntEnumParamP m_gizmoMode;

  otglb::ModelTransform transform(double frame) const {
    otglb::ModelTransform result;
    result.position = {{m_positionX->getValue(frame),
                        m_positionY->getValue(frame),
                        m_positionZ->getValue(frame)}};
    result.rotation = {{m_rotationX->getValue(frame),
                        m_rotationY->getValue(frame),
                        m_rotationZ->getValue(frame)}};
    result.scale    = {{m_scaleX->getValue(frame) / 100.0,
                        m_scaleY->getValue(frame) / 100.0,
                        m_scaleZ->getValue(frame) / 100.0}};
    return result;
  }

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting                   = nullptr,
      const std::vector<otglb::ModelTransform> *downstream = nullptr) const {
    if (!m_source.isConnected()) return {};
    auto *source = m_source.source();
    if (!source)
      throw std::runtime_error(
          "3D Transformer input is not a compatible 3D source.");

    std::vector<otglb::ModelTransform> transforms;
    transforms.reserve(1 + (downstream ? downstream->size() : 0));
    transforms.push_back(transform(frame));
    if (downstream)
      transforms.insert(transforms.end(), downstream->begin(),
                        downstream->end());
    return source->get3DRenderScene(frame, canceled, lighting, &transforms);
  }

public:
  Transform3DFx()
      : m_positionX(0.0)
      , m_positionY(0.0)
      , m_positionZ(0.0)
      , m_rotationX(0.0)
      , m_rotationY(0.0)
      , m_rotationZ(0.0)
      , m_scaleX(100.0)
      , m_scaleY(100.0)
      , m_scaleZ(100.0)
      , m_gizmoMode(new TIntEnumParam(0, "Translate")) {
    addInputPort("Source", m_source);

    bindParam(this, "positionX", m_positionX);
    bindParam(this, "positionY", m_positionY);
    bindParam(this, "positionZ", m_positionZ);
    bindParam(this, "rotationX", m_rotationX);
    bindParam(this, "rotationY", m_rotationY);
    bindParam(this, "rotationZ", m_rotationZ);
    bindParam(this, "scaleX", m_scaleX);
    bindParam(this, "scaleY", m_scaleY);
    bindParam(this, "scaleZ", m_scaleZ);
    bindParam(this, "gizmoMode", m_gizmoMode);

    m_gizmoMode->addItem(1, "Rotate");
    m_gizmoMode->addItem(2, "Scale");
    for (auto param : {m_rotationX, m_rotationY, m_rotationZ})
      param->setMeasureName("angle");
    for (auto param : {m_scaleX, m_scaleY, m_scaleZ})
      param->setValueRange(0.001, 100000.0);

    enableComputeInFloat(true);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig *lighting = nullptr,
      const std::vector<otglb::ModelTransform> *transforms =
          nullptr) const override {
    return scene(frame, canceled, lighting, transforms);
  }

  void getParamUIs(TParamUIConcept *&concepts, int &length) override {
    concepts             = new TParamUIConcept[length = 1];
    concepts[0].m_type   = TParamUIConcept::TRANSFORM_3D;
    concepts[0].m_label  = "3D Transform";
    concepts[0].m_params = {m_positionX, m_positionY, m_positionZ, m_rotationX,
                            m_rotationY, m_rotationZ, m_scaleX,    m_scaleY,
                            m_scaleZ,    m_gizmoMode};
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    bbox = TRectD();
    if (!m_source.isConnected()) return false;
    try {
      const auto transformed = scene(frame, info.m_isCanceled);
      if (!transformed || transformed->triangles.empty()) return false;
      const auto &bounds = transformed->bounds;
      bbox               = TRectD(bounds[0], bounds[1], bounds[2], bounds[3]);
      return true;
    } catch (const std::exception &) {
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
      const auto transformed = scene(frame, info.m_isCanceled);
      if (!transformed || transformed->triangles.empty()) return;

      otglb::RenderTile request;
      request.width  = tile.getRaster()->getLx();
      request.height = tile.getRaster()->getLy();
      request.x      = tile.m_pos.x;
      request.y      = tile.m_pos.y;
      const auto &a  = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      if (!request.width || !request.height) return;

      const int band =
          std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
      const int height = request.height;
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y      = tile.m_pos.y + y;
        const auto pixels =
            otglb::renderTile(*transformed, request, info.m_isCanceled);
        if (pixels.empty()) {
          tile.getRaster()->clear();
          return;
        }
        if (TRasterFP raster = tile.getRaster())
          copy3DTransformPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copy3DTransformPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copy3DTransformPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("3D Transformer [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(Transform3DFx, "transform3DFx")
