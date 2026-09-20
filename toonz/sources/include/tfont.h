#pragma once

#ifndef TFONT_H
#define TFONT_H

// Toonz includes
#include "trastercm.h"
#include "texception.h"

#include <QString>

// STL includes
#include <cstdint>
#include <string>

#undef DVAPI
#undef DVVAR
#ifdef TVRENDER_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//-----------------------------------------------------

//  Forward declarations
class TVectorImageP;
class TFont;

//=================================================================================================

//! Class to manage loading and rendering of fonts.

class DVAPI TFont {
public:
  struct Impl;

private:
  friend class TFontManager;
  Impl *m_pimpl;

  TFont(const std::wstring family, const std::wstring face, int size);

public:
  ~TFont();

  TPoint drawChar(TVectorImageP &outImage, uint32_t charcode,
                  uint32_t nextCode = 0) const;
  TPoint drawChar(QImage &outImage, TPoint &glyphOrigin, uint32_t charcode,
                  uint32_t nextCode = 0) const;
  TPoint drawChar(TRasterCM32P &outImage, TPoint &glyphOrigin, int inkId,
                  uint32_t charcode, uint32_t nextCode = 0) const;

  TPoint drawText(TVectorImageP &outImage, const QString &text,
                  const QString &nextText = QString()) const;
  TPoint drawText(QImage &outImage, TPoint &glyphOrigin, const QString &text,
                  const QString &nextText = QString()) const;
  TPoint drawText(TRasterCM32P &outImage, TPoint &glyphOrigin, int inkId,
                  const QString &text,
                  const QString &nextText = QString()) const;

  // brief  get kerning distance between two characters
  TPoint getDistance(uint32_t firstChar, uint32_t secondChar) const;
  TPoint getDistance(const QString &text, const QString &nextText) const;

  int getMaxWidth() const;
  // void disableKerning();
  // void enableKerning();
  bool hasKerning() const;
  int getLineAscender() const;
  int getLineDescender() const;
  int getLineSpacing() const;
  int getHeight() const;
  int getAverageCharWidth() const;
  bool hasVertical() const;
  std::wstring getFamily() const;
  std::wstring getTypeface() const;
  // bool isValidCode( wchar_t code ) const;

private:
  // not implemented
  TFont(const TFont &);
  TFont &operator=(const TFont &);
};

//-----------------------------------------------------------------------------

class TFontCreationError final : public TException {
public:
  TFontCreationError() : TException("E_CanNotUseSelectedFont") {}
};

//-----------------------------------------------------------------------------

class TFontLibraryLoadingError final : public TException {
public:
  TFontLibraryLoadingError() : TException("E_CanNotLoadFonts") {}
};

//-----------------------------------------------------------------------------

// singleton
class DVAPI TFontManager {
  struct Impl;
  Impl *m_pimpl;

  TFontManager();
  ~TFontManager();

public:
  static TFontManager *instance();

  TFont *getCurrentFont();

  //! throws TFontLibraryLoadingError if can not load fonts
  void loadFontNames();

  //! if can not create font, throws TFontCreationError and leave as current the
  //! old one
  void setFamily(const std::wstring family);

  //! if can not create font, throws TFontCreationError and leave as current the
  //! old one
  void setTypeface(const std::wstring typeface);

  std::wstring getCurrentFamily() const;
  std::wstring getCurrentTypeface() const;
  void getAllFamilies(std::vector<std::wstring> &families) const;
  void getAllTypefaces(std::vector<std::wstring> &typefaces) const;
  void setVertical(bool vertical);
  void setSize(int size);

  bool isBold(const QString &family, const QString &style);
  bool isItalic(const QString &family, const QString &style);

  // --------- TFont methods called on current font -----------

  TPoint drawChar(TVectorImageP &outImage, uint32_t charcode,
                  uint32_t nextCode = 0) {
    return getCurrentFont()->drawChar(outImage, charcode, nextCode);
  }

  TPoint drawChar(QImage &outImage, TPoint &glyphOrigin, uint32_t charcode,
                  uint32_t nextCode = 0) {
    return getCurrentFont()->drawChar(outImage, glyphOrigin, charcode,
                                      nextCode);
  }

  TPoint drawChar(TRasterCM32P &outImage, TPoint &glyphOrigin, int inkId,
                  uint32_t charcode, uint32_t nextCode = 0) {
    return getCurrentFont()->drawChar(outImage, glyphOrigin, inkId, charcode,
                                      nextCode);
  }

  TPoint drawText(TVectorImageP &outImage, const QString &text,
                  const QString &nextText = QString()) {
    return getCurrentFont()->drawText(outImage, text, nextText);
  }

  TPoint drawText(QImage &outImage, TPoint &glyphOrigin, const QString &text,
                  const QString &nextText = QString()) {
    return getCurrentFont()->drawText(outImage, glyphOrigin, text, nextText);
  }

  TPoint drawText(TRasterCM32P &outImage, TPoint &glyphOrigin, int inkId,
                  const QString &text,
                  const QString &nextText = QString()) {
    return getCurrentFont()->drawText(outImage, glyphOrigin, inkId, text,
                                      nextText);
  }

  TPoint getDistance(uint32_t firstChar, uint32_t secondChar) {
    return getCurrentFont()->getDistance(firstChar, secondChar);
  }

  TPoint getDistance(const QString &text, const QString &nextText = QString()) {
    return getCurrentFont()->getDistance(text, nextText);
  }

  int getMaxWidth() { return getCurrentFont()->getMaxWidth(); }
  bool hasKerning() { return getCurrentFont()->hasKerning(); }
  int getLineAscender() { return getCurrentFont()->getLineAscender(); }
  int getLineDescender() { return getCurrentFont()->getLineDescender(); }
  int getLineSpacing() { return getCurrentFont()->getLineSpacing(); }
  int getHeight() { return getCurrentFont()->getHeight(); }
  int getAverageCharWidth() { return getCurrentFont()->getAverageCharWidth(); }
  bool hasVertical() { return getCurrentFont()->hasVertical(); }
};

//-----------------------------------------------------------------------------

#endif
