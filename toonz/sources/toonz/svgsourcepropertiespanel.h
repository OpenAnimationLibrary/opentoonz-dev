#pragma once

#ifdef _WIN32

#include "pane.h"
#include "sourceeditorbridge.h"
#include "tapp.h"

#include "toonz/txshlevelhandle.h"
#include "toonz/txshleveltypes.h"
#include "toonz/txshsimplelevel.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSize>
#include <QVBoxLayout>

namespace SvgSourceEditor {

//=============================================================================
// SvgSourcePropertiesWidget
//-----------------------------------------------------------------------------

// A deliberately small Quick Edit surface for the selected retained SVG Level.
// The physical SVG remains the document. This widget does not introduce an
// OpenToonz document model, tabs, Save As state, syntax services, or any other
// IDE-style ownership.
class SvgSourcePropertiesWidget final : public QWidget,
                                        public SourceEditorObserver {
  TXshSimpleLevelP m_level;
  TFilePath m_path;
  QByteArray m_loadedHash;

  QLineEdit *m_pathField;
  QLabel *m_statusLabel;
  QLabel *m_sizeLabel;
  QPlainTextEdit *m_sourceEdit;
  QPushButton *m_reloadButton;
  QPushButton *m_applyButton;
  QPushButton *m_vscodeButton;

  bool m_updatingEditor = false;
  bool m_dirty = false;
  bool m_externalChangePending = false;
  bool m_followSelectionPending = false;
  bool m_applying = false;
  int m_diskErrorLine = -1;
  int m_diskErrorColumn = -1;
  bool m_errorLocationOnDisk = false;
  QString m_cleanStatus;

public:
  explicit SvgSourcePropertiesWidget(QWidget *parent = nullptr)
      : QWidget(parent)
      , m_pathField(new QLineEdit(this))
      , m_statusLabel(new QLabel(this))
      , m_sizeLabel(new QLabel(this))
      , m_sourceEdit(new QPlainTextEdit(this))
      , m_reloadButton(new QPushButton(tr("Reload"), this))
      , m_applyButton(new QPushButton(tr("Apply"), this))
      , m_vscodeButton(new QPushButton(tr("Open in Visual Studio Code"), this)) {
    setObjectName(QStringLiteral("SvgSourcePropertiesWidget"));

    m_pathField->setReadOnly(true);
    m_pathField->setPlaceholderText(tr("No SVG Level selected"));

    m_statusLabel->setWordWrap(true);
    m_sizeLabel->setText(QStringLiteral("—"));

    m_sourceEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_sourceEdit->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_sourceEdit->setPlaceholderText(
        tr("Select an SVG Level to inspect or make a quick source edit."));

    auto *metadataLayout = new QFormLayout;
    metadataLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    metadataLayout->addRow(tr("Source:"), m_pathField);
    metadataLayout->addRow(tr("Status:"), m_statusLabel);
    metadataLayout->addRow(tr("Render Size:"), m_sizeLabel);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->addWidget(m_reloadButton);
    buttonLayout->addWidget(m_applyButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_vscodeButton);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addLayout(metadataLayout);
    layout->addWidget(new QLabel(tr("Quick Edit"), this));
    layout->addWidget(m_sourceEdit, 1);
    layout->addLayout(buttonLayout);

    connect(m_sourceEdit, &QPlainTextEdit::textChanged, this,
            [this]() { onTextChanged(); });
    connect(m_reloadButton, &QPushButton::clicked, this,
            [this]() { reloadClicked(); });
    connect(m_applyButton, &QPushButton::clicked, this,
            [this]() { applyClicked(); });
    connect(m_vscodeButton, &QPushButton::clicked, this,
            [this]() { openInVisualStudioCode(); });

    TXshLevelHandle *levelHandle = TApp::instance()->getCurrentLevel();
    connect(levelHandle, &TXshLevelHandle::xshLevelSwitched, this,
            [this](TXshLevel *) { followCurrentSelection(); });
    connect(levelHandle, &TXshLevelHandle::xshLevelChanged, this,
            [this]() { onCurrentLevelChanged(); });

    SourceEditorBridge::instance()->setObserver(this);
    followCurrentSelection(true);
  }

  ~SvgSourcePropertiesWidget() override {
    SourceEditorBridge::instance()->clearObserver(this);
  }

  void onSvgSourceFileChanged(const TFilePath &path, const QByteArray &source,
                              bool usable, const QString &message, int line,
                              int column,
                              const QSize &naturalSize) override {
    if (path != m_path || !m_level || m_applying) return;

    if (m_dirty) {
      m_externalChangePending = true;
      m_diskErrorLine = line;
      m_diskErrorColumn = column;
      m_errorLocationOnDisk = !usable && line > 0;
      m_statusLabel->setText(
          usable
              ? tr("The SVG source changed externally while Quick Edit has "
                   "unapplied changes. Reload to use the disk version, or "
                   "Apply to replace it.")
              : tr("The SVG source changed externally and is currently not "
                   "valid while Quick Edit has unapplied changes. Reload to "
                   "inspect the disk version, or Apply to replace it."));
      return;
    }

    setEditorSource(source);
    setValidationState(usable, message, line, column, naturalSize);
  }

private:
  void followCurrentSelection(bool force = false) {
    TXshSimpleLevel *current =
        TApp::instance()->getCurrentLevel()->getSimpleLevel();

    if (!force && m_dirty && current != m_level.getPointer()) {
      m_followSelectionPending = true;
      m_statusLabel->setText(
          tr("The level selection changed while Quick Edit has unapplied "
             "changes. Apply the current edit, or Reload to discard it and "
             "follow the selected level."));
      return;
    }

    m_followSelectionPending = false;

    if (!current || current->getType() != SVG_XSHLEVEL) {
      clearPanel(tr("Select an SVG Level to view its source properties."));
      return;
    }

    const TFilePath path = SvgLevel::sourcePathForLevel(current);
    m_level = current;
    m_path = path;

    if (path.isEmpty()) {
      clearEditorOnly(tr("The selected SVG Level has no source path."));
      return;
    }

    m_pathField->setText(QDir::toNativeSeparators(path.getQString()));
    SourceEditorBridge::instance()->bind(current, path);
    readSourceFromDisk();
  }

  void onCurrentLevelChanged() {
    TXshSimpleLevel *current =
        TApp::instance()->getCurrentLevel()->getSimpleLevel();
    if (current != m_level.getPointer()) {
      followCurrentSelection();
      return;
    }

    if (!m_level || m_path.isEmpty()) return;

    if (!m_dirty) {
      readSourceFromDisk();
      return;
    }

    const QByteArray diskHash = sourceHash(m_path);
    if (!diskHash.isEmpty() && diskHash != m_loadedHash) {
      m_externalChangePending = true;
      m_statusLabel->setText(
          tr("The SVG source changed on disk while Quick Edit has unapplied "
             "changes. Reload to use the disk version, or Apply to replace "
             "it."));
    }
  }

  void clearPanel(const QString &status) {
    m_level = TXshSimpleLevelP();
    m_path = TFilePath();
    m_loadedHash.clear();
    m_dirty = false;
    m_externalChangePending = false;
    m_diskErrorLine = -1;
    m_diskErrorColumn = -1;
    m_errorLocationOnDisk = false;
    m_cleanStatus = status;

    m_pathField->clear();
    m_sizeLabel->setText(QStringLiteral("—"));
    m_statusLabel->setText(status);

    m_updatingEditor = true;
    m_sourceEdit->clear();
    m_updatingEditor = false;

    m_sourceEdit->setEnabled(false);
    m_reloadButton->setEnabled(false);
    m_applyButton->setEnabled(false);
    m_vscodeButton->setEnabled(false);
  }

  void clearEditorOnly(const QString &status) {
    m_loadedHash.clear();
    m_dirty = false;
    m_externalChangePending = false;
    m_cleanStatus = status;
    m_statusLabel->setText(status);
    m_sizeLabel->setText(QStringLiteral("—"));

    m_updatingEditor = true;
    m_sourceEdit->clear();
    m_updatingEditor = false;

    m_sourceEdit->setEnabled(false);
    m_reloadButton->setEnabled(false);
    m_applyButton->setEnabled(false);
    m_vscodeButton->setEnabled(false);
  }

  void readSourceFromDisk() {
    if (!m_level || m_path.isEmpty()) return;

    QFile file(m_path.getQString());
    if (!file.open(QIODevice::ReadOnly)) {
      clearEditorOnly(tr("The SVG source file is not currently accessible."));
      m_pathField->setText(QDir::toNativeSeparators(m_path.getQString()));
      return;
    }

    const QByteArray source = file.readAll();
    QString error;
    int line = -1;
    int column = -1;
    QSize naturalSize;
    const bool usable = SvgLevel::validateSource(source, &error, &line, &column,
                                                 &naturalSize);

    setEditorSource(source);
    setValidationState(usable, error, line, column, naturalSize);

    m_sourceEdit->setEnabled(true);
    m_reloadButton->setEnabled(true);
    m_vscodeButton->setEnabled(true);
    SourceEditorBridge::instance()->bind(m_level.getPointer(), m_path);
  }

  void setEditorSource(const QByteArray &source) {
    m_updatingEditor = true;
    m_sourceEdit->setPlainText(QString::fromUtf8(source));
    m_updatingEditor = false;

    m_loadedHash = sourceHash(source);
    m_dirty = false;
    m_externalChangePending = false;
    m_applyButton->setEnabled(false);
  }

  void setValidationState(bool usable, const QString &message, int line,
                          int column, const QSize &naturalSize) {
    m_diskErrorLine = line;
    m_diskErrorColumn = column;
    m_errorLocationOnDisk = !usable && line > 0;

    if (naturalSize.isValid() && !naturalSize.isEmpty())
      m_sizeLabel->setText(
          tr("%1 × %2").arg(naturalSize.width()).arg(naturalSize.height()));
    else
      m_sizeLabel->setText(QStringLiteral("—"));

    if (usable) {
      m_cleanStatus =
          tr("Valid SVG source. Quick Edit applies directly to the retained "
             "source file and refreshes this level.");
    } else if (line > 0) {
      m_cleanStatus =
          tr("Source on disk is not currently valid (line %1, column %2): %3. "
             "The previous valid rendering is retained.")
              .arg(line)
              .arg(column)
              .arg(message);
    } else {
      m_cleanStatus =
          tr("The SVG source could not be applied: %1. The previous valid "
             "rendering is retained.")
              .arg(message);
    }

    m_statusLabel->setText(m_cleanStatus);
  }

  void onTextChanged() {
    if (m_updatingEditor || !m_level || m_path.isEmpty()) return;

    const QByteArray source = m_sourceEdit->toPlainText().toUtf8();
    m_dirty = sourceHash(source) != m_loadedHash;
    m_applyButton->setEnabled(m_dirty);

    if (m_externalChangePending) {
      m_statusLabel->setText(
          tr("The SVG source changed on disk while Quick Edit has unapplied "
             "changes. Reload to use the disk version, or Apply to replace "
             "it."));
    } else if (m_dirty) {
      m_statusLabel->setText(
          tr("Quick Edit modified. Apply to validate, save, and refresh the "
             "SVG Level."));
    } else {
      m_statusLabel->setText(m_cleanStatus);
    }
  }

  void reloadClicked() {
    if (!m_level && !m_followSelectionPending) {
      followCurrentSelection(true);
      return;
    }

    if (m_dirty) {
      const QMessageBox::StandardButton choice = QMessageBox::question(
          this, tr("Discard Quick Edit Changes?"),
          tr("Reloading will discard the unapplied SVG source changes in "
             "Quick Edit."),
          QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
      if (choice != QMessageBox::Discard) return;
    }

    m_dirty = false;
    m_externalChangePending = false;

    if (m_followSelectionPending) {
      followCurrentSelection(true);
      return;
    }

    readSourceFromDisk();
  }

  void applyClicked() {
    if (!m_level || m_path.isEmpty() || !m_dirty) return;

    if (m_externalChangePending) {
      const QMessageBox::StandardButton choice = QMessageBox::warning(
          this, tr("SVG Source Changed on Disk"),
          tr("The SVG source changed externally after Quick Edit began. "
             "Applying now will replace the current disk version with the "
             "Quick Edit text."),
          QMessageBox::Apply | QMessageBox::Cancel, QMessageBox::Cancel);
      if (choice != QMessageBox::Apply) return;
    }

    const QByteArray source = m_sourceEdit->toPlainText().toUtf8();
    QString error;
    int line = -1;
    int column = -1;
    QSize naturalSize;

    m_applying = true;
    const bool applied = SourceEditorBridge::instance()->applySource(
        m_level.getPointer(), source, &error, &line, &column, &naturalSize);
    m_applying = false;

    if (!applied) {
      m_errorLocationOnDisk = false;
      m_diskErrorLine = line;
      m_diskErrorColumn = column;
      if (line > 0)
        m_statusLabel->setText(
            tr("Not applied — line %1, column %2: %3")
                .arg(line)
                .arg(column)
                .arg(error));
      else
        m_statusLabel->setText(tr("Not applied — %1").arg(error));
      return;
    }

    m_loadedHash = sourceHash(source);
    m_dirty = false;
    m_externalChangePending = false;
    m_errorLocationOnDisk = false;
    m_applyButton->setEnabled(false);

    if (naturalSize.isValid() && !naturalSize.isEmpty())
      m_sizeLabel->setText(
          tr("%1 × %2").arg(naturalSize.width()).arg(naturalSize.height()));

    m_cleanStatus = tr("Valid SVG source — saved and refreshed.");
    m_statusLabel->setText(m_cleanStatus);

    if (m_followSelectionPending) followCurrentSelection(true);
  }

  void openInVisualStudioCode() {
    if (!m_level || m_path.isEmpty()) return;

    if (m_dirty) {
      const QMessageBox::StandardButton choice = QMessageBox::information(
          this, tr("Quick Edit Has Unapplied Changes"),
          tr("Visual Studio Code will open the saved SVG file. The current "
             "Quick Edit changes have not been written to that file."),
          QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel);
      if (choice != QMessageBox::Open) return;
    }

    const int line = m_errorLocationOnDisk ? m_diskErrorLine : -1;
    const int column = m_errorLocationOnDisk ? m_diskErrorColumn : -1;
    SourceEditorBridge::instance()->editLevelSource(m_level.getPointer(), line,
                                                    column);
  }
};

// Register this as a normal OpenToonz TPanel without adding another MOC/build
// target. The factory is function-local and therefore instantiated once when
// the experimental SVG source UI is activated.
inline void registerSvgSourcePropertiesPanelFactory() {
  class Factory final : public TPanelFactory {
  public:
    Factory() : TPanelFactory(kSourcePropertiesPanelType) {}

    void initialize(TPanel *panel) override {
      panel->setWindowTitle(QObject::tr("SVG Level Properties"));
      panel->allowMultipleInstances(false);
      panel->setWidget(new SvgSourcePropertiesWidget(panel));
      panel->resize(640, 520);
    }
  };

  static Factory factory;
  Q_UNUSED(factory);
}

}  // namespace SvgSourceEditor

#endif  // _WIN32
