#include "mg_image.h"
#include "mg_shell.h"
#include <shlwapi.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../third_party/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../third_party/stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "../../third_party/stb_image_resize.h"

mg_result_t mg_bitmap_load(const char *utf8_path, mg_bitmap_t *out_bmp) {
    if (!utf8_path || !out_bmp) return MG_ERR_INVALID_ARGS;
    memset(out_bmp, 0, sizeof(mg_bitmap_t));

    // Force 4 channels (RGBA) for consistent manipulation and Direct2D compatibility
    out_bmp->data = stbi_load(utf8_path, &out_bmp->width, &out_bmp->height, &out_bmp->channels, 4);
    if (!out_bmp->data) {
        return MG_ERR_UNSUPPORTED_FORMAT;
    }
    out_bmp->channels = 4;
    return MG_SUCCESS;
}

void mg_bitmap_free(mg_bitmap_t *bmp) {
    if (bmp && bmp->data) {
        stbi_image_free(bmp->data);
        bmp->data = NULL;
    }
}

mg_result_t mg_bitmap_save(const char *utf8_path, const mg_bitmap_t *bmp, mg_image_format_t fmt, int quality_percent) {
    if (!utf8_path || !bmp || !bmp->data) return MG_ERR_INVALID_ARGS;

    int success = 0;
    if (fmt == MG_IMG_FMT_JPEG) {
        int q = (quality_percent <= 0) ? 85 : (quality_percent > 100 ? 100 : quality_percent);
        success = stbi_write_jpg(utf8_path, bmp->width, bmp->height, bmp->channels, bmp->data, q);
    } else if (fmt == MG_IMG_FMT_BMP) {
        success = stbi_write_bmp(utf8_path, bmp->width, bmp->height, bmp->channels, bmp->data);
    } else {
        // Default to PNG
        success = stbi_write_png(utf8_path, bmp->width, bmp->height, bmp->channels, bmp->data, bmp->width * bmp->channels);
    }

    return success ? MG_SUCCESS : MG_ERR_GENERIC;
}

static mg_result_t atomic_replace_image(const wchar_t *orig_path_w, const mg_bitmap_t *bmp, mg_image_format_t fmt, int quality) {
    wchar_t tmp_path_w[MAX_PATH];
    swprintf_s(tmp_path_w, MAX_PATH, L"%s.mg_tmp", orig_path_w);

    char tmp_path_u8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, tmp_path_w, -1, tmp_path_u8, sizeof(tmp_path_u8), NULL, NULL);

    mg_result_t res = mg_bitmap_save(tmp_path_u8, bmp, fmt, quality);
    if (res != MG_SUCCESS) {
        DeleteFileW(tmp_path_w);
        return res;
    }

    // Atomic move/replace
    if (!MoveFileExW(tmp_path_w, orig_path_w, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp_path_w);
        return MG_ERR_FILE_ACCESS_DENIED;
    }

    return MG_SUCCESS;
}

mg_result_t mg_image_rotate(const wchar_t *file_path, int angle_deg) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    int w = src.width;
    int h = src.height;
    int ch = src.channels;

    int new_w = w;
    int new_h = h;
    angle_deg = (angle_deg % 360 + 360) % 360;

    if (angle_deg == 90 || angle_deg == 270) {
        new_w = h;
        new_h = w;
    }

    mg_bitmap_t dst;
    dst.width = new_w;
    dst.height = new_h;
    dst.channels = ch;
    dst.data = (uint8_t*)malloc((size_t)new_w * new_h * ch);
    if (!dst.data) {
        mg_bitmap_free(&src);
        return MG_ERR_OUT_OF_MEMORY;
    }

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int src_idx = (y * w + x) * ch;
            int dst_x = x;
            int dst_y = y;

            if (angle_deg == 90) {
                dst_x = h - 1 - y;
                dst_y = x;
            } else if (angle_deg == 180) {
                dst_x = w - 1 - x;
                dst_y = h - 1 - y;
            } else if (angle_deg == 270) {
                dst_x = y;
                dst_y = w - 1 - x;
            }

            int dst_idx = (dst_y * new_w + dst_x) * ch;
            for (int c = 0; c < ch; c++) {
                dst.data[dst_idx + c] = src.data[src_idx + c];
            }
        }
    }

    res = atomic_replace_image(file_path, &dst, target.img_format, 90);
    free(dst.data);
    mg_bitmap_free(&src);
    return res;
}

mg_result_t mg_image_flip(const wchar_t *file_path, bool horizontal, bool vertical) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    int w = src.width;
    int h = src.height;
    int ch = src.channels;

    mg_bitmap_t dst;
    dst.width = w;
    dst.height = h;
    dst.channels = ch;
    dst.data = (uint8_t*)malloc((size_t)w * h * ch);
    if (!dst.data) {
        mg_bitmap_free(&src);
        return MG_ERR_OUT_OF_MEMORY;
    }

    for (int y = 0; y < h; y++) {
        int dst_y = vertical ? (h - 1 - y) : y;
        for (int x = 0; x < w; x++) {
            int dst_x = horizontal ? (w - 1 - x) : x;
            int src_idx = (y * w + x) * ch;
            int dst_idx = (dst_y * w + dst_x) * ch;
            for (int c = 0; c < ch; c++) {
                dst.data[dst_idx + c] = src.data[src_idx + c];
            }
        }
    }

    res = atomic_replace_image(file_path, &dst, target.img_format, 90);
    free(dst.data);
    mg_bitmap_free(&src);
    return res;
}

mg_result_t mg_image_crop(const wchar_t *file_path, int crop_x, int crop_y, int crop_w, int crop_h) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    if (crop_x < 0) crop_x = 0;
    if (crop_y < 0) crop_y = 0;
    if (crop_x + crop_w > src.width)  crop_w = src.width - crop_x;
    if (crop_y + crop_h > src.height) crop_h = src.height - crop_y;

    if (crop_w <= 0 || crop_h <= 0) {
        mg_bitmap_free(&src);
        return MG_ERR_INVALID_ARGS;
    }

    int ch = src.channels;
    mg_bitmap_t dst;
    dst.width = crop_w;
    dst.height = crop_h;
    dst.channels = ch;
    dst.data = (uint8_t*)malloc((size_t)crop_w * crop_h * ch);
    if (!dst.data) {
        mg_bitmap_free(&src);
        return MG_ERR_OUT_OF_MEMORY;
    }

    for (int y = 0; y < crop_h; y++) {
        int src_offset = ((crop_y + y) * src.width + crop_x) * ch;
        int dst_offset = (y * crop_w) * ch;
        memcpy(dst.data + dst_offset, src.data + src_offset, (size_t)crop_w * ch);
    }

    res = atomic_replace_image(file_path, &dst, target.img_format, 90);
    free(dst.data);
    mg_bitmap_free(&src);
    return res;
}

mg_result_t mg_image_resize(const wchar_t *file_path, int target_w, int target_h) {
    if (target_w <= 0 || target_h <= 0) return MG_ERR_INVALID_ARGS;

    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    int ch = src.channels;
    mg_bitmap_t dst;
    dst.width = target_w;
    dst.height = target_h;
    dst.channels = ch;
    dst.data = (uint8_t*)malloc((size_t)target_w * target_h * ch);
    if (!dst.data) {
        mg_bitmap_free(&src);
        return MG_ERR_OUT_OF_MEMORY;
    }

    stbir_resize_uint8(src.data, src.width, src.height, 0,
                       dst.data, target_w, target_h, 0, ch);

    res = atomic_replace_image(file_path, &dst, target.img_format, 90);
    free(dst.data);
    mg_bitmap_free(&src);
    return res;
}

mg_result_t mg_image_compress(const wchar_t *file_path, int quality_percent) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    // For JPEG: save with requested compression quality (default 65)
    // For PNG: re-save optimized
    res = atomic_replace_image(file_path, &src, target.img_format, quality_percent <= 0 ? 65 : quality_percent);
    mg_bitmap_free(&src);
    return res;
}

mg_result_t mg_image_convert(const wchar_t *file_path, mg_image_format_t target_fmt, wchar_t *out_new_path, size_t out_path_len) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    wchar_t new_path_w[MAX_PATH];
    wcsncpy_s(new_path_w, MAX_PATH, file_path, _TRUNCATE);
    PathRemoveExtensionW(new_path_w);

    const wchar_t *new_ext = L".png";
    if (target_fmt == MG_IMG_FMT_JPEG) new_ext = L".jpg";
    else if (target_fmt == MG_IMG_FMT_BMP) new_ext = L".bmp";
    else if (target_fmt == MG_IMG_FMT_PNG) new_ext = L".png";

    PathAddExtensionW(new_path_w, new_ext);

    char new_path_u8[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, new_path_w, -1, new_path_u8, sizeof(new_path_u8), NULL, NULL);

    res = mg_bitmap_save(new_path_u8, &src, target_fmt, 90);
    mg_bitmap_free(&src);

    if (res == MG_SUCCESS && out_new_path && out_path_len > 0) {
        wcsncpy_s(out_new_path, out_path_len, new_path_w, _TRUNCATE);
    }
    return res;
}

mg_result_t mg_image_grayscale(const wchar_t *file_path) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(file_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t src;
    res = mg_bitmap_load(target.file_path_u8, &src);
    if (res != MG_SUCCESS) return res;

    int total_pixels = src.width * src.height;
    int ch = src.channels;
    for (int i = 0; i < total_pixels; i++) {
        uint8_t r = src.data[i * ch + 0];
        uint8_t g = src.data[i * ch + 1];
        uint8_t b = src.data[i * ch + 2];
        uint8_t gray = (uint8_t)(0.299f * r + 0.587f * g + 0.114f * b);
        src.data[i * ch + 0] = gray;
        src.data[i * ch + 1] = gray;
        src.data[i * ch + 2] = gray;
    }

    res = atomic_replace_image(file_path, &src, target.img_format, 90);
    mg_bitmap_free(&src);
    return res;
}
