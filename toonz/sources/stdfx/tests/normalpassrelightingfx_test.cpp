// SPDX-License-Identifier: BSD-3-Clause

// Exercise the production FX through ordinary raster ports.  Encoded 0..1
// fixtures model PNG/TIFF normal maps while signed float fixtures model raw
// OpenEXR XYZ passes; the FX itself must remain independent of either format.
#include "../normalpassrelightingfx.cpp"

#include "tfilepath.h"
#include "tparamcontainer.h"
#include "tstream.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

void expect(float actual, float expected, const char *message,
            float tolerance = 0.0001f) {
  if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance)
    throw std::runtime_error(message);
}

template <class PARAM>
PARAM *parameter(NormalPassRelightingFx &fx, const char *name) {
  auto *result = dynamic_cast<PARAM *>(fx.getParams()->getParam(name));
  require(result != nullptr, "Missing parameter or unexpected parameter type");
  return result;
}

template <class PIXEL>
PIXEL integerPixel(const TPixelF &pixel) {
  using Channel       = typename PIXEL::Channel;
  const float maximum = float(PIXEL::maxChannelValue);
  const auto convert  = [maximum](float value) {
    return Channel(std::lround(std::clamp(value, 0.0f, 1.0f) * maximum));
  };
  return PIXEL(convert(pixel.r), convert(pixel.g), convert(pixel.b),
               convert(pixel.m));
}

// A deterministic raster input which can also enforce the data contract used
// by signed EXR normals.  Production readers have already converted their file
// data into one of these raster types before this FX sees it.
class RasterFixtureFx final : public TStandardRasterFx {
  TPixelF m_pixel;
  bool m_floatOnly;
  std::string m_alias;

public:
  int lastBpp             = 0;
  bool lastWasFloat       = false;
  bool lastWasLinear      = true;
  bool lastSettingsLinear = true;

  RasterFixtureFx(const TPixelF &pixel, bool floatOnly, std::string alias)
      : m_pixel(pixel), m_floatOnly(floatOnly), m_alias(std::move(alias)) {
    enableComputeInFloat(true);
  }

  const TPersistDeclaration *getDeclaration() const override { return nullptr; }

  bool doGetBBox(double, TRectD &bbox, const TRenderSettings &) override {
    bbox = TRectD(0.0, 0.0, 1.0, 1.0);
    return true;
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return tileIsLinear;
  }

  std::string getAlias(double, const TRenderSettings &) const override {
    return m_alias;
  }

  // Child ports normally enter TRasterFx::compute through an active renderer
  // and cache manager. This deterministic fixture bypasses that global render
  // machinery so the focused executable can exercise the production FX in
  // isolation.
  void compute(TTile &tile, double frame,
               const TRenderSettings &settings) override {
    doCompute(tile, frame, settings);
  }

  void doCompute(TTile &tile, double,
                 const TRenderSettings &settings) override {
    lastBpp            = settings.m_bpp;
    lastWasFloat       = bool(TRasterFP(tile.getRaster()));
    lastWasLinear      = tile.getRaster()->isLinear();
    lastSettingsLinear = settings.m_linearColorSpace;
    if (m_floatOnly && !lastWasFloat)
      throw std::runtime_error("Signed normal was not requested as float");

    if (TRasterFP raster = tile.getRaster())
      raster->fill(m_pixel);
    else if (TRaster64P raster = tile.getRaster())
      raster->fill(integerPixel<TPixel64>(m_pixel));
    else if (TRaster32P raster = tile.getRaster())
      raster->fill(integerPixel<TPixel32>(m_pixel));
    else
      throw std::runtime_error("Unexpected fixture raster type");
  }
};

void connect(NormalPassRelightingFx &fx, const char *portName, TFx *input) {
  TFxPort *port = fx.getInputPort(portName);
  require(port != nullptr, "Missing relighting input port");
  port->setFx(input);
}

void disconnect(NormalPassRelightingFx &fx, const char *portName) {
  connect(fx, portName, nullptr);
}

TRasterFP renderFloat(NormalPassRelightingFx &fx) {
  TRasterFP raster(1, 1);
  raster->setLinear(false);
  TTile tile(raster, TPointD());
  TRenderSettings settings;
  settings.m_bpp              = 128;
  settings.m_linearColorSpace = false;
  fx.doCompute(tile, 0.0, settings);
  return raster;
}

TRaster64P render64(NormalPassRelightingFx &fx) {
  TRaster64P raster(1, 1);
  raster->setLinear(false);
  TTile tile(raster, TPointD());
  TRenderSettings settings;
  settings.m_bpp              = 64;
  settings.m_linearColorSpace = false;
  fx.doCompute(tile, 0.0, settings);
  return raster;
}

TRaster32P render32(NormalPassRelightingFx &fx) {
  TRaster32P raster(1, 1);
  raster->setLinear(false);
  TTile tile(raster, TPointD());
  TRenderSettings settings;
  settings.m_bpp              = 32;
  settings.m_linearColorSpace = false;
  fx.doCompute(tile, 0.0, settings);
  return raster;
}

TPixelF normalized(const TPixel32 &pixel) {
  const float scale = 1.0f / float(TPixel32::maxChannelValue);
  return TPixelF(pixel.r * scale, pixel.g * scale, pixel.b * scale,
                 pixel.m * scale);
}

TPixelF normalized(const TPixel64 &pixel) {
  const float scale = 1.0f / float(TPixel64::maxChannelValue);
  return TPixelF(pixel.r * scale, pixel.g * scale, pixel.b * scale,
                 pixel.m * scale);
}

void setNeutralDiffuse(NormalPassRelightingFx &fx) {
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(0);
  parameter<TBoolParam>(fx, "flipX")->setValue(false);
  parameter<TBoolParam>(fx, "flipY")->setValue(false);
  parameter<TBoolParam>(fx, "flipZ")->setValue(false);
  parameter<TDoubleParam>(fx, "normalStrength")->setValue(0, 100.0);
  parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, 0.0);
  parameter<TDoubleParam>(fx, "lightElevation")->setValue(0, 0.0);
  parameter<TPixelParam>(fx, "lightColor")->setValue(0, TPixel32::White);
  parameter<TDoubleParam>(fx, "lightIntensity")->setValue(0, 100.0);
  parameter<TDoubleParam>(fx, "ambient")->setValue(0, 0.0);
  parameter<TDoubleParam>(fx, "diffuse")->setValue(0, 100.0);
  parameter<TDoubleParam>(fx, "specular")->setValue(0, 0.0);
  parameter<TDoubleParam>(fx, "shininess")->setValue(0, 32.0);
}

void expectRgb(const TPixelF &pixel, float r, float g, float b,
               const char *message, float tolerance = 0.0001f) {
  expect(pixel.r, r, message, tolerance);
  expect(pixel.g, g, message, tolerance);
  expect(pixel.b, b, message, tolerance);
}

void testDefaultsAndContract() {
  NormalPassRelightingFx fx;
  require(fx.getFxType() == "STD_normalPassRelightingFx",
          "Unexpected Normal Pass Relighting FX type");
  require(!fx.isZerary() && fx.getInputPortCount() == 2,
          "Relighting must expose two raster inputs");
  require(
      fx.getInputPortName(0) == "Source" && fx.getInputPortName(1) == "Normal",
      "Relighting port persistence keys changed");
  require(fx.getParams()->getParamCount() == 14,
          "Unexpected relighting parameter count");
  require(parameter<TIntEnumParam>(fx, "normalEncoding")->getValue() == 0,
          "Encoded RGB must be the default normal convention");
  require(parameter<TIntEnumParam>(fx, "normalEncoding")->getItemCount() == 2,
          "Normal encoding choices are incomplete");
  require(parameter<TIntEnumParam>(fx, "outputMode")->getValue() == 0 &&
              parameter<TIntEnumParam>(fx, "outputMode")->getItemCount() == 3,
          "Relighting output choices are incomplete");
  require(!parameter<TBoolParam>(fx, "flipX")->getValue() &&
              !parameter<TBoolParam>(fx, "flipY")->getValue() &&
              !parameter<TBoolParam>(fx, "flipZ")->getValue(),
          "Normal axes should not be flipped by default");
  expect(float(parameter<TDoubleParam>(fx, "normalStrength")->getValue(0)),
         100.0f, "Unexpected default normal strength");
  expect(float(parameter<TDoubleParam>(fx, "lightAzimuth")->getValue(0)),
         -45.0f, "Unexpected default light azimuth");
  expect(float(parameter<TDoubleParam>(fx, "lightElevation")->getValue(0)),
         35.0f, "Unexpected default light elevation");
  expect(float(parameter<TDoubleParam>(fx, "lightIntensity")->getValue(0)),
         100.0f, "Unexpected default light intensity");
  expect(float(parameter<TDoubleParam>(fx, "ambient")->getValue(0)), 10.0f,
         "Unexpected default ambient contribution");
  expect(float(parameter<TDoubleParam>(fx, "diffuse")->getValue(0)), 100.0f,
         "Unexpected default diffuse contribution");
  expect(float(parameter<TDoubleParam>(fx, "specular")->getValue(0)), 25.0f,
         "Unexpected default specular contribution");
  expect(float(parameter<TDoubleParam>(fx, "shininess")->getValue(0)), 32.0f,
         "Unexpected default shininess");
  require(fx.canComputeInFloat(), "Relighting did not enable float rendering");
  require(fx.toBeComputedInLinearColorSpace(false, false),
          "Relighting must compute Source illumination in scene-linear color");
}

void testEncodedAndSignedInputs() {
  NormalPassRelightingFx fx;
  setNeutralDiffuse(fx);

  TFxP sourceOwner = new RasterFixtureFx(TPixelF(0.8f, 0.4f, 0.2f, 1.0f), false,
                                         "relight-source");
  auto *source     = static_cast<RasterFixtureFx *>(sourceOwner.getPointer());
  connect(fx, "Source", source);

  // Encoded normal maps are premultiplied on entry.  This half-alpha pixel is
  // stored as (0.25, 0.25, 0.5) but must decode back to the +Z normal
  // (0.5, 0.5, 1.0) before lighting.
  TFxP alphaOwner = new RasterFixtureFx(TPixelF(0.25f, 0.25f, 0.5f, 0.5f),
                                        false, "alpha-normal");
  connect(fx, "Normal", alphaOwner.getPointer());
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(0);
  const TPixelF alphaResult = renderFloat(fx)->pixels(0)[0];
  expectRgb(alphaResult, 0.8f, 0.4f, 0.2f,
            "Encoded normal was not depremultiplied before decoding");

  parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, -90.0);

  // 128/255 is the neutral byte value an ordinary encoded normal map supplies.
  const float neutral = 128.0f / 255.0f;
  TFxP encodedOwner = new RasterFixtureFx(TPixelF(0.0f, neutral, neutral, 1.0f),
                                          false, "encoded-normal");
  auto *encoded     = static_cast<RasterFixtureFx *>(encodedOwner.getPointer());
  connect(fx, "Normal", encoded);
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(0);
  const TPixelF encodedResult = normalized(render32(fx)->pixels(0)[0]);
  require(encoded->lastBpp == 128 && encoded->lastWasFloat &&
              !encoded->lastWasLinear && !encoded->lastSettingsLinear,
          "Encoded normal was not requested as raw float data");

  TFxP signedOwner = new RasterFixtureFx(TPixelF(-1.0f, 0.0f, 0.0f, 1.0f), true,
                                         "signed-normal");
  auto *signedNormal = static_cast<RasterFixtureFx *>(signedOwner.getPointer());
  connect(fx, "Normal", signedNormal);
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(1);
  const TPixelF signedResult = normalized(render32(fx)->pixels(0)[0]);
  require(signedNormal->lastBpp == 128 && signedNormal->lastWasFloat &&
              !signedNormal->lastWasLinear && !signedNormal->lastSettingsLinear,
          "Signed EXR-style normal was clipped or color transformed");

  expectRgb(encodedResult, 0.8f, 0.4f, 0.2f,
            "Encoded normal did not fully face its light", 0.006f);
  expectRgb(signedResult, encodedResult.r, encodedResult.g, encodedResult.b,
            "Encoded and signed normal conventions are not equivalent", 0.006f);
}

void testAxisFlipsAndStrength() {
  struct AxisCase {
    const char *flip;
    TPixelF normal;
    double azimuth;
    double elevation;
  };
  const AxisCase cases[] = {{"flipX", TPixelF(1, 0, 0, 1), 90.0, 0.0},
                            {"flipY", TPixelF(0, 1, 0, 1), 0.0, 90.0},
                            {"flipZ", TPixelF(0, 0, 1, 1), 0.0, 0.0}};

  for (const AxisCase &test : cases) {
    NormalPassRelightingFx fx;
    setNeutralDiffuse(fx);
    parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(1);
    parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, test.azimuth);
    parameter<TDoubleParam>(fx, "lightElevation")->setValue(0, test.elevation);
    TFxP sourceOwner =
        new RasterFixtureFx(TPixelF(1, 1, 1, 1), false, "axis-source");
    TFxP normalOwner = new RasterFixtureFx(test.normal, true, "axis-normal");
    connect(fx, "Source", sourceOwner.getPointer());
    connect(fx, "Normal", normalOwner.getPointer());
    expect(normalized(render32(fx)->pixels(0)[0]).r, 1.0f,
           "Unflipped normal did not face its light", 0.006f);
    parameter<TBoolParam>(fx, test.flip)->setValue(true);
    expect(normalized(render32(fx)->pixels(0)[0]).r, 0.0f,
           "Axis flip did not reverse the normal", 0.006f);
  }

  NormalPassRelightingFx strengthFx;
  setNeutralDiffuse(strengthFx);
  parameter<TIntEnumParam>(strengthFx, "normalEncoding")->setValue(1);
  parameter<TDoubleParam>(strengthFx, "normalStrength")->setValue(0, 0.0);
  TFxP sourceOwner =
      new RasterFixtureFx(TPixelF(1, 1, 1, 1), false, "strength-source");
  TFxP normalOwner =
      new RasterFixtureFx(TPixelF(1, 0, 0, 1), true, "strength-normal");
  connect(strengthFx, "Source", sourceOwner.getPointer());
  connect(strengthFx, "Normal", normalOwner.getPointer());
  expect(normalized(render32(strengthFx)->pixels(0)[0]).r, 1.0f,
         "Zero normal strength did not flatten the normal to +Z", 0.006f);
}

void testRelitAlphaHdrAndSpecular() {
  NormalPassRelightingFx fx;
  setNeutralDiffuse(fx);
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(1);
  TFxP sourceOwner =
      new RasterFixtureFx(TPixelF(2.0f, 0.5f, 0.25f, 0.5f), true, "hdr-source");
  TFxP normalOwner =
      new RasterFixtureFx(TPixelF(0, 0, 1, 1), true, "front-normal");
  connect(fx, "Source", sourceOwner.getPointer());
  connect(fx, "Normal", normalOwner.getPointer());
  const TPixelF hdr = renderFloat(fx)->pixels(0)[0];
  expectRgb(hdr, 2.0f, 0.5f, 0.25f,
            "Float relighting clipped or altered fully lit HDR Source");
  expect(hdr.m, 0.5f, "Relighting changed Source alpha");

  // A pure specular highlight is additive but remains premultiplied by Source
  // alpha.  Turning the light behind the surface must suppress it completely.
  TFxP blackOwner =
      new RasterFixtureFx(TPixelF(0, 0, 0, 0.5f), false, "half-alpha-black");
  connect(fx, "Source", blackOwner.getPointer());
  parameter<TDoubleParam>(fx, "diffuse")->setValue(0, 0.0);
  parameter<TDoubleParam>(fx, "specular")->setValue(0, 100.0);
  TPixelF highlight = renderFloat(fx)->pixels(0)[0];
  expectRgb(highlight, 0.5f, 0.5f, 0.5f,
            "Specular highlight was not premultiplied by Source alpha");
  expect(highlight.m, 0.5f, "Specular highlight changed Source alpha");
  parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, 180.0);
  highlight = renderFloat(fx)->pixels(0)[0];
  expectRgb(highlight, 0.0f, 0.0f, 0.0f,
            "Back-facing surface retained a specular highlight");
  expect(highlight.m, 0.5f, "Back-facing shading changed Source alpha");

  // Exercise the third supported output precision on the same code path.
  parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, 0.0);
  const TPixelF result64 = normalized(render64(fx)->pixels(0)[0]);
  expectRgb(result64, 0.5f, 0.5f, 0.5f,
            "16-bit relighting disagrees with float output", 0.0001f);
}

void testDiagnosticsNormalOnlyAndPassthrough() {
  NormalPassRelightingFx fx;
  setNeutralDiffuse(fx);
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(1);
  TFxP sourceOwner = new RasterFixtureFx(TPixelF(0.3f, 0.2f, 0.1f, 0.5f), false,
                                         "passthrough-source");
  connect(fx, "Source", sourceOwner.getPointer());

  // Incomplete graph construction is non-destructive in Relit mode.
  const TPixelF disconnected = renderFloat(fx)->pixels(0)[0];
  expectRgb(disconnected, 0.3f, 0.2f, 0.1f,
            "Missing Normal did not pass Source through");
  expect(disconnected.m, 0.5f, "Passthrough changed Source alpha");

  TFxP invalidOwner =
      new RasterFixtureFx(TPixelF(0, 0, 0, 1), true, "invalid-normal");
  connect(fx, "Normal", invalidOwner.getPointer());
  const TPixelF invalid = renderFloat(fx)->pixels(0)[0];
  expectRgb(invalid, 0.3f, 0.2f, 0.1f,
            "Invalid Normal did not pass Source through");
  expect(invalid.m, 0.5f, "Invalid Normal changed Source alpha");

  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(0);
  TFxP infiniteOwner = new RasterFixtureFx(
      TPixelF(std::numeric_limits<float>::infinity(), 0.5f, 1.0f, 1.0f), true,
      "infinite-encoded-normal");
  connect(fx, "Normal", infiniteOwner.getPointer());
  const TPixelF infinite = renderFloat(fx)->pixels(0)[0];
  expectRgb(infinite, 0.3f, 0.2f, 0.1f,
            "Non-finite encoded Normal was clamped into a valid direction");
  expect(infinite.m, 0.5f, "Non-finite encoded Normal changed Source alpha");
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(1);

  TFxP normalOwner =
      new RasterFixtureFx(TPixelF(-1, 0, 0, 1.0f), true, "diagnostic-normal");
  connect(fx, "Normal", normalOwner.getPointer());
  parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, -90.0);
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(1);
  const TPixelF lighting = renderFloat(fx)->pixels(0)[0];
  expectRgb(lighting, 0.5f, 0.5f, 0.5f,
            "Lighting diagnostic did not preserve Source premultiplication");
  expect(lighting.m, 0.5f, "Lighting diagnostic changed Source alpha");

  parameter<TIntEnumParam>(fx, "outputMode")->setValue(2);
  const TPixelF preview = renderFloat(fx)->pixels(0)[0];
  expectRgb(preview, 0.0f, 0.25f, 0.25f,
            "Normal preview did not remap signed XYZ for display");
  expect(preview.m, 0.5f, "Normal preview changed Source alpha");

  // Diagnostics are pure data views rather than a mix with Source artwork.
  // Their matte is the intersection of Source and Normal coverage.
  TFxP partialOwner = new RasterFixtureFx(TPixelF(-1, 0, 0, 0.4f), true,
                                          "partial-diagnostic-normal");
  connect(fx, "Normal", partialOwner.getPointer());
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(1);
  const TPixelF partialLighting = renderFloat(fx)->pixels(0)[0];
  expectRgb(partialLighting, 0.2f, 0.2f, 0.2f,
            "Lighting diagnostic mixed in Source artwork");
  expect(partialLighting.m, 0.2f,
         "Lighting diagnostic did not intersect Source and Normal coverage");
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(2);
  const TPixelF partialPreview = renderFloat(fx)->pixels(0)[0];
  expectRgb(partialPreview, 0.0f, 0.1f, 0.1f,
            "Normal diagnostic mixed in Source artwork");
  expect(partialPreview.m, 0.2f,
         "Normal diagnostic did not intersect Source and Normal coverage");

  connect(fx, "Normal", invalidOwner.getPointer());
  const TPixelF invalidPreview = renderFloat(fx)->pixels(0)[0];
  expectRgb(invalidPreview, 0.0f, 0.0f, 0.0f,
            "Invalid Normal contaminated the diagnostic output");
  expect(invalidPreview.m, 0.0f,
         "Invalid Normal did not clear the diagnostic matte");

  // With no Source, the FX supplies white albedo and uses Normal alpha.
  disconnect(fx, "Source");
  TFxP normalOnlyOwner =
      new RasterFixtureFx(TPixelF(-1, 0, 0, 0.4f), true, "normal-only");
  connect(fx, "Normal", normalOnlyOwner.getPointer());
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(0);
  const TPixelF normalOnly = renderFloat(fx)->pixels(0)[0];
  expectRgb(normalOnly, 0.4f, 0.4f, 0.4f,
            "Normal-only relighting did not use premultiplied white albedo");
  expect(normalOnly.m, 0.4f, "Normal-only relighting lost Normal alpha");
}

void verifyPopulated(NormalPassRelightingFx &fx) {
  require(parameter<TIntEnumParam>(fx, "normalEncoding")->getValue() == 1,
          "Normal encoding did not survive clone or persistence");
  require(parameter<TBoolParam>(fx, "flipX")->getValue() &&
              !parameter<TBoolParam>(fx, "flipY")->getValue() &&
              parameter<TBoolParam>(fx, "flipZ")->getValue(),
          "Normal flips did not survive clone or persistence");
  require(parameter<TIntEnumParam>(fx, "outputMode")->getValue() == 2,
          "Output mode did not survive clone or persistence");
  auto *strength = parameter<TDoubleParam>(fx, "normalStrength");
  require(strength->getKeyframeCount() == 2 && strength->isKeyframe(0) &&
              strength->isKeyframe(12),
          "Animated normal strength was not retained");
  expect(float(strength->getValue(0)), 25.0f,
         "First normal strength key changed");
  expect(float(strength->getValue(12)), 175.0f,
         "Second normal strength key changed");
  expect(float(parameter<TDoubleParam>(fx, "lightAzimuth")->getValue(0)),
         123.0f, "Light azimuth did not survive clone or persistence");
  expect(float(parameter<TDoubleParam>(fx, "specular")->getValue(0)), 73.0f,
         "Specular value did not survive clone or persistence");
}

void testCloneAndPersistence(const QString &path) {
  NormalPassRelightingFx fx;
  parameter<TIntEnumParam>(fx, "normalEncoding")->setValue(1);
  parameter<TBoolParam>(fx, "flipX")->setValue(true);
  parameter<TBoolParam>(fx, "flipY")->setValue(false);
  parameter<TBoolParam>(fx, "flipZ")->setValue(true);
  parameter<TIntEnumParam>(fx, "outputMode")->setValue(2);
  parameter<TDoubleParam>(fx, "normalStrength")->setValue(0, 25.0);
  parameter<TDoubleParam>(fx, "normalStrength")->setValue(12, 175.0);
  parameter<TDoubleParam>(fx, "lightAzimuth")->setValue(0, 123.0);
  parameter<TDoubleParam>(fx, "specular")->setValue(0, 73.0);

  std::unique_ptr<TFx> cloneOwner(fx.clone(false));
  auto *clone = dynamic_cast<NormalPassRelightingFx *>(cloneOwner.get());
  require(clone != nullptr, "Relighting clone has the wrong FX type");
  verifyPopulated(*clone);
  parameter<TBoolParam>(*clone, "flipX")->setValue(false);
  require(parameter<TBoolParam>(fx, "flipX")->getValue(),
          "Mutating the clone changed the original FX");

  {
    TOStream stream(TFilePath(path.toStdWString()));
    stream << &fx;
  }
  TIStream stream(TFilePath(path.toStdWString()));
  TPersist *persist = nullptr;
  stream >> persist;
  std::unique_ptr<TPersist> owner(persist);
  auto *restored = dynamic_cast<NormalPassRelightingFx *>(persist);
  require(restored != nullptr,
          "FX registration did not reload Normal Pass Relighting");
  require(restored->getInputPortName(0) == "Source" &&
              restored->getInputPortName(1) == "Normal",
          "Persistence changed relighting port keys");
  verifyPopulated(*restored);
}

}  // namespace

int main(int argc, char *argv[]) {
  QCoreApplication application(argc, argv);
  try {
    QTemporaryDir directory;
    require(directory.isValid(), "Cannot create temporary test directory");
    testDefaultsAndContract();
    testEncodedAndSignedInputs();
    testAxisFlipsAndStrength();
    testRelitAlphaHdrAndSpecular();
    testDiagnosticsNormalOnlyAndPassthrough();
    testCloneAndPersistence(directory.filePath("normal-relighting.fx"));
    std::cout << "Normal Pass Relighting FX regression tests passed\n";
    return 0;
  } catch (const TException &error) {
    std::cerr << "Normal Pass Relighting FX regression test failed: "
              << QString::fromStdWString(error.getMessage()).toStdString()
              << '\n';
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "Normal Pass Relighting FX regression test failed: "
              << error.what() << '\n';
    return 1;
  }
}
