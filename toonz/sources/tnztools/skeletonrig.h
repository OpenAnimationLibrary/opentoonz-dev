#pragma once

#include "toonz/trig.h"
#include <string>

class TStageObject;
class TXsheet;
class QWidget;

namespace SkeletonRig {
void edit(TXsheet *xsheet, TStageObject *object, QWidget *parent);
void recordEdit(TXsheet *xsheet, TStageObject *object, const TRigBone &before,
                const TRigBone &after, const std::string &oldName,
                const std::string &newName);
}  // namespace SkeletonRig
