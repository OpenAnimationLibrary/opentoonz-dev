#pragma once

#ifdef _WIN32

#include <QColor>
#include <QEvent>
#include <QFontDatabase>
#include <QPaintEvent>
#include <QPalette>
#include <QPainter>
#include <QPlainTextEdit>
#include <QResizeEvent>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextFormat>

#include <algorithm>

namespace SvgSourceEditor {

//=============================================================================
// SourceCodeEdit and line-number area
//-----------------------------------------------------------------------------

class SourceCodeEdit;

class LineNumberArea final : public QWidget {
  SourceCodeEdit *m_editor;

public:
  explicit LineNumberArea(SourceCodeEdit *editor);

  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *event) override;
};

class SourceCodeEdit final : public QPlainTextEdit {
  LineNumberArea *m_lineNumberArea;

public:
  explicit SourceCodeEdit(QWidget *parent = nullptr)
      : QPlainTextEdit(parent), m_lineNumberArea(new LineNumberArea(this)) {
    // These names are the stable QSS customization surface for
    // Preferences > Interface > Additional Style Sheet.
    setObjectName(QStringLiteral("SvgSourceEditorCode"));
    setProperty("svgSourceEditor", true);
    viewport()->setObjectName(QStringLiteral("SvgSourceEditorViewport"));

    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setTabStopDistance(
        fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 2.0);

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this](int) { updateLineNumberAreaWidth(); });
    connect(this, &QPlainTextEdit::updateRequest, this,
            [this](const QRect &rect, int dy) {
              if (dy)
                m_lineNumberArea->scroll(0, dy);
              else
                m_lineNumberArea->update(
                    0, rect.y(), m_lineNumberArea->width(), rect.height());

              if (rect.contains(viewport()->rect()))
                updateLineNumberAreaWidth();
            });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { highlightCurrentLine(); });

    updateLineNumberAreaWidth();
    highlightCurrentLine();
  }

  int lineNumberAreaWidth() const {
    int digits = 1;
    int maximum = std::max(1, blockCount());
    while (maximum >= 10) {
      maximum /= 10;
      ++digits;
    }
    return 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
  }

  void lineNumberAreaPaintEvent(QPaintEvent *event) {
    QPainter painter(m_lineNumberArea);
    const QPalette pal = m_lineNumberArea->palette();
    painter.fillRect(event->rect(), pal.color(QPalette::Window));

    QTextBlock block = firstVisibleBlock();
    int blockNumber = block.blockNumber();
    int top =
        qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());

    painter.setPen(pal.color(QPalette::WindowText));
    while (block.isValid() && top <= event->rect().bottom()) {
      if (block.isVisible() && bottom >= event->rect().top()) {
        painter.drawText(
            0, top, m_lineNumberArea->width() - 4, fontMetrics().height(),
            Qt::AlignRight, QString::number(blockNumber + 1));
      }
      block = block.next();
      top = bottom;
      bottom = top +
               (block.isValid() ? qRound(blockBoundingRect(block).height()) : 0);
      ++blockNumber;
    }
  }

protected:
  void resizeEvent(QResizeEvent *event) override {
    QPlainTextEdit::resizeEvent(event);
    const QRect contents = contentsRect();
    m_lineNumberArea->setGeometry(
        QRect(contents.left(), contents.top(), lineNumberAreaWidth(),
              contents.height()));
  }

  void changeEvent(QEvent *event) override {
    QPlainTextEdit::changeEvent(event);
    if (!event) return;

    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::StyleChange) {
      m_lineNumberArea->update();
      highlightCurrentLine();
    }
  }

private:
  void updateLineNumberAreaWidth() {
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
  }

  void highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> selections;
    if (!isReadOnly()) {
      QTextEdit::ExtraSelection selection;

      // Blend a small amount of the selection color into the editor base.
      // This stays legible for both the default white source canvas and a dark
      // canvas supplied through Additional Style Sheet.
      const QPalette pal = viewport()->palette();
      const QColor base = pal.color(QPalette::Base);
      const QColor accent = pal.color(QPalette::Highlight);
      auto blendChannel = [](int baseChannel, int accentChannel) {
        return (baseChannel * 88 + accentChannel * 12) / 100;
      };
      QColor lineColor(blendChannel(base.red(), accent.red()),
                       blendChannel(base.green(), accent.green()),
                       blendChannel(base.blue(), accent.blue()), base.alpha());

      selection.format.setBackground(lineColor);
      selection.format.setProperty(QTextFormat::FullWidthSelection, true);
      selection.cursor = textCursor();
      selection.cursor.clearSelection();
      selections.append(selection);
    }
    setExtraSelections(selections);
  }
};

inline LineNumberArea::LineNumberArea(SourceCodeEdit *editor)
    : QWidget(editor), m_editor(editor) {
  setObjectName(QStringLiteral("SvgSourceEditorLineNumbers"));
  setProperty("svgSourceEditorLineNumbers", true);
  setAttribute(Qt::WA_StyledBackground, true);
}

inline QSize LineNumberArea::sizeHint() const {
  return QSize(m_editor->lineNumberAreaWidth(), 0);
}

inline void LineNumberArea::paintEvent(QPaintEvent *event) {
  m_editor->lineNumberAreaPaintEvent(event);
}

}  // namespace SvgSourceEditor

#endif  // _WIN32
