#define COBJMACROS
#include "ops_image.h"
#include "ops_common.h"
#include <stdio.h>
#include <shlwapi.h>
#include <wincodec.h>

static ULONG_PTR g_gdiplusToken = 0;
static int g_gdiplusInitCount = 0;

static const CLSID CLSID_PNG  = { 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
static const CLSID CLSID_JPEG = { 0x557cf401, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
static const CLSID CLSID_BMP  = { 0x557cf400, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
static const GUID  GUID_EncoderQuality = { 0x1d5be4b5, 0xfa4a, 0x452d, { 0x9c, 0xdd, 0x5d, 0xb3, 0x51, 0x05, 0xe7, 0xeb } };

#define PixelFormat32bppARGB 0x26200A
#define PixelFormat24bppRGB  0x21808

#ifndef SafeRelease
#define SafeRelease(p) do { if ((p)) { (p)->lpVtbl->Release(p); (p) = NULL; } } while (0)
#endif

BOOL Magify_Image_Init(void) {
    if (g_gdiplusInitCount++ > 0) return TRUE;
    GdiplusStartupInput input = { 1, NULL, FALSE, FALSE };
    return (GdiplusStartup(&g_gdiplusToken, &input, NULL) == Ok);
}

void Magify_Image_Shutdown(void) {
    if (--g_gdiplusInitCount == 0 && g_gdiplusToken) {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

// -------------------------------------------------------------
// Native WIC Hardware-Accelerated Decoders (HEIC, WebP, TIFF)
// -------------------------------------------------------------
HRESULT Magify_WIC_DecodeToPixels(const WCHAR *filePath, uint32_t **outPixels, int *outW, int *outH) {
    if (!filePath || !outPixels || !outW || !outH) return E_POINTER;
    *outPixels = NULL;
    *outW = 0;
    *outH = 0;

    CoInitialize(NULL);

    IWICImagingFactory *pFactory = NULL;
    IWICBitmapDecoder *pDecoder = NULL;
    IWICBitmapFrameDecode *pFrame = NULL;
    IWICFormatConverter *pConverter = NULL;
    uint32_t *pixels = NULL;

    HRESULT hr = CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWICImagingFactory, (void**)&pFactory);
    if (FAILED(hr) || !pFactory) goto cleanup;

    hr = IWICImagingFactory_CreateDecoderFromFilename(
        pFactory, filePath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder);
    if (FAILED(hr) || !pDecoder) goto cleanup;

    hr = IWICBitmapDecoder_GetFrame(pDecoder, 0, &pFrame);
    if (FAILED(hr) || !pFrame) goto cleanup;

    UINT w = 0, h = 0;
    hr = IWICBitmapFrameDecode_GetSize(pFrame, &w, &h);
    if (FAILED(hr) || w == 0 || h == 0) {
        if (SUCCEEDED(hr)) hr = E_FAIL;
        goto cleanup;
    }

    hr = IWICImagingFactory_CreateFormatConverter(pFactory, &pConverter);
    if (FAILED(hr) || !pConverter) goto cleanup;

    hr = IWICFormatConverter_Initialize(
        pConverter, (IWICBitmapSource*)pFrame,
        &GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto cleanup;

    UINT stride = w * 4;
    UINT bufSize = stride * h;
    pixels = (uint32_t*)malloc(bufSize);
    if (!pixels) {
        hr = E_OUTOFMEMORY;
        goto cleanup;
    }

    hr = IWICFormatConverter_CopyPixels(pConverter, NULL, stride, bufSize, (BYTE*)pixels);
    if (FAILED(hr)) {
        free(pixels);
        pixels = NULL;
        goto cleanup;
    }

    *outPixels = pixels;
    *outW = (int)w;
    *outH = (int)h;
    hr = S_OK;

cleanup:
    SafeRelease(pConverter);
    SafeRelease(pFrame);
    SafeRelease(pDecoder);
    SafeRelease(pFactory);
    return hr;
}

GpBitmap *Magify_Image_LoadBitmap(const WCHAR *filePath, void **outPixelBuffer) {
    if (outPixelBuffer) *outPixelBuffer = NULL;
    if (!filePath || filePath[0] == L'\0') return NULL;

    Magify_Image_Init();

    GpBitmap *bmp = NULL;
    const WCHAR *ext = PathFindExtensionW(filePath);
    BOOL isModern = (_wcsicmp(ext, L".heic") == 0 ||
                     _wcsicmp(ext, L".heif") == 0 ||
                     _wcsicmp(ext, L".webp") == 0);

    if (!isModern && GdipCreateBitmapFromFile(filePath, &bmp) == Ok && bmp) {
        return bmp;
    }

    // Try WIC decoder for modern or unhandled formats
    uint32_t *pixels = NULL;
    int w = 0, h = 0;
    if (SUCCEEDED(Magify_WIC_DecodeToPixels(filePath, &pixels, &w, &h)) && pixels && w > 0 && h > 0) {
        if (GdipCreateBitmapFromScan0(w, h, w * 4, PixelFormat32bppARGB, (BYTE*)pixels, &bmp) == Ok && bmp) {
            if (outPixelBuffer) {
                *outPixelBuffer = pixels;
                return bmp;
            } else {
                GpBitmap *cloned = NULL;
                if (GdipCloneBitmapAreaI(0, 0, w, h, PixelFormat32bppARGB, bmp, &cloned) == Ok && cloned) {
                    GdipDisposeImage(bmp);
                    free(pixels);
                    return cloned;
                }
                return bmp;
            }
        }
        free(pixels);
    }

    // Fallback attempt
    if (isModern && GdipCreateBitmapFromFile(filePath, &bmp) == Ok && bmp) {
        return bmp;
    }

    return NULL;
}

void Magify_Image_FreeBitmap(GpBitmap *bmp, void *pixelBuffer) {
    if (bmp) {
        GdipDisposeImage(bmp);
    }
    if (pixelBuffer) {
        free(pixelBuffer);
    }
}

static void GetOutputPath(const WCHAR *srcPath, const WCHAR *suffix, const WCHAR *newExt,
                          BOOL overwriteRequested, WCHAR *outPath, WCHAR *tmpPath) {
    Magify_GetSafeOutputPath(srcPath, suffix, newExt, overwriteRequested, outPath, tmpPath);
}

static BOOL SaveBitmapSafely(GpBitmap *bmp, const WCHAR *srcPath, const WCHAR *outPath,
                             const WCHAR *tmpPath, const CLSID *clsid,
                             const EncoderParameters *params, BOOL overwrite, MagifyContext *ctx) {
    const WCHAR *saveFile = overwrite ? tmpPath : outPath;
    if (GdipSaveImageToFile(bmp, saveFile, clsid, params) != Ok) {
        if (ctx) {
            ctx->isErrorState = TRUE;
            swprintf(ctx->progressStatusText, 128, L"Failed to save image file");
            wcsncpy(ctx->statusText, ctx->progressStatusText, 127);
        }
        return FALSE;
    }
    return Magify_FinalizeOutputFile(srcPath, tmpPath, outPath, overwrite, ctx);
}

typedef struct {
    MagifyContext *ctx;
    WCHAR srcPath[MAX_PATH];
    WCHAR formatOrAction[32];
    int intParam;
    BOOL boolParam;
    BOOL overwrite;
    int cropX, cropY, cropW, cropH;
} ImageWorkerArg;

BOOL Magify_LoadImageDimensions(const WCHAR *srcPath, int *outW, int *outH) {
    Magify_Image_Init();
    GpBitmap *bmp = NULL;
    const WCHAR *ext = PathFindExtensionW(srcPath);
    BOOL isModern = (_wcsicmp(ext, L".heic") == 0 ||
                     _wcsicmp(ext, L".heif") == 0 ||
                     _wcsicmp(ext, L".webp") == 0);

    if (!isModern && GdipCreateBitmapFromFile(srcPath, &bmp) == Ok && bmp) {
        UINT w = 0, h = 0;
        GdipGetImageWidth(bmp, &w);
        GdipGetImageHeight(bmp, &h);
        GdipDisposeImage(bmp);
        *outW = (int)w;
        *outH = (int)h;
        return TRUE;
    }

    // WIC dimension query fallback
    CoInitialize(NULL);
    IWICImagingFactory *pFactory = NULL;
    IWICBitmapDecoder *pDecoder = NULL;
    IWICBitmapFrameDecode *pFrame = NULL;
    BOOL success = FALSE;

    if (SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                   &IID_IWICImagingFactory, (void**)&pFactory)) && pFactory) {
        if (SUCCEEDED(IWICImagingFactory_CreateDecoderFromFilename(
                pFactory, srcPath, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &pDecoder)) && pDecoder) {
            if (SUCCEEDED(IWICBitmapDecoder_GetFrame(pDecoder, 0, &pFrame)) && pFrame) {
                UINT w = 0, h = 0;
                if (SUCCEEDED(IWICBitmapFrameDecode_GetSize(pFrame, &w, &h)) && w > 0 && h > 0) {
                    *outW = (int)w;
                    *outH = (int)h;
                    success = TRUE;
                }
            }
        }
    }

    SafeRelease(pFrame);
    SafeRelease(pDecoder);
    SafeRelease(pFactory);
    return success;
}

static DWORD WINAPI ConvertWorker(void *arg) {
    ImageWorkerArg *p = (ImageWorkerArg*)arg;
    Magify_Image_Init();

    p->ctx->jobProgress = 20;
    swprintf(p->ctx->statusText, 128, L"Loading image...");

    void *srcPixels = NULL;
    GpBitmap *bmp = Magify_Image_LoadBitmap(p->srcPath, &srcPixels);
    if (!bmp) {
        swprintf(p->ctx->statusText, 128, L"Failed to load image");
        free(p);
        return 0;
    }

    UINT w = 0, h = 0;
    GdipGetImageWidth(bmp, &w);
    GdipGetImageHeight(bmp, &h);

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];

    if (_wcsicmp(p->formatOrAction, L"PNG") == 0) {
        GetOutputPath(p->srcPath, L"converted", L".png", p->overwrite, outPath, tmpPath);
        p->ctx->jobProgress = 60;
        p->ctx->progressPercent = 60;
        swprintf(p->ctx->statusText, 128, L"Encoding lossless PNG...");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        if (SaveBitmapSafely(bmp, p->srcPath, outPath, tmpPath, &CLSID_PNG, NULL, p->overwrite, p->ctx)) {
            swprintf(p->ctx->statusText, 128, L"Saved PNG: %ls", PathFindFileNameW(p->overwrite ? p->srcPath : outPath));
        } else {
            swprintf(p->ctx->statusText, 128, L"Failed to save PNG");
        }
    } else if (_wcsicmp(p->formatOrAction, L"JPG") == 0 || _wcsicmp(p->formatOrAction, L"JPEG") == 0) {
        GetOutputPath(p->srcPath, L"converted", L".jpg", p->overwrite, outPath, tmpPath);
        p->ctx->jobProgress = 50;
        p->ctx->progressPercent = 50;
        swprintf(p->ctx->statusText, 128, L"Encoding JPEG (92%%)...");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

        // Composite onto solid white background
        GpBitmap *rgbBmp = NULL;
        GdipCreateBitmapFromScan0((int)w, (int)h, 0, PixelFormat24bppRGB, NULL, &rgbBmp);
        GpGraphics *gfx = NULL;
        GdipGetImageGraphicsContext(rgbBmp, &gfx);
        GdipGraphicsClear(gfx, 0xFFFFFFFF);
        GdipDrawImageRectI(gfx, bmp, 0, 0, (int)w, (int)h);

        EncoderParameters params;
        params.Count = 1;
        params.Parameter[0].Guid = GUID_EncoderQuality;
        params.Parameter[0].NumberOfValues = 1;
        params.Parameter[0].Type = 4;
        ULONG quality = 92;
        params.Parameter[0].Value = &quality;

        if (SaveBitmapSafely(rgbBmp, p->srcPath, outPath, tmpPath, &CLSID_JPEG, &params, p->overwrite, p->ctx)) {
            swprintf(p->ctx->statusText, 128, L"Saved JPG: %ls", PathFindFileNameW(p->overwrite ? p->srcPath : outPath));
        } else {
            swprintf(p->ctx->statusText, 128, L"Failed to save JPG");
        }
        GdipDeleteGraphics(gfx);
        GdipDisposeImage(rgbBmp);
    } else if (_wcsicmp(p->formatOrAction, L"BMP") == 0) {
        GetOutputPath(p->srcPath, L"converted", L".bmp", p->overwrite, outPath, tmpPath);
        p->ctx->jobProgress = 60;
        p->ctx->progressPercent = 60;
        if (SaveBitmapSafely(bmp, p->srcPath, outPath, tmpPath, &CLSID_BMP, NULL, p->overwrite, p->ctx)) {
            swprintf(p->ctx->statusText, 128, L"Saved BMP: %ls", PathFindFileNameW(p->overwrite ? p->srcPath : outPath));
        } else {
            swprintf(p->ctx->statusText, 128, L"Failed to save BMP");
        }
    } else if (_wcsicmp(p->formatOrAction, L"ICO") == 0) {
        GetOutputPath(p->srcPath, L"icon", L".ico", p->overwrite, outPath, tmpPath);
        p->ctx->jobProgress = 40;
        p->ctx->progressPercent = 40;
        swprintf(p->ctx->statusText, 128, L"Building multi-resolution ICO...");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

        // Standard Windows icon resolutions: 256, 48, 32, 16
        const int icoSizes[] = { 256, 48, 32, 16 };
        const int icoCount = 4;
        HGLOBAL streamsGlob[4] = {0};
        DWORD streamSizes[4] = {0};

        for (int i = 0; i < icoCount; i++) {
            int sz = icoSizes[i];
            GpBitmap *scaled = NULL;
            GdipCreateBitmapFromScan0(sz, sz, 0, PixelFormat32bppARGB, NULL, &scaled);
            GpGraphics *g = NULL;
            GdipGetImageGraphicsContext(scaled, &g);
            GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
            GdipDrawImageRectRectI(g, bmp, 0, 0, sz, sz, 0, 0, (int)w, (int)h, 2, NULL, NULL, NULL);

            IStream *strm = NULL;
            CreateStreamOnHGlobal(NULL, TRUE, &strm);
            GdipSaveImageToStream(scaled, strm, &CLSID_PNG, NULL);

            HGLOBAL hg = NULL;
            GetHGlobalFromStream(strm, &hg);
            STATSTG stg;
            strm->lpVtbl->Stat(strm, &stg, STATFLAG_NONAME);
            streamSizes[i] = (DWORD)stg.cbSize.QuadPart;
            streamsGlob[i] = hg;

            strm->lpVtbl->Release(strm);
            GdipDeleteGraphics(g);
            GdipDisposeImage(scaled);
        }

        const WCHAR *icoFile = p->overwrite ? tmpPath : outPath;
        HANDLE hIco = CreateFileW(icoFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hIco != INVALID_HANDLE_VALUE) {
            uint16_t idReserved = 0;
            uint16_t idType = 1; // ICO
            uint16_t idCount = (uint16_t)icoCount;
            DWORD bw;
            WriteFile(hIco, &idReserved, 2, &bw, NULL);
            WriteFile(hIco, &idType, 2, &bw, NULL);
            WriteFile(hIco, &idCount, 2, &bw, NULL);

            DWORD offset = 6 + icoCount * 16;
            for (int i = 0; i < icoCount; i++) {
                uint8_t bwByte = (uint8_t)((icoSizes[i] == 256) ? 0 : icoSizes[i]);
                uint8_t bhByte = bwByte;
                uint8_t bColorCount = 0;
                uint8_t bReserved = 0;
                uint16_t wPlanes = 1;
                uint16_t wBitCount = 32;
                DWORD dwBytes = streamSizes[i];
                DWORD dwOffset = offset;

                WriteFile(hIco, &bwByte, 1, &bw, NULL);
                WriteFile(hIco, &bhByte, 1, &bw, NULL);
                WriteFile(hIco, &bColorCount, 1, &bw, NULL);
                WriteFile(hIco, &bReserved, 1, &bw, NULL);
                WriteFile(hIco, &wPlanes, 2, &bw, NULL);
                WriteFile(hIco, &wBitCount, 2, &bw, NULL);
                WriteFile(hIco, &dwBytes, 4, &bw, NULL);
                WriteFile(hIco, &dwOffset, 4, &bw, NULL);

                offset += dwBytes;
            }

            for (int i = 0; i < icoCount; i++) {
                void *data = GlobalLock(streamsGlob[i]);
                if (data) {
                    WriteFile(hIco, data, streamSizes[i], &bw, NULL);
                    GlobalUnlock(streamsGlob[i]);
                }
            }
            CloseHandle(hIco);
            Magify_FinalizeOutputFile(p->srcPath, tmpPath, outPath, p->overwrite, p->ctx);
            swprintf(p->ctx->statusText, 128, L"Created ICO: %ls", PathFindFileNameW(p->overwrite ? p->srcPath : outPath));
        } else {
            p->ctx->isErrorState = TRUE;
            swprintf(p->ctx->statusText, 128, L"Failed to create ICO");
        }
    } else if (_wcsicmp(p->formatOrAction, L"PDF") == 0) {
        Magify_Image_FreeBitmap(bmp, srcPixels);
        free(p);
        return 0;
    }

    Magify_Image_FreeBitmap(bmp, srcPixels);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    free(p);
    return 0;
}

BOOL Magify_Image_Convert(const WCHAR *srcPath, const WCHAR *dstExt, MagifyContext *ctx) {
    if (_wcsicmp(dstExt, L"PDF") == 0) {
        WCHAR dir[MAX_PATH];
        wcsncpy(dir, srcPath, MAX_PATH - 1);
        dir[MAX_PATH - 1] = L'\0';
        PathRemoveFileSpecW(dir);

        WCHAR base[MAX_PATH];
        wcsncpy(base, PathFindFileNameW(srcPath), MAX_PATH - 1);
        base[MAX_PATH - 1] = L'\0';
        PathRemoveExtensionW(base);

        WCHAR outPdf[MAX_PATH];
        swprintf(outPdf, MAX_PATH, L"%ls\\%ls_magify.pdf", dir, base);
        return Magify_ImagesToPDF((const WCHAR**)ctx->files, ctx->fileCount, outPdf, ctx);
    }

    ImageWorkerArg *p = (ImageWorkerArg*)calloc(1, sizeof(ImageWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    wcsncpy(p->formatOrAction, dstExt, 31);
    p->overwrite = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)ConvertWorker, p);
    return TRUE;
}

static DWORD WINAPI RotateWorker(void *arg) {
    ImageWorkerArg *p = (ImageWorkerArg*)arg;
    Magify_Image_Init();

    p->ctx->jobProgress = 25;
    p->ctx->progressPercent = 25;
    swprintf(p->ctx->statusText, 128, L"Rotating %d°...", p->intParam);
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    void *srcPixels = NULL;
    GpBitmap *bmp = Magify_Image_LoadBitmap(p->srcPath, &srcPixels);
    if (!bmp) {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Failed to load image");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        free(p);
        return 0;
    }

    RotateFlipType rft = Rotate90FlipNone;
    if (p->intParam == 90) rft = Rotate90FlipNone;
    else if (p->intParam == 180) rft = Rotate180FlipNone;
    else if (p->intParam == 270) rft = Rotate270FlipNone;

    GdipImageRotateFlip(bmp, rft);
    p->ctx->jobProgress = 65;
    p->ctx->progressPercent = 65;

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    WCHAR suffix[32];
    swprintf(suffix, 32, L"rot%d", p->intParam);
    GetOutputPath(p->srcPath, suffix, NULL, p->overwrite, outPath, tmpPath);

    const WCHAR *ext = PathFindExtensionW(p->srcPath);
    const CLSID *clsid = &CLSID_PNG;
    if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) clsid = &CLSID_JPEG;
    else if (_wcsicmp(ext, L".bmp") == 0) clsid = &CLSID_BMP;

    if (SaveBitmapSafely(bmp, p->srcPath, outPath, tmpPath, clsid, NULL, p->overwrite, p->ctx)) {
        if (p->overwrite) {
            swprintf(p->ctx->statusText, 128, L"Rotated %d° (in-place)", p->intParam);
        } else {
            swprintf(p->ctx->statusText, 128, L"Rotated %d°: %ls", p->intParam, PathFindFileNameW(outPath));
        }
    } else {
        swprintf(p->ctx->statusText, 128, L"Failed to save rotated image");
    }
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    Magify_Image_FreeBitmap(bmp, srcPixels);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_Image_Rotate(const WCHAR *srcPath, int angleDegrees, MagifyContext *ctx) {
    ImageWorkerArg *p = (ImageWorkerArg*)calloc(1, sizeof(ImageWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    p->intParam = angleDegrees;
    p->overwrite = ctx->overwriteInPlace || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)RotateWorker, p);
    return TRUE;
}

static DWORD WINAPI FlipWorker(void *arg) {
    ImageWorkerArg *p = (ImageWorkerArg*)arg;
    Magify_Image_Init();

    p->ctx->jobProgress = 30;
    p->ctx->progressPercent = 30;
    swprintf(p->ctx->statusText, 128, L"Flipping image...");
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    void *srcPixels = NULL;
    GpBitmap *bmp = Magify_Image_LoadBitmap(p->srcPath, &srcPixels);
    if (!bmp) {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Failed to load image");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        free(p);
        return 0;
    }

    RotateFlipType rft = p->boolParam ? RotateNoneFlipX : RotateNoneFlipY;
    GdipImageRotateFlip(bmp, rft);
    p->ctx->jobProgress = 70;
    p->ctx->progressPercent = 70;

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    GetOutputPath(p->srcPath, p->boolParam ? L"fliph" : L"flipv", NULL, p->overwrite, outPath, tmpPath);

    const WCHAR *ext = PathFindExtensionW(p->srcPath);
    const CLSID *clsid = &CLSID_PNG;
    if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) clsid = &CLSID_JPEG;

    if (SaveBitmapSafely(bmp, p->srcPath, outPath, tmpPath, clsid, NULL, p->overwrite, p->ctx)) {
        if (p->overwrite) {
            swprintf(p->ctx->statusText, 128, L"Flipped %ls (in-place)",
                     p->boolParam ? L"Horizontal" : L"Vertical");
        } else {
            swprintf(p->ctx->statusText, 128, L"Flipped %ls: %ls",
                     p->boolParam ? L"Horizontal" : L"Vertical", PathFindFileNameW(outPath));
        }
    } else {
        swprintf(p->ctx->statusText, 128, L"Failed to flip image");
    }
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    Magify_Image_FreeBitmap(bmp, srcPixels);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_Image_Flip(const WCHAR *srcPath, BOOL horizontal, MagifyContext *ctx) {
    ImageWorkerArg *p = (ImageWorkerArg*)calloc(1, sizeof(ImageWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    p->boolParam = horizontal;
    p->overwrite = ctx->overwriteInPlace || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)FlipWorker, p);
    return TRUE;
}

static DWORD WINAPI ResizeWorker(void *arg) {
    ImageWorkerArg *p = (ImageWorkerArg*)arg;
    Magify_Image_Init();

    p->ctx->jobProgress = 20;
    p->ctx->progressPercent = 20;
    swprintf(p->ctx->statusText, 128, L"Resizing image...");
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    void *srcPixels = NULL;
    GpBitmap *src = Magify_Image_LoadBitmap(p->srcPath, &srcPixels);
    if (!src) {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Failed to load image");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        free(p);
        return 0;
    }

    UINT origW = 0, origH = 0;
    GdipGetImageWidth(src, &origW);
    GdipGetImageHeight(src, &origH);

    int newW = p->intParam;
    int newH = (int)origH;

    if (p->intParam == -50) { // 50%
        newW = (int)(origW / 2);
        newH = (int)(origH / 2);
    } else if (p->intParam == -1080) { // 1080p fit
        if (origW > origH) {
            newW = 1920;
            newH = (int)((float)origH * (1920.0f / (float)origW));
        } else {
            newH = 1080;
            newW = (int)((float)origW * (1080.0f / (float)origH));
        }
    }
    if (newW < 1) newW = 1;
    if (newH < 1) newH = 1;

    GpBitmap *dst = NULL;
    GdipCreateBitmapFromScan0(newW, newH, 0, PixelFormat32bppARGB, NULL, &dst);
    GpGraphics *g = NULL;
    GdipGetImageGraphicsContext(dst, &g);
    GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
    GdipDrawImageRectRectI(g, src, 0, 0, newW, newH, 0, 0, (int)origW, (int)origH, 2, NULL, NULL, NULL);

    p->ctx->jobProgress = 70;
    p->ctx->progressPercent = 70;

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    WCHAR sfx[32];
    swprintf(sfx, 32, L"%dx%d", newW, newH);
    GetOutputPath(p->srcPath, sfx, NULL, p->overwrite, outPath, tmpPath);

    const WCHAR *ext = PathFindExtensionW(p->srcPath);
    const CLSID *clsid = &CLSID_PNG;
    if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) clsid = &CLSID_JPEG;

    if (SaveBitmapSafely(dst, p->srcPath, outPath, tmpPath, clsid, NULL, p->overwrite, p->ctx)) {
        if (p->overwrite) {
            swprintf(p->ctx->statusText, 128, L"Resized to %dx%d (in-place)", newW, newH);
        } else {
            swprintf(p->ctx->statusText, 128, L"Resized to %dx%d px", newW, newH);
        }
    } else {
        swprintf(p->ctx->statusText, 128, L"Failed to save resized image");
    }
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    GdipDeleteGraphics(g);
    GdipDisposeImage(dst);
    Magify_Image_FreeBitmap(src, srcPixels);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_Image_Resize(const WCHAR *srcPath, int targetW, int targetH, int percent, MagifyContext *ctx) {
    (void)targetH;
    ImageWorkerArg *p = (ImageWorkerArg*)calloc(1, sizeof(ImageWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    if (percent == 50) p->intParam = -50;
    else if (targetW == 1080) p->intParam = -1080;
    else p->intParam = targetW;

    p->overwrite = ctx->overwriteInPlace || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)ResizeWorker, p);
    return TRUE;
}

BOOL Magify_CompressImageToTargetBytes(const WCHAR *srcPath, size_t maxBytes, MagifyContext *ctx) {
    Magify_Image_Init();

    void *srcPixels = NULL;
    GpBitmap *src = Magify_Image_LoadBitmap(srcPath, &srcPixels);
    if (!src) {
        swprintf(ctx->statusText, 128, L"Failed to open image");
        return FALSE;
    }

    UINT origW = 0, origH = 0;
    GdipGetImageWidth(src, &origW);
    GdipGetImageHeight(src, &origH);

    GpBitmap *curBmp = src;
    float scale = 1.0f;
    size_t bestSize = 0;
    int bestQ = 50;
    uint8_t *bestBuf = NULL;

    for (int iter = 0; iter < 4; iter++) {
        int lowQ = 15, highQ = 95;
        while (lowQ <= highQ) {
            int midQ = (lowQ + highQ) / 2;
            EncoderParameters params;
            params.Count = 1;
            params.Parameter[0].Guid = GUID_EncoderQuality;
            params.Parameter[0].NumberOfValues = 1;
            params.Parameter[0].Type = 4;
            ULONG qVal = (ULONG)midQ;
            params.Parameter[0].Value = &qVal;

            IStream *strm = NULL;
            CreateStreamOnHGlobal(NULL, TRUE, &strm);
            GdipSaveImageToStream(curBmp, strm, &CLSID_JPEG, &params);

            STATSTG stg;
            strm->lpVtbl->Stat(strm, &stg, STATFLAG_NONAME);
            size_t encLen = (size_t)stg.cbSize.QuadPart;

            if (encLen <= maxBytes) {
                bestQ = midQ;
                bestSize = encLen;
                if (bestBuf) free(bestBuf);
                bestBuf = (uint8_t*)malloc(bestSize);
                HGLOBAL hg = NULL;
                GetHGlobalFromStream(strm, &hg);
                void *data = GlobalLock(hg);
                if (data && bestBuf) memcpy(bestBuf, data, bestSize);
                GlobalUnlock(hg);
                lowQ = midQ + 1;
            } else {
                highQ = midQ - 1;
            }
            strm->lpVtbl->Release(strm);
        }

        if (bestSize > 0) break; // found a quality that fits!

        // Scale down dimensions by 0.85x and retry
        scale *= 0.85f;
        int newW = (int)((float)origW * scale);
        int newH = (int)((float)origH * scale);
        if (newW < 64 || newH < 64) break;

        GpBitmap *nextBmp = NULL;
        GdipCreateBitmapFromScan0(newW, newH, 0, PixelFormat24bppRGB, NULL, &nextBmp);
        GpGraphics *g = NULL;
        GdipGetImageGraphicsContext(nextBmp, &g);
        GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
        GdipDrawImageRectRectI(g, src, 0, 0, newW, newH, 0, 0, (int)origW, (int)origH, 2, NULL, NULL, NULL);
        GdipDeleteGraphics(g);

        if (curBmp != src) GdipDisposeImage(curBmp);
        curBmp = nextBmp;
    }

    if (bestSize == 0 || !bestBuf) {
        if (curBmp != src) GdipDisposeImage(curBmp);
        Magify_Image_FreeBitmap(src, srcPixels);
        if (bestBuf) free(bestBuf);
        swprintf(ctx->statusText, 128, L"Could not compress under target");
        return FALSE;
    }

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    WCHAR sfx[32];
    swprintf(sfx, 32, L"target_%luKB", (unsigned long)(maxBytes / 1024));
    BOOL overwrite = ctx->overwriteInPlace || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
    GetOutputPath(srcPath, sfx, L".jpg", overwrite, outPath, tmpPath);

    const WCHAR *saveFile = overwrite ? tmpPath : outPath;
    HANDLE hOut = CreateFileW(saveFile, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD bw = 0;
        WriteFile(hOut, bestBuf, (DWORD)bestSize, &bw, NULL);
        CloseHandle(hOut);
        Magify_FinalizeOutputFile(srcPath, tmpPath, outPath, overwrite, ctx);
        WCHAR szStr[32];
        Magify_FormatFileSize(bestSize, szStr, 32);
        swprintf(ctx->statusText, 128, L"Compressed to %ls (Q%d)", szStr, bestQ);
        wcsncpy(ctx->progressStatusText, ctx->statusText, 127);
    }
    free(bestBuf);

    if (curBmp != src) GdipDisposeImage(curBmp);
    Magify_Image_FreeBitmap(src, srcPixels);
    return TRUE;
}

static DWORD WINAPI CompressWorker(void *arg) {
    ImageWorkerArg *p = (ImageWorkerArg*)arg;
    Magify_Image_Init();

    uint64_t origBytes = Magify_GetFileSize(p->srcPath);

    if (p->intParam < 0) {
        size_t maxBytes = (size_t)(-p->intParam) * 1024;
        p->ctx->jobProgress = 40;
        p->ctx->progressPercent = 40;
        Magify_CompressImageToTargetBytes(p->srcPath, maxBytes, p->ctx);
        p->ctx->progressPercent = 100;
        p->ctx->jobProgress = 100;
        free(p);
        return 0;
    }

    p->ctx->jobProgress = 25;
    p->ctx->progressPercent = 25;
    swprintf(p->ctx->statusText, 128, L"Compressing (%d%%)...", p->intParam);
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    void *srcPixels = NULL;
    GpBitmap *bmp = Magify_Image_LoadBitmap(p->srcPath, &srcPixels);
    if (!bmp) {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Failed to load image");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        free(p);
        return 0;
    }

    EncoderParameters params;
    params.Count = 1;
    params.Parameter[0].Guid = GUID_EncoderQuality;
    params.Parameter[0].NumberOfValues = 1;
    params.Parameter[0].Type = 4;
    ULONG qVal = (ULONG)p->intParam;
    params.Parameter[0].Value = &qVal;

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    WCHAR sfx[32];
    swprintf(sfx, 32, L"q%d", p->intParam);
    GetOutputPath(p->srcPath, sfx, L".jpg", p->overwrite, outPath, tmpPath);

    p->ctx->jobProgress = 70;
    p->ctx->progressPercent = 70;
    if (SaveBitmapSafely(bmp, p->srcPath, outPath, tmpPath, &CLSID_JPEG, &params, p->overwrite, p->ctx)) {
        uint64_t newBytes = p->ctx->finalTotalBytes;
        Magify_FormatSizeDelta(origBytes, newBytes, p->ctx->progressStatusText, 128);
        wcsncpy(p->ctx->statusText, p->ctx->progressStatusText, 127);
    } else {
        swprintf(p->ctx->statusText, 128, L"Compression failed");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    }

    Magify_Image_FreeBitmap(bmp, srcPixels);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_Image_Compress(const WCHAR *srcPath, int qualityOrTarget, MagifyContext *ctx) {
    ImageWorkerArg *p = (ImageWorkerArg*)calloc(1, sizeof(ImageWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    p->intParam = qualityOrTarget;
    p->overwrite = ctx->overwriteInPlace || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)CompressWorker, p);
    return TRUE;
}

static DWORD WINAPI CropWorker(void *arg) {
    ImageWorkerArg *p = (ImageWorkerArg*)arg;
    Magify_Image_Init();

    p->ctx->jobProgress = 30;
    p->ctx->progressPercent = 30;
    swprintf(p->ctx->statusText, 128, L"Cropping image (%dx%d)...", p->cropW, p->cropH);
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    void *srcPixels = NULL;
    GpBitmap *src = Magify_Image_LoadBitmap(p->srcPath, &srcPixels);
    if (!src) {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Failed to load image");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        free(p);
        return 0;
    }

    GpBitmap *cropped = NULL;
    if (GdipCloneBitmapAreaI(p->cropX, p->cropY, p->cropW, p->cropH, PixelFormat32bppARGB, src, &cropped) != Ok || !cropped) {
        p->ctx->isErrorState = TRUE;
        swprintf(p->ctx->statusText, 128, L"Failed to crop region");
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
        Magify_Image_FreeBitmap(src, srcPixels);
        free(p);
        return 0;
    }

    p->ctx->jobProgress = 70;
    p->ctx->progressPercent = 70;

    WCHAR outPath[MAX_PATH], tmpPath[MAX_PATH];
    GetOutputPath(p->srcPath, L"cropped", NULL, p->overwrite, outPath, tmpPath);

    const WCHAR *ext = PathFindExtensionW(p->srcPath);
    const CLSID *clsid = &CLSID_PNG;
    if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) clsid = &CLSID_JPEG;

    if (SaveBitmapSafely(cropped, p->srcPath, outPath, tmpPath, clsid, NULL, p->overwrite, p->ctx)) {
        if (p->overwrite) {
            swprintf(p->ctx->statusText, 128, L"Cropped %dx%d (in-place)", p->cropW, p->cropH);
        } else {
            swprintf(p->ctx->statusText, 128, L"Cropped to %dx%d: %ls",
                     p->cropW, p->cropH, PathFindFileNameW(outPath));
        }
    } else {
        swprintf(p->ctx->statusText, 128, L"Failed to save cropped image");
    }
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

    GdipDisposeImage(cropped);
    Magify_Image_FreeBitmap(src, srcPixels);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    free(p);
    return 0;
}

BOOL Magify_CropImage(const WCHAR *srcPath, int cropX, int cropY, int cropW, int cropH, MagifyContext *ctx) {
    ImageWorkerArg *p = (ImageWorkerArg*)calloc(1, sizeof(ImageWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->srcPath, srcPath, MAX_PATH - 1);
    p->cropX = cropX;
    p->cropY = cropY;
    p->cropW = cropW;
    p->cropH = cropH;
    p->overwrite = ctx->overwriteInPlace || ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)CropWorker, p);
    return TRUE;
}

typedef struct {
    MagifyContext *ctx;
    WCHAR outPdfPath[MAX_PATH];
    int count;
    WCHAR files[MAGIFY_MAX_FILES][MAX_PATH];
} ImagesPdfWorkerArg;

static DWORD WINAPI ImagesToPDFWorker(void *arg) {
    ImagesPdfWorkerArg *p = (ImagesPdfWorkerArg*)arg;
    Magify_Image_Init();

    p->ctx->jobProgress = 15;
    swprintf(p->ctx->statusText, 128, L"Generating PDF document...");

    HANDLE hPdf = CreateFileW(p->outPdfPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hPdf == INVALID_HANDLE_VALUE) {
        swprintf(p->ctx->statusText, 128, L"Cannot create PDF file");
        free(p);
        return 0;
    }

    DWORD bw;
    // PDF Header
    const char *header = "%PDF-1.4\r\n%\xE2\xE3\xCF\xD3\r\n";
    WriteFile(hPdf, header, (DWORD)strlen(header), &bw, NULL);

    int objCount = 0;
    uint32_t offsets[256] = {0};

    // Object 1: Catalog
    offsets[++objCount] = SetFilePointer(hPdf, 0, NULL, FILE_CURRENT);
    char catBuf[128];
    sprintf(catBuf, "1 0 obj\r\n<</Type /Catalog /Pages 2 0 R>>\r\nendobj\r\n");
    WriteFile(hPdf, catBuf, (DWORD)strlen(catBuf), &bw, NULL);

    // Object 2: Pages root
    offsets[++objCount] = SetFilePointer(hPdf, 0, NULL, FILE_CURRENT);
    char pagesBuf[512];
    int pos = sprintf(pagesBuf, "2 0 obj\r\n<</Type /Pages /Count %d /Kids [", p->count);
    for (int i = 0; i < p->count; i++) {
        pos += sprintf(pagesBuf + pos, "%d 0 R ", 3 + i * 3);
    }
    sprintf(pagesBuf + pos, "]>>\r\nendobj\r\n");
    WriteFile(hPdf, pagesBuf, (DWORD)strlen(pagesBuf), &bw, NULL);

    // Page, Content, and Image objects for each file
    for (int i = 0; i < p->count; i++) {
        if (p->ctx->cancelRequested) {
            CloseHandle(hPdf);
            DeleteFileW(p->outPdfPath);
            free(p);
            return 0;
        }

        int pct = 20 + (int)(((float)(i + 1) / (float)p->count) * 60.0f);
        p->ctx->jobProgress = pct;
        p->ctx->progressPercent = pct;
        swprintf(p->ctx->statusText, 128, L"Processing image %d of %d...", i + 1, p->count);
        wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);

        void *imgPixels = NULL;
        GpBitmap *bmp = Magify_Image_LoadBitmap(p->files[i], &imgPixels);
        if (!bmp) continue;

        UINT imgW = 0, imgH = 0;
        GdipGetImageWidth(bmp, &imgW);
        GdipGetImageHeight(bmp, &imgH);

        IStream *strm = NULL;
        CreateStreamOnHGlobal(NULL, TRUE, &strm);
        EncoderParameters params;
        params.Count = 1;
        params.Parameter[0].Guid = GUID_EncoderQuality;
        params.Parameter[0].NumberOfValues = 1;
        params.Parameter[0].Type = 4;
        ULONG q = 90;
        params.Parameter[0].Value = &q;
        GdipSaveImageToStream(bmp, strm, &CLSID_JPEG, &params);

        STATSTG stg;
        strm->lpVtbl->Stat(strm, &stg, STATFLAG_NONAME);
        DWORD jpegSize = (DWORD)stg.cbSize.QuadPart;
        HGLOBAL hg = NULL;
        GetHGlobalFromStream(strm, &hg);
        void *jpegData = GlobalLock(hg);

        int pageObjId = 3 + i * 3;
        int contentObjId = pageObjId + 1;
        int imageObjId = pageObjId + 2;

        // A4 page: 595 x 842 pt
        float pageW = 595.0f, pageH = 842.0f;
        float margin = 20.0f;
        float availW = pageW - margin * 2.0f;
        float availH = pageH - margin * 2.0f;

        float sX = availW / (float)imgW;
        float sY = availH / (float)imgH;
        float s = (sX < sY) ? sX : sY;
        float drawW = (float)imgW * s;
        float drawH = (float)imgH * s;
        float drawX = margin + (availW - drawW) * 0.5f;
        float drawY = margin + (availH - drawH) * 0.5f;

        // 1. Page Object
        offsets[++objCount] = SetFilePointer(hPdf, 0, NULL, FILE_CURRENT);
        char pObjBuf[256];
        sprintf(pObjBuf, "%d 0 obj\r\n<</Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] "
                         "/Contents %d 0 R /Resources <</XObject <</Im%d %d 0 R>>>>>>\r\nendobj\r\n",
                pageObjId, contentObjId, i, imageObjId);
        WriteFile(hPdf, pObjBuf, (DWORD)strlen(pObjBuf), &bw, NULL);

        // 2. Content Object
        char contentData[256];
        int cLen = sprintf(contentData, "q %.2f 0 0 %.2f %.2f %.2f cm /Im%d Do Q\r\n",
                           drawW, drawH, drawX, drawY, i);

        offsets[++objCount] = SetFilePointer(hPdf, 0, NULL, FILE_CURRENT);
        char cObjBuf[128];
        sprintf(cObjBuf, "%d 0 obj\r\n<</Length %d>>\r\nstream\r\n", contentObjId, cLen);
        WriteFile(hPdf, cObjBuf, (DWORD)strlen(cObjBuf), &bw, NULL);
        WriteFile(hPdf, contentData, cLen, &bw, NULL);
        const char *endStream = "endstream\r\nendobj\r\n";
        WriteFile(hPdf, endStream, (DWORD)strlen(endStream), &bw, NULL);

        // 3. Image XObject
        offsets[++objCount] = SetFilePointer(hPdf, 0, NULL, FILE_CURRENT);
        char imgObjBuf[256];
        sprintf(imgObjBuf, "%d 0 obj\r\n<</Type /XObject /Subtype /Image /Width %u /Height %u "
                           "/ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length %lu>>\r\nstream\r\n",
                imageObjId, imgW, imgH, (unsigned long)jpegSize);
        WriteFile(hPdf, imgObjBuf, (DWORD)strlen(imgObjBuf), &bw, NULL);
        WriteFile(hPdf, jpegData, jpegSize, &bw, NULL);
        WriteFile(hPdf, "\r\nendstream\r\nendobj\r\n", 23, &bw, NULL);

        GlobalUnlock(hg);
        strm->lpVtbl->Release(strm);
        Magify_Image_FreeBitmap(bmp, imgPixels);
    }

    // XREF Table
    uint32_t xrefOffset = SetFilePointer(hPdf, 0, NULL, FILE_CURRENT);
    char xrefHdr[64];
    sprintf(xrefHdr, "xref\r\n0 %d\r\n0000000000 65535 f \r\n", objCount + 1);
    WriteFile(hPdf, xrefHdr, (DWORD)strlen(xrefHdr), &bw, NULL);

    for (int i = 1; i <= objCount; i++) {
        char line[32];
        sprintf(line, "%010u 00000 n \r\n", offsets[i]);
        WriteFile(hPdf, line, (DWORD)strlen(line), &bw, NULL);
    }

    char trailer[256];
    sprintf(trailer, "trailer\r\n<</Size %d /Root 1 0 R>>\r\nstartxref\r\n%u\r\n%%%%EOF\r\n",
            objCount + 1, xrefOffset);
    WriteFile(hPdf, trailer, (DWORD)strlen(trailer), &bw, NULL);

    CloseHandle(hPdf);

    p->ctx->finalTotalBytes = Magify_GetFileSize(p->outPdfPath);
    p->ctx->progressPercent = 100;
    p->ctx->jobProgress = 100;
    swprintf(p->ctx->statusText, 128, L"Created PDF: %ls", PathFindFileNameW(p->outPdfPath));
    wcsncpy(p->ctx->progressStatusText, p->ctx->statusText, 127);
    free(p);
    return 0;
}

BOOL Magify_ImagesToPDF(const WCHAR **srcFiles, int fileCount, const WCHAR *outPdfPath, MagifyContext *ctx) {
    if (!srcFiles || fileCount <= 0 || !outPdfPath) return FALSE;

    ImagesPdfWorkerArg *p = (ImagesPdfWorkerArg*)calloc(1, sizeof(ImagesPdfWorkerArg));
    if (!p) return FALSE;
    p->ctx = ctx;
    wcsncpy(p->outPdfPath, outPdfPath, MAX_PATH - 1);
    p->count = (fileCount > MAGIFY_MAX_FILES) ? MAGIFY_MAX_FILES : fileCount;
    for (int i = 0; i < p->count; i++) {
        wcsncpy(p->files[i], srcFiles[i], MAX_PATH - 1);
    }

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)ImagesToPDFWorker, p);
    return TRUE;
}

BOOL Magify_GeneratePreview(const WCHAR *srcPath, int targetW, int targetH,
                            uint32_t *outPixels, int *actualW, int *actualH) {
    Magify_Image_Init();

    void *srcPixels = NULL;
    GpBitmap *src = Magify_Image_LoadBitmap(srcPath, &srcPixels);
    if (!src) return FALSE;

    UINT origW = 0, origH = 0;
    GdipGetImageWidth(src, &origW);
    GdipGetImageHeight(src, &origH);

    float sX = (float)targetW / (float)origW;
    float sY = (float)targetH / (float)origH;
    float s = (sX < sY) ? sX : sY;
    int fitW = (int)((float)origW * s);
    int fitH = (int)((float)origH * s);
    if (fitW < 1) fitW = 1;
    if (fitH < 1) fitH = 1;

    *actualW = fitW;
    *actualH = fitH;

    GpBitmap *scaled = NULL;
    GdipCreateBitmapFromScan0(fitW, fitH, fitW * 4, PixelFormat32bppARGB, (BYTE*)outPixels, &scaled);
    GpGraphics *g = NULL;
    GdipGetImageGraphicsContext(scaled, &g);
    GdipSetInterpolationMode(g, InterpolationModeHighQualityBicubic);
    GdipDrawImageRectRectI(g, src, 0, 0, fitW, fitH, 0, 0, (int)origW, (int)origH, 2, NULL, NULL, NULL);

    GdipDeleteGraphics(g);
    GdipDisposeImage(scaled);
    Magify_Image_FreeBitmap(src, srcPixels);
    return TRUE;
}
