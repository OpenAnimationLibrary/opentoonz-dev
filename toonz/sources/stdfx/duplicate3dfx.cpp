#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
constexpr double Pi = 3.14159265358979323846;

template <class PIXEL>
void copyDuplicatePixels(TRasterPT<PIXEL> raster,
                         const std::vector<otglb::ColorPixel> &pixels,
                         int yOffset) {
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

struct Vec3 { double x, y, z; };

Vec3 rotate(Vec3 p, double x, double y, double z) {
  const double rx = x * Pi / 180.0, ry = y * Pi / 180.0,
               rz = z * Pi / 180.0;
  const double sx = std::sin(rx), cx = std::cos(rx);
  const double sy = std::sin(ry), cy = std::cos(ry);
  const double sz = std::sin(rz), cz = std::cos(rz);
  p = {p.x, cx * p.y - sx * p.z, sx * p.y + cx * p.z};
  p = {cy * p.x + sy * p.z, p.y, -sy * p.x + cy * p.z};
  return {cz * p.x - sz * p.y, sz * p.x + cz * p.y, p.z};
}

void include(std::array<double, 4> &bounds, bool &empty,
             const otglb::ProjectedVertex &p) {
  if (empty) {
    bounds = {{p.x, p.y, p.x, p.y}};
    empty = false;
  } else {
    bounds[0] = std::min(bounds[0], p.x);
    bounds[1] = std::min(bounds[1], p.y);
    bounds[2] = std::max(bounds[2], p.x);
    bounds[3] = std::max(bounds[3], p.y);
  }
}
}  // namespace

// A:M Duplicator-inspired non-destructive 3D array modifier. Each instance
// applies the per-copy Translate / Rotate / Scale step cumulatively. The
// source instance is copy 0. Pivot provides the sweep center; non-zero rotation
// around an offset pivot naturally creates circular/spiral arrays.
class Duplicate3DFx final : public TStandardRasterFx, public T3DRenderSource {
  FX_PLUGIN_DECLARATION(Duplicate3DFx)

  T3DSourcePort m_source;
  TIntParamP m_count;
  TDoubleParamP m_translateX, m_translateY, m_translateZ;
  TDoubleParamP m_rotateX, m_rotateY, m_rotateZ;
  TDoubleParamP m_scale;
  TDoubleParamP m_pivotX, m_pivotY, m_pivotZ;

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled,
      const T3DRenderContext &context = T3DRenderContext()) const {
    if (!m_source.isConnected()) return {};
    auto *source = m_source.source();
    if (!source)
      throw std::runtime_error("Duplicate input is not a compatible 3D source.");
    const auto base = source->get3DRenderScene(frame, canceled, context);
    if (!base || base->triangles.empty()) return base;

    const int count = m_count->getValue();
    const double tx = m_translateX->getValue(frame);
    const double ty = m_translateY->getValue(frame);
    const double tz = m_translateZ->getValue(frame);
    const double rx = m_rotateX->getValue(frame);
    const double ry = m_rotateY->getValue(frame);
    const double rz = m_rotateZ->getValue(frame);
    const double stepScale = m_scale->getValue(frame) / 100.0;
    const Vec3 pivot{m_pivotX->getValue(frame), m_pivotY->getValue(frame),
                     m_pivotZ->getValue(frame)};
    if (count < 1 || count > 1000 || !std::isfinite(stepScale) || stepScale <= 0)
      throw std::runtime_error("Duplicate requires Count 1-1000 and positive finite Scale.");

    auto out = std::make_shared<otglb::RenderScene>();
    out->wireframe = base->wireframe;
    out->warnings = base->warnings;
    const std::size_t limit = 256ull * 1024 * 1024 / sizeof(otglb::RenderTriangle);
    if (base->triangles.size() > limit / std::size_t(count))
      throw std::runtime_error("Duplicate geometry exceeds the 256 MiB render budget.");
    out->triangles.reserve(base->triangles.size() * std::size_t(count));
    bool empty = true;

    for (int copy = 0; copy < count; ++copy) {
      if (canceled && *canceled) return {};
      const double s = std::pow(stepScale, copy);
      if (!std::isfinite(s) || s <= 0 || s > 1e12)
        throw std::runtime_error("Duplicate cumulative scale exceeds the supported range.");
      for (const auto &sourceTriangle : base->triangles) {
        auto triangle = sourceTriangle;
        for (auto &v : triangle.vertices) {
          Vec3 p{v.x - pivot.x, v.y - pivot.y, v.depth - pivot.z};
          p = {p.x * s, p.y * s, p.z * s};
          p = rotate(p, rx * copy, ry * copy, rz * copy);
          v.x = p.x + pivot.x + tx * copy;
          v.y = p.y + pivot.y + ty * copy;
          v.depth = p.z + pivot.z + tz * copy;
          if (!std::isfinite(v.x) || !std::isfinite(v.y) ||
              !std::isfinite(v.depth) || std::abs(v.x) > 1e12 ||
              std::abs(v.y) > 1e12)
            throw std::runtime_error("Duplicate transform exceeds the supported coordinate range.");
          include(out->bounds, empty, v);
        }
        out->triangles.push_back(std::move(triangle));
      }
    }
    return out;
  }

public:
  Duplicate3DFx()
      : m_count(3)
      , m_translateX(100.0), m_translateY(0.0), m_translateZ(0.0)
      , m_rotateX(0.0), m_rotateY(0.0), m_rotateZ(0.0)
      , m_scale(100.0)
      , m_pivotX(0.0), m_pivotY(0.0), m_pivotZ(0.0) {
    // Persistence key: no whitespace. UI can present this as 3D Source.
    addInputPort("Source", m_source);
    bindParam(this, "count", m_count);
    bindParam(this, "translateX", m_translateX);
    bindParam(this, "translateY", m_translateY);
    bindParam(this, "translateZ", m_translateZ);
    bindParam(this, "rotateX", m_rotateX);
    bindParam(this, "rotateY", m_rotateY);
    bindParam(this, "rotateZ", m_rotateZ);
    bindParam(this, "scale", m_scale);
    bindParam(this, "pivotX", m_pivotX);
    bindParam(this, "pivotY", m_pivotY);
    bindParam(this, "pivotZ", m_pivotZ);
    m_count->setValueRange(1, 1000);
    m_scale->setValueRange(0.001, 10000.0);
    for (auto param : {m_rotateX, m_rotateY, m_rotateZ})
      param->setMeasureName("angle");
    enableComputeInFloat(true);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const T3DRenderContext &context = T3DRenderContext()) const override {
    return scene(frame, canceled, context);
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
      bbox = TRectD(-500, -500, 500, 500);
      return true;
    }
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }
  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    const double mb = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                      112.0 / (1024.0 * 1024.0);
    return int(std::min(double(std::numeric_limits<int>::max()), std::ceil(mb)));
  }
  bool toBeComputedInLinearColorSpace(bool, bool) const override { return false; }

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
      request.x = tile.m_pos.x; request.y = tile.m_pos.y;
      const auto &a = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      const int band = std::max(1, std::min(128, 2 * 1024 * 1024 /
                                            std::max(1, request.width)));
      const int height = request.height;
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y = tile.m_pos.y + y;
        const auto pixels = otglb::renderTile(*rendered, request,
                                               info.m_isCanceled);
        if (pixels.empty()) { tile.getRaster()->clear(); return; }
        if (TRasterFP raster = tile.getRaster())
          copyDuplicatePixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copyDuplicatePixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copyDuplicatePixels<TPixel32>(raster, pixels, y);
        else throw std::runtime_error("Unsupported Duplicate 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("Duplicate 3D [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(Duplicate3DFx, "duplicate3dFx")
