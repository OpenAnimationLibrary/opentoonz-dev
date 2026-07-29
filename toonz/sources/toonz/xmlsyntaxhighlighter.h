#pragma once

#ifdef _WIN32

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QVector>

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
  bool m_enabled = true;

public:
  explicit XmlSyntaxHighlighter(QTextDocument *document)
      : QSyntaxHighlighter(document) {
    const bool dark =
        QGuiApplication::palette().color(QPalette::Base).lightness() < 128;

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

  void setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    rehighlight();
  }

protected:
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
};


}  // namespace SvgSourceEditor

#endif  // _WIN32
