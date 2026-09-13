#include "stdfx.h"
#include "tfxparam.h"
#include "tnotanimatableparam.h"
#include "glbrenderer.h"
#include "tfxmaterial.h"
#include "tparamset.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QFile>
#include <QDebug>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>

#include <iomanip>
#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace {
template <class PIXEL>
void copyGlbPixels(TRasterPT<PIXEL> raster,
                   const std::vector<otglb::ColorPixel> &pixels, int yOffset) {
  using Channel = typename PIXEL::Channel;
  const double maximum = PIXEL::maxChannelValue;
  const double round = std::is_floating_point<Channel>::value ? 0.0 : 0.5;
  raster->lock();
  const int rows = int(pixels.size() / raster->getLx());
  for (int y = 0; y < rows; ++y) {
    auto *row = raster->pixels(y + yOffset);
    for (int x = 0; x < raster->getLx(); ++x) {
      const auto &p = pixels[std::size_t(y) * raster->getLx() + x];
      row[x] = PIXEL(Channel(p.r * maximum + round), Channel(p.g * maximum + round),
                     Channel(p.b * maximum + round), Channel(p.alpha * maximum + round));
    }
  }
  raster->unlock();
}
}  // namespace

// Read-only GLB source with scene-owned, animated material color overrides.
class GlbModelFx final : public TStandardZeraryFx, public TFxMaterialSource {
  FX_PLUGIN_DECLARATION(GlbModelFx)

  TStringParamP m_modelFile;
  TDoubleParamP m_positionX, m_positionY, m_positionZ;
  TDoubleParamP m_rotationX, m_rotationY, m_rotationZ;
  TDoubleParamP m_scale;
  TIntEnumParamP m_projection;
  TDoubleParamP m_cameraDistance, m_fieldOfView, m_orthoSize;
  TDoubleParamP m_nearClip, m_farClip;
  TIntEnumParamP m_lighting, m_renderStyle;
  TIntEnumParamP m_colorMode;
  TParamSetP m_materialColors;

  struct Cache {
    QMutex mutex;
    QString revision;
    std::string digest;
    otglb::Result loaded;
    otglb::RenderOptions options;
    std::shared_ptr<const otglb::RenderScene> projected;
    bool warned = false;
  };
  // Render clones share owned data, but hold the lock only while preparing it.
  std::shared_ptr<Cache> m_cache = std::make_shared<Cache>();

  QString fileRevision() const {
    const QFileInfo file(QString::fromStdWString(m_modelFile->getValue()));
    return file.absoluteFilePath() + ":" +
           QString::number(file.lastModified().toMSecsSinceEpoch()) + ":" +
           QString::number(file.size()) + ":" +
           QString::number(file.isFile() && file.isReadable());
  }

  otglb::RenderOptions options(double frame) const {
    otglb::RenderOptions o;
    o.position = {{m_positionX->getValue(frame), m_positionY->getValue(frame), m_positionZ->getValue(frame)}};
    o.rotation = {{m_rotationX->getValue(frame), m_rotationY->getValue(frame), m_rotationZ->getValue(frame)}};
    o.scale = m_scale->getValue(frame) / 100.0;
    o.cameraDistance = m_cameraDistance->getValue(frame);
    o.fieldOfView = m_fieldOfView->getValue(frame);
    o.orthoHeight = m_orthoSize->getValue(frame);
    o.nearClip = m_nearClip->getValue(frame);
    o.farClip = m_farClip->getValue(frame);
    o.perspective = m_projection->getValue() == 1;
    o.headlight = m_lighting->getValue() == 1 && m_renderStyle->getValue() == 0;
    o.wireframe = m_renderStyle->getValue() == 1;
    return o;
  }

  void loadLocked() const {
    const QString path = QString::fromStdWString(m_modelFile->getValue());
    const QString revision = fileRevision();
    if (revision != m_cache->revision) {
#ifdef _WIN32
      const std::filesystem::path native(path.toStdWString());
#else
      const auto native = std::filesystem::u8path(path.toUtf8().toStdString());
#endif
      auto loaded = otglb::load(native);
      std::string digest;
      if (loaded) {
        QFile file(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file))
          throw std::runtime_error("Cannot identify GLB materials; retry loading the file.");
        digest = hash.result().toHex().toStdString();
      }
      if (fileRevision() != revision)
        throw std::runtime_error("GLB changed while loading; retry the render.");
      m_cache->loaded = std::move(loaded);
      m_cache->digest = std::move(digest);
      m_cache->revision = revision;
      m_cache->projected.reset();
      m_cache->warned = false;
      for (const auto &warning : m_cache->loaded.warnings)
        qWarning().noquote() << "GLB Model:" << path << QString::fromStdString(warning);
    }
    if (!m_cache->loaded) throw std::runtime_error(m_cache->loaded.error);
  }

  std::string materialKey(std::size_t index) const {
    return "m" + m_cache->digest + "_" + std::to_string(index);
  }

  std::shared_ptr<const otglb::RenderScene> projected(double frame,
                                                    const int *canceled) const {
    if (m_modelFile->getValue().empty()) return {};
    auto settings = options(frame);
    QMutexLocker lock(&m_cache->mutex);
    loadLocked();
    settings.materialColors = m_colorMode->getValue() == 1;
    if (settings.materialColors) {
      const auto &asset = *m_cache->loaded.asset;
      for (std::size_t i = 0; i <= asset.materials.size(); ++i) {
        auto color = otglb::materialColor(asset, i);
        const int index = m_materialColors->getParamIdx(materialKey(i));
        if (index < m_materialColors->getParamCount()) {
          TPixelParamP param = m_materialColors->getParam(index);
          if (param) {
            const auto c = param->getValueD(frame);
            color = {{float(std::clamp(c.r, 0.0, 1.0)), float(std::clamp(c.g, 0.0, 1.0)),
                      float(std::clamp(c.b, 0.0, 1.0))}};
          }
        }
        settings.colors.push_back(color);
      }
    }
    if (!m_cache->projected || !(settings == m_cache->options)) {
      auto scene = std::make_shared<otglb::RenderScene>(
          otglb::prepareRender(*m_cache->loaded.asset, settings, canceled));
      if (canceled && *canceled) return {};
      if (!m_cache->warned) {
        for (const auto &warning : scene->warnings)
          qWarning().noquote() << "GLB Model:" << QString::fromStdWString(m_modelFile->getValue()) << QString::fromStdString(warning);
        m_cache->warned = true;
      }
      m_cache->projected = std::move(scene);
      m_cache->options = settings;
    }
    return m_cache->projected;
  }

public:
  std::vector<TFxMaterial> getMaterials() const override {
    if (m_modelFile->getValue().empty()) return {};
    QMutexLocker lock(&m_cache->mutex);
    loadLocked();
    const auto &asset = *m_cache->loaded.asset;
    bool defaultUsed = false;
    for (const auto &mesh : asset.meshes)
      for (const auto &primitive : mesh.primitives)
        defaultUsed |= primitive.material == otglb::NoIndex;
    std::vector<TFxMaterial> materials;
    for (std::size_t i = 0; i < asset.materials.size() + (defaultUsed ? 1 : 0); ++i) {
      const auto c = otglb::materialColor(asset, i);
      std::string name = i == asset.materials.size() ? "Default material" : asset.materials[i].name;
      if (name.empty()) name = "Material " + std::to_string(i + 1);
      materials.push_back({materialKey(i), name,
          TPixel32(int(c[0] * 255 + .5f), int(c[1] * 255 + .5f), int(c[2] * 255 + .5f), 255)});
    }
    return materials;
  }

  GlbModelFx()
      : m_modelFile(L"")
      , m_positionX(0.0)
      , m_positionY(0.0)
      , m_positionZ(0.0)
      , m_rotationX(0.0)
      , m_rotationY(0.0)
      , m_rotationZ(0.0)
      , m_scale(100.0)
      , m_projection(new TIntEnumParam(0, "Orthographic"))
      , m_cameraDistance(10.0)
      , m_fieldOfView(45.0)
      , m_orthoSize(10.0)
      , m_nearClip(0.1)
      , m_farClip(1000.0)
      , m_lighting(new TIntEnumParam(0, "Unlit"))
      , m_renderStyle(new TIntEnumParam(0, "Solid"))
      , m_colorMode(new TIntEnumParam(0, "Grayscale"))
      , m_materialColors(new TFxMaterialParamSet) {
    bindParam(this, "modelFile", m_modelFile);
    bindParam(this, "positionX", m_positionX);
    bindParam(this, "positionY", m_positionY);
    bindParam(this, "positionZ", m_positionZ);
    bindParam(this, "rotationX", m_rotationX);
    bindParam(this, "rotationY", m_rotationY);
    bindParam(this, "rotationZ", m_rotationZ);
    bindParam(this, "scale", m_scale);
    bindParam(this, "projection", m_projection);
    bindParam(this, "cameraDistance", m_cameraDistance);
    bindParam(this, "fieldOfView", m_fieldOfView);
    bindParam(this, "orthoSize", m_orthoSize);
    bindParam(this, "nearClip", m_nearClip);
    bindParam(this, "farClip", m_farClip);
    bindParam(this, "lighting", m_lighting);
    bindParam(this, "renderStyle", m_renderStyle);
    bindParam(this, "colorMode", m_colorMode);
    bindParam(this, "materialColors", m_materialColors);
    m_colorMode->addItem(1, "Material Colors");

    m_projection->addItem(1, "Perspective");
    m_lighting->addItem(1, "Headlight");
    m_renderStyle->addItem(1, "Wireframe");
    m_rotationX->setMeasureName("angle");
    m_rotationY->setMeasureName("angle");
    m_rotationZ->setMeasureName("angle");
    m_fieldOfView->setMeasureName("angle");
    m_scale->setValueRange(0.001, 100000.0);
    m_cameraDistance->setValueRange(0.001, 1000000.0);
    m_fieldOfView->setValueRange(1.0, 179.0);
    m_orthoSize->setValueRange(0.001, 1000000.0);
    m_nearClip->setValueRange(0.001, 1000000.0);
    m_farClip->setValueRange(0.001, 1000000.0);
    enableComputeInFloat(true);
  }

  bool isZerary() const override { return true; }

  TFx *clone(bool recursive = true) const override {
    auto *copy = static_cast<GlbModelFx *>(TStandardZeraryFx::clone(recursive));
    copy->m_cache = m_cache;
    return copy;
  }

  std::string getAlias(double frame, const TRenderSettings &info) const override {
    std::ostringstream key;
    key.imbue(std::locale::classic());
    key << std::setprecision(17);
    const auto o = options(frame);
    for (double v : o.position) key << v << ',';
    for (double v : o.rotation) key << v << ',';
    key << o.scale << ',' << o.cameraDistance << ',' << o.fieldOfView << ','
        << o.orthoHeight << ',' << o.nearClip << ',' << o.farClip;
    // Child names are part of the identity: equal colors assigned to different
    // materials must not reuse one another's cached images.
    for (int i = 0; i < m_materialColors->getParamCount(); ++i)
      key << ':' << m_materialColors->getParamName(i) << '='
          << m_materialColors->getParam(i)->getValueAlias(frame, 17);
    return TRasterFx::getAlias(frame, info) + "[GLB-color-v1:" +
           fileRevision().toUtf8().toStdString() + ":" + key.str() + "]";
  }

  bool doGetBBox(double frame, TRectD &bbox, const TRenderSettings &info) override {
    bbox = TRectD();
    try {
      const auto scene = projected(frame, info.m_isCanceled);
      if (!scene || scene->triangles.empty()) return false;
      const auto &b = scene->bounds;
      bbox = TRectD(b[0], b[1], b[2], b[3]);
      return true;
    } catch (const std::exception &) {
      // Let compute report an actionable error instead of silently suppressing
      // this source because of an empty bounding box.
      bbox = TRectD(-500, -500, 500, 500);
      return true;
    }
  }

  bool canHandle(const TRenderSettings &, double) override { return true; }

  int getMemoryRequirement(const TRectD &rect, double,
                           const TRenderSettings &) override {
    if (rect.isEmpty()) return 0;
    // Four depth/coverage samples plus the RGBA output. Tell the normal
    // FX scheduler to subdivide large requests before allocating these buffers.
    const double megabytes = std::ceil(rect.getLx()) * std::ceil(rect.getLy()) *
                             112.0 / (1024.0 * 1024.0);
    return int(std::min(double(std::numeric_limits<int>::max()), std::ceil(megabytes)));
  }

  bool toBeComputedInLinearColorSpace(bool, bool) const override { return false; }

  void doCompute(TTile &tile, double frame, const TRenderSettings &info) override {
    tile.getRaster()->clear();
    try {
      const auto scene = projected(frame, info.m_isCanceled);
      if (!scene || scene->triangles.empty()) return;
      otglb::RenderTile request;
      request.width = tile.getRaster()->getLx(); request.height = tile.getRaster()->getLy();
      request.x = tile.m_pos.x; request.y = tile.m_pos.y;
      const auto &a = info.m_affine;
      request.affine = {{a.a11, a.a12, a.a13, a.a21, a.a22, a.a23}};
      if (!request.width || !request.height) return;
      // Swatches and direct computations can bypass scheduler subdivision.
      // Bound temporary storage there too, including full-resolution renders.
      const int band = std::max(1, std::min(128, 2 * 1024 * 1024 / request.width));
      const int height = request.height;
      for (int y = 0; y < height; y += band) {
        request.height = std::min(band, height - y);
        request.y = tile.m_pos.y + y;
        const auto pixels = otglb::renderTile(*scene, request, info.m_isCanceled);
        if (pixels.empty()) { tile.getRaster()->clear(); return; }
        if (TRasterFP raster = tile.getRaster()) copyGlbPixels<TPixelF>(raster, pixels, y);
        else if (TRaster64P raster = tile.getRaster()) copyGlbPixels<TPixel64>(raster, pixels, y);
        else if (TRaster32P raster = tile.getRaster()) copyGlbPixels<TPixel32>(raster, pixels, y);
        else throw std::runtime_error("Unsupported GLB output pixel type.");
      }
    } catch (const std::exception &error) {
      throw TException(QString("GLB Model [%1]: %2\n%3")
          .arg(QString::fromStdWString(getFxId()), QString::fromUtf8(error.what()),
               QString::fromStdWString(m_modelFile->getValue())).toStdWString());
    }
  }
};

FX_PLUGIN_IDENTIFIER(GlbModelFx, "glbModelFx")
