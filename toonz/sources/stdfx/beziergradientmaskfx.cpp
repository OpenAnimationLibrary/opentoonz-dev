#include "tfxparam.h"
#include "tparamset.h"
#include "tparamuiconcept.h"
#include "trop.h"
#include "trasterfx.h"
#include "stdfx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr int kPointCount = 4;
constexpr int kSamplesPerSegment = 24;

struct CubicPath {
  std::array<TPointD, kPointCount> anchors;
  std::array<TPointD, kPointCount> outgoing;
  std::array<TPointD, kPointCount> incoming;
};

TPointD cubicPoint(const TPointD &p0, const TPointD &p1, const TPointD &p2,
                   const TPointD &p3, double t) {
  const double u  = 1.0 - t;
  const double u2 = u * u;
  const double t2 = t * t;
  return p0 * (u2 * u) + p1 * (3.0 * u2 * t) + p2 * (3.0 * u * t2) +
         p3 * (t2 * t);
}

std::vector<TPointD> flatten(const CubicPath &path) {
  std::vector<TPointD> points;
  points.reserve(kPointCount * kSamplesPerSegment);
  for (int segment = 0; segment < kPointCount; ++segment) {
    const int next = (segment + 1) % kPointCount;
    for (int sample = 0; sample < kSamplesPerSegment; ++sample) {
      const double t = static_cast<double>(sample) / kSamplesPerSegment;
      points.push_back(cubicPoint(path.anchors[segment], path.outgoing[segment],
                                  path.incoming[next], path.anchors[next], t));
    }
  }
  return points;
}

double squaredDistanceToSegment(const TPointD &point, const TPointD &start,
                                const TPointD &end) {
  const TPointD edge = end - start;
  const double lengthSquared = edge.x * edge.x + edge.y * edge.y;
  if (lengthSquared == 0.0) {
    const TPointD offset = point - start;
    return offset.x * offset.x + offset.y * offset.y;
  }
  const TPointD offset = point - start;
  const double projection = std::clamp(
      (offset.x * edge.x + offset.y * edge.y) / lengthSquared, 0.0, 1.0);
  const TPointD nearest = start + edge * projection;
  const TPointD delta   = point - nearest;
  return delta.x * delta.x + delta.y * delta.y;
}

bool isInside(const TPointD &point, const std::vector<TPointD> &polygon) {
  bool inside = false;
  for (int index = 0, previous = polygon.size() - 1;
       index < static_cast<int>(polygon.size()); previous = index++) {
    const TPointD &a = polygon[previous];
    const TPointD &b = polygon[index];
    if ((a.y > point.y) == (b.y > point.y)) continue;
    const double crossing = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
    if (point.x < crossing) inside = !inside;
  }
  return inside;
}

double distanceToPath(const TPointD &point, const std::vector<TPointD> &path) {
  double minimum = std::numeric_limits<double>::infinity();
  for (int index = 0; index < static_cast<int>(path.size()); ++index) {
    const TPointD &start = path[index];
    const TPointD &end   = path[(index + 1) % path.size()];
    minimum = std::min(minimum, squaredDistanceToSegment(point, start, end));
  }
  return std::sqrt(minimum);
}

template <class PIXEL>
void setMaskPixel(PIXEL &pixel, double alpha);

template <>
void setMaskPixel(TPixel32 &pixel, double alpha) {
  pixel = TPixel32(255, 255, 255,
                   static_cast<UCHAR>(std::lround(alpha * 255.0)));
}

template <>
void setMaskPixel(TPixel64 &pixel, double alpha) {
  pixel = TPixel64(65535, 65535, 65535,
                   static_cast<USHORT>(std::lround(alpha * 65535.0)));
}

template <>
void setMaskPixel(TPixelF &pixel, double alpha) {
  pixel = TPixelF(1.0F, 1.0F, 1.0F, static_cast<float>(alpha));
}

template <class RASTER, class PIXEL>
void renderMask(const RASTER &raster, const TPointD &tileOrigin,
                const TAffine &renderToWorld,
                const std::vector<TPointD> &flattenedPath, double innerFeather,
                double outerFeather, double opacity, bool invert) {
  raster->lock();
  for (int y = 0; y < raster->getLy(); ++y) {
    PIXEL *pixel = raster->pixels(y);
    for (int x = 0; x < raster->getLx(); ++x, ++pixel) {
      const TPointD worldPoint = renderToWorld * (tileOrigin + TPointD(x, y));
      const double distance = distanceToPath(worldPoint, flattenedPath);
      const double signedDistance =
          isInside(worldPoint, flattenedPath) ? distance : -distance;
      const double featherWidth = innerFeather + outerFeather;
      double alpha = featherWidth > 0.0
                         ? std::clamp((signedDistance + outerFeather) /
                                          featherWidth,
                                      0.0, 1.0)
                         : (signedDistance >= 0.0 ? 1.0 : 0.0);
      if (invert) alpha = 1.0 - alpha;
      setMaskPixel(*pixel, alpha * opacity);
    }
  }
  raster->unlock();
}

}  // namespace

class BezierGradientMaskFx final : public TStandardZeraryFx {
  FX_PLUGIN_DECLARATION(BezierGradientMaskFx)

  TPointParamP m_anchor[kPointCount];
  TPointParamP m_outgoing[kPointCount];
  TPointParamP m_incoming[kPointCount];
  TDoubleParamP m_innerFeather;
  TDoubleParamP m_outerFeather;
  TDoubleParamP m_opacity;
  TBoolParamP m_invert;

  CubicPath getPath(double frame) const;

public:
  BezierGradientMaskFx();

  bool canHandle(const TRenderSettings &, double) override { return true; }
  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &settings) override;
  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &settings) override;
  void getParamUIs(TParamUIConcept *&concepts, int &length) override;
};

BezierGradientMaskFx::BezierGradientMaskFx()
    : m_innerFeather(0.0)
    , m_outerFeather(80.0)
    , m_opacity(100.0)
    , m_invert(false) {
  const double radius = 150.0;
  const double handle = radius * 0.5522847498;
  const std::array<TPointD, kPointCount> anchors = {
      TPointD(0.0, -radius), TPointD(radius, 0.0), TPointD(0.0, radius),
      TPointD(-radius, 0.0)};
  const std::array<TPointD, kPointCount> outgoing = {
      TPointD(handle, -radius), TPointD(radius, handle),
      TPointD(-handle, radius), TPointD(-radius, -handle)};
  const std::array<TPointD, kPointCount> incoming = {
      TPointD(-handle, -radius), TPointD(radius, -handle),
      TPointD(handle, radius), TPointD(-radius, handle)};

  for (int index = 0; index < kPointCount; ++index) {
    m_anchor[index]   = anchors[index];
    m_outgoing[index] = outgoing[index];
    m_incoming[index] = incoming[index];
    for (TPointParamP point : {m_anchor[index], m_outgoing[index],
                               m_incoming[index]}) {
      point->getX()->setMeasureName("fxLength");
      point->getY()->setMeasureName("fxLength");
    }
    bindParam(this, "anchor_" + std::to_string(index + 1), m_anchor[index]);
    bindParam(this, "outgoing_" + std::to_string(index + 1),
              m_outgoing[index]);
    bindParam(this, "incoming_" + std::to_string(index + 1),
              m_incoming[index]);
  }

  m_innerFeather->setMeasureName("fxLength");
  m_outerFeather->setMeasureName("fxLength");
  m_innerFeather->setValueRange(0.0, std::numeric_limits<double>::max());
  m_outerFeather->setValueRange(0.0, std::numeric_limits<double>::max());
  m_opacity->setValueRange(0.0, 100.0);
  bindParam(this, "inner_feather", m_innerFeather);
  bindParam(this, "outer_feather", m_outerFeather);
  bindParam(this, "opacity", m_opacity);
  bindParam(this, "invert", m_invert);
  enableComputeInFloat(true);
}

CubicPath BezierGradientMaskFx::getPath(double frame) const {
  CubicPath path;
  for (int index = 0; index < kPointCount; ++index) {
    path.anchors[index]  = m_anchor[index]->getValue(frame);
    path.outgoing[index] = m_outgoing[index]->getValue(frame);
    path.incoming[index] = m_incoming[index]->getValue(frame);
  }
  return path;
}

bool BezierGradientMaskFx::doGetBBox(double, TRectD &bbox,
                                     const TRenderSettings &) {
  bbox = TConsts::infiniteRectD;
  return true;
}

void BezierGradientMaskFx::doCompute(TTile &tile, double frame,
                                     const TRenderSettings &settings) {
  const std::vector<TPointD> path = flatten(getPath(frame));
  const double innerFeather = m_innerFeather->getValue(frame);
  const double outerFeather = m_outerFeather->getValue(frame);
  const double opacity      = m_opacity->getValue(frame) / 100.0;
  const TPointD tileOrigin = tile.m_pos + tile.getRaster()->getCenterD();
  const TAffine renderToWorld = settings.m_affine.inv();

  if (TRaster32P raster = tile.getRaster())
    renderMask<TRaster32P, TPixel32>(raster, tileOrigin, renderToWorld, path,
                                     innerFeather, outerFeather, opacity,
                                     m_invert->getValue());
  else if (TRaster64P raster = tile.getRaster())
    renderMask<TRaster64P, TPixel64>(raster, tileOrigin, renderToWorld, path,
                                     innerFeather, outerFeather, opacity,
                                     m_invert->getValue());
  else if (TRasterFP raster = tile.getRaster())
    renderMask<TRasterFP, TPixelF>(raster, tileOrigin, renderToWorld, path,
                                   innerFeather, outerFeather, opacity,
                                   m_invert->getValue());
  else
    throw TRopException("Bezier Gradient Mask: unsupported raster type");
}

void BezierGradientMaskFx::getParamUIs(TParamUIConcept *&concepts,
                                       int &length) {
  concepts = new TParamUIConcept[length = kPointCount * 3];
  for (int index = 0; index < kPointCount; ++index) {
    const int first = index * 3;
    concepts[first].m_type  = TParamUIConcept::POINT;
    concepts[first].m_label = "Anchor " + std::to_string(index + 1);
    concepts[first].m_params.push_back(m_anchor[index]);
    concepts[first + 1].m_type  = TParamUIConcept::POINT;
    concepts[first + 1].m_label = "Outgoing Handle " + std::to_string(index + 1);
    concepts[first + 1].m_params.push_back(m_outgoing[index]);
    concepts[first + 2].m_type  = TParamUIConcept::POINT;
    concepts[first + 2].m_label = "Incoming Handle " + std::to_string(index + 1);
    concepts[first + 2].m_params.push_back(m_incoming[index]);
  }
}

FX_PLUGIN_IDENTIFIER(BezierGradientMaskFx, "bezierGradientMaskFx")
