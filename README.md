# Magify

> **Lightning-fast, zero-dependency circular radial overlay & contextual quick-actions utility for Windows Explorer, crafted in pure C11 & Win32.**

[![Build](https://img.shields.io/badge/Build-GCC%2016%20%7C%20MSVC-blue.svg)](#building)
[![Language](https://img.shields.io/badge/Language-Pure%20C11-00599C.svg)](#architecture)
[![Platform](https://img.shields.io/badge/Platform-Windows%2010%2F11%20x64-0078D6.svg)](#)
[![DPI](https://img.shields.io/badge/DPI-Per--Monitor%20V2-success.svg)](#features)
[![License](https://img.shields.io/badge/License-MIT-lightgrey.svg)](#license)

---

## Overview

**Magify** brings modern, tactile radial context menus directly to Windows File Explorer. Right-click any file (or selection of files) and select **Magify** to summon a sleek, 120fps hardware-accelerated radial HUD right at your cursor position.

Convert image formats, generate multi-resolution `.ico` files, compile images into native PDF 1.4 documents, crop with live aspect-ratio guides, compress under strict byte-size targets (e.g., strictly under 200 KB for web uploads), batch rename with an inline floating glass HUD, or package files into PKZIP archives—all with zero bloat and near-instant startup.

---

## Key Features

### 🎛️ 120fps Spring-Animated Radial HUD
- **32-Bit Premultiplied ARGB DIB Surface**: Rendered directly via `UpdateLayeredWindow` with full per-pixel alpha transparency and subtle drop shadows.
- **Pure-C11 SDF Polar Renderer**: Anti-aliased circular sectors, inner hub badge, and glow rings rendered using signed-distance fields with zero external graphics library dependencies.
- **Polar Hit-Testing**: Instant cursor angle (`atan2f`) and distance hit-testing with interactive hover expansion (`140px -> 150px`) and Cyber Cyan / Electric Violet accent glows.
- **Instant Dismissal**: Dismisses gracefully on `Esc`, clicking outside (`WM_ACTIVATE` / `WM_KILLFOCUS`), right-clicking, or clicking the center hub.

### ⚡ Windows Shell & High-Speed IPC
- **1-Click Context Menu Integration**: Registers under `HKCU\Software\Classes\*\shell\Magify` without requiring administrator privileges (`MultiSelectModel = Player`, custom executable icon).
- **Named Pipe Aggregator (`\\.\pipe\MagifySession`)**: When dozens of files are selected in Explorer, a single instance aggregates all file paths within 45ms.
- **Standalone Control Dashboard**: Double-clicking `magify.exe` with no arguments opens a sleek dark-themed Win32 Control Center for checking status, installing/uninstalling context menus, and testing overlays.
- **Per-Monitor V2 DPI Awareness**: Seamless crisp rendering across mixed-DPI displays (100%, 125%, 150%, 200%).

### 🖼️ Pure-C11 Image Processing Engine
- **Flat GDI+ C API**: Zero C++ runtime overhead—directly interoperates with Windows Flat GDI+ exports.
- **Format Conversion**: Convert between `PNG`, `JPEG`, `BMP`, and multi-resolution Windows `ICO` (`16x16`, `32x32`, `48x48`, `256x256` embedded PNG chunks).
- **Native PDF 1.4 Generator**: Converts single or multiple images directly into valid, standards-compliant PDF documents with embedded `/DCTDecode` streams and exact cross-reference (`xref`) tables.
- **Transformations**: 90° CW, 90° CCW, 180° rotation, Horizontal and Vertical flipping.
- **Target-Byte Compression**: Automatically binary-searches JPEG quality and applies progressive bicubic downsampling to guarantee outputs strictly under target thresholds (e.g. `< 200 KB`, `< 500 KB`).
- **Interactive Crop HUD**: Floating overlay with 8 draggable boundary handles, dark-scrim backdrop, rule-of-thirds grid, and aspect ratio locks (`Free`, `1:1`, `4:3`, `16:9`, `A4`).
- **Safe Overwrite / Non-Destructive Mode**: Saves as `<filename>_magify.<ext>` by default; hold <kbd>Shift</kbd> during click to replace in-place atomically.

### ✏️ Inline Rename HUD & ZIP Compression
- **Floating Dark-Glass Pill HUD**: Floating in-place rename interface with live caret, text selection (<kbd>Ctrl+A</kbd>), clipboard paste (<kbd>Ctrl+V</kbd>), and locked file extension badge.
- **Atomic & Batch Rename**: Renames single files or batch-renames multi-file selections sequentially (`<name>_1`, `<name>_2`, etc.) via `MoveFileExW`.
- **Pure-C11 PKZIP Writer**: Generates valid IEEE 802.3 CRC-32 PKZIP archives without external libraries, with seamless fallback to background Windows `tar.exe` deflate compression.

---

## Architecture & Directory Structure

```
magify/
├── assets/
│   ├── magify.ico             # 7-size multi-resolution application icon
│   ├── magify.manifest        # Per-Monitor V2 High-DPI XML application manifest
│   ├── magify.rc              # Windows resource compilation script
│   └── magify.svg             # High-resolution vector source logo
├── include/
│   ├── magify.h               # Core context, file classification & UI state definitions
│   ├── ipc.h                  # Named Pipe aggregation IPC server/client
│   ├── registry.h             # Explorer context menu registration hooks
│   ├── ui_overlay.h           # Layered DIB, SDF polar renderer & HUD declarations
│   ├── ops_common.h           # Rename operations & PKZIP archive generator
│   ├── ops_image.h            # Flat GDI+ C engine, crop math & native PDF 1.4 writer
│   └── ops_pdf.h              # PDF operations & manipulation stubs
├── src/
│   ├── main.c                 # wWinMain, argument dispatcher & Control Dashboard
│   ├── ipc.c                  # Named Pipe aggregation implementation
│   ├── registry.c             # HKCU shell registry management
│   ├── ui_overlay.c           # 120fps spring animation, SDF math & HUD drawing
│   ├── ops_common.c           # Rename logic & pure-C PKZIP implementation
│   ├── ops_image.c            # GDI+ format conversions, crop, target compression & PDF 1.4
│   └── ops_pdf.c              # PDF stream & page management
├── tests/
│   └── test_core.c            # 7-suite comprehensive automated regression test
├── Makefile                   # GCC / MinGW-w64 build configuration
├── Makefile.msvc              # NMake / MSVC build configuration
└── build.bat                  # One-click automated build & test script
```

---

## Building

Magify builds cleanly under both **GCC (MinGW-w64)** and **Microsoft Visual C++ (MSVC)** with zero warnings under `-Wall -Wextra` / `/W4`.

### Prerequisites
- Windows 10 (Build 1809+) or Windows 11
- Either:
  - **GCC / MinGW-w64** (e.g. `w64devkit` or MSYS2) with `windres` and `make`
  - **Visual Studio 2022+** (Community/Professional/Build Tools) with C/C++ workload

### Option A: Using GCC / MinGW-w64 (Recommended)
```bash
cd magify
make
```

### Option B: Using the Automated Build Script
```cmd
cd magify
build.bat
```

### Option C: Using MSVC / NMake
```cmd
cd magify
nmake -f Makefile.msvc
```

---

## Running & Testing

### Automated Regression Tests
To run the full 7-suite automated test (verifying magic-byte classification, polar hit-testing, PKZIP generation, atomic rename, GDI+ format conversion, 90° rotation, crop, target compression, and native PDF 1.4 generation):
```bash
cd magify
make test
```

### Registering the Context Menu
To integrate Magify into your Windows Explorer right-click menu:
```cmd
build\magify.exe --install
```
Or launch `magify.exe` without arguments to access the interactive GUI Control Dashboard.

To remove context menu integration:
```cmd
build\magify.exe --uninstall
```

---

## License

Magify is open-source software licensed under the [MIT License](LICENSE).
