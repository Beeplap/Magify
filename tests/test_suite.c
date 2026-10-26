#include "mg_core.h"
#include "mg_shell.h"
#include "mg_image.h"
#include "mg_pdf.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    printf("==========================================\n");
    printf("        MAGIFY ENGINE VERIFICATION        \n");
    printf("==========================================\n");

    // 1. Test File Inspection on Image
    mg_target_file_t target_img = {0};
    mg_result_t res = mg_file_inspect(L"test_sample.png", &target_img);
    printf("[TEST 1] File inspect (Image): res=%d, type=%d, fmt=%d\n", res, target_img.file_type, target_img.img_format);
    assert(res == MG_SUCCESS);
    assert(target_img.file_type == MG_FILE_TYPE_IMAGE);
    assert(target_img.img_format == MG_IMG_FMT_PNG);

    // 2. Test Image Rotation
    res = mg_image_rotate(L"test_sample.png", 90);
    printf("[TEST 2] Image rotate 90 deg: res=%d\n", res);
    assert(res == MG_SUCCESS);

    mg_bitmap_t bmp_rot;
    res = mg_bitmap_load("test_sample.png", &bmp_rot);
    assert(res == MG_SUCCESS);
    printf("         Rotated image size: %dx%d (Expected 300x400)\n", bmp_rot.width, bmp_rot.height);
    assert(bmp_rot.width == 300 && bmp_rot.height == 400);
    mg_bitmap_free(&bmp_rot);

    // 3. Test Convert Image to PDF
    wchar_t out_pdf[MAX_PATH] = {0};
    res = mg_pdf_create_from_image(L"test_sample.png", out_pdf, MAX_PATH);
    printf("[TEST 3] Create PDF from Image: res=%d, path=%ls\n", res, out_pdf);
    assert(res == MG_SUCCESS);

    // 4. Test File Inspection on Generated PDF
    mg_target_file_t target_pdf = {0};
    res = mg_file_inspect(L"test_sample.pdf", &target_pdf);
    printf("[TEST 4] File inspect (PDF): res=%d, type=%d\n", res, target_pdf.file_type);
    assert(res == MG_SUCCESS);
    assert(target_pdf.file_type == MG_FILE_TYPE_PDF);

    // 5. Test PDF Page Inspection
    uint32_t page_count = 0;
    res = mg_pdf_inspect(L"test_sample.pdf", &page_count);
    printf("[TEST 5] PDF Page count: res=%d, pages=%u\n", res, page_count);
    assert(res == MG_SUCCESS);
    assert(page_count == 1);

    // 6. Test PDF Page Rotation
    res = mg_pdf_rotate_pages(L"test_sample.pdf", 90);
    printf("[TEST 6] PDF Rotate pages: res=%d\n", res);
    assert(res == MG_SUCCESS);

    // 7. Test PDF Compression
    res = mg_pdf_compress(L"test_sample.pdf");
    printf("[TEST 7] PDF Compression: res=%d\n", res);
    assert(res == MG_SUCCESS);

    // 8. Test Image Grayscale
    res = mg_image_grayscale(L"test_sample.png");
    printf("[TEST 8] Image Grayscale: res=%d\n", res);
    assert(res == MG_SUCCESS);

    printf("\n✨ ALL 8 VERIFICATION TESTS PASSED SUCCESSFULLY! ✨\n");
    return 0;
}
