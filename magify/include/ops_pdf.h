#ifndef MAGIFY_OPS_PDF_H
#define MAGIFY_OPS_PDF_H

#include "magify.h"

BOOL Magify_PDF_Compress(const WCHAR *srcPath, int dpiPreset, MagifyContext *ctx);
BOOL Magify_PDF_ConvertToImages(const WCHAR *srcPath, const WCHAR *format, MagifyContext *ctx);
BOOL Magify_PDF_RotatePages(const WCHAR *srcPath, int angleDegrees, MagifyContext *ctx);
BOOL Magify_PDF_Merge(const WCHAR **fileList, int count, const WCHAR *outPath, MagifyContext *ctx);

#endif // MAGIFY_OPS_PDF_H
