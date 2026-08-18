#include "toonz/txshsvglevel.h"

#include "../toonz/svglevelloader.h"

#include "toonz/txshleveltypes.h"
#include "tstream.h"

#include <map>
#include <string>

PERSIST_IDENTIFIER(TXshSvgLevel, "svgLevel")

//-----------------------------------------------------------------------------

TXshSvgLevel::TXshSvgLevel(const std::wstring &name)
    : TXshSimpleLevel(name), m_sourceDpi(120.0, 120.0) {
  setType(SVG_XSHLEVEL);
  setIsReadOnly(true);
}

//-----------------------------------------------------------------------------

void TXshSvgLevel::loadData(TIStream &is) {
  std::string tagName;
  bool nameRead = false;
  TFilePath path;

  for (;;) {
    if (is.matchTag(tagName)) {
      if (tagName == "svgInfo") {
        std::string value;
        double dpix = 120.0;
        double dpiy = 120.0;
        if (is.getTagParam("sourceDpix", value)) dpix = std::stod(value);
        if (is.getTagParam("sourceDpiy", value)) dpiy = std::stod(value);
        if (dpix <= 0.0) dpix = 120.0;
        if (dpiy <= 0.0) dpiy = 120.0;
        m_sourceDpi = TPointD(dpix, dpiy);
        is.matchEndTag();
      } else if (tagName == "path") {
        is >> path;
        is.matchEndTag();
      } else {
        throw TException("unexpected tag " + tagName);
      }
    } else {
      if (nameRead) break;
      nameRead = true;
      std::wstring name;
      is >> name;
      setName(name);
    }
  }

  setType(SVG_XSHLEVEL);
  setPath(path, true);
  setIsReadOnly(true);
  setDirtyFlag(false);
}

//-----------------------------------------------------------------------------

void TXshSvgLevel::saveData(TOStream &os) {
  os << getName();

  std::map<std::string, std::string> attr;
  attr["sourceDpix"] = std::to_string(m_sourceDpi.x);
  attr["sourceDpiy"] = std::to_string(m_sourceDpi.y);
  os.openCloseChild("svgInfo", attr);
  os.child("path") << getPath();
}

//-----------------------------------------------------------------------------

void TXshSvgLevel::load() {
  setType(SVG_XSHLEVEL);
  setIsReadOnly(true);

  SvgLevel::restoreRetainedLevel(this);

  // Restore failure must not turn the level into editable raster data. Missing
  // or unusable sources are handled later by the deferred relink/error UI.
  setType(SVG_XSHLEVEL);
  setIsReadOnly(true);
  setDirtyFlag(false);
}

//-----------------------------------------------------------------------------

void TXshSvgLevel::save() {
  // Scene save persists this level through saveData(). The authoritative SVG
  // source is intentionally not rewritten as a side effect of scene saving.
  setDirtyFlag(false);
}

//-----------------------------------------------------------------------------

void TXshSvgLevel::save(const TFilePath &fp, const TFilePath &oldFp,
                        bool overwritePalette) {
  (void)oldFp;
  (void)overwritePalette;
  SvgLevel::saveRetainedCopy(this, fp);
}
