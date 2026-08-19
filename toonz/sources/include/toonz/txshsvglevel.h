#pragma once

#ifndef TXSHSVGLEVEL_INCLUDED
#define TXSHSVGLEVEL_INCLUDED

#include "toonz/txshsimplelevel.h"

// Retained-source SVG level.
//
// The level itself remains SVG. Pixel images produced for Canvas, Preview, or
// rendering are disposable representations supplied through ImageManager.
// This class owns only the SVG-specific persistent identity and source-level
// sizing state; SVG parsing/rasterization stays in SvgLevel.
class DVAPI TXshSvgLevel final : public TXshSimpleLevel {
  PERSIST_DECLARATION(TXshSvgLevel)

private:
  TPointD m_sourceDpi;

public:
  explicit TXshSvgLevel(const std::wstring &name = std::wstring());
  ~TXshSvgLevel() override = default;

  void setSourceDpi(const TPointD &dpi) { m_sourceDpi = dpi; }
  TPointD getSourceDpi() const { return m_sourceDpi; }

  void loadData(TIStream &is) override;
  void saveData(TOStream &os) override;

  void load() override;

  // Scene saving must never rewrite the authoritative SVG source. The explicit
  // Save Level / Save Level As path is handled by the filepath overload below.
  void save() override;
  void save(const TFilePath &fp, const TFilePath &oldFp = TFilePath(),
            bool overwritePalette = true) override;
};

#endif  // TXSHSVGLEVEL_INCLUDED
