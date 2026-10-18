#pragma once

#include "trig.h"

class TOStream;
class TIStream;

DVAPI void saveRigBone(TOStream &os, const TRigBone &bone);
DVAPI TRigBone loadRigBone(TIStream &is);
