#include "tsimplecolorstyles.h"
#include "trailscale.h"
#include "tpalette.h"
#include "tvectorimage.h"
#include "trasterimage.h"
#include "tstroke.h"
#include "tstream.h"
#include "tenv.h"
#include "tiio_std.h"
#include "tlevel_io.h"
#include "image/pli/tiio_pli.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <QLocale>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

bool close(double a, double b) { return std::abs(a - b) < 1e-8; }

template <class Style>
void checkParameter() {
  Style style;
  require(style.getParamCount() == 5 &&
              style.getParamType(TrailStyles::cycleParam) == TColorStyle::ENUM &&
              style.getParamType(TrailStyles::frameOffsetParam) == TColorStyle::INT &&
              style.getParamType(TrailStyles::sizeMultiplierParam) == TColorStyle::DOUBLE,
          "Trail Cycle / Frame Offset / Size Multiplier order changed");
  require(style.getParamNames(TrailStyles::frameOffsetParam) == "Frame Offset" &&
              style.getParamNames(TrailStyles::sizeMultiplierParam) == "Size Multiplier",
          "Settings labels changed");
  double low, high;
  style.getParamRange(TrailStyles::sizeMultiplierParam, low, high);
  require(low == 0.25 && high == 10.0 && style.getTrailSizeMultiplier() == 1.0,
          "Wrong multiplier range or legacy default");
  style.setTrailFrameOffset(32768);
  style.setTrailCycleMode(TrailCycle::Mode::Repeat);
  for (double value : {0.25, 0.5, 0.75, 1.0, 1.5, 2.25, 10.0}) {
    style.setParamValue(TrailStyles::sizeMultiplierParam, value);
    require(style.getTrailSizeMultiplier() == value, "Fractional size was lost");
    require(style.getTrailFrameOffset() == 32768 &&
                style.getTrailCycleMode() == TrailCycle::Mode::Repeat,
            "Size edit damaged Frame Offset or Trail Cycle");
    Style copy;
    copy.copy(style);
    TColorStyleP clone = style.clone();
    require(copy == style && *clone == style &&
                TrailStyles::getSizeMultiplier(clone.getPointer()) == value,
            "Size copy/clone/equality failed");
  }
  const int version = style.getVersionNumber();
  style.setParamValue(TrailStyles::sizeMultiplierParam, 2);
  require(style.getTrailSizeMultiplier() == 2.0 && style.getVersionNumber() != version,
          "Size did not invalidate cached transforms");
  style.setTrailSizeMultiplier(std::numeric_limits<double>::quiet_NaN());
  require(style.getTrailSizeMultiplier() == 1.0, "NaN reached a render transform");
}

TVectorImageP fixtureImage(TPalette *palette, double x, double width) {
  TVectorImageP image = new TVectorImage;
  image->setPalette(palette);
  auto *stroke = new TStroke(std::vector<TThickPoint>{
      {x, 0, 2}, {x + width * 0.5, 0, 2}, {x + width, 0, 2}});
  stroke->setStyle(1);
  image->addStroke(stroke, false);
  return image;
}

// Real style transforms, not a duplicate implementation of the placement loop.
template <class Style>
void checkGeometry(Style &style, const TRectD &sourceBounds) {
  TStroke click(std::vector<TThickPoint>{
      {4, 7, 6}, {4.25, 7, 6}, {4.5, 7, 6}, {4.75, 7, 6}, {5, 7, 6}});
  click.outlineOptions().m_patternFrameOffset = 1;
  click.outlineOptions().m_patternFrameStep = 0;
  for (double rotation : {-180.0, -90.0, -37.0, 0.0, 90.0, 137.0}) {
    style.setParamValue(1, rotation);
    style.setTrailSizeMultiplier(1.0);
    std::vector<TAffine> normal;
    style.computeTransformations(normal, &click);
    require(normal.size() == 1, "Baseline click is not a singleton");
    for (double multiplier : {0.25, 0.5, 1.0, 2.25, 10.0}) {
      style.setTrailSizeMultiplier(multiplier);
      for (double distance : {-50.0, 0.0, 50.0}) {
        style.setParamValue(0, distance);
        std::vector<TAffine> scaled;
        style.computeTransformations(scaled, &click);
        require(scaled.size() == 1, "Scaled click added or lost a stamp");
        const TAffine &a = scaled[0];
        require(close(a.a11, normal[0].a11 * multiplier) &&
                    close(a.a12, normal[0].a12 * multiplier) &&
                    close(a.a21, normal[0].a21 * multiplier) &&
                    close(a.a22, normal[0].a22 * multiplier),
                "Source artwork did not scale uniformly");
        const TPointD center = a * (0.5 * (sourceBounds.getP00() + sourceBounds.getP11()));
        require(close(center.x, 4) && close(center.y, 7), "Click anchor moved");
        const TRectD bounds = style.getStrokeBBox(&click);
        for (TPointD corner : {sourceBounds.getP00(), sourceBounds.getP01(),
                              sourceBounds.getP10(), sourceBounds.getP11()}) {
          const TPointD point = a * corner;
          require(point.x >= bounds.x0 - 1e-8 && point.x <= bounds.x1 + 1e-8 &&
                      point.y >= bounds.y0 - 1e-8 && point.y <= bounds.y1 + 1e-8,
                  "Rotated stamp escaped the advertised drawing bounds");
        }
      }
    }
  }
  require(click.getLength() == 1.0 &&
              click.outlineOptions().m_patternFrameOffset == 1 &&
              click.outlineOptions().m_patternFrameStep == 0,
          "Size changed the click carrier or recorded sequence");
  TStroke line(std::vector<TThickPoint>{{0, 0, 6}, {500, 0, 6}, {1000, 0, 6}});
  style.setParamValue(0, 0.0);
  std::size_t previous = 100000;
  for (double multiplier : {0.25, 0.5, 1.0, 2.0, 10.0}) {
    style.setTrailSizeMultiplier(multiplier);
    std::vector<TAffine> placements;
    style.computeTransformations(placements, &line);
    require(!placements.empty() && placements.size() <= previous,
            "Spacing did not grow with artwork size");
    previous = placements.size();
  }
}

void checkSources(const QString &root) {
  const QString folder = root + "/custom styles";
  require(QDir().mkpath(folder), "Cannot create fixtures");
  Tiio::defineStd();
  TRasterImagePatternStrokeStyle::setRootDir(TFilePath(root));
  for (int frame : {10, 30, 70}) {
    TRaster32P raster(64, 16);
    raster->fill(TPixel32::Red);
    TImageWriterP writer(TFilePath(folder + QString("/wide.%1.bmp").arg(frame, 4, 10, QChar('0'))));
    writer->save(TRasterImageP(raster));
  }
  TRasterImagePatternStrokeStyle raster("wide");
  require(raster.getLevelFrameCount() == 3, "Raster sequence missing");
  checkGeometry(raster, TRectD(-64, -16, 64, 16));

  TPaletteP colors = new TPalette;
  TVectorImageP image = fixtureImage(colors.getPointer(), 100, 100);
  {
    TLevelWriterPli writer(TFilePath(folder + "/widevector.pli"), nullptr);
    for (int frame : {10, 30, 70}) writer.getFrameWriter(TFrameId(frame))->save(image);
  }
  TLevelReader::define("pli", TLevelReaderPli::create);
  TVectorImagePatternStrokeStyle::setRootDir(TFilePath(root));
  TVectorImagePatternStrokeStyle vector("widevector");
  require(vector.getLevelFrameCount() == 3, "Vector sequence missing");
  checkGeometry(vector, image->getBBox());
}

void checkPalette(TPaletteP palette, double rasterSize, double vectorSize) {
  require(close(TrailStyles::getSizeMultiplier(palette->getStyle(1)), rasterSize) &&
              close(TrailStyles::getSizeMultiplier(palette->getStyle(2)), vectorSize),
          "Size lost in palette/PLI round trip");
  require(TrailStyles::getFrameOffset(palette->getStyle(1)) == 32768 &&
              TrailStyles::getFrameOffset(palette->getStyle(2)) == 70,
          "Frame Offset lost in size round trip");
  require(TrailStyles::getMode(palette->getStyle(1)) == TrailCycle::Mode::Off &&
              TrailStyles::getMode(palette->getStyle(2)) == TrailCycle::Mode::Repeat,
          "Cycle default lost in size round trip");
}

void checkPersistence(const QString &root) {
  // Decimal metadata must not depend on the UI locale.
  QLocale::setDefault(QLocale(QLocale::German));
  for (double value : {0.25, 0.5, 1.0, 2.25, 10.0}) {
    TPaletteP palette = new TPalette;
    palette->setStyle(1, new TRasterImagePatternStrokeStyle);
    palette->getPage(0)->addStyle(palette->addStyle(new TVectorImagePatternStrokeStyle));
    TrailStyles::setSizeMultiplier(palette->getStyle(1), value);
    TrailStyles::setSizeMultiplier(palette->getStyle(2), 0.5);
    TrailStyles::setFrameOffset(palette->getStyle(1), 32768);
    TrailStyles::setFrameOffset(palette->getStyle(2), 70);
    TrailStyles::setMode(palette->getStyle(2), TrailCycle::Mode::Repeat);
    const TFilePath tpl(root + "/size.tpl");
    { TOStream stream(tpl); palette->saveData(stream); }
    TPaletteP restored = new TPalette;
    { TIStream stream(tpl); restored->loadData(stream); }
    checkPalette(restored, value, 0.5);
    const TFilePath pli(root + "/size.pli");
    TVectorImageP image = fixtureImage(palette.getPointer(), 0, 1);
    image->getStroke(0)->outlineOptions().m_patternFrameOffset = 2;
    image->getStroke(0)->outlineOptions().m_patternFrameStep = -1;
    { TLevelWriterPli writer(pli, nullptr); writer.getFrameWriter(TFrameId(1))->save(image); }
    TLevelReaderPli reader(pli);
    TLevelP info = reader.loadInfo();
    checkPalette(TPaletteP(info->getPalette()), value, 0.5);
    TVectorImageP loaded = reader.getFrameReader(TFrameId(1))->load();
    require(loaded->getStroke(0)->outlineOptions().m_patternFrameOffset == 2 &&
                loaded->getStroke(0)->outlineOptions().m_patternFrameStep == -1,
            "Size persistence changed stroke sequence");
  }
}

void checkAnimation(const QString &root) {
  TPaletteP palette = new TPalette;
  palette->setStyle(1, new TRasterImagePatternStrokeStyle);
  auto *style = palette->getStyle(1);
  TrailStyles::setSizeMultiplier(style, 0.5);
  palette->setKeyframe(1, 0);
  TrailStyles::setSizeMultiplier(style, 2.0);
  palette->setKeyframe(1, 10);
  TrailStyles::setFrameOffset(style, 70);
  palette->setFrame(5);
  require(close(TrailStyles::getSizeMultiplier(style), 1.25) &&
              TrailStyles::getFrameOffset(style) == 70,
          "Size animation changed non-animated Frame Offset");
  const TFilePath tpl(root + "/animated-size.tpl");
  { TOStream stream(tpl); palette->saveData(stream); }
  TPaletteP restored = new TPalette;
  { TIStream stream(tpl); restored->loadData(stream); }
  restored->setFrame(10);
  require(close(TrailStyles::getSizeMultiplier(restored->getStyle(1)), 2.0),
          "Size keyframe not saved");
  restored->setFrame(5);
  require(close(TrailStyles::getSizeMultiplier(restored->getStyle(1)), 1.25),
          "Size keyframe interpolation not restored");
}
}  // namespace

int main(int argc, char **argv) try {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  require(directory.isValid(), "No test directory");
  TEnv::setArgPathValue(TEnv::getRootVarName(), directory.path().toStdString());
  checkParameter<TRasterImagePatternStrokeStyle>();
  checkParameter<TVectorImagePatternStrokeStyle>();
  checkSources(directory.path());
  checkPersistence(directory.path());
  checkAnimation(directory.path());
} catch (const TException &error) {
  std::wcerr << error.getMessage() << '\n';
  return 1;
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
