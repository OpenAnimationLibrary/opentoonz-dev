

#include "edittool.h"
#include "tools/tool.h"
#include "tools/cursors.h"
#include "tproperty.h"
#include "toonz/stageobjectutil.h"
#include "tstroke.h"
#include "tgl.h"
#include "tenv.h"
#include "toonzqt/gutil.h"

#include "toonz/tstageobjecttree.h"
#include "toonz/tstageobjectspline.h"
#include "toonz/toonzscene.h"
#include "toonz/txshcolumn.h"
#include "toonz/stage.h"
#include "toonz/tcamera.h"

#include "tools/toolhandle.h"
#include "toonz/tcolumnhandle.h"
#include "toonz/tframehandle.h"
#include "toonz/tobjecthandle.h"
#include "toonz/txsheethandle.h"
#include "toonz/tscenehandle.h"
#include "toonz/tfxhandle.h"
#include "toonz/tstageobjectcmd.h"

#include "edittoolgadgets.h"

// For Qt translation support
#include <QCoreApplication>

#include <QDebug>
#include <QApplication>
#include <QDesktopWidget>

//=============================================================================
// Scale Constraints
//-----------------------------------------------------------------------------

namespace ScaleConstraints {
enum { None = 0, AspectRatio, Mass };
}

TEnv::IntVar LockCenterX("EditToolLockCenterX", 0);
TEnv::IntVar LockCenterY("EditToolLockCenterY", 0);
TEnv::IntVar LockPositionX("EditToolLockPositionX", 0);
TEnv::IntVar LockPositionY("EditToolLockPositionY", 0);
TEnv::IntVar LockRotation("EditToolLockRotation", 0);
TEnv::IntVar LockShearH("EditToolLockShearH", 0);
TEnv::IntVar LockShearV("EditToolLockShearV", 0);
TEnv::IntVar LockScaleH("EditToolLockScaleH", 0);
TEnv::IntVar LockScaleV("EditToolLockScaleV", 0);
TEnv::IntVar LockGlobalScale("EditToolLockGlobalScale", 0);

TEnv::IntVar ShowEWNSposition("EditToolShowEWNSposition", 1);
TEnv::IntVar ShowZposition("EditToolShowZposition", 1);
TEnv::IntVar ShowSOposition("EditToolShowSOposition", 1);
TEnv::IntVar ShowRotation("EditToolShowRotation", 1);
TEnv::IntVar ShowGlobalScale("EditToolShowGlobalScale", 1);
TEnv::IntVar ShowHVscale("EditToolShowHVscale", 1);
TEnv::IntVar ShowShear("EditToolShowShear", 0);
TEnv::IntVar ShowCenterPosition("EditToolShowCenterPosition", 0);
TEnv::StringVar Active("EditToolActiveAxis", "Position");

//=============================================================================
namespace {
//-----------------------------------------------------------------------------

using EditToolGadgets::DragTool;

// [NOTE: full file content continues with all original Drag* tools and EditTool methods, plus the keyDown method inserted after leftButtonUp as in the original PR #7034]

}  // truncated for this call - will fix
