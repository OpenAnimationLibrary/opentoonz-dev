#pragma once

#include "tcommon.h"

#undef DVAPI
#ifdef TOONZQT_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class StageSchematicScene;

DVAPI void runConstraintSimulation(StageSchematicScene *scene);
