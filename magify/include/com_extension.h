#ifndef MAGIFY_COM_EXTENSION_H
#define MAGIFY_COM_EXTENSION_H

#define COBJMACROS
#include <windows.h>
#include <shobjidl.h>
#include <ole2.h>
#include <guiddef.h>

// GUID for Magify Windows 11 Explorer Command:
// {A7B8C9D0-1234-5678-9ABC-DEF012345678}
#define MAGIFY_CLSID_STR L"{A7B8C9D0-1234-5678-9ABC-DEF012345678}"

// Extern declaration of CLSID
extern const CLSID CLSID_MagifyExplorerCommand;

// Returns the IClassFactory interface pointer for Magify Explorer Command
IClassFactory *Magify_GetClassFactory(void);

// Runs the LocalServer32 COM class factory loop until all references are released
int Magify_RunComServer(void);

#endif // MAGIFY_COM_EXTENSION_H
