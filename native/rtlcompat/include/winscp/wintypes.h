//---------------------------------------------------------------------------
// wintypes.h — Win32 scalar/struct typedefs the engine still names directly.
// These are gradually replaced by platform adapters (Phase 2); for now they let the
// headers parse. Handle-like types are opaque pointers on non-Windows.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_WINTYPES_H
#define WINSCP_RTLCOMPAT_WINTYPES_H

#include "winscp/rtldefs.h"
#include <cstdint>

typedef void *          HANDLE;
typedef HANDLE          HKEY;
typedef HANDLE          HINSTANCE;
typedef HANDLE          HMODULE;
typedef HANDLE          HWND;
typedef unsigned long   DWORD;
typedef unsigned int    UINT;
typedef int             BOOL;
typedef unsigned char   BYTE;
typedef unsigned short  Word;
typedef unsigned long   LCID;
typedef DWORD           REGSAM;

struct FILETIME   { DWORD dwLowDateTime; DWORD dwHighDateTime; };
struct SYSTEMTIME { Word wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };

#endif
