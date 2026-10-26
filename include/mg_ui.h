#ifndef MG_UI_H
#define MG_UI_H

#include "mg_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Slice Action Identifiers
typedef enum mg_slice_action {
    MG_ACTION_NONE = 0,
    // Image Actions
    MG_ACTION_IMG_ROTATE,
    MG_ACTION_IMG_CROP,
    MG_ACTION_IMG_RESIZE,
    MG_ACTION_IMG_COMPRESS,
    MG_ACTION_IMG_CONVERT,
    MG_ACTION_IMG_GRAYSCALE,
    // PDF Actions
    MG_ACTION_PDF_ROTATE,
    MG_ACTION_PDF_COMPRESS,
    MG_ACTION_PDF_TO_IMG,
    MG_ACTION_PDF_SPLIT,
    MG_ACTION_PDF_MERGE,
    MG_ACTION_PDF_CONVERT,
    // Generic File Actions
    MG_ACTION_GEN_RENAME,
    MG_ACTION_GEN_ZIP,
    MG_ACTION_GEN_HASH,
    MG_ACTION_GEN_DUPLICATE
} mg_slice_action_t;

// Single Slice Metadata
typedef struct mg_slice {
    mg_slice_action_t action;
    const wchar_t    *label;
    const wchar_t    *sublabel;
    const wchar_t    *icon_glyph; // Segoe MDL2 / Unicode symbol
    float             angle_start;
    float             angle_end;
    bool              is_hovered;
    float             hover_anim; // 0.0 to 1.0
} mg_slice_t;

#define MG_MAX_SLICES 8

// Radial Menu State
typedef struct mg_radial_menu {
    HWND              hwnd;
    mg_target_file_t  target;
    mg_slice_t        slices[MG_MAX_SLICES];
    int               slice_count;
    int               hovered_slice;
    int               selected_slice;
    float             spawn_anim;    // 0.0 to 1.0 spring
    bool              is_closing;
    float             close_anim;
    int               center_x;
    int               center_y;
    float             inner_radius;
    float             outer_radius;
    float             dpi_scale;
    bool              action_executed;
    wchar_t           status_message[128];
} mg_radial_menu_t;

// Launch and display the radial menu over the target file
mg_result_t mg_ui_show_radial_menu(const wchar_t *file_path);

#ifdef __cplusplus
}
#endif

#endif // MG_UI_H
