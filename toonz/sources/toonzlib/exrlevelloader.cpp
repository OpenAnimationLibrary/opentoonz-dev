// SPDX-License-Identifier: BSD-3-Clause

#include "exrlevelloader.h"

#include "exrlevelsource.h"
#include "imagebuilders.h"

#include "toonz/fullcolorpalette.h"
#include "toonz/levelproperties.h"
#include "toonz/levelset.h"
#include "toonz/namebuilder.h"
#include "toonz/preferences.h"
#include "toonz/sceneproperties.h"
#include "toonz/toonzscene.h"
#include "toonz/txshexrlevel.h"
#include "toonz/txshleveltypes.h"

#include "toutputproperties.h"
#include "trasterfx.h"
#include "trop.h"
#include "tsystem.h"

#include <QString>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace {

struct SourceFrame {
  TFrameId fid;
  TFilePath path;
};

class LoadingLevelRangeReset final {
public:
  ~LoadingLevelRangeReset() { setLoadingLevelRange(TFrameId(1), TFrameId(0)); }
};

bool sameSourcePath(ToonzScene *scene, const TFilePath &left,
                    const TFilePath &right) {
  const TFilePath decodedLeft  = scene ? scene->decodeFilePath(left) : left;
  const TFilePath decodedRight = scene ? scene->decodeFilePath(right) : right;
  return decodedLeft.getParentDir() == decodedRight.getParentDir() &&
         decodedLeft.getLevelNameW() == decodedRight.getLevelNameW();
}

std::vector<SourceFrame> collectSourceFrames(const TFilePath &path) {
  std::vector<SourceFrame> result;
  try {
    const TFilePathSet files =
        TSystem::readDirectory(path.getParentDir(), false, true, true);
    for (const TFilePath &file : files) {
      if (file.getLevelNameW() == path.getLevelNameW())
        result.push_back({file.getFrame(), file});
    }
  } catch (...) {
    // A missing/unreadable source must not prevent its scene-level identity
    // from being restored. The level will simply have no available frames.
  }

  if (result.empty() && TFileStatus(path).doesExist())
    result.push_back({path.getFrame(), path});

  std::sort(result.begin(), result.end(),
            [](const SourceFrame &left, const SourceFrame &right) {
              if (left.fid != right.fid) return left.fid < right.fid;
              return left.path < right.path;
            });
  result.erase(
      std::unique(result.begin(), result.end(),
                  [](const SourceFrame &left, const SourceFrame &right) {
                    return left.fid == right.fid;
                  }),
      result.end());
  return result;
}

std::wstring fromUtf8(const std::string &value) {
  return QString::fromUtf8(value.c_str(), static_cast<int>(value.size()))
      .toStdWString();
}

std::wstring bindingLabel(const ExrLevelSource::PartInfo &part,
                          const std::string &layer, bool multipart) {
  std::wstring partLabel;
  if (!part.name.empty())
    partLabel = fromUtf8(part.name);
  else if (multipart)
    partLabel = L"Part " + std::to_wstring(part.index + 1);

  const std::wstring layerLabel = layer.empty() ? L"RGBA" : fromUtf8(layer);
  return partLabel.empty() ? layerLabel : partLabel + L" / " + layerLabel;
}

std::string lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string layerComponent(const std::string &layer,
                           const std::string &channel) {
  if (layer.empty())
    return channel.find('.') == std::string::npos ? channel : std::string();
  const std::string prefix = layer + '.';
  if (channel.compare(0, prefix.size(), prefix) != 0) return {};
  const std::string component = channel.substr(prefix.size());
  return component.find('.') == std::string::npos ? component : std::string();
}

bool isRawLayer(const ExrLevelSource::PartInfo &part,
                const std::string &layer) {
  bool hasRgb             = false;
  bool hasVectorComponent = false;
  for (const std::string &channel : part.channels) {
    const std::string component = lower(layerComponent(layer, channel));
    hasRgb |= component == "r" || component == "g" || component == "b";
    hasVectorComponent |= component == "x" || component == "z";
  }

  // XYZ/vector data is never display-referred color. A named non-RGB AOV is
  // data by default; an unlayered Y-only image remains grayscale.
  if (hasVectorComponent || (!layer.empty() && !hasRgb)) return true;

  // Studios commonly put an AOV's semantic name on the multipart part while
  // leaving its channels at the root (for example, part "normal" with RGB or
  // part "depth" with Y). Consider both identities before applying display
  // gamma.
  const std::string name               = lower(part.name + "." + layer);
  static const char *const dataNames[] = {
      "normal", "depth", "position", "vector", "velocity",
      "motion", "uv",    "id",       "mask",   "crypto"};
  for (const char *token : dataNames)
    if (name.find(token) != std::string::npos) return true;
  return false;
}

ExrLevelSource::Selection sourceSelection(
    const TXshExrLevel::Selection &selection,
    bool allowNamedPartFallback = false) {
  ExrLevelSource::Selection result;
  result.part = selection.part;
  result.partIndexFallback =
      !allowNamedPartFallback && selection.part.rfind("name:", 0) == 0
          ? -1
          : selection.partIndexFallback;
  result.layer = selection.layer;
  return result;
}

enum class ExrOutputDepth { Unknown, Byte, Word, Float };

ExrOutputDepth requestedDepth(int imFlags) {
  if (imFlags & ImageManager::isFloatEnabled) return ExrOutputDepth::Float;
  if (imFlags & ImageManager::is64bitEnabled) return ExrOutputDepth::Word;
  return ExrOutputDepth::Byte;
}

void copyImageMetadata(const TRasterImageP &destination,
                       const TRasterImageP &source) {
  destination->setSavebox(source->getSavebox());
  destination->setSubsampling(source->getSubsampling());
  double dpix = 0.0, dpiy = 0.0;
  source->getDpi(dpix, dpiy);
  destination->setDpi(dpix, dpiy);
}

TRasterImageP shrinkFloatImage(const TRasterImageP &image, int subsampling) {
  if (subsampling <= 1) {
    image->setSubsampling(1);
    return image;
  }

  const TRasterFP source = image->getRaster();
  if (!source) return {};

  const int lx = (source->getLx() - 1) / subsampling + 1;
  const int ly = (source->getLy() - 1) / subsampling + 1;
  TRasterFP raster(lx, ly);
  source->lock();
  raster->lock();
  for (int y = 0; y < ly; ++y) {
    const TPixelF *sourcePixel = source->pixels(y * subsampling);
    TPixelF *destinationPixel  = raster->pixels(y);
    for (int x = 0; x < lx; ++x)
      destinationPixel[x] = sourcePixel[x * subsampling];
  }
  raster->unlock();
  source->unlock();

  TRasterImageP result(raster);
  copyImageMetadata(result, image);
  TRect savebox = image->getSavebox();
  if (!savebox.isEmpty()) {
    // The shrunk raster contains source samples at 0, subsampling, ... .
    // Therefore the first content sample must be rounded up, while the last
    // one is rounded down.
    savebox.x0 = (savebox.x0 + subsampling - 1) / subsampling;
    savebox.y0 = (savebox.y0 + subsampling - 1) / subsampling;
    savebox.x1 /= subsampling;
    savebox.y1 /= subsampling;
  }
  result->setSavebox(savebox);
  result->setSubsampling(subsampling);
  return result;
}

TRasterImageP convertFloatImage(const TRasterImageP &image,
                                ExrOutputDepth depth) {
  if (depth == ExrOutputDepth::Float) return image;

  TRasterP raster;
  if (depth == ExrOutputDepth::Word)
    raster = TRaster64P(image->getRaster()->getSize());
  else
    raster = TRaster32P(image->getRaster()->getSize());
  TRop::convert(raster, image->getRaster());

  TRasterImageP result(raster);
  copyImageMetadata(result, image);
  return result;
}

class ExrLevelImageBuilder final : public ImageBuilder {
  TFilePath m_path;
  ExrLevelSource::Selection m_selection;
  bool m_raw;
  double m_gamma               = -1.0;
  int m_subsampling            = 0;
  ExrOutputDepth m_outputDepth = ExrOutputDepth::Unknown;

  double requestedGamma(void *extData) const {
    if (m_raw) return 1.0;
    if (!extData) return LevelOptions::DefaultColorSpaceGamma;
    const auto *data = static_cast<ImageLoader::BuildExtData *>(extData);
    return data->m_sl ? data->m_sl->getProperties()->colorSpaceGamma()
                      : LevelOptions::DefaultColorSpaceGamma;
  }

  int requestedSubsampling(int imFlags, void *extData) const {
    if (!extData) return 1;
    const auto *data = static_cast<ImageLoader::BuildExtData *>(extData);
    if (imFlags & ImageManager::toBeModified) return 1;
    if (data->m_subs > 0) return data->m_subs;
    if (m_subsampling > 0) return m_subsampling;
    return data->m_sl
               ? std::max(1, data->m_sl->getProperties()->getSubsampling())
               : 1;
  }

public:
  ExrLevelImageBuilder(const TFilePath &path,
                       const TXshExrLevel::Selection &selection)
      : m_path(path)
      , m_selection(sourceSelection(selection))
      , m_raw(selection.raw) {}

  bool isImageCompatible(int imFlags, void *extData) override {
    return m_subsampling > 0 &&
           areAlmostEqual(m_gamma, requestedGamma(extData)) &&
           m_subsampling == requestedSubsampling(imFlags, extData) &&
           m_outputDepth == requestedDepth(imFlags);
  }

protected:
  bool getInfo(TImageInfo &info, int, void *) override {
    return ExrLevelSource::getInfo(m_path, info, m_selection);
  }

  TImageP build(int imFlags, void *extData) override {
    try {
      const double gamma  = requestedGamma(extData);
      TRasterImageP image = ExrLevelSource::load(m_path, m_selection, gamma);
      if (!image) return {};

      const int subsampling      = requestedSubsampling(imFlags, extData);
      const ExrOutputDepth depth = requestedDepth(imFlags);
      TRasterImageP result       = shrinkFloatImage(image, subsampling);
      if (!result) return {};
      result = convertFloatImage(result, depth);
      if (!(imFlags & ImageManager::dontPutInCache)) {
        m_gamma       = gamma;
        m_subsampling = subsampling;
        m_outputDepth = depth;
      }
      return result;
    } catch (...) {
      return {};
    }
  }

  void invalidate() override {
    ImageBuilder::invalidate();
    m_gamma       = -1.0;
    m_subsampling = 0;
    m_outputDepth = ExrOutputDepth::Unknown;
  }
};

bool bindingMatches(const TXshExrLevel *level,
                    const ExrLevel::Binding &binding) {
  const TXshExrLevel::Selection &selection = level->getSelection();
  const bool namedPart = selection.part.rfind("name:", 0) == 0;
  return selection.part == binding.part &&
         (namedPart ||
          selection.partIndexFallback == binding.partIndexFallback) &&
         selection.layer == binding.layer && selection.raw == binding.raw;
}

TXshExrLevel *findAnyRetainedSource(ToonzScene *scene, const TFilePath &path) {
  if (!scene) return nullptr;
  TLevelSet *levels = scene->getLevelSet();
  for (int index = 0; index < levels->getLevelCount(); ++index) {
    auto *level = dynamic_cast<TXshExrLevel *>(levels->getLevel(index));
    if (level && sameSourcePath(scene, level->getPath(), path)) return level;
  }
  return nullptr;
}

std::wstring uniqueLevelName(ToonzScene *scene, std::wstring requested) {
  if (requested.empty()) requested = L"EXR";
  NameModifier names(requested);
  std::wstring candidate = names.getNext();
  while (scene->getLevelSet()->hasLevel(candidate)) candidate = names.getNext();
  return candidate;
}

void canonicalizeSelection(TXshExrLevel *level,
                           const TFilePath &representative) {
  TXshExrLevel::Selection selection = level->getSelection();
  try {
    if (selection.part.empty()) {
      std::vector<ExrLevel::Binding> bindings =
          ExrLevel::inspectBindings(representative);
      if (bindings.empty()) return;
      const ExrLevel::Binding &binding = bindings.front();
      selection.part                   = binding.part;
      selection.partIndexFallback      = binding.partIndexFallback;
      selection.layer                  = binding.layer;
      selection.raw                    = binding.raw;
      level->setSelection(selection, level->isPrimaryBinding());
      return;
    }

    const std::vector<ExrLevelSource::PartInfo> parts =
        ExrLevelSource::inspect(representative);
    // A named selector is an identity contract across the entire sequence.
    // Never retarget it to the representative frame's same-numbered part if
    // that frame happens to omit the name; a later frame may still carry it.
    const ExrLevelSource::PartInfo resolved =
        ExrLevelSource::resolve(parts, sourceSelection(selection));
    selection.part              = resolved.stableId;
    selection.partIndexFallback = resolved.index;
    level->setSelection(selection, level->isPrimaryBinding());
  } catch (...) {
    // Keep the serialized selector and level identity intact. Per-frame image
    // requests will remain empty until the source becomes available/valid.
  }
}

void restore(TXshExrLevel *level, const std::vector<TFrameId> *requestedFids) {
  if (!level) return;

  TFrameId rangeFrom, rangeTo;
  getLoadingLevelRange(rangeFrom, rangeTo);
  const bool rangeEnabled = rangeFrom <= rangeTo;
  LoadingLevelRangeReset resetLoadingRange;

  ToonzScene *scene = level->getScene();
  const TFilePath path =
      scene ? scene->decodeFilePath(level->getPath()) : level->getPath();
  const std::vector<SourceFrame> sources = collectSourceFrames(path);

  if (scene && !level->getPalette())
    level->setPalette(FullColorPalette::instance()->getPalette(scene));

  std::vector<TFrameId> oldFids;
  level->getFids(oldFids);
  for (const TFrameId &fid : oldFids)
    ImageManager::instance()->unbind(level->getImageId(fid));
  level->clearFrames();

  level->setFloatChannelLevel(true);
  level->setIsReadOnly(true);
  if (sources.empty()) {
    level->getProperties()->setDirtyFlag(false);
    return;
  }

  canonicalizeSelection(level, sources.front().path);
  const TXshExrLevel::Selection selection = level->getSelection();

  bool initializedInfo = false;
  for (const SourceFrame &source : sources) {
    if (rangeEnabled && (source.fid < rangeFrom || rangeTo < source.fid))
      continue;
    if (requestedFids && std::find(requestedFids->begin(), requestedFids->end(),
                                   source.fid) == requestedFids->end())
      continue;

    const std::string imageId = level->getImageId(source.fid);
    ImageManager::instance()->bind(
        imageId, new ExrLevelImageBuilder(source.path, selection));
    // setFrame observes the retained binding and therefore does not install
    // the generic flattened ImageLoader for this frame.
    level->setFrame(source.fid, TImageP());

    if (!initializedInfo) {
      TImageInfo info;
      if (ExrLevelSource::getInfo(source.path, info,
                                  sourceSelection(selection))) {
        level->getProperties()->setImageRes(TDimension(info.m_lx, info.m_ly));
        level->getProperties()->setBpp(info.m_bitsPerSample *
                                       info.m_samplePerPixel);
        initializedInfo = true;
      }
    }
  }

  level->setRenumberTable();
  level->getProperties()->setDirtyFlag(false);
}

}  // namespace

namespace ExrLevel {

std::vector<Binding> inspectBindings(const TFilePath &path,
                                     std::string *error) {
  try {
    const std::vector<SourceFrame> frames = collectSourceFrames(path);
    if (frames.empty())
      throw std::runtime_error("The EXR source does not exist");

    const std::vector<ExrLevelSource::PartInfo> parts =
        ExrLevelSource::inspect(frames.front().path);
    std::vector<Binding> result;
    std::string validationError;
    const bool multipart = parts.size() > 1;
    for (const ExrLevelSource::PartInfo &part : parts) {
      if (!part.supported || part.deep) {
        std::string partError;
        ExrLevelSource::validateLayer(part, {}, &partError);
        if (validationError.empty()) validationError = std::move(partError);
        continue;
      }

      auto addLayer = [&](const std::string &layer) {
        std::string layerError;
        if (!ExrLevelSource::validateLayer(part, layer, &layerError)) {
          if (validationError.empty()) validationError = std::move(layerError);
          return;
        }
        Binding binding;
        binding.part              = part.stableId;
        binding.partIndexFallback = part.index;
        binding.layer             = layer;
        binding.label             = bindingLabel(part, layer, multipart);
        binding.raw               = isRawLayer(part, layer);
        result.push_back(std::move(binding));
      };

      if (part.hasBaseLayer) addLayer({});
      for (const std::string &layer : part.layers) addLayer(layer);
    }

    if (result.empty())
      throw std::runtime_error(
          validationError.empty()
              ? "The EXR contains no supported flat image part/layer"
              : validationError);
    result.front().primary = true;
    if (error) error->clear();
    return result;
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
  } catch (...) {
    if (error) *error = "Unable to inspect the EXR source";
  }
  return {};
}

TXshExrLevel *findRetainedLevel(ToonzScene *scene, const TFilePath &path,
                                const Binding &binding) {
  if (!scene) return nullptr;
  TLevelSet *levels = scene->getLevelSet();
  for (int index = 0; index < levels->getLevelCount(); ++index) {
    auto *level = dynamic_cast<TXshExrLevel *>(levels->getLevel(index));
    if (level && sameSourcePath(scene, level->getPath(), path) &&
        bindingMatches(level, binding))
      return level;
  }
  return nullptr;
}

TXshExrLevel *loadRetainedLevel(ToonzScene *scene, const TFilePath &path,
                                const Binding &binding,
                                const std::wstring &levelName,
                                const std::vector<TFrameId> &fids,
                                const LevelOptions *levelOptions) {
  if (!scene) return nullptr;
  if (TXshExrLevel *existing = findRetainedLevel(scene, path, binding))
    return existing;

  std::wstring requested = levelName;
  if (requested.empty()) {
    requested = path.getWideName();
    if (!binding.primary && !binding.label.empty())
      requested += L" [" + binding.label + L"]";
  }

  auto *level = new TXshExrLevel(uniqueLevelName(scene, requested));
  level->setScene(scene);
  level->setType(OVL_XSHLEVEL);

  TXshExrLevel::Selection selection;
  selection.part              = binding.part;
  selection.partIndexFallback = binding.partIndexFallback;
  selection.layer             = binding.layer;
  selection.raw               = binding.raw;
  level->setSelection(selection, binding.primary);
  level->setPath(scene->codeFilePath(path), true);

  LevelProperties *properties = level->getProperties();
  TXshExrLevel *sourceLevel   = findAnyRetainedSource(scene, path);
  bool formatSpecified        = false;
  if (sourceLevel) {
    properties->options() = sourceLevel->getProperties()->options();
    level->clonePropertiesFrom(sourceLevel);
    // With no new options, every sibling must inherit the representative's
    // interpretation exactly, including any user edits made after loading.
    formatSpecified = !levelOptions;
  }

  if (levelOptions) {
    properties->options() = *levelOptions;
  } else if (!sourceLevel) {
    const Preferences &preferences = *Preferences::instance();
    const int formatIndex          = preferences.matchLevelFormat(path);
    if (formatIndex >= 0) {
      properties->options() = preferences.levelFormat(formatIndex).m_options;
      formatSpecified       = true;
    } else {
      properties->setSubsampling(
          scene->getProperties()->getFullcolorSubsampling());
    }
  }

  level->setPalette(
      FullColorPalette::instance()->getPalette(level->getScene()));
  if (fids.empty())
    level->load();
  else
    level->load(fids);

  if (properties->getDpiPolicy() == LevelProperties::DP_ImageDpi) {
    const TPointD imageDpi = level->getImageDpi();
    if (imageDpi == TPointD() ||
        Preferences::instance()->getUnits() == "pixel" ||
        Preferences::instance()->isIgnoreImageDpiEnabled()) {
      properties->setDpiPolicy(LevelProperties::DP_CustomDpi);
      properties->setDpi(scene->getCurrentCamera()->getDpi());
    } else {
      properties->setDpi(imageDpi);
    }
  }

  if (!formatSpecified)
    properties->setColorSpaceGamma(scene->getProperties()
                                       ->getOutputProperties()
                                       ->getRenderSettings()
                                       .m_colorSpaceGamma);

  scene->getLevelSet()->insertLevel(level);
  return level;
}

void restoreRetainedLevel(TXshExrLevel *level) { restore(level, nullptr); }

void restoreRetainedLevel(TXshExrLevel *level,
                          const std::vector<TFrameId> &fids) {
  restore(level, &fids);
}

bool saveRetainedCopy(TXshExrLevel *level, const TFilePath &path,
                      const TFilePath &oldPath, bool overwrite) {
  if (!level) return false;
  ToonzScene *scene = level->getScene();
  const TFilePath source =
      scene ? scene->decodeFilePath(oldPath.isEmpty() ? level->getPath()
                                                      : oldPath)
            : (oldPath.isEmpty() ? level->getPath() : oldPath);
  const TFilePath destination = scene ? scene->decodeFilePath(path) : path;

  if (sameSourcePath(nullptr, source, destination)) return true;
  const std::vector<SourceFrame> sources = collectSourceFrames(source);
  if (sources.empty() || !TSystem::touchParentDir(destination)) return false;

  std::vector<std::pair<TFilePath, TFilePath>> copies;
  copies.reserve(sources.size());
  for (const SourceFrame &frame : sources) {
    const TFilePath target = frame.fid == TFrameId::NO_FRAME
                                 ? destination
                                 : destination.withFrame(frame.fid);
    if (!overwrite && TFileStatus(target).doesExist()) return false;
    copies.emplace_back(target, frame.path);
  }

  for (const auto &copy : copies) {
    if (copy.first != copy.second)
      TSystem::copyFile(copy.first, copy.second, overwrite);
  }
  return true;
}

}  // namespace ExrLevel
