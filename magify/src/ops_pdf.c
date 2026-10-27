#include "ops_pdf.h"
#include "ops_image.h"
#include "ops_common.h"
#include "miniz_tinfl.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <shlwapi.h>
#include <commdlg.h>

// -------------------------------------------------------------
// Dynamic PDFium Function Pointers & Bridge
// -------------------------------------------------------------
typedef void   (*FPDF_InitLibrary_fn)(void);
typedef void   (*FPDF_DestroyLibrary_fn)(void);
typedef void*  (*FPDF_LoadMemDocument_fn)(const void* data_buf, int size, const char* password);
typedef void   (*FPDF_CloseDocument_fn)(void* document);
typedef int    (*FPDF_GetPageCount_fn)(void* document);
typedef void*  (*FPDF_LoadPage_fn)(void* document, int page_index);
typedef void   (*FPDF_ClosePage_fn)(void* page);
typedef double (*FPDF_GetPageWidth_fn)(void* page);
typedef double (*FPDF_GetPageHeight_fn)(void* page);
typedef void*  (*FPDFBitmap_CreateEx_fn)(int width, int height, int format, void* first_scan, int stride);
typedef void   (*FPDFBitmap_Destroy_fn)(void* bitmap);
typedef void   (*FPDFBitmap_FillRect_fn)(void* bitmap, int left, int top, int width, int height, unsigned long color);
typedef void   (*FPDF_RenderPageBitmap_fn)(void* bitmap, void* page, int start_x, int start_y, int size_x, int size_y, int rotate, int flags);
typedef void*  (*FPDF_CreateNewDocument_fn)(void);
typedef int    (*FPDF_ImportPages_fn)(void* dest_doc, void* src_doc, const char* page_range, int index);
typedef struct {
    int version;
    int (*WriteBlock)(void* pThis, const void* pData, unsigned long size);
    HANDLE hFile;
} MagifyPdfiumFileWrite;
typedef int    (*FPDF_SaveAsCopy_fn)(void* doc, void* pFileWrite, unsigned long flags);

static HMODULE g_hPdfium = NULL;
static BOOL    g_pdfiumAttempted = FALSE;
static BOOL    g_pdfiumAvailable = FALSE;

static FPDF_InitLibrary_fn       pFPDF_InitLibrary = NULL;
static FPDF_DestroyLibrary_fn    pFPDF_DestroyLibrary = NULL;
static FPDF_LoadMemDocument_fn   pFPDF_LoadMemDocument = NULL;
static FPDF_CloseDocument_fn     pFPDF_CloseDocument = NULL;
static FPDF_GetPageCount_fn      pFPDF_GetPageCount = NULL;
static FPDF_LoadPage_fn          pFPDF_LoadPage = NULL;
static FPDF_ClosePage_fn         pFPDF_ClosePage = NULL;
static FPDF_GetPageWidth_fn      pFPDF_GetPageWidth = NULL;
static FPDF_GetPageHeight_fn     pFPDF_GetPageHeight = NULL;
static FPDFBitmap_CreateEx_fn    pFPDFBitmap_CreateEx = NULL;
static FPDFBitmap_Destroy_fn     pFPDFBitmap_Destroy = NULL;
static FPDFBitmap_FillRect_fn    pFPDFBitmap_FillRect = NULL;
static FPDF_RenderPageBitmap_fn  pFPDF_RenderPageBitmap = NULL;
static FPDF_CreateNewDocument_fn pFPDF_CreateNewDocument = NULL;
static FPDF_ImportPages_fn       pFPDF_ImportPages = NULL;
static FPDF_SaveAsCopy_fn        pFPDF_SaveAsCopy = NULL;

static int MagifyPdfiumWriteBlock(void* pThis, const void* pData, unsigned long size) {
    MagifyPdfiumFileWrite* fw = (MagifyPdfiumFileWrite*)pThis;
    if (!fw || fw->hFile == INVALID_HANDLE_VALUE || !pData || size == 0) return 1;
    DWORD bw = 0;
    return WriteFile(fw->hFile, pData, size, &bw, NULL) ? 1 : 0;
}

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif

static void InitPdfiumBridge(void) {
    if (g_pdfiumAttempted) return;
    g_pdfiumAttempted = TRUE;

    // 1. Check directory where current executable is located
    WCHAR exeDir[MAX_PATH];
    if (GetModuleFileNameW(NULL, exeDir, MAX_PATH)) {
        PathRemoveFileSpecW(exeDir);
        WCHAR dllPath[MAX_PATH];
        swprintf(dllPath, MAX_PATH, L"%ls\\pdfium.dll", exeDir);
        g_hPdfium = LoadLibraryW(dllPath);
    }

    // 2. Check %LOCALAPPDATA%\Programs\Magify\pdfium.dll
    if (!g_hPdfium) {
        WCHAR appData[MAX_PATH];
        if (ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\Programs\\Magify\\pdfium.dll", appData, MAX_PATH)) {
            g_hPdfium = LoadLibraryW(appData);
        }
    }

    // 3. Fallback to standard LoadLibraryW search order
    if (!g_hPdfium) {
        g_hPdfium = LoadLibraryW(L"pdfium.dll");
    }

    if (!g_hPdfium) return;

    pFPDF_InitLibrary       = (FPDF_InitLibrary_fn)GetProcAddress(g_hPdfium, "FPDF_InitLibrary");
    pFPDF_DestroyLibrary    = (FPDF_DestroyLibrary_fn)GetProcAddress(g_hPdfium, "FPDF_DestroyLibrary");
    pFPDF_LoadMemDocument   = (FPDF_LoadMemDocument_fn)GetProcAddress(g_hPdfium, "FPDF_LoadMemDocument");
    pFPDF_CloseDocument     = (FPDF_CloseDocument_fn)GetProcAddress(g_hPdfium, "FPDF_CloseDocument");
    pFPDF_GetPageCount      = (FPDF_GetPageCount_fn)GetProcAddress(g_hPdfium, "FPDF_GetPageCount");
    pFPDF_LoadPage          = (FPDF_LoadPage_fn)GetProcAddress(g_hPdfium, "FPDF_LoadPage");
    pFPDF_ClosePage         = (FPDF_ClosePage_fn)GetProcAddress(g_hPdfium, "FPDF_ClosePage");
    pFPDF_GetPageWidth      = (FPDF_GetPageWidth_fn)GetProcAddress(g_hPdfium, "FPDF_GetPageWidth");
    pFPDF_GetPageHeight     = (FPDF_GetPageHeight_fn)GetProcAddress(g_hPdfium, "FPDF_GetPageHeight");
    pFPDFBitmap_CreateEx    = (FPDFBitmap_CreateEx_fn)GetProcAddress(g_hPdfium, "FPDFBitmap_CreateEx");
    pFPDFBitmap_Destroy     = (FPDFBitmap_Destroy_fn)GetProcAddress(g_hPdfium, "FPDFBitmap_Destroy");
    pFPDFBitmap_FillRect    = (FPDFBitmap_FillRect_fn)GetProcAddress(g_hPdfium, "FPDFBitmap_FillRect");
    pFPDF_RenderPageBitmap  = (FPDF_RenderPageBitmap_fn)GetProcAddress(g_hPdfium, "FPDF_RenderPageBitmap");
    pFPDF_CreateNewDocument = (FPDF_CreateNewDocument_fn)GetProcAddress(g_hPdfium, "FPDF_CreateNewDocument");
    pFPDF_ImportPages       = (FPDF_ImportPages_fn)GetProcAddress(g_hPdfium, "FPDF_ImportPages");
    pFPDF_SaveAsCopy        = (FPDF_SaveAsCopy_fn)GetProcAddress(g_hPdfium, "FPDF_SaveAsCopy");

    if (pFPDF_InitLibrary && pFPDF_LoadMemDocument && pFPDF_RenderPageBitmap) {
        pFPDF_InitLibrary();
        g_pdfiumAvailable = TRUE;
    }
}

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

// -------------------------------------------------------------
// Byte Buffer Utility for Dynamic PDF Generation
// -------------------------------------------------------------
typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} ByteBuffer;

static void BufInit(ByteBuffer *b, size_t initialCap) {
    b->capacity = initialCap > 0 ? initialCap : 8192;
    b->data = (uint8_t*)malloc(b->capacity);
    b->size = 0;
}

static void BufAppend(ByteBuffer *b, const void *src, size_t len) {
    if (!src || len == 0) return;
    if (b->size + len > b->capacity) {
        while (b->size + len > b->capacity) b->capacity = (b->capacity * 3) / 2 + 1024;
        b->data = (uint8_t*)realloc(b->data, b->capacity);
    }
    memcpy(b->data + b->size, src, len);
    b->size += len;
}

static void BufAppendStr(ByteBuffer *b, const char *str) {
    if (str) BufAppend(b, str, strlen(str));
}

static void BufAppendFmt(ByteBuffer *b, const char *fmt, ...) {
    char tmp[512];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    if (n > 0) BufAppend(b, tmp, (size_t)n);
}

static void BufFree(ByteBuffer *b) {
    if (b->data) free(b->data);
    b->data = NULL;
    b->size = b->capacity = 0;
}

static BOOL IsPdfWhitespace(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\0' || c == '\f');
}

// -------------------------------------------------------------
// PDF Parser Helpers
// -------------------------------------------------------------
static int FindIntKey(const char *dictStr, const char *key, int defaultVal) {
    const char *p = strstr(dictStr, key);
    if (!p) return defaultVal;
    p += strlen(key);
    while (*p && (IsPdfWhitespace(*p) || *p == ':')) p++;
    if (*p == '-' || (*p >= '0' && *p <= '9')) {
        return atoi(p);
    }
    return defaultVal;
}

static BOOL FindRefKey(const char *dictStr, const char *key, int *outId, int *outGen) {
    const char *p = strstr(dictStr, key);
    if (!p) return FALSE;
    p += strlen(key);
    while (*p && IsPdfWhitespace(*p)) p++;
    if (*p < '0' || *p > '9') return FALSE;
    int id = atoi(p);
    while (*p && *p >= '0' && *p <= '9') p++;
    while (*p && IsPdfWhitespace(*p)) p++;
    int gen = atoi(p);
    while (*p && *p >= '0' && *p <= '9') p++;
    while (*p && IsPdfWhitespace(*p)) p++;
    if (*p == 'R') {
        *outId = id;
        if (outGen) *outGen = gen;
        return TRUE;
    }
    return FALSE;
}

static BOOL FindMediaBox(const char *dictStr, float *outW, float *outH) {
    const char *p = strstr(dictStr, "/MediaBox");
    if (!p) return FALSE;
    p = strchr(p, '[');
    if (!p) return FALSE;
    p++;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    if (sscanf(p, "%f %f %f %f", &x0, &y0, &x1, &y1) == 4) {
        float w = (x1 > x0) ? (x1 - x0) : (x0 - x1);
        float h = (y1 > y0) ? (y1 - y0) : (y0 - y1);
        if (w > 10.0f && h > 10.0f) {
            *outW = w;
            *outH = h;
            return TRUE;
        }
    }
    return FALSE;
}

static int FindKidsRefs(const char *dictStr, int *outIds, int maxKids) {
    const char *p = strstr(dictStr, "/Kids");
    if (!p) return 0;
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    int count = 0;
    while (*p && *p != ']' && count < maxKids) {
        while (*p && IsPdfWhitespace(*p)) p++;
        if (*p == ']') break;
        if (*p >= '0' && *p <= '9') {
            int id = atoi(p);
            while (*p && *p >= '0' && *p <= '9') p++;
            while (*p && IsPdfWhitespace(*p)) p++;
            while (*p && *p >= '0' && *p <= '9') p++; // skip gen
            while (*p && IsPdfWhitespace(*p)) p++;
            if (*p == 'R') {
                p++;
                outIds[count++] = id;
            }
        } else {
            p++;
        }
    }
    return count;
}

// -------------------------------------------------------------
// Recursive Page Traversal
// -------------------------------------------------------------
static void TraversePagesNode(MagifyPdfDoc *doc, int objId, int inheritedRot, float inheritedW, float inheritedH, int depth) {
    if (depth > 32) return;
    if (objId <= 0 || objId >= MAGIFY_PDF_MAX_OBJECTS) return;
    if (!doc->objects[objId].inUse) return;
    if (doc->pageCount >= MAGIFY_PDF_MAX_PAGES) return;

    size_t len = doc->objects[objId].length;
    char *dictStr = (char*)malloc(len + 1);
    if (!dictStr) return;
    memcpy(dictStr, doc->data + doc->objects[objId].offset, len);
    dictStr[len] = '\0';

    int rot = FindIntKey(dictStr, "/Rotate", inheritedRot);
    rot = (rot % 360 + 360) % 360;

    float w = inheritedW, h = inheritedH;
    FindMediaBox(dictStr, &w, &h);

    int kids[MAGIFY_PDF_MAX_PAGES];
    int kidCount = FindKidsRefs(dictStr, kids, MAGIFY_PDF_MAX_PAGES);

    if (kidCount > 0 && strstr(dictStr, "/Kids")) {
        for (int i = 0; i < kidCount; i++) {
            TraversePagesNode(doc, kids[i], rot, w, h, depth + 1);
        }
    } else {
        MagifyPdfPageInfo *pi = &doc->pages[doc->pageCount];
        pi->pageIndex = doc->pageCount;
        pi->pageObjectId = objId;
        pi->pageGen = doc->objects[objId].gen;
        pi->currentRotation = rot;
        pi->addedRotation = 0;
        pi->widthPt = (w > 0.0f) ? w : 595.0f;
        pi->heightPt = (h > 0.0f) ? h : 842.0f;
        pi->markedDeleted = FALSE;
        pi->hThumbBitmap = NULL;
        pi->thumbPixels = NULL;
        pi->thumbW = 120;
        pi->thumbH = 160;
        pi->sourceDocIndex = 0;
        wcsncpy(pi->sourcePath, doc->filePath, MAX_PATH - 1);
        doc->pageCount++;
    }

    free(dictStr);
}

// -------------------------------------------------------------
// Document Open & Object Scanner
// -------------------------------------------------------------
MagifyPdfDoc *Magify_PdfDoc_Open(const WCHAR *filePath) {
    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return NULL;

    LARGE_INTEGER liSize;
    if (!GetFileSizeEx(hFile, &liSize) || liSize.QuadPart < 32 || liSize.QuadPart > 256 * 1024 * 1024) {
        CloseHandle(hFile);
        return NULL;
    }

    size_t size = (size_t)liSize.QuadPart;
    uint8_t *data = (uint8_t*)malloc(size + 16);
    if (!data) {
        CloseHandle(hFile);
        return NULL;
    }

    DWORD br = 0;
    if (!ReadFile(hFile, data, (DWORD)size, &br, NULL) || br != size) {
        free(data);
        CloseHandle(hFile);
        return NULL;
    }
    CloseHandle(hFile);
    data[size] = '\0';

    if (memcmp(data, "%PDF-", 5) != 0) {
        free(data);
        return NULL;
    }

    MagifyPdfDoc *doc = (MagifyPdfDoc*)calloc(1, sizeof(MagifyPdfDoc));
    if (!doc) {
        free(data);
        return NULL;
    }
    doc->data = data;
    doc->size = size;
    wcsncpy(doc->filePath, filePath, MAX_PATH - 1);

    // Scan objects via pattern "<digits> <digits> obj"
    for (size_t i = 0; i + 4 < size; i++) {
        if (data[i] == 'o' && data[i+1] == 'b' && data[i+2] == 'j' &&
            (IsPdfWhitespace(data[i+3]) || data[i+3] == '<' || data[i+3] == '[' || data[i+3] == '/')) {
            // Scan backwards for "<id> <gen>"
            size_t p = (i > 0) ? i - 1 : 0;
            while (p > 0 && IsPdfWhitespace(data[p])) p--;
            size_t genEnd = p;
            while (p > 0 && data[p] >= '0' && data[p] <= '9') p--;
            size_t genStart = p + 1;
            while (p > 0 && IsPdfWhitespace(data[p])) p--;
            size_t idEnd = p;
            while (p > 0 && data[p] >= '0' && data[p] <= '9') p--;
            size_t idStart = (p == 0 && data[0] >= '0' && data[0] <= '9') ? 0 : p + 1;

            if (idEnd >= idStart && genEnd >= genStart) {
                int objId = atoi((const char*)data + idStart);
                int gen   = atoi((const char*)data + genStart);

                if (objId > 0 && objId < MAGIFY_PDF_MAX_OBJECTS) {
                    size_t objStart = i + 3;
                    while (objStart < size && IsPdfWhitespace(data[objStart])) objStart++;

                    // Find "endobj"
                    size_t objEnd = size;
                    for (size_t k = objStart; k + 6 <= size; k++) {
                        if (memcmp(data + k, "endobj", 6) == 0) {
                            objEnd = k;
                            break;
                        }
                    }

                    MagifyPdfObjectInfo *info = &doc->objects[objId];
                    info->objId = objId;
                    info->gen = gen;
                    info->offset = objStart;
                    info->length = (objEnd >= objStart) ? (objEnd - objStart) : 0;
                    info->inUse = TRUE;

                    // Stream inspection
                    for (size_t s = objStart; s + 6 <= objEnd; s++) {
                        if (memcmp(data + s, "stream", 6) == 0 && (s == 0 || IsPdfWhitespace(data[s-1]))) {
                            size_t sData = s + 6;
                            if (sData < objEnd && data[sData] == '\r') sData++;
                            if (sData < objEnd && data[sData] == '\n') sData++;

                            for (size_t e = sData; e + 9 <= objEnd; e++) {
                                if (memcmp(data + e, "endstream", 9) == 0) {
                                    size_t sLen = e - sData;
                                    if (sLen > 0 && data[sData + sLen - 1] == '\n') sLen--;
                                    if (sLen > 0 && data[sData + sLen - 1] == '\r') sLen--;

                                    info->isStream = TRUE;
                                    info->streamOffset = sData;
                                    info->streamLength = sLen;
                                    break;
                                }
                            }
                            break;
                        }
                    }

                    // Check if image XObject
                    if (info->isStream) {
                        char *dictHead = (char*)malloc(info->length + 1);
                        if (dictHead) {
                            memcpy(dictHead, data + objStart, info->length);
                            dictHead[info->length] = '\0';
                            if (strstr(dictHead, "/Subtype /Image") || strstr(dictHead, "/Subtype/Image")) {
                                info->isImage = TRUE;
                                info->imgWidth = FindIntKey(dictHead, "/Width", 0);
                                info->imgHeight = FindIntKey(dictHead, "/Height", 0);
                                if (strstr(dictHead, "/DCTDecode")) info->isDCT = TRUE;
                            }
                            free(dictHead);
                        }
                    }

                    if (objId >= doc->objectCount) doc->objectCount = objId + 1;
                }
            }
        }
    }

    // Resolve /Root
    for (size_t k = (size > 8192) ? (size - 8192) : 0; k + 6 <= size; k++) {
        if (memcmp(data + k, "/Root", 5) == 0) {
            int rootId = 0, rootGen = 0;
            if (FindRefKey((const char*)data + k, "/Root", &rootId, &rootGen)) {
                doc->rootObjId = rootId;
                break;
            }
        }
    }

    // If Root found, resolve /Pages
    if (doc->rootObjId > 0 && doc->rootObjId < MAGIFY_PDF_MAX_OBJECTS && doc->objects[doc->rootObjId].inUse) {
        size_t rLen = doc->objects[doc->rootObjId].length;
        char *rStr = (char*)malloc(rLen + 1);
        if (rStr) {
            memcpy(rStr, doc->data + doc->objects[doc->rootObjId].offset, rLen);
            rStr[rLen] = '\0';
            int pagesId = 0;
            if (FindRefKey(rStr, "/Pages", &pagesId, NULL)) {
                doc->pagesObjId = pagesId;
            }
            free(rStr);
        }
    }

    if (doc->pagesObjId > 0) {
        TraversePagesNode(doc, doc->pagesObjId, 0, 595.0f, 842.0f, 0);
    }

    // Fallback scanner if pages array is still empty
    if (doc->pageCount == 0) {
        for (int i = 1; i < doc->objectCount && doc->pageCount < MAGIFY_PDF_MAX_PAGES; i++) {
            if (doc->objects[i].inUse && doc->objects[i].length > 0) {
                char *buf = (char*)malloc(doc->objects[i].length + 1);
                if (buf) {
                    memcpy(buf, doc->data + doc->objects[i].offset, doc->objects[i].length);
                    buf[doc->objects[i].length] = '\0';
                    if ((strstr(buf, "/Type /Page") || strstr(buf, "/Type/Page")) &&
                        !strstr(buf, "/Type /Pages") && !strstr(buf, "/Type/Pages")) {
                        float w = 595.0f, h = 842.0f;
                        FindMediaBox(buf, &w, &h);
                        int rot = FindIntKey(buf, "/Rotate", 0);
                        MagifyPdfPageInfo *pi = &doc->pages[doc->pageCount];
                        pi->pageIndex = doc->pageCount;
                        pi->pageObjectId = i;
                        pi->pageGen = doc->objects[i].gen;
                        pi->currentRotation = (rot % 360 + 360) % 360;
                        pi->addedRotation = 0;
                        pi->widthPt = w;
                        pi->heightPt = h;
                        pi->markedDeleted = FALSE;
                        pi->hThumbBitmap = NULL;
                        pi->thumbPixels = NULL;
                        pi->thumbW = 120;
                        pi->thumbH = 160;
                        pi->sourceDocIndex = 0;
                        wcsncpy(pi->sourcePath, doc->filePath, MAX_PATH - 1);
                        doc->pageCount++;
                    }
                    free(buf);
                }
            }
        }
    }

    if (doc->pageCount == 0) {
        Magify_PdfDoc_Close(doc);
        return NULL;
    }

    return doc;
}

void Magify_PdfDoc_Close(MagifyPdfDoc *doc) {
    if (!doc) return;
    for (int i = 0; i < doc->pageCount; i++) {
        if (doc->pages[i].thumbPixels) free(doc->pages[i].thumbPixels);
        if (doc->pages[i].hThumbBitmap) DeleteObject(doc->pages[i].hThumbBitmap);
    }
    if (doc->data) free(doc->data);
    free(doc);
}

// -------------------------------------------------------------
// XRef Table and Footer Writer
// -------------------------------------------------------------
static void WritePdfFooter(ByteBuffer *b, size_t *objOffsets, int maxObjId, int rootId) {
    size_t startXref = b->size;
    BufAppendFmt(b, "xref\n0 %d\n", maxObjId + 1);
    BufAppendStr(b, "0000000000 65535 f \r\n");
    for (int i = 1; i <= maxObjId; i++) {
        if (objOffsets[i] > 0) {
            BufAppendFmt(b, "%010llu 00000 n \r\n", (unsigned long long)objOffsets[i]);
        } else {
            BufAppendStr(b, "0000000000 65535 f \r\n");
        }
    }
    BufAppendFmt(b, "trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%llu\n%%%%EOF\n",
                 maxObjId + 1, rootId, (unsigned long long)startXref);
}

// -------------------------------------------------------------
// Synchronous Lossless PDF Rotate
// -------------------------------------------------------------
BOOL Magify_PdfRotateAllSync(const WCHAR *srcPath, const WCHAR *outPath, int deltaDegrees) {
    MagifyPdfDoc *doc = Magify_PdfDoc_Open(srcPath);
    if (!doc || doc->pageCount == 0) {
        if (doc) Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    ByteBuffer outBuf;
    BufInit(&outBuf, doc->size + 8192);
    BufAppendStr(&outBuf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    size_t *objOffsets = (size_t*)calloc(doc->objectCount + 1, sizeof(size_t));
    if (!objOffsets) {
        BufFree(&outBuf);
        Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    // Emit all objects
    for (int id = 1; id < doc->objectCount; id++) {
        if (!doc->objects[id].inUse) continue;
        objOffsets[id] = outBuf.size;

        // Check if page object
        int isPage = -1;
        for (int p = 0; p < doc->pageCount; p++) {
            if (doc->pages[p].pageObjectId == id) { isPage = p; break; }
        }

        if (isPage >= 0) {
            int newRot = (doc->pages[isPage].currentRotation + deltaDegrees + 360) % 360;
            size_t oLen = doc->objects[id].length;
            char *dict = (char*)malloc(oLen + 1);
            if (dict) {
                memcpy(dict, doc->data + doc->objects[id].offset, oLen);
                dict[oLen] = '\0';

                BufAppendFmt(&outBuf, "%d 0 obj\n", id);
                char *rotPos = strstr(dict, "/Rotate");
                if (rotPos) {
                    size_t preLen = rotPos - dict;
                    BufAppend(&outBuf, dict, preLen);
                    BufAppendFmt(&outBuf, "/Rotate %d", newRot);
                    const char *after = rotPos + 7;
                    while (*after && (*after == ' ' || (*after >= '0' && *after <= '9'))) after++;
                    BufAppendStr(&outBuf, after);
                } else {
                    char *endDict = strstr(dict, ">>");
                    if (endDict) {
                        size_t preLen = endDict - dict;
                        BufAppend(&outBuf, dict, preLen);
                        BufAppendFmt(&outBuf, " /Rotate %d >>", newRot);
                        BufAppendStr(&outBuf, endDict + 2);
                    } else {
                        BufAppend(&outBuf, dict, oLen);
                    }
                }
                BufAppendStr(&outBuf, "\nendobj\n");
                free(dict);
            }
        } else {
            BufAppendFmt(&outBuf, "%d 0 obj\n", id);
            BufAppend(&outBuf, doc->data + doc->objects[id].offset, doc->objects[id].length);
            BufAppendStr(&outBuf, "\nendobj\n");
        }
    }

    int rootId = (doc->rootObjId > 0) ? doc->rootObjId : 1;
    WritePdfFooter(&outBuf, objOffsets, doc->objectCount - 1, rootId);

    // Write output
    HANDLE hOut = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = FALSE;
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        if (WriteFile(hOut, outBuf.data, (DWORD)outBuf.size, &bw, NULL) && bw == outBuf.size) {
            ok = TRUE;
        }
        CloseHandle(hOut);
    }

    free(objOffsets);
    BufFree(&outBuf);
    Magify_PdfDoc_Close(doc);
    return ok;
}

// -------------------------------------------------------------
// Synchronous Lossless PDF Split (Burst or Range)
// -------------------------------------------------------------
BOOL Magify_PdfSplitSync(const WCHAR *srcPath, const WCHAR *rangeSpec, const WCHAR *outDirOrPath) {
    MagifyPdfDoc *doc = Magify_PdfDoc_Open(srcPath);
    if (!doc || doc->pageCount == 0) {
        if (doc) Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    BOOL isBurst = (!rangeSpec || wcscmp(rangeSpec, L"BURST") == 0);

    WCHAR dir[MAX_PATH], stem[MAX_PATH];
    wcsncpy(dir, srcPath, MAX_PATH - 1);
    PathRemoveFileSpecW(dir);
    wcsncpy(stem, PathFindFileNameW(srcPath), MAX_PATH - 1);
    PathRemoveExtensionW(stem);

    WCHAR baseDir[MAX_PATH];
    if (outDirOrPath && outDirOrPath[0]) {
        wcsncpy(baseDir, outDirOrPath, MAX_PATH - 1);
    } else {
        swprintf(baseDir, MAX_PATH, L"%ls\\%ls_split", dir, stem);
    }
    if (isBurst) {
        CreateDirectoryW(baseDir, NULL);
    }

    InitPdfiumBridge();
    if (g_pdfiumAvailable && pFPDF_CreateNewDocument && pFPDF_ImportPages && pFPDF_SaveAsCopy && doc->data && doc->size > 0) {
        void *srcDoc = pFPDF_LoadMemDocument(doc->data, (int)doc->size, NULL);
        if (srcDoc) {
            if (isBurst) {
                for (int p = 0; p < doc->pageCount; p++) {
                    void *destDoc = pFPDF_CreateNewDocument();
                    if (destDoc) {
                        char range[16];
                        snprintf(range, sizeof(range), "%d", p + 1);
                        pFPDF_ImportPages(destDoc, srcDoc, range, 0);

                        WCHAR pagePath[MAX_PATH];
                        swprintf(pagePath, MAX_PATH, L"%ls\\page_%03d.pdf", baseDir, p + 1);

                        HANDLE hOut = CreateFileW(pagePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                        if (hOut != INVALID_HANDLE_VALUE) {
                            MagifyPdfiumFileWrite fw;
                            fw.version = 1;
                            fw.WriteBlock = MagifyPdfiumWriteBlock;
                            fw.hFile = hOut;
                            pFPDF_SaveAsCopy(destDoc, &fw, 0);
                            CloseHandle(hOut);
                        }
                        pFPDF_CloseDocument(destDoc);
                    }
                }
                pFPDF_CloseDocument(srcDoc);
                Magify_PdfDoc_Close(doc);
                return TRUE;
            } else {
                // Custom range export (e.g. "1-2" or "1, 3") to single output PDF
                void *destDoc = pFPDF_CreateNewDocument();
                if (destDoc) {
                    char rangeA[128];
                    WideCharToMultiByte(CP_UTF8, 0, rangeSpec, -1, rangeA, sizeof(rangeA), NULL, NULL);
                    pFPDF_ImportPages(destDoc, srcDoc, rangeA, 0);

                    WCHAR outPath[MAX_PATH];
                    if (outDirOrPath && outDirOrPath[0]) {
                        wcsncpy(outPath, outDirOrPath, MAX_PATH - 1);
                    } else {
                        swprintf(outPath, MAX_PATH, L"%ls\\%ls_split_%ls.pdf", dir, stem, rangeSpec);
                    }

                    HANDLE hOut = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
                    if (hOut != INVALID_HANDLE_VALUE) {
                        MagifyPdfiumFileWrite fw;
                        fw.version = 1;
                        fw.WriteBlock = MagifyPdfiumWriteBlock;
                        fw.hFile = hOut;
                        pFPDF_SaveAsCopy(destDoc, &fw, 0);
                        CloseHandle(hOut);
                    }
                    pFPDF_CloseDocument(destDoc);
                    pFPDF_CloseDocument(srcDoc);
                    Magify_PdfDoc_Close(doc);
                    return TRUE;
                }
            }
            pFPDF_CloseDocument(srcDoc);
        }
    }

    if (isBurst) {
        for (int p = 0; p < doc->pageCount; p++) {
            WCHAR pagePath[MAX_PATH];
            swprintf(pagePath, MAX_PATH, L"%ls\\page_%03d.pdf", baseDir, p + 1);

            ByteBuffer outBuf;
            BufInit(&outBuf, 16384);
            BufAppendStr(&outBuf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

            // We rebuild a clean single-page PDF
            int pageObjId = doc->pages[p].pageObjectId;
            size_t offsets[MAGIFY_PDF_MAX_OBJECTS] = {0};

            // Obj 1: Catalog
            offsets[1] = outBuf.size;
            BufAppendStr(&outBuf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

            // Obj 2: Pages
            offsets[2] = outBuf.size;
            BufAppendFmt(&outBuf, "2 0 obj\n<< /Type /Pages /Kids [ 3 0 R ] /Count 1 >>\nendobj\n");

            // Obj 3: Page (mapped to 3)
            offsets[3] = outBuf.size;
            size_t pLen = doc->objects[pageObjId].length;
            char *pDict = (char*)malloc(pLen + 1);
            if (pDict) {
                memcpy(pDict, doc->data + doc->objects[pageObjId].offset, pLen);
                pDict[pLen] = '\0';
                BufAppendStr(&outBuf, "3 0 obj\n");

                // Replace /Parent
                char *par = strstr(pDict, "/Parent");
                if (par) {
                    size_t pre = par - pDict;
                    BufAppend(&outBuf, pDict, pre);
                    BufAppendStr(&outBuf, "/Parent 2 0 R");
                    const char *aft = par + 7;
                    while (*aft && IsPdfWhitespace(*aft)) aft++;
                    while (*aft && *aft >= '0' && *aft <= '9') aft++;
                    while (*aft && IsPdfWhitespace(*aft)) aft++;
                    while (*aft && *aft >= '0' && *aft <= '9') aft++;
                    while (*aft && IsPdfWhitespace(*aft)) aft++;
                    if (*aft == 'R') aft++;
                    BufAppendStr(&outBuf, aft);
                } else {
                    BufAppend(&outBuf, pDict, pLen);
                }
                BufAppendStr(&outBuf, "\nendobj\n");
                free(pDict);
            }

            // Copy all referenced resource/content objects from original doc offset keeping original IDs
            int maxId = 3;
            for (int id = 1; id < doc->objectCount; id++) {
                if (id == pageObjId || id == doc->rootObjId || id == doc->pagesObjId || id == 1 || id == 2 || id == 3) continue;
                if (!doc->objects[id].inUse) continue;
                if (id >= MAGIFY_PDF_MAX_OBJECTS) continue;
                offsets[id] = outBuf.size;
                BufAppendFmt(&outBuf, "%d 0 obj\n", id);
                BufAppend(&outBuf, doc->data + doc->objects[id].offset, doc->objects[id].length);
                BufAppendStr(&outBuf, "\nendobj\n");
                if (id > maxId) maxId = id;
            }

            WritePdfFooter(&outBuf, offsets, maxId, 1);

            HANDLE hOut = CreateFileW(pagePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (hOut != INVALID_HANDLE_VALUE) {
                DWORD bw = 0;
                WriteFile(hOut, outBuf.data, (DWORD)outBuf.size, &bw, NULL);
                CloseHandle(hOut);
            }
            BufFree(&outBuf);
        }
        Magify_PdfDoc_Close(doc);
        return TRUE;
    }

    // Custom Range Mode (e.g. "1-3, 5")
    BOOL selected[MAGIFY_PDF_MAX_PAGES] = {0};
    int selCount = 0;
    const WCHAR *r = rangeSpec;
    while (*r) {
        while (*r == L' ' || *r == L',') r++;
        if (!*r) break;
        int start = _wtoi(r);
        int end = start;
        while (*r >= L'0' && *r <= L'9') r++;
        if (*r == L'-') {
            r++;
            end = _wtoi(r);
            while (*r >= L'0' && *r <= L'9') r++;
        }
        for (int p = start; p <= end; p++) {
            int idx = p - 1;
            if (idx >= 0 && idx < doc->pageCount && !selected[idx]) {
                selected[idx] = TRUE;
                selCount++;
            }
        }
    }

    if (selCount == 0) {
        Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    ByteBuffer outBuf;
    BufInit(&outBuf, doc->size);
    BufAppendStr(&outBuf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    size_t offsets[MAGIFY_PDF_MAX_OBJECTS] = {0};
    offsets[1] = outBuf.size;
    BufAppendStr(&outBuf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Pages object
    offsets[2] = outBuf.size;
    BufAppendFmt(&outBuf, "2 0 obj\n<< /Type /Pages /Kids [");
    int nextMappedPageId = 10;
    int mappedPageIds[MAGIFY_PDF_MAX_PAGES];
    for (int p = 0; p < doc->pageCount; p++) {
        if (selected[p]) {
            mappedPageIds[p] = nextMappedPageId++;
            BufAppendFmt(&outBuf, " %d 0 R", mappedPageIds[p]);
        }
    }
    BufAppendFmt(&outBuf, " ] /Count %d >>\nendobj\n", selCount);

    int maxId = nextMappedPageId;
    for (int p = 0; p < doc->pageCount; p++) {
        if (!selected[p]) continue;
        int mId = mappedPageIds[p];
        int origId = doc->pages[p].pageObjectId;
        offsets[mId] = outBuf.size;
        BufAppendFmt(&outBuf, "%d 0 obj\n", mId);
        size_t oLen = doc->objects[origId].length;
        char *dict = (char*)malloc(oLen + 1);
        if (dict) {
            memcpy(dict, doc->data + doc->objects[origId].offset, oLen);
            dict[oLen] = '\0';
            char *par = strstr(dict, "/Parent");
            if (par) {
                size_t pre = par - dict;
                BufAppend(&outBuf, dict, pre);
                BufAppendStr(&outBuf, "/Parent 2 0 R");
                const char *aft = par + 7;
                while (*aft && IsPdfWhitespace(*aft)) aft++;
                while (*aft && *aft >= '0' && *aft <= '9') aft++;
                while (*aft && IsPdfWhitespace(*aft)) aft++;
                while (*aft && *aft >= '0' && *aft <= '9') aft++;
                while (*aft && IsPdfWhitespace(*aft)) aft++;
                if (*aft == 'R') aft++;
                BufAppendStr(&outBuf, aft);
            } else {
                BufAppend(&outBuf, dict, oLen);
            }
            free(dict);
        }
        BufAppendStr(&outBuf, "\nendobj\n");
    }

    // Copy resource objects
    int baseResId = nextMappedPageId + 10;
    for (int id = 1; id < doc->objectCount; id++) {
        BOOL isPageObj = FALSE;
        for (int p = 0; p < doc->pageCount; p++) {
            if (doc->pages[p].pageObjectId == id) { isPageObj = TRUE; break; }
        }
        if (isPageObj || id == doc->rootObjId || id == doc->pagesObjId) continue;
        if (!doc->objects[id].inUse) continue;

        int mId = id + baseResId;
        if (mId >= MAGIFY_PDF_MAX_OBJECTS) continue;
        offsets[mId] = outBuf.size;
        BufAppendFmt(&outBuf, "%d 0 obj\n", mId);
        BufAppend(&outBuf, doc->data + doc->objects[id].offset, doc->objects[id].length);
        BufAppendStr(&outBuf, "\nendobj\n");
        if (mId > maxId) maxId = mId;
    }

    WritePdfFooter(&outBuf, offsets, maxId, 1);

    HANDLE hOut = CreateFileW(outDirOrPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = FALSE;
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        WriteFile(hOut, outBuf.data, (DWORD)outBuf.size, &bw, NULL);
        CloseHandle(hOut);
        ok = TRUE;
    }

    BufFree(&outBuf);
    Magify_PdfDoc_Close(doc);
    return ok;
}

// -------------------------------------------------------------
// Synchronous Multi-PDF Merge
// -------------------------------------------------------------
BOOL Magify_PdfMergeSync(const WCHAR **fileList, int count, const WCHAR *outPath) {
    if (!fileList || count < 2 || !outPath) return FALSE;

    MagifyPdfDoc *docs[8] = {0};
    int docCount = (count > 8) ? 8 : count;
    int totalPages = 0;

    for (int i = 0; i < docCount; i++) {
        docs[i] = Magify_PdfDoc_Open(fileList[i]);
        if (docs[i]) totalPages += docs[i]->pageCount;
    }

    if (totalPages == 0) {
        for (int i = 0; i < docCount; i++) if (docs[i]) Magify_PdfDoc_Close(docs[i]);
        return FALSE;
    }

    ByteBuffer outBuf;
    BufInit(&outBuf, 65536);
    BufAppendStr(&outBuf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    size_t offsets[MAGIFY_PDF_MAX_OBJECTS] = {0};

    // Obj 1: Catalog
    offsets[1] = outBuf.size;
    BufAppendStr(&outBuf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    // Pre-calculate page mapping
    // Catalog = 1, Pages = 2
    int currentId = 10;
    int pageObjIds[MAGIFY_PDF_MAX_PAGES];
    int pageIdx = 0;

    for (int d = 0; d < docCount; d++) {
        if (!docs[d]) continue;
        for (int p = 0; p < docs[d]->pageCount; p++) {
            pageObjIds[pageIdx++] = currentId++;
        }
    }

    // Obj 2: Pages
    offsets[2] = outBuf.size;
    BufAppendFmt(&outBuf, "2 0 obj\n<< /Type /Pages /Kids [");
    for (int i = 0; i < totalPages; i++) {
        BufAppendFmt(&outBuf, " %d 0 R", pageObjIds[i]);
    }
    BufAppendFmt(&outBuf, " ] /Count %d >>\nendobj\n", totalPages);

    int maxId = currentId;
    pageIdx = 0;

    for (int d = 0; d < docCount; d++) {
        if (!docs[d]) continue;
        int baseOffset = (d + 1) * 800;

        for (int p = 0; p < docs[d]->pageCount; p++) {
            int mappedPageId = pageObjIds[pageIdx++];
            int origId = docs[d]->pages[p].pageObjectId;
            offsets[mappedPageId] = outBuf.size;

            BufAppendFmt(&outBuf, "%d 0 obj\n", mappedPageId);
            size_t oLen = docs[d]->objects[origId].length;
            char *dict = (char*)malloc(oLen + 1);
            if (dict) {
                memcpy(dict, docs[d]->data + docs[d]->objects[origId].offset, oLen);
                dict[oLen] = '\0';
                char *par = strstr(dict, "/Parent");
                if (par) {
                    size_t pre = par - dict;
                    BufAppend(&outBuf, dict, pre);
                    BufAppendStr(&outBuf, "/Parent 2 0 R");
                    const char *aft = par + 7;
                    while (*aft && IsPdfWhitespace(*aft)) aft++;
                    while (*aft && *aft >= '0' && *aft <= '9') aft++;
                    while (*aft && IsPdfWhitespace(*aft)) aft++;
                    while (*aft && *aft >= '0' && *aft <= '9') aft++;
                    while (*aft && IsPdfWhitespace(*aft)) aft++;
                    if (*aft == 'R') aft++;
                    BufAppendStr(&outBuf, aft);
                } else {
                    BufAppend(&outBuf, dict, oLen);
                }
                free(dict);
            }
            BufAppendStr(&outBuf, "\nendobj\n");
            if (mappedPageId > maxId) maxId = mappedPageId;
        }

        // Copy resource objects with offset
        for (int id = 1; id < docs[d]->objectCount; id++) {
            BOOL isPageObj = FALSE;
            for (int p = 0; p < docs[d]->pageCount; p++) {
                if (docs[d]->pages[p].pageObjectId == id) { isPageObj = TRUE; break; }
            }
            if (isPageObj || id == docs[d]->rootObjId || id == docs[d]->pagesObjId) continue;
            if (!docs[d]->objects[id].inUse) continue;

            int mId = id + baseOffset;
            if (mId >= MAGIFY_PDF_MAX_OBJECTS) continue;
            offsets[mId] = outBuf.size;
            BufAppendFmt(&outBuf, "%d 0 obj\n", mId);
            BufAppend(&outBuf, docs[d]->data + docs[d]->objects[id].offset, docs[d]->objects[id].length);
            BufAppendStr(&outBuf, "\nendobj\n");
            if (mId > maxId) maxId = mId;
        }
    }

    WritePdfFooter(&outBuf, offsets, maxId, 1);

    HANDLE hOut = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = FALSE;
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        WriteFile(hOut, outBuf.data, (DWORD)outBuf.size, &bw, NULL);
        CloseHandle(hOut);
        ok = TRUE;
    }

    for (int i = 0; i < docCount; i++) if (docs[i]) Magify_PdfDoc_Close(docs[i]);
    BufFree(&outBuf);
    return ok;
}

// -------------------------------------------------------------
// Synchronous In-Memory PDF Compression
// -------------------------------------------------------------
BOOL Magify_PdfCompressSync(const WCHAR *srcPath, const WCHAR *outPath, int presetDpi, int *outSavingsPct) {
    MagifyPdfDoc *doc = Magify_PdfDoc_Open(srcPath);
    if (!doc || doc->pageCount == 0) {
        if (doc) Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    Magify_Image_Init();

    int maxDim = 1600;
    int quality = 70;
    if (presetDpi >= 300) { maxDim = 2400; quality = 82; }
    else if (presetDpi <= 100) { maxDim = 1024; quality = 50; }

    ByteBuffer outBuf;
    BufInit(&outBuf, doc->size);
    BufAppendStr(&outBuf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    size_t *offsets = (size_t*)calloc(doc->objectCount + 1, sizeof(size_t));
    if (!offsets) {
        BufFree(&outBuf);
        Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    for (int id = 1; id < doc->objectCount; id++) {
        if (!doc->objects[id].inUse) continue;
        offsets[id] = outBuf.size;

        if (doc->objects[id].isImage && doc->objects[id].isDCT && doc->objects[id].streamLength > 1024) {
            // Compress JPEG stream
            const uint8_t *sBytes = doc->data + doc->objects[id].streamOffset;
            size_t sLen = doc->objects[id].streamLength;

            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sLen);
            if (hMem) {
                void *pLock = GlobalLock(hMem);
                memcpy(pLock, sBytes, sLen);
                GlobalUnlock(hMem);

                IStream *pInStream = NULL;
                if (CreateStreamOnHGlobal(hMem, TRUE, &pInStream) == S_OK) {
                    GpBitmap *pBmp = NULL;
                    if (GdipCreateBitmapFromStream(pInStream, &pBmp) == Ok && pBmp) {
                        UINT w = 0, h = 0;
                        GdipGetImageWidth((GpImage*)pBmp, &w);
                        GdipGetImageHeight((GpImage*)pBmp, &h);

                        GpBitmap *targetBmp = pBmp;
                        BOOL needFreeTarget = FALSE;

                        if (w > (UINT)maxDim || h > (UINT)maxDim) {
                            float scale = ((float)maxDim / (float)w < (float)maxDim / (float)h)
                                        ? ((float)maxDim / (float)w) : ((float)maxDim / (float)h);
                            int nw = (int)((float)w * scale);
                            int nh = (int)((float)h * scale);
                            if (nw < 1) nw = 1;
                            if (nh < 1) nh = 1;

                            GpBitmap *scaledBmp = NULL;
                            if (GdipCreateBitmapFromScan0(nw, nh, 0, 0x26200A, NULL, &scaledBmp) == Ok && scaledBmp) {
                                GpGraphics *g = NULL;
                                GdipGetImageGraphicsContext((GpImage*)scaledBmp, &g);
                                GdipSetInterpolationMode(g, 7); // HighQualityBicubic
                                GdipDrawImageRectRectI(g, (GpImage*)pBmp, 0, 0, nw, nh, 0, 0, w, h, 2, NULL, NULL, NULL);
                                GdipDeleteGraphics(g);
                                targetBmp = scaledBmp;
                                needFreeTarget = TRUE;
                            }
                        }

                        // Encode JPEG
                        IStream *pOutStream = NULL;
                        if (CreateStreamOnHGlobal(NULL, TRUE, &pOutStream) == S_OK) {
                            CLSID clsidJpeg = { 0x557cf401, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
                            GUID guidQuality = { 0x1d5be4b5, 0xfa4a, 0x452d, { 0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb } };
                            EncoderParameters encParams;
                            ULONG qVal = (ULONG)quality;
                            encParams.Count = 1;
                            encParams.Parameter[0].Guid = guidQuality;
                            encParams.Parameter[0].Type = 4;
                            encParams.Parameter[0].NumberOfValues = 1;
                            encParams.Parameter[0].Value = &qVal;

                            if (GdipSaveImageToStream((GpImage*)targetBmp, pOutStream, &clsidJpeg, &encParams) == Ok) {
                                STATSTG stat;
                                pOutStream->lpVtbl->Stat(pOutStream, &stat, STATFLAG_NONAME);
                                size_t newLen = (size_t)stat.cbSize.QuadPart;

                                if (newLen > 0 && newLen < sLen) {
                                    HGLOBAL hOutMem = NULL;
                                    GetHGlobalFromStream(pOutStream, &hOutMem);
                                    uint8_t *pData = (uint8_t*)GlobalLock(hOutMem);

                                    // Emit updated dictionary with new /Length
                                    UINT nw = 0, nh = 0;
                                    GdipGetImageWidth((GpImage*)targetBmp, &nw);
                                    GdipGetImageHeight((GpImage*)targetBmp, &nh);

                                    BufAppendFmt(&outBuf, "%d 0 obj\n<< /Type /XObject /Subtype /Image /Width %u /Height %u /BitsPerComponent 8 /ColorSpace /DeviceRGB /Filter /DCTDecode /Length %llu >>\nstream\n",
                                                 id, nw, nh, (unsigned long long)newLen);
                                    BufAppend(&outBuf, pData, newLen);
                                    BufAppendStr(&outBuf, "\nendstream\nendobj\n");
                                    GlobalUnlock(hOutMem);

                                    pOutStream->lpVtbl->Release(pOutStream);
                                    if (needFreeTarget) GdipDisposeImage((GpImage*)targetBmp);
                                    GdipDisposeImage((GpImage*)pBmp);
                                    pInStream->lpVtbl->Release(pInStream);
                                    continue;
                                }
                            }
                            pOutStream->lpVtbl->Release(pOutStream);
                        }
                        if (needFreeTarget) GdipDisposeImage((GpImage*)targetBmp);
                        GdipDisposeImage((GpImage*)pBmp);
                    }
                    pInStream->lpVtbl->Release(pInStream);
                }
            }
        }

        // Copy regular object as-is
        BufAppendFmt(&outBuf, "%d 0 obj\n", id);
        BufAppend(&outBuf, doc->data + doc->objects[id].offset, doc->objects[id].length);
        BufAppendStr(&outBuf, "\nendobj\n");
    }

    int rootId = (doc->rootObjId > 0) ? doc->rootObjId : 1;
    WritePdfFooter(&outBuf, offsets, doc->objectCount - 1, rootId);

    HANDLE hOut = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = FALSE;
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        WriteFile(hOut, outBuf.data, (DWORD)outBuf.size, &bw, NULL);
        CloseHandle(hOut);
        ok = TRUE;
    }

    if (outSavingsPct) {
        int pct = (int)((1.0 - (double)outBuf.size / (double)doc->size) * 100.0);
        *outSavingsPct = (pct > 0) ? pct : 0;
    }

    free(offsets);
    BufFree(&outBuf);
    Magify_PdfDoc_Close(doc);
    return ok;
}

// -------------------------------------------------------------
// Synchronous Text Extraction (.txt)
// -------------------------------------------------------------
BOOL Magify_PdfExtractTextSync(const WCHAR *srcPath, const WCHAR *outTxtPath) {
    MagifyPdfDoc *doc = Magify_PdfDoc_Open(srcPath);
    if (!doc || doc->pageCount == 0) {
        if (doc) Magify_PdfDoc_Close(doc);
        return FALSE;
    }

    ByteBuffer txtBuf;
    BufInit(&txtBuf, 16384);

    for (int p = 0; p < doc->pageCount; p++) {
        BufAppendFmt(&txtBuf, "--- Page %d ---\n", p + 1);

        int pageId = doc->pages[p].pageObjectId;
        size_t pLen = doc->objects[pageId].length;
        char *pDict = (char*)malloc(pLen + 1);
        if (!pDict) continue;
        memcpy(pDict, doc->data + doc->objects[pageId].offset, pLen);
        pDict[pLen] = '\0';

        int contentsId = 0;
        FindRefKey(pDict, "/Contents", &contentsId, NULL);
        free(pDict);

        if (contentsId > 0 && doc->objects[contentsId].isStream) {
            const uint8_t *sData = doc->data + doc->objects[contentsId].streamOffset;
            size_t sLen = doc->objects[contentsId].streamLength;

            size_t uncompLen = 0;
            void *pUncomp = tinfl_decompress_mem_to_heap(sData, sLen, &uncompLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
            if (!pUncomp) {
                pUncomp = tinfl_decompress_mem_to_heap(sData, sLen, &uncompLen, 0);
            }

            const char *streamStr = (pUncomp) ? (const char*)pUncomp : (const char*)sData;
            size_t streamLen = (pUncomp) ? uncompLen : sLen;

            // Extract Tj and TJ strings
            for (size_t i = 0; i < streamLen; i++) {
                if (streamStr[i] == '(') {
                    i++;
                    while (i < streamLen && streamStr[i] != ')') {
                        if (streamStr[i] == '\\' && i + 1 < streamLen) {
                            i++;
                            char esc = streamStr[i];
                            if (esc == 'n') BufAppendStr(&txtBuf, "\n");
                            else if (esc == 'r') BufAppendStr(&txtBuf, "\r");
                            else if (esc == 't') BufAppendStr(&txtBuf, "\t");
                            else BufAppend(&txtBuf, &esc, 1);
                        } else {
                            BufAppend(&txtBuf, &streamStr[i], 1);
                        }
                        i++;
                    }
                    BufAppendStr(&txtBuf, " ");
                } else if (streamStr[i] == '\n' || streamStr[i] == '\r') {
                    // Check operators
                    if (i > 2 && streamStr[i-2] == 'T' && streamStr[i-1] == '*') {
                        BufAppendStr(&txtBuf, "\n");
                    }
                }
            }
            BufAppendStr(&txtBuf, "\n\n");
            if (pUncomp) free(pUncomp);
        }
    }

    HANDLE hOut = CreateFileW(outTxtPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = FALSE;
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        WriteFile(hOut, txtBuf.data, (DWORD)txtBuf.size, &bw, NULL);
        CloseHandle(hOut);
        ok = TRUE;
    }

    BufFree(&txtBuf);
    Magify_PdfDoc_Close(doc);
    return ok;
}

// -------------------------------------------------------------
// Thumbnail & Page Renderer (PDFium + Standalone Fallback)
// -------------------------------------------------------------
BOOL Magify_PdfDoc_RenderThumbnail(MagifyPdfDoc *doc, int pageIdx, int targetW, int targetH, uint32_t *outPixels) {
    if (!doc || pageIdx < 0 || pageIdx >= doc->pageCount || !outPixels) return FALSE;

    InitPdfiumBridge();

    // 1. Dynamic PDFium Attempt
    if (g_pdfiumAvailable) {
        void *pDoc = pFPDF_LoadMemDocument(doc->data, (int)doc->size, NULL);
        if (pDoc) {
            void *pPage = pFPDF_LoadPage(pDoc, pageIdx);
            if (pPage) {
                void *pBmp = pFPDFBitmap_CreateEx(targetW, targetH, 4, outPixels, targetW * 4);
                if (pBmp) {
                    pFPDFBitmap_FillRect(pBmp, 0, 0, targetW, targetH, 0xFFFFFFFF);
                    int rot = (doc->pages[pageIdx].currentRotation + doc->pages[pageIdx].addedRotation) % 360;
                    pFPDF_RenderPageBitmap(pBmp, pPage, 0, 0, targetW, targetH, rot / 90, 0x01);
                    pFPDFBitmap_Destroy(pBmp);
                    pFPDF_ClosePage(pPage);
                    pFPDF_CloseDocument(pDoc);
                    return TRUE;
                }
                pFPDF_ClosePage(pPage);
            }
            pFPDF_CloseDocument(pDoc);
        }
    }

    // 2. Standalone Native High-DPI Fallback (Pure C11)
    // Fill white paper background
    for (int i = 0; i < targetW * targetH; i++) {
        outPixels[i] = 0xFFFFFFFF; // 100% white
    }

    // Draw page border
    for (int x = 0; x < targetW; x++) {
        outPixels[x] = 0xFFCBD5E1;
        outPixels[(targetH - 1) * targetW + x] = 0xFFCBD5E1;
    }
    for (int y = 0; y < targetH; y++) {
        outPixels[y * targetW] = 0xFFCBD5E1;
        outPixels[y * targetW + (targetW - 1)] = 0xFFCBD5E1;
    }

    // Check if page references an embedded image
    int pageId = doc->pages[pageIdx].pageObjectId;
    int foundImgId = -1;
    for (int id = 1; id < doc->objectCount; id++) {
        if (doc->objects[id].isImage && doc->objects[id].isDCT && doc->objects[id].streamLength > 100) {
            foundImgId = id;
            break;
        }
    }

    if (foundImgId > 0) {
        Magify_Image_Init();
        const uint8_t *sData = doc->data + doc->objects[foundImgId].streamOffset;
        size_t sLen = doc->objects[foundImgId].streamLength;

        // If zlib/Flate wrapped, decompress first
        const uint8_t *decData = sData;
        size_t decLen = sLen;
        void *pHeapDecomp = NULL;
        if (sLen > 2 && sData[0] == 0x78) {
            size_t uncompLen = 0;
            pHeapDecomp = tinfl_decompress_mem_to_heap(sData, sLen, &uncompLen, TINFL_FLAG_PARSE_ZLIB_HEADER);
            if (pHeapDecomp && uncompLen > 0) {
                decData = (const uint8_t*)pHeapDecomp;
                decLen = uncompLen;
            }
        }

        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, decLen);
        if (hMem) {
            void *pLock = GlobalLock(hMem);
            memcpy(pLock, decData, decLen);
            GlobalUnlock(hMem);
            if (pHeapDecomp) { free(pHeapDecomp); pHeapDecomp = NULL; }

            IStream *pStream = NULL;
            if (CreateStreamOnHGlobal(hMem, TRUE, &pStream) == S_OK) {
                GpBitmap *pBmp = NULL;
                if (GdipCreateBitmapFromStream(pStream, &pBmp) == Ok && pBmp) {
                    UINT imgW = 0, imgH = 0;
                    GdipGetImageWidth((GpImage*)pBmp, &imgW);
                    GdipGetImageHeight((GpImage*)pBmp, &imgH);

                    // Fit image centered inside thumbnail
                    float scale = ((float)(targetW - 16) / (float)imgW < (float)(targetH - 24) / (float)imgH)
                                ? ((float)(targetW - 16) / (float)imgW) : ((float)(targetH - 24) / (float)imgH);
                    int dw = (int)((float)imgW * scale);
                    int dh = (int)((float)imgH * scale);
                    int dx = (targetW - dw) / 2;
                    int dy = (targetH - dh) / 2;

                    GpBitmap *targetBmp = NULL;
                    if (GdipCreateBitmapFromScan0(targetW, targetH, targetW * 4, 0x26200A, (BYTE*)outPixels, &targetBmp) == Ok && targetBmp) {
                        GpGraphics *g = NULL;
                        GdipGetImageGraphicsContext((GpImage*)targetBmp, &g);
                        GdipSetInterpolationMode(g, 7); // HighQualityBicubic
                        GdipDrawImageRectRectI(g, (GpImage*)pBmp, dx, dy, dw, dh, 0, 0, imgW, imgH, 2, NULL, NULL, NULL);
                        GdipDeleteGraphics(g);
                        GdipDisposeImage((GpImage*)targetBmp);
                    }
                    GdipDisposeImage((GpImage*)pBmp);
                }
                pStream->lpVtbl->Release(pStream);
            }
        }
    } else {
        // Draw document header bar and simulated text lines
        for (int y = 14; y < 22; y++) {
            for (int x = 14; x < targetW - 14; x++) {
                outPixels[y * targetW + x] = 0xFF64748B; // Slate header
            }
        }
        for (int row = 0; row < 6; row++) {
            int lineY = 32 + row * 18;
            int lineW = (row % 2 == 1) ? (targetW - 38) : (targetW - 20);
            for (int y = lineY; y < lineY + 6 && y < targetH - 10; y++) {
                for (int x = 14; x < lineW; x++) {
                    outPixels[y * targetW + x] = 0xFF94A3B8; // Text line
                }
            }
        }
    }

    (void)pageId;
    return TRUE;
}

// -------------------------------------------------------------
// Visual Page Organizer HUD Lifecycle
// -------------------------------------------------------------
MagifyPdfOrganizerState *Magify_PdfOrganizer_Init(MagifyContext *ctx) {
    if (!ctx || ctx->fileCount == 0) return NULL;

    MagifyPdfDoc *doc = Magify_PdfDoc_Open(ctx->files[0]);
    if (!doc || doc->pageCount == 0) {
        if (doc) Magify_PdfDoc_Close(doc);
        return NULL;
    }

    MagifyPdfOrganizerState *org = (MagifyPdfOrganizerState*)calloc(1, sizeof(MagifyPdfOrganizerState));
    if (!org) {
        Magify_PdfDoc_Close(doc);
        return NULL;
    }

    org->primaryDoc = doc;
    org->totalPages = doc->pageCount;
    org->hoverPageIndex = -1;
    org->hoverPillIndex = -1;
    org->dragPageIndex = -1;
    org->dragTargetIndex = -1;

    for (int i = 0; i < doc->pageCount; i++) {
        org->pages[i] = doc->pages[i];
        org->pages[i].thumbW = 120;
        org->pages[i].thumbH = 160;
        org->pages[i].thumbPixels = (uint32_t*)malloc(120 * 160 * sizeof(uint32_t));
        if (org->pages[i].thumbPixels) {
            Magify_PdfDoc_RenderThumbnail(doc, i, 120, 160, org->pages[i].thumbPixels);
        }
    }

    int rows = (org->totalPages + 3) / 4;
    int totalH = rows * 206;
    org->maxScrollY = (totalH > 440) ? (totalH - 440) : 0;

    return org;
}

void Magify_PdfOrganizer_Free(MagifyPdfOrganizerState *org) {
    if (!org) return;
    for (int i = 0; i < org->totalPages; i++) {
        if (org->pages[i].thumbPixels) free(org->pages[i].thumbPixels);
        if (org->pages[i].hThumbBitmap) DeleteObject(org->pages[i].hThumbBitmap);
    }
    if (org->primaryDoc) Magify_PdfDoc_Close(org->primaryDoc);
    for (int d = 0; d < org->extraDocCount; d++) {
        if (org->extraDocs[d]) Magify_PdfDoc_Close(org->extraDocs[d]);
    }
    free(org);
}

BOOL Magify_PdfOrganizer_AddDoc(MagifyPdfOrganizerState *org, const WCHAR *filePath) {
    if (!org || org->extraDocCount >= 7) return FALSE;

    MagifyPdfDoc *newDoc = Magify_PdfDoc_Open(filePath);
    if (!newDoc || newDoc->pageCount == 0) {
        if (newDoc) Magify_PdfDoc_Close(newDoc);
        return FALSE;
    }

    int docIdx = org->extraDocCount + 1;
    org->extraDocs[org->extraDocCount++] = newDoc;

    for (int p = 0; p < newDoc->pageCount && org->totalPages < MAGIFY_PDF_MAX_PAGES; p++) {
        int idx = org->totalPages++;
        org->pages[idx] = newDoc->pages[p];
        org->pages[idx].sourceDocIndex = docIdx;
        wcsncpy(org->pages[idx].sourcePath, filePath, MAX_PATH - 1);
        org->pages[idx].thumbW = 120;
        org->pages[idx].thumbH = 160;
        org->pages[idx].thumbPixels = (uint32_t*)malloc(120 * 160 * sizeof(uint32_t));
        if (org->pages[idx].thumbPixels) {
            Magify_PdfDoc_RenderThumbnail(newDoc, p, 120, 160, org->pages[idx].thumbPixels);
        }
    }

    int rows = (org->totalPages + 3) / 4;
    int totalH = rows * 206;
    org->maxScrollY = (totalH > 440) ? (totalH - 440) : 0;
    return TRUE;
}

BOOL Magify_PdfApplyOrganizerChanges(MagifyContext *ctx, const MagifyPdfOrganizerState *org) {
    if (!ctx || !org || org->totalPages == 0) return FALSE;

    // Check overwrite vs _edited.pdf
    BOOL isOverwrite = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    WCHAR outPath[MAX_PATH];
    if (isOverwrite) {
        wcsncpy(outPath, ctx->files[0], MAX_PATH - 1);
    } else {
        WCHAR dir[MAX_PATH], base[MAX_PATH];
        wcsncpy(dir, ctx->files[0], MAX_PATH - 1);
        PathRemoveFileSpecW(dir);
        wcsncpy(base, PathFindFileNameW(ctx->files[0]), MAX_PATH - 1);
        PathRemoveExtensionW(base);
        swprintf(outPath, MAX_PATH, L"%ls\\%ls_edited.pdf", dir, base);
    }

    // Collect active non-deleted pages
    int activeCount = 0;
    for (int i = 0; i < org->totalPages; i++) {
        if (!org->pages[i].markedDeleted) activeCount++;
    }

    if (activeCount == 0) {
        swprintf(ctx->statusText, 128, L"Cannot save: all pages marked deleted");
        return FALSE;
    }

    ByteBuffer outBuf;
    BufInit(&outBuf, 65536);
    BufAppendStr(&outBuf, "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");

    size_t offsets[MAGIFY_PDF_MAX_OBJECTS] = {0};
    offsets[1] = outBuf.size;
    BufAppendStr(&outBuf, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    int currentId = 10;
    int pageObjIds[MAGIFY_PDF_MAX_PAGES];
    int activeIdx = 0;

    for (int i = 0; i < org->totalPages; i++) {
        if (!org->pages[i].markedDeleted) {
            pageObjIds[activeIdx++] = currentId++;
        }
    }

    offsets[2] = outBuf.size;
    BufAppendFmt(&outBuf, "2 0 obj\n<< /Type /Pages /Kids [");
    for (int i = 0; i < activeCount; i++) {
        BufAppendFmt(&outBuf, " %d 0 R", pageObjIds[i]);
    }
    BufAppendFmt(&outBuf, " ] /Count %d >>\nendobj\n", activeCount);

    int maxId = currentId;
    activeIdx = 0;

    for (int i = 0; i < org->totalPages; i++) {
        if (org->pages[i].markedDeleted) continue;
        int mappedPageId = pageObjIds[activeIdx++];
        offsets[mappedPageId] = outBuf.size;

        MagifyPdfDoc *srcDoc = (org->pages[i].sourceDocIndex == 0)
                             ? org->primaryDoc
                             : org->extraDocs[org->pages[i].sourceDocIndex - 1];

        int origId = org->pages[i].pageObjectId;
        int finalRot = (org->pages[i].currentRotation + org->pages[i].addedRotation) % 360;

        BufAppendFmt(&outBuf, "%d 0 obj\n", mappedPageId);
        size_t oLen = srcDoc->objects[origId].length;
        char *dict = (char*)malloc(oLen + 1);
        if (dict) {
            memcpy(dict, srcDoc->data + srcDoc->objects[origId].offset, oLen);
            dict[oLen] = '\0';

            char *rotPos = strstr(dict, "/Rotate");
            if (rotPos) {
                size_t preLen = rotPos - dict;
                BufAppend(&outBuf, dict, preLen);
                BufAppendFmt(&outBuf, "/Rotate %d", finalRot);
                const char *after = rotPos + 7;
                while (*after && (*after == ' ' || (*after >= '0' && *after <= '9'))) after++;
                BufAppendStr(&outBuf, after);
            } else {
                char *endDict = strstr(dict, ">>");
                if (endDict) {
                    size_t preLen = endDict - dict;
                    BufAppend(&outBuf, dict, preLen);
                    BufAppendFmt(&outBuf, " /Rotate %d >>", finalRot);
                    BufAppendStr(&outBuf, endDict + 2);
                } else {
                    BufAppend(&outBuf, dict, oLen);
                }
            }
            free(dict);
        }
        BufAppendStr(&outBuf, "\nendobj\n");
    }

    // Copy resource objects
    for (int d = 0; d <= org->extraDocCount; d++) {
        MagifyPdfDoc *srcDoc = (d == 0) ? org->primaryDoc : org->extraDocs[d - 1];
        if (!srcDoc) continue;
        int baseOffset = (d + 1) * 800;

        for (int id = 1; id < srcDoc->objectCount; id++) {
            BOOL isPageObj = FALSE;
            for (int p = 0; p < srcDoc->pageCount; p++) {
                if (srcDoc->pages[p].pageObjectId == id) { isPageObj = TRUE; break; }
            }
            if (isPageObj || id == srcDoc->rootObjId || id == srcDoc->pagesObjId) continue;
            if (!srcDoc->objects[id].inUse) continue;

            int mId = id + baseOffset;
            if (mId >= MAGIFY_PDF_MAX_OBJECTS) continue;
            offsets[mId] = outBuf.size;
            BufAppendFmt(&outBuf, "%d 0 obj\n", mId);
            BufAppend(&outBuf, srcDoc->data + srcDoc->objects[id].offset, srcDoc->objects[id].length);
            BufAppendStr(&outBuf, "\nendobj\n");
            if (mId > maxId) maxId = mId;
        }
    }

    WritePdfFooter(&outBuf, offsets, maxId, 1);

    HANDLE hOut = CreateFileW(outPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    BOOL ok = FALSE;
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        WriteFile(hOut, outBuf.data, (DWORD)outBuf.size, &bw, NULL);
        CloseHandle(hOut);
        ok = TRUE;
        swprintf(ctx->statusText, 128, L"Saved PDF (%d pages)", activeCount);
    }

    BufFree(&outBuf);
    return ok;
}

// -------------------------------------------------------------
// Asynchronous Worker Thread Functions
// -------------------------------------------------------------
typedef struct {
    MagifyContext *ctx;
    WCHAR srcPath[MAX_PATH];
    WCHAR outPath[MAX_PATH];
    WCHAR paramStr[64];
    int intParam;
} PdfWorkerParam;

static DWORD WINAPI WorkerRotate(void *arg) {
    PdfWorkerParam *p = (PdfWorkerParam*)arg;
    p->ctx->progressPercent = 30;
    p->ctx->jobProgress = 30;
    swprintf(p->ctx->progressStatusText, 128, L"Rotating PDF pages...");
    wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    Magify_GetSafeOutputPath(p->srcPath, L"rotated", L".pdf", p->ctx->overwriteInPlace, outPath, tmpPath);
    const WCHAR *target = p->ctx->overwriteInPlace ? tmpPath : outPath;

    BOOL ok = Magify_PdfRotateAllSync(p->srcPath, target, p->intParam);
    if (ok) {
        Magify_FinalizeOutputFile(p->srcPath, tmpPath, outPath, p->ctx->overwriteInPlace, p->ctx);
        if (p->ctx->overwriteInPlace) {
            swprintf(p->ctx->statusText, 128, L"Rotated pages %d° (in-place)", p->intParam);
        } else {
            swprintf(p->ctx->statusText, 128, L"Rotated pages %d°: %ls", p->intParam, PathFindFileNameW(outPath));
        }
    } else {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Error: Unsupported or corrupted file format");
    }

    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_PdfRotateAll(MagifyContext *ctx, int deltaDegrees) {
    if (!ctx || ctx->fileCount == 0) return FALSE;
    PdfWorkerParam *p = (PdfWorkerParam*)calloc(1, sizeof(PdfWorkerParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, ctx->files[0], MAX_PATH - 1);
    p->intParam = deltaDegrees;
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)WorkerRotate, p);
    return TRUE;
}

static DWORD WINAPI WorkerSplit(void *arg) {
    PdfWorkerParam *p = (PdfWorkerParam*)arg;
    p->ctx->progressPercent = 40;
    p->ctx->jobProgress = 40;
    swprintf(p->ctx->progressStatusText, 128, L"Splitting PDF document...");
    wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);

    WCHAR stem[MAX_PATH];
    wcsncpy(stem, PathFindFileNameW(p->srcPath), MAX_PATH - 1);
    PathRemoveExtensionW(stem);

    BOOL ok = Magify_PdfSplitSync(p->srcPath, p->paramStr, NULL);
    if (ok) {
        swprintf(p->ctx->statusText, 128, L"Exported split pages to %ls_split", stem);
    } else {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Error: Unsupported or corrupted file format");
    }

    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_PdfSplit(MagifyContext *ctx, const WCHAR *rangeSpec) {
    if (!ctx || ctx->fileCount == 0) return FALSE;
    PdfWorkerParam *p = (PdfWorkerParam*)calloc(1, sizeof(PdfWorkerParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, ctx->files[0], MAX_PATH - 1);
    wcsncpy(p->paramStr, (rangeSpec && rangeSpec[0]) ? rangeSpec : L"BURST", 63);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)WorkerSplit, p);
    return TRUE;
}

static DWORD WINAPI WorkerMerge(void *arg) {
    PdfWorkerParam *p = (PdfWorkerParam*)arg;
    p->ctx->progressPercent = 30;
    p->ctx->jobProgress = 30;
    swprintf(p->ctx->progressStatusText, 128, L"Merging PDF documents...");
    wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);

    const WCHAR *list[MAGIFY_MAX_FILES];
    for (int i = 0; i < p->ctx->fileCount; i++) list[i] = p->ctx->files[i];

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    if (p->outPath[0]) {
        wcsncpy(outPath, p->outPath, MAX_PATH - 1);
        outPath[MAX_PATH - 1] = L'\0';
    } else {
        Magify_GetSafeOutputPath(p->ctx->files[0], L"merged", L".pdf", FALSE, outPath, tmpPath);
    }

    BOOL ok = Magify_PdfMergeSync(list, p->ctx->fileCount, outPath);
    if (ok) {
        p->ctx->finalTotalBytes = Magify_GetFileSize(outPath);
        swprintf(p->ctx->statusText, 128, L"Merged %d PDFs into %ls", p->ctx->fileCount, PathFindFileNameW(outPath));
    } else {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Error: Unsupported or corrupted file format");
    }

    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_PdfMerge(MagifyContext *ctx, const WCHAR *outputPath) {
    if (!ctx || ctx->fileCount == 0) return FALSE;

    if (ctx->fileCount == 1) {
        // Prompt for second PDF
        WCHAR secondFile[MAX_PATH] = {0};
        OPENFILENAMEW ofn;
        memset(&ofn, 0, sizeof(ofn));
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.lpstrFilter = L"PDF Files (*.pdf)\0*.pdf\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = secondFile;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (GetOpenFileNameW(&ofn)) {
            wcsncpy(ctx->files[1], secondFile, MAX_PATH - 1);
            ctx->fileCount = 2;
        } else {
            return FALSE;
        }
    }

    PdfWorkerParam *p = (PdfWorkerParam*)calloc(1, sizeof(PdfWorkerParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    if (outputPath) wcsncpy(p->outPath, outputPath, MAX_PATH - 1);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)WorkerMerge, p);
    return TRUE;
}

static DWORD WINAPI WorkerCompress(void *arg) {
    PdfWorkerParam *p = (PdfWorkerParam*)arg;
    p->ctx->progressPercent = 30;
    p->ctx->jobProgress = 30;
    swprintf(p->ctx->progressStatusText, 128, L"Compressing PDF streams...");
    wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    Magify_GetSafeOutputPath(p->srcPath, L"compressed", L".pdf", p->ctx->overwriteInPlace, outPath, tmpPath);
    const WCHAR *target = p->ctx->overwriteInPlace ? tmpPath : outPath;

    int pct = 0;
    BOOL ok = Magify_PdfCompressSync(p->srcPath, target, p->intParam, &pct);
    if (ok) {
        Magify_FinalizeOutputFile(p->srcPath, tmpPath, outPath, p->ctx->overwriteInPlace, p->ctx);
        uint64_t origSz = p->ctx->originalTotalBytes;
        uint64_t newSz = p->ctx->finalTotalBytes;
        Magify_FormatSizeDelta(origSz, newSz, p->ctx->progressStatusText, 128);
        wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);
    } else {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Error: Unsupported or corrupted file format");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    }

    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_PdfCompress(MagifyContext *ctx, int presetLevel) {
    if (!ctx || ctx->fileCount == 0) return FALSE;
    PdfWorkerParam *p = (PdfWorkerParam*)calloc(1, sizeof(PdfWorkerParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, ctx->files[0], MAX_PATH - 1);
    p->intParam = presetLevel;
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)WorkerCompress, p);
    return TRUE;
}

static DWORD WINAPI WorkerConvertToImages(void *arg) {
    PdfWorkerParam *p = (PdfWorkerParam*)arg;
    MagifyPdfDoc *doc = Magify_PdfDoc_Open(p->srcPath);
    if (!doc || doc->pageCount == 0) {
        if (doc) Magify_PdfDoc_Close(doc);
        p->ctx->isErrorState = TRUE;
        p->ctx->progressPercent = 100;
        p->ctx->jobProgress = 100;
        swprintf(p->ctx->statusText, 128, L"Error: Unsupported or corrupted file format");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        free(p);
        return 0;
    }

    Magify_Image_Init();

    WCHAR dir[MAX_PATH], base[MAX_PATH], outDir[MAX_PATH];
    wcsncpy(dir, p->srcPath, MAX_PATH - 1);
    PathRemoveFileSpecW(dir);
    wcsncpy(base, PathFindFileNameW(p->srcPath), MAX_PATH - 1);
    PathRemoveExtensionW(base);
    swprintf(outDir, MAX_PATH, L"%ls\\%ls_pages", dir, base);
    CreateDirectoryW(outDir, NULL);

    InitPdfiumBridge();
    void *pPdfDoc = NULL;
    if (g_pdfiumAvailable && doc->data && doc->size > 0) {
        pPdfDoc = pFPDF_LoadMemDocument(doc->data, (int)doc->size, NULL);
    }

    CLSID clsidFormat = { 0x557cf401, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } }; // JPG default
    const WCHAR *ext = L"jpg";
    if (wcsicmp(p->paramStr, L"PNG") == 0) {
        clsidFormat = (CLSID){ 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
        ext = L"png";
    }

    int totalPages = doc->pageCount;
    if (pPdfDoc && pFPDF_GetPageCount) {
        int c = pFPDF_GetPageCount(pPdfDoc);
        if (c > 0) totalPages = c;
    }

    for (int i = 0; i < totalPages; i++) {
        if (p->ctx->cancelRequested) break;

        int pct = (i * 100) / totalPages;
        p->ctx->jobProgress = pct;
        p->ctx->progressPercent = pct;
        swprintf(p->ctx->statusText, 128, L"Rendering page %d of %d...", i + 1, totalPages);
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

        int curW = 1240;
        int curH = 1754;
        uint32_t *pagePixels = NULL;
        BOOL rendered = FALSE;

        if (pPdfDoc) {
            void *pPage = pFPDF_LoadPage(pPdfDoc, i);
            if (pPage) {
                double pw = (pFPDF_GetPageWidth ? pFPDF_GetPageWidth(pPage) : 595.0);
                double ph = (pFPDF_GetPageHeight ? pFPDF_GetPageHeight(pPage) : 842.0);
                if (pw > 0 && ph > 0) {
                    if (pw > ph) {
                        curW = 1754;
                        curH = (int)(1754.0 * (ph / pw));
                    } else {
                        curW = 1240;
                        curH = (int)(1240.0 * (ph / pw));
                    }
                }
                if (curW < 100) curW = 1240;
                if (curH < 100) curH = 1754;

                pagePixels = (uint32_t*)malloc((size_t)curW * (size_t)curH * sizeof(uint32_t));
                if (pagePixels) {
                    void *pBmp = pFPDFBitmap_CreateEx(curW, curH, 4, pagePixels, curW * 4);
                    if (pBmp) {
                        pFPDFBitmap_FillRect(pBmp, 0, 0, curW, curH, 0xFFFFFFFF);
                        int rot = 0;
                        if (i < doc->pageCount) {
                            rot = (doc->pages[i].currentRotation + doc->pages[i].addedRotation) % 360;
                        }
                        pFPDF_RenderPageBitmap(pBmp, pPage, 0, 0, curW, curH, rot / 90, 0x01);
                        pFPDFBitmap_Destroy(pBmp);
                        rendered = TRUE;
                    }
                }
                pFPDF_ClosePage(pPage);
            }
        }

        if (!rendered) {
            if (!pagePixels) pagePixels = (uint32_t*)malloc((size_t)curW * (size_t)curH * sizeof(uint32_t));
            if (pagePixels && Magify_PdfDoc_RenderThumbnail(doc, i, curW, curH, pagePixels)) {
                rendered = TRUE;
            }
        }

        if (rendered && pagePixels) {
            GpBitmap *pBmp = NULL;
            if (GdipCreateBitmapFromScan0(curW, curH, curW * 4, 0x26200A, (BYTE*)pagePixels, &pBmp) == Ok && pBmp) {
                WCHAR imgPath[MAX_PATH];
                swprintf(imgPath, MAX_PATH, L"%ls\\page_%03d.%ls", outDir, i + 1, ext);
                GdipSaveImageToFile((GpImage*)pBmp, imgPath, &clsidFormat, NULL);
                GdipDisposeImage((GpImage*)pBmp);
            }
        }

        if (pagePixels) {
            free(pagePixels);
            pagePixels = NULL;
        }
    }

    if (pPdfDoc) {
        pFPDF_CloseDocument(pPdfDoc);
    }
    Magify_PdfDoc_Close(doc);

    swprintf(p->ctx->statusText, 128, L"Exported %ls pages to %ls_pages", ext, base);
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_PdfConvertToImages(MagifyContext *ctx, const WCHAR *format) {
    if (!ctx || ctx->fileCount == 0) return FALSE;
    PdfWorkerParam *p = (PdfWorkerParam*)calloc(1, sizeof(PdfWorkerParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, ctx->files[0], MAX_PATH - 1);
    p->paramStr[0] = L'\0';
    wcsncpy(p->paramStr, (format && format[0]) ? format : L"JPG", 63);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)WorkerConvertToImages, p);
    return TRUE;
}

static DWORD WINAPI WorkerExtractText(void *arg) {
    PdfWorkerParam *p = (PdfWorkerParam*)arg;
    p->ctx->progressPercent = 30;
    p->ctx->jobProgress = 30;
    swprintf(p->ctx->progressStatusText, 128, L"Extracting text...");
    wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    Magify_GetSafeOutputPath(p->srcPath, L"text", L".txt", FALSE, outPath, tmpPath);

    BOOL ok = Magify_PdfExtractTextSync(p->srcPath, outPath);
    if (ok) {
        p->ctx->finalTotalBytes = Magify_GetFileSize(outPath);
        swprintf(p->ctx->statusText, 128, L"Extracted text to %ls", PathFindFileNameW(outPath));
    } else {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Error: Unsupported or corrupted file format");
    }

    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_PdfExtractText(MagifyContext *ctx) {
    if (!ctx || ctx->fileCount == 0) return FALSE;
    PdfWorkerParam *p = (PdfWorkerParam*)calloc(1, sizeof(PdfWorkerParam));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, ctx->files[0], MAX_PATH - 1);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)WorkerExtractText, p);
    return TRUE;
}

// -------------------------------------------------------------
// Legacy Wrapper Compatibility
// -------------------------------------------------------------
BOOL Magify_PDF_Compress(const WCHAR *srcPath, int dpiPreset, MagifyContext *ctx) {
    (void)srcPath;
    return Magify_PdfCompress(ctx, dpiPreset);
}

BOOL Magify_PDF_ConvertToImages(const WCHAR *srcPath, const WCHAR *format, MagifyContext *ctx) {
    (void)srcPath;
    if (wcsicmp(format, L"TXT") == 0) return Magify_PdfExtractText(ctx);
    return Magify_PdfConvertToImages(ctx, format);
}

BOOL Magify_PDF_RotatePages(const WCHAR *srcPath, int angleDegrees, MagifyContext *ctx) {
    (void)srcPath;
    return Magify_PdfRotateAll(ctx, angleDegrees);
}

BOOL Magify_PDF_Merge(const WCHAR **fileList, int count, const WCHAR *outPath, MagifyContext *ctx) {
    (void)fileList;
    (void)count;
    return Magify_PdfMerge(ctx, outPath);
}
