

#include "toonz/txshsoundtextlevel.h"
#include "toonz/txshleveltypes.h"
#include "tstream.h"

//-----------------------------------------------------------------------------

DEFINE_CLASS_CODE(TXshSoundTextLevel, 54)

PERSIST_IDENTIFIER(TXshSoundTextLevel, "soundTextLevel")

//=============================================================================

TXshSoundTextLevel::TXshSoundTextLevel(std::wstring name)
    : TXshLevel(m_classCode, name)
    , m_framesText()
    , m_pencilSize(3)
    , m_gridFade(0)
    , m_markStep(0)
    , m_inkMode(false)
    , m_cellTextMode(false)
    , m_notebookMode(false) {
  for (int i = 0; i < NoteInkPencilCount; i++)
    m_pencilColors[i] = defaultNoteInkPencil(i);
}

//-----------------------------------------------------------------------------

TXshSoundTextLevel::~TXshSoundTextLevel() {}

//-----------------------------------------------------------------------------

TXshSoundTextLevel *TXshSoundTextLevel::clone() const {
  TXshSoundTextLevel *sound = new TXshSoundTextLevel(m_name);
  sound->setType(getType());
  sound->m_framesText      = m_framesText;
  sound->m_framesTextColor = m_framesTextColor;
  sound->m_framesInk       = m_framesInk;
  for (int i = 0; i < NoteInkPencilCount; i++)
    sound->m_pencilColors[i] = m_pencilColors[i];
  sound->m_pencilSize   = m_pencilSize;
  sound->m_gridFade     = m_gridFade;
  sound->m_markStep     = m_markStep;
  sound->m_inkMode      = m_inkMode;
  sound->m_cellTextMode = m_cellTextMode;
  sound->m_notebookMode = m_notebookMode;
  return sound;
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setFrameText(int frameIndex, QString text) {
  while (frameIndex >= m_framesText.size()) {
    m_framesText.append(QString(" "));
    m_framesTextColor.append(QColor(Qt::black));
  }
  m_framesText.replace(frameIndex, text);
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setFrameTextColor(int frameIndex,
                                           const QColor &color) {
  if (frameIndex < 0) return;
  while (frameIndex >= m_framesTextColor.size())
    m_framesTextColor.append(QColor(Qt::black));
  m_framesTextColor[frameIndex] = color.isValid() ? color : QColor(Qt::black);
}

//-----------------------------------------------------------------------------

QColor TXshSoundTextLevel::getFrameTextColor(int frameIndex) const {
  if (frameIndex < 0 || frameIndex >= m_framesTextColor.size())
    return QColor(Qt::black);
  QColor c = m_framesTextColor[frameIndex];
  return c.isValid() ? c : QColor(Qt::black);
}

//-----------------------------------------------------------------------------

QString TXshSoundTextLevel::getFrameText(int frameIndex) const {
  if (frameIndex >= m_framesText.size()) return QString();
  return m_framesText[frameIndex];
}

//-----------------------------------------------------------------------------

int TXshSoundTextLevel::getFrameTextCount() const {
  return m_framesText.size();
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::ensureFrame(int frameIndex) {
  if (frameIndex < 0) return;
  while (frameIndex >= m_framesText.size()) m_framesText.append(QString(" "));
  while (frameIndex >= m_framesTextColor.size())
    m_framesTextColor.append(QColor(Qt::black));
  while (frameIndex >= m_framesInk.size())
    m_framesInk.append(NoteInkStrokeList());
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::addFrameStroke(int frameIndex,
                                        const NoteInkStroke &stroke) {
  if (frameIndex < 0 || stroke.points.isEmpty()) return;
  ensureFrame(frameIndex);
  m_framesInk[frameIndex].append(stroke);
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::insertFrameStroke(int frameIndex, int strokeIndex,
                                           const NoteInkStroke &stroke) {
  if (frameIndex < 0 || stroke.points.isEmpty()) return;
  ensureFrame(frameIndex);
  if (strokeIndex < 0 || strokeIndex > m_framesInk[frameIndex].size())
    m_framesInk[frameIndex].append(stroke);
  else
    m_framesInk[frameIndex].insert(strokeIndex, stroke);
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::removeLastFrameStroke(int frameIndex) {
  if (frameIndex < 0 || frameIndex >= m_framesInk.size()) return;
  if (m_framesInk[frameIndex].isEmpty()) return;
  m_framesInk[frameIndex].removeLast();
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::removeFrameStrokeAt(int frameIndex, int strokeIndex) {
  if (frameIndex < 0 || frameIndex >= m_framesInk.size()) return;
  if (strokeIndex < 0 || strokeIndex >= m_framesInk[frameIndex].size()) return;
  m_framesInk[frameIndex].removeAt(strokeIndex);
}

//-----------------------------------------------------------------------------

QList<NoteInkStrokeList> TXshSoundTextLevel::getAllInk() const {
  return m_framesInk;
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setAllInk(const QList<NoteInkStrokeList> &ink) {
  m_framesInk = ink;
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::clearAllInk() { m_framesInk.clear(); }

//-----------------------------------------------------------------------------

QColor TXshSoundTextLevel::getPencilColor(int index) const {
  if (index < 0 || index >= NoteInkPencilCount) return defaultNoteInkPencil(0);
  return m_pencilColors[index];
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setPencilColor(int index, const QColor &color) {
  if (index < 0 || index >= NoteInkPencilCount) return;
  m_pencilColors[index] = color.isValid() ? color : defaultNoteInkPencil(index);
}

//-----------------------------------------------------------------------------

int TXshSoundTextLevel::getPencilSize() const {
  if (m_pencilSize < 1) return 1;
  if (m_pencilSize > 20) return 20;
  return m_pencilSize;
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setPencilSize(int size) {
  if (size < 1) size = 1;
  if (size > 20) size = 20;
  m_pencilSize = size;
}

//-----------------------------------------------------------------------------

int TXshSoundTextLevel::getGridFade() const {
  if (m_gridFade < 0) return 0;
  if (m_gridFade > 100) return 100;
  return m_gridFade;
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setGridFade(int fade) {
  if (fade < 0) fade = 0;
  if (fade > 100) fade = 100;
  m_gridFade = fade;
}

//-----------------------------------------------------------------------------

int TXshSoundTextLevel::getMarkStep() const {
  if (m_markStep < 0) return 0;
  if (m_markStep > 24) return 24;
  return m_markStep;
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::setMarkStep(int step) {
  if (step < 0) step = 0;
  if (step > 24) step = 24;
  m_markStep = step;
}

//-----------------------------------------------------------------------------

bool TXshSoundTextLevel::isInkMode() const { return m_inkMode; }

void TXshSoundTextLevel::setInkMode(bool on) { m_inkMode = on; }

bool TXshSoundTextLevel::isCellTextMode() const { return m_cellTextMode; }

void TXshSoundTextLevel::setCellTextMode(bool on) { m_cellTextMode = on; }

bool TXshSoundTextLevel::isNotebookMode() const { return m_notebookMode; }

void TXshSoundTextLevel::setNotebookMode(bool on) { m_notebookMode = on; }

QList<QString> TXshSoundTextLevel::getAllFrameText() const {
  return m_framesText;
}

QList<QColor> TXshSoundTextLevel::getAllFrameTextColor() const {
  return m_framesTextColor;
}

void TXshSoundTextLevel::setAllFrameText(const QList<QString> &texts,
                                         const QList<QColor> &colors) {
  m_framesText      = texts;
  m_framesTextColor = colors;
}

void TXshSoundTextLevel::clearAllFrameText() {
  for (int i = 0; i < m_framesText.size(); i++) m_framesText[i] = QString();
}

bool TXshSoundTextLevel::hasFrameText() const {
  for (int i = 0; i < m_framesText.size(); i++) {
    if (!m_framesText[i].trimmed().isEmpty()) return true;
  }
  return false;
}

//-----------------------------------------------------------------------------

const NoteInkStrokeList &TXshSoundTextLevel::getFrameInk(int frameIndex) const {
  static const NoteInkStrokeList empty;
  if (frameIndex < 0 || frameIndex >= m_framesInk.size()) return empty;
  return m_framesInk[frameIndex];
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::loadData(TIStream &is) {
  is >> m_name;
  setName(m_name);
  std::string tagName;
  int type = UNKNOWN_XSHLEVEL;
  while (is.matchTag(tagName)) {
    if (tagName == "type") {
      std::string v;
      is >> v;
      if (v == "textSound") type = SND_TXT_XSHLEVEL;
      is.matchEndTag();
    } else if (tagName == "frame") {
      std::wstring text;
      is >> text;
      m_framesText.push_back(QString::fromStdWString(text));
      QColor textColor(Qt::black);
      std::string innerTag;
      while (is.matchTag(innerTag)) {
        if (innerTag == "tc") {
          int r = 0, g = 0, b = 0;
          is >> r >> g >> b;
          textColor = QColor(r, g, b);
          is.matchEndTag();
        } else
          is.matchEndTag();
      }
      m_framesTextColor.push_back(textColor);
      is.matchEndTag();
    } else if (tagName == "ink") {
      std::string inkTag;
      while (is.matchTag(inkTag)) {
        if (inkTag == "f") {
          int index = 0;
          is >> index;
          ensureFrame(index);
          std::string strokeTag;
          while (is.matchTag(strokeTag)) {
            if (strokeTag == "s") {
              int n = 0;
              is >> n;
              NoteInkStroke stroke;
              int r = 40, g = 40, b = 40;
              std::string innerTag;
              while (is.matchTag(innerTag)) {
                if (innerTag == "c") {
                  is >> r >> g >> b;
                  is.matchEndTag();
                } else if (innerTag == "w") {
                  is >> stroke.width;
                  is.matchEndTag();
                } else
                  is.matchEndTag();
              }
              stroke.color = QColor(r, g, b);
              if (n > 0) stroke.points.reserve(n);
              for (int i = 0; i < n; ++i) {
                double x = 0, y = 0;
                is >> x >> y;
                stroke.points.append(QPointF(x, y));
              }
              if (!stroke.points.isEmpty()) m_framesInk[index].append(stroke);
              is.matchEndTag();
            } else
              is.matchEndTag();
          }
          is.matchEndTag();
        } else
          is.matchEndTag();
      }
      is.matchEndTag();
    } else if (tagName == "noteSize") {
      int size = 3;
      is >> size;
      setPencilSize(size);
      is.matchEndTag();
    } else if (tagName == "noteFade") {
      int fade = 0;
      is >> fade;
      setGridFade(fade);
      is.matchEndTag();
    } else if (tagName == "noteMark") {
      int step = 0;
      is >> step;
      setMarkStep(step);
      is.matchEndTag();
    } else if (tagName == "noteInk") {
      int v = 0;
      is >> v;
      m_inkMode = v != 0;
      is.matchEndTag();
    } else if (tagName == "noteCellText") {
      int v = 0;
      is >> v;
      m_cellTextMode = v != 0;
      is.matchEndTag();
    } else if (tagName == "noteNotebook") {
      int v = 0;
      is >> v;
      m_notebookMode = v != 0;
      is.matchEndTag();
    } else if (tagName == "noteBrushes") {
      for (int i = 0; i < NoteInkPencilCount; i++) {
        int r = 40, g = 40, b = 40;
        is >> r >> g >> b;
        m_pencilColors[i] = QColor(r, g, b);
      }
      is.matchEndTag();
    } else
      throw TException("unexpected tag " + tagName);
  }
  setType(type);
}

//-----------------------------------------------------------------------------

void TXshSoundTextLevel::saveData(TOStream &os) {
  os << m_name;
  int i;
  for (i = 0; i < m_framesText.size(); i++) {
    os.openChild("frame");
    os << m_framesText[i];
    QColor tc = getFrameTextColor(i);
    if (tc != QColor(Qt::black)) {
      os.openChild("tc");
      os << tc.red() << tc.green() << tc.blue();
      os.closeChild();
    }
    os.closeChild();
  }
  os.child("type") << L"textSound";

  bool hasInk = false;
  for (int i = 0; i < m_framesInk.size(); i++) {
    if (!m_framesInk[i].isEmpty()) {
      hasInk = true;
      break;
    }
  }
  bool customBrushes = false;
  for (int i = 0; i < NoteInkPencilCount; i++) {
    if (m_pencilColors[i] != defaultNoteInkPencil(i)) {
      customBrushes = true;
      break;
    }
  }
  if (customBrushes || hasInk) {
    os.openChild("noteBrushes");
    for (int i = 0; i < NoteInkPencilCount; i++) {
      os << m_pencilColors[i].red() << m_pencilColors[i].green()
         << m_pencilColors[i].blue();
    }
    os.closeChild();
  }
  if (m_pencilSize != 3 || hasInk) {
    os.openChild("noteSize");
    os << getPencilSize();
    os.closeChild();
  }
  if (m_inkMode) os.child("noteInk") << 1;
  if (m_cellTextMode) os.child("noteCellText") << 1;
  if (m_notebookMode) os.child("noteNotebook") << 1;
  if (m_gridFade != 0) {
    os.openChild("noteFade");
    os << getGridFade();
    os.closeChild();
  }
  if (m_markStep != 0) {
    os.openChild("noteMark");
    os << getMarkStep();
    os.closeChild();
  }
  if (!hasInk) return;

  os.openChild("ink");
  for (int i = 0; i < m_framesInk.size(); i++) {
    if (m_framesInk[i].isEmpty()) continue;
    os.openChild("f");
    os << i;
    for (const NoteInkStroke &stroke : m_framesInk[i]) {
      os.openChild("s");
      os << (int)stroke.points.size();
      os.openChild("c");
      os << stroke.color.red() << stroke.color.green() << stroke.color.blue();
      os.closeChild();
      os.openChild("w");
      os << stroke.width;
      os.closeChild();
      for (const QPointF &pt : stroke.points)
        os << (double)pt.x() << (double)pt.y();
      os.closeChild();
    }
    os.closeChild();
  }
  os.closeChild();
}
