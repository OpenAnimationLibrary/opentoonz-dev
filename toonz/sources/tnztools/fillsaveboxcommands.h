#pragma once

#ifndef FILL_SAVEBOX_COMMANDS_H
#define FILL_SAVEBOX_COMMANDS_H

class TTool;

namespace FillSaveboxCommands {
bool isEditMode(TTool *tool);
void setEditMode(TTool *tool, bool enabled);
bool fitToDrawing(TTool *tool);
}  // namespace FillSaveboxCommands

#endif  // FILL_SAVEBOX_COMMANDS_H
