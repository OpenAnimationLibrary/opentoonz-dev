// SPDX-License-Identifier: BSD-3-Clause

#include "toonz/txshexrlevel.h"

#include "toonz/levelproperties.h"
#include "toonz/toonzscene.h"
#include "toonz/txshleveltypes.h"

#include "tstream.h"
#include "tsystem.h"

#include <map>
#include <string>

PERSIST_IDENTIFIER(TXshExrLevel, "exrLevel")

namespace {

constexpr int kPersistenceVersion = 1;

bool readBool(const std::string &value, bool fallback) {
  if (value == "1" || value == "true") return true;
  if (value == "0" || value == "false") return false;
  return fallback;
}

int readInt(const std::string &value, int fallback) {
  try {
    return std::stoi(value);
  } catch (...) {
    return fallback;
  }
}

double readDouble(const std::string &value, double fallback) {
  try {
    return std::stod(value);
  } catch (...) {
    return fallback;
  }
}

}  // namespace

//-----------------------------------------------------------------------------

bool TXshExrLevel::Selection::operator==(const Selection &other) const {
  return part == other.part && partIndexFallback == other.partIndexFallback &&
         layer == other.layer && raw == other.raw;
}

//-----------------------------------------------------------------------------

TXshExrLevel::TXshExrLevel(const std::wstring &name) : TXshSimpleLevel(name) {
  setType(OVL_XSHLEVEL);
  setFloatChannelLevel(true);
  setIsReadOnly(true);
}

//-----------------------------------------------------------------------------

TXshExrLevel::~TXshExrLevel() = default;

//-----------------------------------------------------------------------------

void TXshExrLevel::setSelection(const Selection &selection) {
  m_selection = selection;
}

//-----------------------------------------------------------------------------

void TXshExrLevel::setSelection(const Selection &selection,
                                bool primaryBinding) {
  m_selection      = selection;
  m_primaryBinding = primaryBinding;
}

//-----------------------------------------------------------------------------

void TXshExrLevel::loadData(TIStream &is) {
  m_selection      = Selection();
  m_primaryBinding = true;

  std::string tagName;
  bool nameRead = false;

  for (;;) {
    if (is.matchTag(tagName)) {
      if (tagName == "path") {
        TFilePath path;
        is >> path;
        is.matchEndTag();
        setPath(path, true);
      } else if (tagName == "scannedPath") {
        TFilePath path;
        is >> path;
        is.matchEndTag();
        setScannedPath(path);
      } else if (tagName == "info") {
        std::string value;
        double xdpi = 0.0, ydpi = 0.0;
        int subsampling        = 1;
        int doPremultiply      = 0;
        int whiteTransp        = 0;
        int antialiasSoftness  = 0;
        int isStopMotionLevel  = 0;
        double colorSpaceGamma = LevelOptions::DefaultColorSpaceGamma;
        LevelProperties::DpiPolicy dpiPolicy = LevelProperties::DP_ImageDpi;

        if (is.getTagParam("dpix", value)) xdpi = readDouble(value, xdpi);
        if (is.getTagParam("dpiy", value)) ydpi = readDouble(value, ydpi);
        if (xdpi != 0.0 && ydpi != 0.0)
          dpiPolicy = LevelProperties::DP_CustomDpi;

        if (is.getTagAttribute("dpiType") == "image")
          dpiPolicy = LevelProperties::DP_ImageDpi;
        if (is.getTagParam("subsampling", value))
          subsampling = readInt(value, subsampling);
        if (is.getTagParam("premultiply", value))
          doPremultiply = readInt(value, doPremultiply);
        if (is.getTagParam("antialias", value))
          antialiasSoftness = readInt(value, antialiasSoftness);
        if (is.getTagParam("whiteTransp", value))
          whiteTransp = readInt(value, whiteTransp);
        if (is.getTagParam("isStopMotionLevel", value))
          isStopMotionLevel = readInt(value, isStopMotionLevel);
        if (is.getTagParam("colorSpaceGamma", value))
          colorSpaceGamma = readDouble(value, colorSpaceGamma);

        LevelProperties *properties = getProperties();
        properties->setDpiPolicy(dpiPolicy);
        properties->setDpi(TPointD(xdpi, ydpi));
        properties->setSubsampling(subsampling);
        properties->setDoPremultiply(doPremultiply);
        properties->setDoAntialias(antialiasSoftness);
        properties->setWhiteTransp(whiteTransp);
        properties->setIsStopMotion(isStopMotionLevel);
        properties->setColorSpaceGamma(colorSpaceGamma);
      } else if (tagName == "exrSelection") {
        std::string value;
        if (is.getTagParam("part", value)) m_selection.part = value;
        if (is.getTagParam("partIndexFallback", value))
          m_selection.partIndexFallback =
              readInt(value, m_selection.partIndexFallback);
        if (is.getTagParam("layer", value)) m_selection.layer = value;
        if (is.getTagParam("raw", value))
          m_selection.raw = readBool(value, m_selection.raw);
        if (is.getTagParam("primary", value))
          m_primaryBinding = readBool(value, m_primaryBinding);
      } else {
        // Preserve forward compatibility with optional future EXR binding
        // metadata without weakening parsing of the known fields above.
        is.skipCurrentTag();
      }
    } else {
      if (nameRead) break;
      nameRead = true;

      std::wstring token;
      is >> token;
      if (token == L"__empty") is >> token;
      setName(token);
    }
  }

  setType(OVL_XSHLEVEL);
  setFloatChannelLevel(true);
  setIsReadOnly(true);
}

//-----------------------------------------------------------------------------

void TXshExrLevel::saveData(TOStream &os) {
  os << getName();

  std::map<std::string, std::string> info;
  LevelProperties *properties = getProperties();
  if (properties->getDpiPolicy() == LevelProperties::DP_CustomDpi) {
    const TPointD dpi = properties->getDpi();
    if (dpi.x != 0.0 && dpi.y != 0.0) {
      info["dpix"] = std::to_string(dpi.x);
      info["dpiy"] = std::to_string(dpi.y);
    }
  } else {
    info["dpiType"] = "image";
  }

  if (properties->getSubsampling() != 1)
    info["subsampling"] = std::to_string(properties->getSubsampling());
  if (properties->antialiasSoftness() > 0)
    info["antialias"] = std::to_string(properties->antialiasSoftness());
  if (properties->doPremultiply())
    info["premultiply"] = std::to_string(properties->doPremultiply());
  else if (properties->whiteTransp())
    info["whiteTransp"] = std::to_string(properties->whiteTransp());
  else if (properties->isStopMotionLevel())
    info["isStopMotionLevel"] = std::to_string(properties->isStopMotionLevel());
  if (!areAlmostEqual(properties->colorSpaceGamma(),
                      LevelOptions::DefaultColorSpaceGamma))
    info["colorSpaceGamma"] = std::to_string(properties->colorSpaceGamma());
  os.openCloseChild("info", info);

  os.child("path") << getPath();
  if (!getScannedPath().isEmpty()) os.child("scannedPath") << getScannedPath();

  std::map<std::string, std::string> selection;
  selection["version"] = std::to_string(kPersistenceVersion);
  selection["part"]    = m_selection.part;
  selection["partIndexFallback"] =
      std::to_string(m_selection.partIndexFallback);
  selection["layer"]   = m_selection.layer;
  selection["raw"]     = m_selection.raw ? "1" : "0";
  selection["primary"] = m_primaryBinding ? "1" : "0";
  os.openCloseChild("exrSelection", selection);
}

//-----------------------------------------------------------------------------

void TXshExrLevel::load() {
  setType(OVL_XSHLEVEL);
  TFrameId rangeFrom, rangeTo;
  getLoadingLevelRange(rangeFrom, rangeTo);
  setIsSubsequence(rangeFrom <= rangeTo);
  ExrLevel::restoreRetainedLevel(this);
  setFloatChannelLevel(true);
  setIsReadOnly(true);
}

//-----------------------------------------------------------------------------

void TXshExrLevel::load(const std::vector<TFrameId> &fIds) {
  setType(OVL_XSHLEVEL);
  TFrameId rangeFrom, rangeTo;
  getLoadingLevelRange(rangeFrom, rangeTo);
  setIsSubsequence(rangeFrom <= rangeTo);
  ExrLevel::restoreRetainedLevel(this, fIds);
  setFloatChannelLevel(true);
  setIsReadOnly(true);
}

//-----------------------------------------------------------------------------

void TXshExrLevel::updateReadOnly() { setIsReadOnly(true); }

//-----------------------------------------------------------------------------

bool TXshExrLevel::isFrameReadOnly(TFrameId fid) {
  (void)fid;
  return true;
}

//-----------------------------------------------------------------------------

void TXshExrLevel::save() {
  // The selected OpenEXR part/layer is a retained source view, not an editable
  // pixel level. Scene metadata is persisted through saveData().
}

//-----------------------------------------------------------------------------

void TXshExrLevel::save(const TFilePath &fp, const TFilePath &oldFp,
                        bool overwritePalette) {
  (void)overwritePalette;

  if (!ExrLevel::saveRetainedCopy(this, fp, oldFp, true)) {
    const TFilePath destination =
        getScene() ? getScene()->decodeFilePath(fp) : fp;
    throw TSystemException(
        destination,
        "The EXR level could not be copied without re-encoding its source.");
  }

  // Match TXshSimpleLevel Save As semantics: write a copy while leaving the
  // scene level bound to its original authoritative source.
}
