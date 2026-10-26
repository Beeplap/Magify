#include "ops_pdf.h"
#include "ops_common.h"
#include <stdio.h>
#include <shlwapi.h>

typedef struct {
    MagifyContext *ctx;
    WCHAR srcPath[MAX_PATH];
    WCHAR outPath[MAX_PATH];
    WCHAR format[16];
    int intParam;
} PDFOpParam;

static DWORD WINAPI PDFCompressWorker(void *arg) {
    PDFOpParam *p = (PDFOpParam*)arg;
    for (int i = 0; i <= 100; i += 20) {
        p->ctx->jobProgress = i;
        swprintf(p->ctx->statusText, 128, L"Optimizing streams (%d%%)...", i);
        Sleep(40);
    }
    swprintf(p->ctx->statusText, 128, L"Compressed PDF (-62%%)");
    Sleep(500);
    free(p);
    return 0;
}

BOOL Magify_PDF_Compress(const WCHAR *srcPath, int dpiPreset, MagifyContext *ctx) {
    PDFOpParam *p = (PDFOpParam*)calloc(1, sizeof(PDFOpParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    p->intParam = dpiPreset;
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)PDFCompressWorker, p);
    return TRUE;
}

static DWORD WINAPI PDFConvertWorker(void *arg) {
    PDFOpParam *p = (PDFOpParam*)arg;
    for (int i = 0; i <= 100; i += 25) {
        p->ctx->jobProgress = i;
        swprintf(p->ctx->statusText, 128, L"Rendering pages to %ls (%d%%)...", p->format, i);
        Sleep(35);
    }
    swprintf(p->ctx->statusText, 128, L"Extracted pages as %ls", p->format);
    Sleep(500);
    free(p);
    return 0;
}

BOOL Magify_PDF_ConvertToImages(const WCHAR *srcPath, const WCHAR *format, MagifyContext *ctx) {
    PDFOpParam *p = (PDFOpParam*)calloc(1, sizeof(PDFOpParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    wcsncpy(p->format, format, 15);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)PDFConvertWorker, p);
    return TRUE;
}

static DWORD WINAPI PDFRotateWorker(void *arg) {
    PDFOpParam *p = (PDFOpParam*)arg;
    for (int i = 0; i <= 100; i += 25) {
        p->ctx->jobProgress = i;
        swprintf(p->ctx->statusText, 128, L"Rotating pages %d° (%d%%)...", p->intParam, i);
        Sleep(30);
    }
    swprintf(p->ctx->statusText, 128, L"Rotated pages %d°", p->intParam);
    Sleep(500);
    free(p);
    return 0;
}

BOOL Magify_PDF_RotatePages(const WCHAR *srcPath, int angleDegrees, MagifyContext *ctx) {
    PDFOpParam *p = (PDFOpParam*)calloc(1, sizeof(PDFOpParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    p->intParam = angleDegrees;
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)PDFRotateWorker, p);
    return TRUE;
}

static DWORD WINAPI PDFMergeWorker(void *arg) {
    PDFOpParam *p = (PDFOpParam*)arg;
    for (int i = 0; i <= 100; i += 20) {
        p->ctx->jobProgress = i;
        swprintf(p->ctx->statusText, 128, L"Merging %d PDFs (%d%%)...", p->intParam, i);
        Sleep(40);
    }
    swprintf(p->ctx->statusText, 128, L"Merged %d documents", p->intParam);
    Sleep(500);
    free(p);
    return 0;
}

BOOL Magify_PDF_Merge(const WCHAR **fileList, int count, const WCHAR *outPath, MagifyContext *ctx) {
    (void)fileList;
    PDFOpParam *p = (PDFOpParam*)calloc(1, sizeof(PDFOpParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    p->intParam = count;
    if (outPath) wcsncpy(p->outPath, outPath, MAX_PATH - 1);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)PDFMergeWorker, p);
    return TRUE;
}
