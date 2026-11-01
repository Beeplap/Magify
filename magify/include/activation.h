#ifndef MAGIFY_ACTIVATION_H
#define MAGIFY_ACTIVATION_H
#include "ipc.h"

/* Call selection discovery from a COM-initialized STA worker. */
HRESULT Magify_GetExplorerSelection(HWND foreground, MagifyFileList *list);
HRESULT Magify_PickFiles(MagifyFileList *list);
#endif
