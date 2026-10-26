#include "mg_shell.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <stdio.h>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

static const wchar_t *REG_KEY_STAR = L"Software\\Classes\\*\\shell\\Magify";
static const wchar_t *REG_KEY_DIR  = L"Software\\Classes\\Directory\\shell\\Magify";

mg_result_t mg_file_inspect(const wchar_t *file_path, mg_target_file_t *out_target) {
    if (!file_path || !out_target) return MG_ERR_INVALID_ARGS;

    memset(out_target, 0, sizeof(mg_target_file_t));
    wcsncpy_s(out_target->file_path_w, MAX_PATH, file_path, _TRUNCATE);

    // Convert wide path to UTF-8
    WideCharToMultiByte(CP_UTF8, 0, file_path, -1, out_target->file_path_u8, sizeof(out_target->file_path_u8), NULL, NULL);

    // Extract file name
    const wchar_t *p_name = PathFindFileNameW(file_path);
    if (p_name) {
        wcsncpy_s(out_target->file_name_w, MAX_PATH, p_name, _TRUNCATE);
    } else {
        wcsncpy_s(out_target->file_name_w, MAX_PATH, file_path, _TRUNCATE);
    }

    // Get file attributes & size
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExW(file_path, GetFileExInfoStandard, &fad)) {
        return MG_ERR_FILE_NOT_FOUND;
    }
    ULARGE_INTEGER size;
    size.HighPart = fad.nFileSizeHigh;
    size.LowPart = fad.nFileSizeLow;
    out_target->file_size_bytes = size.QuadPart;

    // Read initial 32 bytes to inspect signature
    HANDLE h_file = CreateFileW(file_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h_file == INVALID_HANDLE_VALUE) {
        return MG_ERR_FILE_ACCESS_DENIED;
    }

    uint8_t sig[32] = {0};
    DWORD bytes_read = 0;
    ReadFile(h_file, sig, sizeof(sig), &bytes_read, NULL);
    CloseHandle(h_file);

    out_target->file_type = MG_FILE_TYPE_GENERIC;
    out_target->img_format = MG_IMG_FMT_UNKNOWN;

    if (bytes_read >= 5 && sig[0] == '%' && sig[1] == 'P' && sig[2] == 'D' && sig[3] == 'F' && sig[4] == '-') {
        out_target->file_type = MG_FILE_TYPE_PDF;
        return MG_SUCCESS;
    }

    // Check PNG
    if (bytes_read >= 8 && sig[0] == 0x89 && sig[1] == 'P' && sig[2] == 'N' && sig[3] == 'G' &&
        sig[4] == 0x0D && sig[5] == 0x0A && sig[6] == 0x1A && sig[7] == 0x0A) {
        out_target->file_type = MG_FILE_TYPE_IMAGE;
        out_target->img_format = MG_IMG_FMT_PNG;
        return MG_SUCCESS;
    }

    // Check JPEG
    if (bytes_read >= 3 && sig[0] == 0xFF && sig[1] == 0xD8 && sig[2] == 0xFF) {
        out_target->file_type = MG_FILE_TYPE_IMAGE;
        out_target->img_format = MG_IMG_FMT_JPEG;
        return MG_SUCCESS;
    }

    // Check WEBP
    if (bytes_read >= 12 && sig[0] == 'R' && sig[1] == 'I' && sig[2] == 'F' && sig[3] == 'F' &&
        sig[8] == 'W' && sig[9] == 'E' && sig[10] == 'B' && sig[11] == 'P') {
        out_target->file_type = MG_FILE_TYPE_IMAGE;
        out_target->img_format = MG_IMG_FMT_WEBP;
        return MG_SUCCESS;
    }

    // Check BMP
    if (bytes_read >= 2 && sig[0] == 'B' && sig[1] == 'M') {
        out_target->file_type = MG_FILE_TYPE_IMAGE;
        out_target->img_format = MG_IMG_FMT_BMP;
        return MG_SUCCESS;
    }

    // Check GIF
    if (bytes_read >= 4 && sig[0] == 'G' && sig[1] == 'I' && sig[2] == 'F' && sig[3] == '8') {
        out_target->file_type = MG_FILE_TYPE_IMAGE;
        out_target->img_format = MG_IMG_FMT_GIF;
        return MG_SUCCESS;
    }

    // Fallback: check file extension
    const wchar_t *ext = PathFindExtensionW(file_path);
    if (ext) {
        if (_wcsicmp(ext, L".pdf") == 0) {
            out_target->file_type = MG_FILE_TYPE_PDF;
        } else if (_wcsicmp(ext, L".png") == 0) {
            out_target->file_type = MG_FILE_TYPE_IMAGE;
            out_target->img_format = MG_IMG_FMT_PNG;
        } else if (_wcsicmp(ext, L".jpg") == 0 || _wcsicmp(ext, L".jpeg") == 0) {
            out_target->file_type = MG_FILE_TYPE_IMAGE;
            out_target->img_format = MG_IMG_FMT_JPEG;
        } else if (_wcsicmp(ext, L".webp") == 0) {
            out_target->file_type = MG_FILE_TYPE_IMAGE;
            out_target->img_format = MG_IMG_FMT_WEBP;
        } else if (_wcsicmp(ext, L".bmp") == 0) {
            out_target->file_type = MG_FILE_TYPE_IMAGE;
            out_target->img_format = MG_IMG_FMT_BMP;
        }
    }

    return MG_SUCCESS;
}

static bool set_reg_string(HKEY h_root, const wchar_t *sub_key, const wchar_t *value_name, const wchar_t *data) {
    HKEY h_key = NULL;
    LONG res = RegCreateKeyExW(h_root, sub_key, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &h_key, NULL);
    if (res != ERROR_SUCCESS) return false;

    res = RegSetValueExW(h_key, value_name, 0, REG_SZ, (const BYTE*)data, (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(h_key);
    return (res == ERROR_SUCCESS);
}

mg_result_t mg_shell_install(void) {
    wchar_t exe_path[MAX_PATH];
    if (GetModuleFileNameW(NULL, exe_path, MAX_PATH) == 0) {
        return MG_ERR_GENERIC;
    }

    wchar_t cmd_val[MAX_PATH + 32];
    swprintf_s(cmd_val, MAX_PATH + 32, L"\"%s\" \"%%1\"", exe_path);

    wchar_t icon_val[MAX_PATH + 8];
    swprintf_s(icon_val, MAX_PATH + 8, L"\"%s\",0", exe_path);

    // 1. Register for all files: Software\Classes\*\shell\Magify
    if (!set_reg_string(HKEY_CURRENT_USER, REG_KEY_STAR, NULL, L"Modify with Magify")) return MG_ERR_REGISTRY_FAILED;
    if (!set_reg_string(HKEY_CURRENT_USER, REG_KEY_STAR, L"Icon", icon_val)) return MG_ERR_REGISTRY_FAILED;

    wchar_t cmd_key_star[MAX_PATH];
    swprintf_s(cmd_key_star, MAX_PATH, L"%s\\command", REG_KEY_STAR);
    if (!set_reg_string(HKEY_CURRENT_USER, cmd_key_star, NULL, cmd_val)) return MG_ERR_REGISTRY_FAILED;

    // 2. Register for directories: Software\Classes\Directory\shell\Magify
    if (!set_reg_string(HKEY_CURRENT_USER, REG_KEY_DIR, NULL, L"Modify with Magify")) return MG_ERR_REGISTRY_FAILED;
    if (!set_reg_string(HKEY_CURRENT_USER, REG_KEY_DIR, L"Icon", icon_val)) return MG_ERR_REGISTRY_FAILED;

    wchar_t cmd_key_dir[MAX_PATH];
    swprintf_s(cmd_key_dir, MAX_PATH, L"%s\\command", REG_KEY_DIR);
    if (!set_reg_string(HKEY_CURRENT_USER, cmd_key_dir, NULL, cmd_val)) return MG_ERR_REGISTRY_FAILED;

    // Refresh Windows File Explorer context menus immediately
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);

    return MG_SUCCESS;
}

mg_result_t mg_shell_uninstall(void) {
    // Delete tree for star and directory
    RegDeleteTreeW(HKEY_CURRENT_USER, REG_KEY_STAR);
    RegDeleteTreeW(HKEY_CURRENT_USER, REG_KEY_DIR);

    // Refresh Windows File Explorer
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    return MG_SUCCESS;
}

bool mg_shell_is_installed(void) {
    HKEY h_key = NULL;
    LONG res = RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY_STAR, 0, KEY_READ, &h_key);
    if (res == ERROR_SUCCESS) {
        RegCloseKey(h_key);
        return true;
    }
    return false;
}
