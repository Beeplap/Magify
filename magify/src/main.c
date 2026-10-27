#include "magify.h"
#include "registry.h"
#include "ipc.h"
#include "com_extension.h"
#include "ui_overlay.h"
#include "ops_common.h"
#include <stdio.h>
#include <dwmapi.h>
#include <commctrl.h>
#include <shellapi.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define BTN_TOGGLE_REG  1001
#define BTN_TEST_IMAGE  1002
#define BTN_TEST_PDF    1003
#define BTN_CLOSE       1004

static HINSTANCE g_hInstance = NULL;
static HWND g_hwndDashboard = NULL;
static HBRUSH g_hBrushBg = NULL;
static HBRUSH g_hBrushCard = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontSmall = NULL;

static void InitDpiAwareness(void) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
        SetProcessDpiAwarenessContextProc pSet =
            (SetProcessDpiAwarenessContextProc)(void*)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        if (pSet) {
            pSet(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware();
}

static void CreateDashboardFonts(void) {
    g_hFontTitle = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_hFontNormal = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_hFontSmall = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
}

static void DrawPillButton(LPDRAWITEMSTRUCT dis, const WCHAR *text, COLORREF bgCol, COLORREF textCol, COLORREF borderCol) {
    HDC hdc = dis->hDC;
    RECT rc = dis->rcItem;

    if (dis->itemState & ODS_SELECTED) {
        bgCol = RGB(GetRValue(bgCol) * 0.8, GetGValue(bgCol) * 0.8, GetBValue(bgCol) * 0.8);
    }

    HBRUSH hBrush = CreateSolidBrush(bgCol);
    HPEN hPen = CreatePen(PS_SOLID, 1, borderCol);
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);

    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, 12, 12);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, textCol);
    HGDIOBJ oldFont = SelectObject(hdc, g_hFontNormal);
    DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, oldFont);
}

static void LaunchTestOverlay(MagifyFileClass fileClass) {
    MagifyContext *ctx = (MagifyContext*)calloc(1, sizeof(MagifyContext));
    if (!ctx) return;

    ctx->hInstance = g_hInstance;
    GetCursorPos(&ctx->spawnCursor);
    ctx->dpiScale = Magify_GetDpiScaleForPoint(ctx->spawnCursor);
    ctx->overlayWidth = (int)(MAGIFY_RADIAL_WIDTH * ctx->dpiScale);
    ctx->overlayHeight = (int)(MAGIFY_RADIAL_HEIGHT * ctx->dpiScale);

    ctx->primaryClass = fileClass;
    ctx->fileCount = 1;
    if (fileClass == FILE_CLASS_IMAGE) {
        wcscpy(ctx->files[0], L"sample_photo.png");
        ctx->totalSizeBytes = 2450000;
    } else {
        wcscpy(ctx->files[0], L"quarterly_report.pdf");
        ctx->totalSizeBytes = 4850000;
    }

    Magify_ShowOverlay(ctx);
}

static LRESULT CALLBACK DashboardWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            BOOL isLight = Magify_IsSystemLightTheme();
            BOOL dark = !isLight;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

            CreateWindowW(L"BUTTON", L"Toggle Menu",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          36, 175, 432, 42,
                          hwnd, (HMENU)BTN_TOGGLE_REG, g_hInstance, NULL);

            CreateWindowW(L"BUTTON", L"Test Image Overlay",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          36, 230, 210, 38,
                          hwnd, (HMENU)BTN_TEST_IMAGE, g_hInstance, NULL);

            CreateWindowW(L"BUTTON", L"Test PDF Overlay",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          258, 230, 210, 38,
                          hwnd, (HMENU)BTN_TEST_PDF, g_hInstance, NULL);

            CreateWindowW(L"BUTTON", L"Close Dashboard",
                          WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                          36, 280, 432, 36,
                          hwnd, (HMENU)BTN_CLOSE, g_hInstance, NULL);
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            BOOL isLight = Magify_IsSystemLightTheme();
            BOOL dark = !isLight;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            if (g_hBrushBg) DeleteObject(g_hBrushBg);
            if (g_hBrushCard) DeleteObject(g_hBrushCard);
            if (isLight) {
                g_hBrushBg = CreateSolidBrush(RGB(248, 250, 252));
                g_hBrushCard = CreateSolidBrush(RGB(241, 245, 249));
            } else {
                g_hBrushBg = CreateSolidBrush(RGB(18, 19, 26));
                g_hBrushCard = CreateSolidBrush(RGB(26, 29, 42));
            }
            InvalidateRect(hwnd, NULL, TRUE);
            return 0;
        }

        case WM_DRAWITEM: {
            LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lParam;
            if (dis->CtlID == BTN_TOGGLE_REG) {
                BOOL reg = Magify_IsRegistered();
                if (reg) {
                    DrawPillButton(dis, L"Uninstall Right-Click Context Menu",
                                   RGB(244, 63, 94), RGB(255, 255, 255), RGB(251, 113, 133));
                } else {
                    DrawPillButton(dis, L"Install Right-Click Context Menu (Instant)",
                                   RGB(124, 58, 237), RGB(255, 255, 255), RGB(168, 85, 247));
                }
                return TRUE;
            } else if (dis->CtlID == BTN_TEST_IMAGE) {
                BOOL isLight = Magify_IsSystemLightTheme();
                DrawPillButton(dis, L"Test Radial (Image)",
                               isLight ? RGB(226, 232, 240) : RGB(30, 41, 59),
                               isLight ? RGB(8, 145, 178) : RGB(34, 211, 238),
                               isLight ? RGB(203, 213, 225) : RGB(51, 65, 85));
                return TRUE;
            } else if (dis->CtlID == BTN_TEST_PDF) {
                BOOL isLight = Magify_IsSystemLightTheme();
                DrawPillButton(dis, L"Test Radial (PDF)",
                               isLight ? RGB(226, 232, 240) : RGB(30, 41, 59),
                               isLight ? RGB(109, 40, 217) : RGB(168, 85, 247),
                               isLight ? RGB(203, 213, 225) : RGB(51, 65, 85));
                return TRUE;
            } else if (dis->CtlID == BTN_CLOSE) {
                BOOL isLight = Magify_IsSystemLightTheme();
                DrawPillButton(dis, L"Close",
                               isLight ? RGB(241, 245, 249) : RGB(24, 27, 38),
                               isLight ? RGB(71, 85, 105) : RGB(148, 163, 184),
                               isLight ? RGB(203, 213, 225) : RGB(39, 45, 62));
                return TRUE;
            }
            break;
        }

        case WM_COMMAND: {
            int wmId = LOWORD(wParam);
            if (wmId == BTN_TOGGLE_REG) {
                if (Magify_IsRegistered()) {
                    Magify_UnregisterContextMenu();
                } else {
                    Magify_RegisterContextMenu();
                }
                InvalidateRect(hwnd, NULL, TRUE);
            } else if (wmId == BTN_TEST_IMAGE) {
                LaunchTestOverlay(FILE_CLASS_IMAGE);
            } else if (wmId == BTN_TEST_PDF) {
                LaunchTestOverlay(FILE_CLASS_PDF);
            } else if (wmId == BTN_CLOSE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            BOOL isLight = Magify_IsSystemLightTheme();

            RECT clientRc;
            GetClientRect(hwnd, &clientRc);
            FillRect(hdc, &clientRc, g_hBrushBg);

            // Title & Header
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, isLight ? RGB(8, 145, 178) : RGB(34, 211, 238));
            HGDIOBJ oldFont = SelectObject(hdc, g_hFontTitle);
            RECT titleRc = { 36, 28, 470, 60 };
            DrawTextW(hdc, L"MAGIFY", -1, &titleRc, DT_LEFT | DT_SINGLELINE);

            SetTextColor(hdc, isLight ? RGB(71, 85, 105) : RGB(148, 163, 184));
            SelectObject(hdc, g_hFontSmall);
            RECT subRc = { 36, 60, 470, 80 };
            DrawTextW(hdc, L"120fps Instant Radial Utilities for Images & PDFs", -1, &subRc, DT_LEFT | DT_SINGLELINE);

            // Card background for registration status
            RECT cardRc = { 36, 92, 468, 155 };
            HGDIOBJ oldBrush = SelectObject(hdc, g_hBrushCard);
            HPEN hPen = CreatePen(PS_SOLID, 1, isLight ? RGB(203, 213, 225) : RGB(51, 65, 85));
            HGDIOBJ oldPen = SelectObject(hdc, hPen);
            RoundRect(hdc, cardRc.left, cardRc.top, cardRc.right, cardRc.bottom, 12, 12);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(hPen);

            BOOL reg = Magify_IsRegistered();
            RECT statRc = { 52, 104, 452, 126 };
            SelectObject(hdc, g_hFontNormal);
            if (reg) {
                SetTextColor(hdc, isLight ? RGB(5, 150, 105) : RGB(16, 185, 129));
                DrawTextW(hdc, L"● Context Menu: Active (Right-click any file)", -1, &statRc, DT_LEFT | DT_SINGLELINE);
            } else {
                SetTextColor(hdc, RGB(244, 63, 94));
                DrawTextW(hdc, L"○ Context Menu: Not Installed", -1, &statRc, DT_LEFT | DT_SINGLELINE);
            }

            RECT descRc = { 52, 128, 452, 148 };
            SelectObject(hdc, g_hFontSmall);
            SetTextColor(hdc, isLight ? RGB(71, 85, 105) : RGB(148, 163, 184));
            DrawTextW(hdc, reg ? L"Select one or multiple files in Explorer to open radial overlay."
                               : L"Click below to register into Windows Explorer right-click menu.",
                      -1, &descRc, DT_LEFT | DT_SINGLELINE);

            SelectObject(hdc, oldFont);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY: {
            if (g_hBrushBg) { DeleteObject(g_hBrushBg); g_hBrushBg = NULL; }
            if (g_hBrushCard) { DeleteObject(g_hBrushCard); g_hBrushCard = NULL; }
            if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = NULL; }
            if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
            if (g_hFontSmall) { DeleteObject(g_hFontSmall); g_hFontSmall = NULL; }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static void ShowDashboard(HINSTANCE hInst) {
    BOOL isLight = Magify_IsSystemLightTheme();
    if (isLight) {
        g_hBrushBg = CreateSolidBrush(RGB(248, 250, 252));     // Slate 50 #F8FAFC
        g_hBrushCard = CreateSolidBrush(RGB(241, 245, 249));   // Slate 100 #F1F5F9
    } else {
        g_hBrushBg = CreateSolidBrush(RGB(18, 19, 26));     // Deep Obsidian #12131A
        g_hBrushCard = CreateSolidBrush(RGB(26, 29, 42));   // Card fill #1A1D2A
    }
    CreateDashboardFonts();

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DashboardWndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_MAGIFY_ICON));
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"MagifyDashboardClass";

    RegisterClassExW(&wc);

    int winW = 506;
    int winH = 370;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    int posX = (screenW - winW) / 2;
    int posY = (screenH - winH) / 2;

    g_hwndDashboard = CreateWindowExW(
        WS_EX_APPWINDOW,
        L"MagifyDashboardClass",
        L"Magify — Control Dashboard",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        posX, posY, winW, winH,
        NULL, NULL, hInst, NULL
    );

    if (g_hwndDashboard) {
        ShowWindow(g_hwndDashboard, SW_SHOW);
        UpdateWindow(g_hwndDashboard);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_hBrushBg) DeleteObject(g_hBrushBg);
    if (g_hBrushCard) DeleteObject(g_hBrushCard);
    if (g_hFontTitle) DeleteObject(g_hFontTitle);
    if (g_hFontNormal) DeleteObject(g_hFontNormal);
    if (g_hFontSmall) DeleteObject(g_hFontSmall);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;

    g_hInstance = hInstance;
    InitDpiAwareness();

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;

    // Check for COM LocalServer32 switch
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"-Embedding") == 0 || _wcsicmp(argv[i], L"/Embedding") == 0) {
            LocalFree(argv);
            return Magify_RunComServer();
        }
    }

    // Check CLI Flags
    if (argc >= 2) {
        if (_wcsicmp(argv[1], L"--install") == 0) {
            BOOL ok = Magify_RegisterContextMenu();
            LocalFree(argv);
            return ok ? 0 : 1;
        }
        if (_wcsicmp(argv[1], L"--uninstall") == 0) {
            BOOL ok = Magify_UnregisterContextMenu();
            LocalFree(argv);
            return ok ? 0 : 1;
        }
    }

    // Double-clicked or launched with zero file args -> open Dashboard
    if (argc <= 1) {
        LocalFree(argv);
        ShowDashboard(hInstance);
        return 0;
    }

    // Try sending paths to existing primary instance via Named Pipe
    if (Magify_IPC_SendPathsToPrimary(argc, argv)) {
        LocalFree(argv);
        return 0; // Handed off to existing primary instance!
    }

    // Otherwise, we are the PRIMARY instance
    MagifyContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.hInstance = hInstance;
    GetCursorPos(&ctx.spawnCursor);
    ctx.dpiScale = Magify_GetDpiScaleForPoint(ctx.spawnCursor);
    ctx.overlayWidth = (int)(MAGIFY_RADIAL_WIDTH * ctx.dpiScale);
    ctx.overlayHeight = (int)(MAGIFY_RADIAL_HEIGHT * ctx.dpiScale);

    // Collect initial files
    for (int i = 1; i < argc && ctx.fileCount < MAGIFY_MAX_FILES; i++) {
        const WCHAR *p = argv[i];
        if (p && p[0] != L'-') {
            wcsncpy(ctx.files[ctx.fileCount], p, MAX_PATH - 1);
            ctx.files[ctx.fileCount][MAX_PATH - 1] = L'\0';
            ctx.totalSizeBytes += Magify_GetFileSize(p);
            ctx.fileCount++;
        }
    }
    LocalFree(argv);

    if (ctx.fileCount > 0) {
        ctx.primaryClass = Magify_DetectFileClass(ctx.files[0]);
    } else {
        ctx.primaryClass = FILE_CLASS_GENERIC;
    }

    // Start single-instance IPC Named Pipe Server
    Magify_IPC_StartServer(&ctx);

    // Show Radial Overlay
    Magify_ShowOverlay(&ctx);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Stop IPC Server before exit
    Magify_IPC_StopServer();

    return 0;
}
