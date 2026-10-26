# Magify — Reusable Win32 C Recipes & Technical Skills (`skill.md`)

This file contains tested C patterns that coding agents and developers must use when building Magify.

---

## Skill 1: Per-User Context Menu Registration (`src/registry.c`)
Registers **"Modify with Magify"** with the custom `.exe` icon across **all files (`*`)** without requiring Administrator privileges.

```c
#include <windows.h>
#include <wchar.h>

BOOL Magify_RegisterContextMenu(void) {
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    WCHAR iconVal[MAX_PATH + 16];
    swprintf(iconVal, MAX_PATH + 16, L"\"%ls\",0", exePath);

    WCHAR cmdVal[MAX_PATH + 32];
    swprintf(cmdVal, MAX_PATH + 32, L"\"%ls\" \"%%1\"", exePath);

    HKEY hKey;
    // Register for all files (*) under HKCU (no Admin UAC required)
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\*\\shell\\Magify",
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return FALSE;
    }

    const WCHAR *menuTitle = L"Modify with Magify";
    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)menuTitle,
                   (DWORD)((wcslen(menuTitle) + 1) * sizeof(WCHAR)));
    RegSetValueExW(hKey, L"Icon", 0, REG_SZ, (const BYTE*)iconVal,
                   (DWORD)((wcslen(iconVal) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);

    if (RegCreateKeyExW(HKEY_CURRENT_USER,
        L"Software\\Classes\\*\\shell\\Magify\\command",
        0, NULL, 0, KEY_WRITE, NULL, &hKey, NULL) != ERROR_SUCCESS) {
        return FALSE;
    }

    RegSetValueExW(hKey, NULL, 0, REG_SZ, (const BYTE*)cmdVal,
                   (DWORD)((wcslen(cmdVal) + 1) * sizeof(WCHAR)));
    RegCloseKey(hKey);
    return TRUE;
}

BOOL Magify_UnregisterContextMenu(void) {
    return RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\*\\shell\\Magify") == ERROR_SUCCESS;
}
```

---

## Skill 2: Per-Pixel Alpha Layered Window (`src/ui_overlay.c`)

Creates a borderless, taskbar-hidden, always-on-top transparent surface rendered via a 32-bit ARGB DIB section and `UpdateLayeredWindow`.

```c
HWND Magify_CreateOverlayWindow(HINSTANCE hInst, int width, int height, POINT cursor) {
    // Clamp window position so the radial menu never clips off-screen
    HMONITOR hMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof(MONITORINFO) };
    GetMonitorInfoW(hMon, &mi);

    int x = cursor.x - (width / 2);
    int y = cursor.y - (height / 2);
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;
    if (x + width > mi.rcWork.right)   x = mi.rcWork.right - width;
    if (y + height > mi.rcWork.bottom) y = mi.rcWork.bottom - height;

    return CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MagifyOverlayClass",
        L"Magify",
        WS_POPUP | WS_VISIBLE,
        x, y, width, height,
        NULL, NULL, hInst, NULL
    );
}
```

---

## Skill 3: Polar Coordinate Slice Hit-Testing

Determines which radial slice (0..N-1) or ring the cursor is hovering over in $O(1)$ time.

```c
#include <math.h>

#define MAGIFY_PI 3.14159265358979323846f

int Magify_HitTestRadial(float mouseX, float mouseY, float centerX, float centerY,
                         float innerR, float outerR, int sliceCount, float rotationOffsetDeg) {
    float dx = mouseX - centerX;
    float dy = mouseY - centerY;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < innerR) return -1; // Center Hub
    if (dist > outerR) return -2; // Outside Ring

    // Angle in degrees [0, 360)
    float angleDeg = atan2f(dy, dx) * (180.0f / MAGIFY_PI);
    if (angleDeg < 0.0f) angleDeg += 360.0f;

    angleDeg = fmodf(angleDeg - rotationOffsetDeg + 360.0f, 360.0f);
    float sliceSpan = 360.0f / (float)sliceCount;
    return (int)(angleDeg / sliceSpan);
}
```

---

## Skill 4: Target-Size Image Compression Loop (e.g., "< 200 KB")

Binary-searches JPEG/WebP quality factor in memory (takes < 25ms) to hit an exact maximum KB target for online application portals.

```c
int Magify_CompressToTargetBytes(const uint8_t *rgba, int w, int h, size_t maxBytes,
                                 uint8_t **outBuf, size_t *outSize) {
    int lowQ = 10, highQ = 95, bestQ = 50;
    while (lowQ <= highQ) {
        int midQ = (lowQ + highQ) / 2;
        size_t encodedLen = EncodeJpegToMemory(rgba, w, h, midQ, outBuf);
        if (encodedLen <= maxBytes) {
            bestQ = midQ;
            *outSize = encodedLen;
            lowQ = midQ + 1; // Try higher quality while staying under maxBytes
        } else {
            highQ = midQ - 1;
        }
    }
    return bestQ;
}
```
