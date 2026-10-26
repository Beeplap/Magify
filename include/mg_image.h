#ifndef MG_IMAGE_H
#define MG_IMAGE_H

#include "mg_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decoded raw bitmap in memory
typedef struct mg_bitmap {
    int width;
    int height;
    int channels; // 1 (gray), 3 (RGB), 4 (RGBA)
    uint8_t *data;
} mg_bitmap_t;

// Load image into raw pixel buffer (caller must free with mg_bitmap_free)
mg_result_t mg_bitmap_load(const char *utf8_path, mg_bitmap_t *out_bmp);
void        mg_bitmap_free(mg_bitmap_t *bmp);

// Save bitmap to file (PNG, JPEG, BMP)
mg_result_t mg_bitmap_save(const char *utf8_path, const mg_bitmap_t *bmp, mg_image_format_t fmt, int quality_percent);

// High-level in-place or new-file operations:
mg_result_t mg_image_rotate(const wchar_t *file_path, int angle_deg);
mg_result_t mg_image_flip(const wchar_t *file_path, bool horizontal, bool vertical);
mg_result_t mg_image_crop(const wchar_t *file_path, int crop_x, int crop_y, int crop_w, int crop_h);
mg_result_t mg_image_resize(const wchar_t *file_path, int target_w, int target_h);
mg_result_t mg_image_compress(const wchar_t *file_path, int quality_percent);
mg_result_t mg_image_convert(const wchar_t *file_path, mg_image_format_t target_fmt, wchar_t *out_new_path, size_t out_path_len);
mg_result_t mg_image_grayscale(const wchar_t *file_path);

#ifdef __cplusplus
}
#endif

#endif // MG_IMAGE_H
