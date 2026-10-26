#include "registry.h"
#include <stdio.h>

BOOL Magify_RegisterContextMenu(void) {
    WCHAR exePath[MAX_PATH];
    if (GetModuleFileNameW(NULL, exePath, MAX_PATH) == 0) {
        return FALSE;
    }

    WCHAR iconVal[MAX_PATH + 16];
    swprintf(iconVal, MAX_PATH + 16, L"\"%ls\",0", exePath);

    WCHAR cmdFileVal[MAX_PATH + 32];
    swprintf(cmdFileVal, MAX_PATH + 32, L"\"%ls\" \"%%1\"", exePath);

    WCHAR cmdDirVal[MAX_PATH + 32];
    swprintf(cmdDirVal, MAX_PATH + 32, L"\"%ls\"", exePath);

    const WCHAR *menuTitle = L"Modify with Magify";
    const WCHAR *multiSelectModel = L"Player";

    HKEY hKey = NULL;

    // 1. Register for all files: HKCU\Software\Classes\*\shell\Magify
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\*\\shell\\Magify",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return FALSE;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)menuTitle,
                   (DWORD)((wcslen(menuTitle) + 1) * sizeof(WCHAR)));
    RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (const BYTE*)iconVal,
                   (DWORD)((wcslen(iconVal) + 1) * sizeof(WCHAR)));
    RegSetValueExW(hKey, L"MultiSelectModel", 0, REG_SZ, (const BYTE*)multiSelectModel,
                   (DWORD)((wcslen(multiSelectModel) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);

    // HKCU\Software\Classes\*\shell\Magify\command
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\*\\shell\\Magify\\command",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return FALSE;
    }
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)cmdFileVal,
                   (DWORD)((wcslen(cmdFileVal) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);

    // 2. Register for desktop / folder background: HKCU\Software\Classes\Directory\Background\shell\Magify
    const WCHAR *dirTitle = L"Open Magify";
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Classes\\Directory\\Background\\shell\\Magify",
                        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)dirTitle,
                       (DWORD)((wcslen(dirTitle) + 1) * sizeof(WCHAR)));
        RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (const BYTE*)iconVal,
                       (DWORD)((wcslen(iconVal) + 1) * sizeof(WCHAR)));
        RegCloseKey(hKey);

        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                            L"Software\\Classes\\Directory\\Background\\shell\\Magify\\command",
                            0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)cmdDirVal,
                           (DWORD)((wcslen(cmdDirVal) + 1) * sizeof(WCHAR)));
            RegCloseKey(hKey);
        }
    }

    return TRUE;
}

BOOL Magify_UnregisterContextMenu(void) {
    LONG res1 = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify");
    LONG res2 = RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\Background\\shell\\Magify");
    return (res1 == ERROR_SUCCESS || res2 == ERROR_SUCCESS || res1 == ERROR_FILE_NOT_FOUND);
}

BOOL Magify_IsRegistered(void) {
    HKEY hKey = NULL;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
                             L"Software\\Classes\\*\\shell\\Magify",
                             0, KEY_READ, &hKey);
    if (res == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return TRUE;
    }
    return FALSE;
}
