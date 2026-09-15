#include "stdfx.h"
#include "t3dsource.h"
#include "tfxparam.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
template <class PIXEL>
void copyMaterial3DPixels(TRasterPT<PIXEL> raster,
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
}  // namespace

// Generic 3D material/shading stage. PBR is the first real material model, while
// Matcap and Surface Normal remain useful diagnostics. The node is deliberately
// generic so A:M-native and other material models can be added without growing
// GLB Model itself.
class Material3DFx final : public TStandardRasterFx, public T3DRenderSource {
  FX_PLUGIN_DECLARATION(Material3DFx)

  T3DSourcePort m_source;
  TIntEnumParamP m_mode;
  TDoubleParamP m_metallicScale, m_roughnessScale, m_exposure;

  otglb::MaterialRig material(double frame) const {
    otglb::MaterialRig result;
    result.shader = static_cast<otglb::MaterialShader>(m_mode->getValue());
    result.metallicScale = m_metallicScale->getValue(frame) / 100.0;
    result.roughnessScale = m_roughnessScale->getValue(frame) / 100.0;
    result.exposure = m_exposure->getValue(frame);
    return result;
  }

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled,
      const T3DRenderContext &incoming = T3DRenderContext()) const {
    if (!m_source.isConnected()) return {};
    auto *source = m_source.source();
    if (!source)
      throw std::runtime_error("Material input is not a compatible 3D source.");
    T3DRenderContext context = incoming;
    // Downstream material state wins. This allows a Material node nearer the
    // final output to override an upstream Material node without ambiguity.
    if (!context.hasMaterial) {
      context.hasMaterial = true;
      context.material = material(frame);
    }
    return source->get3DRenderScene(frame, canceled, context);
  }

public:
  Material3DFx()
      : m_mode(new TIntEnumParam(0, "Source Material"))
      , m_metallicScale(100.0)
      , m_roughnessScale(100.0)
      , m_exposure(1.0) {
    // Port names are persistent scene identifiers. Keep this token free of
    // whitespace: scene serialization tokenizes port names at whitespace.
    // The UI can describe it as a 3D Source without changing the saved token.
    addInputPort("Source", m_source);
    bindParam(this, "materialMode", m_mode);
    bindParam(this, "metallicScale", m_metallicScale);
    bindParam(this, "roughnessScale", m_roughnessScale);
    bindParam(this, "exposure", m_exposure);

    m_mode->addItem(1, "Metallic-Roughness PBR");
    m_mode->addItem(2, "Matcap");
    m_mode->addItem(3, "Surface Normal");
    m_metallicScale->setValueRange(0.0, 200.0);
    m_roughnessScale->setValueRange(0.0, 200.0);
    m_exposure->setValueRange(0.0, 20.0);
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
    const double megabytes = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                             112.0 / (1024.0 * 1024.0);
    return int(std::min(double(std::numeric_limits<int>::max()),
                        std::ceil(megabytes)));
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
          copyMaterial3DPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copyMaterial3DPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copyMaterial3DPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported 3D output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("Material 3D [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(Material3DFx, "material3dFx")
