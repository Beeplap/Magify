#ifndef MAGIFY_IPC_H
#define MAGIFY_IPC_H

#include "magify.h"

BOOL Magify_IPC_SendPathsToPrimary(int argc, WCHAR **argv);
BOOL Magify_IPC_StartServer(MagifyContext *ctx);
void Magify_IPC_StopServer(void);

#endif // MAGIFY_IPC_H
