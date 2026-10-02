#include "tsimplecolorstyles.h"
#include "tpalette.h"
#include "trasterimage.h"
#include "ttoonzimage.h"
#include "tstroke.h"
#include "tenv.h"
#include "tiio.h"
#include "tiio_std.h"
#include "tiio_jpg.h"
#include "tfiletype.h"
#include "tlevel_io.h"
#include "image/png/tiio_png.h"
#include "image/tif/tiio_tif.h"
#include "image/tzl/tiio_tzl.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

TPaletteP palette() {
  TPaletteP result = new TPalette;
  result->setStyle(1, TPixel32(255, 0, 0, 255));
  result->addStyle(TPixel32(0, 255, 0, 255));
  result->addStyle(TPixel32(255, 0, 0, 128));
  return result;
}

void checkIndexedPixels() {
  TPaletteP colors = palette();
  TRasterCM32P cm(8, 4);
  cm->fill(TPixelCM32());
  cm->pixels(1)[1] = TPixelCM32(1, 2, 0);
  cm->pixels(1)[2] = TPixelCM32(1, 2, 255);
  cm->pixels(1)[3] = TPixelCM32(1, 2, 128);
  cm->pixels(1)[4] = TPixelCM32(1, 0, 128);
  cm->pixels(1)[5] = TPixelCM32(3, 0, 0);
  TToonzImageP image(cm, TRect(1, 1, 5, 1));
  image->setPalette(colors.getPointer());
  TRaster32P rgba = TrailStyles::rasterSource(image, nullptr);
  require(rgba && rgba->getSize() == cm->getSize(), "TLV canvas was cropped");
  require(rgba->pixels(0)[0] == TPixel32::Transparent,
          "Style zero is not transparent");
  require(rgba->pixels(1)[1] == TPixel32(255, 0, 0, 255), "Ink color was lost");
  require(rgba->pixels(1)[2] == TPixel32(0, 255, 0, 255),
          "Paint color was lost");
  TPixel32 mixed = rgba->pixels(1)[3];
  require(mixed.r >= 126 && mixed.r <= 128 && mixed.g >= 127 &&
              mixed.g <= 129 && mixed.m == 255,
          "Antialiased ink/paint mixture is wrong");
  TPixel32 edge = rgba->pixels(1)[4];
  require(edge.r == edge.m && edge.m >= 126 && edge.m <= 128,
          "Transparent antialiasing is not premultiplied");
  require(rgba->pixels(1)[5] == TPixel32(128, 0, 0, 128),
          "Style opacity was lost");
  require(cm->pixels(1)[3] == TPixelCM32(1, 2, 128),
          "Source pixels were modified");

  image->setPalette(nullptr);
  require(!TrailStyles::rasterSource(image, nullptr),
          "Missing palette was accepted");
  require(bool(TrailStyles::rasterSource(image, colors.getPointer())),
          "Level palette fallback failed");
  cm->pixels(0)[0] = TPixelCM32(4095, 0, 0);
  require(!TrailStyles::rasterSource(image, colors.getPointer()),
          "Invalid ink was accepted");
  cm->pixels(0)[0] = TPixelCM32(4095, 0, 255);
  require(bool(TrailStyles::rasterSource(image, colors.getPointer())),
          "Unused ink index rejected");

  colors->setKeyframe(1, 0);
  colors->setStyle(1, TPixel32(0, 0, 255, 255));
  colors->setKeyframe(1, 10);
  colors->setFrame(0);
  const TPixel32 before = colors->getStyle(1)->getMainColor();
  rgba = TrailStyles::rasterSource(image, colors.getPointer(), 10);
  require(rgba->pixels(1)[1].b == 255,
          "Source palette animation frame was ignored");
  require(
      colors->getFrame() == 0 && colors->getStyle(1)->getMainColor() == before,
      "Source conversion scrubbed the shared palette");
}

void checkRasterTypes() {
  TRaster32P rgba(8, 4);
  rgba->fill(TPixel32(40, 50, 60, 100));
  require(TrailStyles::rasterSource(TRasterImageP(rgba), nullptr) == rgba,
          "Existing RGBA source was changed");
  TRasterGR8P gray(8, 4);
  gray->fill(TPixelGR8(127));
  TRaster32P converted =
      TrailStyles::rasterSource(TRasterImageP(gray), nullptr);
  require(converted && converted->pixels(0)[0] == TPixel32(127, 127, 127, 255),
          "Grayscale raster was not normalized");
  TRasterGR16P gray16(8, 4);
  gray16->fill(TPixelGR16(65535));
  converted = TrailStyles::rasterSource(TRasterImageP(gray16), nullptr);
  require(converted && converted->pixels(0)[0] == TPixel32::White,
          "16-bit grayscale conversion failed");
  TRaster64P high(8, 4);
  high->fill(TPixel64(65535, 0, 0, 65535));
  converted = TrailStyles::rasterSource(TRasterImageP(high), nullptr);
  require(converted && converted->pixels(0)[0] == TPixel32(255, 0, 0, 255),
          "16-bit RGBA conversion failed");
  TRasterFP floating(8, 4);
  floating->fill(TPixelF(0, 1, 0, 1));
  converted = TrailStyles::rasterSource(TRasterImageP(floating), nullptr);
  require(converted && converted->pixels(0)[0] == TPixel32(0, 255, 0, 255),
          "Float raster conversion failed");
  require(!TrailStyles::rasterSource(TImageP(), nullptr),
          "Null source accepted");
}

void checkFilters() {
  const QStringList filters = TrailStyles::sourceFilters().split(' ');
  for (const QString &extension :
       {"pli", "tlv", "png", "tif", "tiff", "bmp", "jpg", "jpeg", "tga", "sgi",
        "rgb", "exr", "nol"}) {
    require(QDir::match(filters, "source.0001." + extension),
            "Raster extension is missing");
    require(QDir::match(filters, "source.0001." + extension.toUpper()),
            "Uppercase raster extension is missing");
  }
  require(!QDir::match(filters, "source.tpl"),
          "Palette sidecar matched as artwork");
  require(!QDir::match(filters, "source.png.bak"), "Backup matched as artwork");
}

void checkRasterFiles(const QString &folder) {
  // Real readers and sequence discovery, not mocked extensions.
  for (const QString &extension :
       {"png", "PNG", "tif", "tiff", "TIF", "bmp", "jpg", "jpeg"}) {
    const QString name = "source_" + extension;
    for (int i = 1; i <= 2; ++i) {
      TFilePath path(folder + "/" + name +
                     QString(".%1.").arg(i, 4, 10, QChar('0')) + extension);
      TRaster32P pixels(32, 32);
      pixels->fill(i == 1 ? TPixel32::Red : TPixel32::Green);
      TImageWriterP writer(path);
      writer->save(TRasterImageP(pixels));
    }
    QFile sidecar(folder + "/" + name + ".bak");
    require(sidecar.open(QIODevice::WriteOnly),
            "Cannot create sidecar fixture");
    sidecar.write("not image data");
    sidecar.close();
    require(QDir().mkpath(folder + "/" + name + ".aaa"),
            "Cannot create directory fixture");
    TFilePath source =
        TrailStyles::findSource(TFilePath(folder), name.toStdString());
    require(source.getType() == extension.toLower().toStdString(),
            "Wrong source resolved");
    TRasterImagePatternStrokeStyle trail(name.toStdString());
    require(trail.getLevelFrameCount() == 2, "Raster sequence was not loaded");
    const TRaster32P icon = trail.getIcon(TDimension(32, 32));
    require(icon && icon->pixels(16)[16].r > 200, "Raster source icon failed");
    TStroke stroke(
        std::vector<TThickPoint>{{0, 0, 10}, {0.5, 0, 10}, {1, 0, 10}});
    std::vector<TAffine> placements;
    trail.computeTransformations(placements, &stroke);
    require(placements.size() == 1,
            "Raster click carrier has wrong placement count");
  }
}

void checkTlvFile(const QString &folder) {
  TPaletteP colors = palette();
  const TFilePath path(folder + "/indexed.tlv");
  {
    TLevelWriterTzl writer(path, nullptr);
    writer.setPalette(colors.getPointer());
    for (int i = 1; i <= 3; ++i) {
      TRasterCM32P pixels(32, 32);
      pixels->fill(TPixelCM32());
      if (i != 2)
        pixels->extractT(8, 8, 23, 23)
            ->fill(TPixelCM32(1, 2, i == 1 ? 0 : 255));
      TToonzImageP image(pixels, i == 2 ? TRect() : TRect(8, 8, 23, 23));
      image->setPalette(colors.getPointer());
      writer.getFrameWriter(TFrameId(i))->save(image);
    }
  }
  require(QFile::exists(path.withType("tpl").getQString()),
          "TLV writer did not save its palette");
  TRasterImagePatternStrokeStyle trail("indexed");
  require(trail.getLevelFrameCount() == 3,
          "TLV sequence or blank frame was dropped");
  const TRaster32P icon = trail.getIcon(TDimension(32, 32));
  require(icon && icon->pixels(16)[16].r > 200 && icon->pixels(16)[16].g < 20,
          "TLV style icon lost its source palette");
  require(QFile::remove(path.withType("tpl").getQString()),
          "Cannot remove palette fixture");
  trail.loadLevel("indexed");
  require(trail.getLevelFrameCount() == 0,
          "Missing TLV palette baked fallback colors");
}
}  // namespace

int main(int argc, char **argv) try {
  QCoreApplication app(argc, argv);
  QTemporaryDir temporary;
  require(temporary.isValid(), "No fixture folder");
  TEnv::setArgPathValue(TEnv::getRootVarName(), temporary.path().toStdString());
  const QString folder = temporary.path() + "/custom styles";
  require(QDir().mkpath(folder), "Cannot create source folder");
  TRasterImagePatternStrokeStyle::setRootDir(TFilePath(temporary.path()));
  Tiio::defineStd();
  for (const char *extension : {"png", "nol"}) {
    Tiio::defineReaderMaker(extension, Tiio::makePngReader);
    Tiio::defineWriterMaker(extension, Tiio::makePngWriter, true);
    Tiio::defineWriterProperties(extension, new Tiio::PngWriterProperties());
    TFileType::declare(extension, TFileType::RASTER_IMAGE);
  }
  for (const char *extension : {"tif", "tiff"}) {
    Tiio::defineReaderMaker(extension, Tiio::makeTifReader);
    Tiio::defineWriterMaker(extension, Tiio::makeTifWriter, true);
    Tiio::defineWriterProperties(extension, new Tiio::TifWriterProperties());
    TFileType::declare(extension, TFileType::RASTER_IMAGE);
  }
  Tiio::defineWriterMaker("jpeg", Tiio::makeJpgWriter, true);
  Tiio::defineWriterProperties("jpeg", new Tiio::JpgWriterProperties());
  TLevelReader::define("tlv", TLevelReaderTzl::create);
  TFileType::declare("tlv", TFileType::CMAPPED_LEVEL);
  checkIndexedPixels();
  checkRasterTypes();
  checkFilters();
  checkRasterFiles(folder);
  checkTlvFile(folder);
} catch (const TException &e) {
  std::wcerr << e.getMessage() << '\n';
  return 1;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
