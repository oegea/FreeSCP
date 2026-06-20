//---------------------------------------------------------------------------
// WinCompat.h — Win32 + Delphi constants, typedefs and SEH shims used by engine .cpp bodies.
// Constants/typedefs only (no functions). Many guard Windows-specific code paths that the
// platform adapter (Phase 2) replaces; they exist so the bodies compile.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_WINCOMPAT_H
#define WINSCP_RTLCOMPAT_WINCOMPAT_H

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"

//--- Windows scalar/pointer typedefs ---
typedef long           LONG;
typedef unsigned int   UINT32;
typedef unsigned short USHORT;
typedef wchar_t        TCHAR;
typedef wchar_t *      LPWSTR;
typedef wchar_t *      PWSTR;
typedef const wchar_t * LPCWSTR;
typedef const wchar_t * LPCTSTR;
typedef const wchar_t * PCWSTR;
typedef long           HRESULT;

// Calling-convention macros (no-ops on clang) — engine uses them in local typedefs.
#define WINAPI
#define CALLBACK
#define APIENTRY
#define WINAPIV
#define STDAPICALLTYPE
#ifndef PASCAL
#define PASCAL
#endif

//--- Windows BOOL constants ---
#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

//--- generic limits / handles ---
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)

//--- HRESULT / error codes ---
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define S_OK 0L
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_NO_MORE_FILES 18L
#define ERROR_INSUFFICIENT_BUFFER 122L

//--- file attribute bits (Delphi faXxx + Win FILE_ATTRIBUTE_*) ---
const int faReadOnly  = 0x00000001;
const int faHidden    = 0x00000002;
const int faSysFile   = 0x00000004;
const int faDirectory = 0x00000010;
const int faArchive   = 0x00000020;
const int faSymLink   = 0x00000400;
const int faAnyFile   = 0x000001FF;
#define FILE_ATTRIBUTE_TEMPORARY 0x100
#define FILE_FLAG_DELETE_ON_CLOSE 0x04000000

//--- file open / access ---
#define GENERIC_READ  0x80000000
#define GENERIC_WRITE 0x40000000
#define CREATE_NEW 1
const Word fmShareDenyWrite = 0x0020;

//--- shell file op (SHFileOperation) ---
#define FO_DELETE 3
#define FOF_SILENT 0x0004
#define FOF_RENAMEONCOLLISION 0x0008
#define FOF_NOCONFIRMATION 0x0010
#define FOF_ALLOWUNDO 0x0040
#define FOF_NOCONFIRMMKDIR 0x0200
#define FOF_NOERRORUI 0x0400
#define FMFD_URLASFILENAME 0x0001

//--- CSIDL (special folders) ---
#define CSIDL_PERSONAL 0x0005
#define CSIDL_DESKTOPDIRECTORY 0x0010
#define SHGFP_TYPE_CURRENT 0

//--- registry access ---
#define KEY_READ 0x20019
#define KEY_WOW64_64KEY 0x0100

//--- process snapshot / access ---
#define TH32CS_SNAPPROCESS 0x00000002
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_VM_READ 0x0010

//--- code page ---
#define CP_ACP 0

//--- time zone ids ---
#define TIME_ZONE_ID_UNKNOWN  0
#define TIME_ZONE_ID_STANDARD 1
#define TIME_ZONE_ID_DAYLIGHT 2
#define TIME_ZONE_ID_INVALID  0xFFFFFFFF

//--- Win structs the engine names directly ---
struct TWin32FindData
{
  DWORD dwFileAttributes = 0;
  FILETIME ftCreationTime{}, ftLastAccessTime{}, ftLastWriteTime{};
  DWORD nFileSizeHigh = 0, nFileSizeLow = 0;
  DWORD dwReserved0 = 0, dwReserved1 = 0;
  wchar_t cFileName[MAX_PATH] = {0};
  wchar_t cAlternateFileName[14] = {0};
};
struct PROCESSENTRY32
{
  DWORD dwSize = 0, cntUsage = 0, th32ProcessID = 0;
  ULONG_PTR th32DefaultHeapID = 0;
  DWORD th32ModuleID = 0, cntThreads = 0, th32ParentProcessID = 0;
  long pcPriClassBase = 0; DWORD dwFlags = 0;
  wchar_t szExeFile[MAX_PATH] = {0};
};
struct SHFILEOPSTRUCT
{
  HWND hwnd = nullptr; UINT wFunc = 0;
  const wchar_t * pFrom = nullptr; const wchar_t * pTo = nullptr;
  Word fFlags = 0; BOOL fAnyOperationsAborted = 0;
  void * hNameMappings = nullptr; const wchar_t * lpszProgressTitle = nullptr;
};
struct TIME_ZONE_INFORMATION
{
  LONG Bias = 0; wchar_t StandardName[32] = {0}; SYSTEMTIME StandardDate{}; LONG StandardBias = 0;
  wchar_t DaylightName[32] = {0}; SYSTEMTIME DaylightDate{}; LONG DaylightBias = 0;
};
typedef TIME_ZONE_INFORMATION * LPTIME_ZONE_INFORMATION;

//--- structured exception handling: neutralized on clang (no Win SEH) ---
// __try { ... } __except(filter) { ... }  ->  a plain block; the handler is dropped.
// This changes behavior (no SEH catch) but lets the bodies compile; revisit per-site.
#ifndef _WIN32
#define __try        if (true)
#define __except(x)  if (false)
#define __finally
#endif

#endif
