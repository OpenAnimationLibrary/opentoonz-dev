#pragma once

#ifdef _WIN32

// Windows-only experimental Source Editor.
//
// The implementation is header-contained while the SVG Level feature is being
// validated. It is included by scriptconsolepanel.cpp so the experiment can be
// staged without changing the broad application source list. Once stabilized,
// these classes can move to ordinary source translation units.

#include "floatingpanelcommand.h"
#include "pane.h"
#include "sourcecodeedit.h"
#include "sourcetextdocument.h"
#include "xmlsyntaxhighlighter.h"
#include "tapp.h"

#include "toonz/toonzscene.h"
#include "toonz/tscenehandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"

#include "toonzqt/icongenerator.h"
#include "toonzqt/menubarcommand.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetList>

#include <cassert>

namespace SvgSourceEditor {

constexpr const char *kSourceEditorCommandId = "MI_OpenSourceEditor";

//=============================================================================
// SourceEditorPanel
//-----------------------------------------------------------------------------

class SourceEditorPanel final : public TPanel {
  SourceTextDocument m_document;
  SourceCodeEdit *m_editor = nullptr;
  XmlSyntaxHighlighter *m_highlighter = nullptr;
  QLabel *m_pathLabel = nullptr;
  QLabel *m_modeLabel = nullptr;
  QLabel *m_statusLabel = nullptr;
  QAction *m_followAction = nullptr;
  QWidget *m_findBar = nullptr;
  QLineEdit *m_findEdit = nullptr;
  QLineEdit *m_replaceEdit = nullptr;
  QCheckBox *m_caseSensitive = nullptr;
  bool m_settingText = false;

public:
  explicit SourceEditorPanel(QWidget *parent = nullptr)
      : TPanel(parent) {
    setPanelType("SourceEditor");
    setWindowTitle(QObject::tr("Source Editor"));
    setIsMaximizable(true);
    allowMultipleInstances(false);
    resize(900, 620);

    QWidget *body = new QWidget(this);
    QVBoxLayout *mainLayout = new QVBoxLayout(body);
    mainLayout->setContentsMargins(3, 3, 3, 3);
    mainLayout->setSpacing(3);

    QToolBar *toolbar = new QToolBar(body);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);

    QAction *newAction = toolbar->addAction(QObject::tr("New"));
    QAction *openAction = toolbar->addAction(QObject::tr("Open"));
    QAction *saveAction = toolbar->addAction(QObject::tr("Save"));
    QAction *saveAsAction = toolbar->addAction(QObject::tr("Save As"));
    toolbar->addSeparator();
    QAction *reloadAction = toolbar->addAction(QObject::tr("Reload"));
    QAction *validateAction = toolbar->addAction(QObject::tr("Validate"));
    QAction *externalAction =
        toolbar->addAction(QObject::tr("Open in Text Editor"));
    toolbar->addSeparator();
    m_followAction =
        toolbar->addAction(QObject::tr("Follow Current SVG Level"));
    m_followAction->setCheckable(true);
    m_followAction->setChecked(true);
    QAction *findAction = toolbar->addAction(QObject::tr("Find"));

    newAction->setShortcut(QKeySequence::New);
    openAction->setShortcut(QKeySequence::Open);
    saveAction->setShortcut(QKeySequence::Save);
    saveAsAction->setShortcut(QKeySequence::SaveAs);
    findAction->setShortcut(QKeySequence::Find);
    for (QAction *action :
         {newAction, openAction, saveAction, saveAsAction, findAction}) {
      action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
      body->addAction(action);
    }

    mainLayout->addWidget(toolbar);

    QHBoxLayout *documentInfoLayout = new QHBoxLayout();
    m_modeLabel = new QLabel(body);
    m_pathLabel = new QLabel(body);
    m_pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    documentInfoLayout->addWidget(m_modeLabel);
    documentInfoLayout->addWidget(m_pathLabel, 1);
    mainLayout->addLayout(documentInfoLayout);

    m_editor = new SourceCodeEdit(body);
    m_editor->setPlaceholderText(QObject::tr(
        "Select an SVG Level or open a text file to begin editing."));
    m_highlighter = new XmlSyntaxHighlighter(m_editor->document());
    mainLayout->addWidget(m_editor, 1);

    m_findBar = createFindBar(body);
    m_findBar->hide();
    mainLayout->addWidget(m_findBar);

    m_statusLabel = new QLabel(body);
    m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(m_statusLabel);

    setWidget(body);

    connect(newAction, &QAction::triggered, this,
            [this]() { newDocument(); });
    connect(openAction, &QAction::triggered, this,
            [this]() { openTextFile(); });
    connect(saveAction, &QAction::triggered, this,
            [this]() { saveDocument(); });
    connect(saveAsAction, &QAction::triggered, this,
            [this]() { saveDocumentAs(); });
    connect(reloadAction, &QAction::triggered, this,
            [this]() { reloadDocument(); });
    connect(validateAction, &QAction::triggered, this,
            [this]() { validateDocument(true); });
    connect(externalAction, &QAction::triggered, this,
            [this]() { openInExternalEditor(); });
    connect(m_followAction, &QAction::toggled, this,
            [this](bool enabled) { onFollowToggled(enabled); });
    connect(findAction, &QAction::triggered, this, [this]() {
      m_findBar->setVisible(!m_findBar->isVisible());
      if (m_findBar->isVisible()) {
        m_findEdit->setFocus();
        m_findEdit->selectAll();
      }
    });

    connect(m_editor->document(), &QTextDocument::modificationChanged, this,
            [this](bool modified) {
              if (m_settingText) return;
              m_document.setModified(modified);
              updateTitle();
            });
    connect(m_editor, &QPlainTextEdit::cursorPositionChanged, this,
            [this]() { updateCursorStatus(); });

    TXshLevelHandle *levelHandle = TApp::instance()->getCurrentLevel();
    connect(levelHandle, &TXshLevelHandle::xshLevelSwitched, this,
            [this](TXshLevel *) { followCurrentSvgLevel(); });
    connect(levelHandle, &TXshLevelHandle::xshLevelChanged, this,
            [this]() {
              if (m_followAction->isChecked() &&
                  !m_document.isModified())
                followCurrentSvgLevel();
            });
    connect(TApp::instance()->getCurrentScene(),
            &TSceneHandle::sceneSwitched, this,
            [this]() { onSceneSwitched(); });

    m_document.setExternalChangeCallback(
        [this](bool conflict) { onExternalFileChanged(conflict); });

    QString empty;
    m_document.newUntitled(empty);
    setEditorText(empty);
    updateDocumentUi();
    QTimer::singleShot(0, this, [this]() { followCurrentSvgLevel(); });
  }

protected:
  void closeEvent(QCloseEvent *event) override {
    if (resolveUnsavedChanges())
      TPanel::closeEvent(event);
    else
      event->ignore();
  }

private:
  QWidget *createFindBar(QWidget *parent) {
    QWidget *bar = new QWidget(parent);
    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);

    m_findEdit = new QLineEdit(bar);
    m_findEdit->setPlaceholderText(QObject::tr("Find"));
    m_replaceEdit = new QLineEdit(bar);
    m_replaceEdit->setPlaceholderText(QObject::tr("Replace"));
    m_caseSensitive = new QCheckBox(QObject::tr("Case Sensitive"), bar);
    QPushButton *previousButton =
        new QPushButton(QObject::tr("Previous"), bar);
    QPushButton *nextButton = new QPushButton(QObject::tr("Next"), bar);
    QPushButton *replaceButton = new QPushButton(QObject::tr("Replace"), bar);
    QPushButton *replaceAllButton =
        new QPushButton(QObject::tr("Replace All"), bar);
    QPushButton *closeButton = new QPushButton(QObject::tr("Close"), bar);

    layout->addWidget(m_findEdit, 1);
    layout->addWidget(m_replaceEdit, 1);
    layout->addWidget(m_caseSensitive);
    layout->addWidget(previousButton);
    layout->addWidget(nextButton);
    layout->addWidget(replaceButton);
    layout->addWidget(replaceAllButton);
    layout->addWidget(closeButton);

    connect(previousButton, &QPushButton::clicked, this,
            [this]() { findText(true); });
    connect(nextButton, &QPushButton::clicked, this,
            [this]() { findText(false); });
    connect(m_findEdit, &QLineEdit::returnPressed, this,
            [this]() { findText(false); });
    connect(replaceButton, &QPushButton::clicked, this,
            [this]() { replaceCurrent(); });
    connect(replaceAllButton, &QPushButton::clicked, this,
            [this]() { replaceAll(); });
    connect(closeButton, &QPushButton::clicked, bar, &QWidget::hide);

    return bar;
  }

  void setEditorText(const QString &text) {
    m_settingText = true;
    m_editor->setPlainText(text);
    m_editor->document()->setModified(false);
    m_document.setModified(false);
    m_settingText = false;
    updateDocumentUi();
  }

  bool resolveUnsavedChanges() {
    if (!m_document.isModified()) return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QObject::tr("Source Editor"));
    box.setText(QObject::tr("The source document has unsaved changes."));
    QPushButton *saveButton =
        box.addButton(QObject::tr("Save"), QMessageBox::AcceptRole);
    QPushButton *discardButton =
        box.addButton(QObject::tr("Discard"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton =
        box.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(saveButton);
    box.setEscapeButton(cancelButton);
    box.exec();

    if (box.clickedButton() == saveButton) return saveDocument();
    if (box.clickedButton() == discardButton) return true;
    return false;
  }

  void newDocument() {
    if (!resolveUnsavedChanges()) return;
    QString text;
    m_document.newUntitled(text);
    setEditorText(text);
    m_followAction->setChecked(false);
    setStatus(QObject::tr("New UTF-8 text document."));
  }

  void openTextFile() {
    if (!resolveUnsavedChanges()) return;

    QString startPath;
    if (!m_document.path().isEmpty())
      startPath = m_document.path().getParentDir().getQString();

    const QString fileName = QFileDialog::getOpenFileName(
        this, QObject::tr("Open Text File"), startPath,
        QObject::tr(
            "Source Files (*.svg *.xml *.html *.htm *.css *.js *.json *.txt);;"
            "SVG/XML Files (*.svg *.xml);;"
            "Web Source Files (*.html *.htm *.css *.js);;"
            "Text Files (*.txt);;All Files (*.*)"));
    if (fileName.isEmpty()) return;

    QString text;
    QString error;
    if (!m_document.openStandalone(TFilePath(fileName), text, error)) {
      showError(error);
      return;
    }

    setEditorText(text);
    m_followAction->setChecked(false);
    setStatus(QObject::tr("Text file loaded."));
  }

  bool saveDocument() {
    if (m_document.path().isEmpty()) return saveDocumentAs();

    QString error;
    if (!m_document.save(m_editor->toPlainText(), error)) {
      showError(error);
      return false;
    }

    m_editor->document()->setModified(false);
    m_document.setModified(false);
    notifySvgRefresh(false);
    updateDocumentUi();
    setStatus(QObject::tr("Saved and validated."));
    return true;
  }

  bool saveDocumentAs() {
    QString startPath;
    if (!m_document.path().isEmpty())
      startPath = m_document.path().getQString();

    bool relinkSvg = false;
    bool saveCopy = false;
    if (m_document.isSvgBound()) {
      QMessageBox choice(this);
      choice.setIcon(QMessageBox::Question);
      choice.setWindowTitle(QObject::tr("Save SVG Source As"));
      choice.setText(QObject::tr(
          "Should the SVG Level use the new file, or should OpenToonz save "
          "only a separate copy?"));
      QPushButton *copyButton =
          choice.addButton(QObject::tr("Save Copy"), QMessageBox::ActionRole);
      QPushButton *relinkButton = choice.addButton(
          QObject::tr("Save and Relink SVG Level"), QMessageBox::AcceptRole);
      QPushButton *cancelButton =
          choice.addButton(QObject::tr("Cancel"), QMessageBox::RejectRole);
      choice.setDefaultButton(relinkButton);
      choice.setEscapeButton(cancelButton);
      choice.exec();

      if (choice.clickedButton() == cancelButton) return false;
      relinkSvg = choice.clickedButton() == relinkButton;
      saveCopy = choice.clickedButton() == copyButton;
    }

    const QString fileName = QFileDialog::getSaveFileName(
        this, QObject::tr("Save Text File"), startPath,
        QObject::tr(
            "SVG Files (*.svg);;XML Files (*.xml);;Text Files (*.txt);;"
            "All Files (*.*)"));
    if (fileName.isEmpty()) return false;

    TFilePath targetPath(fileName);
    if (m_document.isSvgBound()) {
      if (targetPath.getType().empty())
        targetPath = targetPath.withType("svg");
      else if (targetPath.getType() != "svg") {
        showError(QObject::tr(
            "SVG Level source copies and relink targets must use the .svg "
            "extension."));
        return false;
      }
    }

    QString error;
    if (!m_document.saveAs(m_editor->toPlainText(), targetPath,
                           relinkSvg, error)) {
      showError(error);
      return false;
    }

    if (saveCopy) {
      QString copyText;
      QString reopenError;
      if (!m_document.openStandalone(targetPath, copyText,
                                     reopenError)) {
        showError(reopenError);
        return false;
      }
      setEditorText(copyText);
      QSignalBlocker blocker(m_followAction);
      m_followAction->setChecked(false);
      setStatus(QObject::tr(
          "Copy saved and opened as a standalone text file. The SVG Level "
          "remains linked to its original source."));
      return true;
    }

    m_editor->document()->setModified(false);
    m_document.setModified(false);
    notifySvgRefresh(relinkSvg);
    updateDocumentUi();
    setStatus(relinkSvg ? QObject::tr("Saved and relinked SVG Level.")
                        : QObject::tr("Saved."));
    return true;
  }

  void reloadDocument() {
    if (m_document.path().isEmpty()) return;
    if (m_document.isModified() && !confirmDiscardForReload()) return;

    QString text;
    QString error;
    if (!m_document.reload(text, error)) {
      if (!text.isNull()) setEditorText(text);
      showError(error);
      setStatus(QObject::tr(
          "Source loaded from disk, but the previous valid SVG display was "
          "kept because the new source is not renderable."));
      return;
    }

    setEditorText(text);
    notifySvgRefresh(false);
    setStatus(QObject::tr("Reloaded from disk."));
  }

  bool validateDocument(bool showSuccess) {
    QString error;
    int line = -1;
    int column = -1;
    if (!m_document.validate(m_editor->toPlainText(), error, &line, &column)) {
      if (line > 0) {
        QTextCursor cursor(m_editor->document()->findBlockByLineNumber(line - 1));
        if (column > 0)
          cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                              column - 1);
        m_editor->setTextCursor(cursor);
        m_editor->setFocus();
      }
      showError(line > 0
                    ? QObject::tr("Line %1, column %2: %3")
                          .arg(line)
                          .arg(column)
                          .arg(error)
                    : error);
      setStatus(QObject::tr("Validation failed."));
      return false;
    }

    if (showSuccess)
      QMessageBox::information(this, QObject::tr("Source Editor"),
                               QObject::tr("The source is valid."));
    setStatus(QObject::tr("Valid source."));
    return true;
  }

  void openInExternalEditor() {
    if (m_document.path().isEmpty()) return;
    const QString nativePath =
        QDir::toNativeSeparators(m_document.path().getQString());
    if (!QProcess::startDetached(QStringLiteral("notepad.exe"),
                                 QStringList() << nativePath)) {
      if (!QDesktopServices::openUrl(
              QUrl::fromLocalFile(m_document.path().getQString())))
        showError(QObject::tr("Unable to open the source in an external editor."));
    }
  }

  void onFollowToggled(bool enabled) {
    if (!enabled) {
      setStatus(QObject::tr("Source Editor is pinned to the current document."));
      return;
    }

    if (m_document.isModified() && !resolveUnsavedChanges()) {
      QSignalBlocker blocker(m_followAction);
      m_followAction->setChecked(false);
      return;
    }
    followCurrentSvgLevel();
  }

  void followCurrentSvgLevel() {
    if (!m_followAction->isChecked()) return;

    TXshSimpleLevel *level =
        TApp::instance()->getCurrentLevel()->getSimpleLevel();
    if (!level || level->getType() != SVG_XSHLEVEL) {
      setStatus(QObject::tr("Select an SVG Level to follow its source."));
      return;
    }

    if (m_document.isModified()) {
      QSignalBlocker blocker(m_followAction);
      m_followAction->setChecked(false);
      setStatus(QObject::tr(
          "Follow disabled because the current source has unsaved changes."));
      return;
    }

    if (m_document.boundLevel() == level) return;

    QString text;
    QString error;
    if (!m_document.bindSvgLevel(level, text, error)) {
      showError(error);
      return;
    }

    setEditorText(text);
    setStatus(QObject::tr("Following the current SVG Level."));
  }

  void onSceneSwitched() {
    const QString preserved = m_editor->toPlainText();
    const bool preserveText = m_document.isModified();

    QString empty;
    m_document.newUntitled(empty);
    setEditorText(preserveText ? preserved : empty);
    if (preserveText) {
      m_editor->document()->setModified(true);
      m_document.setModified(true);
      QSignalBlocker blocker(m_followAction);
      m_followAction->setChecked(false);
      setStatus(QObject::tr(
          "Unsaved text was detached from the prior scene as an untitled "
          "document."));
    } else {
      QTimer::singleShot(0, this, [this]() { followCurrentSvgLevel(); });
    }
  }

  void onExternalFileChanged(bool conflict) {
    if (conflict) {
      QMessageBox box(this);
      box.setIcon(QMessageBox::Warning);
      box.setWindowTitle(QObject::tr("Source Changed on Disk"));
      box.setText(QObject::tr(
          "The source changed on disk while this editor has unsaved changes."));
      QPushButton *reloadButton =
          box.addButton(QObject::tr("Reload from Disk"),
                        QMessageBox::DestructiveRole);
      QPushButton *keepButton =
          box.addButton(QObject::tr("Keep My Changes"), QMessageBox::AcceptRole);
      QPushButton *saveAsButton =
          box.addButton(QObject::tr("Save As"), QMessageBox::ActionRole);
      box.setDefaultButton(keepButton);
      box.exec();

      if (box.clickedButton() == reloadButton) {
        m_editor->document()->setModified(false);
        m_document.setModified(false);
        reloadDocument();
      } else if (box.clickedButton() == saveAsButton) {
        saveDocumentAs();
      } else {
        setStatus(QObject::tr(
            "External change ignored; local unsaved text is still active."));
      }
      return;
    }

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QObject::tr("Source Changed on Disk"));
    box.setText(QObject::tr("The source file changed outside OpenToonz."));
    QPushButton *reloadButton =
        box.addButton(QObject::tr("Reload"), QMessageBox::AcceptRole);
    QPushButton *ignoreButton =
        box.addButton(QObject::tr("Ignore"), QMessageBox::RejectRole);
    box.setDefaultButton(reloadButton);
    box.exec();

    if (box.clickedButton() == reloadButton)
      reloadDocument();
    else
      setStatus(QObject::tr("External change ignored."));
  }

  bool confirmDiscardForReload() {
    return QMessageBox::question(
               this, QObject::tr("Reload Source"),
               QObject::tr(
                   "Reloading will discard the unsaved text in this editor."),
               QMessageBox::Discard | QMessageBox::Cancel,
               QMessageBox::Cancel) == QMessageBox::Discard;
  }

  void notifySvgRefresh(bool scenePathChanged) {
    TXshSimpleLevel *level = m_document.boundLevel();
    if (!level) return;

    const TFrameId fid(1);
    IconGenerator::instance()->invalidate(level, fid);
    TApp::instance()->getCurrentLevel()->notifyLevelChange();
    TApp::instance()->getCurrentXsheet()->notifyXsheetChanged();
    TApp::instance()->getCurrentScene()->notifySceneChanged(scenePathChanged);
  }

  void updateDocumentUi() {
    const QString modeText =
        m_document.mode() == SourceTextDocument::Mode::SvgLevelSource
            ? QObject::tr("SVG Level Source")
            : m_document.mode() == SourceTextDocument::Mode::StandaloneFile
                  ? QObject::tr("Text File")
                  : QObject::tr("Untitled");

    m_modeLabel->setText(modeText + QStringLiteral(":"));
    m_pathLabel->setText(
        m_document.path().isEmpty()
            ? QObject::tr("No file")
            : QDir::toNativeSeparators(m_document.path().getQString()));

    const QString extension =
        QString::fromStdString(m_document.path().getType()).toLower();
    m_highlighter->setEnabled(
        m_document.isSvgBound() || extension == QStringLiteral("svg") ||
        extension == QStringLiteral("xml") ||
        extension == QStringLiteral("html") ||
        extension == QStringLiteral("htm"));

    updateTitle();
    updateCursorStatus();
  }

  void updateTitle() {
    QString title =
        QObject::tr("Source Editor — %1").arg(m_document.displayName());
    if (m_document.isModified()) title += QStringLiteral(" *");
    setWindowTitle(title);
  }

  void updateCursorStatus() {
    const QTextCursor cursor = m_editor->textCursor();
    const QString position =
        QObject::tr("Ln %1, Col %2")
            .arg(cursor.blockNumber() + 1)
            .arg(cursor.positionInBlock() + 1);
    const QString format =
        m_document.encodingLabel() + QStringLiteral("    ") +
        m_document.lineEndingLabel();
    const QString previous = m_statusLabel->property("message").toString();
    const QString details = format + QStringLiteral("    ") + position;
    m_statusLabel->setText(previous.isEmpty()
                               ? details
                               : previous + QStringLiteral("    ") + details);
  }

  void setStatus(const QString &message) {
    m_statusLabel->setProperty("message", message);
    updateCursorStatus();
  }

  void showError(const QString &error) {
    QMessageBox::warning(this, QObject::tr("Source Editor"), error);
  }

  QTextDocument::FindFlags findFlags(bool backwards = false) const {
    QTextDocument::FindFlags flags;
    if (backwards) flags |= QTextDocument::FindBackward;
    if (m_caseSensitive->isChecked())
      flags |= QTextDocument::FindCaseSensitively;
    return flags;
  }

  bool findText(bool backwards) {
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) return false;

    if (m_editor->find(needle, findFlags(backwards))) return true;

    QTextCursor cursor = m_editor->textCursor();
    cursor.movePosition(backwards ? QTextCursor::End : QTextCursor::Start);
    m_editor->setTextCursor(cursor);
    return m_editor->find(needle, findFlags(backwards));
  }

  void replaceCurrent() {
    QTextCursor cursor = m_editor->textCursor();
    const QString selected = cursor.selectedText();
    const Qt::CaseSensitivity sensitivity =
        m_caseSensitive->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (selected.compare(m_findEdit->text(), sensitivity) == 0)
      cursor.insertText(m_replaceEdit->text());
    findText(false);
  }

  void replaceAll() {
    const QString needle = m_findEdit->text();
    if (needle.isEmpty()) return;

    QTextCursor editCursor(m_editor->document());
    editCursor.beginEditBlock();

    QTextDocument::FindFlags flags = findFlags(false);
    QTextCursor match = m_editor->document()->find(
        needle, QTextCursor(m_editor->document()), flags);
    int count = 0;
    while (!match.isNull()) {
      match.insertText(m_replaceEdit->text());
      ++count;
      match = m_editor->document()->find(needle, match, flags);
    }

    editCursor.endEditBlock();
    setStatus(QObject::tr("Replaced %1 occurrence(s).").arg(count));
  }
};

//=============================================================================
// Panel factory and command registration
//-----------------------------------------------------------------------------

class SourceEditorPanelFactory final : public TPanelFactory {
public:
  SourceEditorPanelFactory() : TPanelFactory("SourceEditor") {}

  TPanel *createPanel(QWidget *parent) override {
    SourceEditorPanel *panel = new SourceEditorPanel(parent);
    panel->setObjectName(getPanelType());
    return panel;
  }

  void initialize(TPanel *) override { assert(false); }
};

static SourceEditorPanelFactory sourceEditorPanelFactory;

inline void addSourceEditorToWindowsMenus(QAction *action) {
  if (!action) return;

  QString windowsTitle = QObject::tr("Windows");
  windowsTitle.remove(QLatin1Char('&'));
  QString stackedWindowsTitle =
      QCoreApplication::translate("StackedMenuBar", "Windows");
  stackedWindowsTitle.remove(QLatin1Char('&'));

  const QWidgetList topLevels = QApplication::topLevelWidgets();
  for (QWidget *topLevel : topLevels) {
    const QList<QMenu *> menus = topLevel->findChildren<QMenu *>();
    for (QMenu *menu : menus) {
      QString title = menu->title();
      title.remove(QLatin1Char('&'));
      const bool isWindowsMenu =
          title.compare(windowsTitle, Qt::CaseInsensitive) == 0 ||
          title.compare(stackedWindowsTitle, Qt::CaseInsensitive) == 0 ||
          title.compare(QStringLiteral("Windows"), Qt::CaseInsensitive) == 0;
      if (!isWindowsMenu) continue;
      if (!menu->actions().contains(action)) {
        menu->addSeparator();
        menu->addAction(action);
      }
    }
  }
}

inline void registerSourceEditorCommand() {
  if (CommandManager::instance()->getAction(kSourceEditorCommandId, false))
    return;

  QAction *action = new DVAction(QObject::tr("&Source Editor"), qApp);
  CommandManager::instance()->define(
      kSourceEditorCommandId, MenuWindowsCommandType, "Ctrl+Alt+E", action, "");

  static OpenFloatingPanel *openCommand =
      new OpenFloatingPanel(kSourceEditorCommandId, "SourceEditor",
                            QObject::tr("Source Editor"));
  Q_UNUSED(openCommand);

  for (int delay : {0, 1500, 5000}) {
    QTimer::singleShot(delay, qApp, [action]() {
      addSourceEditorToWindowsMenus(action);
    });
  }
}


}  // namespace SvgSourceEditor

inline void registerSvgSourceEditorStartup() {
  SvgSourceEditor::registerSourceEditorCommand();
}

Q_COREAPP_STARTUP_FUNCTION(registerSvgSourceEditorStartup)

#endif  // _WIN32
