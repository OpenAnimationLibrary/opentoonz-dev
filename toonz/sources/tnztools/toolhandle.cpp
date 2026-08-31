

#include "tools/toolhandle.h"
#include "toonz/stage2.h"
#include "tools/tool.h"
#include "tools/toolcommandids.h"
#include "timage.h"
// #include "tapp.h"
#include "toonzqt/menubarcommand.h"
#include "toonz/preferences.h"
#include "toonz/toonzfolders.h"
#include <QGuiApplication>
#include <QAction>
#include <QMap>
#include <QDebug>
#include <QSettings>
#include <QTimer>

namespace {

const char *kPersistCurrentToolKey = "PersistCurrentToolAcrossSessions";
const char *kLastUsedToolKey       = "LastUsedTool";

QString preferencesFilePath() {
  return (ToonzFolder::getMyModuleDir() + TFilePath("preferences.ini"))
      .getQString();
}

}  // namespace

//=============================================================================
// ToolHandle
//-----------------------------------------------------------------------------

ToolHandle::ToolHandle()
    : m_tool(0)
    , m_toolName("")
    , m_toolTargetType(TTool::NoTarget)
    , m_storedToolName("")
    , m_toolIsBusy(false)
    , m_suppressToolPersistence(false) {
  // Restore after application startup has completed its normal initial tool
  // selection. This also avoids relying on a clean shutdown to capture state.
  QTimer::singleShot(0, this, []() {
    if (!ToolHandle::isCurrentToolPersistenceEnabled()) return;

    const QString toolName = ToolHandle::getPersistedToolName();
    if (toolName.isEmpty()) return;

    const QByteArray toolId = toolName.toUtf8();
    QAction *action =
        CommandManager::instance()->getAction(toolId.constData(), false);
    if (action) CommandManager::instance()->execute(action);
  });
}

//-----------------------------------------------------------------------------

ToolHandle::~ToolHandle() {}

//-----------------------------------------------------------------------------

TTool *ToolHandle::getTool() const { return m_tool; }

//-----------------------------------------------------------------------------

bool ToolHandle::isCurrentToolPersistenceEnabled() {
  QSettings settings(preferencesFilePath(), QSettings::IniFormat);
  return settings.value(kPersistCurrentToolKey, false).toBool();
}

//-----------------------------------------------------------------------------

void ToolHandle::setCurrentToolPersistenceEnabled(bool enabled) {
  QSettings settings(preferencesFilePath(), QSettings::IniFormat);
  settings.setValue(kPersistCurrentToolKey, enabled ? 1 : 0);
  settings.sync();
}

//-----------------------------------------------------------------------------

QString ToolHandle::getPersistedToolName() {
  QSettings settings(preferencesFilePath(), QSettings::IniFormat);
  return settings.value(kLastUsedToolKey).toString();
}

//-----------------------------------------------------------------------------

void ToolHandle::setPersistedToolName(const QString &toolName) {
  if (toolName.isEmpty()) return;

  QSettings settings(preferencesFilePath(), QSettings::IniFormat);
  settings.setValue(kLastUsedToolKey, toolName);
  // Flush immediately so the selection survives crashes, freezes, forced
  // termination, and unexpected system shutdowns whenever the filesystem can
  // still accept the write.
  settings.sync();
}

//-----------------------------------------------------------------------------

void ToolHandle::setTool(QString name) {
  m_oldToolName = m_toolName = name;

  TTool *tool = TTool::getTool(m_toolName.toStdString(),
                               (TTool::ToolTargetType)m_toolTargetType);
  if (tool == m_tool) return;

  if (m_tool) m_tool->onDeactivate();

  // Camera test uses the automatically activated CameraTestTool
  if (name != "T_CameraTest" && CameraTestCheck::instance()->isEnabled())
    CameraTestCheck::instance()->setIsEnabled(false);

  m_tool = tool;

  if (name != T_Hand && CleanupPreviewCheck::instance()->isEnabled()) {
    // When using a tool, you have to exit from cleanup preview mode
    QAction *act = CommandManager::instance()->getAction("MI_CleanupPreview");
    if (act) CommandManager::instance()->execute(act);
  }

  if (m_tool)  // Should always enter
  {
    m_tool->onActivate();
    emit toolSwitched();

    if (!m_suppressToolPersistence && !isViewerNavigationToolSelected() &&
        name != "T_CameraTest" && isCurrentToolPersistenceEnabled()) {
      const QByteArray toolId = name.toUtf8();
      if (CommandManager::instance()->getAction(toolId.constData(), false))
        setPersistedToolName(name);
    }
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::storeTool() {
  // navigation tools will not be stored
  if (!isViewerNavigationToolSelected()) {
    m_storedToolName = m_toolName;
    m_storedToolTime.start();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::restoreTool() {
  // qDebug() << m_storedToolTime.elapsed();
  // navigation tools will always return to the stored one even if the key press
  // time is short
  if (m_storedToolName != m_toolName && m_storedToolName != "" &&
      (m_storedToolTime.elapsed() >
           Preferences::instance()->getTempToolSwitchTimer() ||
       isViewerNavigationToolSelected())) {
    setTool(m_storedToolName);
  }
}

//-----------------------------------------------------------------------------

bool ToolHandle::isViewerNavigationToolSelected() {
  return m_toolName == T_RotateView || m_toolName == T_ZoomView ||
         m_toolName == T_HandView;
}

//-----------------------------------------------------------------------------

void ToolHandle::setPseudoTool(QString name) {
  QString oldToolName       = m_oldToolName;
  m_suppressToolPersistence = true;
  setTool(name);
  m_suppressToolPersistence = false;
  m_oldToolName             = oldToolName;
}

//-----------------------------------------------------------------------------

void ToolHandle::unsetPseudoTool() {
  if (m_toolName != m_oldToolName) {
    m_suppressToolPersistence = true;
    setTool(m_oldToolName);
    m_suppressToolPersistence = false;
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::setToolBusy(bool value) {
  m_toolIsBusy = value;
  if (!m_toolIsBusy) emit toolEditingFinished();
}

//-----------------------------------------------------------------------------

QIcon currentIcon;

static QIcon getCurrentIcon() { return currentIcon; }

//-----------------------------------------------------------------------------

/*
void ToolHandle::changeTool(QAction* action)
{
}
*/

//-----------------------------------------------------------------------------

void ToolHandle::onImageChanged(TImage::Type imageType) {
  TTool::ToolTargetType targetType = TTool::EmptyTarget;

  switch (imageType) {
  case TImage::RASTER:
    targetType = TTool::RasterImage;
    break;
  case TImage::TOONZ_RASTER:
    targetType = TTool::ToonzImage;
    break;
  case TImage::MESH:
    targetType = TTool::MeshImage;
    break;
  case TImage::META:
    targetType = TTool::MetaImage;
    break;
  case TImage::VECTOR:
    targetType = TTool::VectorImage;
    break;
  default:
    targetType = TTool::EmptyTarget;
    break;
  }

  if (targetType != m_toolTargetType) {
    m_toolTargetType = targetType;
    setTool(m_toolName);
  }

  if (m_tool) {
    m_tool->updateMatrix();
    m_tool->onImageChanged();
  }
}

//-----------------------------------------------------------------------------

void ToolHandle::updateMatrix() {
  if (m_tool) m_tool->updateMatrix();
}
