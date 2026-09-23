// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifndef TXSHEXRLEVEL_INCLUDED
#define TXSHEXRLEVEL_INCLUDED

#include "toonz/txshsimplelevel.h"

#include <string>
#include <vector>

class TXshExrLevel;

// Implemented by the retained EXR loader. Keeping this bridge independent of
// TXshSimpleLevel lets an EXR binding install its own image builders while it
// remains an ordinary OVL_XSHLEVEL to the rest of OpenToonz.
namespace ExrLevel {
DVAPI void restoreRetainedLevel(TXshExrLevel *level);
DVAPI void restoreRetainedLevel(TXshExrLevel *level,
                                const std::vector<TFrameId> &fIds);
DVAPI bool saveRetainedCopy(TXshExrLevel *level, const TFilePath &path,
                            const TFilePath &oldPath, bool overwrite);
}  // namespace ExrLevel

// A read-only, retained-source OpenEXR level. The inherited path always names
// the physical file or numbered sequence; the OpenEXR part/layer selection is
// serialized separately so it cannot interfere with TFilePath frame parsing.
class DVAPI TXshExrLevel final : public TXshSimpleLevel {
  PERSIST_DECLARATION(TXshExrLevel)

public:
  struct Selection {
    // OpenEXR part name/stable id. The zero-based index keeps unnamed parts
    // addressable; named selectors resolve strictly across a sequence.
    std::string part;
    int partIndexFallback = 0;

    // An empty string selects the source's base layer.
    std::string layer;

    bool raw = false;

    bool operator==(const Selection &other) const;
    bool operator!=(const Selection &other) const { return !(*this == other); }
  };

  explicit TXshExrLevel(const std::wstring &name = std::wstring());
  ~TXshExrLevel() override;

  const Selection &getSelection() const { return m_selection; }
  void setSelection(const Selection &selection);
  void setSelection(const Selection &selection, bool primaryBinding);

  bool isPrimaryBinding() const { return m_primaryBinding; }
  void setPrimaryBinding(bool primary) { m_primaryBinding = primary; }

  // Used by level-path deduplication. A primary may still carry an explicit
  // stable part id after the loader resolves the source's first binding.
  bool isDefaultSelection() const { return isPrimaryBinding(); }

  void loadData(TIStream &is) override;
  void saveData(TOStream &os) override;

  void load() override;
  void load(const std::vector<TFrameId> &fIds);
  void updateReadOnly() override;
  bool isFrameReadOnly(TFrameId fid) override;

  // EXR bindings are authoritative-source/read-only. save() never rewrites
  // pixels; the filepath overload implements Save As by copying the source.
  void save() override;
  void save(const TFilePath &fp, const TFilePath &oldFp = TFilePath(),
            bool overwritePalette = true) override;

private:
  Selection m_selection;
  bool m_primaryBinding = true;
};

#ifdef _WIN32
template class DV_EXPORT_API TSmartPointerT<TXshExrLevel>;
#endif

using TXshExrLevelP = TSmartPointerT<TXshExrLevel>;

#endif  // TXSHEXRLEVEL_INCLUDED
