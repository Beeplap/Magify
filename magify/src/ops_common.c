#include "ops_common.h"
#include <stdio.h>
#include <shlwapi.h>
#include <process.h>

static uint32_t g_crcTable[256];
static int g_crcInit = 0;

static void InitCRC32(void) {
    if (g_crcInit) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        g_crcTable[i] = c;
    }
    g_crcInit = 1;
}

uint32_t Magify_CRC32(const uint8_t *data, size_t length) {
    InitCRC32();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < length; i++) {
        crc = g_crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

MagifyFileClass Magify_DetectFileClass(const WCHAR *filePath) {
    if (!filePath || filePath[0] == L'\0') return FILE_CLASS_GENERIC;

    const WCHAR *ext = PathFindExtensionW(filePath);
    if (_wcsicmp(ext, L".pdf") == 0) {
        return FILE_CLASS_PDF;
    }
    if (_wcsicmp(ext, L".png") == 0 ||
        _wcsicmp(ext, L".jpg") == 0 ||
        _wcsicmp(ext, L".jpeg") == 0 ||
        _wcsicmp(ext, L".webp") == 0 ||
        _wcsicmp(ext, L".bmp") == 0 ||
        _wcsicmp(ext, L".ico") == 0 ||
        _wcsicmp(ext, L".gif") == 0 ||
        _wcsicmp(ext, L".tif") == 0 ||
        _wcsicmp(ext, L".tiff") == 0 ||
        _wcsicmp(ext, L".heic") == 0 ||
        _wcsicmp(ext, L".heif") == 0 ||
        _wcsicmp(ext, L".avif") == 0) {
        return FILE_CLASS_IMAGE;
    }

    HANDLE hFile = CreateFileW(filePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        uint8_t buf[16] = {0};
        DWORD bytesRead = 0;
        if (ReadFile(hFile, buf, sizeof(buf), &bytesRead, NULL) && bytesRead >= 4) {
            CloseHandle(hFile);

            if (buf[0] == 0x25 && buf[1] == 0x50 && buf[2] == 0x44 && buf[3] == 0x46) {
                return FILE_CLASS_PDF;
            }
            if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) {
                return FILE_CLASS_IMAGE;
            }
            if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
                return FILE_CLASS_IMAGE;
            }
            if (buf[0] == 0x42 && buf[1] == 0x4D) {
                return FILE_CLASS_IMAGE;
            }
            if (buf[0] == 'G' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == '8') {
                return FILE_CLASS_IMAGE;
            }
            if (buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0x01 && buf[3] == 0x00) {
                return FILE_CLASS_IMAGE;
            }
            if (bytesRead >= 12 &&
                buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F' &&
                buf[8] == 'W' && buf[9] == 'E' && buf[10] == 'B' && buf[11] == 'P') {
                return FILE_CLASS_IMAGE;
            }
            if (bytesRead >= 12 &&
                buf[4] == 'f' && buf[5] == 't' && buf[6] == 'y' && buf[7] == 'p' &&
                ((buf[8] == 'h' && buf[9] == 'e' && buf[10] == 'i') ||
                 (buf[8] == 'm' && buf[9] == 'i' && buf[10] == 'f') ||
                 (buf[8] == 'm' && buf[9] == 's' && buf[10] == 'f'))) {
                return FILE_CLASS_IMAGE;
            }
        } else {
            CloseHandle(hFile);
        }
    }

    return FILE_CLASS_GENERIC;
}

uint64_t Magify_GetFileSize(const WCHAR *filePath) {
    if (!filePath || filePath[0] == L'\0') return 0;
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (GetFileAttributesExW(filePath, GetFileExInfoStandard, &fad)) {
        return ((uint64_t)fad.nFileSizeHigh << 32) | (uint64_t)fad.nFileSizeLow;
    }
    return 0;
}

void Magify_FormatFileSize(uint64_t bytes, WCHAR *outStr, size_t maxLen) {
    if (!outStr || maxLen == 0) return;

    if (bytes < 1024) {
        swprintf(outStr, maxLen, L"%llu B", (unsigned long long)bytes);
    } else if (bytes < 1024 * 1024) {
        double kb = (double)bytes / 1024.0;
        swprintf(outStr, maxLen, L"%.1f KB", kb);
    } else if (bytes < 1024ULL * 1024 * 1024) {
        double mb = (double)bytes / (1024.0 * 1024.0);
        swprintf(outStr, maxLen, L"%.1f MB", mb);
    } else {
        double gb = (double)bytes / (1024.0 * 1024.0 * 1024.0);
        swprintf(outStr, maxLen, L"%.2f GB", gb);
    }
}

typedef struct {
    MagifyContext *ctx;
    LPTHREAD_START_ROUTINE func;
    void *arg;
} WorkerParam;

static unsigned __stdcall WorkerWrapper(void *param) {
    WorkerParam *wp = (WorkerParam*)param;
    if (wp && wp->func) {
        wp->func(wp->arg);
    }
    if (wp && wp->ctx) {
        MagifyContext *ctx = wp->ctx;
        if (!ctx->cancelRequested) {
            if (ctx->isErrorState) {
                // Keep error ring visible for 1.2s
                Sleep(1200);
                ctx->uiState = UI_STATE_CLOSING;
            } else {
                ctx->progressPercent = 100;
                ctx->jobProgress = 100;
                ctx->isSuccessState = TRUE;
                ctx->completionTick = GetTickCount();

                // Format toast size-savings report if applicable
                if (ctx->originalTotalBytes > 0 && ctx->finalTotalBytes > 0 &&
                    !wcsstr(ctx->progressStatusText, L"→")) {
                    Magify_FormatSizeDelta(ctx->originalTotalBytes, ctx->finalTotalBytes,
                                           ctx->progressStatusText, 128);
                    wcsncpy(ctx->statusText, ctx->progressStatusText, 127);
                }

                // 350ms success checkmark + total 750ms before smooth dismiss
                Sleep(750);
                ctx->uiState = UI_STATE_CLOSING;
            }
        } else {
            ctx->uiState = UI_STATE_CLOSING;
        }
        ctx->isProcessing = 0;
        if (ctx->hWorkerThread) {
            CloseHandle(ctx->hWorkerThread);
            ctx->hWorkerThread = NULL;
        }
    }
    free(wp);
    return 0;
}

void Magify_LaunchWorker(MagifyContext *ctx, LPTHREAD_START_ROUTINE func, void *arg) {
    if (!ctx) return;
    WorkerParam *wp = (WorkerParam*)malloc(sizeof(WorkerParam));
    if (!wp) return;
    wp->ctx = ctx;
    wp->func = func;
    wp->arg = arg;

    // Capture initial file size
    ctx->originalTotalBytes = 0;
    for (int i = 0; i < ctx->fileCount; i++) {
        ctx->originalTotalBytes += Magify_GetFileSize(ctx->files[i]);
    }
    ctx->finalTotalBytes = 0;

    ctx->isProcessing = 1;
    ctx->cancelRequested = 0;
    ctx->isSuccessState = FALSE;
    ctx->isErrorState = FALSE;
    ctx->progressPercent = 0;
    ctx->jobProgress = 0;
    ctx->uiState = UI_STATE_PROCESSING;

    uintptr_t th = _beginthreadex(NULL, 0, WorkerWrapper, wp, 0, NULL);
    ctx->hWorkerThread = (HANDLE)th;
}

void Magify_CancelWorker(MagifyContext *ctx) {
    if (!ctx) return;
    ctx->cancelRequested = 1;
    if (ctx->hWorkerThread) {
        WaitForSingleObject(ctx->hWorkerThread, 150);
        CloseHandle(ctx->hWorkerThread);
        ctx->hWorkerThread = NULL;
    }
    ctx->isProcessing = 0;
}

BOOL Magify_GetSafeOutputPath(const WCHAR *srcPath, const WCHAR *suffix, const WCHAR *newExt,
                              BOOL overwriteInPlace, WCHAR *outPath, WCHAR *tempPath) {
    if (!srcPath || !outPath) return FALSE;

    WCHAR dir[MAX_PATH];
    wcsncpy(dir, srcPath, MAX_PATH - 1);
    dir[MAX_PATH - 1] = L'\0';
    PathRemoveFileSpecW(dir);

    WCHAR base[MAX_PATH];
    wcsncpy(base, PathFindFileNameW(srcPath), MAX_PATH - 1);
    base[MAX_PATH - 1] = L'\0';
    PathRemoveExtensionW(base);

    const WCHAR *ext = (newExt && newExt[0]) ? newExt : PathFindExtensionW(srcPath);
    WCHAR dotExt[32];
    if (ext[0] != L'.') {
        swprintf(dotExt, 32, L".%ls", ext);
        ext = dotExt;
    }

    if (overwriteInPlace) {
        if (tempPath) {
            swprintf(tempPath, MAX_PATH, L"%ls\\temp_magify_%u_%u.tmp",
                     dir, (unsigned)GetCurrentProcessId(), (unsigned)GetTickCount());
            wcsncpy(outPath, tempPath, MAX_PATH - 1);
        } else {
            wcsncpy(outPath, srcPath, MAX_PATH - 1);
        }
        return TRUE;
    }

    if (tempPath) tempPath[0] = L'\0';

    const WCHAR *sfx = (suffix && suffix[0]) ? suffix : L"magify";
    swprintf(outPath, MAX_PATH, L"%ls\\%ls_%ls%ls", dir, base, sfx, ext);

    // If file already exists, find non-colliding index _2, _3...
    if (GetFileAttributesW(outPath) != INVALID_FILE_ATTRIBUTES) {
        for (int i = 2; i <= 999; i++) {
            swprintf(outPath, MAX_PATH, L"%ls\\%ls_%ls_%d%ls", dir, base, sfx, i, ext);
            if (GetFileAttributesW(outPath) == INVALID_FILE_ATTRIBUTES) {
                break;
            }
        }
    }
    return TRUE;
}

BOOL Magify_FinalizeOutputFile(const WCHAR *originalPath, const WCHAR *tempPath, const WCHAR *finalPath,
                              BOOL overwriteInPlace, MagifyContext *ctx) {
    if (overwriteInPlace) {
        if (!tempPath || tempPath[0] == L'\0') return TRUE;

        DWORD attrs = GetFileAttributesW(originalPath);
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY)) {
            SetFileAttributesW(originalPath, attrs & ~FILE_ATTRIBUTE_READONLY);
        }

        BOOL ok = MoveFileExW(tempPath, originalPath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) {
            ok = CopyFileW(tempPath, originalPath, FALSE);
            DeleteFileW(tempPath);
        }

        if (!ok) {
            if (ctx) {
                ctx->isErrorState = TRUE;
                swprintf(ctx->progressStatusText, 128, L"Error: File is locked by another app");
                wcsncpy(ctx->statusText, ctx->progressStatusText, 127);
            }
            return FALSE;
        }

        if (ctx) {
            ctx->finalTotalBytes = Magify_GetFileSize(originalPath);
        }
        return TRUE;
    } else {
        if (ctx) {
            ctx->finalTotalBytes = Magify_GetFileSize(finalPath);
        }
        return TRUE;
    }
}

void Magify_FormatSizeDelta(uint64_t origBytes, uint64_t finalBytes, WCHAR *outStr, size_t maxLen) {
    if (!outStr || maxLen == 0) return;

    WCHAR szOrig[32], szFinal[32];
    Magify_FormatFileSize(origBytes, szOrig, 32);
    Magify_FormatFileSize(finalBytes, szFinal, 32);

    if (origBytes > 0 && finalBytes < origBytes) {
        int pct = (int)((1.0 - (double)finalBytes / (double)origBytes) * 100.0);
        swprintf(outStr, maxLen, L"%ls → %ls (-%d%%)", szOrig, szFinal, pct);
    } else if (finalBytes > 0) {
        swprintf(outStr, maxLen, L"%ls → %ls", szOrig, szFinal);
    } else {
        swprintf(outStr, maxLen, L"Saved successfully!");
    }
}

BOOL Magify_RenameFiles(MagifyContext *ctx, const WCHAR *newBaseName) {
    if (!ctx || ctx->fileCount == 0 || !newBaseName || newBaseName[0] == L'\0') {
        return FALSE;
    }

    if (ctx->fileCount == 1) {
        const WCHAR *oldPath = ctx->files[0];
        WCHAR dir[MAX_PATH];
        wcsncpy(dir, oldPath, MAX_PATH - 1);
        dir[MAX_PATH - 1] = L'\0';
        PathRemoveFileSpecW(dir);

        const WCHAR *ext = PathFindExtensionW(oldPath);
        WCHAR newPath[MAX_PATH];
        swprintf(newPath, MAX_PATH, L"%ls\\%ls%ls", dir, newBaseName, ext);

        if (_wcsicmp(oldPath, newPath) == 0) {
            swprintf(ctx->statusText, 128, L"Name unchanged");
            ctx->uiState = UI_STATE_CLOSING;
            return TRUE;
        }

        if (MoveFileExW(oldPath, newPath, MOVEFILE_COPY_ALLOWED)) {
            wcsncpy(ctx->files[0], newPath, MAX_PATH - 1);
            swprintf(ctx->statusText, 128, L"Renamed to %ls%ls", newBaseName, ext);
            wcsncpy(ctx->progressStatusText, ctx->statusText, 127);
            ctx->isSuccessState = TRUE;
            ctx->uiState = UI_STATE_CLOSING;
            return TRUE;
        } else {
            DWORD err = GetLastError();
            if (err == ERROR_ACCESS_DENIED || err == ERROR_SHARING_VIOLATION) {
                swprintf(ctx->statusText, 128, L"Error: File is locked by another app");
            } else {
                swprintf(ctx->statusText, 128, L"Rename failed (Error %lu)", (unsigned long)err);
            }
            wcsncpy(ctx->progressStatusText, ctx->statusText, 127);
            ctx->isErrorState = TRUE;
            return FALSE;
        }
    }

    // Batch rename
    int renamedCount = 0;
    for (int i = 0; i < ctx->fileCount; i++) {
        const WCHAR *oldPath = ctx->files[i];
        WCHAR dir[MAX_PATH];
        wcsncpy(dir, oldPath, MAX_PATH - 1);
        dir[MAX_PATH - 1] = L'\0';
        PathRemoveFileSpecW(dir);

        const WCHAR *ext = PathFindExtensionW(oldPath);
        WCHAR newPath[MAX_PATH];
        swprintf(newPath, MAX_PATH, L"%ls\\%ls_%d%ls", dir, newBaseName, i + 1, ext);

        if (MoveFileExW(oldPath, newPath, MOVEFILE_COPY_ALLOWED)) {
            wcsncpy(ctx->files[i], newPath, MAX_PATH - 1);
            renamedCount++;
        }
    }

    if (renamedCount == ctx->fileCount) {
        swprintf(ctx->statusText, 128, L"Batch renamed %d files", renamedCount);
        ctx->isSuccessState = TRUE;
    } else {
        swprintf(ctx->statusText, 128, L"Batch renamed %d of %d files (some locked)", renamedCount, ctx->fileCount);
        ctx->isErrorState = TRUE;
    }
    wcsncpy(ctx->progressStatusText, ctx->statusText, 127);
    ctx->uiState = UI_STATE_CLOSING;
    return (renamedCount > 0);
}

#pragma pack(push, 1)
typedef struct {
    uint32_t signature;       // 0x04034B50
    uint16_t versionNeeded;   // 20
    uint16_t bitFlag;         // 0
    uint16_t method;          // 0 (Stored)
    uint16_t dosTime;
    uint16_t dosDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLen;
    uint16_t extraFieldLen;
} ZipLocalHeader;

typedef struct {
    uint32_t signature;       // 0x02014B50
    uint16_t versionMadeBy;   // 20
    uint16_t versionNeeded;   // 20
    uint16_t bitFlag;         // 0
    uint16_t method;          // 0 (Stored)
    uint16_t dosTime;
    uint16_t dosDate;
    uint32_t crc32;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t fileNameLen;
    uint16_t extraFieldLen;
    uint16_t commentLen;
    uint16_t diskNumStart;
    uint16_t internalAttr;
    uint32_t externalAttr;
    uint32_t localHeaderOffset;
} ZipCentralHeader;

typedef struct {
    uint32_t signature;       // 0x06054B50
    uint16_t diskNum;
    uint16_t cdDisk;
    uint16_t numEntriesThisDisk;
    uint16_t totalEntries;
    uint32_t cdSize;
    uint32_t cdOffset;
    uint16_t commentLen;
} ZipEndOfCentralDir;
#pragma pack(pop)

static void GetDosDateTime(uint16_t *dosTime, uint16_t *dosDate) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    *dosTime = (uint16_t)((st.wHour << 11) | (st.wMinute << 5) | (st.wSecond >> 1));
    *dosDate = (uint16_t)(((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay);
}

typedef struct {
    uint32_t localOffset;
    uint32_t crc32;
    uint32_t size;
    char nameUtf8[MAX_PATH];
} ZipEntryMeta;

BOOL Magify_CreateZipArchive(MagifyContext *ctx, const WCHAR *outputZipPath) {
    if (!ctx || ctx->fileCount == 0 || !outputZipPath) return FALSE;

    // Check if tar.exe is available and can create a compressed deflate zip
    WCHAR tarCmd[MAX_PATH * 3 + 128];
    WCHAR firstDir[MAX_PATH];
    wcsncpy(firstDir, ctx->files[0], MAX_PATH - 1);
    firstDir[MAX_PATH - 1] = L'\0';
    PathRemoveFileSpecW(firstDir);

    swprintf(tarCmd, sizeof(tarCmd)/sizeof(WCHAR), L"tar.exe -a -c -f \"%ls\" -C \"%ls\"", outputZipPath, firstDir);
    for (int i = 0; i < ctx->fileCount; i++) {
        const WCHAR *fn = PathFindFileNameW(ctx->files[i]);
        WCHAR appendPart[MAX_PATH + 8];
        swprintf(appendPart, MAX_PATH + 8, L" \"%ls\"", fn);
        wcsncat(tarCmd, appendPart, sizeof(tarCmd)/sizeof(WCHAR) - wcslen(tarCmd) - 1);
    }

    STARTUPINFOW si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    memset(&pi, 0, sizeof(pi));

    if (CreateProcessW(NULL, tarCmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        DWORD exitCode = 1;
        GetExitCodeProcess(pi.hProcess, &exitCode);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exitCode == 0 && GetFileAttributesW(outputZipPath) != INVALID_FILE_ATTRIBUTES) {
            uint64_t zipSz = Magify_GetFileSize(outputZipPath);
            WCHAR szStr[32];
            Magify_FormatFileSize(zipSz, szStr, 32);
            swprintf(ctx->statusText, 128, L"Created ZIP archive (%ls)", szStr);
            return TRUE;
        }
    }

    // Fallback: Pure-C11 PKZIP Archive Writer
    HANDLE hZip = CreateFileW(outputZipPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hZip == INVALID_HANDLE_VALUE) return FALSE;

    uint16_t dosTime, dosDate;
    GetDosDateTime(&dosTime, &dosDate);

    ZipEntryMeta entries[MAGIFY_MAX_FILES];
    int validCount = 0;
    DWORD bytesWritten = 0;

    for (int i = 0; i < ctx->fileCount; i++) {
        HANDLE hIn = CreateFileW(ctx->files[i], GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (hIn == INVALID_HANDLE_VALUE) continue;

        DWORD fSize = GetFileSize(hIn, NULL);
        uint8_t *fBuf = (uint8_t*)malloc(fSize ? fSize : 1);
        if (!fBuf) {
            CloseHandle(hIn);
            continue;
        }

        DWORD readBytes = 0;
        if (fSize > 0) {
            ReadFile(hIn, fBuf, fSize, &readBytes, NULL);
        }
        CloseHandle(hIn);

        const WCHAR *wFileName = PathFindFileNameW(ctx->files[i]);
        WideCharToMultiByte(CP_UTF8, 0, wFileName, -1, entries[validCount].nameUtf8, MAX_PATH, NULL, NULL);

        uint16_t nameLen = (uint16_t)strlen(entries[validCount].nameUtf8);
        uint32_t crc = Magify_CRC32(fBuf, fSize);

        entries[validCount].localOffset = SetFilePointer(hZip, 0, NULL, FILE_CURRENT);
        entries[validCount].crc32 = crc;
        entries[validCount].size = fSize;

        ZipLocalHeader loc = {0};
        loc.signature = 0x04034B50;
        loc.versionNeeded = 20;
        loc.method = 0; // Store
        loc.dosTime = dosTime;
        loc.dosDate = dosDate;
        loc.crc32 = crc;
        loc.compressedSize = fSize;
        loc.uncompressedSize = fSize;
        loc.fileNameLen = nameLen;
        loc.extraFieldLen = 0;

        WriteFile(hZip, &loc, sizeof(loc), &bytesWritten, NULL);
        WriteFile(hZip, entries[validCount].nameUtf8, nameLen, &bytesWritten, NULL);
        if (fSize > 0) {
            WriteFile(hZip, fBuf, fSize, &bytesWritten, NULL);
        }

        free(fBuf);
        validCount++;
    }

    uint32_t cdOffset = SetFilePointer(hZip, 0, NULL, FILE_CURRENT);

    for (int i = 0; i < validCount; i++) {
        uint16_t nameLen = (uint16_t)strlen(entries[i].nameUtf8);
        ZipCentralHeader cd = {0};
        cd.signature = 0x02014B50;
        cd.versionMadeBy = 20;
        cd.versionNeeded = 20;
        cd.method = 0;
        cd.dosTime = dosTime;
        cd.dosDate = dosDate;
        cd.crc32 = entries[i].crc32;
        cd.compressedSize = entries[i].size;
        cd.uncompressedSize = entries[i].size;
        cd.fileNameLen = nameLen;
        cd.externalAttr = 0x20; // Archive
        cd.localHeaderOffset = entries[i].localOffset;

        WriteFile(hZip, &cd, sizeof(cd), &bytesWritten, NULL);
        WriteFile(hZip, entries[i].nameUtf8, nameLen, &bytesWritten, NULL);
    }

    uint32_t cdEnd = SetFilePointer(hZip, 0, NULL, FILE_CURRENT);
    uint32_t cdSize = cdEnd - cdOffset;

    ZipEndOfCentralDir eocd = {0};
    eocd.signature = 0x06054B50;
    eocd.numEntriesThisDisk = (uint16_t)validCount;
    eocd.totalEntries = (uint16_t)validCount;
    eocd.cdSize = cdSize;
    eocd.cdOffset = cdOffset;

    WriteFile(hZip, &eocd, sizeof(eocd), &bytesWritten, NULL);
    CloseHandle(hZip);

    uint64_t zipSz = Magify_GetFileSize(outputZipPath);
    ctx->finalTotalBytes = zipSz;
    ctx->progressPercent = 100;
    ctx->jobProgress = 100;
    WCHAR szStr[32];
    Magify_FormatFileSize(zipSz, szStr, 32);
    swprintf(ctx->statusText, 128, L"Created ZIP archive (%ls)", szStr);
    wcsncpy(ctx->progressStatusText, ctx->statusText, 127);
    return TRUE;
}

typedef struct {
    MagifyContext *ctx;
    WCHAR outZipPath[MAX_PATH];
} ZipWorkerArg;

static DWORD WINAPI GenericZipWorkerThread(void *arg) {
    ZipWorkerArg *zwa = (ZipWorkerArg*)arg;
    for (int p = 0; p <= 60; p += 20) {
        if (zwa->ctx->cancelRequested) {
            free(zwa);
            return 0;
        }
        zwa->ctx->jobProgress = p;
        zwa->ctx->progressPercent = p;
        swprintf(zwa->ctx->progressStatusText, 128, L"Creating ZIP archive (%d%%)...", p);
        wcsncpy(zwa->ctx->statusText, zwa->ctx->progressStatusText, 127);
        Sleep(30);
    }
    Magify_CreateZipArchive(zwa->ctx, zwa->outZipPath);
    free(zwa);
    return 0;
}

void Magify_CompressGenericZip(MagifyContext *ctx) {
    if (!ctx || ctx->fileCount == 0) return;

    ZipWorkerArg *zwa = (ZipWorkerArg*)malloc(sizeof(ZipWorkerArg));
    if (!zwa) return;
    zwa->ctx = ctx;

    const WCHAR *firstFile = ctx->files[0];
    Magify_GetSafeOutputPath(firstFile, L"magify", L".zip", FALSE, zwa->outZipPath, NULL);

    Magify_LaunchWorker(ctx, (LPTHREAD_START_ROUTINE)GenericZipWorkerThread, zwa);
}
