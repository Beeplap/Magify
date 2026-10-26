#include "mg_ui.h"
#include "mg_shell.h"
#include "mg_image.h"
#include "mg_pdf.h"

#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <math.h>
#include <stdio.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

using namespace Gdiplus;

#define PI 3.14159265358979323846f
#define DEG2RAD(d) ((d) * (PI / 180.0f))

static ULONG_PTR g_gdiplus_token = 0;
static mg_radial_menu_t *g_current_menu = NULL;

static void init_slices_for_target(mg_radial_menu_t *menu) {
    menu->slice_count = 0;

    if (menu->target.file_type == MG_FILE_TYPE_IMAGE) {
        menu->slice_count = 6;
        // 0: Rotate 90 CW
        menu->slices[0].action = MG_ACTION_IMG_ROTATE;
        menu->slices[0].label = L"Rotate";
        menu->slices[0].sublabel = L"+90° CW";
        menu->slices[0].icon_glyph = L"↻";

        // 1: Crop
        menu->slices[1].action = MG_ACTION_IMG_CROP;
        menu->slices[1].label = L"Crop";
        menu->slices[1].sublabel = L"Center 80%";
        menu->slices[1].icon_glyph = L"✂";

        // 2: Resize
        menu->slices[2].action = MG_ACTION_IMG_RESIZE;
        menu->slices[2].label = L"Resize";
        menu->slices[2].sublabel = L"Scale 50%";
        menu->slices[2].icon_glyph = L"⤢";

        // 3: Compress
        menu->slices[3].action = MG_ACTION_IMG_COMPRESS;
        menu->slices[3].label = L"Compress";
        menu->slices[3].sublabel = L"Optimize";
        menu->slices[3].icon_glyph = L"🗜";

        // 4: Convert
        menu->slices[4].action = MG_ACTION_IMG_CONVERT;
        menu->slices[4].label = L"Convert";
        menu->slices[4].sublabel = L"Make PDF";
        menu->slices[4].icon_glyph = L"⇄";

        // 5: Grayscale
        menu->slices[5].action = MG_ACTION_IMG_GRAYSCALE;
        menu->slices[5].label = L"Filter";
        menu->slices[5].sublabel = L"Grayscale";
        menu->slices[5].icon_glyph = L"◐";
    } else if (menu->target.file_type == MG_FILE_TYPE_PDF) {
        menu->slice_count = 6;
        // 0: Rotate Pages
        menu->slices[0].action = MG_ACTION_PDF_ROTATE;
        menu->slices[0].label = L"Rotate";
        menu->slices[0].sublabel = L"Pages 90°";
        menu->slices[0].icon_glyph = L"↻";

        // 1: Compress PDF
        menu->slices[1].action = MG_ACTION_PDF_COMPRESS;
        menu->slices[1].label = L"Compress";
        menu->slices[1].sublabel = L"Optimize";
        menu->slices[1].icon_glyph = L"🗜";

        // 2: Split
        menu->slices[2].action = MG_ACTION_PDF_SPLIT;
        menu->slices[2].label = L"Split";
        menu->slices[2].sublabel = L"Extract";
        menu->slices[2].icon_glyph = L"✂";

        // 3: Merge
        menu->slices[3].action = MG_ACTION_PDF_MERGE;
        menu->slices[3].label = L"Merge";
        menu->slices[3].sublabel = L"Combine";
        menu->slices[3].icon_glyph = L"⊕";

        // 4: To Images
        menu->slices[4].action = MG_ACTION_PDF_TO_IMG;
        menu->slices[4].label = L"Export";
        menu->slices[4].sublabel = L"To PNG";
        menu->slices[4].icon_glyph = L"🖼";

        // 5: Rename
        menu->slices[5].action = MG_ACTION_GEN_RENAME;
        menu->slices[5].label = L"Rename";
        menu->slices[5].sublabel = L"Smart Tag";
        menu->slices[5].icon_glyph = L"✎";
    } else {
        // Generic files
        menu->slice_count = 6;
        menu->slices[0].action = MG_ACTION_GEN_RENAME;
        menu->slices[0].label = L"Rename";
        menu->slices[0].sublabel = L"Timestamp";
        menu->slices[0].icon_glyph = L"✎";

        menu->slices[1].action = MG_ACTION_GEN_ZIP;
        menu->slices[1].label = L"Archive";
        menu->slices[1].sublabel = L"Make ZIP";
        menu->slices[1].icon_glyph = L"🗜";

        menu->slices[2].action = MG_ACTION_GEN_DUPLICATE;
        menu->slices[2].label = L"Duplicate";
        menu->slices[2].sublabel = L"Clone File";
        menu->slices[2].icon_glyph = L"⧉";

        menu->slices[3].action = MG_ACTION_GEN_HASH;
        menu->slices[3].label = L"Checksum";
        menu->slices[3].sublabel = L"SHA-256";
        menu->slices[3].icon_glyph = L"🔒";

        menu->slices[4].action = MG_ACTION_NONE;
        menu->slices[4].label = L"Properties";
        menu->slices[4].sublabel = L"File Info";
        menu->slices[4].icon_glyph = L"ℹ";

        menu->slices[5].action = MG_ACTION_NONE;
        menu->slices[5].label = L"Cancel";
        menu->slices[5].sublabel = L"Dismiss";
        menu->slices[5].icon_glyph = L"✕";
    }

    float angle_per_slice = 360.0f / (float)menu->slice_count;
    for (int i = 0; i < menu->slice_count; i++) {
        // Offset by -90 deg so slice 0 starts centered at 12 o'clock
        float mid_angle = (float)i * angle_per_slice - 90.0f;
        menu->slices[i].angle_start = mid_angle - (angle_per_slice / 2.0f);
        menu->slices[i].angle_end   = mid_angle + (angle_per_slice / 2.0f);
        menu->slices[i].is_hovered  = false;
        menu->slices[i].hover_anim  = 0.0f;
    }
}

// Draw a circular arc pie ring sector into a GraphicsPath
static void add_annular_sector_to_path(GraphicsPath *path, float cx, float cy, float r_in, float r_out, float start_deg, float sweep_deg) {
    float start_rad = DEG2RAD(start_deg);
    float end_rad   = DEG2RAD(start_deg + sweep_deg);

    // Outer arc
    path->AddArc(cx - r_out, cy - r_out, r_out * 2.0f, r_out * 2.0f, start_deg, sweep_deg);

    // Line to inner arc end
    float in_end_x = cx + r_in * cosf(end_rad);
    float in_end_y = cy + r_in * sinf(end_rad);
    path->AddLine(cx + r_out * cosf(end_rad), cy + r_out * sinf(end_rad), in_end_x, in_end_y);

    // Inner arc backwards
    path->AddArc(cx - r_in, cy - r_in, r_in * 2.0f, r_in * 2.0f, start_deg + sweep_deg, -sweep_deg);

    // Close path to start
    path->CloseFigure();
}

static void render_radial_frame(mg_radial_menu_t *menu) {
    if (!menu || !menu->hwnd) return;

    RECT rc;
    GetClientRect(menu->hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    // Create 32-bit ARGB DIB Section
    HDC screen_dc = GetDC(NULL);
    HDC mem_dc = CreateCompatibleDC(screen_dc);

    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pv_bits = NULL;
    HBITMAP h_dib = CreateDIBSection(mem_dc, &bmi, DIB_RGB_COLORS, &pv_bits, NULL, 0);
    HBITMAP h_old_bmp = (HBITMAP)SelectObject(mem_dc, h_dib);

    // Clear to 100% transparent
    memset(pv_bits, 0, (size_t)width * height * 4);

    {
        Graphics g(mem_dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.SetPixelOffsetMode(PixelOffsetModeHighQuality);

        float cx = (float)menu->center_x;
        float cy = (float)menu->center_y;

        // Spring animation scale factor
        float t = menu->spawn_anim;
        // Damped spring: 1.0 - exp(-7t) * cos(9t)
        float spring_scale = (t >= 1.0f) ? 1.0f : (1.0f - expf(-7.0f * t) * cosf(9.0f * t));
        if (spring_scale < 0.05f) spring_scale = 0.05f;

        float cur_r_in  = menu->inner_radius * spring_scale;
        float cur_r_out = menu->outer_radius * spring_scale;

        // 1. Draw outer ambient backdrop glow / vignette
        float bg_radius = (menu->outer_radius + 35.0f) * spring_scale;
        GraphicsPath bg_path;
        bg_path.AddEllipse(cx - bg_radius, cy - bg_radius, bg_radius * 2.0f, bg_radius * 2.0f);
        PathGradientBrush pgb(&bg_path);
        Color center_glow(140, 15, 18, 28);
        Color edge_glow(0, 15, 18, 28);
        int count = 1;
        pgb.SetCenterColor(center_glow);
        pgb.SetSurroundColors(&edge_glow, &count);
        g.FillPath(&pgb, &bg_path);

        // 2. Draw Slices
        FontFamily font_family(L"Segoe UI");
        Font font_icon(&font_family, 18.0f * spring_scale, FontStyleBold, UnitPixel);
        Font font_label(&font_family, 12.0f * spring_scale, FontStyleBold, UnitPixel);
        Font font_sub(&font_family, 9.5f * spring_scale, FontStyleRegular, UnitPixel);

        StringFormat str_format;
        str_format.SetAlignment(StringAlignmentCenter);
        str_format.SetLineAlignment(StringAlignmentCenter);

        for (int i = 0; i < menu->slice_count; i++) {
            mg_slice_t *sl = &menu->slices[i];
            bool is_hov = (menu->hovered_slice == i && !menu->action_executed);

            // Active hover extrusion: expand outer radius by +14px
            float slice_r_out = cur_r_out + (is_hov ? (14.0f * spring_scale) : 0.0f);
            float sweep = (sl->angle_end - sl->angle_start) - 2.5f; // small 2.5 deg gap between slices
            float start = sl->angle_start + 1.25f;

            GraphicsPath slice_path;
            add_annular_sector_to_path(&slice_path, cx, cy, cur_r_in, slice_r_out, start, sweep);

            if (is_hov) {
                // Vibrant Violet/Cyan hover gradient fill
                LinearGradientBrush hover_brush(
                    PointF(cx, cy - slice_r_out),
                    PointF(cx, cy + slice_r_out),
                    Color(245, 138, 43, 226), // #8A2BE2
                    Color(245, 0, 245, 212)   // #00F5D4
                );
                g.FillPath(&hover_brush, &slice_path);

                // Neon glowing border
                Pen hover_pen(Color(255, 0, 245, 212), 2.2f);
                g.DrawPath(&hover_pen, &slice_path);
            } else {
                // Sleek obsidian slate fill
                SolidBrush norm_brush(Color(220, 24, 28, 42)); // Dark Obsidian
                g.FillPath(&norm_brush, &slice_path);

                Pen border_pen(Color(90, 75, 85, 110), 1.2f);
                g.DrawPath(&border_pen, &slice_path);
            }

            // Draw Icon & Text
            float mid_deg = (sl->angle_start + sl->angle_end) / 2.0f;
            float mid_rad = DEG2RAD(mid_deg);
            float label_radius = (cur_r_in + slice_r_out) * 0.50f;

            float item_x = cx + label_radius * cosf(mid_rad);
            float item_y = cy + label_radius * sinf(mid_rad);

            // Icon glyph
            SolidBrush text_brush(is_hov ? Color(255, 255, 255, 255) : Color(240, 225, 230, 245));
            RectF icon_rc(item_x - 30.0f, item_y - 24.0f, 60.0f, 22.0f);
            g.DrawString(sl->icon_glyph, -1, &font_icon, icon_rc, &str_format, &text_brush);

            // Label
            RectF label_rc(item_x - 45.0f, item_y - 3.0f, 90.0f, 16.0f);
            g.DrawString(sl->label, -1, &font_label, label_rc, &str_format, &text_brush);

            // Sublabel
            SolidBrush sub_brush(is_hov ? Color(245, 200, 255, 255) : Color(180, 148, 163, 184));
            RectF sub_rc(item_x - 45.0f, item_y + 11.0f, 90.0f, 14.0f);
            g.DrawString(sl->sublabel, -1, &font_sub, sub_rc, &str_format, &sub_brush);
        }

        // 3. Draw Center Hub
        float hub_r = (cur_r_in - 5.0f);
        if (hub_r > 5.0f) {
            GraphicsPath hub_path;
            hub_path.AddEllipse(cx - hub_r, cy - hub_r, hub_r * 2.0f, hub_r * 2.0f);

            // Obsidian core fill
            SolidBrush hub_brush(Color(245, 14, 17, 26));
            g.FillPath(&hub_brush, &hub_path);

            // Glowing cyan/violet boundary ring
            Pen hub_pen(menu->hovered_slice == -1 ? Color(255, 0, 245, 212) : Color(180, 138, 43, 226), 2.0f);
            g.DrawPath(&hub_pen, &hub_path);

            // Center Text
            Font font_hub_title(&font_family, 13.0f * spring_scale, FontStyleBold, UnitPixel);
            Font font_hub_info(&font_family, 9.5f * spring_scale, FontStyleRegular, UnitPixel);
            SolidBrush hub_title_brush(Color(255, 0, 245, 212));
            SolidBrush hub_sub_brush(Color(220, 200, 210, 225));

            if (menu->action_executed) {
                // Done checkmark toast
                Font font_done(&font_family, 26.0f * spring_scale, FontStyleBold, UnitPixel);
                SolidBrush done_brush(Color(255, 0, 245, 212));
                RectF done_rc(cx - 50.0f, cy - 28.0f, 100.0f, 32.0f);
                g.DrawString(L"✓", -1, &font_done, done_rc, &str_format, &done_brush);

                RectF msg_rc(cx - 55.0f, cy + 6.0f, 110.0f, 20.0f);
                g.DrawString(menu->status_message, -1, &font_hub_info, msg_rc, &str_format, &hub_sub_brush);
            } else {
                RectF title_rc(cx - 50.0f, cy - 25.0f, 100.0f, 18.0f);
                g.DrawString(L"MAGIFY", -1, &font_hub_title, title_rc, &str_format, &hub_title_brush);

                // Truncate file name for display
                wchar_t disp_name[32] = {0};
                wcsncpy_s(disp_name, 32, menu->target.file_name_w, 18);
                if (wcslen(menu->target.file_name_w) > 18) {
                    wcscat_s(disp_name, 32, L"...");
                }
                RectF name_rc(cx - 55.0f, cy - 6.0f, 110.0f, 16.0f);
                g.DrawString(disp_name, -1, &font_hub_info, name_rc, &str_format, &hub_sub_brush);

                // File size or type
                wchar_t size_str[32] = {0};
                if (menu->target.file_size_bytes < 1024 * 1024) {
                    swprintf_s(size_str, 32, L"%.1f KB", (float)menu->target.file_size_bytes / 1024.0f);
                } else {
                    swprintf_s(size_str, 32, L"%.1f MB", (float)menu->target.file_size_bytes / (1024.0f * 1024.0f));
                }
                RectF size_rc(cx - 50.0f, cy + 11.0f, 100.0f, 14.0f);
                g.DrawString(size_str, -1, &font_hub_info, size_rc, &str_format, &hub_sub_brush);
            }
        }
    }

    // UpdateLayeredWindow
    POINT pt_src = {0, 0};
    SIZE wnd_size = {width, height};
    POINT pt_wnd;
    RECT wnd_rect;
    GetWindowRect(menu->hwnd, &wnd_rect);
    pt_wnd.x = wnd_rect.left;
    pt_wnd.y = wnd_rect.top;

    BLENDFUNCTION blend = {0};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(menu->hwnd, screen_dc, &pt_wnd, &wnd_size, mem_dc, &pt_src, 0, &blend, ULW_ALPHA);

    SelectObject(mem_dc, h_old_bmp);
    DeleteObject(h_dib);
    DeleteDC(mem_dc);
    ReleaseDC(NULL, screen_dc);
}

static void execute_selected_slice(mg_radial_menu_t *menu, int slice_idx) {
    if (slice_idx < 0 || slice_idx >= menu->slice_count) return;

    mg_slice_t *sl = &menu->slices[slice_idx];
    menu->action_executed = true;
    swprintf_s(menu->status_message, 128, L"%s Applied!", sl->label);

    switch (sl->action) {
        case MG_ACTION_IMG_ROTATE:
            mg_image_rotate(menu->target.file_path_w, 90);
            break;
        case MG_ACTION_IMG_CROP:
            // Crop center 80%
            if (menu->target.width == 0) {
                mg_bitmap_t bmp;
                if (mg_bitmap_load(menu->target.file_path_u8, &bmp) == MG_SUCCESS) {
                    menu->target.width = bmp.width;
                    menu->target.height = bmp.height;
                    mg_bitmap_free(&bmp);
                }
            }
            if (menu->target.width > 0 && menu->target.height > 0) {
                int cw = (int)(menu->target.width * 0.8f);
                int ch = (int)(menu->target.height * 0.8f);
                int cx = (menu->target.width - cw) / 2;
                int cy = (menu->target.height - ch) / 2;
                mg_image_crop(menu->target.file_path_w, cx, cy, cw, ch);
            }
            break;
        case MG_ACTION_IMG_RESIZE:
            // Scale to 50%
            if (menu->target.width == 0) {
                mg_bitmap_t bmp;
                if (mg_bitmap_load(menu->target.file_path_u8, &bmp) == MG_SUCCESS) {
                    menu->target.width = bmp.width;
                    menu->target.height = bmp.height;
                    mg_bitmap_free(&bmp);
                }
            }
            if (menu->target.width > 10 && menu->target.height > 10) {
                mg_image_resize(menu->target.file_path_w, menu->target.width / 2, menu->target.height / 2);
            }
            break;
        case MG_ACTION_IMG_COMPRESS:
            mg_image_compress(menu->target.file_path_w, 65);
            break;
        case MG_ACTION_IMG_CONVERT:
            mg_pdf_create_from_image(menu->target.file_path_w, NULL, 0);
            swprintf_s(menu->status_message, 128, L"PDF Created!");
            break;
        case MG_ACTION_IMG_GRAYSCALE:
            mg_image_grayscale(menu->target.file_path_w);
            break;
        case MG_ACTION_PDF_ROTATE:
            mg_pdf_rotate_pages(menu->target.file_path_w, 90);
            break;
        case MG_ACTION_PDF_COMPRESS:
            mg_pdf_compress(menu->target.file_path_w);
            break;
        case MG_ACTION_GEN_DUPLICATE: {
            wchar_t dup_path[MAX_PATH];
            swprintf_s(dup_path, MAX_PATH, L"%s_copy", menu->target.file_path_w);
            CopyFileW(menu->target.file_path_w, dup_path, FALSE);
            swprintf_s(menu->status_message, 128, L"Cloned File!");
            break;
        }
        case MG_ACTION_GEN_RENAME: {
            SYSTEMTIME st;
            GetLocalTime(&st);
            wchar_t renamed_path[MAX_PATH];
            wcsncpy_s(renamed_path, MAX_PATH, menu->target.file_path_w, _TRUNCATE);
            PathRemoveExtensionW(renamed_path);
            wchar_t stamp[64];
            swprintf_s(stamp, 64, L"_%04d%02d%02d_%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
            wcscat_s(renamed_path, MAX_PATH, stamp);
            const wchar_t *ext = PathFindExtensionW(menu->target.file_path_w);
            if (ext) wcscat_s(renamed_path, MAX_PATH, ext);
            MoveFileW(menu->target.file_path_w, renamed_path);
            swprintf_s(menu->status_message, 128, L"Renamed!");
            break;
        }
        default:
            break;
    }

    render_radial_frame(menu);
    // Auto-close smoothly after 450ms
    SetTimer(menu->hwnd, 2, 450, NULL);
}

static LRESULT CALLBACK RadialWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    mg_radial_menu_t *menu = (mg_radial_menu_t*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
        case WM_CREATE: {
            CREATESTRUCTW *cs = (CREATESTRUCTW*)lParam;
            menu = (mg_radial_menu_t*)cs->lpCreateParams;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)menu);
            menu->hwnd = hwnd;
            menu->spawn_anim = 0.0f;
            menu->hovered_slice = -2; // None
            menu->action_executed = false;
            // 60 FPS animation timer
            SetTimer(hwnd, 1, 16, NULL);
            return 0;
        }
        case WM_TIMER: {
            if (wParam == 1) { // Spawn animation tick
                menu->spawn_anim += 0.09f;
                if (menu->spawn_anim >= 1.0f) {
                    menu->spawn_anim = 1.0f;
                    KillTimer(hwnd, 1);
                }
                render_radial_frame(menu);
            } else if (wParam == 2) { // Auto-close timer after action
                KillTimer(hwnd, 2);
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (menu->action_executed) return 0;

            int mouse_x = (short)LOWORD(lParam);
            int mouse_y = (short)HIWORD(lParam);

            float dx = (float)(mouse_x - menu->center_x);
            float dy = (float)(mouse_y - menu->center_y);
            float dist = sqrtf(dx * dx + dy * dy);

            int new_hovered = -2;
            if (dist < menu->inner_radius) {
                new_hovered = -1; // Center hub
            } else if (dist <= menu->outer_radius + 30.0f) {
                // Determine angle in degrees [-180, 180]
                float angle = atan2f(dy, dx) * (180.0f / PI);
                // Check which slice matches
                for (int i = 0; i < menu->slice_count; i++) {
                    float s = menu->slices[i].angle_start;
                    float e = menu->slices[i].angle_end;
                    // Handle wrap-around across -180/180
                    float a = angle;
                    while (a < s) a += 360.0f;
                    while (a >= s + 360.0f) a -= 360.0f;
                    if (a >= s && a < e) {
                        new_hovered = i;
                        break;
                    }
                }
            }

            if (new_hovered != menu->hovered_slice) {
                menu->hovered_slice = new_hovered;
                render_radial_frame(menu);
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (menu->action_executed) return 0;
            if (menu->hovered_slice >= 0) {
                execute_selected_slice(menu, menu->hovered_slice);
            } else if (menu->hovered_slice == -1) {
                // Clicked center hub -> dismiss
                DestroyWindow(hwnd);
            } else {
                // Clicked outside -> dismiss
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_RBUTTONDOWN:
        case WM_KILLFOCUS: {
            // Dismiss menu on right click or losing focus
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            return 0;
        }
        case WM_DESTROY: {
            PostQuitMessage(0);
            return 0;
        }
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

extern "C" mg_result_t mg_ui_show_radial_menu(const wchar_t *file_path) {
    if (!file_path) return MG_ERR_INVALID_ARGS;

    // Start GDI+
    GdiplusStartupInput gdiplus_input;
    if (g_gdiplus_token == 0) {
        GdiplusStartup(&g_gdiplus_token, &gdiplus_input, NULL);
    }

    mg_radial_menu_t menu = {0};
    g_current_menu = &menu;

    // Inspect file
    mg_result_t res = mg_file_inspect(file_path, &menu.target);
    if (res != MG_SUCCESS) {
        return res;
    }

    init_slices_for_target(&menu);

    HINSTANCE h_inst = GetModuleHandleW(NULL);
    const wchar_t *class_name = L"MagifyRadialOverlayClass";

    WNDCLASSEXW wcex = {0};
    wcex.cbSize = sizeof(WNDCLASSEXW);
    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = RadialWndProc;
    wcex.hInstance = h_inst;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.lpszClassName = class_name;
    RegisterClassExW(&wcex);

    // Compute dimensions
    const int wnd_dim = 480;
    menu.center_x = wnd_dim / 2;
    menu.center_y = wnd_dim / 2;
    menu.inner_radius = 62.0f;
    menu.outer_radius = 175.0f;

    // Position centered at mouse cursor
    POINT pt;
    GetCursorPos(&pt);
    int pos_x = pt.x - menu.center_x;
    int pos_y = pt.y - menu.center_y;

    // Clamp inside current monitor
    HMONITOR h_mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {0};
    mi.cbSize = sizeof(MONITORINFO);
    if (GetMonitorInfoW(h_mon, &mi)) {
        if (pos_x < mi.rcWork.left) pos_x = mi.rcWork.left;
        if (pos_y < mi.rcWork.top)  pos_y = mi.rcWork.top;
        if (pos_x + wnd_dim > mi.rcWork.right)  pos_x = mi.rcWork.right - wnd_dim;
        if (pos_y + wnd_dim > mi.rcWork.bottom) pos_y = mi.rcWork.bottom - wnd_dim;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        class_name,
        MG_APP_NAME,
        WS_POPUP,
        pos_x, pos_y, wnd_dim, wnd_dim,
        NULL, NULL, h_inst, &menu
    );

    if (!hwnd) {
        return MG_ERR_GENERIC;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    // Initial render frame
    render_radial_frame(&menu);

    // Win32 Message Pump
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnregisterClassW(class_name, h_inst);
    return MG_SUCCESS;
}
