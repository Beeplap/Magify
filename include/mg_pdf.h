#ifndef MG_PDF_H
#define MG_PDF_H

#include "mg_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// PDF inspection
mg_result_t mg_pdf_inspect(const wchar_t *file_path, uint32_t *out_page_count);

// High-speed in-place page rotation (updates /Rotate tags)
mg_result_t mg_pdf_rotate_pages(const wchar_t *file_path, int angle_deg);

// Stream recompression and size optimization
mg_result_t mg_pdf_compress(const wchar_t *file_path);

// Split PDF into a page range [start_page, end_page] (1-indexed)
mg_result_t mg_pdf_split(const wchar_t *file_path, uint32_t start_page, uint32_t end_page, wchar_t *out_path, size_t out_path_len);

// Merge two PDF files into a single document
mg_result_t mg_pdf_merge(const wchar_t *file_path1, const wchar_t *file_path2, const wchar_t *out_path);

// Wrap an image into a standalone single-page PDF
mg_result_t mg_pdf_create_from_image(const wchar_t *img_path, wchar_t *out_pdf_path, size_t out_path_len);

#ifdef __cplusplus
}
#endif

#endif // MG_PDF_H
