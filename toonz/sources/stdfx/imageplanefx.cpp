#include "stdfx.h"
#include "t3dsource.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
template <class PIXEL>
void copyPixels(TRasterPT<PIXEL> raster,
                const std::vector<otglb::ColorPixel> &pixels, int offset) {
  using Channel = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  const double round = std::is_floating_point<Channel>::value ? 0.0 : 0.5;
  raster->lock();
  const int width = raster->getLx();
  const int rows = int(pixels.size() / width);
  for (int y = 0; y < rows; ++y) {
    auto *row = raster->pixels(y + offset);
    for (int x = 0; x < width; ++x) {
      const auto &p = pixels[std::size_t(y) * width + x];
      row[x] = PIXEL(Channel(p.r * maximum + round),
                     Channel(p.g * maximum + round),
                     Channel(p.b * maximum + round),
                     Channel(p.alpha * maximum + round));
    }
  }
  raster->unlock();
}
}  // namespace

class ImagePlaneFx final : public TStandardRasterFx, public T3DRenderSource {
  FX_PLUGIN_DECLARATION(ImagePlaneFx)
  TRasterFxPort m_input;

  std::shared_ptr<const otglb::RenderScene> scene(
      double frame, const int *canceled,
      const std::vector<otglb::ModelTransform> *transforms,
      const TRenderSettings *settings) const {
    if (!m_input.isConnected()) return {};
    TRenderSettings neutral = settings ? *settings : TRenderSettings();
    neutral.m_affine = TAffine();
    TRectD bbox;
    if (!m_input->doGetBBox(frame, bbox, neutral) || bbox.isEmpty()) return {};
    if (!std::isfinite(bbox.x0) || !std::isfinite(bbox.y0) ||
        !std::isfinite(bbox.x1) || !std::isfinite(bbox.y1))
      throw std::runtime_error("Image Plane requires a finite input bounding box.");
    const double x0 = std::floor(bbox.x0), y0 = std::floor(bbox.y0);
    const double width = std::ceil(bbox.x1) - x0;
    const double height = std::ceil(bbox.y1) - y0;
    if (!(width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
          width * height <= 16.0 * 1024 * 1024))
      throw std::runtime_error("Image Plane input exceeds the 16 megapixel limit.");
    const int w = int(width), h = int(height);
    TRaster32P raster(w, h);
    raster->clear();
    TTile source(raster, TPointD(x0, y0));
    m_input->compute(source, frame, neutral);
    if (canceled && *canceled) return {};
    auto pixels = std::make_shared<std::vector<otglb::ColorPixel>>(
        std::size_t(w) * h);
    raster->lock();
    for (int y = 0; y < h; ++y) {
      const auto *row = raster->pixels(y);
      for (int x = 0; x < w; ++x) {
        const auto &p = row[x];
        (*pixels)[std::size_t(y) * w + x] =
            {p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, p.m / 255.0f};
      }
    }
    raster->unlock();
    return std::make_shared<otglb::RenderScene>(otglb::prepareImagePlane(
        w, h, std::move(pixels),
        transforms ? *transforms : std::vector<otglb::ModelTransform>{}));
  }

public:
  ImagePlaneFx() {
    addInputPort("Source", m_input);
    enableComputeInFloat(true);
  }

  std::shared_ptr<const otglb::RenderScene> get3DRenderScene(
      double frame, const int *canceled,
      const otglb::LightingRig * = nullptr,
      const std::vector<otglb::ModelTransform> *transforms = nullptr,
      const TRenderSettings *settings = nullptr) const override {
    return scene(frame, canceled, transforms, settings);
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &settings) override {
    bbox = TRectD();
    if (!m_input.isConnected()) return false;
    try {
      TRenderSettings neutral = settings;
      neutral.m_affine = TAffine();
      TRectD source;
      if (!m_input->doGetBBox(frame, source, neutral) || source.isEmpty())
        return false;
      const double width = std::ceil(source.x1) - std::floor(source.x0);
      const double height = std::ceil(source.y1) - std::floor(source.y0);
      if (!(std::isfinite(width) && std::isfinite(height) && width > 0 &&
            height > 0 && width <= 8192 && height <= 8192))
        throw std::runtime_error("Image Plane requires a bounded input.");
      bbox = TRectD(-width / 2, -height / 2, width / 2, height / 2);
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
  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return false;
  }
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    tile.getRaster()->clear();
    if (!m_input.isConnected()) return;
    try {
      const auto result = scene(frame, settings.m_isCanceled, nullptr, &settings);
      if (!result || result->triangles.empty()) return;
      otglb::RenderTile request;
      request.width = tile.getRaster()->getLx();
      request.height = tile.getRaster()->getLy();
      request.x = tile.m_pos.x;
      request.y = tile.m_pos.y;
      const auto &a = settings.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      if (!request.width || !request.height) return;
      const int height = request.height;
      const int band = std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y = tile.m_pos.y + y;
        const auto pixels = otglb::renderTile(*result, request, settings.m_isCanceled);
        if (pixels.empty()) {
          tile.getRaster()->clear();
          return;
        }
        if (TRasterFP raster = tile.getRaster())
          copyPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster())
          copyPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster())
          copyPixels<TPixel32>(raster, pixels, y);
        else
          throw std::runtime_error("Unsupported Image Plane output pixel type.");
      }
    } catch (const std::exception &e) {
      throw TException(QString("Image Plane [%1]: %2")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(e.what()))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(ImagePlaneFx, "imagePlaneFx")
