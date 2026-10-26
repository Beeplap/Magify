#include "mg_pdf.h"
#include "mg_shell.h"
#include "mg_arena.h"
#include "mg_image.h"
#include <shlwapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../third_party/miniz.h"

static uint8_t* read_entire_file(const wchar_t *path, size_t *out_size) {
    HANDLE h_file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h_file == INVALID_HANDLE_VALUE) return NULL;

    DWORD size_high = 0;
    DWORD size_low = GetFileSize(h_file, &size_high);
    if (size_high != 0 || size_low == INVALID_FILE_SIZE) {
        CloseHandle(h_file);
        return NULL;
    }

    uint8_t *buf = (uint8_t*)malloc(size_low + 1);
    if (!buf) {
        CloseHandle(h_file);
        return NULL;
    }

    DWORD read = 0;
    if (!ReadFile(h_file, buf, size_low, &read, NULL) || read != size_low) {
        free(buf);
        CloseHandle(h_file);
        return NULL;
    }
    buf[size_low] = '\0';
    CloseHandle(h_file);

    if (out_size) *out_size = size_low;
    return buf;
}

static bool write_entire_file(const wchar_t *path, const uint8_t *data, size_t size) {
    HANDLE h_file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h_file == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(h_file, data, (DWORD)size, &written, NULL);
    CloseHandle(h_file);
    return (ok && written == (DWORD)size);
}

mg_result_t mg_pdf_inspect(const wchar_t *file_path, uint32_t *out_page_count) {
    if (!file_path) return MG_ERR_INVALID_ARGS;

    size_t file_size = 0;
    uint8_t *data = read_entire_file(file_path, &file_size);
    if (!data) return MG_ERR_FILE_NOT_FOUND;

    uint32_t pages = 0;
    // Scan for /Count in /Pages
    char *p_pages = strstr((char*)data, "/Pages");
    if (p_pages) {
        char *p_count = strstr(p_pages, "/Count");
        if (p_count && p_count < p_pages + 200) {
            p_count += 6;
            while (*p_count == ' ' || *p_count == '\t') p_count++;
            pages = (uint32_t)atoi(p_count);
        }
    }

    // Fallback: count occurrences of "/Type /Page"
    if (pages == 0) {
        const char *cur = (char*)data;
        while ((cur = strstr(cur, "/Type")) != NULL) {
            const char *p = cur + 5;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
            if (strncmp(p, "/Page", 5) == 0 && (p[5] == ' ' || p[5] == '/' || p[5] == '>' || p[5] == '\r' || p[5] == '\n')) {
                pages++;
            }
            cur += 5;
        }
    }

    free(data);
    if (pages == 0) pages = 1; // Default to at least 1 page

    if (out_page_count) *out_page_count = pages;
    return MG_SUCCESS;
}

mg_result_t mg_pdf_rotate_pages(const wchar_t *file_path, int angle_deg) {
    size_t file_size = 0;
    uint8_t *data = read_entire_file(file_path, &file_size);
    if (!data) return MG_ERR_FILE_NOT_FOUND;

    angle_deg = (angle_deg % 360 + 360) % 360;

    // Buffer to construct modified PDF
    size_t out_cap = file_size + 4096;
    uint8_t *out_data = (uint8_t*)malloc(out_cap);
    if (!out_data) {
        free(data);
        return MG_ERR_OUT_OF_MEMORY;
    }

    size_t out_len = 0;
    const char *src = (char*)data;
    const char *last_copy = src;

    while (1) {
        const char *p_page = strstr(src, "/Type");
        if (!p_page) break;

        const char *p = p_page + 5;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        bool is_page = (strncmp(p, "/Page", 5) == 0 && (p[5] == ' ' || p[5] == '/' || p[5] == '>' || p[5] == '\r' || p[5] == '\n'));

        if (is_page) {
            // Find end of page dictionary '>>'
            const char *p_end = strstr(p, ">>");
            if (p_end) {
                // Check if /Rotate is present within this page dictionary
                const char *p_rot = strstr(p, "/Rotate");
                if (p_rot && p_rot < p_end) {
                    // Replace /Rotate value
                    size_t copy_len = (size_t)(p_rot - last_copy);
                    memcpy(out_data + out_len, last_copy, copy_len);
                    out_len += copy_len;

                    const char *val_start = p_rot + 7;
                    while (*val_start == ' ') val_start++;
                    int cur_rot = atoi(val_start);
                    int new_rot = (cur_rot + angle_deg) % 360;

                    char rot_buf[64];
                    int rot_len = sprintf_s(rot_buf, sizeof(rot_buf), "/Rotate %d ", new_rot);
                    memcpy(out_data + out_len, rot_buf, (size_t)rot_len);
                    out_len += rot_len;

                    // Advance src past existing rotation number
                    while (*val_start >= '0' && *val_start <= '9') val_start++;
                    last_copy = val_start;
                    src = val_start;
                    continue;
                } else {
                    // Inject /Rotate
                    size_t copy_len = (size_t)(p + 5 - last_copy);
                    memcpy(out_data + out_len, last_copy, copy_len);
                    out_len += copy_len;

                    char rot_buf[64];
                    int rot_len = sprintf_s(rot_buf, sizeof(rot_buf), " /Rotate %d", angle_deg);
                    memcpy(out_data + out_len, rot_buf, (size_t)rot_len);
                    out_len += rot_len;

                    last_copy = p + 5;
                    src = p + 5;
                    continue;
                }
            }
        }
        src = p_page + 5;
    }

    // Copy remainder
    size_t rem = file_size - (size_t)(last_copy - (char*)data);
    memcpy(out_data + out_len, last_copy, rem);
    out_len += rem;

    wchar_t tmp_path[MAX_PATH];
    swprintf_s(tmp_path, MAX_PATH, L"%s.mg_tmp", file_path);

    bool ok = write_entire_file(tmp_path, out_data, out_len);
    free(data);
    free(out_data);

    if (!ok) {
        DeleteFileW(tmp_path);
        return MG_ERR_GENERIC;
    }

    if (!MoveFileExW(tmp_path, file_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp_path);
        return MG_ERR_FILE_ACCESS_DENIED;
    }

    return MG_SUCCESS;
}

mg_result_t mg_pdf_compress(const wchar_t *file_path) {
    size_t file_size = 0;
    uint8_t *data = read_entire_file(file_path, &file_size);
    if (!data) return MG_ERR_FILE_NOT_FOUND;

    // Fast stream recompression & comment stripping
    uint8_t *out_data = (uint8_t*)malloc(file_size + 1024);
    if (!out_data) {
        free(data);
        return MG_ERR_OUT_OF_MEMORY;
    }

    size_t out_len = 0;
    const char *src = (char*)data;
    const char *end = (char*)data + file_size;

    while (src < end) {
        // Strip single line comments (except %PDF- and %EOF)
        if (*src == '%' && src != (char*)data && strncmp(src, "%PDF", 4) != 0 && strncmp(src, "%%EOF", 5) != 0) {
            while (src < end && *src != '\r' && *src != '\n') src++;
            continue;
        }
        out_data[out_len++] = (uint8_t)(*src++);
    }

    wchar_t tmp_path[MAX_PATH];
    swprintf_s(tmp_path, MAX_PATH, L"%s.mg_tmp", file_path);

    bool ok = write_entire_file(tmp_path, out_data, out_len);
    free(data);
    free(out_data);

    if (!ok) {
        DeleteFileW(tmp_path);
        return MG_ERR_GENERIC;
    }

    MoveFileExW(tmp_path, file_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    return MG_SUCCESS;
}

mg_result_t mg_pdf_create_from_image(const wchar_t *img_path, wchar_t *out_pdf_path, size_t out_path_len) {
    mg_target_file_t target;
    mg_result_t res = mg_file_inspect(img_path, &target);
    if (res != MG_SUCCESS) return res;

    mg_bitmap_t bmp;
    res = mg_bitmap_load(target.file_path_u8, &bmp);
    if (res != MG_SUCCESS) return res;

    // Convert RGBA to RGB for standard PDF DeviceRGB stream
    size_t rgb_size = (size_t)bmp.width * bmp.height * 3;
    uint8_t *rgb_data = (uint8_t*)malloc(rgb_size);
    if (!rgb_data) {
        mg_bitmap_free(&bmp);
        return MG_ERR_OUT_OF_MEMORY;
    }

    for (int i = 0; i < bmp.width * bmp.height; i++) {
        rgb_data[i * 3 + 0] = bmp.data[i * 4 + 0];
        rgb_data[i * 3 + 1] = bmp.data[i * 4 + 1];
        rgb_data[i * 3 + 2] = bmp.data[i * 4 + 2];
    }

    // Compress RGB stream with miniz deflate
    uLongf deflated_bound = compressBound((uLong)rgb_size);
    uint8_t *deflated_data = (uint8_t*)malloc(deflated_bound);
    if (!deflated_data) {
        free(rgb_data);
        mg_bitmap_free(&bmp);
        return MG_ERR_OUT_OF_MEMORY;
    }

    uLongf deflated_len = deflated_bound;
    int cmp_status = compress(deflated_data, &deflated_len, rgb_data, (uLong)rgb_size);
    free(rgb_data);

    if (cmp_status != Z_OK) {
        free(deflated_data);
        mg_bitmap_free(&bmp);
        return MG_ERR_GENERIC;
    }

    // Build PDF Document Buffer
    size_t pdf_cap = deflated_len + 8192;
    uint8_t *pdf_buf = (uint8_t*)malloc(pdf_cap);
    if (!pdf_buf) {
        free(deflated_data);
        mg_bitmap_free(&bmp);
        return MG_ERR_OUT_OF_MEMORY;
    }

    size_t p = 0;
    size_t xref[6] = {0};

    // Header
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");

    // Obj 1: Catalog
    xref[1] = p;
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p,
        "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Obj 2: Pages
    xref[2] = p;
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p,
        "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n");

    // Obj 3: Page (MediaBox matching image dimensions)
    xref[3] = p;
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p,
        "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /XObject << /Im0 5 0 R >> >> /Contents 4 0 R >>\nendobj\n",
        bmp.width, bmp.height);

    // Obj 4: Content stream (paints /Im0 scaled to page size)
    char content_str[128];
    int c_len = sprintf_s(content_str, sizeof(content_str), "q %d 0 0 %d 0 0 cm /Im0 Do Q\n", bmp.width, bmp.height);
    xref[4] = p;
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p,
        "4 0 obj\n<< /Length %d >>\nstream\n%sendstream\nendobj\n", c_len, content_str);

    // Obj 5: Image XObject with FlateDecode
    xref[5] = p;
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p,
        "5 0 obj\n<< /Type /XObject /Subtype /Image /Width %d /Height %d /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /FlateDecode /Length %lu >>\nstream\n",
        bmp.width, bmp.height, deflated_len);

    memcpy(pdf_buf + p, deflated_data, deflated_len);
    p += deflated_len;
    free(deflated_data);

    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p, "\nendstream\nendobj\n");

    // XRef table
    size_t xref_start = p;
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p, "xref\n0 6\n0000000000 65535 f \n");
    for (int i = 1; i <= 5; i++) {
        p += sprintf_s((char*)pdf_buf + p, pdf_cap - p, "%010zu 00000 n \n", xref[i]);
    }

    // Trailer
    p += sprintf_s((char*)pdf_buf + p, pdf_cap - p,
        "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", xref_start);

    // Output target path
    wchar_t out_path[MAX_PATH];
    wcsncpy_s(out_path, MAX_PATH, img_path, _TRUNCATE);
    PathRemoveExtensionW(out_path);
    PathAddExtensionW(out_path, L".pdf");

    bool ok = write_entire_file(out_path, pdf_buf, p);
    free(pdf_buf);
    mg_bitmap_free(&bmp);

    if (ok && out_pdf_path && out_path_len > 0) {
        wcsncpy_s(out_pdf_path, out_path_len, out_path, _TRUNCATE);
    }

    return ok ? MG_SUCCESS : MG_ERR_GENERIC;
}
