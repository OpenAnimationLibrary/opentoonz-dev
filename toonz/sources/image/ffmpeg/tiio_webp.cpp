#include "tiio_webp.h"

#include "thirdparty.h"
#include "timageinfo.h"
#include "tmsgcore.h"
#include "tsystem.h"
#include "toonz/stage.h"
#include "toonz/toonzfolders.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QSharedPointer>
#include <QStringList>
#include <QWaitCondition>

#include <cstring>

namespace {

int ffmpegTimeoutMs() {
  const int timeoutSeconds = ThirdParty::getFFmpegTimeout();
  return timeoutSeconds > 0 ? timeoutSeconds * 1000 : 30000;
}

bool waitForProcess(QProcess &process) {
  if (!process.waitForFinished(ffmpegTimeoutMs())) {
    process.kill();
    process.waitForFinished(1000);
    return false;
  }

  return process.exitStatus() == QProcess::NormalExit &&
         process.exitCode() == 0;
}

double parseFrameRate(const QString &value) {
  const QStringList values = value.trimmed().split('/');
  if (values.size() != 2) return 0.0;

  bool numeratorOk         = false;
  bool denominatorOk       = false;
  const double numerator   = values.at(0).toDouble(&numeratorOk);
  const double denominator = values.at(1).toDouble(&denominatorOk);
  if (!numeratorOk || !denominatorOk || denominator == 0.0) return 0.0;

  return numerator / denominator;
}

enum class WebPCacheStatus { NotStarted, Extracting, Ready, Failed };

struct WebPCacheState {
  QMutex mutex;
  QWaitCondition finished;
  WebPCacheStatus status = WebPCacheStatus::NotStarted;
  int frameCount         = 0;
};

QMutex s_webpCacheRegistryMutex;
QHash<QString, QSharedPointer<WebPCacheState>> s_webpCacheRegistry;

QSharedPointer<WebPCacheState> webpCacheState(const QString &cacheKey) {
  QMutexLocker locker(&s_webpCacheRegistryMutex);
  auto state = s_webpCacheRegistry.value(cacheKey);
  if (!state) {
    state = QSharedPointer<WebPCacheState>::create();
    s_webpCacheRegistry.insert(cacheKey, state);
  }
  return state;
}

class TImageReaderWebP final : public TImageReader {
public:
  TImageReaderWebP(const TFilePath &path, int frameIndex,
                   TLevelReaderWebP *levelReader, TImageInfo *info)
      : TImageReader(path)
      , m_frameIndex(frameIndex)
      , m_levelReader(levelReader)
      , m_info(info) {
    if (m_levelReader) m_levelReader->addRef();
  }

  ~TImageReaderWebP() override {
    if (m_levelReader) m_levelReader->release();
  }

  TImageP load() override {
    return m_levelReader ? m_levelReader->load(m_frameIndex) : TImageP();
  }

  TDimension getSize() const {
    return m_levelReader ? m_levelReader->getSize() : TDimension();
  }

  TRect getBBox() const { return TRect(); }

  const TImageInfo *getImageInfo() const override { return m_info; }

private:
  int m_frameIndex;
  TLevelReaderWebP *m_levelReader;
  TImageInfo *m_info;

  TImageReaderWebP(const TImageReaderWebP &)            = delete;
  TImageReaderWebP &operator=(const TImageReaderWebP &) = delete;
};

}  // namespace

//=============================================================================
// TLevelReaderWebP
//-----------------------------------------------------------------------------

TLevelReaderWebP::TLevelReaderWebP(const TFilePath &path)
    : TLevelReader(path) {
  QString cacheRoot = ToonzFolder::getCacheRootFolder().getQString();
  TFilePath cachePath(cacheRoot + "/ffmpeg");
  if (!TSystem::doesExistFileOrLevel(cachePath)) {
    try {
      TSystem::mkDir(cachePath);
    } catch (const TSystemException &) {
      throw TImageException(path, "Cannot create FFmpeg cache directory.");
    }
  }
  m_cacheDir = cachePath.getQString();

  QFileInfo fileInfo(path.getQString());
  QString fullPath = fileInfo.absoluteFilePath();
  if (fullPath.isEmpty()) fullPath = path.getQString();

  // Include source identity, size and modification time so replacing a WebP at
  // the same path cannot reuse frames extracted from an older file.
  QByteArray cacheIdentity = fullPath.toUtf8();
  cacheIdentity.append('|');
  cacheIdentity.append(QByteArray::number(fileInfo.size()));
  cacheIdentity.append('|');
  cacheIdentity.append(
      QByteArray::number(fileInfo.lastModified().toMSecsSinceEpoch()));
  const QByteArray hash = QCryptographicHash::hash(
      cacheIdentity, QCryptographicHash::Md5);
  m_tempBaseName = QString::fromLatin1(hash.toHex().left(16)) + "_webp";

  QStringList probeArgs;
  probeArgs << "-v" << "error" << "-select_streams" << "v:0"
            << "-show_entries"
            << "stream=width,height,avg_frame_rate,r_frame_rate" << "-of"
            << "default=noprint_wrappers=1" << path.getQString();

  QProcess ffprobe;
  ThirdParty::runFFprobe(ffprobe, probeArgs);
  if (!waitForProcess(ffprobe)) {
    throw TImageException(path, "Unable to probe WebP image information.");
  }

  const QString output =
      QString::fromUtf8(ffprobe.readAllStandardOutput());
  ffprobe.close();

  int width          = 0;
  int height         = 0;
  double averageRate = 0.0;
  double nominalRate = 0.0;

  const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    if (line.startsWith("width="))
      width = line.section('=', 1, 1).toInt();
    else if (line.startsWith("height="))
      height = line.section('=', 1, 1).toInt();
    else if (line.startsWith("avg_frame_rate="))
      averageRate = parseFrameRate(line.section('=', 1, 1));
    else if (line.startsWith("r_frame_rate="))
      nominalRate = parseFrameRate(line.section('=', 1, 1));
  }

  if (width <= 0 || height <= 0)
    throw TImageException(path, "Invalid WebP image dimensions.");

  // Count decoded frames rather than estimating duration x frame rate. Animated
  // WebP supports variable frame delays, so the estimate can be many times
  // larger than the number of source animation frames.
  m_frameCount = probeFrameCount(true);
  if (m_frameCount <= 0) m_frameCount = probeFrameCount(false);
  if (m_frameCount <= 0) m_frameCount = 1;

  m_size      = TDimension(width, height);
  m_imageInfo = new TImageInfo();
  m_imageInfo->m_frameRate = averageRate > 0.0 ? averageRate : nominalRate;
  m_imageInfo->m_lx             = width;
  m_imageInfo->m_ly             = height;
  m_imageInfo->m_bitsPerSample  = 8;
  m_imageInfo->m_samplePerPixel = 4;
  m_imageInfo->m_dpix           = Stage::standardDpi;
  m_imageInfo->m_dpiy           = Stage::standardDpi;
}

//-----------------------------------------------------------------------------

TLevelReaderWebP::~TLevelReaderWebP() {
  // Extracted frames are shared by Level Strip, Xsheet and viewer readers.
  // Removing them here races with other active readers and causes repeated
  // extraction attempts and misleading failure dialogs while scrubbing.
  delete m_imageInfo;
}

//-----------------------------------------------------------------------------

QString TLevelReaderWebP::getFramePattern() const {
  return m_cacheDir + QDir::separator() + m_tempBaseName + "_%06d.png";
}

//-----------------------------------------------------------------------------

QString TLevelReaderWebP::getFramePath(int frameIndex) const {
  return m_cacheDir + QDir::separator() + m_tempBaseName + "_" +
         QString("%1").arg(frameIndex, 6, 10, QChar('0')) + ".png";
}

//-----------------------------------------------------------------------------

void TLevelReaderWebP::removeExtractedFrames() {
  if (m_cacheDir.isEmpty() || m_tempBaseName.isEmpty()) return;

  QDir cacheDirectory(m_cacheDir);
  const QStringList files = cacheDirectory.entryList(
      QStringList() << m_tempBaseName + "_*.png", QDir::Files);
  for (const QString &file : files) QFile::remove(cacheDirectory.filePath(file));

  m_framesExtracted = false;
}

//-----------------------------------------------------------------------------

int TLevelReaderWebP::probeFrameCount(bool ignoreLoop) const {
  QStringList args;
  args << "-v" << "error";
  if (ignoreLoop) args << "-ignore_loop" << "1";
  args << "-count_frames" << "-select_streams" << "v:0"
       << "-show_entries" << "stream=nb_read_frames" << "-of"
       << "default=noprint_wrappers=1:nokey=1" << m_path.getQString();

  QProcess ffprobe;
  ThirdParty::runFFprobe(ffprobe, args);
  if (!waitForProcess(ffprobe)) return 0;

  bool ok = false;
  const int frameCount =
      QString::fromUtf8(ffprobe.readAllStandardOutput()).trimmed().toInt(&ok);
  ffprobe.close();

  return ok && frameCount > 0 ? frameCount : 0;
}

//-----------------------------------------------------------------------------

bool TLevelReaderWebP::extractFrames(bool ignoreLoop, int frameLimit) {
  QStringList args;
  args << "-hide_banner" << "-loglevel" << "error";
  if (ignoreLoop) args << "-ignore_loop" << "1";
  args << "-threads" << "auto" << "-i" << m_path.getQString() << "-map"
       << "0:v:0" << "-fps_mode" << "passthrough";
  if (frameLimit > 0)
    args << "-frames:v" << QString::number(frameLimit);
  args << "-pix_fmt" << "rgba" << "-start_number" << "1" << "-y" << "-f"
       << "image2" << getFramePattern();

  QProcess ffmpeg;
  ThirdParty::runFFmpeg(ffmpeg, args);
  const bool success = waitForProcess(ffmpeg);
  ffmpeg.close();
  return success;
}

//-----------------------------------------------------------------------------

int TLevelReaderWebP::countExtractedFrames() const {
  int frameIndex = 1;
  while (QFile::exists(getFramePath(frameIndex))) ++frameIndex;
  return frameIndex - 1;
}

//-----------------------------------------------------------------------------

bool TLevelReaderWebP::ensureFramesExtracted() {
  if (m_framesExtracted && countExtractedFrames() == m_frameCount) return true;

  const auto sharedState = webpCacheState(m_tempBaseName);
  {
    QMutexLocker locker(&sharedState->mutex);
    while (sharedState->status == WebPCacheStatus::Extracting)
      sharedState->finished.wait(&sharedState->mutex);

    if (sharedState->status == WebPCacheStatus::Ready &&
        sharedState->frameCount == m_frameCount &&
        countExtractedFrames() == m_frameCount) {
      m_framesExtracted = true;
      return true;
    }

    if (sharedState->status == WebPCacheStatus::Failed) return false;

    sharedState->status = WebPCacheStatus::Extracting;
  }

  // Only the reader that changed the shared state to Extracting reaches here.
  // Other Level Strip, Xsheet and viewer readers wait and then reuse its files.
  bool extracted = false;
  int extractedFrameCount = countExtractedFrames();
  if (extractedFrameCount == m_frameCount) {
    extracted = true;
  } else {
    removeExtractedFrames();

    // Explicitly ignore the WebP loop count and cap output at the exact decoded
    // source-frame count. Passthrough timing prevents FFmpeg from duplicating
    // held frames to make a constant-rate image sequence.
    extracted = extractFrames(true, m_frameCount);
    if (!extracted) {
      // Older FFmpeg builds can decode still WebP through image2 but do not have
      // the animated WebP demuxer's ignore_loop option.
      removeExtractedFrames();
      extracted = extractFrames(false, m_frameCount);
    }
    extractedFrameCount = countExtractedFrames();
    extracted = extracted && extractedFrameCount == m_frameCount;
  }

  {
    QMutexLocker locker(&sharedState->mutex);
    sharedState->frameCount = extracted ? m_frameCount : 0;
    sharedState->status =
        extracted ? WebPCacheStatus::Ready : WebPCacheStatus::Failed;
    sharedState->finished.wakeAll();
  }

  if (!extracted) {
    removeExtractedFrames();
    DVGui::warning(
        QObject::tr("FFmpeg decoded %1 of %2 expected WebP frames from: %3")
            .arg(extractedFrameCount)
            .arg(m_frameCount)
            .arg(m_path.getQString()));
    return false;
  }

  m_framesExtracted = true;
  return true;
}

//-----------------------------------------------------------------------------

TLevelP TLevelReaderWebP::loadInfo() {
  TLevelP level;
  for (int frameIndex = 1; frameIndex <= m_frameCount; ++frameIndex)
    level->setFrame(frameIndex, TImageP());

  return level;
}

//-----------------------------------------------------------------------------

TImageReaderP TLevelReaderWebP::getFrameReader(TFrameId fid) {
  if (!fid.getLetter().isEmpty()) return TImageReaderP(nullptr);

  const int frameIndex = fid.getNumber();
  if (frameIndex < 1 || frameIndex > m_frameCount)
    return TImageReaderP(nullptr);

  return TImageReaderP(
      new TImageReaderWebP(m_path, frameIndex, this, m_imageInfo));
}

//-----------------------------------------------------------------------------

TDimension TLevelReaderWebP::getSize() { return m_size; }

//-----------------------------------------------------------------------------

TImageP TLevelReaderWebP::load(int frameIndex) {
  if (!ensureFramesExtracted() || frameIndex < 1 ||
      frameIndex > m_frameCount)
    return TImageP();

  QImage image;
  if (!image.load(getFramePath(frameIndex), "PNG")) return TImageP();
  if (image.format() != QImage::Format_ARGB32)
    image = image.convertToFormat(QImage::Format_ARGB32);

  const int width  = image.width();
  const int height = image.height();
  TRasterPT<TPixelRGBM32> raster;
  raster.create(width, height);
  raster->lock();
  std::memcpy(raster->getRawData(), image.constBits(), width * height * 4);
  raster->unlock();
  raster->yMirror();

  return TRasterImageP(raster);
}
