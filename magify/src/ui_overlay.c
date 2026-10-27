#include "ui_overlay.h"
#include "ops_common.h"
#include "ops_image.h"
#include "ops_pdf.h"
#include <math.h>
#include <stdio.h>
#include <shlwapi.h>
#include <commdlg.h>

#define MAGIFY_PI 3.14159265358979323846f
#define TIMER_ANIM_ID 1
#define TIMER_CARET_ID 2
#define ANIM_INTERVAL_MS 8 // ~120 fps

typedef struct {
    WCHAR title[32];
    WCHAR subtitle[32];
} SliceDesc;

static MagifyContext *g_overlayCtx = NULL;
static HDC g_hMemDC = NULL;
static HBITMAP g_hDIB = NULL;
static uint32_t *g_pixels = NULL;
static int g_surfWidth = 0;
static int g_surfHeight = 0;

// Scratch DC for antialiased text
static HDC g_hTextDC = NULL;
static HBITMAP g_hTextDIB = NULL;
static uint32_t *g_textPixels = NULL;
static const int TEXT_SCRATCH_W = 512;
static const int TEXT_SCRATCH_H = 128;

// Smooth hover expansion tracking
static float g_sliceHover[8] = {0};
static float g_hubHover = 0.0f;

static BOOL s_themeCached = FALSE;
static BOOL s_cachedIsLight = FALSE;
static DWORD s_lastThemeQueryTick = 0;

float Magify_GetDpiScaleForPoint(POINT pt) {
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *GetDpiForMonitorFn)(HMONITOR, int, UINT*, UINT*);
        GetDpiForMonitorFn fn = (GetDpiForMonitorFn)(void*)GetProcAddress(hShcore, "GetDpiForMonitor");
        if (fn) {
            UINT dpiX = 96, dpiY = 96;
            if (SUCCEEDED(fn(hMon, 0 /* MDT_EFFECTIVE_DPI */, &dpiX, &dpiY)) && dpiY > 0) {
                FreeLibrary(hShcore);
                float s = (float)dpiY / 96.0f;
                return (s < 1.0f) ? 1.0f : s;
            }
        }
        FreeLibrary(hShcore);
    }
    // Fallback: system display DPI
    HDC hdc = GetDC(NULL);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    float s = (float)dpi / 96.0f;
    return (s < 1.0f) ? 1.0f : s;
}

float Magify_GetDpiScaleForWindow(HWND hwnd) {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef UINT (WINAPI *GetDpiForWindowFn)(HWND);
        GetDpiForWindowFn fn = (GetDpiForWindowFn)(void*)GetProcAddress(hUser32, "GetDpiForWindow");
        if (fn) {
            UINT dpi = fn(hwnd);
            if (dpi > 0) {
                float s = (float)dpi / 96.0f;
                return (s < 1.0f) ? 1.0f : s;
            }
        }
    }
    POINT pt = {0, 0};
    RECT rc;
    if (GetWindowRect(hwnd, &rc)) {
        pt.x = (rc.left + rc.right) / 2;
        pt.y = (rc.top + rc.bottom) / 2;
    }
    return Magify_GetDpiScaleForPoint(pt);
}

BOOL Magify_IsSystemLightTheme(void) {
    DWORD now = GetTickCount();
    if (s_themeCached && (now - s_lastThemeQueryTick < 2000)) {
        return s_cachedIsLight;
    }

    HKEY hKey;
    DWORD value = 0;
    DWORD size = sizeof(value);
    LONG res = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey);
    if (res == ERROR_SUCCESS) {
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&value, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            s_cachedIsLight = (value != 0);
            s_themeCached = TRUE;
            s_lastThemeQueryTick = now;
            return s_cachedIsLight;
        }
        RegCloseKey(hKey);
    }
    s_cachedIsLight = FALSE; // Default to dark mode
    s_themeCached = TRUE;
    s_lastThemeQueryTick = now;
    return FALSE;
}

MagifyThemePalette Magify_GetActiveTheme(void) {
    MagifyThemePalette p;
    memset(&p, 0, sizeof(p));
    p.isLightMode = Magify_IsSystemLightTheme();

    if (p.isLightMode) {
        // Light Mode: Background #F8FAFC (Alpha 92%), Text #0F172A, Border/Glow #6D28D9 -> #0891B2, Slices #E2E8F0
        p.hubBgR = 248; p.hubBgG = 250; p.hubBgB = 252; p.hubBgA = 240;
        p.hubBdrR = 109; p.hubBdrG = 40; p.hubBdrB = 217; // Deep Violet #6D28D9
        p.sliceNormR = 226; p.sliceNormG = 232; p.sliceNormB = 240; p.sliceNormA = 235; // Slate #E2E8F0
        p.sliceHovR1 = 109; p.sliceHovG1 = 40; p.sliceHovB1 = 217;
        p.sliceHovR2 = 8; p.sliceHovG2 = 145; p.sliceHovB2 = 178; // Cyan #0891B2
        p.glowR = 8; p.glowG = 145; p.glowB = 178;
        p.trackR = 203; p.trackG = 213; p.trackB = 225; p.trackA = 140; // Slate 300
        p.textPrimary = RGB(15, 23, 42); // Slate 900
        p.textSecondary = RGB(71, 85, 105); // Slate 600
        p.textAccent = RGB(8, 145, 178); // Cyan 600
        p.cardBg = RGB(248, 250, 252);
        p.cardBorder = RGB(109, 40, 217);
    } else {
        // Dark Mode: Background #12131A (Alpha 88%), Text #F8FAFC, Border/Glow #7C3AED -> #22D3EE, Slices #1E2230
        p.hubBgR = 18; p.hubBgG = 19; p.hubBgB = 26; p.hubBgA = 235;
        p.hubBdrR = 124; p.hubBdrG = 58; p.hubBdrB = 237; // Electric Violet #7C3AED
        p.sliceNormR = 30; p.sliceNormG = 34; p.sliceNormB = 48; p.sliceNormA = 228; // #1E2230
        p.sliceHovR1 = 124; p.sliceHovG1 = 58; p.sliceHovB1 = 237;
        p.sliceHovR2 = 6; p.sliceHovG2 = 182; p.sliceHovB2 = 212; // Cyan #06B6D4
        p.glowR = 34; p.glowG = 211; p.glowB = 238; // Cyber Cyan #22D3EE
        p.trackR = 30; p.trackG = 41; p.trackB = 59; p.trackA = 90; // Slate 800
        p.textPrimary = RGB(248, 250, 252);
        p.textSecondary = RGB(148, 163, 184);
        p.textAccent = RGB(34, 211, 238);
        p.cardBg = RGB(18, 19, 26);
        p.cardBorder = RGB(124, 58, 237);
    }
    return p;
}

// Forward declarations
static void ResizeOverlayWindow(MagifyContext *ctx, int newW, int newH);
static void InitRenameHUD(MagifyContext *ctx);
static void InitCropHUD(MagifyContext *ctx);
static void InitPdfOrganizerHUD(MagifyContext *ctx);
static void RenderPdfOrganizerHUD(MagifyContext *ctx);
static void HandlePdfOrganizerMouseMove(MagifyContext *ctx, int mx, int my);
static void HandlePdfOrganizerMouseDown(MagifyContext *ctx, int mx, int my);
static void HandlePdfOrganizerMouseUp(MagifyContext *ctx);
static void HandlePdfOrganizerMouseWheel(MagifyContext *ctx, int delta);
static void HandlePdfOrganizerKeyDown(MagifyContext *ctx, WPARAM wParam);

int Magify_HitTestRadial(float mouseX, float mouseY, float centerX, float centerY,
                         float innerR, float outerR, int sliceCount, float rotationOffsetDeg) {
    float dx = mouseX - centerX;
    float dy = mouseY - centerY;
    float dist = sqrtf(dx * dx + dy * dy);

    if (dist < innerR) return -1; // Center Hub
    if (dist > outerR) return -2; // Outside Ring

    float angleDeg = atan2f(dy, dx) * (180.0f / MAGIFY_PI);
    if (angleDeg < 0.0f) angleDeg += 360.0f;

    angleDeg = fmodf(angleDeg - rotationOffsetDeg + 360.0f, 360.0f);
    float sliceSpan = 360.0f / (float)sliceCount;
    int idx = (int)(angleDeg / sliceSpan);
    if (idx >= sliceCount) idx = sliceCount - 1;
    return idx;
}

static inline float ClampFloat(float v, float mn, float mx) {
    if (v < mn) return mn;
    if (v > mx) return mx;
    return v;
}

static inline uint32_t PremultiplyARGB(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    if (a == 0) return 0;
    float af = (float)a / 255.0f;
    uint8_t pr = (uint8_t)((float)r * af);
    uint8_t pg = (uint8_t)((float)g * af);
    uint8_t pb = (uint8_t)((float)b * af);
    return ((uint32_t)a << 24) | ((uint32_t)pr << 16) | ((uint32_t)pg << 8) | (uint32_t)pb;
}

static inline uint32_t AlphaBlendPremul(uint32_t dst, uint32_t src) {
    uint8_t sa = (uint8_t)(src >> 24);
    if (sa == 0) return dst;
    if (sa == 255) return src;

    float invA = 1.0f - ((float)sa / 255.0f);
    uint8_t dr = (uint8_t)(dst >> 16);
    uint8_t dg = (uint8_t)(dst >> 8);
    uint8_t db = (uint8_t)dst;
    uint8_t da = (uint8_t)(dst >> 24);

    uint8_t sr = (uint8_t)(src >> 16);
    uint8_t sg = (uint8_t)(src >> 8);
    uint8_t sb = (uint8_t)src;

    uint8_t outR = (uint8_t)(sr + (float)dr * invA);
    uint8_t outG = (uint8_t)(sg + (float)dg * invA);
    uint8_t outB = (uint8_t)(sb + (float)db * invA);
    uint8_t outA = (uint8_t)(sa + (float)da * invA);

    return ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
}

static void InitDIB(HDC hdcScreen, int width, int height) {
    if (g_pixels && g_surfWidth == width && g_surfHeight == height) return;

    if (g_hDIB) { DeleteObject(g_hDIB); g_hDIB = NULL; }
    if (g_hMemDC) { DeleteDC(g_hMemDC); g_hMemDC = NULL; }

    g_surfWidth = width;
    g_surfHeight = height;
    g_hMemDC = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_hDIB = CreateDIBSection(g_hMemDC, &bmi, DIB_RGB_COLORS, (void**)&g_pixels, NULL, 0);
    SelectObject(g_hMemDC, g_hDIB);

    if (!g_hTextDC) {
        g_hTextDC = CreateCompatibleDC(hdcScreen);
        BITMAPINFO tbmi = {0};
        tbmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        tbmi.bmiHeader.biWidth = TEXT_SCRATCH_W;
        tbmi.bmiHeader.biHeight = -TEXT_SCRATCH_H;
        tbmi.bmiHeader.biPlanes = 1;
        tbmi.bmiHeader.biBitCount = 32;
        tbmi.bmiHeader.biCompression = BI_RGB;
        g_hTextDIB = CreateDIBSection(g_hTextDC, &tbmi, DIB_RGB_COLORS, (void**)&g_textPixels, NULL, 0);
        SelectObject(g_hTextDC, g_hTextDIB);
    }
}

static void DrawCrispTextARGB(const WCHAR *text, int xCenter, int yCenter,
                              COLORREF textColor, int fontSize, int fontWeight, float alpha, BOOL leftAlign) {
    if (!text || text[0] == L'\0' || alpha <= 0.01f || !g_textPixels) return;

    memset(g_textPixels, 0, TEXT_SCRATCH_W * TEXT_SCRATCH_H * sizeof(uint32_t));

    HFONT hFont = CreateFontW(
        -fontSize, 0, 0, 0, fontWeight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    HGDIOBJ hOldFont = SelectObject(g_hTextDC, hFont);

    RECT rc = { 0, 0, TEXT_SCRATCH_W, TEXT_SCRATCH_H };
    SetBkMode(g_hTextDC, TRANSPARENT);
    SetTextColor(g_hTextDC, RGB(255, 255, 255));
    UINT format = (leftAlign ? DT_LEFT : DT_CENTER) | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    DrawTextW(g_hTextDC, text, -1, &rc, format);

    SelectObject(g_hTextDC, hOldFont);
    DeleteObject(hFont);

    int dstStartX = leftAlign ? xCenter : (xCenter - (TEXT_SCRATCH_W / 2));
    int dstStartY = yCenter - (TEXT_SCRATCH_H / 2);

    uint8_t cr = GetRValue(textColor);
    uint8_t cg = GetGValue(textColor);
    uint8_t cb = GetBValue(textColor);

    for (int sy = 0; sy < TEXT_SCRATCH_H; sy++) {
        int dy = dstStartY + sy;
        if (dy < 0 || dy >= g_surfHeight) continue;

        for (int sx = 0; sx < TEXT_SCRATCH_W; sx++) {
            int dx = dstStartX + sx;
            if (dx < 0 || dx >= g_surfWidth) continue;

            uint32_t srcVal = g_textPixels[sy * TEXT_SCRATCH_W + sx];
            uint8_t gray = (uint8_t)(srcVal & 0xFF);
            if (gray == 0) continue;

            float factor = ((float)gray / 255.0f) * alpha;
            uint8_t outA = (uint8_t)(255.0f * factor);
            uint8_t outR = (uint8_t)((float)cr * factor);
            uint8_t outG = (uint8_t)((float)cg * factor);
            uint8_t outB = (uint8_t)((float)cb * factor);

            uint32_t srcPremul = ((uint32_t)outA << 24) | ((uint32_t)outR << 16) | ((uint32_t)outG << 8) | (uint32_t)outB;
            int idx = dy * g_surfWidth + dx;
            g_pixels[idx] = AlphaBlendPremul(g_pixels[idx], srcPremul);
        }
    }
}

static void DrawRoundedBoxARGB(int x, int y, int w, int h, float radius,
                               uint8_t bgR, uint8_t bgG, uint8_t bgB, uint8_t bgA,
                               uint8_t bdrR, uint8_t bdrG, uint8_t bdrB, uint8_t bdrA, float bdrWidth) {
    float x0 = (float)x, y0 = (float)y;
    float x1 = (float)(x + w), y1 = (float)(y + h);

    int minY = (y < 0) ? 0 : y;
    int maxY = (y + h >= g_surfHeight) ? g_surfHeight - 1 : y + h;
    int minX = (x < 0) ? 0 : x;
    int maxX = (x + w >= g_surfWidth) ? g_surfWidth - 1 : x + w;

    for (int py = minY; py <= maxY; py++) {
        float dy = 0.0f;
        if ((float)py < y0 + radius) dy = (y0 + radius) - (float)py;
        else if ((float)py > y1 - radius) dy = (float)py - (y1 - radius);

        for (int px = minX; px <= maxX; px++) {
            float dx = 0.0f;
            if ((float)px < x0 + radius) dx = (x0 + radius) - (float)px;
            else if ((float)px > x1 - radius) dx = (float)px - (x1 - radius);

            float dist = sqrtf(dx * dx + dy * dy);
            if (dist > radius + 0.5f) continue;

            float innerDist = radius - dist;
            float cov = ClampFloat(innerDist + 0.5f, 0.0f, 1.0f);

            int pIdx = py * g_surfWidth + px;
            if (innerDist < bdrWidth) {
                float bCov = ClampFloat((bdrWidth - innerDist) + 0.5f, 0.0f, 1.0f) * cov;
                uint8_t a = (uint8_t)((float)bdrA * bCov);
                g_pixels[pIdx] = AlphaBlendPremul(g_pixels[pIdx], PremultiplyARGB(a, bdrR, bdrG, bdrB));
            } else {
                uint8_t a = (uint8_t)((float)bgA * cov);
                g_pixels[pIdx] = AlphaBlendPremul(g_pixels[pIdx], PremultiplyARGB(a, bgR, bgG, bgB));
            }
        }
    }
}

static int GetSubmenuSliceCount(MagifyContext *ctx) {
    if (ctx->activeSubmenu == SUBMENU_CONVERT) {
        if (ctx->primaryClass == FILE_CLASS_IMAGE) return 5; // PNG, JPG, BMP, ICO, PDF
        if (ctx->primaryClass == FILE_CLASS_PDF)   return 4; // JPG, PNG, Text, Back
        return 2;
    }
    if (ctx->activeSubmenu == SUBMENU_EDIT) {
        if (ctx->primaryClass == FILE_CLASS_IMAGE) return 5; // Rotate CW, CCW, Flip H, Resize, Crop
        if (ctx->primaryClass == FILE_CLASS_PDF)   return 5; // Page Organizer, Rotate CW, Rotate CCW, Split, Merge
        return 2;
    }
    if (ctx->activeSubmenu == SUBMENU_COMPRESS) {
        if (ctx->primaryClass == FILE_CLASS_IMAGE) return 4; // Lossless, 80%, 60%, <200KB
        if (ctx->primaryClass == FILE_CLASS_PDF)   return 4; // Light 300, Balanced 150, Extreme 96, Back
        return 1;
    }
    return 4;
}

static void GetSubmenuSlices(MagifyContext *ctx, SliceDesc *descs, int maxCount) {
    for (int i = 0; i < maxCount; i++) {
        descs[i].title[0] = L'\0';
        descs[i].subtitle[0] = L'\0';
    }

    if (ctx->activeSubmenu == SUBMENU_CONVERT) {
        if (ctx->primaryClass == FILE_CLASS_IMAGE) {
            wcscpy(descs[0].title, L"PNG");   wcscpy(descs[0].subtitle, L"Lossless");
            wcscpy(descs[1].title, L"JPG");   wcscpy(descs[1].subtitle, L"Compact 92%");
            wcscpy(descs[2].title, L"BMP");   wcscpy(descs[2].subtitle, L"Raw 24-bit");
            wcscpy(descs[3].title, L"ICO");   wcscpy(descs[3].subtitle, L"Multi-Size");
            wcscpy(descs[4].title, L"PDF");   wcscpy(descs[4].subtitle, L"Document");
        } else if (ctx->primaryClass == FILE_CLASS_PDF) {
            wcscpy(descs[0].title, L"JPG Pages"); wcscpy(descs[0].subtitle, L"Export");
            wcscpy(descs[1].title, L"PNG Pages"); wcscpy(descs[1].subtitle, L"300 DPI");
            wcscpy(descs[2].title, L"Extract");   wcscpy(descs[2].subtitle, L"Plain Text");
            wcscpy(descs[3].title, L"Back");      wcscpy(descs[3].subtitle, L"Return");
        } else {
            wcscpy(descs[0].title, L"ZIP");    wcscpy(descs[0].subtitle, L"Archive");
            wcscpy(descs[1].title, L"Cancel"); wcscpy(descs[1].subtitle, L"Return");
        }
    } else if (ctx->activeSubmenu == SUBMENU_EDIT) {
        if (ctx->primaryClass == FILE_CLASS_IMAGE) {
            wcscpy(descs[0].title, L"Rotate 90°");  wcscpy(descs[0].subtitle, L"Clockwise");
            wcscpy(descs[1].title, L"Rotate -90°"); wcscpy(descs[1].subtitle, L"Counter-CW");
            wcscpy(descs[2].title, L"Flip H");      wcscpy(descs[2].subtitle, L"Mirror");
            wcscpy(descs[3].title, L"Resize");      wcscpy(descs[3].subtitle, L"50% / 1080p");
            wcscpy(descs[4].title, L"Crop");        wcscpy(descs[4].subtitle, L"Aspect HUD");
        } else if (ctx->primaryClass == FILE_CLASS_PDF) {
            wcscpy(descs[0].title, L"Organizer");   wcscpy(descs[0].subtitle, L"Visual HUD");
            wcscpy(descs[1].title, L"Rotate 90°");  wcscpy(descs[1].subtitle, L"Clockwise");
            wcscpy(descs[2].title, L"Rotate -90°"); wcscpy(descs[2].subtitle, L"Counter-CW");
            wcscpy(descs[3].title, L"Split Pages"); wcscpy(descs[3].subtitle, L"Burst");
            wcscpy(descs[4].title, L"Merge PDFs");  wcscpy(descs[4].subtitle, L"Combine");
        }
    } else if (ctx->activeSubmenu == SUBMENU_COMPRESS) {
        if (ctx->primaryClass == FILE_CLASS_IMAGE) {
            wcscpy(descs[0].title, L"Lossless");  wcscpy(descs[0].subtitle, L"Deflate Q90");
            wcscpy(descs[1].title, L"Balanced");  wcscpy(descs[1].subtitle, L"80% Quality");
            wcscpy(descs[2].title, L"Maximum");   wcscpy(descs[2].subtitle, L"60% Web");
            wcscpy(descs[3].title, L"< 200 KB");  wcscpy(descs[3].subtitle, L"Portal Fit");
        } else if (ctx->primaryClass == FILE_CLASS_PDF) {
            wcscpy(descs[0].title, L"Light");    wcscpy(descs[0].subtitle, L"300 DPI");
            wcscpy(descs[1].title, L"Balanced"); wcscpy(descs[1].subtitle, L"150 DPI");
            wcscpy(descs[2].title, L"Extreme");  wcscpy(descs[2].subtitle, L"96 DPI");
            wcscpy(descs[3].title, L"Back");     wcscpy(descs[3].subtitle, L"Return");
        } else {
            wcscpy(descs[0].title, L"Zip Deflate"); wcscpy(descs[0].subtitle, L"Standard");
        }
    }
}

// -------------------------------------------------------------
// Rename HUD Implementation
// -------------------------------------------------------------
static void InitRenameHUD(MagifyContext *ctx) {
    ctx->uiState = UI_STATE_RENAME_HUD;
    const WCHAR *file = (ctx->fileCount > 0) ? ctx->files[0] : L"document.txt";
    const WCHAR *fName = PathFindFileNameW(file);
    const WCHAR *ext = PathFindExtensionW(file);

    wcsncpy(ctx->renameExt, ext, 31);
    ctx->renameExt[31] = L'\0';

    wcsncpy(ctx->renameBuf, fName, MAX_PATH - 1);
    ctx->renameBuf[MAX_PATH - 1] = L'\0';
    PathRemoveExtensionW(ctx->renameBuf);

    ctx->renameCursor = (int)wcslen(ctx->renameBuf);
    ctx->renameSelStart = 0;
    ctx->renameSelEnd = ctx->renameCursor;
    ctx->renameCaretVisible = TRUE;
}

static void RenderRenameHUD(MagifyContext *ctx) {
    float cx = (float)g_surfWidth * 0.5f;
    float cy = (float)g_surfHeight * 0.5f;

    int pillW = 410;
    int pillH = 124;
    int px = (int)(cx - (float)pillW * 0.5f);
    int py = (int)(cy - (float)pillH * 0.5f);

    const MagifyThemePalette *th = &ctx->themePalette;

    // Container pill
    DrawRoundedBoxARGB(px, py, pillW, pillH, 18.0f,
                       th->hubBgR, th->hubBgG, th->hubBgB, th->hubBgA,
                       th->hubBdrR, th->hubBdrG, th->hubBdrB, 220,
                       1.5f);

    // Header label & hints
    DrawCrispTextARGB(L"RENAME FILE", px + 22, py + 16, th->textAccent, 11, FW_BOLD, 1.0f, TRUE);
    DrawCrispTextARGB(L"Enter to apply • Esc to cancel", px + pillW - 170, py + 16, th->textSecondary, 9, FW_NORMAL, 1.0f, TRUE);

    // Inner text input box
    int bx = px + 18;
    int by = py + 36;
    int bw = pillW - 36;
    int bh = 38;

    if (th->isLightMode) {
        DrawRoundedBoxARGB(bx, by, bw, bh, 8.0f,
                           241, 245, 249, 255,   // Light input #F1F5F9
                           203, 213, 225, 200,   // Slate 300
                           1.0f);
    } else {
        DrawRoundedBoxARGB(bx, by, bw, bh, 8.0f,
                           28, 32, 46, 255,   // Dark input surface #1C202E
                           51, 65, 85, 200,   // Slate border
                           1.0f);
    }

    // Text & caret rendering
    int textStartX = bx + 12;
    int textY = by + 19;

    // Selection highlight
    BOOL hasSel = (ctx->renameSelStart != ctx->renameSelEnd);
    if (hasSel) {
        int selW = (int)wcslen(ctx->renameBuf) * 8 + 4;
        if (selW > bw - 70) selW = bw - 70;
        DrawRoundedBoxARGB(textStartX - 2, by + 8, selW, 22, 4.0f,
                           124, 58, 237, 180, 124, 58, 237, 255, 1.0f);
    }

    COLORREF txtCol = hasSel ? (th->isLightMode ? RGB(0, 0, 0) : RGB(255, 255, 255)) : th->textPrimary;
    DrawCrispTextARGB(ctx->renameBuf, textStartX, textY, txtCol, 12, FW_SEMIBOLD, 1.0f, TRUE);

    // Caret cursor
    if (ctx->renameCaretVisible && !hasSel) {
        int caretX = textStartX + ctx->renameCursor * 8;
        if (caretX > bx + bw - 70) caretX = bx + bw - 70;
        uint8_t carR = GetRValue(th->textAccent);
        uint8_t carG = GetGValue(th->textAccent);
        uint8_t carB = GetBValue(th->textAccent);
        for (int y = by + 9; y <= by + 28; y++) {
            if (y >= 0 && y < g_surfHeight && caretX >= 0 && caretX < g_surfWidth) {
                g_pixels[y * g_surfWidth + caretX] = PremultiplyARGB(255, carR, carG, carB);
                g_pixels[y * g_surfWidth + caretX + 1] = PremultiplyARGB(255, carR, carG, carB);
            }
        }
    }

    // Locked extension badge
    int badgeW = 54;
    int badgeH = 26;
    int badgeX = bx + bw - badgeW - 6;
    int badgeY = by + 6;
    if (th->isLightMode) {
        DrawRoundedBoxARGB(badgeX, badgeY, badgeW, badgeH, 6.0f,
                           224, 242, 254, 230,   // Light Cyan #E0F2FE
                           8, 145, 178, 200,     // #0891B2
                           1.0f);
        DrawCrispTextARGB(ctx->renameExt, badgeX + (badgeW / 2), badgeY + 13, RGB(8, 145, 178), 10, FW_BOLD, 1.0f, FALSE);
    } else {
        DrawRoundedBoxARGB(badgeX, badgeY, badgeW, badgeH, 6.0f,
                           22, 78, 99, 230,   // #164E63
                           6, 182, 212, 200,  // #06B6D4
                           1.0f);
        DrawCrispTextARGB(ctx->renameExt, badgeX + (badgeW / 2), badgeY + 13, RGB(34, 211, 238), 10, FW_BOLD, 1.0f, FALSE);
    }

    // Apply button
    int btnW = 100, btnH = 28;
    int btnX = px + pillW - btnW - 18;
    int btnY = py + 84;
    DrawRoundedBoxARGB(btnX, btnY, btnW, btnH, 8.0f,
                       16, 185, 129, 240, // Emerald #10B981
                       5, 150, 105, 255,
                       1.0f);
    DrawCrispTextARGB(L"Rename", btnX + (btnW / 2), btnY + 14, RGB(255, 255, 255), 11, FW_BOLD, 1.0f, FALSE);

    // Cancel button
    int cBtnW = 75;
    int cBtnX = btnX - cBtnW - 10;
    if (th->isLightMode) {
        DrawRoundedBoxARGB(cBtnX, btnY, cBtnW, btnH, 8.0f,
                           226, 232, 240, 200, 203, 213, 225, 180, 1.0f);
        DrawCrispTextARGB(L"Cancel", cBtnX + (cBtnW / 2), btnY + 14, th->textSecondary, 10, FW_SEMIBOLD, 1.0f, FALSE);
    } else {
        DrawRoundedBoxARGB(cBtnX, btnY, cBtnW, btnH, 8.0f,
                           30, 41, 59, 200,   // Slate #1E293B
                           71, 85, 105, 180,
                           1.0f);
        DrawCrispTextARGB(L"Cancel", cBtnX + (cBtnW / 2), btnY + 14, RGB(148, 163, 184), 10, FW_SEMIBOLD, 1.0f, FALSE);
    }
}

// -------------------------------------------------------------
// Interactive Crop HUD Implementation
// -------------------------------------------------------------
static void InitCropHUD(MagifyContext *ctx) {
    ctx->uiState = UI_STATE_IMAGE_EDIT;
    ctx->cropAspectLock = 0; // Free
    ctx->cropDragMode = -1;
    ctx->cropNormX = 0.10f;
    ctx->cropNormY = 0.10f;
    ctx->cropNormW = 0.80f;
    ctx->cropNormH = 0.80f;

    Magify_LoadImageDimensions(ctx->files[0], &ctx->cropOrigW, &ctx->cropOrigH);
    if (ctx->cropOrigW <= 0) ctx->cropOrigW = 1920;
    if (ctx->cropOrigH <= 0) ctx->cropOrigH = 1080;

    ResizeOverlayWindow(ctx, 680, 560);

    // Generate thumbnail preview
    int maxW = (int)(600 * ctx->dpiScale);
    int maxH = (int)(400 * ctx->dpiScale);
    if (ctx->cropPreviewPixels) free(ctx->cropPreviewPixels);
    ctx->cropPreviewPixels = (uint32_t*)malloc(maxW * maxH * sizeof(uint32_t));

    Magify_GeneratePreview(ctx->files[0], maxW, maxH,
                           ctx->cropPreviewPixels,
                           &ctx->cropPreviewW, &ctx->cropPreviewH);
}

static void RenderCropHUD(MagifyContext *ctx) {
    const MagifyThemePalette *th = &ctx->themePalette;

    // 1. Container frame
    DrawRoundedBoxARGB(10, 10, g_surfWidth - 20, g_surfHeight - 20, 18.0f,
                       th->hubBgR, th->hubBgG, th->hubBgB, th->hubBgA,
                       th->hubBdrR, th->hubBdrG, th->hubBdrB, 200,
                       1.5f);

    // 2. Header
    DrawCrispTextARGB(L"INTERACTIVE CROP & ASPECT RATIO", g_surfWidth / 2, 28, th->textAccent, 12, FW_BOLD, 1.0f, FALSE);

    int curCropPxW = (int)(ctx->cropNormW * (float)ctx->cropOrigW);
    int curCropPxH = (int)(ctx->cropNormH * (float)ctx->cropOrigH);
    WCHAR dimStr[64];
    swprintf(dimStr, 64, L"Orig: %dx%d px  ->  Crop: %dx%d px", ctx->cropOrigW, ctx->cropOrigH, curCropPxW, curCropPxH);
    DrawCrispTextARGB(dimStr, g_surfWidth / 2, 46, th->textPrimary, 10, FW_NORMAL, 1.0f, FALSE);

    // 3. Central Viewport & Preview Image
    int vpX = (g_surfWidth - ctx->cropPreviewW) / 2;
    int vpY = 62 + (400 - ctx->cropPreviewH) / 2;
    int vpW = ctx->cropPreviewW;
    int vpH = ctx->cropPreviewH;

    // Blit preview image pixels
    if (ctx->cropPreviewPixels && vpW > 0 && vpH > 0) {
        for (int y = 0; y < vpH; y++) {
            int dy = vpY + y;
            if (dy < 0 || dy >= g_surfHeight) continue;
            for (int x = 0; x < vpW; x++) {
                int dx = vpX + x;
                if (dx < 0 || dx >= g_surfWidth) continue;
                uint32_t p = ctx->cropPreviewPixels[y * vpW + x];
                g_pixels[dy * g_surfWidth + dx] = p;
            }
        }
    }

    // 4. Crop Box Screen Coordinates
    int scX = vpX + (int)(ctx->cropNormX * (float)vpW);
    int scY = vpY + (int)(ctx->cropNormY * (float)vpH);
    int scW = (int)(ctx->cropNormW * (float)vpW);
    int scH = (int)(ctx->cropNormH * (float)vpH);
    if (scW < 10) scW = 10;
    if (scH < 10) scH = 10;

    // Darkened outside mask (65% black alpha over areas outside crop rect in viewport)
    for (int y = 0; y < vpH; y++) {
        int dy = vpY + y;
        if (dy < 0 || dy >= g_surfHeight) continue;
        for (int x = 0; x < vpW; x++) {
            int dx = vpX + x;
            if (dx < 0 || dx >= g_surfWidth) continue;

            if (dx < scX || dx >= scX + scW || dy < scY || dy >= scY + scH) {
                int idx = dy * g_surfWidth + dx;
                uint32_t p = g_pixels[idx];
                uint8_t a = (uint8_t)(p >> 24);
                uint8_t r = (uint8_t)((p >> 16) * 0.35f);
                uint8_t g = (uint8_t)((p >> 8) * 0.35f);
                uint8_t b = (uint8_t)((p) * 0.35f);
                g_pixels[idx] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    // Cyan crop border
    for (int x = scX; x <= scX + scW; x++) {
        if (x >= 0 && x < g_surfWidth) {
            if (scY >= 0 && scY < g_surfHeight) {
                g_pixels[scY * g_surfWidth + x] = PremultiplyARGB(255, 34, 211, 238);
                if (scY + 1 < g_surfHeight) g_pixels[(scY + 1) * g_surfWidth + x] = PremultiplyARGB(255, 34, 211, 238);
            }
            int by = scY + scH;
            if (by >= 0 && by < g_surfHeight) {
                g_pixels[by * g_surfWidth + x] = PremultiplyARGB(255, 34, 211, 238);
                if (by - 1 >= 0) g_pixels[(by - 1) * g_surfWidth + x] = PremultiplyARGB(255, 34, 211, 238);
            }
        }
    }
    for (int y = scY; y <= scY + scH; y++) {
        if (y >= 0 && y < g_surfHeight) {
            if (scX >= 0 && scX < g_surfWidth) {
                g_pixels[y * g_surfWidth + scX] = PremultiplyARGB(255, 34, 211, 238);
                if (scX + 1 < g_surfWidth) g_pixels[y * g_surfWidth + scX + 1] = PremultiplyARGB(255, 34, 211, 238);
            }
            int rx = scX + scW;
            if (rx >= 0 && rx < g_surfWidth) {
                g_pixels[y * g_surfWidth + rx] = PremultiplyARGB(255, 34, 211, 238);
                if (rx - 1 >= 0) g_pixels[y * g_surfWidth + rx - 1] = PremultiplyARGB(255, 34, 211, 238);
            }
        }
    }

    // Rule of thirds grid lines
    int v1 = scX + scW / 3;
    int v2 = scX + (scW * 2) / 3;
    int h1 = scY + scH / 3;
    int h2 = scY + (scH * 2) / 3;
    for (int y = scY; y <= scY + scH; y++) {
        if (y >= 0 && y < g_surfHeight) {
            if (v1 >= 0 && v1 < g_surfWidth) g_pixels[y * g_surfWidth + v1] = AlphaBlendPremul(g_pixels[y * g_surfWidth + v1], PremultiplyARGB(120, 34, 211, 238));
            if (v2 >= 0 && v2 < g_surfWidth) g_pixels[y * g_surfWidth + v2] = AlphaBlendPremul(g_pixels[y * g_surfWidth + v2], PremultiplyARGB(120, 34, 211, 238));
        }
    }
    for (int x = scX; x <= scX + scW; x++) {
        if (x >= 0 && x < g_surfWidth) {
            if (h1 >= 0 && h1 < g_surfHeight) g_pixels[h1 * g_surfWidth + x] = AlphaBlendPremul(g_pixels[h1 * g_surfWidth + x], PremultiplyARGB(120, 34, 211, 238));
            if (h2 >= 0 && h2 < g_surfHeight) g_pixels[h2 * g_surfWidth + x] = AlphaBlendPremul(g_pixels[h2 * g_surfWidth + x], PremultiplyARGB(120, 34, 211, 238));
        }
    }

    // 8 Draggable Corner & Edge Handle Grips
    int hPts[8][2] = {
        { scX, scY },                     // 0: Top-Left
        { scX + scW, scY },               // 1: Top-Right
        { scX + scW, scY + scH },         // 2: Bottom-Right
        { scX, scY + scH },               // 3: Bottom-Left
        { scX + scW / 2, scY },           // 4: Top-Mid
        { scX + scW, scY + scH / 2 },     // 5: Right-Mid
        { scX + scW / 2, scY + scH },     // 6: Bottom-Mid
        { scX, scY + scH / 2 }            // 7: Left-Mid
    };
    for (int i = 0; i < 8; i++) {
        DrawRoundedBoxARGB(hPts[i][0] - 5, hPts[i][1] - 5, 10, 10, 5.0f,
                           255, 255, 255, 255,
                           6, 182, 212, 255, 1.5f);
    }

    // 5. Bottom Toolbar
    int barY = g_surfHeight - 75;
    DrawRoundedBoxARGB(30, barY, g_surfWidth - 60, 48, 10.0f,
                       24, 27, 38, 230,
                       51, 65, 85, 160, 1.0f);

    const WCHAR *aspectLabels[] = { L"Free", L"1:1", L"4:3", L"16:9", L"35:45" };
    int ax = 42;
    for (int i = 0; i < 5; i++) {
        int bw = (i == 4) ? 58 : 50;
        BOOL isActive = (ctx->cropAspectLock == i);
        uint8_t bgR = isActive ? 124 : 30;
        uint8_t bgG = isActive ? 58  : 41;
        uint8_t bgB = isActive ? 237 : 59;
        COLORREF tCol = isActive ? RGB(255, 255, 255) : RGB(148, 163, 184);

        DrawRoundedBoxARGB(ax, barY + 10, bw, 28, 6.0f, bgR, bgG, bgB, 240, 51, 65, 85, 180, 1.0f);
        DrawCrispTextARGB(aspectLabels[i], ax + (bw / 2), barY + 24, tCol, 10, FW_BOLD, 1.0f, FALSE);
        ax += bw + 8;
    }

    // Rotate 90° button
    DrawRoundedBoxARGB(365, barY + 10, 80, 28, 6.0f, 30, 41, 59, 240, 51, 65, 85, 180, 1.0f);
    DrawCrispTextARGB(L"Rotate 90°", 365 + 40, barY + 24, RGB(220, 240, 255), 10, FW_NORMAL, 1.0f, FALSE);

    // Cancel button
    DrawRoundedBoxARGB(455, barY + 10, 75, 28, 6.0f, 30, 41, 59, 240, 51, 65, 85, 180, 1.0f);
    DrawCrispTextARGB(L"Cancel", 455 + 37, barY + 24, RGB(148, 163, 184), 10, FW_NORMAL, 1.0f, FALSE);

    // Apply Crop button (Emerald #10B981)
    DrawRoundedBoxARGB(540, barY + 10, 95, 28, 6.0f, 16, 185, 129, 255, 5, 150, 105, 255, 1.0f);
    DrawCrispTextARGB(L"Apply Crop", 540 + 47, barY + 24, RGB(255, 255, 255), 10, FW_BOLD, 1.0f, FALSE);
}

// -------------------------------------------------------------
// Radial Menu Renderer
// -------------------------------------------------------------
static void RenderRadialMenu(MagifyContext *ctx) {
    float cx = (float)g_surfWidth * 0.5f;
    float cy = (float)g_surfHeight * 0.5f;

    float t = ctx->animProgress;
    float animScale = 1.0f;
    if (t < 1.0f) {
        float c1 = 1.70158f;
        float c3 = c1 + 1.0f;
        float p = t - 1.0f;
        animScale = 1.0f + c3 * (p * p * p) + c1 * (p * p);
        if (animScale < 0.05f) animScale = 0.05f;
    }

    if (ctx->isClosing) {
        float cp = 1.0f - ctx->closeProgress;
        animScale *= cp;
        if (animScale < 0.01f) animScale = 0.01f;
    }

    float globalAlpha = ctx->isClosing ? (1.0f - ctx->closeProgress) : ClampFloat(t * 1.5f, 0.0f, 1.0f);
    if (globalAlpha <= 0.001f) return;

    // Center Hub smooth expansion when processing
    float hubTarget = (ctx->uiState == UI_STATE_PROCESSING) ? 1.0f : 0.0f;
    ctx->hubExpandAnim += (hubTarget - ctx->hubExpandAnim) * 0.28f;
    float hubRadius = (50.0f + 24.0f * ctx->hubExpandAnim) * animScale;
    float innerRadius = 56.0f * animScale;
    float baseOuterRadius = 142.0f * animScale;
    float slicesAlpha = (1.0f - ctx->hubExpandAnim);

    int sliceCount = 4;
    float rotOffset = 225.0f;
    if (ctx->uiState == UI_STATE_RADIAL_SUB) {
        sliceCount = GetSubmenuSliceCount(ctx);
        rotOffset = 270.0f - (360.0f / (float)sliceCount) * 0.5f;
    }

    float sliceSpan = 360.0f / (float)sliceCount;
    const MagifyThemePalette *th = &ctx->themePalette;

    for (int i = 0; i < sliceCount; i++) {
        float target = (ctx->hoveredSlice == i) ? 1.0f : 0.0f;
        g_sliceHover[i] += (target - g_sliceHover[i]) * 0.28f;
    }
    float hubHoverTarget = (ctx->hoveredSlice == -1) ? 1.0f : 0.0f;
    g_hubHover += (hubHoverTarget - g_hubHover) * 0.28f;

    for (int y = 0; y < g_surfHeight; y++) {
        float dy = (float)y - cy;
        for (int x = 0; x < g_surfWidth; x++) {
            float dx = (float)x - cx;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist > (baseOuterRadius + 18.0f) + 2.0f) continue;

            // Center Hub & Circular Progress Ring
            if (dist <= hubRadius + 1.5f) {
                float d = hubRadius - dist;
                float cov = ClampFloat(d + 0.5f, 0.0f, 1.0f);

                if (ctx->hubExpandAnim > 0.05f) {
                    // Processing / Success / Error circular progress ring
                    float ringThick = 9.0f * animScale;
                    float ringInner = hubRadius - ringThick;

                    if (dist >= ringInner - 1.0f && dist <= hubRadius + 1.0f) {
                        float ringMid = (ringInner + hubRadius) * 0.5f;
                        float dRing = (ringThick * 0.5f) - fabsf(dist - ringMid);
                        float covRing = ClampFloat(dRing + 0.5f, 0.0f, 1.0f);

                        float angleDeg = atan2f(dy, dx) * (180.0f / MAGIFY_PI);
                        if (angleDeg < 0.0f) angleDeg += 360.0f;
                        float relAngle = fmodf(angleDeg - 270.0f + 360.0f, 360.0f);

                        float pct = (float)ctx->progressPercent / 100.0f;
                        if (pct < 0.05f && !ctx->isSuccessState && !ctx->isErrorState) pct = 0.05f;
                        float maxAngle = pct * 360.0f;

                        if (ctx->isErrorState) {
                            uint8_t a = (uint8_t)(255.0f * covRing * globalAlpha);
                            g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, 244, 63, 94);
                            continue;
                        } else if (ctx->isSuccessState) {
                            uint8_t a = (uint8_t)(255.0f * covRing * globalAlpha);
                            g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, 16, 185, 129);
                            continue;
                        } else if (relAngle <= maxAngle) {
                            // Rotating gradient sweep: Violet -> Cyan (theme based)
                            float tGrad = relAngle / 360.0f;
                            uint8_t r = (uint8_t)((float)th->sliceHovR1 + ((float)th->sliceHovR2 - (float)th->sliceHovR1) * tGrad);
                            uint8_t g = (uint8_t)((float)th->sliceHovG1 + ((float)th->sliceHovG2 - (float)th->sliceHovG1) * tGrad);
                            uint8_t b = (uint8_t)((float)th->sliceHovB1 + ((float)th->sliceHovB2 - (float)th->sliceHovB1) * tGrad);
                            uint8_t a = (uint8_t)(255.0f * covRing * globalAlpha);
                            g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, r, g, b);
                            continue;
                        } else {
                            // Track background
                            uint8_t a = (uint8_t)((float)th->trackA * covRing * globalAlpha);
                            g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, th->trackR, th->trackG, th->trackB);
                            continue;
                        }
                    }

                    // Interior background
                    uint8_t a = (uint8_t)((float)th->hubBgA * cov * globalAlpha);
                    g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, th->hubBgR, th->hubBgG, th->hubBgB);
                    continue;
                }

                // Normal Hub
                uint8_t r = th->hubBgR, g = th->hubBgG, b = th->hubBgB;
                uint8_t a = (uint8_t)((float)th->hubBgA * cov * globalAlpha);

                if (g_hubHover > 0.01f) {
                    if (th->isLightMode) {
                        r = (uint8_t)(r - (r - 220) * g_hubHover);
                        g = (uint8_t)(g - (g - 225) * g_hubHover);
                        b = (uint8_t)(b - (b - 235) * g_hubHover);
                    } else {
                        r = (uint8_t)(r + (34 - r) * g_hubHover);
                        g = (uint8_t)(g + (45 - g) * g_hubHover);
                        b = (uint8_t)(b + (70 - b) * g_hubHover);
                    }
                }

                float borderDist = fabsf(dist - hubRadius);
                if (borderDist < 2.5f) {
                    float bFactor = (1.0f - (borderDist / 2.5f)) * globalAlpha;
                    uint8_t br = (uint8_t)((float)th->hubBdrR * bFactor);
                    uint8_t bg = (uint8_t)((float)th->hubBdrG * bFactor);
                    uint8_t bb = (uint8_t)((float)th->hubBdrB * bFactor);
                    g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, br, bg, bb);
                    continue;
                }

                g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, r, g, b);
                continue;
            }

            // Radial Slices
            if (dist >= innerRadius - 1.5f && slicesAlpha > 0.01f) {
                float angleDeg = atan2f(dy, dx) * (180.0f / MAGIFY_PI);
                if (angleDeg < 0.0f) angleDeg += 360.0f;

                float relAngle = fmodf(angleDeg - rotOffset + 360.0f, 360.0f);
                int sIdx = (int)(relAngle / sliceSpan);
                if (sIdx >= sliceCount) sIdx = sliceCount - 1;

                float sliceLocalAngle = relAngle - ((float)sIdx * sliceSpan);
                float halfSpan = sliceSpan * 0.5f;
                float angleFromCenter = fabsf(sliceLocalAngle - halfSpan);

                float gapDeg = 1.4f;
                float angDist = (halfSpan - angleFromCenter) - gapDeg;
                float angPixDist = angDist * (MAGIFY_PI / 180.0f) * dist;

                float hAmount = g_sliceHover[sIdx];
                float outerR = baseOuterRadius + (12.0f * hAmount * animScale);

                float dInner = dist - innerRadius;
                float dOuter = outerR - dist;
                float radDist = (dInner < dOuter) ? dInner : dOuter;
                float sdfDist = (radDist < angPixDist) ? radDist : angPixDist;

                if (sdfDist < -1.0f) continue;
                float cov = ClampFloat(sdfDist + 0.5f, 0.0f, 1.0f);

                float radFactor = (dist - innerRadius) / (outerR - innerRadius);
                radFactor = ClampFloat(radFactor, 0.0f, 1.0f);

                uint8_t normR = th->sliceNormR, normG = th->sliceNormG, normB = th->sliceNormB;
                uint8_t normA = (uint8_t)((float)th->sliceNormA * cov * globalAlpha * slicesAlpha);

                uint8_t hovR = (uint8_t)((float)th->sliceHovR1 + ((float)th->sliceHovR2 - (float)th->sliceHovR1) * radFactor);
                uint8_t hovG = (uint8_t)((float)th->sliceHovG1 + ((float)th->sliceHovG2 - (float)th->sliceHovG1) * radFactor);
                uint8_t hovB = (uint8_t)((float)th->sliceHovB1 + ((float)th->sliceHovB2 - (float)th->sliceHovB1) * radFactor);
                uint8_t hovA = (uint8_t)(245.0f * cov * globalAlpha * slicesAlpha);

                uint8_t r = (uint8_t)(normR + ((float)hovR - normR) * hAmount);
                uint8_t g = (uint8_t)(normG + ((float)hovG - normG) * hAmount);
                uint8_t b = (uint8_t)(normB + ((float)hovB - normB) * hAmount);
                uint8_t a = (uint8_t)(normA + ((float)hovA - normA) * hAmount);

                if (hAmount > 0.1f && dOuter < 3.0f && dOuter >= -0.5f) {
                    float edgeGlow = (1.0f - (dOuter / 3.0f)) * hAmount * globalAlpha * slicesAlpha;
                    r = (uint8_t)(r + ((float)th->glowR - r) * edgeGlow);
                    g = (uint8_t)(g + ((float)th->glowG - g) * edgeGlow);
                    b = (uint8_t)(b + ((float)th->glowB - b) * edgeGlow);
                }

                g_pixels[y * g_surfWidth + x] = PremultiplyARGB(a, r, g, b);
            }
        }
    }

    // Center Hub Text
    float s = (ctx->dpiScale >= 1.0f) ? ctx->dpiScale : 1.0f;
    if (ctx->uiState == UI_STATE_PROCESSING || ctx->hubExpandAnim > 0.1f) {
        if (ctx->isErrorState) {
            DrawCrispTextARGB(L"✕ ERROR", (int)cx, (int)(cy - 8.0f * s), RGB(244, 63, 94), (int)(14.0f * s), FW_BOLD, globalAlpha, FALSE);
            DrawCrispTextARGB(ctx->progressStatusText, (int)cx, (int)(cy + 14.0f * s), RGB(203, 213, 225), (int)(9.0f * s), FW_NORMAL, globalAlpha, FALSE);
        } else if (ctx->isSuccessState) {
            DrawCrispTextARGB(L"✓ COMPLETE", (int)cx, (int)(cy - 8.0f * s), RGB(16, 185, 129), (int)(14.0f * s), FW_BOLD, globalAlpha, FALSE);
            DrawCrispTextARGB(ctx->progressStatusText, (int)cx, (int)(cy + 14.0f * s), RGB(248, 250, 252), (int)(9.0f * s), FW_BOLD, globalAlpha, FALSE);
        } else {
            WCHAR progStr[32];
            swprintf(progStr, 32, L"%d%%", (int)ctx->progressPercent);
            DrawCrispTextARGB(progStr, (int)cx, (int)(cy - 10.0f * s), th->textPrimary, (int)(20.0f * s), FW_BOLD, globalAlpha, FALSE);
            const WCHAR *statusMsg = (ctx->progressStatusText[0] != L'\0') ? ctx->progressStatusText : ctx->statusText;
            DrawCrispTextARGB(statusMsg, (int)cx, (int)(cy + 14.0f * s), th->textAccent, (int)(9.0f * s), FW_NORMAL, globalAlpha, FALSE);
        }
    } else if (ctx->uiState == UI_STATE_RADIAL_SUB) {
        DrawCrispTextARGB(L"← BACK", (int)cx, (int)(cy - 6.0f * s), th->textAccent, (int)(14.0f * s), FW_BOLD, globalAlpha, FALSE);
        DrawCrispTextARGB(L"Click to return", (int)cx, (int)(cy + 12.0f * s), th->textSecondary, (int)(9.0f * s), FW_NORMAL, globalAlpha, FALSE);
    } else {
        WCHAR badge[32] = {0};
        if (ctx->fileCount > 1) {
            swprintf(badge, 32, L"%d FILES", ctx->fileCount);
        } else if (ctx->primaryClass == FILE_CLASS_IMAGE) {
            wcscpy(badge, L"IMAGE");
        } else if (ctx->primaryClass == FILE_CLASS_PDF) {
            wcscpy(badge, L"PDF");
        } else {
            wcscpy(badge, L"FILE");
        }
        DrawCrispTextARGB(badge, (int)cx, (int)(cy - 16.0f * s), th->textAccent, (int)(12.0f * s), FW_BOLD, globalAlpha, FALSE);

        WCHAR nameDisplay[32] = {0};
        if (ctx->fileCount == 1 && ctx->files[0][0] != L'\0') {
            const WCHAR *fName = PathFindFileNameW(ctx->files[0]);
            wcsncpy(nameDisplay, fName, 14);
            nameDisplay[14] = L'\0';
            if (wcslen(fName) > 14) wcscat(nameDisplay, L"...");
        } else {
            wcscpy(nameDisplay, L"Magify");
        }
        DrawCrispTextARGB(nameDisplay, (int)cx, (int)cy, th->textPrimary, (int)(11.0f * s), FW_SEMIBOLD, globalAlpha, FALSE);

        WCHAR sizeStr[32] = {0};
        Magify_FormatFileSize(ctx->totalSizeBytes, sizeStr, 32);
        DrawCrispTextARGB(sizeStr, (int)cx, (int)(cy + 15.0f * s), th->textSecondary, (int)(10.0f * s), FW_NORMAL, globalAlpha, FALSE);
    }

    // Slice Labels
    SliceDesc descs[8];
    if (ctx->uiState == UI_STATE_RADIAL_ROOT) {
        wcscpy(descs[0].title, L"CONVERT");   wcscpy(descs[0].subtitle, L"Target Format");
        wcscpy(descs[1].title, L"EDIT");      wcscpy(descs[1].subtitle, L"Tools & Actions");
        wcscpy(descs[2].title, L"COMPRESS");  wcscpy(descs[2].subtitle, L"Optimize Size");
        wcscpy(descs[3].title, L"RENAME");    wcscpy(descs[3].subtitle, L"Instant HUD");
    } else if (ctx->uiState == UI_STATE_RADIAL_SUB) {
        GetSubmenuSlices(ctx, descs, 8);
    }

    float labelR = (innerRadius + baseOuterRadius) * 0.5f;
    for (int i = 0; i < sliceCount; i++) {
        if (descs[i].title[0] == L'\0') continue;

        float midAngle = rotOffset + ((float)i + 0.5f) * sliceSpan;
        float rad = midAngle * (MAGIFY_PI / 180.0f);
        float hAmount = g_sliceHover[i];
        float curLabelR = labelR + (4.0f * hAmount);

        int lx = (int)(cx + cosf(rad) * curLabelR);
        int ly = (int)(cy + sinf(rad) * curLabelR);

        COLORREF titleColor = (hAmount > 0.5f) ? (th->isLightMode ? RGB(109, 40, 217) : RGB(255, 255, 255)) : th->textPrimary;
        COLORREF subColor = (hAmount > 0.5f) ? (th->isLightMode ? RGB(8, 145, 178) : RGB(220, 245, 255)) : th->textSecondary;

        int titleFont = (int)(((sliceCount <= 4) ? 12 : 11) * s);
        int subFont = (int)(((sliceCount <= 4) ? 9 : 8) * s);

        DrawCrispTextARGB(descs[i].title, lx, (int)(ly - 7.0f * s), titleColor, titleFont, FW_BOLD, globalAlpha, FALSE);
        DrawCrispTextARGB(descs[i].subtitle, lx, (int)(ly + 8.0f * s), subColor, subFont, FW_NORMAL, globalAlpha, FALSE);
    }
}

static void RenderFrame(MagifyContext *ctx) {
    if (!g_pixels || g_surfWidth == 0 || g_surfHeight == 0) return;
    memset(g_pixels, 0, g_surfWidth * g_surfHeight * sizeof(uint32_t));

    if (ctx->uiState == UI_STATE_RENAME_HUD) {
        RenderRenameHUD(ctx);
    } else if (ctx->uiState == UI_STATE_IMAGE_EDIT) {
        RenderCropHUD(ctx);
    } else if (ctx->uiState == UI_STATE_PDF_EDIT) {
        RenderPdfOrganizerHUD(ctx);
    } else {
        RenderRadialMenu(ctx);
    }

    POINT ptSrc = {0, 0};
    SIZE sz = { g_surfWidth, g_surfHeight };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };

    RECT rcWin;
    GetWindowRect(ctx->hwndOverlay, &rcWin);
    POINT ptDst = { rcWin.left, rcWin.top };

    UpdateLayeredWindow(
        ctx->hwndOverlay,
        NULL,
        &ptDst,
        &sz,
        g_hMemDC,
        &ptSrc,
        0,
        &bf,
        ULW_ALPHA
    );
}

static void ResizeOverlayWindow(MagifyContext *ctx, int newW, int newH) {
    ctx->overlayWidth = (int)((float)newW * ctx->dpiScale);
    ctx->overlayHeight = (int)((float)newH * ctx->dpiScale);

    HMONITOR hMon = MonitorFromPoint(ctx->spawnCursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof(MONITORINFO) };
    GetMonitorInfoW(hMon, &mi);

    int x = ctx->spawnCursor.x - (ctx->overlayWidth / 2);
    int y = ctx->spawnCursor.y - (ctx->overlayHeight / 2);

    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;
    if (x + ctx->overlayWidth > mi.rcWork.right)   x = mi.rcWork.right - ctx->overlayWidth;
    if (y + ctx->overlayHeight > mi.rcWork.bottom) y = mi.rcWork.bottom - ctx->overlayHeight;

    SetWindowPos(ctx->hwndOverlay, HWND_TOPMOST, x, y, ctx->overlayWidth, ctx->overlayHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);

    HDC hdcScreen = GetDC(NULL);
    InitDIB(hdcScreen, ctx->overlayWidth, ctx->overlayHeight);
    ReleaseDC(NULL, hdcScreen);
}

void Magify_RedrawOverlay(MagifyContext *ctx) {
    if (!ctx || !ctx->hwndOverlay || !IsWindow(ctx->hwndOverlay)) return;
    RenderFrame(ctx);
}

void Magify_CloseOverlay(MagifyContext *ctx) {
    if (!ctx || ctx->isClosing) return;
    ctx->isClosing = TRUE;
    ctx->closeProgress = 0.0f;
}

static void ExecuteAction(MagifyContext *ctx, int slice) {
    ctx->overwriteInPlace = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    if (ctx->uiState == UI_STATE_RADIAL_ROOT) {
        if (slice == 0) { // CONVERT
            ctx->uiState = UI_STATE_RADIAL_SUB;
            ctx->activeSubmenu = SUBMENU_CONVERT;
            ctx->hoveredSlice = -1;
        } else if (slice == 1) { // EDIT
            ctx->uiState = UI_STATE_RADIAL_SUB;
            ctx->activeSubmenu = SUBMENU_EDIT;
            ctx->hoveredSlice = -1;
        } else if (slice == 2) { // COMPRESS
            ctx->uiState = UI_STATE_RADIAL_SUB;
            ctx->activeSubmenu = SUBMENU_COMPRESS;
            ctx->hoveredSlice = -1;
        } else if (slice == 3) { // RENAME
            InitRenameHUD(ctx);
        }
        return;
    }

    if (ctx->uiState == UI_STATE_RADIAL_SUB) {
        const WCHAR *file = (ctx->fileCount > 0) ? ctx->files[0] : L"sample.file";

        if (ctx->activeSubmenu == SUBMENU_CONVERT) {
            if (ctx->primaryClass == FILE_CLASS_IMAGE) {
                const WCHAR *exts[] = { L"PNG", L"JPG", L"BMP", L"ICO", L"PDF" };
                if (slice >= 0 && slice < 5) {
                    Magify_Image_Convert(file, exts[slice], ctx);
                }
            } else if (ctx->primaryClass == FILE_CLASS_PDF) {
                if (slice == 0) Magify_PdfConvertToImages(ctx, L"JPG");
                else if (slice == 1) Magify_PdfConvertToImages(ctx, L"PNG");
                else if (slice == 2) Magify_PdfExtractText(ctx);
                else { ctx->uiState = UI_STATE_RADIAL_ROOT; ctx->hoveredSlice = -1; }
            } else {
                if (slice == 0) Magify_CompressGenericZip(ctx);
                else { ctx->uiState = UI_STATE_RADIAL_ROOT; ctx->hoveredSlice = -1; }
            }
        } else if (ctx->activeSubmenu == SUBMENU_EDIT) {
            if (ctx->primaryClass == FILE_CLASS_IMAGE) {
                if (slice == 0) Magify_Image_Rotate(file, 90, ctx);
                else if (slice == 1) Magify_Image_Rotate(file, 270, ctx);
                else if (slice == 2) Magify_Image_Flip(file, TRUE, ctx);
                else if (slice == 3) Magify_Image_Resize(file, 0, 0, 50, ctx);
                else if (slice == 4) InitCropHUD(ctx);
            } else if (ctx->primaryClass == FILE_CLASS_PDF) {
                if (slice == 0) InitPdfOrganizerHUD(ctx);
                else if (slice == 1) Magify_PdfRotateAll(ctx, 90);
                else if (slice == 2) Magify_PdfRotateAll(ctx, 270);
                else if (slice == 3) Magify_PdfSplit(ctx, L"BURST");
                else if (slice == 4) Magify_PdfMerge(ctx, NULL);
            }
        } else if (ctx->activeSubmenu == SUBMENU_COMPRESS) {
            if (ctx->primaryClass == FILE_CLASS_IMAGE) {
                int quals[] = { 90, 80, 60, -200 };
                if (slice >= 0 && slice < 4) {
                    Magify_Image_Compress(file, quals[slice], ctx);
                }
            } else if (ctx->primaryClass == FILE_CLASS_PDF) {
                if (slice == 0) Magify_PdfCompress(ctx, 300);
                else if (slice == 1) Magify_PdfCompress(ctx, 150);
                else if (slice == 2) Magify_PdfCompress(ctx, 96);
                else { ctx->uiState = UI_STATE_RADIAL_ROOT; ctx->hoveredSlice = -1; }
            } else {
                Magify_CompressGenericZip(ctx);
            }
        }
    }
}

static void HandleCropMouseDown(MagifyContext *ctx, int mx, int my) {
    int vpX = (g_surfWidth - ctx->cropPreviewW) / 2;
    int vpY = 62 + (400 - ctx->cropPreviewH) / 2;
    int vpW = ctx->cropPreviewW;
    int vpH = ctx->cropPreviewH;

    // Check Bottom Toolbar Buttons
    int barY = g_surfHeight - 75;
    if (my >= barY + 10 && my <= barY + 38) {
        int ax = 42;
        for (int i = 0; i < 5; i++) {
            int bw = (i == 4) ? 58 : 50;
            if (mx >= ax && mx <= ax + bw) {
                ctx->cropAspectLock = i;
                if (i > 0) {
                    float ratios[] = { 1.0f, 1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 35.0f / 45.0f };
                    float r = ratios[i];
                    float curPxW = ctx->cropNormW * (float)ctx->cropOrigW;
                    float newPxH = curPxW / r;
                    ctx->cropNormH = newPxH / (float)ctx->cropOrigH;
                    if (ctx->cropNormH > 1.0f - ctx->cropNormY) {
                        ctx->cropNormH = 1.0f - ctx->cropNormY;
                        ctx->cropNormW = (ctx->cropNormH * (float)ctx->cropOrigH * r) / (float)ctx->cropOrigW;
                    }
                }
                RenderFrame(ctx);
                return;
            }
            ax += bw + 8;
        }

        // Rotate 90°
        if (mx >= 365 && mx <= 445) {
            Magify_Image_Rotate(ctx->files[0], 90, ctx);
            InitCropHUD(ctx);
            return;
        }
        // Cancel
        if (mx >= 455 && mx <= 530) {
            ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
            ctx->uiState = UI_STATE_RADIAL_ROOT;
            RenderFrame(ctx);
            return;
        }
        // Apply Crop
        if (mx >= 540 && mx <= 635) {
            int cropX = (int)(ctx->cropNormX * (float)ctx->cropOrigW);
            int cropY = (int)(ctx->cropNormY * (float)ctx->cropOrigH);
            int cropW = (int)(ctx->cropNormW * (float)ctx->cropOrigW);
            int cropH = (int)(ctx->cropNormH * (float)ctx->cropOrigH);
            Magify_CropImage(ctx->files[0], cropX, cropY, cropW, cropH, ctx);
            ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
            return;
        }
    }

    // Handles hit test
    int scX = vpX + (int)(ctx->cropNormX * (float)vpW);
    int scY = vpY + (int)(ctx->cropNormY * (float)vpH);
    int scW = (int)(ctx->cropNormW * (float)vpW);
    int scH = (int)(ctx->cropNormH * (float)vpH);

    int hPts[8][2] = {
        { scX, scY },                     // 0: Top-Left
        { scX + scW, scY },               // 1: Top-Right
        { scX + scW, scY + scH },         // 2: Bottom-Right
        { scX, scY + scH },               // 3: Bottom-Left
        { scX + scW / 2, scY },           // 4: Top-Mid
        { scX + scW, scY + scH / 2 },     // 5: Right-Mid
        { scX + scW / 2, scY + scH },     // 6: Bottom-Mid
        { scX, scY + scH / 2 }            // 7: Left-Mid
    };

    for (int i = 0; i < 8; i++) {
        if (abs(mx - hPts[i][0]) <= 8 && abs(my - hPts[i][1]) <= 8) {
            ctx->cropDragMode = i;
            ctx->cropDragStartPt.x = mx;
            ctx->cropDragStartPt.y = my;
            ctx->cropDragStartX = ctx->cropNormX;
            ctx->cropDragStartY = ctx->cropNormY;
            ctx->cropDragStartW = ctx->cropNormW;
            ctx->cropDragStartH = ctx->cropNormH;
            SetCapture(ctx->hwndOverlay);
            return;
        }
    }

    // Pan body
    if (mx >= scX && mx <= scX + scW && my >= scY && my <= scY + scH) {
        ctx->cropDragMode = 8; // Pan
        ctx->cropDragStartPt.x = mx;
        ctx->cropDragStartPt.y = my;
        ctx->cropDragStartX = ctx->cropNormX;
        ctx->cropDragStartY = ctx->cropNormY;
        ctx->cropDragStartW = ctx->cropNormW;
        ctx->cropDragStartH = ctx->cropNormH;
        SetCapture(ctx->hwndOverlay);
    }
}

static void HandleCropMouseMove(MagifyContext *ctx, int mx, int my) {
    if (ctx->cropDragMode == -1) return;

    int vpW = ctx->cropPreviewW;
    int vpH = ctx->cropPreviewH;
    if (vpW <= 0 || vpH <= 0) return;

    float dx = (float)(mx - ctx->cropDragStartPt.x) / (float)vpW;
    float dy = (float)(my - ctx->cropDragStartPt.y) / (float)vpH;

    float nx = ctx->cropDragStartX;
    float ny = ctx->cropDragStartY;
    float nw = ctx->cropDragStartW;
    float nh = ctx->cropDragStartH;

    if (ctx->cropDragMode == 8) { // Pan
        nx += dx;
        ny += dy;
        if (nx < 0.0f) nx = 0.0f;
        if (ny < 0.0f) ny = 0.0f;
        if (nx + nw > 1.0f) nx = 1.0f - nw;
        if (ny + nh > 1.0f) ny = 1.0f - nh;
    } else {
        if (ctx->cropDragMode == 0) { // TL
            nx += dx; ny += dy; nw -= dx; nh -= dy;
        } else if (ctx->cropDragMode == 1) { // TR
            ny += dy; nw += dx; nh -= dy;
        } else if (ctx->cropDragMode == 2) { // BR
            nw += dx; nh += dy;
        } else if (ctx->cropDragMode == 3) { // BL
            nx += dx; nw -= dx; nh += dy;
        } else if (ctx->cropDragMode == 4) { // Top
            ny += dy; nh -= dy;
        } else if (ctx->cropDragMode == 5) { // Right
            nw += dx;
        } else if (ctx->cropDragMode == 6) { // Bottom
            nh += dy;
        } else if (ctx->cropDragMode == 7) { // Left
            nx += dx; nw -= dx;
        }

        if (nw < 0.05f) nw = 0.05f;
        if (nh < 0.05f) nh = 0.05f;
        if (nx < 0.0f) { nw += nx; nx = 0.0f; }
        if (ny < 0.0f) { nh += ny; ny = 0.0f; }
        if (nx + nw > 1.0f) nw = 1.0f - nx;
        if (ny + nh > 1.0f) nh = 1.0f - ny;

        if (ctx->cropAspectLock > 0) {
            float ratios[] = { 1.0f, 1.0f, 4.0f / 3.0f, 16.0f / 9.0f, 35.0f / 45.0f };
            float r = ratios[ctx->cropAspectLock];
            nh = (nw * (float)ctx->cropOrigW / (float)ctx->cropOrigH) / r;
            if (ny + nh > 1.0f) {
                nh = 1.0f - ny;
                nw = (nh * (float)ctx->cropOrigH * r) / (float)ctx->cropOrigW;
            }
        }
    }

    ctx->cropNormX = nx;
    ctx->cropNormY = ny;
    ctx->cropNormW = nw;
    ctx->cropNormH = nh;
    RenderFrame(ctx);
}

// -------------------------------------------------------------
// Visual PDF Page Organizer HUD Implementation
// -------------------------------------------------------------
static void InitPdfOrganizerHUD(MagifyContext *ctx) {
    if (ctx->pdfOrganizer) {
        Magify_PdfOrganizer_Free((MagifyPdfOrganizerState*)ctx->pdfOrganizer);
        ctx->pdfOrganizer = NULL;
    }

    MagifyPdfOrganizerState *org = Magify_PdfOrganizer_Init(ctx);
    if (!org) {
        swprintf(ctx->statusText, 128, L"Failed to load PDF pages");
        return;
    }

    ctx->pdfOrganizer = (void*)org;
    ctx->uiState = UI_STATE_PDF_EDIT;
    ResizeOverlayWindow(ctx, 760, 580);
    RenderFrame(ctx);
}

static void RenderPdfOrganizerHUD(MagifyContext *ctx) {
    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)ctx->pdfOrganizer;
    if (!org) return;

    const MagifyThemePalette *th = &ctx->themePalette;
    float s = (float)g_surfWidth / 760.0f;

    // Background
    DrawRoundedBoxARGB(0, 0, g_surfWidth, g_surfHeight, 16.0f * s,
                       th->hubBgR, th->hubBgG, th->hubBgB, th->hubBgA,
                       th->hubBdrR, th->hubBdrG, th->hubBdrB, 255, 1.2f * s);

    // Top Header Bar
    const WCHAR *fileName = PathFindFileNameW(ctx->files[0]);
    DrawCrispTextARGB(fileName, (int)(26 * s), (int)(28 * s), th->textPrimary, (int)(13 * s), FW_BOLD, 1.0f, TRUE);

    int activePages = 0;
    for (int p = 0; p < org->totalPages; p++) {
        if (!org->pages[p].markedDeleted) activePages++;
    }
    WCHAR pageCountStr[32];
    if (activePages == org->totalPages) {
        swprintf(pageCountStr, 32, L"%d Pages", org->totalPages);
    } else {
        swprintf(pageCountStr, 32, L"%d / %d Pages", activePages, org->totalPages);
    }
    DrawRoundedBoxARGB((int)(220 * s), (int)(16 * s), (int)(95 * s), (int)(26 * s), 13.0f * s,
                       15, 23, 42, 255, 6, 182, 212, 180, 1.0f * s);
    DrawCrispTextARGB(pageCountStr, (int)(267 * s), (int)(28 * s), RGB(34, 211, 238), (int)(10 * s), FW_BOLD, 1.0f, FALSE);

    // [+ Add PDF to Merge] button
    DrawRoundedBoxARGB((int)(330 * s), (int)(16 * s), (int)(145 * s), (int)(26 * s), 13.0f * s,
                       30, 41, 59, 230, 124, 58, 237, 180, 1.0f * s);
    DrawCrispTextARGB(L"+ Add PDF to Merge", (int)(402 * s), (int)(28 * s), RGB(226, 232, 240), (int)(9 * s), FW_BOLD, 1.0f, FALSE);

    // [Split Range] button
    DrawRoundedBoxARGB((int)(490 * s), (int)(16 * s), (int)(105 * s), (int)(26 * s), 13.0f * s,
                       30, 41, 59, 230, 51, 65, 85, 160, 1.0f * s);
    DrawCrispTextARGB(L"Split Range", (int)(542 * s), (int)(28 * s), RGB(226, 232, 240), (int)(9 * s), FW_NORMAL, 1.0f, FALSE);

    // Close button (X)
    DrawRoundedBoxARGB((int)(700 * s), (int)(16 * s), (int)(32 * s), (int)(26 * s), 13.0f * s,
                       30, 41, 59, 220, 244, 63, 94, 180, 1.0f * s);
    DrawCrispTextARGB(L"✕", (int)(716 * s), (int)(28 * s), RGB(244, 63, 94), (int)(11 * s), FW_BOLD, 1.0f, FALSE);

    // 4 Columns layout
    int colX[4] = { (int)(46 * s), (int)(222 * s), (int)(398 * s), (int)(574 * s) };
    int cardW = (int)(140 * s);
    int cardH = (int)(180 * s);
    int rowH  = (int)(206 * s);

    for (int i = 0; i < org->totalPages; i++) {
        int col = i % 4;
        int row = i / 4;
        int cardX = colX[col];
        int cardY = (int)(62 * s) + row * rowH - org->scrollOffsetY;

        if (cardY + cardH < (int)(54 * s) || cardY > (int)(520 * s)) continue;

        BOOL isHovered = (org->hoverPageIndex == i);
        uint8_t bdrR = isHovered ? 6 : 51;
        uint8_t bdrG = isHovered ? 182 : 65;
        uint8_t bdrB = isHovered ? 212 : 85;
        uint8_t bdrA = isHovered ? 255 : 160;

        uint8_t bgR = isHovered ? 36 : 28;
        uint8_t bgG = isHovered ? 38 : 30;
        uint8_t bgB = isHovered ? 52 : 44;

        DrawRoundedBoxARGB(cardX, cardY, cardW, cardH, 10.0f * s,
                           bgR, bgG, bgB, 240,
                           bdrR, bdrG, bdrB, bdrA, isHovered ? (1.5f * s) : (1.0f * s));

        // Thumbnail blit inside card
        if (org->pages[i].thumbPixels) {
            int tW = org->pages[i].thumbW;
            int tH = org->pages[i].thumbH;
            int rot = (org->pages[i].currentRotation + org->pages[i].addedRotation) % 360;

            int dstX0 = cardX + (int)(10 * s);
            int dstY0 = cardY + (int)(22 * s);
            int dw = (int)(120 * s);
            int dh = (int)(136 * s);

            for (int dy = 0; dy < dh; dy++) {
                int py = dstY0 + dy;
                if (py < (int)(56 * s) || py >= (int)(518 * s) || py >= g_surfHeight) continue;

                for (int dx = 0; dx < dw; dx++) {
                    int px = dstX0 + dx;
                    if (px < 0 || px >= g_surfWidth) continue;

                    int sx = (dx * tW) / dw;
                    int sy = (dy * tH) / dh;
                    if (rot == 90) {
                        int tmp = sx;
                        sx = (dy * tW) / dh;
                        sy = tH - 1 - (tmp * tH) / dw;
                    } else if (rot == 180) {
                        sx = tW - 1 - sx;
                        sy = tH - 1 - sy;
                    } else if (rot == 270) {
                        int tmp = sx;
                        sx = tW - 1 - (dy * tW) / dh;
                        sy = (tmp * tH) / dw;
                    }

                    if (sx >= 0 && sx < tW && sy >= 0 && sy < tH) {
                        uint32_t srcP = org->pages[i].thumbPixels[sy * tW + sx];
                        g_pixels[py * g_surfWidth + px] = srcP;
                    }
                }
            }
        }

        // Page number pill
        WCHAR numStr[32];
        if (org->pages[i].sourceDocIndex > 0) {
            swprintf(numStr, 32, L"P%d (D%d)", i + 1, org->pages[i].sourceDocIndex + 1);
        } else {
            swprintf(numStr, 32, L"Page %d", i + 1);
        }
        DrawRoundedBoxARGB(cardX + (int)(6 * s), cardY + (int)(6 * s), (int)(56 * s), (int)(16 * s), 5.0f * s,
                           15, 23, 42, 230, 71, 85, 105, 180, 1.0f * s);
        DrawCrispTextARGB(numStr, cardX + (int)(34 * s), cardY + (int)(14 * s), RGB(226, 232, 240), (int)(9 * s), FW_BOLD, 1.0f, FALSE);

        // Deleted overlay
        if (org->pages[i].markedDeleted) {
            DrawRoundedBoxARGB(cardX, cardY, cardW, cardH, 10.0f * s,
                               244, 63, 94, 160,
                               244, 63, 94, 255, 1.5f * s);
            DrawCrispTextARGB(L"✕ DELETED", cardX + cardW / 2, cardY + cardH / 2, RGB(255, 255, 255), (int)(12 * s), FW_BOLD, 1.0f, FALSE);
        }

        // Mini action pills (CCW, CW, Delete)
        if (isHovered && !org->isDragging) {
            int pillY = cardY + (int)(154 * s);
            int hp = org->hoverPillIndex;

            // CCW
            DrawRoundedBoxARGB(cardX + (int)(12 * s), pillY, (int)(34 * s), (int)(20 * s), 6.0f * s,
                               (hp == 0) ? 124 : 30, (hp == 0) ? 58 : 41, (hp == 0) ? 237 : 59, 230,
                               148, 163, 184, 180, 1.0f * s);
            DrawCrispTextARGB(L"↺", cardX + (int)(29 * s), pillY + (int)(10 * s), RGB(248, 250, 252), (int)(10 * s), FW_BOLD, 1.0f, FALSE);

            // CW
            DrawRoundedBoxARGB(cardX + (int)(53 * s), pillY, (int)(34 * s), (int)(20 * s), 6.0f * s,
                               (hp == 1) ? 124 : 30, (hp == 1) ? 58 : 41, (hp == 1) ? 237 : 59, 230,
                               148, 163, 184, 180, 1.0f * s);
            DrawCrispTextARGB(L"↻", cardX + (int)(70 * s), pillY + (int)(10 * s), RGB(248, 250, 252), (int)(10 * s), FW_BOLD, 1.0f, FALSE);

            // Delete / Restore
            DrawRoundedBoxARGB(cardX + (int)(94 * s), pillY, (int)(34 * s), (int)(20 * s), 6.0f * s,
                               (hp == 2) ? 244 : 30, (hp == 2) ? 63 : 41, (hp == 2) ? 94 : 59, 230,
                               (hp == 2) ? 255 : 148, (hp == 2) ? 255 : 163, (hp == 2) ? 255 : 184, 180, 1.0f * s);
            DrawCrispTextARGB(L"✕", cardX + (int)(111 * s), pillY + (int)(10 * s), RGB(248, 250, 252), (int)(10 * s), FW_BOLD, 1.0f, FALSE);
        }
    }

    // Drag indicator & floating card
    if (org->isDragging && org->dragPageIndex >= 0) {
        if (org->dragTargetIndex >= 0 && org->dragTargetIndex <= org->totalPages) {
            int slotCol = org->dragTargetIndex % 4;
            int slotRow = org->dragTargetIndex / 4;
            int insertX = (org->dragTargetIndex == org->totalPages && slotCol == 0)
                        ? colX[3] + cardW + (int)(8 * s)
                        : colX[slotCol] - (int)(6 * s);
            int insertY = (int)(62 * s) + slotRow * rowH - org->scrollOffsetY;

            if (insertY >= (int)(50 * s) && insertY + cardH <= (int)(530 * s)) {
                DrawRoundedBoxARGB(insertX, insertY, (int)(5 * s), cardH, 2.0f * s,
                                   34, 211, 238, 255, 6, 182, 212, 255, 1.0f * s);
            }
        }

        int floatX = org->dragMousePos.x - (cardW / 2);
        int floatY = org->dragMousePos.y - (cardH / 2);
        DrawRoundedBoxARGB(floatX, floatY, cardW, cardH, 10.0f * s,
                           30, 41, 59, 210,
                           34, 211, 238, 255, 2.0f * s);
        WCHAR dragStr[32];
        swprintf(dragStr, 32, L"Page %d", org->dragPageIndex + 1);
        DrawCrispTextARGB(dragStr, floatX + cardW / 2, floatY + cardH / 2, RGB(255, 255, 255), (int)(12 * s), FW_BOLD, 1.0f, FALSE);
    }

    // Bottom Action Bar
    DrawRoundedBoxARGB((int)(24 * s), (int)(528 * s), g_surfWidth - (int)(48 * s), (int)(42 * s), 10.0f * s,
                       15, 23, 42, 235,
                       51, 65, 85, 180, 1.0f * s);

    // [Rotate All 90° CW]
    DrawRoundedBoxARGB((int)(36 * s), (int)(533 * s), (int)(145 * s), (int)(32 * s), 8.0f * s,
                       30, 41, 59, 220, 71, 85, 105, 180, 1.0f * s);
    DrawCrispTextARGB(L"Rotate All 90° CW", (int)(108 * s), (int)(549 * s), RGB(226, 232, 240), (int)(9 * s), FW_BOLD, 1.0f, FALSE);

    // [Burst All Pages]
    DrawRoundedBoxARGB((int)(190 * s), (int)(533 * s), (int)(130 * s), (int)(32 * s), 8.0f * s,
                       30, 41, 59, 220, 71, 85, 105, 180, 1.0f * s);
    DrawCrispTextARGB(L"Burst All Pages", (int)(255 * s), (int)(549 * s), RGB(226, 232, 240), (int)(9 * s), FW_BOLD, 1.0f, FALSE);

    // [Cancel (Esc)]
    DrawRoundedBoxARGB((int)(475 * s), (int)(533 * s), (int)(95 * s), (int)(32 * s), 8.0f * s,
                       51, 65, 85, 220, 71, 85, 105, 180, 1.0f * s);
    DrawCrispTextARGB(L"Cancel (Esc)", (int)(522 * s), (int)(549 * s), RGB(203, 213, 225), (int)(9 * s), FW_NORMAL, 1.0f, FALSE);

    // [Save PDF (Enter)]
    DrawRoundedBoxARGB((int)(580 * s), (int)(533 * s), (int)(136 * s), (int)(32 * s), 8.0f * s,
                       16, 185, 129, 240, 5, 150, 105, 255, 1.2f * s);
    DrawCrispTextARGB(L"Save PDF (Enter)", (int)(648 * s), (int)(549 * s), RGB(255, 255, 255), (int)(10 * s), FW_BOLD, 1.0f, FALSE);
}

static void HandlePdfOrganizerMouseMove(MagifyContext *ctx, int mx, int my) {
    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)ctx->pdfOrganizer;
    if (!org) return;

    float s = (float)g_surfWidth / 760.0f;
    int colX[4] = { (int)(46 * s), (int)(222 * s), (int)(398 * s), (int)(574 * s) };
    int cardW = (int)(140 * s);
    int cardH = (int)(180 * s);
    int rowH  = (int)(206 * s);

    if (org->isDragging) {
        org->dragMousePos = (POINT){ mx, my };
        int col = (mx - (int)(46 * s) + (cardW / 2)) / (cardW + (int)(36 * s));
        if (col < 0) col = 0;
        if (col > 3) col = 3;
        int row = (my - (int)(62 * s) + org->scrollOffsetY + (rowH / 2)) / rowH;
        if (row < 0) row = 0;
        int target = row * 4 + col;
        if (target > org->totalPages) target = org->totalPages;
        org->dragTargetIndex = target;
        RenderFrame(ctx);
        return;
    }

    int foundPage = -1;
    int foundPill = -1;

    for (int i = 0; i < org->totalPages; i++) {
        int col = i % 4;
        int row = i / 4;
        int cardX = colX[col];
        int cardY = (int)(62 * s) + row * rowH - org->scrollOffsetY;

        if (cardY + cardH < (int)(54 * s) || cardY > (int)(520 * s)) continue;

        if (mx >= cardX && mx <= cardX + cardW && my >= cardY && my <= cardY + cardH) {
            foundPage = i;
            int pillY = cardY + (int)(154 * s);
            if (my >= pillY && my <= pillY + (int)(20 * s)) {
                if (mx >= cardX + (int)(12 * s) && mx <= cardX + (int)(46 * s)) foundPill = 0;
                else if (mx >= cardX + (int)(53 * s) && mx <= cardX + (int)(87 * s)) foundPill = 1;
                else if (mx >= cardX + (int)(94 * s) && mx <= cardX + (int)(128 * s)) foundPill = 2;
            }
            break;
        }
    }

    if (foundPage != org->hoverPageIndex || foundPill != org->hoverPillIndex) {
        org->hoverPageIndex = foundPage;
        org->hoverPillIndex = foundPill;
        RenderFrame(ctx);
    }
}

static void HandlePdfOrganizerMouseDown(MagifyContext *ctx, int mx, int my) {
    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)ctx->pdfOrganizer;
    if (!org) return;

    float s = (float)g_surfWidth / 760.0f;

    // [+ Add PDF to Merge]
    if (mx >= (int)(330 * s) && mx <= (int)(475 * s) && my >= (int)(16 * s) && my <= (int)(42 * s)) {
        WCHAR addPath[MAX_PATH] = {0};
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = ctx->hwndOverlay;
        ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = addPath;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            Magify_PdfOrganizer_AddDoc(org, addPath);
            RenderFrame(ctx);
        }
        return;
    }

    // [Split Range]
    if (mx >= (int)(490 * s) && mx <= (int)(595 * s) && my >= (int)(16 * s) && my <= (int)(42 * s)) {
        Magify_PdfSplit(ctx, L"BURST");
        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
        Magify_PdfOrganizer_Free(org);
        ctx->pdfOrganizer = NULL;
        ctx->uiState = UI_STATE_RADIAL_ROOT;
        return;
    }

    // Close button (X) or Cancel
    if ((mx >= (int)(700 * s) && mx <= (int)(732 * s) && my >= (int)(16 * s) && my <= (int)(42 * s)) ||
        (mx >= (int)(475 * s) && mx <= (int)(570 * s) && my >= (int)(533 * s) && my <= (int)(565 * s))) {
        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
        Magify_PdfOrganizer_Free(org);
        ctx->pdfOrganizer = NULL;
        ctx->uiState = UI_STATE_RADIAL_ROOT;
        RenderFrame(ctx);
        return;
    }

    // [Rotate All 90° CW]
    if (mx >= (int)(36 * s) && mx <= (int)(181 * s) && my >= (int)(533 * s) && my <= (int)(565 * s)) {
        for (int p = 0; p < org->totalPages; p++) {
            if (!org->pages[p].markedDeleted) {
                org->pages[p].addedRotation = (org->pages[p].addedRotation + 90) % 360;
            }
        }
        RenderFrame(ctx);
        return;
    }

    // [Burst All Pages]
    if (mx >= (int)(190 * s) && mx <= (int)(320 * s) && my >= (int)(533 * s) && my <= (int)(565 * s)) {
        Magify_PdfSplit(ctx, L"BURST");
        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
        Magify_PdfOrganizer_Free(org);
        ctx->pdfOrganizer = NULL;
        ctx->uiState = UI_STATE_RADIAL_ROOT;
        return;
    }

    // [Save PDF (Enter)]
    if (mx >= (int)(580 * s) && mx <= (int)(716 * s) && my >= (int)(533 * s) && my <= (int)(565 * s)) {
        Magify_PdfApplyOrganizerChanges(ctx, org);
        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
        Magify_PdfOrganizer_Free(org);
        ctx->pdfOrganizer = NULL;
        ctx->uiState = UI_STATE_RADIAL_ROOT;
        return;
    }

    // Card interactions
    int colX[4] = { (int)(46 * s), (int)(222 * s), (int)(398 * s), (int)(574 * s) };
    int cardW = (int)(140 * s);
    int cardH = (int)(180 * s);
    int rowH  = (int)(206 * s);

    for (int i = 0; i < org->totalPages; i++) {
        int col = i % 4;
        int row = i / 4;
        int cardX = colX[col];
        int cardY = (int)(62 * s) + row * rowH - org->scrollOffsetY;

        if (cardY + cardH < (int)(54 * s) || cardY > (int)(520 * s)) continue;

        if (mx >= cardX && mx <= cardX + cardW && my >= cardY && my <= cardY + cardH) {
            int pillY = cardY + (int)(154 * s);
            if (my >= pillY && my <= pillY + (int)(20 * s)) {
                if (mx >= cardX + (int)(12 * s) && mx <= cardX + (int)(46 * s)) {
                    org->pages[i].addedRotation = (org->pages[i].addedRotation + 270) % 360;
                    RenderFrame(ctx);
                    return;
                } else if (mx >= cardX + (int)(53 * s) && mx <= cardX + (int)(87 * s)) {
                    org->pages[i].addedRotation = (org->pages[i].addedRotation + 90) % 360;
                    RenderFrame(ctx);
                    return;
                } else if (mx >= cardX + (int)(94 * s) && mx <= cardX + (int)(128 * s)) {
                    org->pages[i].markedDeleted = !org->pages[i].markedDeleted;
                    RenderFrame(ctx);
                    return;
                }
            }

            // Start drag
            org->isDragging = TRUE;
            org->dragPageIndex = i;
            org->dragTargetIndex = i;
            org->dragMousePos = (POINT){ mx, my };
            SetCapture(ctx->hwndOverlay);
            RenderFrame(ctx);
            return;
        }
    }
}

static void HandlePdfOrganizerMouseUp(MagifyContext *ctx) {
    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)ctx->pdfOrganizer;
    if (!org) return;

    if (org->isDragging) {
        org->isDragging = FALSE;
        ReleaseCapture();

        if (org->dragTargetIndex >= 0 && org->dragTargetIndex < org->totalPages &&
            org->dragTargetIndex != org->dragPageIndex) {
            MagifyPdfPageInfo temp = org->pages[org->dragPageIndex];
            if (org->dragPageIndex < org->dragTargetIndex) {
                for (int k = org->dragPageIndex; k < org->dragTargetIndex; k++) {
                    org->pages[k] = org->pages[k + 1];
                }
            } else {
                for (int k = org->dragPageIndex; k > org->dragTargetIndex; k--) {
                    org->pages[k] = org->pages[k - 1];
                }
            }
            org->pages[org->dragTargetIndex] = temp;
        }

        org->dragPageIndex = -1;
        org->dragTargetIndex = -1;
        RenderFrame(ctx);
    }
}

static void HandlePdfOrganizerMouseWheel(MagifyContext *ctx, int delta) {
    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)ctx->pdfOrganizer;
    if (!org) return;

    org->scrollOffsetY -= (delta / 120) * 50;
    if (org->scrollOffsetY < 0) org->scrollOffsetY = 0;
    if (org->scrollOffsetY > org->maxScrollY) org->scrollOffsetY = org->maxScrollY;
    RenderFrame(ctx);
}

static void HandlePdfOrganizerKeyDown(MagifyContext *ctx, WPARAM wParam) {
    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)ctx->pdfOrganizer;
    if (!org) return;

    if (wParam == VK_ESCAPE) {
        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
        Magify_PdfOrganizer_Free(org);
        ctx->pdfOrganizer = NULL;
        ctx->uiState = UI_STATE_RADIAL_ROOT;
        RenderFrame(ctx);
    } else if (wParam == VK_RETURN) {
        Magify_PdfApplyOrganizerChanges(ctx, org);
        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
        Magify_PdfOrganizer_Free(org);
        ctx->pdfOrganizer = NULL;
        ctx->uiState = UI_STATE_RADIAL_ROOT;
    } else if (wParam == VK_LEFT) {
        if (org->hoverPageIndex > 0) {
            org->hoverPageIndex--;
            RenderFrame(ctx);
        }
    } else if (wParam == VK_RIGHT) {
        if (org->hoverPageIndex < org->totalPages - 1) {
            org->hoverPageIndex++;
            RenderFrame(ctx);
        }
    } else if (wParam == VK_DELETE || wParam == 'D' || wParam == 'd') {
        if (org->hoverPageIndex >= 0 && org->hoverPageIndex < org->totalPages) {
            org->pages[org->hoverPageIndex].markedDeleted = !org->pages[org->hoverPageIndex].markedDeleted;
            RenderFrame(ctx);
        }
    } else if (wParam == 'R' || wParam == 'r') {
        if (org->hoverPageIndex >= 0 && org->hoverPageIndex < org->totalPages) {
            org->pages[org->hoverPageIndex].addedRotation = (org->pages[org->hoverPageIndex].addedRotation + 90) % 360;
            RenderFrame(ctx);
        }
    } else if (wParam == 'L' || wParam == 'l') {
        if (org->hoverPageIndex >= 0 && org->hoverPageIndex < org->totalPages) {
            org->pages[org->hoverPageIndex].addedRotation = (org->pages[org->hoverPageIndex].addedRotation + 270) % 360;
            RenderFrame(ctx);
        }
    }
}

static LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    MagifyContext *ctx = (MagifyContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (uMsg) {
        case WM_CREATE: {
            CREATESTRUCTW *cs = (CREATESTRUCTW*)lParam;
            ctx = (MagifyContext*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)ctx);
            SetTimer(hwnd, TIMER_ANIM_ID, ANIM_INTERVAL_MS, NULL);
            SetTimer(hwnd, TIMER_CARET_ID, 500, NULL);
            return 0;
        }

        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED: {
            s_themeCached = FALSE;
            if (ctx) {
                ctx->themePalette = Magify_GetActiveTheme();
                RenderFrame(ctx);
            }
            return 0;
        }

        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            if (ctx) {
                ctx->dpiScale = (float)newDpi / 96.0f;
                if (ctx->dpiScale < 1.0f) ctx->dpiScale = 1.0f;
                RECT *prc = (RECT*)lParam;
                int newW = prc->right - prc->left;
                int newH = prc->bottom - prc->top;
                ctx->overlayWidth = newW;
                ctx->overlayHeight = newH;
                SetWindowPos(hwnd, NULL, prc->left, prc->top, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
                HDC hdcScreen = GetDC(hwnd);
                InitDIB(hdcScreen, newW, newH);
                ReleaseDC(hwnd, hdcScreen);
                RenderFrame(ctx);
            }
            return 0;
        }

        case WM_USER + 101: {
            RenderFrame(ctx);
            return 0;
        }

        case WM_TIMER: {
            if (wParam == TIMER_ANIM_ID) {
                // Immediate global ESC key check: instant dismissal even across windows!
                if (!ctx->isClosing && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
                    if (ctx->uiState == UI_STATE_IMAGE_EDIT || ctx->uiState == UI_STATE_PDF_EDIT || ctx->uiState == UI_STATE_RENAME_HUD) {
                        ctx->uiState = UI_STATE_RADIAL_ROOT;
                        ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
                        RenderFrame(ctx);
                        return 0;
                    } else if (ctx->uiState == UI_STATE_RADIAL_SUB) {
                        ctx->uiState = UI_STATE_RADIAL_ROOT;
                        ctx->activeSubmenu = SUBMENU_NONE;
                        ctx->hoveredSlice = -1;
                        for (int i = 0; i < 8; i++) g_sliceHover[i] = 0.0f;
                        RenderFrame(ctx);
                        return 0;
                    } else {
                        Magify_CloseOverlay(ctx);
                        return 0;
                    }
                }

                if (!ctx->isClosing) {
                    if (ctx->animProgress < 1.0f) {
                        ctx->animProgress += 0.08f;
                        if (ctx->animProgress > 1.0f) ctx->animProgress = 1.0f;
                    }
                } else {
                    ctx->closeProgress += 0.12f;
                    if (ctx->closeProgress >= 1.0f) {
                        KillTimer(hwnd, TIMER_ANIM_ID);
                        KillTimer(hwnd, TIMER_CARET_ID);
                        DestroyWindow(hwnd);
                        return 0;
                    }
                }
                RenderFrame(ctx);
            } else if (wParam == TIMER_CARET_ID) {
                if (ctx->uiState == UI_STATE_RENAME_HUD) {
                    ctx->renameCaretVisible = !ctx->renameCaretVisible;
                    RenderFrame(ctx);
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (ctx->uiState == UI_STATE_PDF_EDIT) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                HandlePdfOrganizerMouseWheel(ctx, delta);
                return 0;
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (ctx->isClosing) return 0;
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);

            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);

            if (ctx->uiState == UI_STATE_IMAGE_EDIT) {
                HandleCropMouseMove(ctx, mx, my);
                return 0;
            }

            if (ctx->uiState == UI_STATE_PDF_EDIT) {
                HandlePdfOrganizerMouseMove(ctx, mx, my);
                return 0;
            }

            float cx = (float)g_surfWidth * 0.5f;
            float cy = (float)g_surfHeight * 0.5f;

            int sliceCount = (ctx->uiState == UI_STATE_RADIAL_ROOT) ? 4 : GetSubmenuSliceCount(ctx);
            float rotOffset = (ctx->uiState == UI_STATE_RADIAL_ROOT) ? 225.0f : (270.0f - (360.0f / (float)sliceCount) * 0.5f);

            int hit = Magify_HitTestRadial(
                (float)mx, (float)my, cx, cy,
                52.0f, 155.0f, sliceCount, rotOffset
            );

            if (hit != ctx->hoveredSlice) {
                ctx->hoveredSlice = hit;
                RenderFrame(ctx);
            }
            return 0;
        }

        case WM_MOUSELEAVE: {
            if (ctx->hoveredSlice != -2 && ctx->uiState < UI_STATE_RENAME_HUD) {
                ctx->hoveredSlice = -2;
                RenderFrame(ctx);
            }
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int mx = (short)LOWORD(lParam);
            int my = (short)HIWORD(lParam);

            if (ctx->uiState == UI_STATE_IMAGE_EDIT) {
                HandleCropMouseDown(ctx, mx, my);
                return 0;
            }

            if (ctx->uiState == UI_STATE_PDF_EDIT) {
                HandlePdfOrganizerMouseDown(ctx, mx, my);
                return 0;
            }

            if (ctx->uiState == UI_STATE_RENAME_HUD) {
                float cx = (float)g_surfWidth * 0.5f;
                float cy = (float)g_surfHeight * 0.5f;
                int pillW = 410, pillH = 124;
                int px = (int)(cx - (float)pillW * 0.5f);
                int py = (int)(cy - (float)pillH * 0.5f);

                // Rename Button click
                int btnW = 100, btnH = 28;
                int btnX = px + pillW - btnW - 18;
                int btnY = py + 84;
                if (mx >= btnX && mx <= btnX + btnW && my >= btnY && my <= btnY + btnH) {
                    Magify_RenameFiles(ctx, ctx->renameBuf);
                    return 0;
                }

                // Cancel Button click
                int cBtnW = 75;
                int cBtnX = btnX - cBtnW - 10;
                if (mx >= cBtnX && mx <= cBtnX + cBtnW && my >= btnY && my <= btnY + btnH) {
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    RenderFrame(ctx);
                    return 0;
                }

                // Click inside text box
                int bx = px + 18, by = py + 36, bw = pillW - 36, bh = 38;
                if (mx >= bx && mx <= bx + bw - 60 && my >= by && my <= by + bh) {
                    int charPos = (mx - (bx + 12)) / 8;
                    int len = (int)wcslen(ctx->renameBuf);
                    if (charPos < 0) charPos = 0;
                    if (charPos > len) charPos = len;
                    ctx->renameCursor = charPos;
                    ctx->renameSelStart = 0;
                    ctx->renameSelEnd = 0;
                    ctx->renameCaretVisible = TRUE;
                    RenderFrame(ctx);
                } else if (mx < px || mx > px + pillW || my < py || my > py + pillH) {
                    // Click outside pill returns to radial root
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    RenderFrame(ctx);
                }
                return 0;
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (ctx->isClosing) return 0;

            if (ctx->uiState == UI_STATE_IMAGE_EDIT) {
                if (ctx->cropDragMode != -1) {
                    ctx->cropDragMode = -1;
                    ReleaseCapture();
                }
                return 0;
            }

            if (ctx->uiState == UI_STATE_PDF_EDIT) {
                HandlePdfOrganizerMouseUp(ctx);
                return 0;
            }

            if (ctx->uiState == UI_STATE_RENAME_HUD) {
                return 0;
            }

            if (ctx->hoveredSlice == -1) {
                if (ctx->uiState == UI_STATE_RADIAL_SUB) {
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    ctx->activeSubmenu = SUBMENU_NONE;
                    ctx->hoveredSlice = -1;
                    RenderFrame(ctx);
                } else {
                    Magify_CloseOverlay(ctx);
                }
            } else if (ctx->hoveredSlice >= 0) {
                ExecuteAction(ctx, ctx->hoveredSlice);
                RenderFrame(ctx);
            } else if (ctx->hoveredSlice == -2) {
                if (ctx->uiState == UI_STATE_PROCESSING) {
                    Magify_CancelWorker(ctx);
                }
                Magify_CloseOverlay(ctx);
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            Magify_CloseOverlay(ctx);
            return 0;
        }

        case WM_CHAR: {
            if (ctx->uiState == UI_STATE_RENAME_HUD) {
                WCHAR ch = (WCHAR)wParam;
                if (ch >= 32) {
                    // Reject invalid Windows filename chars: \ / : * ? " < > |
                    if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
                        ch == L'?'  || ch == L'\"' || ch == L'<' || ch == L'>' || ch == L'|') {
                        return 0;
                    }

                    int len = (int)wcslen(ctx->renameBuf);
                    if (ctx->renameSelStart != ctx->renameSelEnd) {
                        ctx->renameBuf[0] = ch;
                        ctx->renameBuf[1] = L'\0';
                        ctx->renameCursor = 1;
                        ctx->renameSelStart = 0;
                        ctx->renameSelEnd = 0;
                    } else if (len < MAX_PATH - 32) {
                        for (int i = len; i >= ctx->renameCursor; i--) {
                            ctx->renameBuf[i + 1] = ctx->renameBuf[i];
                        }
                        ctx->renameBuf[ctx->renameCursor++] = ch;
                    }
                    ctx->renameCaretVisible = TRUE;
                    RenderFrame(ctx);
                }
                return 0;
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (ctx->uiState == UI_STATE_RENAME_HUD) {
                if (wParam == VK_RETURN) {
                    Magify_RenameFiles(ctx, ctx->renameBuf);
                    return 0;
                }
                if (wParam == VK_ESCAPE) {
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_BACK) {
                    int len = (int)wcslen(ctx->renameBuf);
                    if (ctx->renameSelStart != ctx->renameSelEnd) {
                        ctx->renameBuf[0] = L'\0';
                        ctx->renameCursor = 0;
                        ctx->renameSelStart = 0;
                        ctx->renameSelEnd = 0;
                    } else if (ctx->renameCursor > 0) {
                        for (int i = ctx->renameCursor - 1; i < len; i++) {
                            ctx->renameBuf[i] = ctx->renameBuf[i + 1];
                        }
                        ctx->renameCursor--;
                    }
                    ctx->renameCaretVisible = TRUE;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_DELETE) {
                    int len = (int)wcslen(ctx->renameBuf);
                    if (ctx->renameSelStart != ctx->renameSelEnd) {
                        ctx->renameBuf[0] = L'\0';
                        ctx->renameCursor = 0;
                        ctx->renameSelStart = 0;
                        ctx->renameSelEnd = 0;
                    } else if (ctx->renameCursor < len) {
                        for (int i = ctx->renameCursor; i < len; i++) {
                            ctx->renameBuf[i] = ctx->renameBuf[i + 1];
                        }
                    }
                    ctx->renameCaretVisible = TRUE;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_LEFT) {
                    ctx->renameSelStart = 0;
                    ctx->renameSelEnd = 0;
                    if (ctx->renameCursor > 0) ctx->renameCursor--;
                    ctx->renameCaretVisible = TRUE;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_RIGHT) {
                    ctx->renameSelStart = 0;
                    ctx->renameSelEnd = 0;
                    int len = (int)wcslen(ctx->renameBuf);
                    if (ctx->renameCursor < len) ctx->renameCursor++;
                    ctx->renameCaretVisible = TRUE;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_HOME) {
                    ctx->renameSelStart = 0;
                    ctx->renameSelEnd = 0;
                    ctx->renameCursor = 0;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_END) {
                    ctx->renameSelStart = 0;
                    ctx->renameSelEnd = 0;
                    ctx->renameCursor = (int)wcslen(ctx->renameBuf);
                    RenderFrame(ctx);
                    return 0;
                }
                if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'A' || wParam == 'a')) {
                    ctx->renameSelStart = 0;
                    ctx->renameSelEnd = (int)wcslen(ctx->renameBuf);
                    RenderFrame(ctx);
                    return 0;
                }
                if ((GetKeyState(VK_CONTROL) & 0x8000) && (wParam == 'V' || wParam == 'v')) {
                    if (OpenClipboard(hwnd)) {
                        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                        if (hData) {
                            const WCHAR *pClip = (const WCHAR*)GlobalLock(hData);
                            if (pClip) {
                                while (*pClip && ctx->renameCursor < MAX_PATH - 32) {
                                    WCHAR ch = *pClip++;
                                    if (ch >= 32 && ch != L'\\' && ch != L'/' && ch != L':' &&
                                        ch != L'*'  && ch != L'?'  && ch != L'\"' && ch != L'<' &&
                                        ch != L'>'  && ch != L'|') {
                                        ctx->renameBuf[ctx->renameCursor++] = ch;
                                        ctx->renameBuf[ctx->renameCursor] = L'\0';
                                    }
                                }
                                GlobalUnlock(hData);
                            }
                        }
                        CloseClipboard();
                        ctx->renameSelStart = 0;
                        ctx->renameSelEnd = 0;
                        RenderFrame(ctx);
                    }
                    return 0;
                }
                return 0;
            }

            if (ctx->uiState == UI_STATE_IMAGE_EDIT) {
                if (wParam == VK_ESCAPE) {
                    ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    RenderFrame(ctx);
                    return 0;
                }
                if (wParam == VK_RETURN) {
                    int cropX = (int)(ctx->cropNormX * (float)ctx->cropOrigW);
                    int cropY = (int)(ctx->cropNormY * (float)ctx->cropOrigH);
                    int cropW = (int)(ctx->cropNormW * (float)ctx->cropOrigW);
                    int cropH = (int)(ctx->cropNormH * (float)ctx->cropOrigH);
                    Magify_CropImage(ctx->files[0], cropX, cropY, cropW, cropH, ctx);
                    ResizeOverlayWindow(ctx, MAGIFY_RADIAL_WIDTH, MAGIFY_RADIAL_HEIGHT);
                    return 0;
                }
                return 0;
            }

            if (ctx->uiState == UI_STATE_PDF_EDIT) {
                HandlePdfOrganizerKeyDown(ctx, wParam);
                return 0;
            }

            int sliceCount = (ctx->uiState == UI_STATE_RADIAL_ROOT) ? 4 : GetSubmenuSliceCount(ctx);
            if (sliceCount < 1) sliceCount = 1;

            if (wParam == VK_ESCAPE) {
                if (ctx->uiState == UI_STATE_PROCESSING) {
                    Magify_CancelWorker(ctx);
                    Magify_CloseOverlay(ctx);
                    return 0;
                }
                if (ctx->uiState == UI_STATE_RADIAL_SUB) {
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    ctx->activeSubmenu = SUBMENU_NONE;
                    ctx->hoveredSlice = -1;
                    for (int i = 0; i < 8; i++) g_sliceHover[i] = 0.0f;
                    RenderFrame(ctx);
                } else {
                    Magify_CloseOverlay(ctx);
                }
                return 0;
            }

            if (wParam == VK_RIGHT || (wParam == VK_TAB && !(GetKeyState(VK_SHIFT) & 0x8000))) {
                if (ctx->uiState == UI_STATE_RADIAL_ROOT && wParam == VK_RIGHT) {
                    ctx->hoveredSlice = 1; // Edit (Right)
                } else {
                    if (ctx->hoveredSlice < 0) ctx->hoveredSlice = 0;
                    else ctx->hoveredSlice = (ctx->hoveredSlice + 1) % sliceCount;
                }
                for (int i = 0; i < 8; i++) g_sliceHover[i] = (i == ctx->hoveredSlice) ? 1.0f : 0.0f;
                RenderFrame(ctx);
                return 0;
            }

            if (wParam == VK_LEFT || (wParam == VK_TAB && (GetKeyState(VK_SHIFT) & 0x8000))) {
                if (ctx->uiState == UI_STATE_RADIAL_ROOT && wParam == VK_LEFT) {
                    ctx->hoveredSlice = 3; // Rename (Left)
                } else {
                    if (ctx->hoveredSlice < 0) ctx->hoveredSlice = sliceCount - 1;
                    else ctx->hoveredSlice = (ctx->hoveredSlice - 1 + sliceCount) % sliceCount;
                }
                for (int i = 0; i < 8; i++) g_sliceHover[i] = (i == ctx->hoveredSlice) ? 1.0f : 0.0f;
                RenderFrame(ctx);
                return 0;
            }

            if (wParam == VK_DOWN) {
                if (ctx->uiState == UI_STATE_RADIAL_ROOT) {
                    ctx->hoveredSlice = 2; // Compress (Bottom)
                } else {
                    if (ctx->hoveredSlice < 0) ctx->hoveredSlice = 0;
                    else ctx->hoveredSlice = (ctx->hoveredSlice + 1) % sliceCount;
                }
                for (int i = 0; i < 8; i++) g_sliceHover[i] = (i == ctx->hoveredSlice) ? 1.0f : 0.0f;
                RenderFrame(ctx);
                return 0;
            }

            if (wParam == VK_UP) {
                if (ctx->uiState == UI_STATE_RADIAL_ROOT) {
                    ctx->hoveredSlice = 0; // Convert (Top)
                } else {
                    if (ctx->hoveredSlice < 0) ctx->hoveredSlice = sliceCount - 1;
                    else ctx->hoveredSlice = (ctx->hoveredSlice - 1 + sliceCount) % sliceCount;
                }
                for (int i = 0; i < 8; i++) g_sliceHover[i] = (i == ctx->hoveredSlice) ? 1.0f : 0.0f;
                RenderFrame(ctx);
                return 0;
            }

            if (wParam == VK_RETURN || wParam == VK_SPACE) {
                int targetSlice = (ctx->hoveredSlice >= 0 && ctx->hoveredSlice < sliceCount) ? ctx->hoveredSlice : 0;
                ExecuteAction(ctx, targetSlice);
                return 0;
            }

            if (wParam == VK_BACK) {
                if (ctx->uiState == UI_STATE_RADIAL_SUB) {
                    ctx->uiState = UI_STATE_RADIAL_ROOT;
                    ctx->activeSubmenu = SUBMENU_NONE;
                    ctx->hoveredSlice = -1;
                    for (int i = 0; i < 8; i++) g_sliceHover[i] = 0.0f;
                    RenderFrame(ctx);
                    return 0;
                }
            }

            if (wParam == 'C' || wParam == 'c') {
                ExecuteAction(ctx, 0); // Convert
            } else if (wParam == 'E' || wParam == 'e') {
                ExecuteAction(ctx, 1); // Edit
            } else if (wParam == 'M' || wParam == 'm') {
                ExecuteAction(ctx, 2); // Compress
            } else if (wParam == 'R' || wParam == 'r') {
                InitRenameHUD(ctx);
                RenderFrame(ctx);
            }
            return 0;
        }

        case WM_ACTIVATE: {
            if (LOWORD(wParam) == WA_INACTIVE && ctx->uiState < UI_STATE_RENAME_HUD) {
                Magify_CloseOverlay(ctx);
            }
            return 0;
        }

        case WM_DESTROY: {
            if (ctx) Magify_CancelWorker(ctx);
            if (ctx && ctx->pdfOrganizer) {
                Magify_PdfOrganizer_Free((MagifyPdfOrganizerState*)ctx->pdfOrganizer);
                ctx->pdfOrganizer = NULL;
            }
            if (ctx && ctx->cropPreviewPixels) {
                free(ctx->cropPreviewPixels);
                ctx->cropPreviewPixels = NULL;
            }
            if (g_hDIB) { DeleteObject(g_hDIB); g_hDIB = NULL; }
            if (g_hMemDC) { DeleteDC(g_hMemDC); g_hMemDC = NULL; }
            if (g_hTextDIB) { DeleteObject(g_hTextDIB); g_hTextDIB = NULL; }
            if (g_hTextDC) { DeleteDC(g_hTextDC); g_hTextDC = NULL; }
            g_pixels = NULL;
            g_textPixels = NULL;
            g_surfWidth = 0;
            g_surfHeight = 0;
            Magify_Image_Shutdown();
            if (ctx && ctx->isBackgroundMode) {
                ctx->hwndOverlay = NULL;
                return 0;
            }
            PostQuitMessage(0);
            return 0;
        }
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

HWND Magify_CreateOverlayWindow(HINSTANCE hInst, int width, int height, POINT cursor) {
    HMONITOR hMon = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { .cbSize = sizeof(MONITORINFO) };
    GetMonitorInfoW(hMon, &mi);

    int x = cursor.x - (width / 2);
    int y = cursor.y - (height / 2);

    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top)  y = mi.rcWork.top;
    if (x + width > mi.rcWork.right)   x = mi.rcWork.right - width;
    if (y + height > mi.rcWork.bottom) y = mi.rcWork.bottom - height;

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"MagifyOverlayClass";

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"MagifyOverlayClass",
        L"Magify",
        WS_POPUP,
        x, y, width, height,
        NULL, NULL, hInst, g_overlayCtx
    );

    return hwnd;
}

void Magify_ShowOverlay(MagifyContext *ctx) {
    g_overlayCtx = ctx;

    float dpiScale = Magify_GetDpiScaleForPoint(ctx->spawnCursor);
    ctx->dpiScale = dpiScale;
    if (ctx->uiState == UI_STATE_RADIAL_ROOT || ctx->uiState == UI_STATE_RADIAL_SUB) {
        ctx->overlayWidth = (int)(MAGIFY_RADIAL_WIDTH * ctx->dpiScale);
        ctx->overlayHeight = (int)(MAGIFY_RADIAL_HEIGHT * ctx->dpiScale);
    }

    ctx->themePalette = Magify_GetActiveTheme();
    Magify_Image_Init();

    HDC hdcScreen = GetDC(NULL);
    InitDIB(hdcScreen, ctx->overlayWidth, ctx->overlayHeight);
    ReleaseDC(NULL, hdcScreen);

    ctx->animProgress = 0.0f;
    ctx->closeProgress = 0.0f;
    ctx->isClosing = FALSE;
    ctx->hoveredSlice = -1;

    ctx->hwndOverlay = Magify_CreateOverlayWindow(
        ctx->hInstance,
        ctx->overlayWidth,
        ctx->overlayHeight,
        ctx->spawnCursor
    );

    if (!ctx->hwndOverlay) return;

    ShowWindow(ctx->hwndOverlay, SW_SHOW);
    UpdateWindow(ctx->hwndOverlay);

    DWORD foreThread = GetWindowThreadProcessId(GetForegroundWindow(), NULL);
    DWORD curThread = GetCurrentThreadId();
    if (foreThread && foreThread != curThread) {
        AttachThreadInput(curThread, foreThread, TRUE);
        SetForegroundWindow(ctx->hwndOverlay);
        SetFocus(ctx->hwndOverlay);
        AttachThreadInput(curThread, foreThread, FALSE);
    } else {
        SetForegroundWindow(ctx->hwndOverlay);
        SetFocus(ctx->hwndOverlay);
    }

    RenderFrame(ctx);
}
