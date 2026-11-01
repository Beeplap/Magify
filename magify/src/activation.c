#define COBJMACROS
#include "activation.h"
#include <exdisp.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <servprov.h>
#include <shlguid.h>

static HRESULT CopySelection(IShellItemArray *items, MagifyFileList *list) {
    DWORD count = 0;
    HRESULT hr = IShellItemArray_GetCount(items, &count);
    if (FAILED(hr)) return hr;
    if (count > MAGIFY_MAX_FILES) return HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
    list->count = 0;
    for (DWORD i = 0; i < count; ++i) {
        IShellItem *item = NULL;
        PWSTR path = NULL;
        hr = IShellItemArray_GetItemAt(items, i, &item);
        if (SUCCEEDED(hr)) hr = IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &path);
        if (SUCCEEDED(hr)) {
            if (wcslen(path) >= MAX_PATH) hr = HRESULT_FROM_WIN32(ERROR_FILENAME_EXCED_RANGE);
            else wcscpy(list->paths[list->count++], path);
        }
        CoTaskMemFree(path);
        if (item) IShellItem_Release(item);
        if (FAILED(hr)) { list->count = 0; return hr; }
    }
    return list->count ? S_OK : S_FALSE;
}

static HRESULT SelectionFromBrowser(IDispatch *dispatch, MagifyFileList *list) {
    IServiceProvider *provider = NULL;
    IShellBrowser *browser = NULL;
    IShellView *view = NULL;
    IFolderView2 *folderView = NULL;
    IShellItemArray *items = NULL;
    HRESULT hr = IDispatch_QueryInterface(dispatch, &IID_IServiceProvider, (void**)&provider);
    if (SUCCEEDED(hr)) hr = IServiceProvider_QueryService(provider, &SID_STopLevelBrowser,
                                                        &IID_IShellBrowser, (void**)&browser);
    if (SUCCEEDED(hr)) hr = IShellBrowser_QueryActiveShellView(browser, &view);
    // Windows 11 exposes multiple tabs with the same top-level HWND.
    // Only the visible Shell view belongs to the tab the user is looking at.
    HWND viewWindow = NULL;
    if (SUCCEEDED(hr)) hr = IShellView_GetWindow(view, &viewWindow);
    if (SUCCEEDED(hr) && !IsWindowVisible(viewWindow)) hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (SUCCEEDED(hr)) hr = IShellView_QueryInterface(view, &IID_IFolderView2, (void**)&folderView);
    if (SUCCEEDED(hr)) hr = IFolderView2_GetSelection(folderView, FALSE, &items);
    if (SUCCEEDED(hr) && items) hr = CopySelection(items, list);
    if (items) IShellItemArray_Release(items);
    if (folderView) IFolderView2_Release(folderView);
    if (view) IShellView_Release(view);
    if (browser) IShellBrowser_Release(browser);
    if (provider) IServiceProvider_Release(provider);
    return hr;
}

HRESULT Magify_GetExplorerSelection(HWND foreground, MagifyFileList *list) {
    if (!list) return E_POINTER;
    list->count = 0;
    WCHAR className[64] = {0};
    GetClassNameW(foreground, className, 64);
    BOOL desktop = wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0;
    if (!desktop && wcscmp(className, L"CabinetWClass") != 0 &&
        wcscmp(className, L"ExploreWClass") != 0) return S_FALSE;

    IShellWindows *windows = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_LOCAL_SERVER,
                                  &IID_IShellWindows, (void**)&windows);
    if (FAILED(hr)) return hr;
    if (desktop) {
        VARIANT location, root;
        VariantInit(&location); VariantInit(&root);
        V_VT(&location) = VT_I4; V_I4(&location) = CSIDL_DESKTOP;
        long desktopHwnd = 0;
        IDispatch *dispatch = NULL;
        hr = IShellWindows_FindWindowSW(windows, &location, &root, SWC_DESKTOP,
                                       &desktopHwnd, SWFO_NEEDDISPATCH, &dispatch);
        if (SUCCEEDED(hr) && dispatch) hr = SelectionFromBrowser(dispatch, list);
        if (dispatch) IDispatch_Release(dispatch);
    } else {
        long count = 0;
        hr = IShellWindows_get_Count(windows, &count);
        for (long i = 0; SUCCEEDED(hr) && i < count; ++i) {
            VARIANT index;
            VariantInit(&index); V_VT(&index) = VT_I4; V_I4(&index) = i;
            IDispatch *dispatch = NULL;
            if (FAILED(IShellWindows_Item(windows, index, &dispatch)) || !dispatch) continue;
            IWebBrowserApp *app = NULL;
            SHANDLE_PTR hwnd = 0;
            BOOL match = SUCCEEDED(IDispatch_QueryInterface(dispatch, &IID_IWebBrowserApp, (void**)&app)) &&
                         SUCCEEDED(IWebBrowserApp_get_HWND(app, &hwnd)) && (HWND)hwnd == foreground;
            if (app) IWebBrowserApp_Release(app);
            if (match) hr = SelectionFromBrowser(dispatch, list);
            IDispatch_Release(dispatch);
            if (match && hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) break;
        }
    }
    IShellWindows_Release(windows);
    return list->count ? hr : (FAILED(hr) ? hr : S_FALSE);
}

HRESULT Magify_PickFiles(MagifyFileList *list) {
    IFileOpenDialog *dialog = NULL;
    IShellItemArray *items = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IFileOpenDialog, (void**)&dialog);
    if (SUCCEEDED(hr)) hr = IFileOpenDialog_SetOptions(dialog,
        FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT | FOS_NOCHANGEDIR);
    if (SUCCEEDED(hr)) hr = IFileOpenDialog_SetTitle(dialog, L"Choose files to manage with Magify");
    if (SUCCEEDED(hr)) hr = IFileOpenDialog_Show(dialog, NULL);
    if (SUCCEEDED(hr)) hr = IFileOpenDialog_GetResults(dialog, &items);
    if (SUCCEEDED(hr)) hr = CopySelection(items, list);
    if (items) IShellItemArray_Release(items);
    if (dialog) IFileOpenDialog_Release(dialog);
    return hr;
}
