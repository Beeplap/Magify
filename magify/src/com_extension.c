#include "com_extension.h"
#include "magify.h"
#include "ipc.h"
#include <stdio.h>
#include <shlwapi.h>

// CLSID_MagifyExplorerCommand: {A7B8C9D0-1234-5678-9ABC-DEF012345678}
const CLSID CLSID_MagifyExplorerCommand = {
    0xA7B8C9D0, 0x1234, 0x5678,
    { 0x9A, 0xBC, 0xDE, 0xF0, 0x12, 0x34, 0x56, 0x78 }
};

static volatile LONG g_serverLockCount = 0;
static volatile LONG g_activeObjectsCount = 0;
static volatile LONG g_hasInvoked = 0;

typedef struct {
    IExplorerCommandVtbl *lpVtbl;
    volatile LONG refCount;
} MagifyExplorerCommand;

typedef struct {
    IClassFactoryVtbl *lpVtbl;
    volatile LONG refCount;
} MagifyClassFactory;

static HRESULT AllocCoTaskString(const WCHAR *src, LPWSTR *ppszOut) {
    if (!ppszOut) return E_POINTER;
    *ppszOut = NULL;
    if (!src) return S_OK;

    size_t len = wcslen(src);
    size_t bytes = (len + 1) * sizeof(WCHAR);
    WCHAR *buf = (WCHAR*)CoTaskMemAlloc(bytes);
    if (!buf) return E_OUTOFMEMORY;

    memcpy(buf, src, bytes);
    *ppszOut = buf;
    return S_OK;
}

static void LaunchStandaloneProcess(const WCHAR **paths, int count) {
    WCHAR exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) return;

    size_t totalLen = wcslen(exePath) + 32;
    for (int i = 0; i < count; i++) {
        if (paths && paths[i]) {
            totalLen += wcslen(paths[i]) + 8;
        }
    }

    WCHAR *cmdLine = (WCHAR*)malloc(totalLen * sizeof(WCHAR));
    if (!cmdLine) return;

    int pos = swprintf(cmdLine, totalLen, L"\"%ls\"", exePath);
    for (int i = 0; i < count; i++) {
        if (paths && paths[i]) {
            pos += swprintf(cmdLine + pos, totalLen - pos, L" \"%ls\"", paths[i]);
        }
    }

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    free(cmdLine);
}

// -------------------------------------------------------------
// IExplorerCommand Virtual Method Implementations
// -------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE Command_QueryInterface(IExplorerCommand *This, REFIID riid, void **ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IExplorerCommand)) {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE Command_AddRef(IExplorerCommand *This) {
    MagifyExplorerCommand *self = (MagifyExplorerCommand*)This;
    return (ULONG)InterlockedIncrement(&self->refCount);
}

static ULONG STDMETHODCALLTYPE Command_Release(IExplorerCommand *This) {
    MagifyExplorerCommand *self = (MagifyExplorerCommand*)This;
    ULONG count = (ULONG)InterlockedDecrement(&self->refCount);
    if (count == 0) {
        free(self);
        InterlockedDecrement(&g_activeObjectsCount);
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE Command_GetTitle(IExplorerCommand *This, IShellItemArray *psiItemArray, LPWSTR *ppszName) {
    (void)This;
    (void)psiItemArray;
    return AllocCoTaskString(L"Modify with Magify", ppszName);
}

static HRESULT STDMETHODCALLTYPE Command_GetIcon(IExplorerCommand *This, IShellItemArray *psiItemArray, LPWSTR *ppszIcon) {
    (void)This;
    (void)psiItemArray;
    WCHAR exePath[MAX_PATH + 8];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        return E_FAIL;
    }
    wcscat(exePath, L",0");
    return AllocCoTaskString(exePath, ppszIcon);
}

static HRESULT STDMETHODCALLTYPE Command_GetToolTip(IExplorerCommand *This, IShellItemArray *psiItemArray, LPWSTR *ppszInfotip) {
    (void)This;
    (void)psiItemArray;
    return AllocCoTaskString(L"Open Magify radial file control", ppszInfotip);
}

static HRESULT STDMETHODCALLTYPE Command_GetCanonicalName(IExplorerCommand *This, GUID *pguidCommandName) {
    (void)This;
    if (!pguidCommandName) return E_POINTER;
    *pguidCommandName = CLSID_MagifyExplorerCommand;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_GetState(IExplorerCommand *This, IShellItemArray *psiItemArray, WINBOOL fOkToBeSlow, EXPCMDSTATE *pCmdState) {
    (void)This;
    (void)psiItemArray;
    (void)fOkToBeSlow;
    if (!pCmdState) return E_POINTER;
    *pCmdState = ECS_ENABLED;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_GetFlags(IExplorerCommand *This, EXPCMDFLAGS *pFlags) {
    (void)This;
    if (!pFlags) return E_POINTER;
    *pFlags = ECF_DEFAULT;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE Command_EnumSubCommands(IExplorerCommand *This, IEnumExplorerCommand **ppEnum) {
    (void)This;
    if (ppEnum) *ppEnum = NULL;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE Command_Invoke(IExplorerCommand *This, IShellItemArray *psiItemArray, IBindCtx *pbc) {
    (void)This;
    (void)pbc;

    WCHAR *paths[MAGIFY_MAX_FILES] = {0};
    int count = 0;

    if (psiItemArray) {
        DWORD numItems = 0;
        if (SUCCEEDED(IShellItemArray_GetCount(psiItemArray, &numItems))) {
            for (DWORD i = 0; i < numItems && count < MAGIFY_MAX_FILES; i++) {
                IShellItem *item = NULL;
                if (SUCCEEDED(IShellItemArray_GetItemAt(psiItemArray, i, &item)) && item) {
                    LPWSTR pathStr = NULL;
                    if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &pathStr)) && pathStr) {
                        paths[count] = _wcsdup(pathStr);
                        if (paths[count]) count++;
                        CoTaskMemFree(pathStr);
                    }
                    IShellItem_Release(item);
                }
            }
        }
    }

    if (count > 0) {
        if (!Magify_IPC_SendFileList((const WCHAR**)paths, count)) {
            LaunchStandaloneProcess((const WCHAR**)paths, count);
        }
        for (int i = 0; i < count; i++) {
            free(paths[i]);
        }
    } else {
        LaunchStandaloneProcess(NULL, 0);
    }

    InterlockedExchange(&g_hasInvoked, 1);
    return S_OK;
}

static IExplorerCommandVtbl g_ExplorerCommandVtbl = {
    Command_QueryInterface,
    Command_AddRef,
    Command_Release,
    Command_GetTitle,
    Command_GetIcon,
    Command_GetToolTip,
    Command_GetCanonicalName,
    Command_GetState,
    Command_Invoke,
    Command_GetFlags,
    Command_EnumSubCommands
};

// -------------------------------------------------------------
// IClassFactory Virtual Method Implementations
// -------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE ClassFactory_QueryInterface(IClassFactory *This, REFIID riid, void **ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;

    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IClassFactory)) {
        *ppvObject = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ClassFactory_AddRef(IClassFactory *This) {
    (void)This;
    return 2;
}

static ULONG STDMETHODCALLTYPE ClassFactory_Release(IClassFactory *This) {
    (void)This;
    return 1;
}

static HRESULT STDMETHODCALLTYPE ClassFactory_CreateInstance(IClassFactory *This, IUnknown *pUnkOuter, REFIID riid, void **ppvObject) {
    (void)This;
    if (!ppvObject) return E_POINTER;
    *ppvObject = NULL;

    if (pUnkOuter != NULL) {
        return CLASS_E_NOAGGREGATION;
    }

    MagifyExplorerCommand *cmd = (MagifyExplorerCommand*)calloc(1, sizeof(MagifyExplorerCommand));
    if (!cmd) return E_OUTOFMEMORY;

    cmd->lpVtbl = &g_ExplorerCommandVtbl;
    cmd->refCount = 1;

    InterlockedIncrement(&g_activeObjectsCount);

    HRESULT hr = cmd->lpVtbl->QueryInterface((IExplorerCommand*)cmd, riid, ppvObject);
    cmd->lpVtbl->Release((IExplorerCommand*)cmd);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ClassFactory_LockServer(IClassFactory *This, WINBOOL fLock) {
    (void)This;
    if (fLock) {
        InterlockedIncrement(&g_serverLockCount);
    } else {
        InterlockedDecrement(&g_serverLockCount);
    }
    return S_OK;
}

static IClassFactoryVtbl g_ClassFactoryVtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    ClassFactory_CreateInstance,
    ClassFactory_LockServer
};

static MagifyClassFactory g_ClassFactory = {
    &g_ClassFactoryVtbl,
    1
};

IClassFactory *Magify_GetClassFactory(void) {
    return (IClassFactory*)&g_ClassFactory;
}

// -------------------------------------------------------------
// LocalServer32 Watchdog & Message Loop
// -------------------------------------------------------------
static VOID CALLBACK ServerWatchdogTimer(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime) {
    (void)hwnd; (void)uMsg; (void)idEvent; (void)dwTime;
    // If command was invoked or all objects have been released, exit server
    if (g_hasInvoked || (g_activeObjectsCount <= 0 && g_serverLockCount <= 0)) {
        PostQuitMessage(0);
    }
}

int Magify_RunComServer(void) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    DWORD regCookie = 0;
    hr = CoRegisterClassObject(&CLSID_MagifyExplorerCommand,
                              (IUnknown*)&g_ClassFactory,
                              CLSCTX_LOCAL_SERVER,
                              REGCLS_MULTIPLEUSE,
                              &regCookie);
    if (FAILED(hr)) {
        CoUninitialize();
        return 1;
    }

    // Set 4-second watchdog timer to check for idle release or invocation completion
    UINT_PTR timerId = SetTimer(NULL, 0, 4000, ServerWatchdogTimer);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (g_hasInvoked) {
            // Wait 250ms then quit
            Sleep(250);
            break;
        }
    }

    if (timerId) KillTimer(NULL, timerId);

    CoRevokeClassObject(regCookie);
    CoUninitialize();
    return 0;
}
