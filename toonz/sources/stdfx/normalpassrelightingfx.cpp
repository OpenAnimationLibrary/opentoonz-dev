// SPDX-License-Identifier: BSD-3-Clause

#include "stdfx.h"
#include "tfxparam.h"
#include "tparamset.h"
#include "tpixelutils.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr double Pi      = 3.14159265358979323846;
constexpr double Epsilon = 1.0e-12;

struct Vector3 {
  double x, y, z;
};

struct RelightingValues {
  Vector3 lightDirection;
  Vector3 lightColor;
  double normalStrength;
  double lightIntensity;
  double ambient;
  double diffuse;
  double specular;
  double shininess;
  int normalEncoding;
  int outputMode;
  bool flipX;
  bool flipY;
  bool flipZ;
};

double finiteUnit(double value) {
  return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0.0;
}

bool normalize(Vector3 &vector) {
  const double lengthSquared =
      vector.x * vector.x + vector.y * vector.y + vector.z * vector.z;
  if (!std::isfinite(lengthSquared) || lengthSquared <= Epsilon) return false;
  const double inverseLength = 1.0 / std::sqrt(lengthSquared);
  vector.x *= inverseLength;
  vector.y *= inverseLength;
  vector.z *= inverseLength;
  return true;
}

double dot(const Vector3 &left, const Vector3 &right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vector3 lightDirection(double azimuth, double elevation) {
  const double a  = azimuth * Pi / 180.0;
  const double e  = elevation * Pi / 180.0;
  const double ce = std::cos(e);
  return {std::sin(a) * ce, std::sin(e), std::cos(a) * ce};
}

bool decodeNormal(const TPixelF &pixel, const RelightingValues &values,
                  Vector3 &normal, double &alpha) {
  alpha = finiteUnit(pixel.m);
  if (alpha <= Epsilon) return false;
  if (!std::isfinite(pixel.r) || !std::isfinite(pixel.g) ||
      !std::isfinite(pixel.b))
    return false;

  if (values.normalEncoding == 0) {
    // OpenToonz rasters are premultiplied. Recover the stored normal-map
    // components before remapping unsigned RGB into a signed vector.
    normal.x = std::clamp(double(pixel.r) / alpha, 0.0, 1.0) * 2.0 - 1.0;
    normal.y = std::clamp(double(pixel.g) / alpha, 0.0, 1.0) * 2.0 - 1.0;
    normal.z = std::clamp(double(pixel.b) / alpha, 0.0, 1.0) * 2.0 - 1.0;
  } else {
    // Signed AOVs are numeric data rather than premultiplied colors. In
    // particular, dividing negative EXR normals by alpha would corrupt them.
    normal = {pixel.r, pixel.g, pixel.b};
  }

  if (values.flipX) normal.x = -normal.x;
  if (values.flipY) normal.y = -normal.y;
  if (values.flipZ) normal.z = -normal.z;

  if (!std::isfinite(normal.x) || !std::isfinite(normal.y) ||
      !std::isfinite(normal.z) || !normalize(normal))
    return false;

  if (values.normalStrength <= Epsilon) {
    normal = {0.0, 0.0, 1.0};
    return true;
  }

  normal.x *= values.normalStrength;
  normal.y *= values.normalStrength;
  return normalize(normal);
}

double clampedIntegerChannel(double value) {
  if (std::isnan(value)) return 0.0;
  if (!std::isfinite(value)) return value > 0.0 ? 1.0 : 0.0;
  return std::clamp(value, 0.0, 1.0);
}

template <class PIXEL>
void writeRelitPixel(PIXEL &pixel, const Vector3 &rgb, double alpha) {
  using Channel        = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  pixel.r              = Channel(clampedIntegerChannel(rgb.x) * maximum + 0.5);
  pixel.g              = Channel(clampedIntegerChannel(rgb.y) * maximum + 0.5);
  pixel.b              = Channel(clampedIntegerChannel(rgb.z) * maximum + 0.5);
  pixel.m              = Channel(clampedIntegerChannel(alpha) * maximum + 0.5);
}

template <>
void writeRelitPixel<TPixelF>(TPixelF &pixel, const Vector3 &rgb,
                              double alpha) {
  pixel.r = float(rgb.x);
  pixel.g = float(rgb.y);
  pixel.b = float(rgb.z);
  pixel.m = float(alpha);
}

template <class PIXEL>
void relightRaster(const TRasterPT<PIXEL> &output, const TRasterFP &normalMap,
                   bool hasSource, const RelightingValues &values,
                   const int *canceled) {
  const double maximum     = PIXEL::maxChannelValue;
  Vector3 halfVector       = {values.lightDirection.x, values.lightDirection.y,
                              values.lightDirection.z + 1.0};
  const bool hasHalfVector = normalize(halfVector);

  output->lock();
  normalMap->lock();
  for (int y = 0; y < output->getLy(); ++y) {
    if (canceled && *canceled) break;
    PIXEL *outputPixel         = output->pixels(y);
    const TPixelF *normalPixel = normalMap->pixels(y);
    for (int x = 0; x < output->getLx(); ++x, ++outputPixel, ++normalPixel) {
      Vector3 normal;
      double normalAlpha;
      if (!decodeNormal(*normalPixel, values, normal, normalAlpha)) {
        if (!hasSource || values.outputMode != 0)
          *outputPixel = PIXEL::Transparent;
        continue;
      }

      const double sourceAlpha =
          hasSource ? finiteUnit(double(outputPixel->m) / maximum)
                    : normalAlpha;
      const Vector3 source =
          hasSource ? Vector3{double(outputPixel->r) / maximum,
                              double(outputPixel->g) / maximum,
                              double(outputPixel->b) / maximum}
                    : Vector3{sourceAlpha, sourceAlpha, sourceAlpha};

      const double nDotL  = std::max(0.0, dot(normal, values.lightDirection));
      const double direct = values.lightIntensity * nDotL;
      const Vector3 diffuseLight = {
          values.ambient + values.diffuse * direct * values.lightColor.x,
          values.ambient + values.diffuse * direct * values.lightColor.y,
          values.ambient + values.diffuse * direct * values.lightColor.z};

      double specular = 0.0;
      if (values.specular > 0.0 && nDotL > 0.0 && normal.z > 0.0 &&
          hasHalfVector) {
        const double nDotH = std::max(0.0, dot(normal, halfVector));
        specular           = values.specular * values.lightIntensity *
                   std::pow(nDotH, values.shininess);
      }
      const Vector3 lighting = {
          diffuseLight.x + specular * values.lightColor.x,
          diffuseLight.y + specular * values.lightColor.y,
          diffuseLight.z + specular * values.lightColor.z};

      Vector3 target;
      double outputAlpha = sourceAlpha;
      if (values.outputMode == 1) {
        outputAlpha = normalAlpha * (hasSource ? sourceAlpha : 1.0);
        target      = {lighting.x * outputAlpha, lighting.y * outputAlpha,
                       lighting.z * outputAlpha};
      } else if (values.outputMode == 2) {
        outputAlpha = normalAlpha * (hasSource ? sourceAlpha : 1.0);
        target      = {(normal.x * 0.5 + 0.5) * outputAlpha,
                       (normal.y * 0.5 + 0.5) * outputAlpha,
                       (normal.z * 0.5 + 0.5) * outputAlpha};
      } else {
        // Source is already premultiplied. Diffuse/ambient multiplication
        // preserves that representation; the additive specular term must be
        // premultiplied explicitly.
        target = {source.x * diffuseLight.x +
                      specular * values.lightColor.x * sourceAlpha,
                  source.y * diffuseLight.y +
                      specular * values.lightColor.y * sourceAlpha,
                  source.z * diffuseLight.z +
                      specular * values.lightColor.z * sourceAlpha};
      }

      if (hasSource && values.outputMode == 0) {
        // Normal alpha is an effect mask. Invalid/transparent normal pixels
        // therefore leave the source untouched, including its alpha.
        target.x = source.x + normalAlpha * (target.x - source.x);
        target.y = source.y + normalAlpha * (target.y - source.y);
        target.z = source.z + normalAlpha * (target.z - source.z);
      }
      writeRelitPixel(*outputPixel, target, outputAlpha);
    }
  }
  normalMap->unlock();
  output->unlock();
}

}  // namespace

class NormalPassRelightingFx final : public TStandardRasterFx {
  FX_PLUGIN_DECLARATION(NormalPassRelightingFx)

  enum NormalEncoding { UnsignedRgb = 0, SignedXyz };
  enum OutputMode { RelitSource = 0, LightingOnly, NormalPreview };

  TRasterFxPort m_source;
  TRasterFxPort m_normal;
  TIntEnumParamP m_normalEncoding;
  TBoolParamP m_flipX;
  TBoolParamP m_flipY;
  TBoolParamP m_flipZ;
  TDoubleParamP m_normalStrength;
  TDoubleParamP m_lightAzimuth;
  TDoubleParamP m_lightElevation;
  TPixelParamP m_lightColor;
  TDoubleParamP m_lightIntensity;
  TDoubleParamP m_ambient;
  TDoubleParamP m_diffuse;
  TDoubleParamP m_specular;
  TDoubleParamP m_shininess;
  TIntEnumParamP m_outputMode;

public:
  NormalPassRelightingFx()
      : m_normalEncoding(new TIntEnumParam(UnsignedRgb, "Unsigned RGB [0..1]"))
      , m_flipX(false)
      , m_flipY(false)
      , m_flipZ(false)
      , m_normalStrength(100.0)
      , m_lightAzimuth(-45.0)
      , m_lightElevation(35.0)
      , m_lightColor(TPixel32::White)
      , m_lightIntensity(100.0)
      , m_ambient(10.0)
      , m_diffuse(100.0)
      , m_specular(25.0)
      , m_shininess(32.0)
      , m_outputMode(new TIntEnumParam(RelitSource, "Relit Source")) {
    addInputPort("Source", m_source);
    addInputPort("Normal", m_normal);

    m_normalEncoding->addItem(SignedXyz, "Signed XYZ [-1..1]");
    m_outputMode->addItem(LightingOnly, "Lighting Only");
    m_outputMode->addItem(NormalPreview, "Normal Preview");

    bindParam(this, "normalEncoding", m_normalEncoding);
    bindParam(this, "flipX", m_flipX);
    bindParam(this, "flipY", m_flipY);
    bindParam(this, "flipZ", m_flipZ);
    bindParam(this, "normalStrength", m_normalStrength);
    bindParam(this, "lightAzimuth", m_lightAzimuth);
    bindParam(this, "lightElevation", m_lightElevation);
    bindParam(this, "lightColor", m_lightColor);
    bindParam(this, "lightIntensity", m_lightIntensity);
    bindParam(this, "ambient", m_ambient);
    bindParam(this, "diffuse", m_diffuse);
    bindParam(this, "specular", m_specular);
    bindParam(this, "shininess", m_shininess);
    bindParam(this, "outputMode", m_outputMode);

    m_normalStrength->setValueRange(0.0, 1000.0);
    m_lightAzimuth->setValueRange(-360.0, 360.0);
    m_lightElevation->setValueRange(-90.0, 90.0);
    m_lightIntensity->setValueRange(0.0, 1000.0);
    m_ambient->setValueRange(0.0, 1000.0);
    m_diffuse->setValueRange(0.0, 1000.0);
    m_specular->setValueRange(0.0, 1000.0);
    m_shininess->setValueRange(1.0, 1024.0);
    m_lightAzimuth->setMeasureName("angle");
    m_lightElevation->setMeasureName("angle");
    m_lightColor->enableMatte(false);

    enableComputeInFloat(true);
  }

  bool doGetBBox(double frame, TRectD &bbox,
                 const TRenderSettings &info) override {
    if (m_source.isConnected()) return m_source->doGetBBox(frame, bbox, info);
    if (m_normal.isConnected()) return m_normal->doGetBBox(frame, bbox, info);
    bbox = TRectD();
    return false;
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  void transform(double, int port, const TRectD &rectOnOutput,
                 const TRenderSettings &infoOnOutput, TRectD &rectOnInput,
                 TRenderSettings &infoOnInput) override {
    rectOnInput = rectOnOutput;
    infoOnInput = infoOnOutput;
    if (port == 1) {
      infoOnInput.m_bpp              = 128;
      infoOnInput.m_linearColorSpace = false;
    }
  }

  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    return m_normal.isConnected() ? TRasterFx::memorySize(rect, 128) : 0;
  }

  bool toBeComputedInLinearColorSpace(bool, bool) const override {
    return true;
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    const bool hasSource = m_source.isConnected();
    const int outputMode = m_outputMode->getValue();

    if (hasSource)
      m_source->compute(tile, frame, info);
    else
      tile.getRaster()->clear();

    if (!m_normal.isConnected()) {
      if (outputMode != RelitSource) tile.getRaster()->clear();
      return;
    }
    if (info.m_isCanceled && *info.m_isCanceled) return;

    TRasterFP normalRaster(tile.getRaster()->getLx(),
                           tile.getRaster()->getLy());
    normalRaster->clear();
    normalRaster->setLinear(false);
    TTile normalTile(normalRaster, tile.m_pos);
    TRenderSettings normalInfo(info);
    normalInfo.m_bpp              = 128;
    normalInfo.m_linearColorSpace = false;
    m_normal->compute(normalTile, frame, normalInfo);
    if (info.m_isCanceled && *info.m_isCanceled) return;

    TPixelF color = toPixelF(m_lightColor->getValueD(frame));
    if (tile.getRaster()->isLinear())
      color = toLinear(color, info.m_colorSpaceGamma);
    RelightingValues values = {
        lightDirection(m_lightAzimuth->getValue(frame),
                       m_lightElevation->getValue(frame)),
        {color.r, color.g, color.b},
        m_normalStrength->getValue(frame) / 100.0,
        m_lightIntensity->getValue(frame) / 100.0,
        m_ambient->getValue(frame) / 100.0,
        m_diffuse->getValue(frame) / 100.0,
        m_specular->getValue(frame) / 100.0,
        m_shininess->getValue(frame),
        m_normalEncoding->getValue(),
        outputMode,
        m_flipX->getValue(),
        m_flipY->getValue(),
        m_flipZ->getValue()};

    if (TRasterFP raster = tile.getRaster())
      relightRaster<TPixelF>(raster, normalRaster, hasSource, values,
                             info.m_isCanceled);
    else if (TRaster64P raster = tile.getRaster())
      relightRaster<TPixel64>(raster, normalRaster, hasSource, values,
                              info.m_isCanceled);
    else if (TRaster32P raster = tile.getRaster())
      relightRaster<TPixel32>(raster, normalRaster, hasSource, values,
                              info.m_isCanceled);
    else
      throw TException("NormalPassRelightingFx: unsupported pixel type");
  }
};

FX_PLUGIN_IDENTIFIER(NormalPassRelightingFx, "normalPassRelightingFx")
