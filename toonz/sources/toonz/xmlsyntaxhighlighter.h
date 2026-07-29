#pragma once

#ifdef _WIN32

#include <QAbstractScrollArea>
#include <QColor>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTimer>
#include <QVector>
#include <QWidget>

namespace SvgSourceEditor {

//=============================================================================
// XML / SVG syntax highlighting
//-----------------------------------------------------------------------------

class XmlSyntaxHighlighter final : public QSyntaxHighlighter {
  struct Rule {
    QRegularExpression pattern;
    QTextCharFormat format;
  };

  QVector<Rule> m_rules;
  QTextCharFormat m_commentFormat;
  QPointer<QWidget> m_editor;
  int m_darkState = -1;
  bool m_enabled = true;

public:
  explicit XmlSyntaxHighlighter(QTextDocument *document)
      : QSyntaxHighlighter(document) {
    // The default document created by QPlainTextEdit is parented to the editor.
    // Walk upward as a safeguard for a document with an intermediate owner.
    QObject *owner = document;
    while (owner && !m_editor) {
      m_editor = qobject_cast<QWidget *>(owner);
      owner = owner->parent();
    }

    if (m_editor) m_editor->installEventFilter(this);

    rebuildFormats(isDarkBackground());
    QTimer::singleShot(0, this, [this]() { updateColorScheme(); });
  }

  ~XmlSyntaxHighlighter() override {
    if (m_editor) m_editor->removeEventFilter(this);
  }

  void setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    rehighlight();
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == m_editor && event &&
        (event->type() == QEvent::PaletteChange ||
         event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::StyleChange ||
         event->type() == QEvent::Polish ||
         event->type() == QEvent::Show)) {
      // Style-sheet palette changes settle after the event that announced them.
      QTimer::singleShot(0, this, [this]() { updateColorScheme(); });
    }
    return QObject::eventFilter(watched, event);
  }

  void highlightBlock(const QString &text) override {
    if (!m_enabled) {
      setCurrentBlockState(0);
      return;
    }

    for (const Rule &rule : m_rules) {
      QRegularExpressionMatchIterator iterator =
          rule.pattern.globalMatch(text);
      while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        setFormat(match.capturedStart(), match.capturedLength(), rule.format);
      }
    }

    setCurrentBlockState(0);
    int startIndex =
        previousBlockState() == 1 ? 0 : text.indexOf(QStringLiteral("<!--"));

    while (startIndex >= 0) {
      const int endIndex = text.indexOf(QStringLiteral("-->"), startIndex);
      int commentLength = 0;
      if (endIndex == -1) {
        setCurrentBlockState(1);
        commentLength = text.length() - startIndex;
      } else {
        commentLength = endIndex - startIndex + 3;
      }
      setFormat(startIndex, commentLength, m_commentFormat);
      startIndex = endIndex == -1
                       ? -1
                       : text.indexOf(QStringLiteral("<!--"),
                                      startIndex + commentLength);
    }
  }

private:
  bool isDarkBackground() const {
    QPalette palette = QGuiApplication::palette();

    if (m_editor) {
      palette = m_editor->palette();
      if (QAbstractScrollArea *area =
              qobject_cast<QAbstractScrollArea *>(m_editor.data()))
        palette = area->viewport()->palette();
    }

    QColor base = palette.color(QPalette::Base);
    if (!base.isValid()) base = palette.color(QPalette::Window);
    return base.lightness() < 128;
  }

  void updateColorScheme() {
    const bool dark = isDarkBackground();
    if (m_darkState == (dark ? 1 : 0)) return;
    rebuildFormats(dark);
    rehighlight();
  }

  void rebuildFormats(bool dark) {
    m_darkState = dark ? 1 : 0;
    m_rules.clear();

    QTextCharFormat tagFormat;
    tagFormat.setForeground(dark ? QColor(86, 156, 214) : QColor(0, 0, 160));
    tagFormat.setFontWeight(QFont::DemiBold);

    QTextCharFormat attributeFormat;
    attributeFormat.setForeground(dark ? QColor(156, 220, 254)
                                       : QColor(128, 0, 128));

    QTextCharFormat valueFormat;
    valueFormat.setForeground(dark ? QColor(206, 145, 120)
                                   : QColor(160, 70, 0));

    QTextCharFormat entityFormat;
    entityFormat.setForeground(dark ? QColor(220, 220, 170)
                                    : QColor(120, 80, 0));

    QTextCharFormat processingFormat;
    processingFormat.setForeground(dark ? QColor(197, 134, 192)
                                        : QColor(120, 0, 120));

    m_commentFormat.setForeground(dark ? QColor(106, 153, 85)
                                       : QColor(0, 128, 0));

    m_rules.push_back(
        {QRegularExpression(R"(</?\s*[A-Za-z_][A-Za-z0-9_.:-]*)"),
         tagFormat});
    m_rules.push_back(
        {QRegularExpression(
             R"(\b[A-Za-z_:][A-Za-z0-9_.:-]*(?=\s*=))"),
         attributeFormat});
    m_rules.push_back(
        {QRegularExpression(R"(("[^"]*"|'[^']*'))"), valueFormat});
    m_rules.push_back(
        {QRegularExpression(R"(&[A-Za-z0-9#]+;)"), entityFormat});
    m_rules.push_back(
        {QRegularExpression(R"(<\?.*?\?>|<!DOCTYPE.*?>)",
                            QRegularExpression::CaseInsensitiveOption),
         processingFormat});
  }
};

}  // namespace SvgSourceEditor

#endif  // _WIN32
