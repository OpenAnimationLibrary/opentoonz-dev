#pragma once

#include "tfilepath.h"

// Converts a PDF selected through an ordinary resource-loading path into a
// persistent PNG level. Returns false when the user cancels or conversion
// fails.
bool convertPdfToPngLevel(const TFilePath &source, TFilePath &levelPath);
