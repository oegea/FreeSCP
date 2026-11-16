//---------------------------------------------------------------------------
// WinTime.cpp — Win32 time API over POSIX: FILETIME <-> SYSTEMTIME, local/UTC conversion, the
// system time zone as a TIME_ZONE_INFORMATION, and reading/writing a file's timestamps.
//
// The engine does all its remote<->local timestamp math (Common.cpp: UnixToDateTime,
// DateTimeToFileTime, ConvertTimestampToUnix, IsDateInDST, ...) on top of these, exactly as on
// Windows. They used to be stubs ("local == UTC", GetFileTime = zeros, SetFileTime = no-op), with
// three visible effects: remote listings were shown in UTC, every upload stamped the remote file
// with 1601-01-01 (2009-04-22 once truncated to the 32-bit SFTP field) and downloads kept "now".
//
// Conventions (same as Win32):
//   FILETIME    100 ns ticks since 1601-01-01 UTC, as two 32-bit halves (8 bytes — the engine
//               reinterprets it as an __int64).
//   SYSTEMTIME  broken-down time; UTC unless the API name says "local".
//---------------------------------------------------------------------------
#include "SysUtils.hpp"
#include "winscp/SysExtra.h"
#include <cerrno>
#include <cstring>
#include <ctime>
#include <mutex>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

const long long kTicksPerSec = 10000000LL;
const long long kEpochDiffSec = 11644473600LL;   // 1601-01-01 -> 1970-01-01

long long TicksOf(const FILETIME & ft)
{
  return (long long)(((unsigned long long)ft.dwHighDateTime << 32) | (unsigned long long)ft.dwLowDateTime);
}
FILETIME FileTimeOf(long long ticks)
{
  FILETIME ft;
  ft.dwLowDateTime = (unsigned int)((unsigned long long)ticks & 0xFFFFFFFFULL);
  ft.dwHighDateTime = (unsigned int)((unsigned long long)ticks >> 32);
  return ft;
}
// floor division: ticks before 1970 must still split into (seconds, non-negative remainder)
void SplitTicks(long long ticks, time_t & secs, long & hundredNs)
{
  long long s = ticks / kTicksPerSec, r = ticks % kTicksPerSec;
  if (r < 0) { r += kTicksPerSec; --s; }
  secs = (time_t)(s - kEpochDiffSec);
  hundredNs = (long)r;
}
void TmToSystemTime(const struct tm & t, long hundredNs, SYSTEMTIME & st)
{
  st.wYear = (Word)(t.tm_year + 1900); st.wMonth = (Word)(t.tm_mon + 1); st.wDay = (Word)t.tm_mday;
  st.wDayOfWeek = (Word)t.tm_wday;
  st.wHour = (Word)t.tm_hour; st.wMinute = (Word)t.tm_min; st.wSecond = (Word)t.tm_sec;
  st.wMilliseconds = (Word)(hundredNs / 10000);
}
struct tm SystemTimeToTm(const SYSTEMTIME & st)
{
  struct tm t; std::memset(&t, 0, sizeof(t));
  t.tm_year = st.wYear - 1900; t.tm_mon = st.wMonth - 1; t.tm_mday = st.wDay;
  t.tm_hour = st.wHour; t.tm_min = st.wMinute; t.tm_sec = st.wSecond;
  return t;
}
long GmtOffset(time_t t, bool * isDst = nullptr)
{
  struct tm lt; std::memset(&lt, 0, sizeof(lt));
  localtime_r(&t, &lt);
  if (isDst != nullptr) *isDst = (lt.tm_isdst > 0);
  return (long)lt.tm_gmtoff;
}
int DaysInMonth(int year, int month)   // month 1..12
{
  static const int d[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
  return (month == 2 && leap) ? 29 : d[month - 1];
}
// A DST transition as Windows expresses it: "the Nth <weekday> of <month> at hh:mm", local wall
// time in effect just BEFORE the change (N = 5 means the last one). wYear = 0 marks the relative form.
SYSTEMTIME TransitionRule(time_t instant, long offsetBefore)
{
  time_t wall = instant + offsetBefore;
  struct tm t; std::memset(&t, 0, sizeof(t));
  gmtime_r(&wall, &t);
  SYSTEMTIME st; std::memset(&st, 0, sizeof(st));
  st.wYear = 0;
  st.wMonth = (Word)(t.tm_mon + 1);
  st.wDayOfWeek = (Word)t.tm_wday;
  int nth = (t.tm_mday - 1) / 7 + 1;
  if (t.tm_mday + 7 > DaysInMonth(t.tm_year + 1900, t.tm_mon + 1)) nth = 5;
  st.wDay = (Word)nth;
  st.wHour = (Word)t.tm_hour; st.wMinute = (Word)t.tm_min; st.wSecond = (Word)t.tm_sec;
  return st;
}
void CopyName(wchar_t * dst, size_t cap, const char * src)
{
  size_t i = 0;
  if (src != nullptr) for (; src[i] != 0 && i + 1 < cap; ++i) dst[i] = (wchar_t)(unsigned char)src[i];
  dst[i] = 0;
}
int FdOf(HANDLE h) { return (int)reinterpret_cast<intptr_t>(h); }

#ifdef __APPLE__
#define ST_MTIM st_mtimespec
#define ST_ATIM st_atimespec
#define ST_CTIM st_birthtimespec   // creation time
#else
#define ST_MTIM st_mtim
#define ST_ATIM st_atim
#define ST_CTIM st_ctim            // no portable creation time: status-change time is the closest
#endif

} // namespace

// Shared with FindFirst/FindNext (SysExtra.cpp), which fills TSearchRec::FindData.
FILETIME winscp_timespec_to_filetime(const struct timespec & ts)
{
  return FileTimeOf(((long long)ts.tv_sec + kEpochDiffSec) * kTicksPerSec + ts.tv_nsec / 100);
}
void winscp_stat_to_filetimes(const struct stat & st, FILETIME * creation, FILETIME * access, FILETIME * write)
{
  if (creation != nullptr) *creation = winscp_timespec_to_filetime(st.ST_CTIM);
  if (access != nullptr) *access = winscp_timespec_to_filetime(st.ST_ATIM);
  if (write != nullptr) *write = winscp_timespec_to_filetime(st.ST_MTIM);
}

BOOL __fastcall FileTimeToSystemTime(const FILETIME * ft, SYSTEMTIME * st)
{
  if (ft == nullptr || st == nullptr) return FALSE;
  time_t secs; long rem; SplitTicks(TicksOf(*ft), secs, rem);
  struct tm t; std::memset(&t, 0, sizeof(t));
  if (gmtime_r(&secs, &t) == nullptr) return FALSE;
  TmToSystemTime(t, rem, *st);
  return TRUE;
}

BOOL __fastcall SystemTimeToFileTime(const SYSTEMTIME * st, FILETIME * ft)
{
  if (st == nullptr || ft == nullptr) return FALSE;
  struct tm t = SystemTimeToTm(*st);
  time_t secs = ::timegm(&t);
  *ft = FileTimeOf(((long long)secs + kEpochDiffSec) * kTicksPerSec + (long long)st->wMilliseconds * 10000LL);
  return TRUE;
}

// Win32: both use the CURRENT bias (including whether DST is in effect now), not the one that
// applied at the time being converted.
BOOL __fastcall FileTimeToLocalFileTime(const FILETIME * ft, FILETIME * lft)
{
  if (ft == nullptr || lft == nullptr) return FALSE;
  *lft = FileTimeOf(TicksOf(*ft) + (long long)GmtOffset(time(nullptr)) * kTicksPerSec);
  return TRUE;
}
BOOL __fastcall LocalFileTimeToFileTime(const FILETIME * lft, FILETIME * ft)
{
  if (ft == nullptr || lft == nullptr) return FALSE;
  *ft = FileTimeOf(TicksOf(*lft) - (long long)GmtOffset(time(nullptr)) * kTicksPerSec);
  return TRUE;
}

// UTC -> local wall time, with the DST state that applies to that moment (zone = the system's).
BOOL __fastcall SystemTimeToTzSpecificLocalTime(TIME_ZONE_INFORMATION *, SYSTEMTIME * in, SYSTEMTIME * out)
{
  if (in == nullptr || out == nullptr) return FALSE;
  struct tm t = SystemTimeToTm(*in);
  time_t secs = ::timegm(&t);
  struct tm lt; std::memset(&lt, 0, sizeof(lt));
  if (localtime_r(&secs, &lt) == nullptr) return FALSE;
  TmToSystemTime(lt, (long)in->wMilliseconds * 10000L, *out);
  return TRUE;
}

// The system zone in Windows terms: Bias (minutes; UTC = local + Bias), the daylight bias and the
// two yearly transition rules. Derived from the zone database by locating this year's offset
// changes. A zone without DST returns TIME_ZONE_ID_UNKNOWN with both rules empty (wMonth = 0),
// which the engine treats as "no daylight saving".
DWORD __fastcall GetTimeZoneInformation(TIME_ZONE_INFORMATION * tzi)
{
  if (tzi == nullptr) return TIME_ZONE_ID_INVALID;
  static std::mutex mtx;
  std::lock_guard<std::mutex> lock(mtx);
  tzset();
  *tzi = TIME_ZONE_INFORMATION();

  time_t now = time(nullptr);
  struct tm nowUtc; std::memset(&nowUtc, 0, sizeof(nowUtc));
  gmtime_r(&now, &nowUtc);
  struct tm jan; std::memset(&jan, 0, sizeof(jan));
  jan.tm_year = nowUtc.tm_year; jan.tm_mon = 0; jan.tm_mday = 1;
  const time_t yearStart = ::timegm(&jan);
  jan.tm_year += 1;
  const time_t yearEnd = ::timegm(&jan);

  bool nowDst = false;
  const long nowOff = GmtOffset(now, &nowDst);
  long stdOff = nowOff, dstOff = nowOff;
  bool haveToDst = false, haveToStd = false;
  time_t toDst = 0, toStd = 0;
  long beforeToDst = 0, beforeToStd = 0;

  const time_t day = 86400;
  bool prevDst = false;
  long prevOff = GmtOffset(yearStart, &prevDst);
  for (time_t t = yearStart; t < yearEnd; t += day)
  {
    bool curDst = false;
    long curOff = GmtOffset(t + day, &curDst);
    if (curDst != prevDst)
    {
      // binary search the first second in (t, t + day] with the new state
      time_t lo = t, hi = t + day;
      while (hi - lo > 1)
      {
        time_t mid = lo + (hi - lo) / 2;
        bool midDst = false; GmtOffset(mid, &midDst);
        if (midDst == prevDst) lo = mid; else hi = mid;
      }
      if (curDst) { if (!haveToDst) { haveToDst = true; toDst = hi; beforeToDst = prevOff; dstOff = curOff; stdOff = prevOff; } }
      else        { if (!haveToStd) { haveToStd = true; toStd = hi; beforeToStd = prevOff; stdOff = curOff; dstOff = prevOff; } }
    }
    prevDst = curDst; prevOff = curOff;
  }

  CopyName(tzi->StandardName, 32, tzname[0]);
  CopyName(tzi->DaylightName, 32, tzname[1]);
  if (!haveToDst || !haveToStd)
  {
    tzi->Bias = (LONG)(-nowOff / 60);
    return TIME_ZONE_ID_UNKNOWN;
  }
  tzi->Bias = (LONG)(-stdOff / 60);
  tzi->StandardBias = 0;
  tzi->DaylightBias = (LONG)(-(dstOff - stdOff) / 60);
  tzi->DaylightDate = TransitionRule(toDst, beforeToDst);
  tzi->StandardDate = TransitionRule(toStd, beforeToStd);
  return nowDst ? TIME_ZONE_ID_DAYLIGHT : TIME_ZONE_ID_STANDARD;
}

// HANDLE = a file descriptor (see CreateFile in SysExtra.cpp).
BOOL __fastcall GetFileTime(HANDLE h, FILETIME * creation, FILETIME * access, FILETIME * write)
{
  struct stat st;
  if (::fstat(FdOf(h), &st) != 0) return FALSE;
  winscp_stat_to_filetimes(st, creation, access, write);
  return TRUE;
}

BOOL __fastcall SetFileTime(HANDLE h, const FILETIME * /*creation*/, const FILETIME * access, const FILETIME * write)
{
  struct timespec ts[2];
  const FILETIME * src[2] = { access, write };
  for (int i = 0; i < 2; ++i)
  {
    if (src[i] == nullptr) { ts[i].tv_sec = 0; ts[i].tv_nsec = UTIME_OMIT; continue; }
    time_t secs; long rem; SplitTicks(TicksOf(*src[i]), secs, rem);
    ts[i].tv_sec = secs; ts[i].tv_nsec = rem * 100;
  }
  return (::futimens(FdOf(h), ts) == 0) ? TRUE : FALSE;
}
