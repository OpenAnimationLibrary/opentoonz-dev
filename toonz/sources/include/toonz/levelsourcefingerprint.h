#pragma once

#ifndef LEVELSOURCEFINGERPRINT_INCLUDED
#define LEVELSOURCEFINGERPRINT_INCLUDED

#include "tfilepath.h"

#include <QFileInfo>

// Lightweight identity for the on-disk source accepted by a loaded level.
//
// The first implementation intentionally handles physical single-file sources
// only. Image-sequence member aggregation is added separately so that normal
// frame access never needs to scan a directory.
struct LevelSourceFingerprint {
  TFilePath m_path;
  qint64 m_size         = -1;
  qint64 m_modifiedTime = -1;
  int m_frameCount      = -1;
  bool m_valid          = false;

  static LevelSourceFingerprint fromFile(const TFilePath &decodedPath,
                                         int frameCount = -1) {
    LevelSourceFingerprint fingerprint;

    // A level-name path represents an image sequence rather than one physical
    // file. Sequence fingerprints require member aggregation and are therefore
    // deliberately left invalid in this first implementation.
    if (decodedPath.isEmpty() || decodedPath.isLevelName()) return fingerprint;

    QFileInfo info(decodedPath.getQString());
    if (!info.exists() || !info.isFile()) return fingerprint;

    QString canonicalPath = info.canonicalFilePath();
    if (canonicalPath.isEmpty()) canonicalPath = info.absoluteFilePath();

    fingerprint.m_path         = TFilePath(canonicalPath);
    fingerprint.m_size         = info.size();
    fingerprint.m_modifiedTime = info.lastModified().toMSecsSinceEpoch();
    fingerprint.m_frameCount   = frameCount;
    fingerprint.m_valid        = true;
    return fingerprint;
  }

  bool operator==(const LevelSourceFingerprint &other) const {
    if (m_valid != other.m_valid) return false;
    if (!m_valid) return true;

    if (m_path != other.m_path || m_size != other.m_size ||
        m_modifiedTime != other.m_modifiedTime)
      return false;

    // Frame extent is compared when both sides know it. This lets the initial
    // metadata-only probe compare against an accepted fingerprint while
    // leaving room for sequence/movie extent checks in the next phase.
    return m_frameCount < 0 || other.m_frameCount < 0 ||
           m_frameCount == other.m_frameCount;
  }

  bool operator!=(const LevelSourceFingerprint &other) const {
    return !(*this == other);
  }
};

#endif  // LEVELSOURCEFINGERPRINT_INCLUDED
