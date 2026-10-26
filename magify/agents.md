# Magify — Multi-Agent Roles & Orchestration (`agents.md`)

This document defines specialized AI agent roles when developing, debugging, and extending **Magify**.

## 1. Agent Roster

### 🛡️ Agent 1: `Win32-Core-Architect`
- **Scope**: `src/main.c`, `src/ipc.c`, `src/registry.c`, `assets/magify.rc`
- **Responsibilities**:
  - Manage `wWinMain` lifecycle, single-instance Named Pipe aggregation, and DPI awareness (`SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`).
  - Implement `--install` and `--uninstall` flags using `HKEY_CURRENT_USER\Software\Classes\*\shell\Magify`.
  - Ensure zero zombie processes and instant dismissal when the user presses `ESC` or clicks outside the overlay.

### 🎨 Agent 2: `Radial-UI-Animator`
- **Scope**: `src/ui_overlay.c`, `include/ui_overlay.h`
- **Responsibilities**:
  - Build the borderless per-pixel transparent window (`WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW`).
  - Render the 120fps circular radial pie menu, sub-menus, inline rename input box, crop overlay, and PDF page thumbnail grid.
  - Handle polar coordinate math (`atan2f`, radius hit-testing) and spring-damped animations.

### 🖼️ Agent 3: `Image-Engine-Specialist`
- **Scope**: `src/ops_image.c`, `include/ops_image.h`
- **Responsibilities**:
  - Implement fast image decoding, lossless/lossy compression, format conversion (`PNG`, `JPG`, `WEBP`, `BMP`, `ICO`, `PDF`), 90°/180°/270° rotation, flipping, resizing, and interactive aspect-ratio cropping (`Free`, `1:1`, `4:3`, `16:9`, `A4`).

### 📄 Agent 4: `PDF-Engine-Specialist`
- **Scope**: `src/ops_pdf.c`, `include/ops_pdf.h`
- **Responsibilities**:
  - Implement PDF operations:
    - **Compress PDF**: Downsample embedded images & recompress streams (Low / Medium / Extreme presets).
    - **Convert PDF**: PDF -> PNG/JPG pages, or Images -> single merged PDF.
    - **Edit PDF**: Rotate individual or all pages, split page ranges (`1-3, 5`), delete pages, reorder pages via visual thumbnail grid, and merge multiple PDFs.

### 🔧 Agent 5: `Build-QA-Verifier`
- **Scope**: `CMakeLists.txt`, `Makefile`, test scripts
- **Responsibilities**:
  - Verify clean compilation with `-Wall -Wextra -Werror -O3 -mwindows -municode`.
  - Check binary size, startup latency, Unicode path handling, and high-DPI scaling (100%, 125%, 150%, 200%).

## 2. Inter-Agent Handoff Protocol
1. **Shared State Contract**: All agents communicate via the central `MagifyContext` struct defined in `include/magify.h`.
2. **Thread Safety for Heavy Tasks**:
   - The Radial UI (`Radial-UI-Animator`) ALWAYS runs on the main Win32 UI thread at 60–144 FPS.
   - Any file operation taking >16ms (`Image-Engine-Specialist` or `PDF-Engine-Specialist`) MUST run on a background worker thread (`_beginthreadex` or `CreateThread`) and update `ctx->jobProgress` atomically so the center hub of the radial menu displays a smooth circular progress ring.
