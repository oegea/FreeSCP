//---------------------------------------------------------------------------
// SysExtra.h — remaining System.SysUtils / DateUtils / IOUtils / Win32 surface used by
// engine bodies: date math, path helpers, file ops, TEncoding, and Windows-API stubs.
// Portable parts are real (paths via std::filesystem, Delphi serial dates); Windows-only
// parts (modules, processes, timezone) are stubs returning mac-appropriate defaults so the
// Windows code paths compile and are simply skipped.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_SYSEXTRA_H
#define WINSCP_RTLCOMPAT_SYSEXTRA_H

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/WinCompat.h"
#include "winscp/Object.h"
#include "winscp/UnicodeString.h"
#include "winscp/AnsiStrings.h"
#include "winscp/DynamicArray.h"
#include <cstdio>

class TDateTime;       // from SysUtils.hpp
struct TSearchRec;
struct TTimeStamp;
struct TFormatSettings;
int __fastcall ExceptionErrorMessage(TObject * E, void * Addr, wchar_t * Buffer, int Len);

//--- Delphi date/time constants ---
const int    HoursPerDay   = 24;
const int    MinsPerHour   = 60;
const int    SecsPerMin    = 60;
const int    MinsPerDay    = HoursPerDay * MinsPerHour;
const int    SecsPerDay    = MinsPerDay * SecsPerMin;
const int    MSecsPerSec   = 1000;
const __int64 MSecsPerDay  = (__int64)SecsPerDay * MSecsPerSec;
const int    DateDelta     = 693594;   // days between 0000-12-31 and 1899-12-30
const int    UnixDateDelta = 25569;    // days between 1899-12-30 and 1970-01-01
extern const UnicodeString sLineBreak;

//--- Windows version globals (functions here; stubbed to "not Windows") ---
int  __fastcall Win32MajorVersion();
int  __fastcall Win32MinorVersion();
int  __fastcall Win32BuildNumber();
UnicodeString __fastcall Win32CSDVersion();
bool __fastcall CheckWin32Version(int AMajor, int AMinor);
#define kernel32 L"kernel32"
extern void * HInstance;
extern void * LibModuleList;

//--- date/time (Delphi TDateTime = days since 1899-12-30) ---
TDateTime __fastcall Now();
TDateTime __fastcall Date();
TDateTime __fastcall Time();
TDateTime __fastcall EncodeDate(int Year, int Month, int Day);
TDateTime __fastcall EncodeTime(int Hour, int Min, int Sec, int MSec);
void __fastcall DecodeDate(const TDateTime & DT, Word & Year, Word & Month, Word & Day);
void __fastcall DecodeTime(const TDateTime & DT, Word & Hour, Word & Min, Word & Sec, Word & MSec);
int  __fastcall DayOfWeek(const TDateTime & DT);
TDateTime __fastcall IncMilliSecond(const TDateTime & DT, __int64 N);
TDateTime __fastcall IncSecond(const TDateTime & DT, __int64 N);
TDateTime __fastcall IncMinute(const TDateTime & DT, __int64 N);
TDateTime __fastcall IncHour(const TDateTime & DT, __int64 N);
TDateTime __fastcall IncDay(const TDateTime & DT, int N);
TDateTime __fastcall IncYear(const TDateTime & DT, int N);
bool __fastcall IsSameDay(const TDateTime & A, const TDateTime & B);
double __fastcall DaysBetween(const TDateTime & A, const TDateTime & B);
double __fastcall HoursBetween(const TDateTime & A, const TDateTime & B);
double __fastcall MinutesBetween(const TDateTime & A, const TDateTime & B);
double __fastcall SecondsBetween(const TDateTime & A, const TDateTime & B);
double __fastcall MonthsBetween(const TDateTime & A, const TDateTime & B);
double __fastcall YearsBetween(const TDateTime & A, const TDateTime & B);
int __fastcall MilliSecondOfTheDay(const TDateTime & DT);
int __fastcall MilliSecondOfTheHour(const TDateTime & DT);
int __fastcall MilliSecondOfTheMinute(const TDateTime & DT);
int __fastcall MilliSecondOfTheSecond(const TDateTime & DT);
__int64 __fastcall MilliSecondOfTheYear(const TDateTime & DT);
UnicodeString __fastcall FormatDateTime(const UnicodeString & Fmt, const TDateTime & DT);
UnicodeString __fastcall DateTimeToStr(const TDateTime & DT);
UnicodeString __fastcall DateToStr(const TDateTime & DT);
UnicodeString __fastcall TimeToStr(const TDateTime & DT);
UnicodeString __fastcall FormatFloat(const UnicodeString & Fmt, double Value);
TDateTime __fastcall SystemTimeToDateTime(const SYSTEMTIME & ST);
TTimeStamp __fastcall DateTimeToTimeStamp(const TDateTime & DT);
bool __fastcall TryStrToDateTime(const UnicodeString & S, TDateTime & Value);
bool __fastcall TryStrToDateTime(const UnicodeString & S, TDateTime & Value, const TFormatSettings & FS);
bool __fastcall TryStrToInt(const UnicodeString & S, int & Value);
bool __fastcall TryStrToInt64(const UnicodeString & S, __int64 & Value);

//--- path / file helpers (std::filesystem-backed) ---
UnicodeString __fastcall ExtractFilePath(const UnicodeString & FileName);
UnicodeString __fastcall ExtractFileName(const UnicodeString & FileName);
UnicodeString __fastcall ExtractFileExt(const UnicodeString & FileName);
UnicodeString __fastcall ExtractFileDir(const UnicodeString & FileName);
UnicodeString __fastcall ExtractFileDrive(const UnicodeString & FileName);
UnicodeString __fastcall ChangeFileExt(const UnicodeString & FileName, const UnicodeString & Ext);
UnicodeString __fastcall IncludeTrailingBackslash(const UnicodeString & S);
UnicodeString __fastcall IncludeTrailingPathDelimiter(const UnicodeString & S);
UnicodeString __fastcall ExcludeTrailingBackslash(const UnicodeString & S);
UnicodeString __fastcall ExpandFileName(const UnicodeString & FileName);
UnicodeString __fastcall ExtractShortPathName(const UnicodeString & FileName);
UnicodeString __fastcall GetCurrentDir();
bool __fastcall DirectoryExists(const UnicodeString & Dir);
bool __fastcall FileExists(const UnicodeString & FileName);
bool __fastcall DeleteFile(const UnicodeString & FileName);
bool __fastcall RemoveDir(const UnicodeString & Dir);
int  __fastcall GetFileAttributes(const wchar_t * FileName);
UnicodeString __fastcall ExpandEnvironmentStrings(const UnicodeString & S);
DWORD __fastcall ExpandEnvironmentStrings(const wchar_t * Src, wchar_t * Dst, DWORD Size);  // Win
UnicodeString __fastcall GetEnvironmentVariable(const UnicodeString & Name);
bool __fastcall PathIsRelative(const wchar_t * Path);

//--- Delphi FindFirst/FindNext/FindClose (std::filesystem-backed) ---
int  __fastcall FindFirst(const UnicodeString & Path, int Attr, TSearchRec & F);
int  __fastcall FindNext(TSearchRec & F);
void __fastcall FindClose(TSearchRec & F);

//--- misc string ---
UnicodeString __fastcall UTF8ToString(const RawByteString & S);
UnicodeString __fastcall AnsiLowerCase(const UnicodeString & S);
UnicodeString __fastcall AnsiReplaceStr(const UnicodeString & Text, const UnicodeString & From, const UnicodeString & To);
UnicodeString __fastcall RightStr(const UnicodeString & S, int Count);

//--- Win32 API stubs (return mac-appropriate defaults; callers gate on results) ---
BOOL  __fastcall CloseHandle(HANDLE h);
HANDLE __fastcall GetCurrentProcess();
DWORD __fastcall GetCurrentProcessId();
HMODULE __fastcall GetModuleHandle(const wchar_t * Name);
HMODULE __fastcall LoadLibrary(const wchar_t * Name);
BOOL  __fastcall FreeLibrary(HMODULE h);
void * __fastcall GetProcAddress(HMODULE h, const char * Name);
void  __fastcall SetLastError(DWORD e);
BOOL  __fastcall FileTimeToSystemTime(const FILETIME * ft, SYSTEMTIME * st);
BOOL  __fastcall FileTimeToLocalFileTime(const FILETIME * ft, FILETIME * lft);
int   __fastcall StrCmpLogicalW(const wchar_t * a, const wchar_t * b);
void  __fastcall CoTaskMemFree(void * p);

//--- TEncoding (System.SysUtils) — minimal ---
class TEncoding
{
public:
  // Class properties in Delphi — used without parens (TEncoding::UTF8).
  static TEncoding * UTF8;
  static TEncoding * ANSI;
  static TEncoding * Default;
  RawByteString __fastcall GetBytes(const UnicodeString & S);
  UnicodeString __fastcall GetString(const RawByteString & B);
  UnicodeString __fastcall GetString(const System::DynamicArray<System::Byte> & B);
  UnicodeString __fastcall GetString(const System::DynamicArray<System::Byte> & B, int Offset, int Count);
  static int __fastcall GetBufferEncoding(const System::DynamicArray<System::Byte> & B, TEncoding *& E, TEncoding * Default);
};

//--- Base64 (Soap.EncdDecd) ---
UnicodeString __fastcall EncodeBase64(const void * Data, int Size);
System::DynamicArray<System::Byte> __fastcall DecodeBase64(const UnicodeString & S);  // TBytes

//--- misc Win/Delphi types used in Windows-only code paths ---
typedef int TLocaleID;
struct CPINFOEX { UINT MaxCharSize = 0; wchar_t CodePageName[64] = {0}; };
struct DYNAMIC_TIME_ZONE_INFORMATION { TIME_ZONE_INFORMATION tzi; wchar_t TimeZoneKeyName[128] = {0}; };
typedef DYNAMIC_TIME_ZONE_INFORMATION * PDYNAMIC_TIME_ZONE_INFORMATION;
// (TGetTimeZoneInformationForYear / GetCurrentPackageFamilyNameProc / TBrandingFormatString
//  are typedef'd locally by the engine via WINAPI — defined empty in WinCompat.h.)

// TPath (System.IOUtils) — minimal static helpers.
class TPath
{
public:
  static UnicodeString __fastcall GetTempPath();
  static UnicodeString __fastcall Combine(const UnicodeString & A, const UnicodeString & B);
  static UnicodeString __fastcall GetFileName(const UnicodeString & P);
  static UnicodeString __fastcall GetDirectoryName(const UnicodeString & P);
  static UnicodeString __fastcall GetExtension(const UnicodeString & P);
};

//--- Delphi exception introspection (stubs) ---
TObject * __fastcall ExceptObject();
void *    __fastcall ExceptAddr();

//--- more Win32 stubs (Windows-only paths) ---
DWORD __fastcall GetTempPathW(DWORD n, wchar_t * buf);
int   __fastcall GetUserDefaultLCID();
int   __fastcall lstrcmp(const wchar_t * a, const wchar_t * b);
int   __fastcall lstrcmpi(const wchar_t * a, const wchar_t * b);
const wchar_t * __fastcall PathSkipRoot(const wchar_t * Path);
bool __fastcall FileGetSymLinkTarget(const UnicodeString & FileName, UnicodeString & Target);
HANDLE __fastcall CreateFile(const wchar_t *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
HANDLE __fastcall CreateToolhelp32Snapshot(DWORD, DWORD);
HANDLE __fastcall OpenProcess(DWORD, BOOL, DWORD);
BOOL  __fastcall Process32First(HANDLE, PROCESSENTRY32 *);
BOOL  __fastcall Process32Next(HANDLE, PROCESSENTRY32 *);
int   __fastcall SHFileOperation(SHFILEOPSTRUCT *);
long  __fastcall SHGetFolderPath(HWND, int, HANDLE, DWORD, wchar_t *);
BOOL  __fastcall GetProductInfo(DWORD, DWORD, DWORD, DWORD, DWORD *);
DWORD __fastcall GetTimeZoneInformation(TIME_ZONE_INFORMATION *);
DWORD __fastcall GetModuleFileNameEx(HANDLE, HMODULE, wchar_t *, DWORD);
BOOL  __fastcall IsWow64Process(HANDLE, BOOL *);
void  __fastcall GlobalFree(void *);
long  __fastcall FindMimeFromData(void *, const wchar_t *, void *, DWORD, const wchar_t *, DWORD, wchar_t **, DWORD);
int   __fastcall LoadString(HINSTANCE, UINT, wchar_t *, int);
DWORD __fastcall GetFileVersionInfoSize(const wchar_t *, DWORD *);
BOOL  __fastcall GetFileVersionInfo(const wchar_t *, DWORD, DWORD, void *);
BOOL  __fastcall VerQueryValue(const void *, const wchar_t *, void **, UINT *);
void  __fastcall Randomize();
DWORD __fastcall GetTickCount();
void  __fastcall Sleep(DWORD ms);
DWORD __fastcall SleepEx(DWORD ms, BOOL alertable);
int   __fastcall CompareValue(int A, int B);
__int64 __fastcall CompareValue(__int64 A, __int64 B);
double __fastcall CompareValue(double A, double B);
int   random(int Range);
DWORD __fastcall SHGetFileInfo(const wchar_t * Path, DWORD Attrs, TSHFileInfoW * Info, UINT cb, UINT Flags);
int   __fastcall Random(int Range);
UnicodeString __fastcall StripHotkey(const UnicodeString & S);
DWORD __fastcall GetTempPath(DWORD n, wchar_t * buf);
BOOL  __fastcall SystemTimeToTzSpecificLocalTime(TIME_ZONE_INFORMATION *, SYSTEMTIME *, SYSTEMTIME *);
BOOL  __fastcall GetCPInfoEx(UINT, DWORD, CPINFOEX *);
std::FILE * __fastcall _wfopen(const wchar_t * Path, const wchar_t * Mode);

#endif
