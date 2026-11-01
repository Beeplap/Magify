#include "ipc.h"
#include <process.h>
#include <stdlib.h>

static HANDLE g_stop = NULL;
static HANDLE g_thread = NULL;
static HANDLE g_owner = NULL;
static HANDLE g_pipe = INVALID_HANDLE_VALUE;
static DWORD g_mainThreadId = 0;
static HWND g_receiver = NULL;

BOOL Magify_IPC_IsServerRunning(void) {
    if (WaitNamedPipeW(MAGIFY_PIPE_NAME, 0)) return TRUE;
    return GetLastError() == ERROR_SEM_TIMEOUT;
}

static BOOL WriteExact(HANDLE pipe, const void *data, DWORD size) {
    DWORD written = 0;
    return WriteFile(pipe, data, size, &written, NULL) && written == size;
}

BOOL Magify_IPC_SendFileList(const WCHAR **paths, int count) {
    if (!paths || count <= 0 || count > MAGIFY_MAX_FILES) return FALSE;
    for (int i = 0; i < count; ++i) {
        if (!paths[i] || !paths[i][0] || wcslen(paths[i]) >= MAX_PATH) return FALSE;
    }
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int retry = 0; retry < 40; ++retry) {
        pipe = CreateFileW(MAGIFY_PIPE_NAME, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_PIPE_BUSY) return FALSE;
        WaitNamedPipeW(MAGIFY_PIPE_NAME, 50);
    }
    if (pipe == INVALID_HANDLE_VALUE) return FALSE;

    /* Transfer the Explorer-launched client's foreground activation rights. */
    ULONG serverPid = 0;
    if (GetNamedPipeServerProcessId(pipe, &serverPid)) AllowSetForegroundWindow(serverPid);
    BOOL ok = TRUE;
    for (int i = 0; ok && i < count; ++i) {
        uint32_t len = (uint32_t)wcslen(paths[i]);
        ok = WriteExact(pipe, &len, sizeof(len)) &&
             WriteExact(pipe, paths[i], len * sizeof(WCHAR));
    }
    uint32_t term = 0;
    if (ok) ok = WriteExact(pipe, &term, sizeof(term));
    DWORD ack = 0, received = 0;
    if (ok) ok = ReadFile(pipe, &ack, sizeof(ack), &received, NULL) &&
                 received == sizeof(ack) && ack == 1;
    // Let the server know the acknowledgment was consumed before disconnecting.
    if (ok) ok = WriteExact(pipe, &term, sizeof(term));
    CloseHandle(pipe);
    return ok;
}

BOOL Magify_IPC_SendPathsToPrimary(int argc, WCHAR **argv) {
    return argc > 1 && Magify_IPC_SendFileList((const WCHAR**)(argv + 1), argc - 1);
}

/* Every pending operation is cancellable, including incomplete client packets. */
static BOOL PipeIO(HANDLE pipe, HANDLE event, void *data, DWORD size, BOOL write) {
    OVERLAPPED ov = {0};
    ov.hEvent = event;
    ResetEvent(event);
    DWORD transferred = 0;
    BOOL ok = write ? WriteFile(pipe, data, size, &transferred, &ov)
                    : ReadFile(pipe, data, size, &transferred, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        HANDLE waits[] = {g_stop, event};
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
            CancelIoEx(pipe, &ov);
            GetOverlappedResult(pipe, &ov, &transferred, TRUE);
            return FALSE;
        }
        ok = GetOverlappedResult(pipe, &ov, &transferred, FALSE);
    }
    return ok && transferred == size;
}

static unsigned __stdcall IPC_ServerWorker(void *arg) {
    (void)arg;
    HANDLE event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!event) return 1;
    while (WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT) {
        OVERLAPPED ov = {0};
        ov.hEvent = event;
        ResetEvent(event);
        BOOL connected = ConnectNamedPipe(g_pipe, &ov);
        DWORD error = connected ? ERROR_SUCCESS : GetLastError();
        if (error == ERROR_IO_PENDING) {
            HANDLE waits[] = {g_stop, event};
            if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0 + 1) {
                DWORD unused;
                CancelIoEx(g_pipe, &ov);
                GetOverlappedResult(g_pipe, &ov, &unused, TRUE);
                break;
            }
            DWORD unused;
            connected = GetOverlappedResult(g_pipe, &ov, &unused, FALSE);
        } else if (error == ERROR_PIPE_CONNECTED) {
            connected = TRUE;
        }
        if (connected) {
            MagifyFileList *list = calloc(1, sizeof(*list));
            BOOL complete = FALSE;
            while (list && WaitForSingleObject(g_stop, 0) == WAIT_TIMEOUT) {
                uint32_t len = 0;
                if (!PipeIO(g_pipe, event, &len, sizeof(len), FALSE)) break;
                if (!len) { complete = list->count > 0; break; }
                if (len >= MAX_PATH || list->count == MAGIFY_MAX_FILES) break;
                WCHAR *path = list->paths[list->count];
                if (!PipeIO(g_pipe, event, path, len * sizeof(WCHAR), FALSE)) break;
                if (wmemchr(path, L'\0', len)) break;
                path[len] = L'\0';
                ++list->count;
            }
            DWORD ack = 0;
            BOOL posted = complete && (g_receiver
                ? PostMessageW(g_receiver, MAGIFY_WM_FILES, 0, (LPARAM)list)
                : PostThreadMessageW(g_mainThreadId, MAGIFY_WM_FILES, 0, (LPARAM)list));
            if (posted) {
                ack = 1;
                list = NULL; /* The UI thread owns this immutable packet now. */
            }
            free(list);
            if (PipeIO(g_pipe, event, &ack, sizeof(ack), TRUE) && ack) {
                DWORD receipt;
                PipeIO(g_pipe, event, &receipt, sizeof(receipt), FALSE);
            }
        }
        DisconnectNamedPipe(g_pipe);
    }
    CloseHandle(event);
    return 0;
}

BOOL Magify_IPC_ApplyFileList(MagifyContext *ctx, LPARAM packet) {
    MagifyFileList *list = (MagifyFileList*)packet;
    if (!list) return FALSE;
    BOOL changed = FALSE;
    /* Never change the inputs of a running operation or an open editor. */
    if (ctx && !InterlockedCompareExchange(&ctx->isProcessing, 0, 0) &&
        (!ctx->hWorkerThread || WaitForSingleObject(ctx->hWorkerThread, 0) == WAIT_OBJECT_0) &&
        (!ctx->hwndOverlay || ctx->uiState <= UI_STATE_RADIAL_SUB || ctx->isClosing)) {
        if (!ctx->hwndOverlay || ctx->isClosing) {
            ctx->fileCount = 0;
            ctx->totalSizeBytes = 0;
            ctx->uiState = UI_STATE_RADIAL_ROOT;
            ctx->activeSubmenu = SUBMENU_NONE;
            ctx->isSuccessState = FALSE;
            ctx->isErrorState = FALSE;
            ctx->hubExpandAnim = 0.0f;
        }
        for (int n = 0; n < list->count; ++n) {
            BOOL duplicate = FALSE;
            for (int i = 0; i < ctx->fileCount; ++i)
                if (_wcsicmp(ctx->files[i], list->paths[n]) == 0) duplicate = TRUE;
            if (!duplicate && ctx->fileCount < MAGIFY_MAX_FILES) {
                wcscpy(ctx->files[ctx->fileCount++], list->paths[n]);
                ctx->totalSizeBytes += Magify_GetFileSize(list->paths[n]);
            }
        }
        if (ctx->fileCount) ctx->primaryClass = Magify_DetectFileClass(ctx->files[0]);
        changed = ctx->fileCount > 0;
    }
    free(list);
    return changed;
}

BOOL Magify_IPC_StartServer(MagifyContext *ctx) {
    if (g_thread) return FALSE;
    g_owner = CreateMutexW(NULL, FALSE, L"Local\\MagifySessionOwner");
    if (!g_owner) return FALSE;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_owner); g_owner = NULL;
        return FALSE;
    }
    /* PostThreadMessage requires a queue, even before the first window exists. */
    MSG msg;
    PeekMessageW(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);
    g_mainThreadId = GetCurrentThreadId();
    g_receiver = ctx ? ctx->hwndDispatcher : NULL;
    g_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_pipe = CreateNamedPipeW(MAGIFY_PIPE_NAME,
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, 4096, 4096, 50, NULL);
    if (g_stop && g_pipe != INVALID_HANDLE_VALUE)
        g_thread = (HANDLE)_beginthreadex(NULL, 0, IPC_ServerWorker, NULL, 0, NULL);
    if (!g_thread) { Magify_IPC_StopServer(); return FALSE; }
    return TRUE;
}

void Magify_IPC_StopServer(void) {
    if (g_stop) SetEvent(g_stop);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread); g_thread = NULL;
    }
    if (g_pipe != INVALID_HANDLE_VALUE) { CloseHandle(g_pipe); g_pipe = INVALID_HANDLE_VALUE; }
    if (g_stop) { CloseHandle(g_stop); g_stop = NULL; }
    if (g_owner) { CloseHandle(g_owner); g_owner = NULL; }
    MSG msg;
    while (PeekMessageW(&msg, g_receiver, MAGIFY_WM_FILES, MAGIFY_WM_FILES, PM_REMOVE))
        free((void*)msg.lParam);
    g_receiver = NULL;
}
