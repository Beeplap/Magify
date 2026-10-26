#ifndef MAGIFY_OPS_PDF_H
#define MAGIFY_OPS_PDF_H

#include "magify.h"

#define MAGIFY_PDF_MAX_PAGES 256
#define MAGIFY_PDF_MAX_OBJECTS 8192

typedef struct {
    int pageIndex;          // 0-based index in doc
    int pageObjectId;       // PDF object id (e.g. 12)
    int pageGen;            // usually 0
    int currentRotation;    // 0, 90, 180, 270 (from /Rotate or inherited)
    int addedRotation;      // user edits in HUD (+90, -90, 180)
    float widthPt, heightPt;// from /MediaBox [x0 y0 x1 y1]
    BOOL markedDeleted;     // user toggle in HUD
    HBITMAP hThumbBitmap;   // cached 120x160 DIB
    uint32_t *thumbPixels;  // 120x160 32-bit ARGB
    int thumbW, thumbH;
    int sourceDocIndex;     // 0 = primary doc, 1.. = added docs
    WCHAR sourcePath[MAX_PATH];
} MagifyPdfPageInfo;

typedef struct {
    int objId;
    int gen;
    size_t offset;          // byte offset in original buffer
    size_t length;          // byte length of object definition
    BOOL inUse;
    BOOL isStream;
    size_t streamOffset;
    size_t streamLength;
    BOOL isImage;           // /Subtype /Image
    BOOL isDCT;             // /Filter /DCTDecode
    int imgWidth, imgHeight;
} MagifyPdfObjectInfo;

typedef struct {
    uint8_t *data;
    size_t size;
    WCHAR filePath[MAX_PATH];
    int rootObjId;
    int pagesObjId;
    int pageCount;
    MagifyPdfPageInfo pages[MAGIFY_PDF_MAX_PAGES];
    int objectCount;
    MagifyPdfObjectInfo objects[MAGIFY_PDF_MAX_OBJECTS];
} MagifyPdfDoc;

typedef struct {
    MagifyPdfDoc *primaryDoc;
    MagifyPdfDoc *extraDocs[8];
    int extraDocCount;

    int totalPages;
    MagifyPdfPageInfo pages[MAGIFY_PDF_MAX_PAGES];

    int hoverPageIndex;
    int hoverPillIndex;     // -1=none, 0=CCW, 1=CW, 2=Delete
    int dragPageIndex;      // -1=not dragging, 0..N
    int dragTargetIndex;    // drop insertion target index
    POINT dragMousePos;
    BOOL isDragging;
    int scrollOffsetY;
    int maxScrollY;

    WCHAR customRange[64];
    BOOL isCustomRangeMode;
    BOOL splitBurstMode;
} MagifyPdfOrganizerState;

// High-level operations (worker-backed)
BOOL Magify_PdfRotateAll(MagifyContext *ctx, int deltaDegrees);
BOOL Magify_PdfSplit(MagifyContext *ctx, const WCHAR *rangeSpec);
BOOL Magify_PdfMerge(MagifyContext *ctx, const WCHAR *outputPath);
BOOL Magify_PdfCompress(MagifyContext *ctx, int presetLevel);
BOOL Magify_PdfConvertToImages(MagifyContext *ctx, const WCHAR *format);
BOOL Magify_PdfExtractText(MagifyContext *ctx);

// Organizer lifecycle & state
MagifyPdfOrganizerState *Magify_PdfOrganizer_Init(MagifyContext *ctx);
void Magify_PdfOrganizer_Free(MagifyPdfOrganizerState *org);
BOOL Magify_PdfApplyOrganizerChanges(MagifyContext *ctx, const MagifyPdfOrganizerState *org);
BOOL Magify_PdfOrganizer_AddDoc(MagifyPdfOrganizerState *org, const WCHAR *filePath);

// Document parser & renderer helpers
MagifyPdfDoc *Magify_PdfDoc_Open(const WCHAR *filePath);
void Magify_PdfDoc_Close(MagifyPdfDoc *doc);
BOOL Magify_PdfDoc_RenderThumbnail(MagifyPdfDoc *doc, int pageIdx, int targetW, int targetH, uint32_t *outPixels);

// Synchronous operations for direct script/test execution
BOOL Magify_PdfRotateAllSync(const WCHAR *srcPath, const WCHAR *outPath, int deltaDegrees);
BOOL Magify_PdfSplitSync(const WCHAR *srcPath, const WCHAR *rangeSpec, const WCHAR *outDirOrPath);
BOOL Magify_PdfMergeSync(const WCHAR **fileList, int count, const WCHAR *outPath);
BOOL Magify_PdfCompressSync(const WCHAR *srcPath, const WCHAR *outPath, int presetDpi, int *outSavingsPct);
BOOL Magify_PdfExtractTextSync(const WCHAR *srcPath, const WCHAR *outTxtPath);

// Legacy/Compatibility wrappers
BOOL Magify_PDF_Compress(const WCHAR *srcPath, int dpiPreset, MagifyContext *ctx);
BOOL Magify_PDF_ConvertToImages(const WCHAR *srcPath, const WCHAR *format, MagifyContext *ctx);
BOOL Magify_PDF_RotatePages(const WCHAR *srcPath, int angleDegrees, MagifyContext *ctx);
BOOL Magify_PDF_Merge(const WCHAR **fileList, int count, const WCHAR *outPath, MagifyContext *ctx);

#endif // MAGIFY_OPS_PDF_H
