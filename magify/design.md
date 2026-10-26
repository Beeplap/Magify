# Magify — Brand, Logo & Radial UI/UX Specification (`design.md`)

## 1. Brand Identity & Logo Design
- **Name**: Magify
- **Visual Metaphor**: A futuristic **Magnifying Glass Loop** fused with a **4-Blade Camera Aperture** (Image mastery) and a **Folded Document Corner** (PDF mastery).
- **Color Palette**:
  - **Deep Obsidian Glass (Background)**: `#12131A` at `88%` opacity (`rgba(18, 19, 26, 0.88)`)
  - **Magify Electric Violet (Primary Accent)**: `#7C3AED` -> `#A855F7`
  - **Cyber Cyan (Active Hover / Highlight)**: `#06B6D4` -> `#22D3EE`
  - **Emerald Success**: `#10B981`
  - **Coral Danger / Delete**: `#F43F5E`
  - **Pure Crisp Text**: `#F8FAFC`
  - **Muted Subtext**: `#94A3B8`

### Master Logo SVG (`assets/magify.svg`)
Save this SVG and convert it to `assets/magify.ico` (16x16, 32x32, 48x48, 256x256) so it appears sharp in the Windows right-click menu:

```xml
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <defs>
    <linearGradient id="magGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#22D3EE" />
      <stop offset="50%" stop-color="#7C3AED" />
      <stop offset="100%" stop-color="#EC4899" />
    </linearGradient>
    <linearGradient id="darkCore" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="#1E1B4B" />
      <stop offset="100%" stop-color="#0F172A" />
    </linearGradient>
  </defs>
  <!-- Rounded Squircle Badge for Windows Context Menu Visibility -->
  <rect x="16" y="16" width="224" height="224" rx="52" fill="url(#darkCore)" stroke="url(#magGrad)" stroke-width="10"/>
  <!-- Document / PDF Fold inside Lens -->
  <path d="M82 68 H130 L154 92 V156 C154 162.6 148.6 168 142 168 H82 C75.4 168 70 162.6 70 156 V80 C70 73.4 75.4 68 82 68 Z" fill="#1E293B" stroke="#22D3EE" stroke-width="6"/>
  <path d="M130 68 V92 H154" fill="none" stroke="#22D3EE" stroke-width="6" stroke-linejoin="round"/>
  <!-- Glowing Lens Ring -->
  <circle cx="114" cy="118" r="58" fill="none" stroke="url(#magGrad)" stroke-width="16" />
  <!-- Image Sparkle / Crop Star inside Lens -->
  <path d="M104 102 L110 116 L124 122 L110 128 L104 142 L98 128 L84 122 L98 116 Z" fill="#F8FAFC"/>
  <!-- Magnifier Handle -->
  <path d="M158 162 L202 206" stroke="url(#magGrad)" stroke-width="22" stroke-linecap="round"/>
</svg>
```

## 2. Radial Menu Geometry & Layout (Level 1)

When invoked, `magify.exe` spawns a 420x420 (scaled by DPI) per-pixel transparent window centered at `(cursor.x, cursor.y)`.

```text
                  [ 🔼 CONVERT ]
                 (225° to 315°)
                       |
   [ ◀️ RENAME ] ------( 🔮 )------ [ ▶️ EDIT ]
  (135° to 225°)   Center Hub     (315° to 45°)
                   (File Info)
                       |
                 [ 🔽 COMPRESS ]
                  (45° to 135°)
```

- **Center Hub (Radius: 0px -> 48px)**:
  - Displays the Magify Icon, detected file type badge (PDF or PNG), and original file size (2.4 MB).
  - Clicking the Center Hub or pressing `ESC` closes the overlay.
- **Inner Ring (Radius: 54px -> 140px) — 4 Primary Sectors**:
  - **Top (225°–315°) — CONVERT**: Opens format target sub-ring.
  - **Right (315°–45°) — EDIT**: Opens contextual Image or PDF sub-ring / editor.
  - **Bottom (45°–135°) — COMPRESS**: Opens smart compression presets sub-ring.
  - **Left (135°–225°) — RENAME**: Morphs into an instant floating text box with the filename pre-selected (excluding extension).

## 3. Contextual Sub-Rings & Editor Modes (Level 2)

### A. When an IMAGE is Selected (.png, .jpg, .webp, .bmp)
- **Click EDIT $\rightarrow$ Expands Outer Ring (Radius: 146px -> 205px)**:
  - Rotate 90° CW (Instant preview & save)
  - Rotate 90° CCW
  - Flip Horizontal / Vertical
  - Resize Dimensions (50%, 75%, 1080p, Custom px)
  - Interactive Crop (Expands window into a sleek borderless Crop Canvas with Free, 1:1, 4:3, 16:9, and Passport/ID aspect ratio locks)
- **Click CONVERT $\rightarrow$ Outer Ring Options**:
  - To PDF (Wraps image(s) into a clean A4 or Fit-to-Image PDF)
  - To PNG
  - To JPG
  - To WEBP
  - To ICO (Generates multi-size Windows .ico)
- **Click COMPRESS $\rightarrow$ Outer Ring Options**:
  - Lossless Optimize (Strips metadata, optimizes Huffman/deflate)
  - Balanced (80% Quality) (Shows estimated output size)
  - Maximum Web (60% Quality)
  - Target Size (< 200 KB / < 500 KB) (Auto-binary-searches JPEG/WebP quality to fit strict portal upload limits!)

### B. When a PDF is Selected (.pdf)
- **Click EDIT $\rightarrow$ Outer Ring Options**:
  - Rotate All Pages 90° CW / CCW
  - Split PDF (Every page to separate PDF, or custom range 1-3, 5)
  - Merge PDFs (Combines all selected PDFs, or opens quick "+ Add File" picker)
  - Page Organizer HUD (Opens a fast thumbnail grid where user can click to rotate individual pages, delete pages, or drag to reorder)
- **Click CONVERT $\rightarrow$ Outer Ring Options**:
  - PDF -> JPG Pages (Exports high-DPI images into `<pdfname>_pages/`)
  - PDF -> PNG Pages (300 DPI crisp extraction)
  - Extract All Text (.txt)
- **Click COMPRESS $\rightarrow$ Outer Ring Options**:
  - Light (Print Quality - 300 DPI)
  - Recommended (Standard - 150 DPI)
  - Extreme (Email/Portal - 96 DPI)

## 4. Animation Physics & Micro-Interactions
- **Spawn Animation (140ms)**:
  - Radius expands from $r = 10\text{px}$ to $r = 140\text{px}$ using an ease-out back curve ($\text{overshoot} = 1.06$) while opacity fades from 0% -> 100%.
  - Each of the 4 slices staggers by 12ms for a mechanical shutter effect.
- **Hover Physics**:
  - Hovered slice smoothly scales its outer radius from 140px -> 150px and shifts fill color from `#1E2230` to a radial gradient of `#7C3AED` -> `#06B6D4`.
- **Keyboard Shortcuts Inside Radial Menu**:
  - `R` = Rename, `C` = Convert, `M` = Compress, `E` = Edit, `ESC` = Cancel.
