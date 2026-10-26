#ifndef MAGIFY_OPS_COMMON_H
#define MAGIFY_OPS_COMMON_H

#include "magify.h"

BOOL Magify_RenameFiles(MagifyContext *ctx, const WCHAR *newBaseName);
BOOL Magify_CreateZipArchive(MagifyContext *ctx, const WCHAR *outputZipPath);
void Magify_CompressGenericZip(MagifyContext *ctx);
void Magify_LaunchWorker(MagifyContext *ctx, LPTHREAD_START_ROUTINE func, void *arg);
void Magify_CancelWorker(MagifyContext *ctx);
BOOL Magify_GetSafeOutputPath(const WCHAR *srcPath, const WCHAR *suffix, const WCHAR *newExt, BOOL overwriteInPlace, WCHAR *outPath, WCHAR *tempPath);
BOOL Magify_FinalizeOutputFile(const WCHAR *originalPath, const WCHAR *tempPath, const WCHAR *finalPath, BOOL overwriteInPlace, MagifyContext *ctx);
void Magify_FormatSizeDelta(uint64_t origBytes, uint64_t finalBytes, WCHAR *outStr, size_t maxLen);
uint32_t Magify_CRC32(const uint8_t *data, size_t length);

#endif // MAGIFY_OPS_COMMON_H
