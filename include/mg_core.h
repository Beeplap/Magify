#ifndef MG_CORE_H
#define MG_CORE_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Version definitions
#define MG_VERSION_MAJOR 1
#define MG_VERSION_MINOR 0
#define MG_VERSION_PATCH 0
#define MG_APP_NAME L"Magify"

// Return codes
typedef enum mg_result {
    MG_SUCCESS = 0,
    MG_ERR_INVALID_ARGS = -1,
    MG_ERR_OUT_OF_MEMORY = -2,
    MG_ERR_FILE_NOT_FOUND = -3,
    MG_ERR_FILE_ACCESS_DENIED = -4,
    MG_ERR_UNSUPPORTED_FORMAT = -5,
    MG_ERR_D2D_INIT_FAILED = -6,
    MG_ERR_REGISTRY_FAILED = -7,
    MG_ERR_OPERATION_ABORTED = -8,
    MG_ERR_GENERIC = -99
} mg_result_t;

// Detected File Category
typedef enum mg_file_type {
    MG_FILE_TYPE_UNKNOWN = 0,
    MG_FILE_TYPE_IMAGE,
    MG_FILE_TYPE_PDF,
    MG_FILE_TYPE_GENERIC
} mg_file_type_t;

// Sub-format specific identification
typedef enum mg_image_format {
    MG_IMG_FMT_UNKNOWN = 0,
    MG_IMG_FMT_PNG,
    MG_IMG_FMT_JPEG,
    MG_IMG_FMT_WEBP,
    MG_IMG_FMT_BMP,
    MG_IMG_FMT_GIF
} mg_image_format_t;

// Target file metadata
typedef struct mg_target_file {
    wchar_t  file_path_w[MAX_PATH];
    char     file_path_u8[MAX_PATH * 3];
    wchar_t  file_name_w[MAX_PATH];
    uint64_t file_size_bytes;
    mg_file_type_t file_type;
    mg_image_format_t img_format;
    uint32_t width;      // for images/PDF preview
    uint32_t height;
    uint32_t page_count; // for PDFs
} mg_target_file_t;

// Application CLI Action Mode
typedef enum mg_app_mode {
    MG_MODE_UNKNOWN = 0,
    MG_MODE_INSTALL,
    MG_MODE_UNINSTALL,
    MG_MODE_RADIAL_FILE,
    MG_MODE_STANDALONE,
    MG_MODE_HELP
} mg_app_mode_t;

#ifdef __cplusplus
}
#endif

#endif // MG_CORE_H
