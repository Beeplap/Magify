#include "mg_core.h"
#include "mg_shell.h"
#include "mg_ui.h"
#include <shellapi.h>
#include <commdlg.h>
#include <stdio.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)nCmdShow;

    // Set high-DPI awareness
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc > 1) {
        const wchar_t *arg1 = argv[1];
        bool silent = false;
        for (int i = 1; i < argc; i++) {
            if (_wcsicmp(argv[i], L"--silent") == 0 || _wcsicmp(argv[i], L"-s") == 0 || _wcsicmp(argv[i], L"/s") == 0) {
                silent = true;
            }
        }

        // Check for --install
        if (_wcsicmp(arg1, L"--install") == 0 || _wcsicmp(arg1, L"-i") == 0 || _wcsicmp(arg1, L"/install") == 0) {
            mg_result_t res = mg_shell_install();
            if (!silent) {
                if (res == MG_SUCCESS) {
                    MessageBoxW(NULL,
                        L"✨ Magify installed successfully!\n\nYou can now right-click any file or folder and select 'Modify with Magify' to launch the radial menu.",
                        L"Magify — Shell Integration",
                        MB_OK | MB_ICONINFORMATION);
                } else {
                    MessageBoxW(NULL,
                        L"Failed to register context menu entries in the Windows registry.",
                        L"Magify — Installation Error",
                        MB_OK | MB_ICONERROR);
                }
            }
            LocalFree(argv);
            return (res == MG_SUCCESS) ? 0 : 1;
        }

        // Check for --uninstall
        if (_wcsicmp(arg1, L"--uninstall") == 0 || _wcsicmp(arg1, L"-u") == 0 || _wcsicmp(arg1, L"/uninstall") == 0) {
            mg_result_t res = mg_shell_uninstall();
            if (!silent && res == MG_SUCCESS) {
                MessageBoxW(NULL,
                    L"Magify context menu integration has been completely removed.",
                    L"Magify — Uninstalled",
                    MB_OK | MB_ICONINFORMATION);
            }
            LocalFree(argv);
            return (res == MG_SUCCESS) ? 0 : 1;
        }

        // Check for --help
        if (_wcsicmp(arg1, L"--help") == 0 || _wcsicmp(arg1, L"-h") == 0 || _wcsicmp(arg1, L"/?") == 0) {
            MessageBoxW(NULL,
                L"Magify — Pure C High-Speed Radial File Controller & Editor\n\n"
                L"Usage:\n"
                L"  magify.exe <file_path>   : Open radial menu on target file\n"
                L"  magify.exe --install     : Register context menu into Windows Explorer\n"
                L"  magify.exe --uninstall   : Remove context menu from Windows Explorer\n",
                L"Magify Help",
                MB_OK | MB_ICONINFORMATION);
            LocalFree(argv);
            return 0;
        }

        // Otherwise: target file path passed from Explorer context menu
        mg_result_t ui_res = mg_ui_show_radial_menu(arg1);
        LocalFree(argv);
        return (ui_res == MG_SUCCESS) ? 0 : 1;
    }

    // No arguments: User double-clicked the executable
    bool installed = mg_shell_is_installed();
    if (!installed) {
        int choice = MessageBoxW(NULL,
            L"Welcome to Magify!\n\n"
            L"Would you like to install the 'Modify with Magify' context menu option into Windows File Explorer?\n\n"
            L"This enables instant right-click radial file editing for PDFs, Images, and all documents without needing administrator rights.",
            L"Magify Setup",
            MB_YESNO | MB_ICONQUESTION);

        if (choice == IDYES) {
            mg_shell_install();
            MessageBoxW(NULL,
                L"✨ Magify is now enabled!\n\nSimply right-click any image, PDF, or document in Explorer and choose 'Modify with Magify'.",
                L"Magify Installed",
                MB_OK | MB_ICONINFORMATION);
        }
    } else {
        // Already installed: offer to pick a file to test or uninstall
        int choice = MessageBoxW(NULL,
            L"✨ Magify is already active in your Windows Explorer context menu!\n\n"
            L"• Click [Yes] to pick a file and try the radial menu now.\n"
            L"• Click [No] to uninstall context menu integration.\n"
            L"• Click [Cancel] to exit.",
            L"Magify Status",
            MB_YESNOCANCEL | MB_ICONINFORMATION);

        if (choice == IDYES) {
            // Open File Dialog
            wchar_t file_buf[MAX_PATH] = {0};
            OPENFILENAMEW ofn = {0};
            ofn.lStructSize = sizeof(OPENFILENAMEW);
            ofn.lpstrFilter = L"All Files (*.*)\0*.*\0Images (*.png;*.jpg;*.webp;*.bmp)\0*.png;*.jpg;*.webp;*.bmp\0PDF Documents (*.pdf)\0*.pdf\0";
            ofn.lpstrFile = file_buf;
            ofn.nMaxFile = MAX_PATH;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameW(&ofn)) {
                mg_ui_show_radial_menu(file_buf);
            }
        } else if (choice == IDNO) {
            mg_shell_uninstall();
            MessageBoxW(NULL, L"Magify context menu integration has been removed.", L"Magify", MB_OK | MB_ICONINFORMATION);
        }
    }

    if (argv) LocalFree(argv);
    return 0;
}
