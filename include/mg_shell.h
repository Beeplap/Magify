#ifndef MG_SHELL_H
#define MG_SHELL_H

#include "mg_core.h"

#ifdef __cplusplus
extern "C" {
#endif

// Detect file type and gather basic metadata
mg_result_t mg_file_inspect(const wchar_t *file_path, mg_target_file_t *out_target);

// Install context menu verbs into Windows Registry (per-user, no admin prompt needed)
mg_result_t mg_shell_install(void);

// Uninstall context menu verbs from Windows Registry
mg_result_t mg_shell_uninstall(void);

// Check if Magify is currently registered in the shell
bool mg_shell_is_installed(void);

#ifdef __cplusplus
}
#endif

#endif // MG_SHELL_H
