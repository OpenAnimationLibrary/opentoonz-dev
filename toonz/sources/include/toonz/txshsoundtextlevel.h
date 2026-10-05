#pragma once

#ifndef TXSHSOUNDTEXTLEVEL_INCLUDED
#define TXSHSOUNDTEXTLEVEL_INCLUDED

#include "toonz/txshlevel.h"
#include "toonz/noteink.h"
#include "tsound.h"
#include "tpersist.h"

#include <QColor>
#include <QList>

#undef DVAPI
#undef DVVAR
#ifdef TOONZLIB_EXPORTS
#define DVAPI DV_EXPORT_API
#define DVVAR DV_EXPORT_VAR
#else
#define DVAPI DV_IMPORT_API
#define DVVAR DV_IMPORT_VAR
#endif

//=============================================================================
//! The TXshSoundTextLevel
//=============================================================================

class DVAPI TXshSoundTextLevel final : public TXshLevel {
  PERSIST_DECLARATION(TXshSoundTextLevel)

  DECLARE_CLASS_CODE

  QList<QString> m_framesText;
  QList<QColor> m_framesTextColor;
  QList<NoteInkStrokeList> m_framesInk;
  QColor m_pencilColors[NoteInkPencilCount];
  int m_pencilSize;
  int m_gridFade;
  int m_markStep;
  bool m_inkMode;
  bool m_cellTextMode;
  bool m_notebookMode;

public:
  TXshSoundTextLevel(std::wstring name = L"");
  ~TXshSoundTextLevel();

  TXshSoundTextLevel *clone() const;

  //! Overridden from TXshLevel
  TXshSoundTextLevel *getSoundTextLevel() override { return this; }

  void setFrameText(int frameIndex, QString);
  QString getFrameText(int frameIndex) const;
  void setFrameTextColor(int frameIndex, const QColor &color);
  QColor getFrameTextColor(int frameIndex) const;
  int getFrameTextCount() const;

  void ensureFrame(int frameIndex);
  void addFrameStroke(int frameIndex, const NoteInkStroke &stroke);
  void insertFrameStroke(int frameIndex, int strokeIndex,
                         const NoteInkStroke &stroke);
  void removeLastFrameStroke(int frameIndex);
  void removeFrameStrokeAt(int frameIndex, int strokeIndex);
  const NoteInkStrokeList &getFrameInk(int frameIndex) const;
  QList<NoteInkStrokeList> getAllInk() const;
  void setAllInk(const QList<NoteInkStrokeList> &ink);
  void clearAllInk();

  QColor getPencilColor(int index) const;
  void setPencilColor(int index, const QColor &color);
  int getPencilSize() const;
  void setPencilSize(int size);
  int getGridFade() const;
  void setGridFade(int fade);
  int getMarkStep() const;
  void setMarkStep(int step);
  bool isInkMode() const;
  void setInkMode(bool on);
  bool isCellTextMode() const;
  void setCellTextMode(bool on);
  bool isNotebookMode() const;
  void setNotebookMode(bool on);
  QList<QString> getAllFrameText() const;
  QList<QColor> getAllFrameTextColor() const;
  void setAllFrameText(const QList<QString> &texts,
                       const QList<QColor> &colors);
  void clearAllFrameText();
  bool hasFrameText() const;

  void loadData(TIStream &is) override;
  void saveData(TOStream &os) override;

  void load() override{};
  void save() override{};

private:
  // not implemented
  TXshSoundTextLevel(const TXshSoundTextLevel &);
  TXshSoundTextLevel &operator=(const TXshSoundTextLevel &);
};

#ifdef _WIN32
template class DV_EXPORT_API TSmartPointerT<TXshSoundTextLevel>;
#endif
typedef TSmartPointerT<TXshSoundTextLevel> TXshSoundTextLevelP;

#endif  // TXSHSOUNDTEXTLEVEL_INCLUDED
