#include "tsimplecolorstyles.h"
#include "tnztools/trailcyclestate.h"
#include "tpalette.h"
#include "trasterimage.h"
#include "tvectorimage.h"
#include "tstroke.h"
#include "tstream.h"
#include "tenv.h"
#include "tiio_std.h"
#include "tlevel_io.h"
#include "image/pli/tiio_pli.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using TrailCycle::Mode;
void require(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}

template <class Style>
void checkParameter() {
  Style style;
  require(
      style.getParamCount() == 4 &&
          style.getParamType(TrailStyles::frameOffsetParam) == TColorStyle::INT,
      "Frame Offset is not immediately after Trail Cycle");
  require(style.getTrailFrameOffset() == 0, "Old styles need automatic offset");
  int low, high;
  style.getParamRange(TrailStyles::frameOffsetParam, low, high);
  require(low == 0 && high >= 100000,
          "Offset was limited to a guessed frame count");
  style.setParamValue(TrailStyles::frameOffsetParam, 700);
  require(TrailStyles::getFrameOffset(&style) == 700 &&
              TrailStyles::startFrameIndex(&style) == -1,
          "Missing resource was not ignored or lost the requested value");
  TColorStyleP saved = style.clone();
  Style copied;
  copied.copy(style);
  require(
      TrailStyles::getFrameOffset(saved.getPointer()) == 700 && copied == style,
      "Copy/clone lost offset");
  style.setParamValue(TrailStyles::frameOffsetParam, 10);
  require(
      style != copied && TrailStyles::getFrameOffset(saved.getPointer()) == 700,
      "Offset equality or edited-style isolation failed");
  style.setParamValue(TrailStyles::frameOffsetParam, -1);
  require(style.getTrailFrameOffset() == 0,
          "Negative offset was not defaulted");
}

void checkPalette(const TPaletteP &palette, Mode mode, int rasterOffset,
                  int vectorOffset, const char *phase) {
  const int actualRaster = TrailStyles::getFrameOffset(palette->getStyle(1));
  const int actualVector = TrailStyles::getFrameOffset(palette->getStyle(2));
  if (actualRaster != rasterOffset || actualVector != vectorOffset)
    throw std::runtime_error(std::string(phase) + ": expected offsets " +
                             std::to_string(rasterOffset) + "," +
                             std::to_string(vectorOffset) + ", got " +
                             std::to_string(actualRaster) + "," +
                             std::to_string(actualVector));
  require(TrailStyles::getMode(palette->getStyle(1)) == mode &&
              TrailStyles::getMode(palette->getStyle(2)) == Mode::Repeat,
          "Offset metadata damaged cycle metadata");
  for (int frame : {0, 5, 10, 30, 2}) {
    palette->setFrame(frame);
    require(
        TrailStyles::getFrameOffset(palette->getStyle(1)) == rasterOffset &&
            TrailStyles::getFrameOffset(palette->getStyle(2)) == vectorOffset,
        "Scrubbing changed the non-animated offset");
  }
  palette->setFrame(5);
  require(
      palette->getStyle(1)->getParamValue(TColorStyle::double_tag(), 0) == 10.0,
      "Offset stopped Distance animation");
}

void checkPersistence(const QString &root) {
  for (Mode mode : {Mode::Off, Mode::Forward, Mode::Backward, Mode::Repeat}) {
    for (int offset :
         {0, 70, 32767, 32768, 65536, (std::numeric_limits<int>::max)()}) {
      TPaletteP palette = new TPalette;
      palette->setStyle(1, new TRasterImagePatternStrokeStyle);
      palette->getPage(0)->addStyle(
          palette->addStyle(new TVectorImagePatternStrokeStyle));
      TColorStyle *style = palette->getStyle(1);
      style->setParamValue(0, 0.0);
      TrailStyles::setFrameOffset(style, 10);
      palette->setKeyframe(1, 0);
      style->setParamValue(0, 20.0);
      TrailStyles::setFrameOffset(style, 30);
      palette->setKeyframe(1, 10);
      TrailStyles::setMode(style, mode);
      TrailStyles::setFrameOffset(style, offset);
      TrailStyles::setMode(palette->getStyle(2), Mode::Repeat);
      TrailStyles::setFrameOffset(palette->getStyle(2), 10);
      checkPalette(palette, mode, offset, 10, "original");
      const TFilePath tpl(root + "/offset.tpl");
      {
        TOStream stream(tpl);
        palette->saveData(stream);
      }
      TPaletteP loaded = new TPalette;
      {
        TIStream stream(tpl);
        loaded->loadData(stream);
      }
      checkPalette(loaded, mode, offset, 10, "TPL");

      TVectorImageP image = new TVectorImage;
      image->setPalette(palette.getPointer());
      for (int id : {1, 2}) {
        auto *stroke = new TStroke(
            std::vector<TThickPoint>{{0, 0, 3}, {0.5, 0, 3}, {1, 0, 3}});
        stroke->setStyle(id);
        stroke->outlineOptions().m_patternFrameOffset = 1;
        stroke->outlineOptions().m_patternFrameStep   = 0;
        image->addStroke(stroke, false);
      }
      const TFilePath pli(root + "/offset.pli");
      {
        TLevelWriterPli writer(pli, nullptr);
        writer.getFrameWriter(TFrameId(1))->save(image);
      }
      TLevelReaderPli reader(pli);
      TLevelP info = reader.loadInfo();
      require(info && info->getPalette(), "No PLI palette");
      checkPalette(TPaletteP(info->getPalette()), mode, offset, 10, "PLI");
      TVectorImageP result = reader.getFrameReader(TFrameId(1))->load();
      require(result && result->getStrokeCount() == 2, "PLI lost stamps");
      TrailStyles::setFrameOffset(info->getPalette()->getStyle(1), 12345);
      require(
          result->getStroke(0)->outlineOptions().m_patternFrameOffset == 1 &&
              result->getStroke(0)->outlineOptions().m_patternFrameStep == 0,
          "Changing style offset changed existing stamps");
    }
  }
}

void checkLookup(TColorStyle &style, int frame, int index) {
  TrailStyles::setFrameOffset(&style, frame);
  require(TrailStyles::startFrameIndex(&style) == index,
          "Sparse source frame lookup failed");
}

void checkSources(const QString &root) {
  const QString folder = root + "/custom styles";
  require(QDir().mkpath(folder), "Cannot create source fixtures");
  Tiio::defineStd();
  TRasterImagePatternStrokeStyle::setRootDir(TFilePath(root));
  for (int frame : {10, 30, 70}) {
    TRaster32P pixels(16, 16);
    pixels->fill(TPixel32::Red);
    TImageWriterP writer(TFilePath(
        folder + QString("/sparse.%1.bmp").arg(frame, 4, 10, QChar('0'))));
    writer->save(TRasterImageP(pixels));
  }
  TRasterImagePatternStrokeStyle raster("sparse");
  require(raster.getLevelFrameCount() == 3, "BMP source sequence failed");
  checkLookup(raster, 0, -1);
  checkLookup(raster, 10, 0);
  checkLookup(raster, 30, 1);
  checkLookup(raster, 70, 2);
  checkLookup(raster, 2, -1);  // A frame count is not a drawing-number range.
  checkLookup(raster, 20, -1);
  checkLookup(raster, (std::numeric_limits<int>::max)(), -1);

  TVectorImageP image = new TVectorImage;
  TPaletteP colors    = new TPalette;
  image->setPalette(colors.getPointer());
  auto *stroke =
      new TStroke(std::vector<TThickPoint>{{0, 0, 3}, {5, 0, 3}, {10, 0, 3}});
  stroke->setStyle(1);
  image->addStroke(stroke, false);
  {
    TLevelWriterPli writer(TFilePath(folder + "/vector.pli"), nullptr);
    for (const TFrameId &fid : {TFrameId(10), TFrameId(30, 'a'), TFrameId(70)})
      writer.getFrameWriter(fid)->save(image);
  }
  TLevelReader::define("pli", TLevelReaderPli::create);
  TVectorImagePatternStrokeStyle::setRootDir(TFilePath(root));
  TVectorImagePatternStrokeStyle vector("vector");
  require(vector.getLevelFrameCount() == 3, "PLI source sequence failed");
  checkLookup(vector, 10, 0);
  checkLookup(vector, 30,
              -1);  // Never silently select 30a when 30 was requested.
  checkLookup(vector, 70, 2);
  checkLookup(vector, 90, -1);

  // Resolve real sparse source IDs before using the shared gesture state.
  TrailCycle::State state;
  const TrailCycle::StyleKey key{nullptr, 1, "sparse"};
  TrailStyles::setFrameOffset(&raster, 30);
  auto selected = state.begin(Mode::Forward, key, raster.getLevelFrameCount(),
                              TrailStyles::startFrameIndex(&raster));
  require(selected.offset == 1,
          "Frame number was mistaken for a zero-based index");
  state.commit(selected, true);
  TrailStyles::setFrameOffset(&raster, 20);
  selected = state.begin(Mode::Forward, key, raster.getLevelFrameCount(),
                         TrailStyles::startFrameIndex(&raster));
  require(selected.offset == 2,
          "Missing drawing restarted or wrapped the cursor");
}
}  // namespace

int main(int argc, char **argv) try {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  require(directory.isValid(), "No temporary directory");
  TEnv::setArgPathValue(TEnv::getRootVarName(), directory.path().toStdString());
  checkParameter<TRasterImagePatternStrokeStyle>();
  checkParameter<TVectorImagePatternStrokeStyle>();
  checkPersistence(directory.path());
  checkSources(directory.path());
} catch (const TException &e) {
  std::wcerr << e.getMessage() << '\n';
  return 1;
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
