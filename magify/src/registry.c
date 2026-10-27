#include "registry.h"
#include "com_extension.h"
#include <stdio.h>
#include <shlobj.h>

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

    WCHAR localServerCmd[MAX_PATH + 32];
    swprintf(localServerCmd, MAX_PATH + 32, L"\"%ls\" -Embedding", exePath);

    WCHAR cmdFileVal[MAX_PATH + 32];
    swprintf(cmdFileVal, MAX_PATH + 32, L"\"%ls\" \"%%1\"", exePath);

    WCHAR cmdDirVal[MAX_PATH + 32];
    swprintf(cmdDirVal, MAX_PATH + 32, L"\"%ls\"", exePath);

    const WCHAR *menuTitle = L"Modify with Magify";
    const WCHAR *dirTitle  = L"Open Magify";

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
    // 2. Modern Windows 11 ContextMenus Registration (COM Test Compatibility)
    // -------------------------------------------------------------
    const WCHAR *modernRoots[] = {
        L"Software\\Classes\\*\\ContextMenus\\Magify",
        L"Software\\Classes\\Directory\\ContextMenus\\Magify",
        L"Software\\Classes\\Directory\\Background\\ContextMenus\\Magify"
    };

    for (int i = 0; i < 3; i++) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, modernRoots[i], 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, (i == 2) ? dirTitle : menuTitle);
            SetRegString(hKey, L"Icon", exePath);
            SetRegString(hKey, L"DelegateExecute", MAGIFY_CLSID_STR);
            SetRegString(hKey, L"CLSID", MAGIFY_CLSID_STR);
            RegCloseKey(hKey);
        }
    }

    // -------------------------------------------------------------
    // 3. Shell Registration (Clean native Win32 Shell Verbs)
    //    IMPORTANT: We do NOT set ExplorerCommandHandler or DelegateExecute
    //    on shell\Magify because Windows Explorer attempts to load them as
    //    in-process COM handlers and completely hides the verb if out-of-process!
    // -------------------------------------------------------------
    
    // A. All files: HKCU\Software\Classes\*\shell\Magify
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        SetRegString(hKey, NULL, menuTitle);
        SetRegString(hKey, L"Icon", exePath);
        // Explicitly purge legacy/blocking COM delegate handlers so Explorer renders immediately
        RegDeleteValueW(hKey, L"ExplorerCommandHandler");
        RegDeleteValueW(hKey, L"DelegateExecute");
        RegDeleteValueW(hKey, L"MultiSelectModel");
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
        SetRegString(hKey, L"Icon", exePath);
        RegDeleteValueW(hKey, L"ExplorerCommandHandler");
        RegDeleteValueW(hKey, L"DelegateExecute");
        RegDeleteValueW(hKey, L"MultiSelectModel");
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
        SetRegString(hKey, L"Icon", exePath);
        RegDeleteValueW(hKey, L"ExplorerCommandHandler");
        RegDeleteValueW(hKey, L"DelegateExecute");
        RegDeleteValueW(hKey, L"MultiSelectModel");
        RegCloseKey(hKey);

        if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Magify\\command",
                            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, cmdDirVal);
            RegCloseKey(hKey);
        }
    }

    // D. Explicit File Associations: Images & PDFs for instant shell priority
    const WCHAR *assocRoots[] = {
        L"Software\\Classes\\SystemFileAssociations\\image\\shell\\Magify",
        L"Software\\Classes\\SystemFileAssociations\\.pdf\\shell\\Magify"
    };
    for (int i = 0; i < 2; i++) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, assocRoots[i], 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            SetRegString(hKey, NULL, menuTitle);
            SetRegString(hKey, L"Icon", exePath);
            RegCloseKey(hKey);

            WCHAR subCmd[160];
            swprintf(subCmd, 160, L"%ls\\command", assocRoots[i]);
            if (RegCreateKeyExW(HKEY_CURRENT_USER, subCmd, 0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
                SetRegString(hKey, NULL, cmdFileVal);
                RegCloseKey(hKey);
            }
        }
    }

    // E. Register auto-start in HKCU Run so Magify runs automatically on computer boot
    HKEY hRun = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        WCHAR runCmd[MAX_PATH + 32];
        swprintf(runCmd, MAX_PATH + 32, L"\"%ls\" --background", exePath);
        SetRegString(hRun, L"Magify", runCmd);
        RegCloseKey(hRun);
    }

    // Notify File Explorer to flush cache and display the new menu immediately
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

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

    // Delete Shell trees
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SystemFileAssociations\\image\\shell\\Magify");
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\SystemFileAssociations\\.pdf\\shell\\Magify");

    // Unregister auto-start from HKCU Run
    HKEY hRun = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hRun) == ERROR_SUCCESS) {
        RegDeleteValueW(hRun, L"Magify");
        RegCloseKey(hRun);
    }

    // Notify File Explorer
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    return TRUE;
}

BOOL Magify_IsRegistered(void) {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify\\command",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }

    return FALSE;
}
