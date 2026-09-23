// SPDX-License-Identifier: BSD-3-Clause

#include "stdfx.h"
#include "tfxaovsource.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"

#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfHeader.h>
#include <OpenEXR/ImfIO.h>
#include <OpenEXR/ImfInputPart.h>
#include <OpenEXR/ImfMultiPartInputFile.h>
#include <OpenEXR/ImfPartType.h>
#include <OpenEXR/ImfTiledInputPart.h>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QStringList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
namespace Exr = OPENEXR_IMF_NAMESPACE;

class QFileInputStream final : public Exr::IStream {
  QFile m_file;

public:
  explicit QFileInputStream(const QString &path)
      : Exr::IStream("OpenToonz EXR AOV stream"), m_file(path) {
    if (!m_file.open(QIODevice::ReadOnly))
      throw std::runtime_error(QString("Cannot open EXR file: %1")
                                   .arg(m_file.errorString())
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
    return std::uint64_t(position);
  }

  void seekg(std::uint64_t position) override {
    if (position > std::uint64_t(std::numeric_limits<qint64>::max()) ||
        !m_file.seek(qint64(position)))
      throw std::runtime_error("Cannot seek in EXR file");
  }

  void clear() override { m_file.unsetError(); }
  std::int64_t size() override { return m_file.size(); }
};

struct ExrChannelInfo {
  Exr::PixelType type = Exr::HALF;
  int xSampling       = 1;
  int ySampling       = 1;
};

struct ExrPartInfo {
  std::string name;
  std::string type;
  IMATH_NAMESPACE::Box2i displayWindow;
  IMATH_NAMESPACE::Box2i dataWindow;
  std::vector<std::string> channels;
  std::vector<std::string> layers;
  std::map<std::string, ExrChannelInfo> channelInfo;
  bool hasBaseLayer = false;
};

std::size_t checkedPixelCount(const IMATH_NAMESPACE::Box2i &window) {
  const std::int64_t width =
      std::int64_t(window.max.x) - std::int64_t(window.min.x) + 1;
  const std::int64_t height =
      std::int64_t(window.max.y) - std::int64_t(window.min.y) + 1;
  if (width <= 0 || height <= 0 ||
      width > std::int64_t(std::numeric_limits<int>::max()) ||
      height > std::int64_t(std::numeric_limits<int>::max()) ||
      std::uint64_t(width) >
          std::uint64_t(std::numeric_limits<std::size_t>::max()) /
              std::uint64_t(height))
    throw std::runtime_error("Invalid or excessively large EXR image window");
  return std::size_t(width) * std::size_t(height);
}

QString pixelTypeName(Exr::PixelType type) {
  switch (type) {
  case Exr::UINT:
    return QStringLiteral("uint");
  case Exr::HALF:
    return QStringLiteral("half");
  case Exr::FLOAT:
    return QStringLiteral("float");
  default:
    return QStringLiteral("unknown");
  }
}

QString partTypeName(const std::string &type) {
  if (type == Exr::SCANLINEIMAGE) return QStringLiteral("scanline");
  if (type == Exr::TILEDIMAGE) return QStringLiteral("tiled");
  if (type == Exr::DEEPSCANLINE) return QStringLiteral("deep scanline");
  if (type == Exr::DEEPTILE) return QStringLiteral("deep tiled");
  return QString::fromUtf8(type.c_str());
}

class ExrDocument {
  std::unique_ptr<QFileInputStream> m_stream;
  std::unique_ptr<Exr::MultiPartInputFile> m_input;
  std::vector<ExrPartInfo> m_parts;
  std::map<std::pair<int, std::string>,
           std::shared_ptr<const std::vector<float>>>
      m_planes;
  mutable QMutex m_mutex;

public:
  explicit ExrDocument(const QString &path) {
    m_stream = std::make_unique<QFileInputStream>(path);
    m_input  = std::make_unique<Exr::MultiPartInputFile>(*m_stream);
    if (m_input->parts() <= 0)
      throw std::runtime_error("EXR contains no image parts");

    m_parts.reserve(m_input->parts());
    for (int p = 0; p < m_input->parts(); ++p) {
      const Exr::Header &header = m_input->header(p);
      ExrPartInfo part;
      part.name          = header.hasName() ? header.name() : std::string();
      part.type          = header.hasType() ? header.type()
                                            : (header.hasTileDescription()
                                                   ? std::string(Exr::TILEDIMAGE)
                                                   : std::string(Exr::SCANLINEIMAGE));
      part.displayWindow = header.displayWindow();
      part.dataWindow    = header.dataWindow();
      checkedPixelCount(part.displayWindow);
      checkedPixelCount(part.dataWindow);

      std::set<std::string> layers;
      header.channels().layers(layers);
      part.layers.assign(layers.begin(), layers.end());
      for (Exr::ChannelList::ConstIterator channel = header.channels().begin();
           channel != header.channels().end(); ++channel) {
        const std::string name = channel.name();
        const std::size_t dot  = name.rfind('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 == name.size())
          part.hasBaseLayer = true;
        part.channels.push_back(name);
        part.channelInfo.emplace(
            name,
            ExrChannelInfo{channel.channel().type, channel.channel().xSampling,
                           channel.channel().ySampling});
      }
      m_parts.push_back(std::move(part));
    }
  }

  const std::vector<ExrPartInfo> &parts() const { return m_parts; }

  std::shared_ptr<const std::vector<float>> plane(int part,
                                                  const std::string &channel) {
    QMutexLocker lock(&m_mutex);
    if (part < 0 || part >= int(m_parts.size()))
      throw std::runtime_error("EXR part index is out of range");
    const auto key    = std::make_pair(part, channel);
    const auto cached = m_planes.find(key);
    if (cached != m_planes.end()) return cached->second;

    const ExrPartInfo &info = m_parts[part];
    const auto channelIt    = info.channelInfo.find(channel);
    if (channelIt == info.channelInfo.end())
      throw std::runtime_error("EXR channel was not found: " + channel);
    if (channelIt->second.xSampling != 1 || channelIt->second.ySampling != 1)
      throw std::runtime_error(
          "Subsampled EXR channels are not yet supported by EXR AOV: " +
          channel);
    if (info.type == Exr::DEEPSCANLINE || info.type == Exr::DEEPTILE)
      throw std::runtime_error(
          "Deep EXR parts are detected but cannot be flattened into an "
          "OpenToonz RGBA tile yet. Select a flat part.");

    auto values = std::make_shared<std::vector<float>>(
        checkedPixelCount(info.dataWindow));
    const std::int64_t width =
        std::int64_t(info.dataWindow.max.x) - info.dataWindow.min.x + 1;
    Exr::FrameBuffer frameBuffer;
    frameBuffer.insert(
        channel,
        Exr::Slice::Make(Exr::FLOAT, values->data(), info.dataWindow,
                         sizeof(float), std::size_t(width) * sizeof(float)));

    if (info.type == Exr::TILEDIMAGE) {
      Exr::TiledInputPart input(*m_input, part);
      input.setFrameBuffer(frameBuffer);
      input.readTiles(0, input.numXTiles(0) - 1, 0, input.numYTiles(0) - 1, 0);
    } else if (info.type == Exr::SCANLINEIMAGE) {
      Exr::InputPart input(*m_input, part);
      input.setFrameBuffer(frameBuffer);
      input.readPixels(info.dataWindow.min.y, info.dataWindow.max.y);
    } else {
      throw std::runtime_error("Unsupported EXR part type: " + info.type);
    }

    auto result = std::shared_ptr<const std::vector<float>>(values);
    m_planes.emplace(key, result);
    return result;
  }
};

QMutex &documentRegistryMutex() {
  static QMutex mutex;
  return mutex;
}

std::map<std::string, std::weak_ptr<ExrDocument>> &documentRegistry() {
  static std::map<std::string, std::weak_ptr<ExrDocument>> registry;
  return registry;
}

std::shared_ptr<ExrDocument> sharedDocument(const QString &revision,
                                            const QString &path) {
  const std::string key = revision.toUtf8().toStdString();
  {
    QMutexLocker lock(&documentRegistryMutex());
    const auto found = documentRegistry().find(key);
    if (found != documentRegistry().end()) {
      if (auto document = found->second.lock()) return document;
    }
  }

  auto created = std::make_shared<ExrDocument>(path);
  QMutexLocker lock(&documentRegistryMutex());
  auto &entry = documentRegistry()[key];
  if (auto document = entry.lock()) return document;
  entry = created;
  if (documentRegistry().size() > 32) {
    for (auto it = documentRegistry().begin();
         it != documentRegistry().end();) {
      if (it->second.expired())
        it = documentRegistry().erase(it);
      else
        ++it;
    }
  }
  return created;
}

bool equalIgnoreCase(const std::string &left, const std::string &right) {
  return QString::fromUtf8(left.c_str())
             .compare(QString::fromUtf8(right.c_str()), Qt::CaseInsensitive) ==
         0;
}

std::string layerComponent(const std::string &layer,
                           const std::string &channel) {
  if (layer.empty()) {
    const std::size_t dot = channel.rfind('.');
    return dot == std::string::npos || dot == 0 || dot + 1 == channel.size()
               ? channel
               : std::string();
  }
  const std::string prefix = layer + '.';
  if (channel.compare(0, prefix.size(), prefix) != 0) return {};
  return channel.substr(prefix.size());
}

std::string findComponent(const ExrPartInfo &part, const std::string &layer,
                          const std::string &component) {
  const std::string exact = layer.empty() ? component : layer + '.' + component;
  if (part.channelInfo.count(exact)) return exact;
  for (const std::string &channel : part.channels) {
    if (equalIgnoreCase(layerComponent(layer, channel), component))
      return channel;
  }
  return {};
}

struct Plane {
  std::shared_ptr<const std::vector<float>> values;
  float fill = 0.0f;

  float at(std::size_t index) const { return values ? (*values)[index] : fill; }
};

struct LayerPlanes {
  std::array<Plane, 4> rgba;
};

LayerPlanes loadLayer(ExrDocument &document, int partIndex,
                      const std::string &layer) {
  const ExrPartInfo &part = document.parts().at(partIndex);
  LayerPlanes result;
  result.rgba[3].fill = 1.0f;

  std::array<std::string, 4> names = {
      findComponent(part, layer, "R"), findComponent(part, layer, "G"),
      findComponent(part, layer, "B"), findComponent(part, layer, "A")};
  if (names[0].empty() && names[1].empty() && names[2].empty()) {
    const std::string x = findComponent(part, layer, "X");
    const std::string y = findComponent(part, layer, "Y");
    const std::string z = findComponent(part, layer, "Z");
    if (!x.empty() || !z.empty()) {
      names[0] = x;
      names[1] = y;
      names[2] = z;
    } else if (!y.empty()) {
      names[0] = names[1] = names[2] = y;
    } else {
      for (const std::string &channel : part.channels) {
        if (!layerComponent(layer, channel).empty()) {
          names[0] = names[1] = names[2] = channel;
          break;
        }
      }
    }
  }
  for (int c = 0; c < 4; ++c)
    if (!names[c].empty())
      result.rgba[c].values = document.plane(partIndex, names[c]);
  return result;
}

float integerChannel(float value) {
  if (std::isnan(value)) return 0.0f;
  if (!std::isfinite(value)) return value > 0.0f ? 1.0f : 0.0f;
  return std::clamp(value, 0.0f, 1.0f);
}

template <class PIXEL>
void writePixel(PIXEL &pixel, const std::array<float, 4> &rgba) {
  using Channel        = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  pixel.r              = Channel(integerChannel(rgba[0]) * maximum + 0.5);
  pixel.g              = Channel(integerChannel(rgba[1]) * maximum + 0.5);
  pixel.b              = Channel(integerChannel(rgba[2]) * maximum + 0.5);
  pixel.m              = Channel(integerChannel(rgba[3]) * maximum + 0.5);
}

template <>
void writePixel<TPixelF>(TPixelF &pixel, const std::array<float, 4> &rgba) {
  pixel.r = rgba[0];
  pixel.g = rgba[1];
  pixel.b = rgba[2];
  pixel.m = rgba[3];
}

std::array<float, 4> falseColor(float value, double low, double high,
                                bool invert) {
  double t = high == low ? 0.0 : (double(value) - low) / (high - low);
  if (invert) t = 1.0 - t;
  t = std::clamp(t, 0.0, 1.0);
  // Compact blue/cyan/green/yellow/red diagnostic ramp.
  const double p = t * 4.0;
  return {float(std::clamp(p - 2.0, 0.0, 1.0)),
          float(std::clamp(std::min(p, 4.0 - p), 0.0, 1.0)),
          float(std::clamp(2.0 - p, 0.0, 1.0)), 1.0f};
}
}  // namespace

class OpenExrAovFx final : public TStandardZeraryFx, public TFxAovSource {
  FX_PLUGIN_DECLARATION(OpenExrAovFx)

  enum OutputMode {
    Layer = 0,
    SingleChannel,
    FalseColor,
    CustomRgba,
    Composite
  };
  enum Interpretation { SceneLinearColor = 0, RawData };
  enum CompositeOperation { Add = 0, Over };

  TStringParamP m_exrFile;
  TIntParamP m_frameOffset;
  TStringParamP m_part;
  TIntEnumParamP m_interpretation;
  TIntEnumParamP m_outputMode;
  TStringParamP m_layer;
  TStringParamP m_channel;
  TDoubleParamP m_falseColorLow, m_falseColorHigh;
  TBoolParamP m_falseColorInvert;
  TStringParamP m_redChannel, m_greenChannel, m_blueChannel, m_alphaChannel;

  struct CompositeSlot {
    TBoolParamP enabled;
    TStringParamP layer;
    TDoubleParamP gain;
    TIntEnumParamP operation;

    CompositeSlot()
        : enabled(false)
        , layer(L"")
        , gain(1.0)
        , operation(new TIntEnumParam(Add, "Add")) {
      operation->addItem(Over, "Over");
      gain->setValueRange(-100.0, 100.0);
    }
  };
  std::array<CompositeSlot, 4> m_slots;

  struct Cache {
    QMutex mutex;
    QString revision;
    std::shared_ptr<ExrDocument> document;
  };
  mutable std::shared_ptr<Cache> m_cache = std::make_shared<Cache>();

  QString resolvedPath(double frame) const {
    QString path = QString::fromStdWString(m_exrFile->getValue());
    const qlonglong number =
        qlonglong(std::llround(frame)) + m_frameOffset->getValue();
    for (int from = 0; from < path.size();) {
      const int first = path.indexOf('#', from);
      if (first < 0) break;
      int last = first;
      while (last < path.size() && path[last] == '#') ++last;
      const int width = last - first;
      QString replacement;
      if (number < 0)
        replacement =
            QStringLiteral("-") + QString::number(-number).rightJustified(
                                      std::max(1, width - 1), QLatin1Char('0'));
      else
        replacement =
            QString::number(number).rightJustified(width, QLatin1Char('0'));
      path.replace(first, width, replacement);
      from = first + replacement.size();
    }
    return QFileInfo(path).absoluteFilePath();
  }

  QString fileRevision(double frame) const {
    const QFileInfo file(resolvedPath(frame));
    return file.absoluteFilePath() + QLatin1Char(':') +
           QString::number(file.lastModified().toMSecsSinceEpoch()) +
           QLatin1Char(':') + QString::number(file.size()) + QLatin1Char(':') +
           QString::number(file.isFile() && file.isReadable());
  }

  std::shared_ptr<ExrDocument> document(double frame) const {
    if (m_exrFile->getValue().empty())
      throw std::runtime_error("Choose an OpenEXR file first");
    const QString path     = resolvedPath(frame);
    const QString revision = fileRevision(frame);
    QMutexLocker lock(&m_cache->mutex);
    if (revision != m_cache->revision || !m_cache->document) {
      auto loaded = sharedDocument(revision, path);
      if (fileRevision(frame) != revision)
        throw std::runtime_error("EXR changed while loading; retry the render");
      m_cache->document = std::move(loaded);
      m_cache->revision = revision;
    }
    return m_cache->document;
  }

  int selectedPart(const ExrDocument &document) const {
    const auto &parts = document.parts();
    const QString requested =
        QString::fromStdWString(m_part->getValue()).trimmed();
    if (requested.isEmpty()) return 0;
    if (requested.startsWith('#')) {
      bool ok           = false;
      const int ordinal = requested.mid(1).toInt(&ok);
      if (ok && ordinal >= 1 && ordinal <= int(parts.size()))
        return ordinal - 1;
      throw std::runtime_error(
          "EXR part index is invalid. Use #1, #2, ... or an exact part name.");
    }
    const std::string name = requested.toUtf8().toStdString();
    int found              = -1;
    for (int p = 0; p < int(parts.size()); ++p) {
      if (parts[p].name != name) continue;
      if (found >= 0)
        throw std::runtime_error(
            "EXR has duplicate part names. Select this part by #N instead.");
      found = p;
    }
    if (found < 0)
      throw std::runtime_error(
          "EXR part was not found. Leave Part blank for the first part, use "
          "its exact name, or #N.");
    return found;
  }

  static std::string utf8(const TStringParamP &param) {
    return QString::fromStdWString(param->getValue()).toUtf8().toStdString();
  }

  Plane loadNamedPlane(ExrDocument &doc, int part, const TStringParamP &param,
                       float emptyFill = 0.0f) const {
    Plane result;
    result.fill            = emptyFill;
    const std::string name = utf8(param);
    if (!name.empty()) result.values = doc.plane(part, name);
    return result;
  }

public:
  OpenExrAovFx()
      : m_exrFile(L"")
      , m_frameOffset(1)
      , m_part(L"")
      , m_interpretation(
            new TIntEnumParam(SceneLinearColor, "Scene-Linear Color"))
      , m_outputMode(new TIntEnumParam(Layer, "Layer RGBA"))
      , m_layer(L"")
      , m_channel(L"")
      , m_falseColorLow(0.0)
      , m_falseColorHigh(1.0)
      , m_falseColorInvert(false)
      , m_redChannel(L"R")
      , m_greenChannel(L"G")
      , m_blueChannel(L"B")
      , m_alphaChannel(L"A") {
    bindParam(this, "exrFile", m_exrFile);
    bindParam(this, "frameOffset", m_frameOffset);
    bindParam(this, "part", m_part);
    bindParam(this, "interpretation", m_interpretation);
    bindParam(this, "outputMode", m_outputMode);
    bindParam(this, "layer", m_layer);
    bindParam(this, "channel", m_channel);
    bindParam(this, "falseColorLow", m_falseColorLow);
    bindParam(this, "falseColorHigh", m_falseColorHigh);
    bindParam(this, "falseColorInvert", m_falseColorInvert);
    bindParam(this, "redChannel", m_redChannel);
    bindParam(this, "greenChannel", m_greenChannel);
    bindParam(this, "blueChannel", m_blueChannel);
    bindParam(this, "alphaChannel", m_alphaChannel);

    m_interpretation->addItem(RawData, "Raw Data");
    m_outputMode->addItem(SingleChannel, "Single Channel");
    m_outputMode->addItem(FalseColor, "False Color");
    m_outputMode->addItem(CustomRgba, "Custom RGBA");
    m_outputMode->addItem(Composite, "Composite Layers");
    m_frameOffset->setValueRange(-1000000, 1000000);
    m_falseColorLow->setValueRange(-1000000.0, 1000000.0);
    m_falseColorHigh->setValueRange(-1000000.0, 1000000.0);

    for (int i = 0; i < int(m_slots.size()); ++i) {
      const std::string suffix = std::to_string(i + 1);
      bindParam(this, "slot" + suffix + "Enabled", m_slots[i].enabled);
      bindParam(this, "slot" + suffix + "Layer", m_slots[i].layer);
      bindParam(this, "slot" + suffix + "Gain", m_slots[i].gain);
      bindParam(this, "slot" + suffix + "Operation", m_slots[i].operation);
    }
    m_slots[0].enabled->setValue(true);
    enableComputeInFloat(true);
  }

  bool isZerary() const override { return true; }

  TFx *clone(bool recursive = true) const override {
    auto *copy =
        static_cast<OpenExrAovFx *>(TStandardZeraryFx::clone(recursive));
    copy->m_cache = m_cache;
    return copy;
  }

  std::vector<TFxAovChoice> getAovChoices(TFxAovChoiceKind kind,
                                          double frame) const override {
    const auto doc = document(frame);
    std::vector<TFxAovChoice> choices;
    if (kind == TFxAovChoiceKind::Part) {
      std::map<std::string, int> nameCounts;
      for (const ExrPartInfo &part : doc->parts()) ++nameCounts[part.name];
      for (int p = 0; p < int(doc->parts().size()); ++p) {
        const ExrPartInfo &part = doc->parts()[p];
        const std::string value =
            !part.name.empty() && nameCounts[part.name] == 1
                ? part.name
                : '#' + std::to_string(p + 1);
        const QString label =
            QStringLiteral("%1: %2 (%3)")
                .arg(p + 1)
                .arg(part.name.empty() ? QStringLiteral("unnamed")
                                       : QString::fromUtf8(part.name.c_str()))
                .arg(partTypeName(part.type));
        choices.push_back({QString::fromUtf8(value.c_str()).toStdWString(),
                           label.toStdWString()});
      }
      return choices;
    }

    const ExrPartInfo &part = doc->parts().at(selectedPart(*doc));
    if (kind == TFxAovChoiceKind::Layer) {
      if (part.hasBaseLayer)
        choices.push_back({L"", QObject::tr("(base layer)").toStdWString()});
      for (const std::string &layer : part.layers) {
        const QString value = QString::fromUtf8(layer.c_str());
        choices.push_back({value.toStdWString(), value.toStdWString()});
      }
    } else {
      for (const std::string &channel : part.channels) {
        const ExrChannelInfo &info = part.channelInfo.at(channel);
        const QString value        = QString::fromUtf8(channel.c_str());
        const QString label =
            QStringLiteral("%1 (%2)").arg(value, pixelTypeName(info.type));
        choices.push_back({value.toStdWString(), label.toStdWString()});
      }
    }
    return choices;
  }

  std::wstring getAovSummary(double frame) const override {
    const auto doc = document(frame);
    QStringList lines;
    lines << QObject::tr("File: %1").arg(resolvedPath(frame));
    lines << QObject::tr("Parts: %1").arg(doc->parts().size());
    for (int p = 0; p < int(doc->parts().size()); ++p) {
      const ExrPartInfo &part = doc->parts()[p];
      const int displayWidth =
          part.displayWindow.max.x - part.displayWindow.min.x + 1;
      const int displayHeight =
          part.displayWindow.max.y - part.displayWindow.min.y + 1;
      const int dataWidth  = part.dataWindow.max.x - part.dataWindow.min.x + 1;
      const int dataHeight = part.dataWindow.max.y - part.dataWindow.min.y + 1;
      lines << QObject::tr(
                   "%1. %2 — %3, display %4×%5, data %6×%7, %8 channels, "
                   "%9 layers")
                   .arg(p + 1)
                   .arg(part.name.empty()
                            ? QObject::tr("unnamed")
                            : QString::fromUtf8(part.name.c_str()))
                   .arg(partTypeName(part.type))
                   .arg(displayWidth)
                   .arg(displayHeight)
                   .arg(dataWidth)
                   .arg(dataHeight)
                   .arg(part.channels.size())
                   .arg(part.layers.size() + (part.hasBaseLayer ? 1 : 0));
    }
    lines << QObject::tr(
        "Deep parts are listed but require a future flattening policy.");
    return lines.join(QLatin1Char('\n')).toStdWString();
  }

  std::string getAlias(double frame,
                       const TRenderSettings &info) const override {
    return TRasterFx::getAlias(frame, info) +
           "[EXR-AOV-v1:" + fileRevision(frame).toUtf8().toStdString() + ']';
  }

  bool doGetBBox(double frame, TRectD &bbox, const TRenderSettings &) override {
    if (m_exrFile->getValue().empty()) {
      bbox = TRectD();
      return false;
    }
    try {
      const auto doc          = document(frame);
      const ExrPartInfo &part = doc->parts().at(selectedPart(*doc));
      const double width =
          double(part.displayWindow.max.x) - part.displayWindow.min.x + 1.0;
      const double height =
          double(part.displayWindow.max.y) - part.displayWindow.min.y + 1.0;
      bbox = TRectD(-width * 0.5, -height * 0.5, width * 0.5, height * 0.5);
      return true;
    } catch (const std::exception &) {
      // Compute will report the actionable file/selection error.
      bbox = TRectD(-500.0, -500.0, 500.0, 500.0);
      return true;
    }
  }

  bool canHandle(const TRenderSettings &, double) override { return false; }
  TAffine handledAffine(const TRenderSettings &, double) override {
    return TAffine();
  }

  bool toBeComputedInLinearColorSpace(bool, bool tileIsLinear) const override {
    return m_interpretation->getValue() == SceneLinearColor ? true
                                                            : tileIsLinear;
  }

  int getMemoryRequirement(const TRectD &, double frame,
                           const TRenderSettings &) override {
    try {
      const auto doc          = document(frame);
      const ExrPartInfo &part = doc->parts().at(selectedPart(*doc));
      int planes              = 4;
      if (m_outputMode->getValue() == SingleChannel ||
          m_outputMode->getValue() == FalseColor)
        planes = 1;
      else if (m_outputMode->getValue() == Composite)
        planes = 4 * int(m_slots.size());
      const double bytes =
          double(checkedPixelCount(part.dataWindow)) * planes * sizeof(float);
      return int(std::min(double(std::numeric_limits<int>::max()),
                          std::ceil(bytes / (1024.0 * 1024.0))));
    } catch (const std::exception &) {
      return 0;
    }
  }

  void doCompute(TTile &tile, double frame,
                 const TRenderSettings &info) override {
    tile.getRaster()->clear();
    if (m_exrFile->getValue().empty()) return;
    try {
      if (info.m_isCanceled && *info.m_isCanceled) return;
      const auto doc          = document(frame);
      const int partIndex     = selectedPart(*doc);
      const ExrPartInfo &part = doc->parts().at(partIndex);
      const int mode          = m_outputMode->getValue();

      LayerPlanes layer;
      Plane single;
      std::array<Plane, 4> custom;
      struct LoadedSlot {
        LayerPlanes layer;
        double gain;
        int operation;
      };
      std::vector<LoadedSlot> loadedSlots;

      if (mode == Layer) {
        layer = loadLayer(*doc, partIndex, utf8(m_layer));
      } else if (mode == SingleChannel || mode == FalseColor) {
        const std::string name = utf8(m_channel);
        if (name.empty()) throw std::runtime_error("Choose an EXR channel");
        single.values = doc->plane(partIndex, name);
      } else if (mode == CustomRgba) {
        custom[0] = loadNamedPlane(*doc, partIndex, m_redChannel);
        custom[1] = loadNamedPlane(*doc, partIndex, m_greenChannel);
        custom[2] = loadNamedPlane(*doc, partIndex, m_blueChannel);
        custom[3] = loadNamedPlane(*doc, partIndex, m_alphaChannel, 1.0f);
      } else if (mode == Composite) {
        for (const CompositeSlot &slot : m_slots) {
          if (!slot.enabled->getValue()) continue;
          loadedSlots.push_back({loadLayer(*doc, partIndex, utf8(slot.layer)),
                                 slot.gain->getValue(frame),
                                 slot.operation->getValue()});
        }
      } else {
        throw std::runtime_error("Unknown EXR AOV output mode");
      }
      if (info.m_isCanceled && *info.m_isCanceled) return;

      const int displayWidth =
          part.displayWindow.max.x - part.displayWindow.min.x + 1;
      const int displayHeight =
          part.displayWindow.max.y - part.displayWindow.min.y + 1;
      const int dataWidth = part.dataWindow.max.x - part.dataWindow.min.x + 1;
      const double bboxX0 = -displayWidth * 0.5;
      const double bboxY0 = -displayHeight * 0.5;
      const double low    = m_falseColorLow->getValue(frame);
      const double high   = m_falseColorHigh->getValue(frame);
      const bool invert   = m_falseColorInvert->getValue();

      auto render = [&](auto raster) {
        using Pixel =
            typename std::remove_reference_t<decltype(*raster)>::Pixel;
        raster->lock();
        for (int y = 0; y < raster->getLy(); ++y) {
          Pixel *row         = raster->pixels(y);
          const int displayY = int(std::floor(tile.m_pos.y + y - bboxY0));
          const int exrY     = part.displayWindow.max.y - displayY;
          for (int x = 0; x < raster->getLx(); ++x) {
            const int displayX = int(std::floor(tile.m_pos.x + x - bboxX0));
            const int exrX     = part.displayWindow.min.x + displayX;
            if (displayX < 0 || displayX >= displayWidth || displayY < 0 ||
                displayY >= displayHeight || exrX < part.dataWindow.min.x ||
                exrX > part.dataWindow.max.x || exrY < part.dataWindow.min.y ||
                exrY > part.dataWindow.max.y) {
              row[x] = Pixel(0, 0, 0, 0);
              continue;
            }
            const std::size_t index =
                std::size_t(exrY - part.dataWindow.min.y) * dataWidth +
                std::size_t(exrX - part.dataWindow.min.x);
            std::array<float, 4> rgba = {0.0f, 0.0f, 0.0f, 0.0f};
            if (mode == Layer) {
              for (int c = 0; c < 4; ++c) rgba[c] = layer.rgba[c].at(index);
            } else if (mode == SingleChannel) {
              rgba[0] = rgba[1] = rgba[2] = single.at(index);
              rgba[3]                     = 1.0f;
            } else if (mode == FalseColor) {
              rgba = falseColor(single.at(index), low, high, invert);
            } else if (mode == CustomRgba) {
              for (int c = 0; c < 4; ++c) rgba[c] = custom[c].at(index);
            } else {
              for (const LoadedSlot &slot : loadedSlots) {
                std::array<float, 4> source;
                for (int c = 0; c < 4; ++c)
                  source[c] = slot.layer.rgba[c].at(index);
                source[0] *= float(slot.gain);
                source[1] *= float(slot.gain);
                source[2] *= float(slot.gain);
                if (slot.operation == Over) {
                  const float behind = 1.0f - source[3];
                  rgba[0]            = source[0] + rgba[0] * behind;
                  rgba[1]            = source[1] + rgba[1] * behind;
                  rgba[2]            = source[2] + rgba[2] * behind;
                  rgba[3]            = source[3] + rgba[3] * behind;
                } else {
                  rgba[0] += source[0];
                  rgba[1] += source[1];
                  rgba[2] += source[2];
                  rgba[3] = std::max(rgba[3], source[3]);
                }
              }
            }
            writePixel(row[x], rgba);
          }
        }
        raster->unlock();
      };

      if (TRasterFP raster = tile.getRaster())
        render(raster);
      else if (TRaster64P raster = tile.getRaster())
        render(raster);
      else if (TRaster32P raster = tile.getRaster())
        render(raster);
      else
        throw std::runtime_error("Unsupported EXR AOV output pixel type");
    } catch (const std::exception &error) {
      throw TException(QString("EXR AOV [%1]: %2\n%3")
                           .arg(QString::fromStdWString(getFxId()),
                                QString::fromUtf8(error.what()),
                                resolvedPath(frame))
                           .toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(OpenExrAovFx, "openExrAovFx")
