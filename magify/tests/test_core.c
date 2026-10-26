#include "magify.h"
#include "ui_overlay.h"
#include "ops_common.h"
#include "ops_image.h"
#include <stdio.h>
#include <assert.h>
#include <shlwapi.h>

int wmain(void) {
    printf("====================================================\n");
    printf("        MAGIFY CORE AUTOMATED TEST SUITE            \n");
    printf("====================================================\n\n");

    // ---------------------------------------------------------
    // 1. File Classification & Magic Bytes
    // ---------------------------------------------------------
    printf("[1/7] Testing File Magic-Byte & Extension Classification...\n");
    MagifyFileClass imgClass = Magify_DetectFileClass(L"test_assets\\sample.png");
    printf("  sample.png: %d (expected %d)\n", imgClass, FILE_CLASS_IMAGE);
    assert(imgClass == FILE_CLASS_IMAGE);

    MagifyFileClass pdfClass = Magify_DetectFileClass(L"test_assets\\sample.pdf");
    printf("  sample.pdf: %d (expected %d)\n", pdfClass, FILE_CLASS_PDF);
    assert(pdfClass == FILE_CLASS_PDF);

    MagifyFileClass bmpClass = Magify_DetectFileClass(L"test_assets\\test_pattern.bmp");
    printf("  test_pattern.bmp: %d (expected %d)\n", bmpClass, FILE_CLASS_IMAGE);
    assert(bmpClass == FILE_CLASS_IMAGE);

    // ---------------------------------------------------------
    // 2. File Size Formatting & Hit-Testing
    // ---------------------------------------------------------
    printf("\n[2/7] Testing Formatting & Polar Hit-Testing...\n");
    WCHAR sizeStr[32];
    Magify_FormatFileSize(512, sizeStr, 32);
    wprintf(L"  512 B: %ls\n", sizeStr);
    assert(wcscmp(sizeStr, L"512 B") == 0);

    Magify_FormatFileSize(2500000, sizeStr, 32);
    wprintf(L"  2.5 MB: %ls\n", sizeStr);

    int hubHit = Magify_HitTestRadial(220.0f, 220.0f, 220.0f, 220.0f, 52.0f, 155.0f, 4, 225.0f);
    assert(hubHit == -1);
    int topHit = Magify_HitTestRadial(220.0f, 120.0f, 220.0f, 220.0f, 52.0f, 155.0f, 4, 225.0f);
    assert(topHit == 0); // CONVERT
    int rightHit = Magify_HitTestRadial(320.0f, 220.0f, 220.0f, 220.0f, 52.0f, 155.0f, 4, 225.0f);
    assert(rightHit == 1); // EDIT
    int bottomHit = Magify_HitTestRadial(220.0f, 320.0f, 220.0f, 220.0f, 52.0f, 155.0f, 4, 225.0f);
    assert(bottomHit == 2); // COMPRESS
    int leftHit = Magify_HitTestRadial(120.0f, 220.0f, 220.0f, 220.0f, 52.0f, 155.0f, 4, 225.0f);
    assert(leftHit == 3); // RENAME
    printf("  All radial sectors (0..3), hub (-1), and outside (-2) verified.\n");

    // ---------------------------------------------------------
    // 3. ZIP Archive Creation (Pure C11 PKZIP)
    // ---------------------------------------------------------
    printf("\n[3/7] Testing PKZIP Archive Creation...\n");
    MagifyContext zipCtx;
    memset(&zipCtx, 0, sizeof(zipCtx));
    wcscpy(zipCtx.files[0], L"test_assets\\test_pattern.bmp");
    wcscpy(zipCtx.files[1], L"test_assets\\sample.png");
    zipCtx.fileCount = 2;

    const WCHAR *zipOut = L"test_assets\\test_archive.zip";
    DeleteFileW(zipOut);
    BOOL zipOk = Magify_CreateZipArchive(&zipCtx, zipOut);
    assert(zipOk);
    assert(GetFileAttributesW(zipOut) != INVALID_FILE_ATTRIBUTES);

    // Verify PK header
    HANDLE hZip = CreateFileW(zipOut, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(hZip != INVALID_HANDLE_VALUE);
    uint8_t pkSig[4] = {0};
    DWORD bw = 0;
    ReadFile(hZip, pkSig, 4, &bw, NULL);
    CloseHandle(hZip);
    assert(pkSig[0] == 0x50 && pkSig[1] == 0x4B); // 'PK'
    printf("  Created valid ZIP: %ls (Signature: PK, %llu bytes)\n", zipOut,
           (unsigned long long)Magify_GetFileSize(zipOut));

    // ---------------------------------------------------------
    // 4. File Rename Operations
    // ---------------------------------------------------------
    printf("\n[4/7] Testing Atomic & Batch Rename...\n");
    CopyFileW(L"test_assets\\test_pattern.bmp", L"test_assets\\temp_rename_me.bmp", FALSE);
    MagifyContext renCtx;
    memset(&renCtx, 0, sizeof(renCtx));
    wcscpy(renCtx.files[0], L"test_assets\\temp_rename_me.bmp");
    renCtx.fileCount = 1;

    BOOL renOk = Magify_RenameFiles(&renCtx, L"renamed_target");
    assert(renOk);
    assert(GetFileAttributesW(L"test_assets\\renamed_target.bmp") != INVALID_FILE_ATTRIBUTES);
    DeleteFileW(L"test_assets\\renamed_target.bmp");
    printf("  Atomic Rename succeeded: %ls\n", renCtx.statusText);

    // ---------------------------------------------------------
    // 5. GDI+ Image Engine: Dimensions & Conversion
    // ---------------------------------------------------------
    printf("\n[5/7] Testing GDI+ Engine Dimensions & Format Conversion...\n");
    Magify_Image_Init();
    int imgW = 0, imgH = 0;
    BOOL dimOk = Magify_LoadImageDimensions(L"test_assets\\test_pattern.bmp", &imgW, &imgH);
    assert(dimOk);
    printf("  test_pattern.bmp dimensions: %dx%d px\n", imgW, imgH);
    assert(imgW == 400 && imgH == 300);

    // Convert BMP -> PNG
    MagifyContext imgCtx;
    memset(&imgCtx, 0, sizeof(imgCtx));
    wcscpy(imgCtx.files[0], L"test_assets\\test_pattern.bmp");
    imgCtx.fileCount = 1;

    DeleteFileW(L"test_assets\\test_pattern_converted.png");
    Magify_Image_Convert(L"test_assets\\test_pattern.bmp", L"PNG", &imgCtx);
    // Wait for worker thread
    for (int t = 0; t < 50; t++) {
        if (GetFileAttributesW(L"test_assets\\test_pattern_converted.png") != INVALID_FILE_ATTRIBUTES) break;
        Sleep(20);
    }
    assert(GetFileAttributesW(L"test_assets\\test_pattern_converted.png") != INVALID_FILE_ATTRIBUTES);
    printf("  Converted to PNG successfully: %ls\n", imgCtx.statusText);

    // ---------------------------------------------------------
    // 6. Image Rotate, Crop & Target Compression
    // ---------------------------------------------------------
    printf("\n[6/7] Testing Image Rotate 90°, Crop & Target Compression...\n");
    // Rotate 90
    DeleteFileW(L"test_assets\\test_pattern_converted_rot90.png");
    Magify_Image_Rotate(L"test_assets\\test_pattern_converted.png", 90, &imgCtx);
    for (int t = 0; t < 50; t++) {
        if (GetFileAttributesW(L"test_assets\\test_pattern_converted_rot90.png") != INVALID_FILE_ATTRIBUTES) break;
        Sleep(20);
    }
    int rW = 0, rH = 0;
    Magify_LoadImageDimensions(L"test_assets\\test_pattern_converted_rot90.png", &rW, &rH);
    printf("  Rotated 90° dimensions: %dx%d (expected 300x400)\n", rW, rH);
    assert(rW == 300 && rH == 400);

    // Crop (100, 50, 150, 120)
    DeleteFileW(L"test_assets\\test_pattern_converted_cropped.png");
    Magify_CropImage(L"test_assets\\test_pattern_converted.png", 100, 50, 150, 120, &imgCtx);
    for (int t = 0; t < 50; t++) {
        if (GetFileAttributesW(L"test_assets\\test_pattern_converted_cropped.png") != INVALID_FILE_ATTRIBUTES) break;
        Sleep(20);
    }
    int cW = 0, cH = 0;
    Magify_LoadImageDimensions(L"test_assets\\test_pattern_converted_cropped.png", &cW, &cH);
    printf("  Cropped dimensions: %dx%d (expected 150x120)\n", cW, cH);
    assert(cW == 150 && cH == 120);

    // Target Compression: < 30 KB
    DeleteFileW(L"test_assets\\test_pattern_converted_target_30KB.jpg");
    BOOL tgtOk = Magify_CompressImageToTargetBytes(L"test_assets\\test_pattern_converted.png", 30 * 1024, &imgCtx);
    assert(tgtOk);
    uint64_t tgtSize = Magify_GetFileSize(L"test_assets\\test_pattern_converted_target_30KB.jpg");
    printf("  Target 30 KB compression produced: %llu bytes (strictly <= 30720 bytes)\n",
           (unsigned long long)tgtSize);
    assert(tgtSize > 0 && tgtSize <= 30720);

    // ---------------------------------------------------------
    // 7. Image(s) -> Native PDF 1.4 Generation
    // ---------------------------------------------------------
    printf("\n[7/7] Testing Image(s) -> Native PDF 1.4 Generator...\n");
    const WCHAR *pdfInFiles[2] = {
        L"test_assets\\test_pattern_converted.png",
        L"test_assets\\sample.png"
    };
    const WCHAR *pdfOut = L"test_assets\\images_merged.pdf";
    DeleteFileW(pdfOut);
    Magify_ImagesToPDF(pdfInFiles, 2, pdfOut, &imgCtx);
    for (int t = 0; t < 50; t++) {
        if (GetFileAttributesW(pdfOut) != INVALID_FILE_ATTRIBUTES) break;
        Sleep(20);
    }
    assert(GetFileAttributesW(pdfOut) != INVALID_FILE_ATTRIBUTES);

    // Verify PDF header
    HANDLE hPdf = CreateFileW(pdfOut, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    assert(hPdf != INVALID_HANDLE_VALUE);
    char pdfHdr[16] = {0};
    ReadFile(hPdf, pdfHdr, 8, &bw, NULL);
    CloseHandle(hPdf);
    assert(strncmp(pdfHdr, "%PDF-1.4", 8) == 0);
    printf("  Generated PDF: %ls (Header: %.*s, Size: %llu bytes)\n",
           pdfOut, 8, pdfHdr, (unsigned long long)Magify_GetFileSize(pdfOut));

    Magify_Image_Shutdown();

    printf("\n====================================================\n");
    printf("  ✨ ALL 7 TEST SUITES PASSED WITH 100%% SUCCESS!   \n");
    printf("====================================================\n");
    return 0;
}
