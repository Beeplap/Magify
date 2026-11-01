#ifndef MAGIFY_IPC_H
#define MAGIFY_IPC_H

#include "magify.h"

#define MAGIFY_WM_FILES (WM_APP + 105)

typedef struct {
    int count;
    WCHAR paths[MAGIFY_MAX_FILES][MAX_PATH];
} MagifyFileList;

BOOL Magify_IPC_SendPathsToPrimary(int argc, WCHAR **argv);
BOOL Magify_IPC_SendFileList(const WCHAR **paths, int count);
BOOL Magify_IPC_StartServer(MagifyContext *ctx);
void Magify_IPC_StopServer(void);
BOOL Magify_IPC_IsServerRunning(void);
/* Takes ownership of a MAGIFY_WM_FILES packet. Call only on the UI thread. */
BOOL Magify_IPC_ApplyFileList(MagifyContext *ctx, LPARAM packet);

#endif // MAGIFY_IPC_H
