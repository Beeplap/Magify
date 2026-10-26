#include "ipc.h"
#include "ui_overlay.h"
#include <process.h>

static HANDLE g_hStopEvent = NULL;
static HANDLE g_hPipe = INVALID_HANDLE_VALUE;
static HANDLE g_hThread = NULL;
static MagifyContext *g_ipcCtx = NULL;

BOOL Magify_IPC_SendFileList(const WCHAR **paths, int count) {
    if (!paths || count <= 0) return FALSE;

    // Check if pipe exists with a quick wait
    if (!WaitNamedPipeW(MAGIFY_PIPE_NAME, 45)) {
        return FALSE;
    }

    HANDLE hPipe = CreateFileW(
        MAGIFY_PIPE_NAME,
        GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hPipe == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    DWORD bytesWritten;
    // Send each path
    for (int i = 0; i < count; i++) {
        const WCHAR *path = paths[i];
        if (!path || wcslen(path) == 0) continue;
        if (path[0] == L'-') continue; // Skip flags

        uint32_t len = (uint32_t)wcslen(path);
        WriteFile(hPipe, &len, sizeof(uint32_t), &bytesWritten, NULL);
        WriteFile(hPipe, path, len * sizeof(WCHAR), &bytesWritten, NULL);
    }

    // Send zero length to terminate
    uint32_t term = 0;
    WriteFile(hPipe, &term, sizeof(uint32_t), &bytesWritten, NULL);
    FlushFileBuffers(hPipe);
    CloseHandle(hPipe);
    return TRUE;
}

BOOL Magify_IPC_SendPathsToPrimary(int argc, WCHAR **argv) {
    if (argc <= 1) return FALSE;
    return Magify_IPC_SendFileList((const WCHAR**)(argv + 1), argc - 1);
}

static unsigned __stdcall IPC_ServerWorker(void *arg) {
    (void)arg;

    while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
        g_hPipe = CreateNamedPipeW(
            MAGIFY_PIPE_NAME,
            PIPE_ACCESS_INBOUND,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            4096,
            4096,
            50,
            NULL
        );

        if (g_hPipe == INVALID_HANDLE_VALUE) {
            Sleep(20);
            continue;
        }

        BOOL connected = ConnectNamedPipe(g_hPipe, NULL) ? TRUE : (GetLastError() == ERROR_PIPE_CONNECTED);

        if (connected && g_ipcCtx) {
            DWORD bytesRead;
            while (WaitForSingleObject(g_hStopEvent, 0) == WAIT_TIMEOUT) {
                uint32_t len = 0;
                if (!ReadFile(g_hPipe, &len, sizeof(uint32_t), &bytesRead, NULL) || bytesRead == 0) {
                    break;
                }
                if (len == 0 || len >= MAX_PATH) break;

                WCHAR tempPath[MAX_PATH];
                if (!ReadFile(g_hPipe, tempPath, len * sizeof(WCHAR), &bytesRead, NULL) || bytesRead == 0) {
                    break;
                }
                tempPath[len] = L'\0';

                // Check for duplicate
                BOOL alreadyHas = FALSE;
                for (int i = 0; i < g_ipcCtx->fileCount; i++) {
                    if (_wcsicmp(g_ipcCtx->files[i], tempPath) == 0) {
                        alreadyHas = TRUE;
                        break;
                    }
                }

                if (!alreadyHas && g_ipcCtx->fileCount < MAGIFY_MAX_FILES) {
                    wcsncpy(g_ipcCtx->files[g_ipcCtx->fileCount], tempPath, MAX_PATH - 1);
                    g_ipcCtx->files[g_ipcCtx->fileCount][MAX_PATH - 1] = L'\0';
                    g_ipcCtx->totalSizeBytes += Magify_GetFileSize(tempPath);
                    g_ipcCtx->fileCount++;

                    // Trigger overlay update
                    if (g_ipcCtx->hwndOverlay && IsWindow(g_ipcCtx->hwndOverlay)) {
                        PostMessageW(g_ipcCtx->hwndOverlay, WM_USER + 101, 0, 0);
                    }
                }
            }
        }

        DisconnectNamedPipe(g_hPipe);
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;
    }

    return 0;
}

BOOL Magify_IPC_StartServer(MagifyContext *ctx) {
    g_ipcCtx = ctx;
    g_hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!g_hStopEvent) return FALSE;

    uintptr_t th = _beginthreadex(NULL, 0, IPC_ServerWorker, NULL, 0, NULL);
    if (!th) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
        return FALSE;
    }

    g_hThread = (HANDLE)th;
    return TRUE;
}

void Magify_IPC_StopServer(void) {
    if (g_hStopEvent) {
        SetEvent(g_hStopEvent);
    }
    if (g_hPipe != INVALID_HANDLE_VALUE) {
        // Cancel blocking operations
        CloseHandle(g_hPipe);
        g_hPipe = INVALID_HANDLE_VALUE;
    }
    if (g_hThread) {
        WaitForSingleObject(g_hThread, 200);
        CloseHandle(g_hThread);
        g_hThread = NULL;
    }
    if (g_hStopEvent) {
        CloseHandle(g_hStopEvent);
        g_hStopEvent = NULL;
    }
    g_ipcCtx = NULL;
}
