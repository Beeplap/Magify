#ifndef MAGIFY_OPS_COMMON_H
#define MAGIFY_OPS_COMMON_H

#include "magify.h"

BOOL Magify_RenameFiles(MagifyContext *ctx, const WCHAR *newBaseName);
BOOL Magify_CreateZipArchive(MagifyContext *ctx, const WCHAR *outputZipPath);
void Magify_CompressGenericZip(MagifyContext *ctx);
void Magify_LaunchWorker(MagifyContext *ctx, LPTHREAD_START_ROUTINE func, void *arg);
uint32_t Magify_CRC32(const uint8_t *data, size_t length);

#endif // MAGIFY_OPS_COMMON_H
