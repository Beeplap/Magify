# Magify — Codex / CLI Coding Agent Instructions (`codex.md`)

> **Target Environment**: Windows 10/11 (x64), C11/C17, MinGW-w64 (`gcc` + `windres`) or MSVC (`cl` + `rc`), Git Bash / PowerShell.

## 1. Non-Negotiable Coding Rules for AI Agents
1. **Pure C Only**: Compile as C11 (`-std=c11`). Do not introduce C++ classes, templates, or STL. When calling COM interfaces (like Direct2D or WIC/Shell), use `COBJMACROS` in C.
2. **Unicode Entry Point**:
   - Always use `int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)`.
   - Parse arguments with `CommandLineToArgvW(GetCommandLineW(), &argc)`.
3. **No Console Window Flash**:
   - Always link with `-mwindows` (MinGW/Clang) or `/SUBSYSTEM:WINDOWS` (MSVC) so right-clicking a file never flashes a black `cmd.exe` box.
4. **Error Handling**:
   - Never silently fail a file operation. Set `ctx->statusText` (e.g., `L"Saved compressed PDF (-64%)"` or `L"Error: File is locked by another app"`) and show a brief 800ms toast/ring animation before closing.

## 2. Build & Verification Commands
Run these commands in the terminal after every code modification:

```bash
# 1. Compile resource file (embeds Magify logo icon & High-DPI manifest)
windres assets/magify.rc -O coff -o build/magify.res

# 2. Compile Magify executable (Optimized, No Console, Unicode)
gcc -std=c11 -O3 -Wall -Wextra -municode -mwindows \
    -Iinclude -Ithird_party \
    src/main.c src/ipc.c src/registry.c src/ui_overlay.c \
    src/ops_common.c src/ops_image.c src/ops_pdf.c \
    build/magify.res \
    -lgdi32 -luser32 -lshell32 -lole32 -lshlwapi -lgdiplus -ldwmapi \
    -o build/magify.exe

# 3. Register context menu for current user
./build/magify.exe --install

# 4. Test overlay with a sample image or PDF
./build/magify.exe "test_assets/sample.pdf"
```

## 3. Core Data Structure Contract (`include/magify.h`)

Keep `MagifyContext` as the single source of truth:

```c
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

typedef struct {
    HINSTANCE       hInstance;
    HWND            hwndOverlay;
    POINT           spawnCursor;        // Screen coordinates where right-click invoked
    float           dpiScale;           // 1.0f = 96 DPI, 1.25f = 120 DPI, etc.
    
    WCHAR           files[64][MAX_PATH];
    int             fileCount;
    MagifyFileClass primaryClass;
    uint64_t        totalSizeBytes;

    MagifyUIState   uiState;
    int             hoveredSlice;       // -1 = center/none, 0..N = active pie slice
    float           animProgress;       // 0.0f -> 1.0f spring value
    volatile LONG   jobProgress;        // 0..100 for background thread
    WCHAR           statusText[128];
} MagifyContext;
```
