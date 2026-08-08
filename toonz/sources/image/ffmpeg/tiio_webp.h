#pragma once

#ifndef TTIO_WEBP_INCLUDED
#define TTIO_WEBP_INCLUDED

#include "tlevel_io.h"
#include "trasterimage.h"
#include "tgeometry.h"

#include <QString>

class TLevelReaderWebP final : public TLevelReader {
public:
  explicit TLevelReaderWebP(const TFilePath &path);
  ~TLevelReaderWebP() override;

  TImageReaderP getFrameReader(TFrameId fid) override;
  TLevelP loadInfo() override;
  TImageP load(int frameIndex);
  TDimension getSize();

  static TLevelReader *create(const TFilePath &path) {
    return new TLevelReaderWebP(path);
  }

private:
  bool ensureFramesExtracted();
  bool extractFrames(bool ignoreLoop, int frameLimit);
  int probeFrameCount(bool ignoreLoop) const;
  int countExtractedFrames() const;
  QString getFramePath(int frameIndex) const;
  QString getFramePattern() const;
  void removeExtractedFrames();

  bool m_framesExtracted = false;
  int m_frameCount       = 0;
  TDimension m_size;
  TImageInfo *m_imageInfo = nullptr;
  QString m_cacheDir;
  QString m_tempBaseName;
};

#endif  // TTIO_WEBP_INCLUDED
