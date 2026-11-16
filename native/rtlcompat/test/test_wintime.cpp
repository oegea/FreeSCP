//---------------------------------------------------------------------------
// Tests for the Win32 time API emulation (WinTime.cpp): FILETIME layout and conversions, the
// system time zone expressed as a TIME_ZONE_INFORMATION, and file timestamps.
// The zone rules asserted below are today's law for those zones (EU: last Sunday of March/October;
// US: second Sunday of March / first Sunday of November; India: no DST; New South Wales: first
// Sunday of October/April). If a government changes them, update the expectations.
//---------------------------------------------------------------------------
#include "SysUtils.hpp"
#include "winscp/SysExtra.h"
#include "winscp/WcsCompat.h"
#undef NDEBUG   // these tests ARE the asserts
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <algorithm>
#include <string>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static void SetZone(const char * tz) { setenv("TZ", tz, 1); tzset(); }

static SYSTEMTIME ST(int y, int mo, int d, int h, int mi, int s)
{
  SYSTEMTIME st{}; st.wYear = (Word)y; st.wMonth = (Word)mo; st.wDay = (Word)d;
  st.wHour = (Word)h; st.wMinute = (Word)mi; st.wSecond = (Word)s;
  return st;
}

int main()
{
  // --- FILETIME is 8 bytes and reads as one __int64 (the engine relies on both) ---
  assert(sizeof(FILETIME) == 8);
  {
    SYSTEMTIME st = ST(2026, 1, 15, 11, 34, 56);
    FILETIME ft;
    assert(SystemTimeToFileTime(&st, &ft));
    __int64 ticks = *reinterpret_cast<const __int64 *>(&ft);
    assert(ticks / 10000000LL - 11644473600LL == 1768476896LL);   // unix time of that UTC instant
    SYSTEMTIME back{};
    assert(FileTimeToSystemTime(&ft, &back));
    assert(back.wYear == 2026 && back.wMonth == 1 && back.wDay == 15 && back.wHour == 11 && back.wMinute == 34 && back.wSecond == 56);
    assert(back.wDayOfWeek == 4);   // a Thursday
  }
  {
    // before 1970 (negative unix time) and the FILETIME epoch itself
    SYSTEMTIME st = ST(1965, 3, 2, 4, 5, 6);
    FILETIME ft; assert(SystemTimeToFileTime(&st, &ft));
    SYSTEMTIME back{}; assert(FileTimeToSystemTime(&ft, &back));
    assert(back.wYear == 1965 && back.wMonth == 3 && back.wDay == 2 && back.wHour == 4 && back.wMinute == 5 && back.wSecond == 6);
    FILETIME zero; zero.dwLowDateTime = 0; zero.dwHighDateTime = 0;
    assert(FileTimeToSystemTime(&zero, &back));
    assert(back.wYear == 1601 && back.wMonth == 1 && back.wDay == 1 && back.wHour == 0);
  }

  // --- time zone rules ---
  TIME_ZONE_INFORMATION tzi;
  SetZone("Europe/Madrid");
  {
    DWORD id = GetTimeZoneInformation(&tzi);
    assert(id == TIME_ZONE_ID_STANDARD || id == TIME_ZONE_ID_DAYLIGHT);
    assert(tzi.Bias == -60 && tzi.StandardBias == 0 && tzi.DaylightBias == -60);
    assert(tzi.DaylightDate.wYear == 0 && tzi.DaylightDate.wMonth == 3 && tzi.DaylightDate.wDayOfWeek == 0 &&
           tzi.DaylightDate.wDay == 5 && tzi.DaylightDate.wHour == 2);
    assert(tzi.StandardDate.wYear == 0 && tzi.StandardDate.wMonth == 10 && tzi.StandardDate.wDayOfWeek == 0 &&
           tzi.StandardDate.wDay == 5 && tzi.StandardDate.wHour == 3);
    // UTC -> local uses the offset of THAT date
    SYSTEMTIME utc = ST(2026, 7, 15, 10, 0, 0), local{};
    assert(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));
    assert(local.wHour == 12 && local.wDay == 15);
    utc = ST(2026, 1, 15, 23, 30, 0);
    assert(SystemTimeToTzSpecificLocalTime(nullptr, &utc, &local));
    assert(local.wHour == 0 && local.wMinute == 30 && local.wDay == 16);
    // FILETIME <-> local FILETIME use the CURRENT offset (Win32 semantics), and are inverses
    FILETIME ft, lft, back; SYSTEMTIME st = ST(2026, 5, 1, 0, 0, 0);
    SystemTimeToFileTime(&st, &ft);
    assert(FileTimeToLocalFileTime(&ft, &lft) && LocalFileTimeToFileTime(&lft, &back));
    assert(back.dwLowDateTime == ft.dwLowDateTime && back.dwHighDateTime == ft.dwHighDateTime);
    __int64 diff = *reinterpret_cast<const __int64 *>(&lft) - *reinterpret_cast<const __int64 *>(&ft);
    assert(diff == 36000000000LL || diff == 72000000000LL);   // +1 h or +2 h
  }
  SetZone("America/New_York");
  {
    GetTimeZoneInformation(&tzi);
    assert(tzi.Bias == 300 && tzi.DaylightBias == -60);
    assert(tzi.DaylightDate.wMonth == 3 && tzi.DaylightDate.wDayOfWeek == 0 && tzi.DaylightDate.wDay == 2 && tzi.DaylightDate.wHour == 2);
    assert(tzi.StandardDate.wMonth == 11 && tzi.StandardDate.wDayOfWeek == 0 && tzi.StandardDate.wDay == 1 && tzi.StandardDate.wHour == 2);
  }
  SetZone("Asia/Kolkata");
  {
    assert(GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_UNKNOWN);
    assert(tzi.Bias == -330 && tzi.DaylightDate.wMonth == 0 && tzi.StandardDate.wMonth == 0);
  }
  SetZone("Australia/Sydney");   // southern hemisphere: daylight time spans the new year
  {
    GetTimeZoneInformation(&tzi);
    assert(tzi.Bias == -600 && tzi.DaylightBias == -60);
    assert(tzi.DaylightDate.wMonth == 10 && tzi.DaylightDate.wDay == 1 && tzi.DaylightDate.wHour == 2);
    assert(tzi.StandardDate.wMonth == 4 && tzi.StandardDate.wDay == 1 && tzi.StandardDate.wHour == 3);
  }
  SetZone("UTC");
  {
    assert(GetTimeZoneInformation(&tzi) == TIME_ZONE_ID_UNKNOWN);
    assert(tzi.Bias == 0);
  }

  // --- file timestamps through a CreateFile handle ---
  {
    char path[] = "/tmp/freescp-wintime-XXXXXX";
    int tmp = mkstemp(path); assert(tmp >= 0); close(tmp);
    UnicodeString name; for (const char * p = path; *p; ++p) name += UnicodeString((wchar_t)*p, 1);
    HANDLE h = CreateFile(name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    assert(h != INVALID_HANDLE_VALUE);
    SYSTEMTIME st = ST(2021, 6, 7, 8, 9, 10);
    FILETIME want; SystemTimeToFileTime(&st, &want);
    assert(SetFileTime(h, nullptr, nullptr, &want));
    FILETIME got;
    assert(GetFileTime(h, nullptr, nullptr, &got));
    assert(got.dwLowDateTime == want.dwLowDateTime && got.dwHighDateTime == want.dwHighDateTime);
    CloseHandle(h);
    struct stat sb; assert(stat(path, &sb) == 0);
    assert(sb.st_mtime == 1623053350);   // 2021-06-07 08:09:10 UTC
    // the same value is what FindFirst reports
    TSearchRec rec;
    assert(FindFirst(name, faAnyFile, rec) == 0);
    assert(rec.FindData.ftLastWriteTime.dwLowDateTime == want.dwLowDateTime &&
           rec.FindData.ftLastWriteTime.dwHighDateTime == want.dwHighDateTime);
    FindClose(rec);
    unlink(path);
    FILETIME dummy;
    assert(!GetFileTime(reinterpret_cast<HANDLE>((intptr_t)-1), nullptr, nullptr, &dummy));   // bad handle fails, no garbage
  }

  // --- FindFirst honors the name / mask (it used to return the directory's first entry for any name) ---
  {
    char dir[] = "/tmp/freescp-find-XXXXXX";
    assert(mkdtemp(dir) != nullptr);
    std::string d(dir);
    auto touch = [&](const char * n) { int fd = open((d + "/" + n).c_str(), O_CREAT | O_WRONLY, 0644); assert(fd >= 0); if (write(fd, "xy", 2) != 2) {} close(fd); };
    touch("alpha.txt"); touch("beta.log"); touch("gamma"); mkdir((d + "/sub").c_str(), 0755);
    auto U = [](const std::string & s8) { UnicodeString u; for (char c : s8) u += UnicodeString((wchar_t)c, 1); return u; };
    auto names = [&](const std::string & pattern) {
      std::string out; TSearchRec r;
      if (FindFirst(U(d + "/" + pattern), faAnyFile, r) == 0)
      {
        std::vector<std::string> v;
        do { std::string n; for (char16_t c : r.Name.raw()) n.push_back((char)c); v.push_back(n); } while (FindNext(r) == 0);
        FindClose(r);
        std::sort(v.begin(), v.end());
        for (auto & n : v) out += n + ",";
      }
      return out;
    };
    assert(names("beta.log") == "beta.log,");
    assert(names("sub") == "sub,");
    assert(names("missing.txt") == "");
    assert(names("*") == ".,..,alpha.txt,beta.log,gamma,sub,");        // with the dot entries, like FindFirstFile
    assert(names("*.*") == ".,..,alpha.txt,beta.log,gamma,sub,");
    assert(names("sub/*") == ".,..,");                                    // an empty directory still "has" them
    assert(names("*.txt") == "alpha.txt,");
    assert(names("?eta.*") == "beta.log,");
    assert(names("gamma.*") == "gamma,");
    assert(names("*.zip") == "");
    { TSearchRec r; assert(FindFirst(U(d + "\\*"), faAnyFile, r) == 0); FindClose(r); }   // "<dir>\\*" form
    { TSearchRec r; assert(FindFirst(U(d + "/beta.log"), faAnyFile, r) == 0);
      assert(r.Size == 2 && (r.Attr & faDirectory) == 0); FindClose(r); }
    { TSearchRec r; assert(FindFirst(U(d + "/sub"), faAnyFile, r) == 0); assert((r.Attr & faDirectory) != 0); FindClose(r); }
    for (const char * n : { "alpha.txt", "beta.log", "gamma" }) unlink((d + "/" + n).c_str());
    rmdir((d + "/sub").c_str()); rmdir(dir);
  }

  // --- a time a rounding error before midnight is midnight of that day, not 24:00:00 of the day before ---
  {
    TDateTime midnight = EncodeDate(2026, 1, 1);
    for (double eps : { 0.0, 1e-11, -1e-11, 4e-9, -4e-9 })
    {
      TDateTime t(static_cast<double>(midnight) + eps);
      Word y, m, d, h, mi, s, ms;
      DecodeDate(t, y, m, d); DecodeTime(t, h, mi, s, ms);
      assert(y == 2026 && m == 1 && d == 1 && h == 0 && mi == 0 && s == 0 && ms == 0);
    }
    TDateTime t = EncodeDate(2026, 3, 4) + EncodeTime(23, 59, 59, 999);
    Word y, m, d, h, mi, s, ms;
    DecodeDate(t, y, m, d); DecodeTime(t, h, mi, s, ms);
    assert(y == 2026 && m == 3 && d == 4 && h == 23 && mi == 59 && s == 59 && ms == 999);
  }

  // --- wcstol / wcstoul over 2-byte wchar_t (through the WcsCompat shims) ---
  {
    wchar_t * end = nullptr;
    assert(winscp_wcstol(L"2026", &end, 10) == 2026 && *end == 0);
    assert(winscp_wcstol(L"11x", &end, 10) == 11 && *end == L'x');
    assert(winscp_wcstol(L"-42", nullptr, 10) == -42);
    assert(winscp_wcstol(L"", &end, 10) == 0 && *end == 0);
    assert(winscp_wcstol(L"abc", &end, 10) == 0 && *end == L'a');
    assert(winscp_wcstoul(L"ff", nullptr, 16) == 255);
  }

  // --- Random is not a constant ---
  {
    bool varied = false; int first = Random(1000);
    for (int i = 0; i < 50; ++i) { int r = Random(1000); assert(r >= 0 && r < 1000); if (r != first) varied = true; }
    assert(varied);
    assert(Random(0) == 0);
  }

  std::printf("wintime: all assertions passed\n");
  return 0;
}
