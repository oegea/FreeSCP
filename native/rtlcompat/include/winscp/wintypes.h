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
typedef HANDLE          THandle;
typedef std::uintptr_t  UINT_PTR;
typedef std::intptr_t   INT_PTR;
typedef std::intptr_t   LONG_PTR;
typedef std::uintptr_t  ULONG_PTR;
typedef UINT_PTR        WPARAM;
typedef LONG_PTR        LPARAM;
typedef LONG_PTR        LRESULT;

#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif

// Registry root keys (predefined HKEY handle values).
#define HKEY_CLASSES_ROOT     ((HKEY)(ULONG_PTR)0x80000000)
#define HKEY_CURRENT_USER     ((HKEY)(ULONG_PTR)0x80000001)
#define HKEY_LOCAL_MACHINE    ((HKEY)(ULONG_PTR)0x80000002)
#define HKEY_USERS            ((HKEY)(ULONG_PTR)0x80000003)
#define HKEY_CURRENT_CONFIG   ((HKEY)(ULONG_PTR)0x80000005)
#define HKEY_DYN_DATA         ((HKEY)(ULONG_PTR)0x80000006)

struct FILETIME   { DWORD dwLowDateTime; DWORD dwHighDateTime; };
struct SYSTEMTIME { Word wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; };

// Version-info fixed block (FileInfo.h). Layout matches Win32 VS_FIXEDFILEINFO.
struct VS_FIXEDFILEINFO
{
  DWORD dwSignature, dwStrucVersion;
  DWORD dwFileVersionMS, dwFileVersionLS, dwProductVersionMS, dwProductVersionLS;
  DWORD dwFileFlagsMask, dwFileFlags, dwFileOS, dwFileType, dwFileSubtype;
  DWORD dwFileDateMS, dwFileDateLS;
};
typedef VS_FIXEDFILEINFO   TVSFixedFileInfo;
typedef VS_FIXEDFILEINFO * PVSFixedFileInfo;

// Vcl.Menus TShortCut (a packed key code). Word-sized.
typedef Word TShortCut;

#endif
