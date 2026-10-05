#pragma once

#include "tcommon.h"
#include <QString>

#undef DVAPI
#ifdef TOONZQT_EXPORTS
#define DVAPI DV_EXPORT_API
#else
#define DVAPI DV_IMPORT_API
#endif

class TPalette;
class QWidget;

// Opens a non-destructive export dialog from a snapshot of the supplied
// palette.
DVAPI void openPaletteLutDialog(TPalette *palette, QWidget *parent);

// Shared helper invocation with cancellation and validation of generated
// output.
DVAPI bool generateLutFromImagePair(QWidget *parent, const QString &source,
                                    const QString &target,
                                    const QString &destination, int size = 33);
