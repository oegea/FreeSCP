//---------------------------------------------------------------------------
// SysExtra.cpp — bodies for winscp/SysExtra.h.
//---------------------------------------------------------------------------
#include "SysUtils.hpp"
#include "winscp/SysExtra.h"
#include "StrUtils.hpp"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <cmath>
#include <cstring>
#include <map>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;

const UnicodeString sLineBreak = UnicodeString(L"\n");
void * HInstance = nullptr;
void * LibModuleList = nullptr;

//=== path conversion helpers ===
static std::string ToU8(const UnicodeString & s) { return std::string(UTF8String(s).c_str()); }
static UnicodeString FromU8(const std::string & s) { return UTF8ToString(RawByteString(s.c_str())); }

//=== Windows version: this is not Windows ===
int  __fastcall Win32MajorVersion() { return 0; }
int  __fastcall Win32MinorVersion() { return 0; }
int  __fastcall Win32BuildNumber()  { return 0; }
UnicodeString __fastcall Win32CSDVersion() { return UnicodeString(); }
bool __fastcall CheckWin32Version(int, int) { return false; }

//=== Delphi serial date math (days since 1899-12-30; .frac = time of day) ===
// Gregorian day number for a date, with the 1899-12-30 epoch.
static long DateToSerial(int y, int m, int d)
{
  static const int cum[] = {0,31,59,90,120,151,181,212,243,273,304,334};
  long a = (14 - m) / 12;
  long yy = y + 4800 - a;
  long mm = m + 12 * a - 3;
  long jdn = d + (153 * mm + 2) / 5 + 365 * yy + yy / 4 - yy / 100 + yy / 400 - 32045;
  (void)cum;
  return jdn - 2415019;  // JDN of 1899-12-30
}
static void SerialToDate(long serial, int & y, int & m, int & d)
{
  long jdn = serial + 2415019;
  long a = jdn + 32044;
  long b = (4 * a + 3) / 146097;
  long c = a - (146097 * b) / 4;
  long dd = (4 * c + 3) / 1461;
  long e = c - (1461 * dd) / 4;
  long mm = (5 * e + 2) / 153;
  d = static_cast<int>(e - (153 * mm + 2) / 5 + 1);
  m = static_cast<int>(mm + 3 - 12 * (mm / 10));
  y = static_cast<int>(100 * b + dd - 4800 + mm / 10);
}

TDateTime __fastcall EncodeDate(int Year, int Month, int Day)
{ return TDateTime(static_cast<double>(DateToSerial(Year, Month, Day))); }
TDateTime __fastcall EncodeTime(int Hour, int Min, int Sec, int MSec)
{ return TDateTime((((Hour * 60.0 + Min) * 60.0 + Sec) * 1000.0 + MSec) / MSecsPerDay); }

static void Split(const TDateTime & dt, long & days, double & frac)
{
  double v = dt; days = static_cast<long>(std::floor(v)); frac = v - days;
  if (frac < 0) { frac += 1; days -= 1; }
}
void __fastcall DecodeDate(const TDateTime & DT, Word & Year, Word & Month, Word & Day)
{ long days; double f; Split(DT, days, f); int y,m,d; SerialToDate(days, y, m, d);
  Year = static_cast<Word>(y); Month = static_cast<Word>(m); Day = static_cast<Word>(d); }
void __fastcall DecodeTime(const TDateTime & DT, Word & Hour, Word & Min, Word & Sec, Word & MSec)
{ long days; double f; Split(DT, days, f);
  __int64 ms = static_cast<__int64>(std::llround(f * MSecsPerDay));
  MSec = ms % 1000; ms /= 1000; Sec = ms % 60; ms /= 60; Min = ms % 60; Hour = static_cast<Word>(ms / 60); }

TDateTime __fastcall Now()
{
  std::time_t t = std::time(nullptr);
  std::tm lt{}; localtime_r(&t, &lt);
  return EncodeDate(lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday) +
         EncodeTime(lt.tm_hour, lt.tm_min, lt.tm_sec, 0);
}
TDateTime __fastcall Date() { long d; double f; TDateTime n = Now(); Split(n, d, f); return TDateTime(static_cast<double>(d)); }
TDateTime __fastcall Time() { long d; double f; TDateTime n = Now(); Split(n, d, f); return TDateTime(f); }
int __fastcall DayOfWeek(const TDateTime & DT) { long d; double f; Split(DT, d, f); return static_cast<int>(((d + 6) % 7) + 1); }

TDateTime __fastcall IncMilliSecond(const TDateTime & DT, __int64 N) { return TDateTime((double)DT + (double)N / MSecsPerDay); }
TDateTime __fastcall IncSecond(const TDateTime & DT, __int64 N) { return IncMilliSecond(DT, N * 1000); }
TDateTime __fastcall IncMinute(const TDateTime & DT, __int64 N) { return IncSecond(DT, N * 60); }
TDateTime __fastcall IncHour(const TDateTime & DT, __int64 N) { return IncMinute(DT, N * 60); }
TDateTime __fastcall IncDay(const TDateTime & DT, int N) { return TDateTime((double)DT + N); }
TDateTime __fastcall IncYear(const TDateTime & DT, int N)
{ Word y,m,d,h,mi,s,ms; DecodeDate(DT,y,m,d); DecodeTime(DT,h,mi,s,ms);
  return EncodeDate(y + N, m, d) + EncodeTime(h, mi, s, ms); }

bool __fastcall IsSameDay(const TDateTime & A, const TDateTime & B)
{ long da,db; double f; Split(A,da,f); Split(B,db,f); return da == db; }
double __fastcall DaysBetween(const TDateTime & A, const TDateTime & B) { return std::fabs((double)A - (double)B); }
double __fastcall HoursBetween(const TDateTime & A, const TDateTime & B) { return DaysBetween(A,B) * 24; }
double __fastcall MinutesBetween(const TDateTime & A, const TDateTime & B) { return DaysBetween(A,B) * MinsPerDay; }
double __fastcall SecondsBetween(const TDateTime & A, const TDateTime & B) { return DaysBetween(A,B) * SecsPerDay; }
double __fastcall MonthsBetween(const TDateTime & A, const TDateTime & B) { return DaysBetween(A,B) / 30.4375; }
double __fastcall YearsBetween(const TDateTime & A, const TDateTime & B) { return DaysBetween(A,B) / 365.25; }
int __fastcall MilliSecondOfTheDay(const TDateTime & DT) { long d; double f; Split(DT,d,f); return static_cast<int>(std::llround(f * MSecsPerDay)); }
int __fastcall MilliSecondOfTheHour(const TDateTime & DT) { return MilliSecondOfTheDay(DT) % (60*60*1000); }
int __fastcall MilliSecondOfTheMinute(const TDateTime & DT) { return MilliSecondOfTheDay(DT) % (60*1000); }
int __fastcall MilliSecondOfTheSecond(const TDateTime & DT) { return MilliSecondOfTheDay(DT) % 1000; }
__int64 __fastcall MilliSecondOfTheYear(const TDateTime & DT) { Word y,m,d; DecodeDate(DT,y,m,d);
  return static_cast<__int64>((double)DT - (double)EncodeDate(y,1,1)) * MSecsPerDay + MilliSecondOfTheDay(DT); }

UnicodeString __fastcall FormatDateTime(const UnicodeString &, const TDateTime & DT)
{ Word y,m,d,h,mi,s,ms; DecodeDate(DT,y,m,d); DecodeTime(DT,h,mi,s,ms);
  return Format(L"%.4d-%.2d-%.2d %.2d:%.2d:%.2d", ARRAYOFCONST(((int)y,(int)m,(int)d,(int)h,(int)mi,(int)s))); }
UnicodeString __fastcall FormatFloat(const UnicodeString &, double Value) { return Format(L"%g", ARRAYOFCONST((Value))); }
TDateTime __fastcall SystemTimeToDateTime(const SYSTEMTIME & ST)
{ return EncodeDate(ST.wYear, ST.wMonth, ST.wDay) + EncodeTime(ST.wHour, ST.wMinute, ST.wSecond, ST.wMilliseconds); }
TTimeStamp __fastcall DateTimeToTimeStamp(const TDateTime & DT)
{ long d; double f; Split(DT,d,f); TTimeStamp ts; ts.Date = d + DateDelta; ts.Time = static_cast<int>(std::llround(f * MSecsPerDay)); return ts; }
bool __fastcall TryStrToDateTime(const UnicodeString &, TDateTime &) { return false; }
bool __fastcall TryStrToInt(const UnicodeString & S, int & Value)
{ std::string n; for (char16_t c : S.raw()) n.push_back((char)c); char * e=nullptr; long v=std::strtol(n.c_str(),&e,10);
  if (e==n.c_str()||*e) return false; Value=(int)v; return true; }
bool __fastcall TryStrToInt64(const UnicodeString & S, __int64 & Value)
{ std::string n; for (char16_t c : S.raw()) n.push_back((char)c); char * e=nullptr; long long v=std::strtoll(n.c_str(),&e,10);
  if (e==n.c_str()||*e) return false; Value=v; return true; }

//=== paths ===
UnicodeString __fastcall ExtractFilePath(const UnicodeString & FileName)
{ int p = FileName.LastDelimiter(UnicodeString(L"/\\:")); return (p > 0) ? FileName.SubString(1, p) : UnicodeString(); }
UnicodeString __fastcall ExtractFileDir(const UnicodeString & FileName)
{ int p = FileName.LastDelimiter(UnicodeString(L"/\\:")); if (p > 1) return FileName.SubString(1, p - 1);
  return (p == 1) ? FileName.SubString(1, 1) : UnicodeString(); }
UnicodeString __fastcall ExtractFileDrive(const UnicodeString &) { return UnicodeString(); }
UnicodeString __fastcall ChangeFileExt(const UnicodeString & FileName, const UnicodeString & Ext)
{ int p = FileName.LastDelimiter(UnicodeString(L"./\\:"));
  if (p > 0 && FileName[p] == L'.') return FileName.SubString(1, p - 1) + Ext;
  return FileName + Ext; }
UnicodeString __fastcall IncludeTrailingBackslash(const UnicodeString & S)
{ if (S.IsEmpty() || (S[S.Length()] != L'/' && S[S.Length()] != L'\\')) return S + UnicodeString(L"/"); return S; }
UnicodeString __fastcall IncludeTrailingPathDelimiter(const UnicodeString & S) { return IncludeTrailingBackslash(S); }
UnicodeString __fastcall ExcludeTrailingBackslash(const UnicodeString & S)
{ if (!S.IsEmpty() && (S[S.Length()] == L'/' || S[S.Length()] == L'\\')) return S.SubString(1, S.Length() - 1); return S; }
UnicodeString __fastcall ExpandFileName(const UnicodeString & FileName)
{ std::error_code ec; auto p = fs::absolute(ToU8(FileName), ec); return ec ? FileName : FromU8(p.string()); }
UnicodeString __fastcall ExtractShortPathName(const UnicodeString & FileName) { return FileName; }
UnicodeString __fastcall GetCurrentDir() { std::error_code ec; auto p = fs::current_path(ec); return FromU8(p.string()); }
bool __fastcall DirectoryExists(const UnicodeString & Dir) { std::error_code ec; return fs::is_directory(ToU8(Dir), ec); }
bool __fastcall FileExists(const UnicodeString & FileName) { std::error_code ec; return fs::exists(ToU8(FileName), ec) && !fs::is_directory(ToU8(FileName), ec); }
bool __fastcall DeleteFile(const UnicodeString & FileName) { std::error_code ec; return fs::remove(ToU8(FileName), ec); }
bool __fastcall RemoveDir(const UnicodeString & Dir) { std::error_code ec; return fs::remove(ToU8(Dir), ec); }
int  __fastcall GetFileAttributes(const wchar_t * FileName)
{ std::error_code ec; UnicodeString fn(FileName); auto st = fs::status(ToU8(fn), ec);
  if (ec || st.type() == fs::file_type::not_found) return -1;
  int a = 0; if (fs::is_directory(st)) a |= faDirectory; return a; }
UnicodeString __fastcall ExpandEnvironmentStrings(const UnicodeString & S) { return S; }
UnicodeString __fastcall GetEnvironmentVariable(const UnicodeString & Name)
{ const char * v = ::getenv(ToU8(Name).c_str()); return v ? FromU8(v) : UnicodeString(); }
bool __fastcall PathIsRelative(const wchar_t * Path) { UnicodeString p(Path); return p.IsEmpty() || (p[1] != L'/' && p[1] != L'\\'); }

//=== FindFirst/Next/Close (std::filesystem) ===
namespace {
struct FindState { std::vector<fs::directory_entry> entries; size_t pos = 0; };
std::map<int, FindState *> g_finds; int g_nextHandle = 1;
void Fill(TSearchRec & F, const fs::directory_entry & e)
{
  std::error_code ec;
  F.Name = FromU8(e.path().filename().string());
  F.Attr = e.is_directory(ec) ? faDirectory : 0;
  F.Size = e.is_regular_file(ec) ? static_cast<__int64>(e.file_size(ec)) : 0;
  std::memset(&F.FindData, 0, sizeof(F.FindData));
  F.FindData.dwFileAttributes = static_cast<DWORD>(F.Attr);
}
}
int __fastcall FindFirst(const UnicodeString & Path, int /*Attr*/, TSearchRec & F)
{
  UnicodeString dir = ExtractFilePath(Path);
  std::error_code ec;
  auto * st = new FindState();
  for (auto & e : fs::directory_iterator(ToU8(dir.IsEmpty() ? UnicodeString(L".") : dir), ec))
    st->entries.push_back(e);
  if (ec) { delete st; return -1; }
  int h = g_nextHandle++; g_finds[h] = st; F.FindHandle = h;
  return FindNext(F);
}
int __fastcall FindNext(TSearchRec & F)
{
  auto it = g_finds.find(F.FindHandle);
  if (it == g_finds.end()) return -1;
  FindState * st = it->second;
  if (st->pos >= st->entries.size()) return -1;
  Fill(F, st->entries[st->pos++]);
  return 0;
}
void __fastcall FindClose(TSearchRec & F)
{
  auto it = g_finds.find(F.FindHandle);
  if (it != g_finds.end()) { delete it->second; g_finds.erase(it); }
  F.FindHandle = -1;
}

//=== misc string ===
UnicodeString __fastcall UTF8ToString(const RawByteString & S)
{
  const std::string & b = S.raw(); std::u16string w;
  for (size_t i = 0; i < b.size(); )
  {
    unsigned char c = b[i];
    unsigned int cp; int n;
    if (c < 0x80) { cp = c; n = 1; }
    else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
    else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
    else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
    else { cp = c; n = 1; }
    for (int k = 1; k < n && i + k < b.size(); ++k) cp = (cp << 6) | (b[i+k] & 0x3F);
    i += n;
    if (cp < 0x10000) w.push_back(static_cast<char16_t>(cp));
    else { cp -= 0x10000; w.push_back(static_cast<char16_t>(0xD800 + (cp >> 10))); w.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF))); }
  }
  return UnicodeString(w);
}
UnicodeString __fastcall AnsiLowerCase(const UnicodeString & S) { return S.LowerCase(); }
UnicodeString __fastcall AnsiReplaceStr(const UnicodeString & Text, const UnicodeString & From, const UnicodeString & To)
{ return ReplaceStr(Text, From, To); }
UnicodeString __fastcall RightStr(const UnicodeString & S, int Count)
{ if (Count >= S.Length()) return S; return S.SubString(S.Length() - Count + 1, Count); }

//=== Win32 stubs ===
BOOL  __fastcall CloseHandle(HANDLE) { return TRUE; }
HANDLE __fastcall GetCurrentProcess() { return nullptr; }
DWORD __fastcall GetCurrentProcessId() { return static_cast<DWORD>(getpid()); }
HMODULE __fastcall GetModuleHandle(const wchar_t *) { return nullptr; }
HMODULE __fastcall LoadLibrary(const wchar_t *) { return nullptr; }
BOOL  __fastcall FreeLibrary(HMODULE) { return TRUE; }
void * __fastcall GetProcAddress(HMODULE, const char *) { return nullptr; }
void  __fastcall SetLastError(DWORD) {}
BOOL  __fastcall FileTimeToSystemTime(const FILETIME *, SYSTEMTIME * st) { if (st) std::memset(st, 0, sizeof(*st)); return TRUE; }
BOOL  __fastcall FileTimeToLocalFileTime(const FILETIME * ft, FILETIME * lft) { if (lft && ft) *lft = *ft; return TRUE; }
int   __fastcall StrCmpLogicalW(const wchar_t * a, const wchar_t * b) { return UnicodeString(a).CompareIC(UnicodeString(b)); }
void  __fastcall CoTaskMemFree(void *) {}

//=== TEncoding ===
static TEncoding g_utf8, g_ansi;
TEncoding * __fastcall TEncoding::UTF8() { return &g_utf8; }
TEncoding * __fastcall TEncoding::ANSI() { return &g_ansi; }
TEncoding * __fastcall TEncoding::Default() { return &g_utf8; }
RawByteString __fastcall TEncoding::GetBytes(const UnicodeString & S) { return RawByteString(UTF8String(S).c_str()); }
UnicodeString __fastcall TEncoding::GetString(const RawByteString & B) { return UTF8ToString(B); }
