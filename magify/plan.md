# Magify — Phased Execution Plan (`plan.md`)

## Phase 0: Project Scaffold, Icon & Resource Pipeline
- [x] Create directory structure (`src/`, `include/`, `assets/`, `third_party/`, `build/`).
- [x] Generate `assets/magify.svg` and compile multi-resolution `assets/magify.ico`.
- [x] Create `assets/magify.rc` with embedded icon (`IDI_MAGIFY_ICON`) and Per-Monitor V2 High-DPI application manifest.
- [x] Set up `Makefile` / `CMakeLists.txt` compiling a minimal zero-console `magify.exe`.

## Phase 1: Context Menu Installer & Single-Instance IPC
- [x] Implement `src/registry.c` (`--install` and `--uninstall` CLI flags + GUI dashboard when launched with zero arguments).
- [x] Verify right-clicking any file in Windows Explorer shows **"Modify with Magify"** with the custom Magify logo.
- [x] Implement `src/ipc.c` Named Pipe collector so selecting multiple files in Explorer aggregates all paths into one overlay session.
- [x] Implement Magic Byte + Extension file classifier (`PDF`, `Image`, `Generic`).

## Phase 2: 120fps Radial Overlay Engine (`WS_EX_LAYERED`)
- [x] Implement `src/ui_overlay.c` with GDI+ Flat API / Direct2D rendering onto a 32-bit premultiplied ARGB DIB Section.
- [x] Implement screen-edge clamping so the radial menu never clips off-screen near taskbar/edges.
- [x] Build the 4-slice Level-1 Radial Menu (`Convert`, `Edit`, `Compress`, `Rename`) + Center File Info Hub.
- [x] Add cubic-bezier / spring open/close animations (`140ms` open, `80ms` close) and polar coordinate hover glow.
- [x] Implement click-outside (`WM_NCACTIVATE` / `WM_KILLFOCUS`) and `ESC` instant dismissal.

## Phase 3: Core File Operations & Instant Rename HUD
- [x] Build the **Inline Rename HUD**: clicking `Rename` smoothly morphs the radial ring into a sleek glass input bar with the base filename highlighted (`Enter` to apply via `MoveFileExW`, `ESC` to cancel).
- [x] Build generic ZIP archive compression for non-Image/non-PDF files.

## Phase 4: Image Processing Engine (`src/ops_image.c`)
- [x] Integrate GDI+ Flat C API (lossless PNG, high-quality JPEG, BMP, ICO, and native PDF 1.4 generator).
- [x] Implement Level-2 **Image Convert** sub-ring (`PNG`, `JPG`, `BMP`, `ICO`, `Image(s) -> PDF`).
- [x] Implement Level-2 **Image Compress** sub-ring (`Lossless`, `80% Balanced`, `60% Web`, `< 200 KB Target`).
- [x] Implement Level-2 **Image Edit** sub-ring:
  - Instant `Rotate 90° CW`, `Rotate 90° CCW`, `Flip H`, `Flip V`.
  - Quick Resize presets (`50%`, `1080p`, `Custom`).
  - Interactive **Crop Mode** overlay with aspect-ratio locks (`Free`, `1:1`, `4:3`, `16:9`, `Passport 35:45`).

## Phase 5: PDF Controller & Editor Engine (`src/ops_pdf.c`)
- [ ] Integrate PDF engine (PDFium C API / MuPDF + native lossless PDF writer for fast image-to-PDF and stream manipulation).
- [ ] Implement Level-2 **PDF Compress** sub-ring (`Light 300 DPI`, `Recommended 150 DPI`, `Extreme 96 DPI`).
- [ ] Implement Level-2 **PDF Convert** sub-ring (`PDF -> JPG Pages`, `PDF -> PNG Pages`, `Extract Text`).
- [ ] Implement Level-2 **PDF Edit** sub-ring:
  - `Rotate All Pages CW/CCW`.
  - `Merge PDFs` (merges multi-selected PDFs or allows adding files).
  - `Split PDF` (range input or burst into single pages).
  - **Visual Page Organizer HUD**: Fast thumbnail grid to rotate individual pages, delete unwanted pages, reorder pages, and save.

## Phase 6: Polish, Toast Feedback & Installer Packaging
- [ ] Add circular progress indicator in the Center Hub with percentage & size savings report (e.g., `4.2 MB -> 890 KB (-78%)`).
- [ ] Hold `Shift` while clicking any action to overwrite original file in-place; default click saves non-destructively or per user preference.
- [ ] Stress-test Unicode file paths, read-only/locked files, and 4K High-DPI displays.
