

#pragma warning(disable : 4996)

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ttwain_state.h"
#include "ttwainP.h"
#include "ttwain_statePD.h"
#include "ttwain_util.h"

#ifdef __cplusplus
extern "C" {
#endif

static void *hDSMLib; /* handle of DSM */
extern void TTWAIN_SetState(TWAINSTATE status);

static void TTWAIN_DebugDSMPath(const char *path) {
  char message[_MAX_PATH + 64];
  snprintf(message, sizeof(message), "OpenToonz TWAIN: loading DSM %s\n", path);
  OutputDebugStringA(message);
}

int TTWAIN_LoadSourceManagerPD(void) {
  char systemDir[_MAX_PATH];
  char dsmPath[_MAX_PATH];
  UINT systemDirLength;

  if (TTWAIN_GetState() >= TWAIN_SM_LOADED)
    return TRUE; /* DSM already loaded */

  systemDirLength = GetSystemDirectoryA(systemDir, _MAX_PATH);
  if (!systemDirLength || systemDirLength >= _MAX_PATH) return FALSE;

  if (snprintf(dsmPath, sizeof(dsmPath), "%s\\%s", systemDir, DSM_FILENAME) >=
      (int)sizeof(dsmPath))
    return FALSE;

  TTWAIN_DebugDSMPath(dsmPath);
  hDSMLib = LoadLibraryA(dsmPath);

  if (hDSMLib) {
    TTwainData.DSM_Entry =
        (DSMENTRYPROC)GetProcAddress((HMODULE)hDSMLib, DSM_ENTRYPOINT);
    if (TTwainData.DSM_Entry) {
      TTWAIN_SetAvailable(AVAIABLE_YES);
      TTWAIN_SetState(TWAIN_SM_LOADED);
    } else {
      OutputDebugStringA(
          "OpenToonz TWAIN: DSM loaded but DSM_Entry was not found\n");
      FreeLibrary((HMODULE)hDSMLib);
      hDSMLib = NULL;
    }
  } else {
    char message[128];
    DWORD err = GetLastError();
    snprintf(message, sizeof(message),
             "OpenToonz TWAIN: LoadLibrary failed with Windows error %lu\n",
             (unsigned long)err);
    OutputDebugStringA(message);
    TTwainData.DSM_Entry = 0;
  }
  return (TTWAIN_GetState() >= TWAIN_SM_LOADED);
}
/*---------------------------------------------------------------------------*/
int TTWAIN_UnloadSourceManagerPD(void) {
  if (TTWAIN_GetState() == TWAIN_SM_LOADED) {
    if (hDSMLib) {
      FreeLibrary((HMODULE)hDSMLib);
      hDSMLib = NULL;
    }
    TTwainData.DSM_Entry = NULL;
    TTWAIN_SetState(TWAIN_PRESESSION);
  }
  return (TTWAIN_GetState() == TWAIN_PRESESSION);
}
/*---------------------------------------------------------------------------*/

#ifdef __cplusplus
}
#endif
