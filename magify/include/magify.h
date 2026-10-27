#ifndef MAGIFY_H
#define MAGIFY_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#define MAGIFY_VERSION L"1.0.0"
#define MAGIFY_PIPE_NAME L"\\\\.\\pipe\\MagifySession"
#define IDI_MAGIFY_ICON 101

#define MAGIFY_MAX_FILES 64
#define MAGIFY_RADIAL_WIDTH  440
#define MAGIFY_RADIAL_HEIGHT 440

typedef enum {
    FILE_CLASS_GENERIC = 0,
    FILE_CLASS_IMAGE   = 1,
    FILE_CLASS_PDF     = 2
} MagifyFileClass;

typedef enum {
    UI_STATE_RADIAL_ROOT = 0,   // Main 4-slice pie menu: Rename, Convert, Compress, Edit
    UI_STATE_RADIAL_SUB  = 1,   // Sub-ring options (e.g., Convert -> PNG, JPG, WEBP, PDF)
    UI_STATE_RENAME_HUD  = 2,   // Floating glass input pill for instant rename
    UI_STATE_IMAGE_EDIT  = 3,   // Interactive Crop / Rotate / Resize canvas HUD
    UI_STATE_PDF_EDIT    = 4,   // Page thumbnail grid, Rotate, Split, Merge, Delete
    UI_STATE_PROCESSING  = 5,   // Circular progress ring animation
    UI_STATE_CLOSING     = 6    // 80ms shrink-out animation
} MagifyUIState;

typedef enum {
    SUBMENU_NONE = 0,
    SUBMENU_CONVERT,
    SUBMENU_EDIT,
    SUBMENU_COMPRESS
} MagifySubmenuType;

typedef struct {
    BOOL isLightMode;
    // Hub
    uint8_t hubBgR, hubBgG, hubBgB, hubBgA;
    uint8_t hubBdrR, hubBdrG, hubBdrB;
    // Slices normal
    uint8_t sliceNormR, sliceNormG, sliceNormB, sliceNormA;
    // Slices hover gradient
    uint8_t sliceHovR1, sliceHovG1, sliceHovB1;
    uint8_t sliceHovR2, sliceHovG2, sliceHovB2;
    // Edge glow
    uint8_t glowR, glowG, glowB;
    // Track background
    uint8_t trackR, trackG, trackB, trackA;
    // Text colors (COLORREF)
    COLORREF textPrimary;
    COLORREF textSecondary;
    COLORREF textAccent;
    COLORREF cardBg;
    COLORREF cardBorder;
} MagifyThemePalette;

typedef struct {
    HINSTANCE       hInstance;
    HWND            hwndOverlay;
    POINT           spawnCursor;        // Screen coordinates where right-click invoked
    float           dpiScale;           // 1.0f = 96 DPI, 1.25f = 120 DPI, etc.

    WCHAR           files[MAGIFY_MAX_FILES][MAX_PATH];
    int             fileCount;
    MagifyFileClass primaryClass;
    uint64_t        totalSizeBytes;
    BOOL            isBackgroundMode;

    MagifyUIState   uiState;
    MagifySubmenuType activeSubmenu;
    int             hoveredSlice;       // -1 = center/none, 0..N = active pie slice
    float           animProgress;       // 0.0f -> 1.0f spring value
    float           closeProgress;      // 0.0f -> 1.0f during closing
    BOOL            isClosing;

    // Background Worker Thread & Progress State
    HANDLE          hWorkerThread;
    volatile LONG   isProcessing;       // 0 = idle, 1 = working
    volatile LONG   progressPercent;    // 0..100 for background thread
    volatile LONG   jobProgress;        // compatibility mirror
    WCHAR           progressStatusText[128];
    WCHAR           statusText[128];    // compatibility mirror
    uint64_t        originalTotalBytes;
    uint64_t        finalTotalBytes;
    BOOL            overwriteInPlace;   // Shift+Click was held
    volatile LONG   cancelRequested;    // 1 = cancel requested
    BOOL            isSuccessState;
    BOOL            isErrorState;
    DWORD           completionTick;
    float           hubExpandAnim;      // 0.0f -> 1.0f for center hub expansion

    // Rename HUD buffer & state
    WCHAR           renameBuf[MAX_PATH];
    WCHAR           renameExt[32];
    int             renameCursor;
    int             renameSelStart;
    int             renameSelEnd;
    BOOL            renameCaretVisible;

    // Crop HUD state
    float           cropNormX;
    float           cropNormY;
    float           cropNormW;
    float           cropNormH;
    int             cropOrigW;
    int             cropOrigH;
    int             cropAspectLock;      // 0=Free, 1=1:1, 2=4:3, 3=16:9, 4=35:45
    int             cropDragMode;        // -1=none, 0..7=handles, 8=pan
    POINT           cropDragStartPt;
    float           cropDragStartX;
    float           cropDragStartY;
    float           cropDragStartW;
    float           cropDragStartH;
    void           *cropGdiBitmap;       // GpBitmap* preview
    uint32_t       *cropPreviewPixels;   // Resized ARGB preview
    int             cropPreviewW;
    int             cropPreviewH;

    // PDF Organizer HUD state
    void           *pdfOrganizer;       // MagifyPdfOrganizerState*

    // Theme palette
    MagifyThemePalette themePalette;

    // Overlay surface geometry
    int             overlayWidth;
    int             overlayHeight;
} MagifyContext;

// Detection, formatting & theme helpers
MagifyFileClass Magify_DetectFileClass(const WCHAR *filePath);
void Magify_FormatFileSize(uint64_t bytes, WCHAR *outStr, size_t maxLen);
uint64_t Magify_GetFileSize(const WCHAR *filePath);
BOOL Magify_IsSystemLightTheme(void);
MagifyThemePalette Magify_GetActiveTheme(void);

#endif // MAGIFY_H
