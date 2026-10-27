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
#include <windowsx.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#define BTN_TOGGLE_REG  1001
#define BTN_TEST_IMAGE  1002
#define BTN_TEST_PDF    1003
#define BTN_CLOSE       1004

typedef struct {
    int id;
    RECT rc;
    const WCHAR *label;
    const WCHAR *sublabel;
    COLORREF accent;
    int iconType; // 1=shield/power, 2=image, 3=pdf, 4=close
} DashboardButton;

static HINSTANCE g_hInstance = NULL;
static HWND g_hwndDashboard = NULL;
static HFONT g_hFontTitle = NULL;
static HFONT g_hFontNormal = NULL;
static HFONT g_hFontSmall = NULL;
static HFONT g_hFontMicro = NULL;

static int g_hoveredBtn = -1;
static int g_pressedBtn = -1;
static BOOL g_mouseTracking = FALSE;

#define NUM_DASH_BUTTONS 4
static DashboardButton g_dashButtons[NUM_DASH_BUTTONS] = {
    { BTN_TOGGLE_REG, { 32, 184, 498, 248 }, L"", L"", 0, 1 },
    { BTN_TEST_IMAGE, { 32, 262, 258, 342 }, L"Preview Image Radial", L"Test 120fps Image Overlay", RGB(6, 182, 212), 2 },
    { BTN_TEST_PDF,   { 272, 262, 498, 342 }, L"Preview PDF Radial", L"Test Visual PDF Organizer", RGB(168, 85, 247), 3 },
    { BTN_CLOSE,      { 32, 356, 498, 404 }, L"Dismiss Dashboard", L"Close control panel", RGB(148, 163, 184), 4 }
};

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
    if (!g_hFontTitle) {
        g_hFontTitle = CreateFontW(-22, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    if (!g_hFontNormal) {
        g_hFontNormal = CreateFontW(-13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    if (!g_hFontSmall) {
        g_hFontSmall = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
    if (!g_hFontMicro) {
        g_hFontMicro = CreateFontW(-10, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    }
}

static void DeleteDashboardFonts(void) {
    if (g_hFontTitle) { DeleteObject(g_hFontTitle); g_hFontTitle = NULL; }
    if (g_hFontNormal) { DeleteObject(g_hFontNormal); g_hFontNormal = NULL; }
    if (g_hFontSmall) { DeleteObject(g_hFontSmall); g_hFontSmall = NULL; }
    if (g_hFontMicro) { DeleteObject(g_hFontMicro); g_hFontMicro = NULL; }
}

static void DrawNeumorphicCard(HDC hdc, const RECT *rc, int radius, BOOL isLight, BOOL isDebossed) {
    int x = rc->left;
    int y = rc->top;
    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;

    if (isDebossed) {
        COLORREF bgCol = isLight ? RGB(232, 236, 242) : RGB(20, 22, 28);
        COLORREF innerShadow1 = isLight ? RGB(208, 214, 224) : RGB(12, 13, 17);
        COLORREF innerShadow2 = isLight ? RGB(220, 225, 234) : RGB(16, 17, 22);
        COLORREF innerLight = isLight ? RGB(255, 255, 255) : RGB(34, 38, 48);

        HBRUSH hBg = CreateSolidBrush(bgCol);
        HPEN hPenNone = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldBrush = SelectObject(hdc, hBg);
        HGDIOBJ oldPen = SelectObject(hdc, hPenNone);
        RoundRect(hdc, x, y, x + w, y + h, radius, radius);

        HPEN hPenDark1 = CreatePen(PS_SOLID, 1, innerShadow1);
        SelectObject(hdc, hPenDark1);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, x, y, x + w, y + h, radius, radius);
        DeleteObject(hPenDark1);

        HPEN hPenDark2 = CreatePen(PS_SOLID, 1, innerShadow2);
        SelectObject(hdc, hPenDark2);
        RoundRect(hdc, x + 1, y + 1, x + w - 1, y + h - 1, radius - 2, radius - 2);
        DeleteObject(hPenDark2);

        HPEN hPenLight = CreatePen(PS_SOLID, 1, innerLight);
        SelectObject(hdc, hPenLight);
        RoundRect(hdc, x + 2, y + 2, x + w, y + h, radius, radius);
        DeleteObject(hPenLight);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hBg);
        DeleteObject(hPenNone);
    } else {
        COLORREF bgCol = isLight ? RGB(244, 247, 251) : RGB(26, 29, 37);
        COLORREF shadow1 = isLight ? RGB(214, 220, 230) : RGB(10, 11, 14);
        COLORREF shadow2 = isLight ? RGB(226, 231, 240) : RGB(16, 18, 23);
        COLORREF highlight = isLight ? RGB(255, 255, 255) : RGB(38, 42, 54);

        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));

        HBRUSH hS1 = CreateSolidBrush(shadow1);
        HPEN hPS1 = CreatePen(PS_SOLID, 1, shadow1);
        SelectObject(hdc, hS1); SelectObject(hdc, hPS1);
        RoundRect(hdc, x + 4, y + 4, x + w + 4, y + h + 4, radius, radius);
        DeleteObject(hS1); DeleteObject(hPS1);

        HBRUSH hS2 = CreateSolidBrush(shadow2);
        HPEN hPS2 = CreatePen(PS_SOLID, 1, shadow2);
        SelectObject(hdc, hS2); SelectObject(hdc, hPS2);
        RoundRect(hdc, x + 2, y + 2, x + w + 2, y + h + 2, radius, radius);
        DeleteObject(hS2); DeleteObject(hPS2);

        HBRUSH hHL = CreateSolidBrush(highlight);
        HPEN hPHL = CreatePen(PS_SOLID, 1, highlight);
        SelectObject(hdc, hHL); SelectObject(hdc, hPHL);
        RoundRect(hdc, x - 2, y - 2, x + w - 2, y + h - 2, radius, radius);
        DeleteObject(hHL); DeleteObject(hPHL);

        HBRUSH hFace = CreateSolidBrush(bgCol);
        HPEN hPFace = CreatePen(PS_SOLID, 1, highlight);
        SelectObject(hdc, hFace); SelectObject(hdc, hPFace);
        RoundRect(hdc, x, y, x + w, y + h, radius, radius);
        DeleteObject(hFace); DeleteObject(hPFace);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }
}

static void DrawNeumorphicButton(HDC hdc, const DashboardButton *btn, BOOL isHovered, BOOL isPressed, BOOL isLight) {
    int x = btn->rc.left;
    int y = btn->rc.top;
    int w = btn->rc.right - btn->rc.left;
    int h = btn->rc.bottom - btn->rc.top;
    int radius = 16;

    if (isPressed) {
        x += 1;
        y += 1;
        COLORREF bgCol = isLight ? RGB(225, 230, 238) : RGB(18, 20, 25);
        COLORREF innerShadow1 = isLight ? RGB(195, 202, 214) : RGB(10, 11, 14);
        COLORREF innerShadow2 = isLight ? RGB(210, 216, 226) : RGB(14, 15, 19);

        HBRUSH hBg = CreateSolidBrush(bgCol);
        HPEN hPenNone = CreatePen(PS_NULL, 0, 0);
        HGDIOBJ oldBrush = SelectObject(hdc, hBg);
        HGDIOBJ oldPen = SelectObject(hdc, hPenNone);
        RoundRect(hdc, x, y, x + w, y + h, radius, radius);

        HPEN hPenS1 = CreatePen(PS_SOLID, 1, innerShadow1);
        SelectObject(hdc, hPenS1);
        SelectObject(hdc, GetStockObject(NULL_BRUSH));
        RoundRect(hdc, x, y, x + w, y + h, radius, radius);
        DeleteObject(hPenS1);

        HPEN hPenS2 = CreatePen(PS_SOLID, 1, innerShadow2);
        SelectObject(hdc, hPenS2);
        RoundRect(hdc, x + 1, y + 1, x + w - 1, y + h - 1, radius - 2, radius - 2);
        DeleteObject(hPenS2);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hBg);
        DeleteObject(hPenNone);
    } else {
        COLORREF shadowLayer3 = isLight ? RGB(224, 229, 238) : RGB(10, 11, 14);
        COLORREF shadowLayer2 = isLight ? RGB(212, 218, 228) : RGB(14, 15, 19);
        COLORREF shadowLayer1 = isLight ? RGB(200, 208, 220) : RGB(18, 20, 25);
        COLORREF lightLayer2  = isLight ? RGB(255, 255, 255) : RGB(34, 38, 48);
        COLORREF lightLayer1  = isLight ? RGB(255, 255, 255) : RGB(42, 47, 60);

        COLORREF faceCol = isHovered ? (isLight ? RGB(255, 255, 255) : RGB(34, 38, 49))
                                     : (isLight ? RGB(244, 247, 252) : RGB(27, 30, 38));

        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        HGDIOBJ oldPen = SelectObject(hdc, GetStockObject(NULL_PEN));

        HBRUSH hS3 = CreateSolidBrush(shadowLayer3);
        HPEN hPS3 = CreatePen(PS_SOLID, 1, shadowLayer3);
        SelectObject(hdc, hS3); SelectObject(hdc, hPS3);
        RoundRect(hdc, x + 5, y + 5, x + w + 5, y + h + 5, radius, radius);
        DeleteObject(hS3); DeleteObject(hPS3);

        HBRUSH hS2 = CreateSolidBrush(shadowLayer2);
        HPEN hPS2 = CreatePen(PS_SOLID, 1, shadowLayer2);
        SelectObject(hdc, hS2); SelectObject(hdc, hPS2);
        RoundRect(hdc, x + 3, y + 3, x + w + 3, y + h + 3, radius, radius);
        DeleteObject(hS2); DeleteObject(hPS2);

        HBRUSH hS1 = CreateSolidBrush(shadowLayer1);
        HPEN hPS1 = CreatePen(PS_SOLID, 1, shadowLayer1);
        SelectObject(hdc, hS1); SelectObject(hdc, hPS1);
        RoundRect(hdc, x + 1, y + 1, x + w + 1, y + h + 1, radius, radius);
        DeleteObject(hS1); DeleteObject(hPS1);

        HBRUSH hL2 = CreateSolidBrush(lightLayer2);
        HPEN hPL2 = CreatePen(PS_SOLID, 1, lightLayer2);
        SelectObject(hdc, hL2); SelectObject(hdc, hPL2);
        RoundRect(hdc, x - 2, y - 2, x + w - 2, y + h - 2, radius, radius);
        DeleteObject(hL2); DeleteObject(hPL2);

        HBRUSH hL1 = CreateSolidBrush(lightLayer1);
        HPEN hPL1 = CreatePen(PS_SOLID, 1, lightLayer1);
        SelectObject(hdc, hL1); SelectObject(hdc, hPL1);
        RoundRect(hdc, x - 1, y - 1, x + w - 1, y + h - 1, radius, radius);
        DeleteObject(hL1); DeleteObject(hPL1);

        HBRUSH hFace = CreateSolidBrush(faceCol);
        HPEN hPFace = CreatePen(PS_SOLID, 1, isHovered ? btn->accent : (isLight ? RGB(255, 255, 255) : RGB(50, 56, 70)));
        SelectObject(hdc, hFace); SelectObject(hdc, hPFace);
        RoundRect(hdc, x, y, x + w, y + h, radius, radius);
        DeleteObject(hFace); DeleteObject(hPFace);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }

    int barX = x + 16;
    int barY = y + (h - 24) / 2;
    HBRUSH hBar = CreateSolidBrush(btn->accent);
    HPEN hPBar = CreatePen(PS_SOLID, 1, btn->accent);
    HGDIOBJ oB = SelectObject(hdc, hBar);
    HGDIOBJ oP = SelectObject(hdc, hPBar);
    RoundRect(hdc, barX, barY, barX + 5, barY + 24, 4, 4);
    SelectObject(hdc, oB);
    SelectObject(hdc, oP);
    DeleteObject(hBar);
    DeleteObject(hPBar);

    int iconCenterX = barX + 22;
    int iconCenterY = y + h / 2;
    HPEN hGlyphPen = CreatePen(PS_SOLID, 2, btn->accent);
    HGDIOBJ oGP = SelectObject(hdc, hGlyphPen);
    SelectObject(hdc, GetStockObject(NULL_BRUSH));

    if (btn->iconType == 1) {
        POINT pts[5] = {
            { iconCenterX - 7, iconCenterY - 8 },
            { iconCenterX + 7, iconCenterY - 8 },
            { iconCenterX + 7, iconCenterY + 1 },
            { iconCenterX,     iconCenterY + 8 },
            { iconCenterX - 7, iconCenterY + 1 }
        };
        Polygon(hdc, pts, 5);
        MoveToEx(hdc, iconCenterX - 3, iconCenterY, NULL);
        LineTo(hdc, iconCenterX - 1, iconCenterY + 3);
        LineTo(hdc, iconCenterX + 4, iconCenterY - 3);
    } else if (btn->iconType == 2) {
        RoundRect(hdc, iconCenterX - 8, iconCenterY - 7, iconCenterX + 8, iconCenterY + 7, 4, 4);
        Ellipse(hdc, iconCenterX - 3, iconCenterY - 3, iconCenterX + 4, iconCenterY + 4);
    } else if (btn->iconType == 3) {
        POINT pts[5] = {
            { iconCenterX - 6, iconCenterY - 8 },
            { iconCenterX + 2, iconCenterY - 8 },
            { iconCenterX + 6, iconCenterY - 4 },
            { iconCenterX + 6, iconCenterY + 8 },
            { iconCenterX - 6, iconCenterY + 8 }
        };
        Polygon(hdc, pts, 5);
        MoveToEx(hdc, iconCenterX - 3, iconCenterY, NULL);
        LineTo(hdc, iconCenterX + 3, iconCenterY);
        MoveToEx(hdc, iconCenterX - 3, iconCenterY + 4, NULL);
        LineTo(hdc, iconCenterX + 3, iconCenterY + 4);
    } else if (btn->iconType == 4) {
        MoveToEx(hdc, iconCenterX - 5, iconCenterY - 5, NULL);
        LineTo(hdc, iconCenterX + 6, iconCenterY + 6);
        MoveToEx(hdc, iconCenterX + 5, iconCenterY - 5, NULL);
        LineTo(hdc, iconCenterX - 6, iconCenterY + 6);
    }
    SelectObject(hdc, oGP);
    DeleteObject(hGlyphPen);

    SetBkMode(hdc, TRANSPARENT);
    int textLeft = iconCenterX + 18;

    if (btn->sublabel && btn->sublabel[0] != L'\0') {
        RECT lblRc = { textLeft, y + (h / 2) - 18, x + w - 12, y + (h / 2) + 2 };
        HGDIOBJ oF = SelectObject(hdc, g_hFontNormal);
        SetTextColor(hdc, isHovered ? (isLight ? RGB(0, 0, 0) : RGB(255, 255, 255))
                                    : (isLight ? RGB(15, 23, 42) : RGB(241, 245, 249)));
        DrawTextW(hdc, btn->label, -1, &lblRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        RECT subRc = { textLeft, y + (h / 2) + 1, x + w - 12, y + (h / 2) + 18 };
        SelectObject(hdc, g_hFontSmall);
        SetTextColor(hdc, isLight ? RGB(100, 116, 139) : RGB(148, 163, 184));
        DrawTextW(hdc, btn->sublabel, -1, &subRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oF);
    } else {
        RECT lblRc = { textLeft, y, x + w - 12, y + h };
        HGDIOBJ oF = SelectObject(hdc, g_hFontNormal);
        SetTextColor(hdc, isHovered ? (isLight ? RGB(0, 0, 0) : RGB(255, 255, 255))
                                    : (isLight ? RGB(15, 23, 42) : RGB(241, 245, 249)));
        DrawTextW(hdc, btn->label, -1, &lblRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oF);
    }
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
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            BOOL isLight = Magify_IsSystemLightTheme();
            BOOL dark = !isLight;
            DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            POINT pt = { mx, my };

            int hit = -1;
            for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
                if (PtInRect(&g_dashButtons[i].rc, pt)) {
                    hit = i;
                    break;
                }
            }

            if (hit != g_hoveredBtn) {
                g_hoveredBtn = hit;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            if (!g_mouseTracking) {
                TRACKMOUSEEVENT tme = { sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
                TrackMouseEvent(&tme);
                g_mouseTracking = TRUE;
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            g_mouseTracking = FALSE;
            if (g_hoveredBtn != -1) {
                g_hoveredBtn = -1;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            POINT pt = { mx, my };

            for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
                if (PtInRect(&g_dashButtons[i].rc, pt)) {
                    g_pressedBtn = i;
                    SetCapture(hwnd);
                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_pressedBtn != -1) {
                int clickedIdx = g_pressedBtn;
                g_pressedBtn = -1;
                ReleaseCapture();
                InvalidateRect(hwnd, NULL, FALSE);

                int mx = GET_X_LPARAM(lParam);
                int my = GET_Y_LPARAM(lParam);
                POINT pt = { mx, my };

                if (PtInRect(&g_dashButtons[clickedIdx].rc, pt)) {
                    int btnId = g_dashButtons[clickedIdx].id;
                    if (btnId == BTN_TOGGLE_REG) {
                        if (Magify_IsRegistered()) {
                            Magify_UnregisterContextMenu();
                        } else {
                            Magify_RegisterContextMenu();
                        }
                        InvalidateRect(hwnd, NULL, FALSE);
                    } else if (btnId == BTN_TEST_IMAGE) {
                        LaunchTestOverlay(FILE_CLASS_IMAGE);
                    } else if (btnId == BTN_TEST_PDF) {
                        LaunchTestOverlay(FILE_CLASS_PDF);
                    } else if (btnId == BTN_CLOSE) {
                        DestroyWindow(hwnd);
                    }
                }
            }
            return 0;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) == HTCLIENT && g_hoveredBtn != -1) {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
            break;
        }



        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT cr;
            GetClientRect(hwnd, &cr);
            int w = cr.right - cr.left;
            int h = cr.bottom - cr.top;

            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBmp = CreateCompatibleBitmap(hdc, w, h);
            HGDIOBJ oldBmp = SelectObject(memDC, memBmp);

            BOOL isLight = Magify_IsSystemLightTheme();
            COLORREF bgCol = isLight ? RGB(236, 240, 246) : RGB(22, 24, 31);
            HBRUSH hBgBrush = CreateSolidBrush(bgCol);
            FillRect(memDC, &cr, hBgBrush);
            DeleteObject(hBgBrush);

            // 1. Header Section
            SetBkMode(memDC, TRANSPARENT);
            SetTextColor(memDC, isLight ? RGB(14, 116, 144) : RGB(34, 211, 238));
            HGDIOBJ oldFont = SelectObject(memDC, g_hFontTitle);
            RECT titleRc = { 32, 22, 500, 50 };
            DrawTextW(memDC, L"MAGIFY", -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            SetTextColor(memDC, isLight ? RGB(71, 85, 105) : RGB(148, 163, 184));
            SelectObject(memDC, g_hFontSmall);
            RECT subRc = { 32, 52, 500, 72 };
            DrawTextW(memDC, L"120fps Instant Radial Utilities for Images & PDFs • Native C11", -1, &subRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            // 2. Status Panel (Tactile Neumorphic Indented Panel)
            RECT cardRc = { 32, 80, 498, 168 };
            DrawNeumorphicCard(memDC, &cardRc, 14, isLight, TRUE);

            BOOL isReg = Magify_IsRegistered();
            COLORREF statusAccent = isReg ? (isLight ? RGB(5, 150, 105) : RGB(16, 185, 129))
                                          : (isLight ? RGB(225, 29, 72)  : RGB(244, 63, 94));

            // Glowing Circular LED Indicator
            int ledCenterX = 56;
            int ledCenterY = 114;

            // Outer Soft Halo
            HBRUSH hLedHalo = CreateSolidBrush(isLight ? (isReg ? RGB(209, 250, 229) : RGB(255, 228, 230))
                                                       : (isReg ? RGB(6, 78, 59)     : RGB(136, 19, 55)));
            HPEN hPenNone = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ oB = SelectObject(memDC, hLedHalo);
            HGDIOBJ oP = SelectObject(memDC, hPenNone);
            Ellipse(memDC, ledCenterX - 11, ledCenterY - 11, ledCenterX + 12, ledCenterY + 12);

            // Inner Bright LED
            HBRUSH hLedCore = CreateSolidBrush(statusAccent);
            HPEN hLedBorder = CreatePen(PS_SOLID, 1, statusAccent);
            SelectObject(memDC, hLedCore);
            SelectObject(memDC, hLedBorder);
            Ellipse(memDC, ledCenterX - 5, ledCenterY - 5, ledCenterX + 6, ledCenterY + 6);
            SelectObject(memDC, oB);
            SelectObject(memDC, oP);
            DeleteObject(hLedHalo);
            DeleteObject(hLedCore);
            DeleteObject(hLedBorder);
            DeleteObject(hPenNone);

            // Status Texts
            RECT statTitleRc = { 76, 94, 480, 118 };
            SelectObject(memDC, g_hFontNormal);
            SetTextColor(memDC, statusAccent);
            DrawTextW(memDC, isReg ? L"Explorer Context Menu: ACTIVE (Background Daemon Active)" : L"Explorer Context Menu: NOT INSTALLED",
                      -1, &statTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            RECT statDescRc = { 76, 118, 480, 158 };
            SelectObject(memDC, g_hFontSmall);
            SetTextColor(memDC, isLight ? RGB(100, 116, 139) : RGB(148, 163, 184));
            DrawTextW(memDC, isReg ? L"Auto-starts on boot. Right-click any Image or PDF anytime in Explorer!\nYou don't need to keep this window open — it works automatically."
                                   : L"Click 'Install Right-Click Menu' below to enable the context menu\nfor instant file conversions, batch resizing, and PDF editing.",
                      -1, &statDescRc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);

            // 3. Update Toggle Button Text & Accent dynamically
            if (isReg) {
                g_dashButtons[0].label = L"Uninstall Right-Click Menu";
                g_dashButtons[0].sublabel = L"Remove Magify from Windows File Explorer";
                g_dashButtons[0].accent = RGB(244, 63, 94);
            } else {
                g_dashButtons[0].label = L"Install Right-Click Menu";
                g_dashButtons[0].sublabel = L"Add 'Modify with Magify' to Explorer right-click";
                g_dashButtons[0].accent = RGB(16, 185, 129);
            }

            // 4. Render 3D Neumorphic Buttons
            for (int i = 0; i < NUM_DASH_BUTTONS; i++) {
                BOOL isHov = (g_hoveredBtn == i);
                BOOL isPrs = (g_pressedBtn == i);
                DrawNeumorphicButton(memDC, &g_dashButtons[i], isHov, isPrs, isLight);
            }

            // 5. Minimalist Footer
            RECT footRc = { 32, 416, 498, 436 };
            SelectObject(memDC, g_hFontMicro);
            SetTextColor(memDC, isLight ? RGB(148, 163, 184) : RGB(100, 116, 139));
            DrawTextW(memDC, L"Magify v1.0 • Pure C11 Native • 120 FPS Radial Utilities • github.com/Beeplap/Magify",
                      -1, &footRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

            BitBlt(hdc, 0, 0, w, h, memDC, 0, 0, SRCCOPY);

            SelectObject(memDC, oldFont);
            SelectObject(memDC, oldBmp);
            DeleteObject(memBmp);
            DeleteDC(memDC);

            EndPaint(hwnd, &ps);
            return 0;


        }

        case WM_DESTROY: {
            DeleteDashboardFonts();
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static void ShowDashboard(HINSTANCE hInst) {
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

    int clientW = 530;
    int clientH = 450;
    RECT wr = { 0, 0, clientW, clientH };
    AdjustWindowRectEx(&wr, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_APPWINDOW);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

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

    DeleteDashboardFonts();
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

    // Check Background Daemon Flag
    BOOL isBackground = FALSE;
    for (int i = 1; i < argc; i++) {
        if (_wcsicmp(argv[i], L"--background") == 0 || _wcsicmp(argv[i], L"-b") == 0 ||
            _wcsicmp(argv[i], L"--daemon") == 0 || _wcsicmp(argv[i], L"-d") == 0) {
            isBackground = TRUE;
            break;
        }
    }

    if (isBackground) {
        if (Magify_IPC_IsServerRunning()) {
            LocalFree(argv);
            return 0; // Already running in background
        }
        LocalFree(argv);

        MagifyContext bgCtx;
        memset(&bgCtx, 0, sizeof(bgCtx));
        bgCtx.hInstance = hInstance;
        bgCtx.isBackgroundMode = TRUE;

        Magify_IPC_StartServer(&bgCtx);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) {
            if (msg.message == WM_USER + 105) {
                GetCursorPos(&bgCtx.spawnCursor);
                bgCtx.dpiScale = Magify_GetDpiScaleForPoint(bgCtx.spawnCursor);
                bgCtx.overlayWidth = (int)(MAGIFY_RADIAL_WIDTH * bgCtx.dpiScale);
                bgCtx.overlayHeight = (int)(MAGIFY_RADIAL_HEIGHT * bgCtx.dpiScale);
                bgCtx.uiState = UI_STATE_RADIAL_ROOT;
                bgCtx.activeSubmenu = SUBMENU_NONE;
                bgCtx.hoveredSlice = -1;
                bgCtx.jobProgress = 0;
                bgCtx.progressPercent = 0;
                bgCtx.isErrorState = FALSE;
                bgCtx.statusText[0] = L'\0';
                Magify_ShowOverlay(&bgCtx);
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        Magify_IPC_StopServer();
        return 0;
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
