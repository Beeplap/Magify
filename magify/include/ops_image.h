#ifndef MAGIFY_OPS_IMAGE_H
#define MAGIFY_OPS_IMAGE_H

#include "magify.h"
#include <objidl.h>

#ifndef WINGDIPAPI
#define WINGDIPAPI __stdcall
#endif

typedef int GpStatus;
#define Ok 0

typedef void GpImage;
typedef void GpBitmap;
typedef void GpGraphics;

typedef enum {
    RotateNoneFlipNone = 0,
    Rotate90FlipNone   = 1,
    Rotate180FlipNone  = 2,
    Rotate270FlipNone  = 3,
    RotateNoneFlipX    = 4,
    Rotate90FlipX      = 5,
    Rotate180FlipX     = 6,
    Rotate270FlipX     = 7,
    RotateNoneFlipY    = Rotate180FlipX,
    Rotate90FlipY      = Rotate270FlipX,
    Rotate180FlipY     = RotateNoneFlipX,
    Rotate270FlipY     = Rotate90FlipX
} RotateFlipType;

typedef enum {
    InterpolationModeDefault             = 0,
    InterpolationModeLowQuality          = 1,
    InterpolationModeHighQuality         = 2,
    InterpolationModeBilinear            = 3,
    InterpolationModeBicubic             = 4,
    InterpolationModeNearestNeighbor     = 5,
    InterpolationModeHighQualityBilinear = 6,
    InterpolationModeHighQualityBicubic  = 7
} InterpolationMode;

typedef struct {
    uint32_t GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

typedef struct {
    void *NotificationHook;
    void *NotificationUnhook;
} GdiplusStartupOutput;

typedef struct {
    GUID Guid;
    ULONG NumberOfValues;
    ULONG Type;
    void *Value;
} EncoderParameter;

typedef struct {
    UINT Count;
    EncoderParameter Parameter[1];
} EncoderParameters;

// GDI+ Flat API Function Prototypes
GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR *token, const GdiplusStartupInput *input, GdiplusStartupOutput *output);
void     WINGDIPAPI GdiplusShutdown(ULONG_PTR token);

GpStatus WINGDIPAPI GdipCreateBitmapFromFile(const WCHAR *filename, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipCreateBitmapFromStream(IStream *stream, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipCreateBitmapFromScan0(int width, int height, int stride, int format, BYTE *scan0, GpBitmap **bitmap);
GpStatus WINGDIPAPI GdipDisposeImage(GpImage *image);
GpStatus WINGDIPAPI GdipCloneBitmapAreaI(int x, int y, int width, int height, int format, GpBitmap *src, GpBitmap **dst);

GpStatus WINGDIPAPI GdipGetImageWidth(GpImage *image, UINT *width);
GpStatus WINGDIPAPI GdipGetImageHeight(GpImage *image, UINT *height);
GpStatus WINGDIPAPI GdipImageRotateFlip(GpImage *image, RotateFlipType rfType);

GpStatus WINGDIPAPI GdipGetImageGraphicsContext(GpImage *image, GpGraphics **graphics);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINGDIPAPI GdipSetInterpolationMode(GpGraphics *graphics, InterpolationMode mode);
GpStatus WINGDIPAPI GdipGraphicsClear(GpGraphics *graphics, DWORD color);
GpStatus WINGDIPAPI GdipDrawImageRectI(GpGraphics *graphics, GpImage *image, int x, int y, int width, int height);
GpStatus WINGDIPAPI GdipDrawImageRectRectI(GpGraphics *graphics, GpImage *image,
                                         int dstx, int dsty, int dstwidth, int dstheight,
                                         int srcx, int srcy, int srcwidth, int srcheight,
                                         int srcUnit, void *imageAttributes, void *callback, void *callbackData);

GpStatus WINGDIPAPI GdipSaveImageToFile(GpImage *image, const WCHAR *filename, const CLSID *clsidEncoder, const EncoderParameters *encoderParams);
GpStatus WINGDIPAPI GdipSaveImageToStream(GpImage *image, IStream *stream, const CLSID *clsidEncoder, const EncoderParameters *encoderParams);

// Image Engine Operations
BOOL Magify_Image_Init(void);
void Magify_Image_Shutdown(void);
BOOL Magify_Image_Convert(const WCHAR *srcPath, const WCHAR *dstExt, MagifyContext *ctx);
BOOL Magify_Image_Rotate(const WCHAR *srcPath, int angleDegrees, MagifyContext *ctx);
BOOL Magify_Image_Flip(const WCHAR *srcPath, BOOL horizontal, MagifyContext *ctx);
BOOL Magify_Image_Resize(const WCHAR *srcPath, int targetW, int targetH, int percent, MagifyContext *ctx);
BOOL Magify_Image_Compress(const WCHAR *srcPath, int qualityOrTarget, MagifyContext *ctx);
BOOL Magify_CropImage(const WCHAR *srcPath, int cropX, int cropY, int cropW, int cropH, MagifyContext *ctx);
BOOL Magify_ImagesToPDF(const WCHAR **srcFiles, int fileCount, const WCHAR *outPdfPath, MagifyContext *ctx);
BOOL Magify_CompressImageToTargetBytes(const WCHAR *srcPath, size_t maxBytes, MagifyContext *ctx);
BOOL Magify_LoadImageDimensions(const WCHAR *srcPath, int *outW, int *outH);
BOOL Magify_GeneratePreview(const WCHAR *srcPath, int targetW, int targetH, uint32_t *outPixels, int *actualW, int *actualH);

#endif // MAGIFY_OPS_IMAGE_H
