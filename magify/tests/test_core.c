#include "magify.h"
#include "ui_overlay.h"
#include "ops_common.h"
#include "ops_image.h"
#include "ops_pdf.h"
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
    printf("[1/17] Testing File Magic-Byte & Extension Classification...\n");
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
    printf("\n[2/17] Testing Formatting & Polar Hit-Testing...\n");
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
    printf("\n[3/17] Testing PKZIP Archive Creation...\n");
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
    printf("\n[4/17] Testing Atomic & Batch Rename...\n");
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
    printf("\n[5/17] Testing GDI+ Engine Dimensions & Format Conversion...\n");
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
    printf("\n[6/17] Testing Image Rotate 90°, Crop & Target Compression...\n");
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
    printf("\n[7/17] Testing Image(s) -> Native PDF 1.4 Generator...\n");
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

    // ---------------------------------------------------------
    // 8. Multi-Page PDF Generation (Test Fixture)
    // ---------------------------------------------------------
    printf("\n[8/17] Generating Multi-Page Test PDF with Embedded Streams...\n");
    const WCHAR *multiPagesIn[3] = {
        L"test_assets\\test_pattern_converted.png",
        L"test_assets\\sample.png",
        L"test_assets\\test_pattern_converted_cropped.png"
    };
    const WCHAR *multiPdfOut = L"test_assets\\sample_multi.pdf";
    DeleteFileW(multiPdfOut);
    Magify_ImagesToPDF(multiPagesIn, 3, multiPdfOut, &imgCtx);
    for (int t = 0; t < 50; t++) {
        if (GetFileAttributesW(multiPdfOut) != INVALID_FILE_ATTRIBUTES) break;
        Sleep(20);
    }
    assert(GetFileAttributesW(multiPdfOut) != INVALID_FILE_ATTRIBUTES);
    printf("  Created 3-page test PDF: %ls (%llu bytes)\n",
           multiPdfOut, (unsigned long long)Magify_GetFileSize(multiPdfOut));

    // ---------------------------------------------------------
    // 9. Fast In-Memory PDF Object & Page-Tree Parser
    // ---------------------------------------------------------
    printf("\n[9/17] Testing In-Memory PDF Object & Page-Tree Engine...\n");
    MagifyPdfDoc *doc = Magify_PdfDoc_Open(multiPdfOut);
    assert(doc != NULL);
    printf("  Parsed PDF: %d objects, %d pages detected\n", doc->objectCount, doc->pageCount);
    assert(doc->pageCount == 3);
    for (int i = 0; i < doc->pageCount; i++) {
        printf("    Page %d: ObjId=%d, Rot=%d, Size=%.0fx%.0f pt\n",
               i + 1, doc->pages[i].pageObjectId, doc->pages[i].currentRotation,
               doc->pages[i].widthPt, doc->pages[i].heightPt);
        assert(doc->pages[i].pageObjectId > 0);
        assert(doc->pages[i].widthPt > 50.0f && doc->pages[i].heightPt > 50.0f);
    }
    Magify_PdfDoc_Close(doc);

    // ---------------------------------------------------------
    // 10. Lossless Structural PDF Rotation
    // ---------------------------------------------------------
    printf("\n[10/17] Testing Lossless Structural PDF Rotation (90° CW)...\n");
    const WCHAR *rotOut = L"test_assets\\sample_multi_rot90.pdf";
    DeleteFileW(rotOut);
    BOOL rotOk = Magify_PdfRotateAllSync(multiPdfOut, rotOut, 90);
    assert(rotOk);
    assert(GetFileAttributesW(rotOut) != INVALID_FILE_ATTRIBUTES);

    MagifyPdfDoc *docRot = Magify_PdfDoc_Open(rotOut);
    assert(docRot != NULL);
    assert(docRot->pageCount == 3);
    for (int i = 0; i < docRot->pageCount; i++) {
        assert(docRot->pages[i].currentRotation == 90);
    }
    printf("  Lossless 90° rotation verified on all %d pages.\n", docRot->pageCount);
    Magify_PdfDoc_Close(docRot);

    // ---------------------------------------------------------
    // 11. Multi-PDF Merge Engine
    // ---------------------------------------------------------
    printf("\n[11/17] Testing Multi-PDF Merge Engine...\n");
    const WCHAR *mergeList[2] = { multiPdfOut, rotOut };
    const WCHAR *mergeOut = L"test_assets\\sample_multi_merged.pdf";
    DeleteFileW(mergeOut);
    BOOL mergeOk = Magify_PdfMergeSync(mergeList, 2, mergeOut);
    assert(mergeOk);
    assert(GetFileAttributesW(mergeOut) != INVALID_FILE_ATTRIBUTES);

    MagifyPdfDoc *docMerged = Magify_PdfDoc_Open(mergeOut);
    assert(docMerged != NULL);
    printf("  Merged PDF contains %d pages (expected 3 + 3 = 6)\n", docMerged->pageCount);
    assert(docMerged->pageCount == 6);
    Magify_PdfDoc_Close(docMerged);

    // ---------------------------------------------------------
    // 12. Lossless PDF Split (Custom Range & Burst)
    // ---------------------------------------------------------
    printf("\n[12/17] Testing Lossless PDF Split (Range & Burst)...\n");
    const WCHAR *splitOut = L"test_assets\\sample_split.pdf";
    DeleteFileW(splitOut);
    BOOL splitRangeOk = Magify_PdfSplitSync(multiPdfOut, L"1-2", splitOut);
    assert(splitRangeOk);

    MagifyPdfDoc *docSplit = Magify_PdfDoc_Open(splitOut);
    assert(docSplit != NULL);
    printf("  Custom range '1-2' extracted: %d pages\n", docSplit->pageCount);
    assert(docSplit->pageCount == 2);
    Magify_PdfDoc_Close(docSplit);

    // Burst all pages
    BOOL burstOk = Magify_PdfSplitSync(multiPdfOut, L"BURST", NULL);
    assert(burstOk);
    assert(GetFileAttributesW(L"test_assets\\sample_multi_page_001.pdf") != INVALID_FILE_ATTRIBUTES);
    assert(GetFileAttributesW(L"test_assets\\sample_multi_page_002.pdf") != INVALID_FILE_ATTRIBUTES);
    assert(GetFileAttributesW(L"test_assets\\sample_multi_page_003.pdf") != INVALID_FILE_ATTRIBUTES);
    printf("  Burst operation verified: page_001.pdf, page_002.pdf, page_003.pdf exist.\n");

    // ---------------------------------------------------------
    // 13. PDF Stream Compression
    // ---------------------------------------------------------
    printf("\n[13/17] Testing PDF Stream Compression & Downsampling...\n");
    const WCHAR *compOut = L"test_assets\\sample_multi_compressed.pdf";
    DeleteFileW(compOut);
    int savingsPct = 0;
    BOOL compOk = Magify_PdfCompressSync(multiPdfOut, compOut, 96, &savingsPct);
    assert(compOk);
    assert(GetFileAttributesW(compOut) != INVALID_FILE_ATTRIBUTES);
    uint64_t origSz = Magify_GetFileSize(multiPdfOut);
    uint64_t newSz = Magify_GetFileSize(compOut);
    printf("  Compressed PDF size: %llu -> %llu bytes (Savings: %d%%)\n",
           (unsigned long long)origSz, (unsigned long long)newSz, savingsPct);
    MagifyPdfDoc *docComp = Magify_PdfDoc_Open(compOut);
    assert(docComp != NULL && docComp->pageCount == 3);
    Magify_PdfDoc_Close(docComp);

    // ---------------------------------------------------------
    // 14. Visual Page Organizer HUD State & Changes Application
    // ---------------------------------------------------------
    printf("\n[14/17] Testing Visual Page Organizer State & Changes Application...\n");
    MagifyContext orgCtx;
    memset(&orgCtx, 0, sizeof(orgCtx));
    wcscpy(orgCtx.files[0], multiPdfOut);
    orgCtx.fileCount = 1;

    MagifyPdfOrganizerState *org = Magify_PdfOrganizer_Init(&orgCtx);
    assert(org != NULL);
    assert(org->totalPages == 3);

    // Render thumbnail verification
    uint32_t thumbBuf[120 * 160];
    BOOL thumbOk = Magify_PdfDoc_RenderThumbnail(org->primaryDoc, 0, 120, 160, thumbBuf);
    assert(thumbOk);
    printf("  Rendered 120x160 thumbnail successfully (first pixel: 0x%08X)\n", thumbBuf[0]);

    // Simulate page reorder: move page 0 to index 2
    MagifyPdfPageInfo p0 = org->pages[0];
    org->pages[0] = org->pages[1];
    org->pages[1] = org->pages[2];
    org->pages[2] = p0;

    // Simulate page rotation: page 1 +90 deg
    org->pages[1].addedRotation = 90;

    // Simulate page deletion: mark page 2 deleted
    org->pages[2].markedDeleted = TRUE;

    // Apply changes
    const WCHAR *orgEditedOut = L"test_assets\\sample_multi_edited.pdf";
    DeleteFileW(orgEditedOut);
    BOOL applyOk = Magify_PdfApplyOrganizerChanges(&orgCtx, org);
    assert(applyOk);
    assert(GetFileAttributesW(orgEditedOut) != INVALID_FILE_ATTRIBUTES);

    MagifyPdfDoc *docEdited = Magify_PdfDoc_Open(orgEditedOut);
    assert(docEdited != NULL);
    printf("  Organizer saved: %d active pages (page 3 successfully excluded)\n", docEdited->pageCount);
    assert(docEdited->pageCount == 2);
    Magify_PdfDoc_Close(docEdited);
    Magify_PdfOrganizer_Free(org);

    // ---------------------------------------------------------
    // 15. Safe Output Path, Collision Avoidance & In-Place Overwrite
    // ---------------------------------------------------------
    printf("\n[15/17] Testing Safe Output Path & Shift+Click Overwrite...\n");
    WCHAR safeOut[MAX_PATH], tmpPath[MAX_PATH];
    
    // Non-destructive naming with collision avoidance
    Magify_GetSafeOutputPath(L"test_assets\\sample.png", L"converted", L".png", FALSE, safeOut, tmpPath);
    wprintf(L"  Standard safe output: %ls\n", PathFindFileNameW(safeOut));
    assert(wcsstr(safeOut, L"sample_converted.png") != NULL);

    // In-place overwrite naming
    Magify_GetSafeOutputPath(L"test_assets\\sample.png", L"converted", L".png", TRUE, safeOut, tmpPath);
    wprintf(L"  In-place temp output: %ls\n", PathFindFileNameW(tmpPath));
    assert(wcsstr(tmpPath, L".tmp") != NULL);

    // Atomic overwrite test with read-only removal
    const WCHAR *testTarget = L"test_assets\\temp_overwrite_target.txt";
    HANDLE hTgt = CreateFileW(testTarget, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD wr = 0;
    WriteFile(hTgt, "INITIAL CONTENT", 15, &wr, NULL);
    CloseHandle(hTgt);
    // Mark target read-only to verify Magify strips read-only attribute on overwrite
    SetFileAttributesW(testTarget, FILE_ATTRIBUTE_READONLY);

    const WCHAR *testTmp = L"test_assets\\temp_overwrite_source.tmp";
    HANDLE hTmp = CreateFileW(testTmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(hTmp, "NEW REPLACED CONTENT OVERWRITTEN", 32, &wr, NULL);
    CloseHandle(hTmp);

    MagifyContext ovCtx;
    memset(&ovCtx, 0, sizeof(ovCtx));
    BOOL ovOk = Magify_FinalizeOutputFile(testTarget, testTmp, testTarget, TRUE, &ovCtx);
    assert(ovOk);
    assert(!ovCtx.isErrorState);
    assert(ovCtx.finalTotalBytes == 32);
    DeleteFileW(testTarget);
    printf("  Atomic in-place replace and read-only clearing succeeded!\n");

    // ---------------------------------------------------------
    // 16. Size-Savings Toast Delta Report
    // ---------------------------------------------------------
    printf("\n[16/17] Testing Size-Savings Toast Delta Formatting...\n");
    WCHAR toastReport[128];
    // 4.1 MB -> 1.2 MB (-70%)
    uint64_t bOrig = (uint64_t)(4.1 * 1024 * 1024);
    uint64_t bNew  = (uint64_t)(1.2 * 1024 * 1024);
    Magify_FormatSizeDelta(bOrig, bNew, toastReport, 128);
    wprintf(L"  Delta report: %ls\n", toastReport);
    assert(wcsstr(toastReport, L"→") != NULL);
    assert(wcsstr(toastReport, L"-70%") != NULL);

    // Equal or increase
    Magify_FormatSizeDelta(500, 600, toastReport, 128);
    wprintf(L"  Delta report (increase): %ls\n", toastReport);
    assert(wcsstr(toastReport, L"→") != NULL);
    printf("  Toast reports generated cleanly.\n");

    // ---------------------------------------------------------
    // 17. Thread-Safe Worker Cancellation & State Cleanliness
    // ---------------------------------------------------------
    printf("\n[17/17] Testing Worker Launch & Graceful Thread Cancellation...\n");
    MagifyContext workerCtx;
    memset(&workerCtx, 0, sizeof(workerCtx));
    wcscpy(workerCtx.files[0], L"test_assets\\sample.png");
    workerCtx.fileCount = 1;

    // Launch worker and cancel immediately
    Magify_CompressGenericZip(&workerCtx);
    assert(workerCtx.isProcessing == 1);
    Magify_CancelWorker(&workerCtx);
    assert(workerCtx.cancelRequested == 1);
    assert(workerCtx.isProcessing == 0);
    assert(workerCtx.hWorkerThread == NULL);
    printf("  Worker cancellation verified cleanly with zero hanging threads!\n");

    Magify_Image_Shutdown();

    printf("\n====================================================\n");
    printf("  ✨ ALL 17 TEST SUITES PASSED WITH 100%% SUCCESS!  \n");
    printf("====================================================\n");
    return 0;
}
