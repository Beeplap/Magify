# Magify — Engineering & Architecture Guidance (`guidance.md`)

## 1. Project Overview
**Magify** (`magify.exe`) is an ultra-fast, native Windows utility written in **C (C11/C17)** that integrates directly into the Windows Explorer right-click context menu for all file types (`*`). When invoked, it spawns a borderless, per-pixel alpha-blended **Radial (Circular) Overlay Menu** centered at the mouse cursor, morphing smoothly into a contextual HUD for **Images**, **PDFs**, and **General Files**.

## 2. Core Engineering Philosophy
1. **Sub-15ms Cold Startup**: No heavy runtimes, no WebView2, no Electron, no .NET. Pure Win32 C API + native graphics.
2. **Single-Binary Portability**: The core executable (`magify.exe`) embeds its own icons, shaders/resources, and installer logic via Windows Resource scripts (`.rc`).
3. **Non-Destructive File Safety**:
   - Never overwrite a user's source file in-place without an atomic swap (`MoveFileExW` with `MOVEFILE_REPLACE_EXISTING`) or saving as `<filename>_magify.<ext>` by default (configurable via `Shift+Click` for in-place overwrite).
4. **Unicode-First (`UTF-16LE` / `UTF-8`)**:
   - All Windows API calls MUST use Wide Character variants (`WCHAR`, `CreateWindowExW`, `CreateFileW`, `RegSetValueExW`) so file paths in Nepali, Japanese, Korean, or emojis never break.
5. **Zero Memory Leaks**:
   - Use an **Arena Allocator** (`MagifyArena`) for per-session UI and file-processing allocations so memory is freed instantaneously on exit.

## 3. Recommended Directory Structure
```text
magify/
├── CMakeLists.txt              # Build configuration (MinGW-w64 / Clang / MSVC)
├── Makefile                    # Fast CLI build wrapper
├── guidance.md                 # Project principles & architecture
├── agents.md                   # Multi-agent role definitions
├── codex.md                    # Autonomous CLI coding agent instructions
├── design.md                   # UI/UX, radial menu math, & logo specs
├── skill.md                    # Reusable Win32 & C implementation recipes
├── plan.md                     # Phased execution roadmap
├── assets/
│   ├── magify.svg              # Master vector logo
│   ├── magify.ico              # Multi-resolution icon (16, 32, 48, 256px)
│   └── magify.rc               # Windows resource script (embeds icon & manifest)
├── include/
│   ├── magify.h                # Master header, arena allocator, app state
│   ├── ipc.h                   # Named pipe multi-file aggregation
│   ├── registry.h              # Context menu install/uninstall API
│   ├── ui_overlay.h            # Layered window, D2D/GDI+ rendering, spring physics
│   ├── ops_common.h            # Rename, generic compress (ZIP), file info
│   ├── ops_image.h             # Crop, rotate, resize, convert, compress image
│   └── ops_pdf.h               # Merge, split, rotate, compress, PDF<->Image
├── src/
│   ├── main.c                  # Entry point (wWinMain), CLI parser, IPC dispatch
│   ├── ipc.c                   # Collects multiple selected files from Explorer
│   ├── registry.c              # HKCU\Software\Classes\*\shell\Magify handler
│   ├── ui_overlay.c            # Radial pie menu & contextual editor HUD
│   ├── ops_common.c            # Fast inline rename & archive compression
│   ├── ops_image.c             # Image processing implementation
│   └── ops_pdf.c               # PDF manipulation & page preview engine
└── third_party/
    ├── stb_image.h
    ├── stb_image_write.h
    ├── stb_image_resize2.h
    └── pdfium/                 # Or mupdf headers & static/import libs
```

## 4. File Type Detection Matrix
Do not rely solely on file extensions. Read the first 16 bytes (Magic Bytes) alongside the extension:
- **PDF**: Starts with `%PDF-` (`0x25 0x50 0x44 0x46 0x2D`) $\rightarrow$ Route to `FILE_CLASS_PDF`
- **PNG**: `89 50 4E 47 0D 0A 1A 0A` $\rightarrow$ Route to `FILE_CLASS_IMAGE`
- **JPEG**: `FF D8 FF` $\rightarrow$ Route to `FILE_CLASS_IMAGE`
- **WEBP**: `RIFF....WEBP` $\rightarrow$ Route to `FILE_CLASS_IMAGE`
- **BMP**: `42 4D` $\rightarrow$ Route to `FILE_CLASS_IMAGE`
- **Other**: Route to `FILE_CLASS_GENERIC` (enables Rename, Convert to PDF/Archive, ZIP Compress).

## 5. Multi-File Selection Handling
When a user selects 5 images in Windows Explorer and clicks "Modify with Magify", Explorer spawns 5 processes (`magify.exe "file1.jpg"`, `magify.exe "file2.jpg"`, ...).
1. The first process creates a mutex/named pipe (`\\.\pipe\MagifySession`) and waits 45ms to collect incoming file paths from sibling processes.
2. Sibling processes detect the active pipe, write their `argv[1]` (`WCHAR` path) to the pipe, and exit immediately (0.5ms).
3. The primary process opens the Radial Menu with batch support (e.g., "5 Images Selected $\rightarrow$ Merge to PDF / Batch Compress / Batch Convert").
