#include "registry.h"
#include "com_extension.h"
#include <stdio.h>

static void SetRegString(HKEY hKey, const WCHAR *valName, const WCHAR *valStr) {
    if (!hKey || !valStr) return;
    RegSetValueExW(hKey, valName, 0, REG_SZ, (const BYTE*)valStr,
                   (DWORD)((wcslen(valStr) + 1) * sizeof(WCHAR)));
}

BOOL Magify_RegisterContextMenu(void) {
    WCHAR exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        return FALSE;
    }

    WCHAR iconVal[MAX_PATH + 16];
    swprintf(iconVal, MAX_PATH + 16, L"\"%ls\",0", exePath);

    WCHAR localServerCmd[MAX_PATH + 32];
    swprintf(localServerCmd, MAX_PATH + 32, L"\"%ls\" -Embedding", exePath);

    WCHAR cmdFileVal[MAX_PATH + 32];
    swprintf(cmdFileVal, MAX_PATH + 32, L"\"%ls\" \"%%1\"", exePath);

    WCHAR cmdDirVal[MAX_PATH + 32];
    swprintf(cmdDirVal, MAX_PATH + 32, L"\"%ls\"", exePath);

    const WCHAR *menuTitle = L"Modify with Magify";
    const WCHAR *dirTitle  = L"Open Magify";
    const WCHAR *multiSelectModel = L"Player";

    HKEY hKey = NULL;

    // -------------------------------------------------------------
    // 1. Register LocalServer32 COM CLSID for IExplorerCommand
    //    HKCU\Software\Classes\CLSID\{A7B8C9D0-1234-5678-9ABC-DEF012345678}
    // -------------------------------------------------------------
    WCHAR clsidRoot[128];
    swprintf(clsidRoot, 128, L"Software\\Classes\\CLSID\\%ls", MAGIFY_CLSID_STR);
    if (RegCreateKeyExW(HKEY_CURRENT_USER, clsidRoot, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        SetRegString(hKey, NULL, L"Magify Explorer Command");
        RegCloseKey(hKey);

        WCHAR clsidServer[160];
        swprintf(clsidServer, 160, L"%ls\\LocalServer32", clsidRoot);
        if (RegCreateKeyExW(HKEY_CURRENT_USER, clsidServer, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, localServerCmd);
            SetRegString(hKey, L"ServerExecutable", exePath);
            RegCloseKey(hKey);
        }
    }

    // -------------------------------------------------------------
    // 2. Modern Windows 11 ContextMenus Registration
    // -------------------------------------------------------------
    const WCHAR *modernRoots[] = {
        L"Software\\Classes\\*\\ContextMenus\\Magify",
        L"Software\\Classes\\Directory\\ContextMenus\\Magify",
        L"Software\\Classes\\Directory\\Background\\ContextMenus\\Magify"
    };

    for (int i = 0; i < 3; i++) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, modernRoots[i], 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, (i == 2) ? dirTitle : menuTitle);
            SetRegString(hKey, L"Icon", iconVal);
            SetRegString(hKey, L"DelegateExecute", MAGIFY_CLSID_STR);
            SetRegString(hKey, L"CLSID", MAGIFY_CLSID_STR);
            SetRegString(hKey, L"CommandStateHandler", MAGIFY_CLSID_STR);
            RegCloseKey(hKey);
        }
    }

    // -------------------------------------------------------------
    // 3. Shell Registration with ExplorerCommandHandler & Fallback
    // -------------------------------------------------------------
    // A. All files: HKCU\Software\Classes\*\shell\Magify
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        SetRegString(hKey, NULL, menuTitle);
        SetRegString(hKey, L"Icon", iconVal);
        SetRegString(hKey, L"MultiSelectModel", multiSelectModel);
        SetRegString(hKey, L"ExplorerCommandHandler", MAGIFY_CLSID_STR);
        SetRegString(hKey, L"DelegateExecute", MAGIFY_CLSID_STR);
        RegCloseKey(hKey);

        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify\\command",
                            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, cmdFileVal);
            RegCloseKey(hKey);
        }
    }

    // B. Folders: HKCU\Software\Classes\Directory\shell\Magify
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\Magify",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        SetRegString(hKey, NULL, menuTitle);
        SetRegString(hKey, L"Icon", iconVal);
        SetRegString(hKey, L"ExplorerCommandHandler", MAGIFY_CLSID_STR);
        SetRegString(hKey, L"DelegateExecute", MAGIFY_CLSID_STR);
        RegCloseKey(hKey);

        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\Magify\\command",
                            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, cmdFileVal);
            RegCloseKey(hKey);
        }
    }

    // C. Folder / Desktop Background: HKCU\Software\Classes\Directory\Background\shell\Magify
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Magify",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        SetRegString(hKey, NULL, dirTitle);
        SetRegString(hKey, L"Icon", iconVal);
        SetRegString(hKey, L"ExplorerCommandHandler", MAGIFY_CLSID_STR);
        SetRegString(hKey, L"DelegateExecute", MAGIFY_CLSID_STR);
        RegCloseKey(hKey);

        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Magify\\command",
                            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, cmdDirVal);
            RegCloseKey(hKey);
        }
    }

    return TRUE;
}

BOOL Magify_UnregisterContextMenu(void) {
    WCHAR clsidKey[128];
    swprintf(clsidKey, 128, L"Software\\Classes\\CLSID\\%ls", MAGIFY_CLSID_STR);

    // Delete CLSID tree
    RegDeleteTreeW(HKEY_CURRENT_USER, clsidKey);

    // Delete Modern ContextMenus trees
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\ContextMenus\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\ContextMenus\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\ContextMenus\\Magify");

    // Delete Classic Shell trees
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Magify");

    return TRUE;
}

BOOL Magify_IsRegistered(void) {
    HKEY hKey = NULL;
    WCHAR clsidKey[128];
    swprintf(clsidKey, 128, L"Software\\Classes\\CLSID\\%ls", MAGIFY_CLSID_STR);

    if (RegOpenKeyExW(HKEY_CURRENT_USER, clsidKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }

    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }

    return FALSE;
}
