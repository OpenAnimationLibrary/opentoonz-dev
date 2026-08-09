#include "stdfx.h"
#include "texception.h"
#include "tfxparam.h"
#include "trop.h"
#include "trasterfx.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

double renderedRadius(const TDoubleParamP &radius, double frame,
                      const TRenderSettings &settings) {
  const double shrink =
      0.5 * (settings.m_shrinkX + settings.m_shrinkY);
  const double scale = std::sqrt(std::abs(settings.m_affine.det()));
  return shrink > 0.0 ? std::abs(radius->getValue(frame) * scale / shrink)
                      : 0.0;
}

class RasterLock final {
  TRasterP m_raster;

public:
  explicit RasterLock(const TRasterP &raster) : m_raster(raster) {
    m_raster->lock();
  }

  ~RasterLock() { m_raster->unlock(); }
};

template <typename Pixel>
bool isOpaque(const Pixel &pixel) {
  return pixel.m > 0;
}

template <typename Pixel>
void preserveContiguousEdges(const TRasterPT<Pixel> &output,
                             const TRasterPT<Pixel> &source,
                             const TRasterPT<Pixel> &reference, int margin) {
  const int width  = source->getLx();
  const int height = source->getLy();
  std::vector<int> distance(width * height, -1);
  std::vector<int> frontier;
  frontier.reserve(width * height);

  for (int y = 0; y < height; ++y) {
    const Pixel *sourceRow = source->pixels(y);
    for (int x = 0; x < width; ++x) {
      if (!isOpaque(sourceRow[x])) continue;

      bool touchesReference = false;
      for (int referenceY = std::max(0, y - 1);
           referenceY <= std::min(height - 1, y + 1) && !touchesReference;
           ++referenceY) {
        const Pixel *referenceRow = reference->pixels(referenceY);
        for (int referenceX = std::max(0, x - 1);
             referenceX <= std::min(width - 1, x + 1); ++referenceX) {
          if (isOpaque(referenceRow[referenceX])) {
            touchesReference = true;
            break;
          }
        }
      }

      if (touchesReference) {
        const int index = y * width + x;
        distance[index] = 0;
        frontier.push_back(index);
      }
    }
  }

  for (size_t next = 0; next < frontier.size(); ++next) {
    const int index = frontier[next];
    const int x     = index % width;
    const int y     = index / width;
    if (distance[index] == margin) continue;

    for (int neighborY = std::max(0, y - 1);
         neighborY <= std::min(height - 1, y + 1); ++neighborY) {
      const Pixel *sourceRow = source->pixels(neighborY);
      for (int neighborX = std::max(0, x - 1);
           neighborX <= std::min(width - 1, x + 1); ++neighborX) {
        const int neighborIndex = neighborY * width + neighborX;
        if (distance[neighborIndex] >= 0 || !isOpaque(sourceRow[neighborX]))
          continue;
        distance[neighborIndex] = distance[index] + 1;
        frontier.push_back(neighborIndex);
      }
    }
  }

  for (int y = 0; y < output->getLy(); ++y) {
    Pixel *outputRow       = output->pixels(y);
    const Pixel *sourceRow = source->pixels(y + margin);
    for (int x = 0; x < output->getLx(); ++x) {
      const int sourceIndex = (y + margin) * width + x + margin;
      if (distance[sourceIndex] >= 0) outputRow[x] = sourceRow[x + margin];
    }
  }
}

void preserveContiguousEdges(const TRasterP &output, const TRasterP &source,
                             const TRasterP &reference, int margin) {
  if (TRaster32P output32 = output) {
    preserveContiguousEdges(output32, TRaster32P(source), TRaster32P(reference),
                            margin);
  } else if (TRaster64P output64 = output) {
    preserveContiguousEdges(output64, TRaster64P(source), TRaster64P(reference),
                            margin);
  } else if (TRasterFP outputFloat = output) {
    preserveContiguousEdges(outputFloat, TRasterFP(source), TRasterFP(reference),
                            margin);
  } else {
    throw TException("ContiguousBlurFx: unsupported raster type");
  }
}

}  // namespace

class ContiguousBlurFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(ContiguousBlurFx)

  TRasterFxPort m_source;
  TRasterFxPort m_reference;
  TDoubleParamP m_radius;

public:
  ContiguousBlurFx() : m_radius(5.0) {
    addInputPort("Source", m_source);
    addInputPort("Reference", m_reference);
    bindParam(this, "radius", m_radius);

    m_radius->setMeasureName("fxLength");
    m_radius->setValueRange(0.0, 100.0);
    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bBox,
                 const TRenderSettings &settings) override {
    if (!m_source.isConnected()) {
      bBox = TRectD();
      return false;
    }

    const bool hasBoundingBox = m_source->doGetBBox(frame, bBox, settings);
    const int radius = tceil(renderedRadius(m_radius, frame, settings));
    if (hasBoundingBox && radius > 0) bBox = bBox.enlarge(radius);
    return hasBoundingBox;
  }

  void transform(double frame, int port, const TRectD &rectOnOutput,
                 const TRenderSettings &settingsOnOutput,
                 TRectD &rectOnInput,
                 TRenderSettings &settingsOnInput) override {
    settingsOnInput = settingsOnOutput;
    const int radius =
        tceil(renderedRadius(m_radius, frame, settingsOnOutput));
    rectOnInput = radius > 0 ? rectOnOutput.enlarge(radius) : rectOnOutput;
  }

  int getMemoryRequirement(const TRectD &rect, double frame,
                           const TRenderSettings &settings) override {
    const int radius = tceil(renderedRadius(m_radius, frame, settings));
    return 2 * TRasterFx::memorySize(rect.enlarge(radius), sizeof(float) * 4);
  }

  bool canHandle(const TRenderSettings &settings, double frame) override {
    return m_radius->getValue(frame) == 0.0 ||
           isAlmostIsotropic(settings.m_affine);
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override {
    if (!m_source.isConnected()) return;

    const int radius = tceil(renderedRadius(m_radius, frame, settings));
    if (radius == 0) {
      m_source->compute(tile, frame, settings);
      return;
    }

    const TDimension outputSize = tile.getRaster()->getSize();
    const TDimension inputSize(outputSize.lx + radius * 2,
                               outputSize.ly + radius * 2);
    const TPointD inputPosition = tile.m_pos - TPointD(radius, radius);
    TTile sourceTile;
    m_source->allocateAndCompute(sourceTile, inputPosition, inputSize,
                                 tile.getRaster(), frame, settings);
    TRop::blur(tile.getRaster(), sourceTile.getRaster(), radius, radius, radius,
               false);

    if (!m_reference.isConnected()) return;

    TTile referenceTile;
    m_reference->allocateAndCompute(referenceTile, inputPosition, inputSize,
                                    tile.getRaster(), frame, settings);
    RasterLock outputLock(tile.getRaster());
    RasterLock sourceLock(sourceTile.getRaster());
    RasterLock referenceLock(referenceTile.getRaster());
    preserveContiguousEdges(tile.getRaster(), sourceTile.getRaster(),
                            referenceTile.getRaster(), radius);
  }
};

FX_PLUGIN_IDENTIFIER(ContiguousBlurFx, "contiguousBlurFx")
