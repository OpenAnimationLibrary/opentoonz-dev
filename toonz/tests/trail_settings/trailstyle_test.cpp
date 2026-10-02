#include "tsimplecolorstyles.h"
#include "tnztools/trailstamp.h"
#include "tpalette.h"
#include "tvectorimage.h"
#include "tstroke.h"
#include "tstream.h"
#include "tenv.h"
#include "image/pli/tiio_pli.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>
#include <fstream>
#include <regex>
#include <stdexcept>

namespace {
using TrailCycle::Mode;

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

template <class Style>
void checkParametersAndCopies() {
  Style first;
  require(first.getParamCount() == 4, "Trail settings missing");
  require(first.getParamType(0) == TColorStyle::DOUBLE &&
              first.getParamType(1) == TColorStyle::DOUBLE &&
              first.getParamType(2) == TColorStyle::ENUM &&
              first.getParamType(3) == TColorStyle::INT,
          "Existing parameter indices changed");
  require(first.getTrailCycleMode() == Mode::Off, "Legacy default changed");
  first.setParamValue(0,
                      12);  // Preserve callers using an integer for Distance.
  require(first.getParamValue(TColorStyle::double_tag(), 0) == 12.0,
          "Integer Distance call was redirected into Trail Cycle");
  first.setParamValue(2, 3);
  TColorStyleP clone = first.clone();
  require(TrailStyles::getMode(clone.getPointer()) == Mode::Repeat,
          "Clone lost mode");
  Style second;
  second.copy(first);
  require(second.getTrailCycleMode() == Mode::Repeat, "Copy lost mode");
  require(second == first, "Style equality lost enum");
  second.setParamValue(2, 0);
  require(!(second == first), "Style equality ignores mode");
  second.setParamValue(2, 99);
  require(second.getTrailCycleMode() == Mode::Off, "Invalid mode was accepted");
  TStroke stroke(std::vector<TThickPoint>{{0, 0, 2}, {20, 0, 2}, {40, 0, 2}});
  stroke.outlineOptions().m_patternFrameOffset = 2;
  stroke.outlineOptions().m_patternFrameStep   = -1;
  first.setParamValue(2, 0);
  require(stroke.outlineOptions().m_patternFrameOffset == 2 &&
              stroke.outlineOptions().m_patternFrameStep == -1,
          "Changing the drawing default changed a committed stroke");
}

void checkClickStroke(const QString &root) {
  std::array<TThickPoint, 5> carrier;
  require(TrailStamp::makeClickCarrier({{4.0, 7.0, 6.0}}, carrier),
          "Cannot create click carrier");
  TStroke stroke;
  stroke.reshape(carrier.data(), int(carrier.size()));
  require(stroke.getChunkCount() == 2 && stroke.getLength() == 1.0,
          "Click did not produce a real nonzero-length stroke");
  require(stroke.getThickPoint(0) == carrier.front(),
          "Click stroke changed its first stamp's center or thickness");

  require(QDir().mkpath(root + "/custom styles"),
          "Cannot create Trail fixture");
  TVectorImageP source    = new TVectorImage;
  TPaletteP sourcePalette = new TPalette;
  source->setPalette(sourcePalette.getPointer());
  auto *sourceStroke =
      new TStroke(std::vector<TThickPoint>{{-4, 0, 4}, {0, 0, 4}, {4, 0, 4}});
  sourceStroke->setStyle(1);
  source->addStroke(sourceStroke, false);
  {
    TLevelWriterPli writer(TFilePath(root + "/custom styles/click.pli"),
                           nullptr);
    writer.getFrameWriter(TFrameId(1))->save(source);
  }
  TLevelReader::define("pli", TLevelReaderPli::create);
  TVectorImagePatternStrokeStyle::setRootDir(TFilePath(root));
  TVectorImagePatternStrokeStyle style("click");
  require(style.getLevelFrameCount() == 1, "Single-frame Trail did not load");
  for (double distance : {-50.0, 0.0, 50.0}) {
    style.setParamValue(0, distance);
    for (int step : {-1, 0, 1}) {
      stroke.outlineOptions().m_patternFrameStep = step;
      for (double rotation : {-90.0, 0.0, 90.0}) {
        style.setParamValue(1, rotation);
        std::vector<TAffine> transforms;
        style.computeTransformations(transforms, &stroke);
        require(transforms.size() == 1,
                "Click must supply exactly one real Trail transformation");
        const TPointD center = transforms.front() * TPointD();
        require(
            std::abs(center.x - 4.0) < 1e-6 && std::abs(center.y - 7.0) < 1e-6,
            "Distance or Rotation moved the stamp away from its click");
      }
    }
  }
}

TPaletteP makePalette() {
  TPaletteP palette = new TPalette;
  palette->setStyle(1, new TRasterImagePatternStrokeStyle);
  palette->getPage(0)->addStyle(
      palette->addStyle(new TVectorImagePatternStrokeStyle));
  TrailStyles::setMode(palette->getStyle(1), Mode::Forward);
  TrailStyles::setMode(palette->getStyle(2), Mode::Repeat);
  return palette;
}

void checkAnimation(const TPaletteP &palette) {
  TColorStyle *style = palette->getStyle(1);
  TrailStyles::setMode(style, Mode::Backward);
  style->setParamValue(0, 0.0);
  palette->setKeyframe(1, 0);
  style->setParamValue(0, 20.0);
  TrailStyles::setMode(style, Mode::Off);
  palette->setKeyframe(1, 10);
  TrailStyles::setMode(style, Mode::Forward);
  for (int frame : {0, 5, 10, 30, 1}) {
    palette->setFrame(frame);
    require(TrailStyles::getMode(style) == Mode::Forward,
            "Palette scrubbing replaced the non-animated drawing default");
  }
  palette->setFrame(5);
  require(style->getParamValue(TColorStyle::double_tag(), 0) == 10.0,
          "Distance animation stopped working");
}

void checkTpl(const TFilePath &path, const TPaletteP &palette) {
  {
    TOStream os(path);
    palette->saveData(os);
  }
  TPaletteP loaded = new TPalette;
  {
    TIStream is(path);
    loaded->loadData(is);
  }
  require(TrailStyles::getMode(loaded->getStyle(1)) == Mode::Forward &&
              TrailStyles::getMode(loaded->getStyle(2)) == Mode::Repeat,
          "TPL round trip lost per-style modes");
  checkAnimation(loaded);

  // Old-style payloads have no attribute; the remaining fields are unchanged.
  std::ifstream input(path.getQString().toStdString());
  std::string bytes((std::istreambuf_iterator<char>(input)), {});
  require(bytes.find("trailCycle=") != std::string::npos,
          "TPL did not write the bounded metadata");
  input.close();
  bytes = std::regex_replace(bytes, std::regex(" trailCycle=\"[0-9]+\""), "");
  {
    std::ofstream out(path.getQString().toStdString());
    out << bytes;
  }
  {
    TIStream is(path);
    loaded->loadData(is);
  }
  require(TrailStyles::getMode(loaded->getStyle(1)) == Mode::Off &&
              TrailStyles::getMode(loaded->getStyle(2)) == Mode::Off,
          "Old TPL data no longer defaults to Off");
}

void checkPli(const TFilePath &path, const TPaletteP &palette) {
  TVectorImageP image = new TVectorImage;
  image->setPalette(palette.getPointer());
  for (int i = 0; i < 3; ++i) {
    auto *stroke =
        new TStroke(std::vector<TThickPoint>{{0, double(i * 10), 2},
                                             {20, double(i * 10), 2},
                                             {40, double(i * 10), 2}});
    if (i == 2) {
      std::array<TThickPoint, 5> carrier;
      require(TrailStamp::makeClickCarrier({{0, 20, 2}}, carrier),
              "Could not prepare click round-trip fixture");
      stroke->reshape(carrier.data(), int(carrier.size()));
    }
    stroke->setStyle(i == 1 ? 2 : 1);
    if (i == 0) {
      stroke->outlineOptions().m_patternFrameOffset = 2;
      stroke->outlineOptions().m_patternFrameStep   = 0;
    }
    if (i == 1) {
      stroke->outlineOptions().m_patternFrameOffset = -1;
      stroke->outlineOptions().m_patternFrameStep   = -1;
    }
    image->addStroke(stroke, false);
  }
  {
    TLevelWriterPli writer(path, nullptr);
    writer.getFrameWriter(TFrameId(1))->save(image);
  }
  TLevelReaderPli reader(path);
  TLevelP info     = reader.loadInfo();
  TPalette *loaded = info->getPalette();
  require(loaded &&
              TrailStyles::getMode(loaded->getStyle(1)) == Mode::Forward &&
              TrailStyles::getMode(loaded->getStyle(2)) == Mode::Repeat,
          "PLI palette round trip lost the style modes");
  TVectorImageP result = reader.getFrameReader(TFrameId(1))->load();
  require(result && result->getStrokeCount() == 3, "PLI lost strokes");
  require(result->getStroke(2)->getLength() > 0.0 &&
              result->getStroke(2)->getLength() < 2.0,
          "Saved click carrier collapsed or grew to multiple stamps");
  const int offsets[] = {2, -1, 0};
  const int steps[]   = {0, -1, 1};
  for (int i = 0; i < 3; ++i) {
    const auto &options = result->getStroke(i)->outlineOptions();
    require(options.m_patternFrameOffset == offsets[i] &&
                options.m_patternFrameStep == steps[i],
            "PLI stroke sequence leaked or was discarded");
  }
}
}  // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir directory;
  require(directory.isValid(), "No temporary directory");
  TEnv::setArgPathValue(TEnv::getRootVarName(), directory.path().toStdString());
  checkParametersAndCopies<TRasterImagePatternStrokeStyle>();
  checkParametersAndCopies<TVectorImagePatternStrokeStyle>();
  checkClickStroke(directory.path());
  TPaletteP palette = makePalette();
  checkAnimation(palette);
  checkTpl(TFilePath(directory.path() + "/trail.tpl"), palette);
  checkPli(TFilePath(directory.path() + "/trail.pli"), palette);
}
