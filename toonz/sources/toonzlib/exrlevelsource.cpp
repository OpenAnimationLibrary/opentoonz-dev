// SPDX-License-Identifier: BSD-3-Clause

#include "exrlevelsource.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTiledInputPart.h>

#include <QFile>
#include <QString>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <utility>

namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;

class QFileInputStream final : public Exr::IStream {
  QFile m_file;

public:
  explicit QFileInputStream(const TFilePath &path)
      : Exr::IStream("OpenToonz EXR level stream"), m_file(path.getQString()) {
    if (!m_file.open(QIODevice::ReadOnly))
      throw std::runtime_error(QString("Cannot open EXR file '%1': %2")
                                   .arg(path.getQString(), m_file.errorString())
                                   .toUtf8()
                                   .toStdString());
  }

  bool read(char buffer[], int size) override {
    if (size < 0) throw std::invalid_argument("Invalid EXR read size");
    if (size == 0) return m_file.pos() < m_file.size();
    const qint64 actual = m_file.read(buffer, size);
    if (actual != size) {
      if (actual < 0)
        throw std::runtime_error(QString("EXR read failed: %1")
                                     .arg(m_file.errorString())
                                     .toUtf8()
                                     .toStdString());
      throw std::runtime_error("Unexpected end of EXR file");
    }
    return m_file.pos() < m_file.size();
  }

  std::uint64_t tellg() override {
    const qint64 position = m_file.pos();
    if (position < 0)
      throw std::runtime_error("Cannot query EXR file position");
    return static_cast<std::uint64_t>(position);
  }

  void seekg(std::uint64_t position) override {
    if (position >
            static_cast<std::uint64_t>(std::numeric_limits<qint64>::max()) ||
        !m_file.seek(static_cast<qint64>(position)))
      throw std::runtime_error("Cannot seek in EXR file");
  }

  void clear() override { m_file.unsetError(); }
  std::int64_t size() override { return m_file.size(); }
};

ExrLevelSource::Window window(const IMATH_NAMESPACE::Box2i &box) {
  return {box.min.x, box.min.y, box.max.x, box.max.y};
}

std::size_t checkedPixelCount(const ExrLevelSource::Window &box,
                              const char *description) {
  const std::int64_t width = static_cast<std::int64_t>(box.xMax) - box.xMin + 1;
  const std::int64_t height =
      static_cast<std::int64_t>(box.yMax) - box.yMin + 1;
  if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() ||
      height > std::numeric_limits<int>::max() ||
      static_cast<std::uint64_t>(width) >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) /
              static_cast<std::uint64_t>(height))
    throw std::runtime_error(std::string("Invalid or excessively large EXR ") +
                             description);
  return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

std::string pixelTypeName(Exr::PixelType type) {
  switch (type) {
  case Exr::UINT:
    return "uint";
  case Exr::HALF:
    return "half";
  case Exr::FLOAT:
    return "float";
  default:
    return "unknown";
  }
}

std::string headerType(const Exr::Header &header) {
  if (header.hasType()) return header.type();
  return header.hasTileDescription() ? std::string(Exr::TILEDIMAGE)
                                     : std::string(Exr::SCANLINEIMAGE);
}

ExrLevelSource::PartInfo partInfo(const Exr::Header &header, int index) {
  ExrLevelSource::PartInfo result;
  result.index    = index;
  result.name     = header.hasName() ? header.name() : std::string();
  result.stableId = result.name.empty() ? "part:" + std::to_string(index)
                                        : "name:" + result.name;
  result.type     = headerType(header);
  result.tiled = result.type == Exr::TILEDIMAGE || result.type == Exr::DEEPTILE;
  result.deep =
      result.type == Exr::DEEPSCANLINE || result.type == Exr::DEEPTILE;
  result.supported =
      result.type == Exr::SCANLINEIMAGE || result.type == Exr::TILEDIMAGE;
  result.displayWindow = window(header.displayWindow());
  result.dataWindow    = window(header.dataWindow());
  checkedPixelCount(result.displayWindow, "display window");
  checkedPixelCount(result.dataWindow, "data window");

  std::set<std::string> layers;
  header.channels().layers(layers);
  result.layers.assign(layers.begin(), layers.end());
  for (Exr::ChannelList::ConstIterator channel = header.channels().begin();
       channel != header.channels().end(); ++channel) {
    const std::string name = channel.name();
    const std::size_t dot  = name.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size())
      result.hasBaseLayer = true;
    result.channels.push_back(name);
    result.channelInfo.push_back({name, pixelTypeName(channel.channel().type),
                                  channel.channel().xSampling,
                                  channel.channel().ySampling});
  }
  return result;
}

class Document {
  std::unique_ptr<QFileInputStream> m_stream;
  std::unique_ptr<Exr::MultiPartInputFile> m_input;
  std::vector<ExrLevelSource::PartInfo> m_parts;

public:
  explicit Document(const TFilePath &path)
      : m_stream(std::make_unique<QFileInputStream>(path))
      , m_input(std::make_unique<Exr::MultiPartInputFile>(*m_stream)) {
    if (m_input->parts() <= 0)
      throw std::runtime_error("EXR contains no image parts");
    m_parts.reserve(m_input->parts());
    for (int index = 0; index < m_input->parts(); ++index)
      m_parts.push_back(partInfo(m_input->header(index), index));
  }

  const std::vector<ExrLevelSource::PartInfo> &parts() const { return m_parts; }

  std::map<std::string, std::vector<float>> readPlanes(
      int partIndex, const std::array<std::string, 4> &channelNames) {
    const ExrLevelSource::PartInfo &part = m_parts.at(partIndex);
    if (part.deep)
      throw std::runtime_error(
          "Deep EXR parts require an explicit flattening policy and cannot "
          "be loaded as an OpenToonz raster yet");
    if (!part.supported)
      throw std::runtime_error("Unsupported EXR part type: " + part.type);

    const IMATH_NAMESPACE::Box2i dataWindow(
        IMATH_NAMESPACE::V2i(part.dataWindow.xMin, part.dataWindow.yMin),
        IMATH_NAMESPACE::V2i(part.dataWindow.xMax, part.dataWindow.yMax));
    const std::size_t pixelCount =
        checkedPixelCount(part.dataWindow, "data window");
    std::map<std::string, std::vector<float>> values;
    Exr::FrameBuffer frameBuffer;
    for (const std::string &channelName : channelNames) {
      if (channelName.empty() || values.count(channelName)) continue;
      const auto metadata =
          std::find_if(part.channelInfo.begin(), part.channelInfo.end(),
                       [&](const ExrLevelSource::ChannelInfo &channel) {
                         return channel.name == channelName;
                       });
      if (metadata == part.channelInfo.end())
        throw std::runtime_error("EXR channel was not found: " + channelName);
      if (metadata->xSampling != 1 || metadata->ySampling != 1)
        throw std::runtime_error(
            "Subsampled EXR channels are not supported by the EXR level "
            "decoder: " +
            channelName);

      auto inserted =
          values.emplace(channelName, std::vector<float>(pixelCount));
      frameBuffer.insert(
          channelName,
          Exr::Slice::Make(Exr::FLOAT, inserted.first->second.data(),
                           dataWindow, sizeof(float),
                           static_cast<std::size_t>(part.dataWindow.width()) *
                               sizeof(float)));
    }
    if (values.empty())
      throw std::runtime_error("Selected EXR layer contains no image channels");

    if (part.type == Exr::TILEDIMAGE) {
      Exr::TiledInputPart input(*m_input, partIndex);
      input.setFrameBuffer(frameBuffer);
      input.readTiles(0, input.numXTiles(0) - 1, 0, input.numYTiles(0) - 1, 0);
    } else {
      Exr::InputPart input(*m_input, partIndex);
      input.setFrameBuffer(frameBuffer);
      input.readPixels(part.dataWindow.yMin, part.dataWindow.yMax);
    }
    return values;
  }
};

std::string lower(std::string value) {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

std::string layerComponent(const std::string &layer,
                           const std::string &channel) {
  if (layer.empty()) {
    const std::size_t dot = channel.rfind('.');
    return dot == std::string::npos ? channel : std::string();
  }
  const std::string prefix = layer + '.';
  if (channel.compare(0, prefix.size(), prefix) != 0) return {};
  const std::string component = channel.substr(prefix.size());
  return component.find('.') == std::string::npos ? component : std::string();
}

std::string findComponent(const ExrLevelSource::PartInfo &part,
                          const std::string &layer,
                          const std::string &component) {
  const std::string wanted = lower(component);
  for (const std::string &channel : part.channels)
    if (lower(layerComponent(layer, channel)) == wanted) return channel;
  return {};
}

struct LayerPlanes {
  std::map<std::string, std::vector<float>> values;
  std::array<std::string, 4> rgba;
  std::array<float, 4> fill = {0.0f, 0.0f, 0.0f, 1.0f};

  float at(int component, std::size_t index) const {
    if (rgba[component].empty()) return fill[component];
    return values.at(rgba[component])[index];
  }
};

struct LayerSelection {
  std::string layer;
  std::array<std::string, 4> rgba;
};

const ExrLevelSource::ChannelInfo &channelInfo(
    const ExrLevelSource::PartInfo &part, const std::string &name) {
  const auto found =
      std::find_if(part.channelInfo.begin(), part.channelInfo.end(),
                   [&](const ExrLevelSource::ChannelInfo &channel) {
                     return channel.name == name;
                   });
  if (found == part.channelInfo.end())
    throw std::runtime_error("EXR channel metadata was not found: " + name);
  return *found;
}

LayerSelection selectLayer(const ExrLevelSource::PartInfo &part,
                           std::string layer) {
  if (part.deep)
    throw std::runtime_error(
        "Deep EXR parts require an explicit flattening policy and cannot be "
        "loaded as an OpenToonz raster yet");
  if (!part.supported)
    throw std::runtime_error("Unsupported EXR part type: " + part.type);

  if (layer.empty() && !part.hasBaseLayer)
    throw std::runtime_error("EXR part does not contain a base image layer");

  const std::string r                 = findComponent(part, layer, "R");
  const std::string g                 = findComponent(part, layer, "G");
  const std::string b                 = findComponent(part, layer, "B");
  const std::string a                 = findComponent(part, layer, "A");
  std::array<std::string, 4> selected = {r, g, b, a};

  if (r.empty() && g.empty() && b.empty()) {
    const std::string x = findComponent(part, layer, "X");
    const std::string y = findComponent(part, layer, "Y");
    const std::string z = findComponent(part, layer, "Z");
    const int xyzCount  = !x.empty() + !y.empty() + !z.empty();
    if (xyzCount >= 2) {
      selected[0] = x;
      selected[1] = y;
      selected[2] = z;
    } else if (!y.empty()) {
      selected[0] = selected[1] = selected[2] = y;
    } else {
      std::vector<std::string> candidates;
      for (const std::string &channel : part.channels) {
        const std::string component = layerComponent(layer, channel);
        if (!component.empty() && lower(component) != "a")
          candidates.push_back(channel);
      }
      if (candidates.size() == 1)
        selected[0] = selected[1] = selected[2] = candidates.front();
      else
        throw std::runtime_error(
            "Selected EXR layer does not contain a recognizable RGBA, Y, or "
            "XYZ channel set");
    }
  }

  bool hasColor = false;
  for (int component = 0; component < 4; ++component) {
    if (selected[component].empty()) continue;
    const ExrLevelSource::ChannelInfo &metadata =
        channelInfo(part, selected[component]);
    if (metadata.xSampling != 1 || metadata.ySampling != 1)
      throw std::runtime_error(
          "Subsampled EXR channels are not supported by the EXR level "
          "decoder: " +
          selected[component]);
    hasColor |= component < 3;
  }
  if (!hasColor)
    throw std::runtime_error("Selected EXR layer contains no image channels");

  return {std::move(layer), std::move(selected)};
}

LayerPlanes readLayer(Document &document, const ExrLevelSource::PartInfo &part,
                      const std::string &layer) {
  const LayerSelection selected = selectLayer(part, layer);
  LayerPlanes result;
  result.rgba   = selected.rgba;
  result.values = document.readPlanes(part.index, selected.rgba);
  return result;
}

float toDisplay(float value, double gamma) {
  if (value < 0.0f || gamma == 1.0) return value;
  return static_cast<float>(std::pow(static_cast<double>(value), 1.0 / gamma));
}

TRect contentRect(const ExrLevelSource::PartInfo &part) {
  const int xMin = std::max(part.displayWindow.xMin, part.dataWindow.xMin);
  const int yMin = std::max(part.displayWindow.yMin, part.dataWindow.yMin);
  const int xMax = std::min(part.displayWindow.xMax, part.dataWindow.xMax);
  const int yMax = std::min(part.displayWindow.yMax, part.dataWindow.yMax);
  if (xMin > xMax || yMin > yMax) return TRect();
  return TRect(xMin - part.displayWindow.xMin, part.displayWindow.yMax - yMax,
               xMax - part.displayWindow.xMin, part.displayWindow.yMax - yMin);
}

void fillInfo(const ExrLevelSource::PartInfo &part, TImageInfo &info) {
  info = TImageInfo(part.displayWindow.width(), part.displayWindow.height());
  const TRect content   = contentRect(part);
  info.m_x0             = content.x0;
  info.m_y0             = content.y0;
  info.m_x1             = content.x1;
  info.m_y1             = content.y1;
  info.m_samplePerPixel = 4;
  info.m_bitsPerSample  = 32;
  info.m_valid          = true;
}
}  // namespace

namespace ExrLevelSource {

std::vector<PartInfo> inspect(const TFilePath &path) {
  return Document(path).parts();
}

PartInfo resolve(const std::vector<PartInfo> &parts,
                 const Selection &selection) {
  if (parts.empty()) throw std::runtime_error("EXR contains no image parts");
  if (!selection.part.empty()) {
    const auto found =
        std::find_if(parts.begin(), parts.end(), [&](const PartInfo &part) {
          return part.stableId == selection.part || part.name == selection.part;
        });
    if (found != parts.end()) return *found;
  }
  if (selection.partIndexFallback >= 0 &&
      selection.partIndexFallback < static_cast<int>(parts.size()))
    return parts[selection.partIndexFallback];
  throw std::runtime_error("EXR part selection cannot be resolved");
}

bool validateLayer(const PartInfo &part, const std::string &layer,
                   std::string *error) {
  try {
    selectLayer(part, layer);
    if (error) error->clear();
    return true;
  } catch (const std::exception &exception) {
    if (error) *error = exception.what();
    return false;
  }
}

TRasterImageP load(const TFilePath &path, const Selection &selection,
                   double colorSpaceGamma) {
  if (!(colorSpaceGamma > 0.0) || !std::isfinite(colorSpaceGamma))
    throw std::invalid_argument("EXR color-space gamma must be positive");

  Document document(path);
  const PartInfo part = resolve(document.parts(), selection);
  LayerPlanes planes  = readLayer(document, part, selection.layer);

  TRasterFP raster(part.displayWindow.width(), part.displayWindow.height());
  raster->lock();
  raster->clear();
  const int dataWidth = part.dataWindow.width();
  for (int rasterY = 0; rasterY < raster->getLy(); ++rasterY) {
    TPixelF *row   = raster->pixels(rasterY);
    const int exrY = part.displayWindow.yMax - rasterY;
    if (exrY < part.dataWindow.yMin || exrY > part.dataWindow.yMax) continue;
    for (int rasterX = 0; rasterX < raster->getLx(); ++rasterX) {
      const int exrX = part.displayWindow.xMin + rasterX;
      if (exrX < part.dataWindow.xMin || exrX > part.dataWindow.xMax) continue;
      const std::size_t index =
          static_cast<std::size_t>(exrY - part.dataWindow.yMin) * dataWidth +
          static_cast<std::size_t>(exrX - part.dataWindow.xMin);
      row[rasterX].r = toDisplay(planes.at(0, index), colorSpaceGamma);
      row[rasterX].g = toDisplay(planes.at(1, index), colorSpaceGamma);
      row[rasterX].b = toDisplay(planes.at(2, index), colorSpaceGamma);
      row[rasterX].m = planes.at(3, index);
    }
  }
  raster->unlock();

  TRasterImageP image(raster);
  image->setSavebox(contentRect(part));
  return image;
}

bool getInfo(const TFilePath &path, TImageInfo &info,
             const Selection &selection, std::string *error) {
  try {
    const std::vector<PartInfo> parts = inspect(path);
    const PartInfo part               = resolve(parts, selection);
    std::string validationError;
    if (!validateLayer(part, selection.layer, &validationError))
      throw std::runtime_error(validationError);
    fillInfo(part, info);
    if (error) error->clear();
    return true;
  } catch (const std::exception &exception) {
    info = TImageInfo();
    if (error) *error = exception.what();
    return false;
  }
}

}  // namespace ExrLevelSource
