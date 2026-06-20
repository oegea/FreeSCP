//---------------------------------------------------------------------------
// enginebridge.cpp — the ONLY UI TU compiled with engine flags (-fshort-wchar -fms-extensions).
// Calls the ported engine/rtlcompat and converts UnicodeString <-> UTF-8 std::string.
//---------------------------------------------------------------------------
#include "enginebridge.h"

#include <vcl.h>              // ported RTL + platform layer (rtlcompat): UnicodeString,
                              // FindFirst/TSearchRec, path helpers, GetEnvironmentVariable.
#include <algorithm>
#include <cstdio>

namespace {

std::string ToU8(const UnicodeString & s) { return std::string(UTF8String(s).c_str()); }
UnicodeString FromU8(const std::string & s) { return UTF8ToString(RawByteString(s.c_str())); }

} // namespace

namespace engine {

std::string banner()
{
  return "WinSCP — native port (engine RTL + platform layer; local FS via ported FindFirst)";
}

std::string homeDir()
{
  UnicodeString h = GetEnvironmentVariable(UnicodeString(L"HOME"));
  return ToU8(h.IsEmpty() ? UnicodeString(L"/") : h);
}

std::string parentDir(const std::string & utf8Path)
{
  UnicodeString p = ExcludeTrailingBackslash(FromU8(utf8Path));
  int slash = p.LastDelimiter(UnicodeString(L"/"));
  if (slash <= 1) return "/";
  return ToU8(p.SubString(1, slash - 1));
}

std::string joinPath(const std::string & dir, const std::string & name)
{
  return ToU8(IncludeTrailingBackslash(FromU8(dir)) + FromU8(name));
}

std::string formatSize(std::int64_t bytes)
{
  const char * unit[] = { "B", "KiB", "MiB", "GiB", "TiB" };
  double v = static_cast<double>(bytes); int u = 0;
  while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
  char buf[48];
  std::snprintf(buf, sizeof(buf), (u == 0 ? "%.0f %s" : "%.1f %s"), v, unit[u]);
  return buf;
}

std::vector<DirEntry> listLocalDir(const std::string & utf8Path)
{
  std::vector<DirEntry> result;
  UnicodeString path = IncludeTrailingBackslash(FromU8(utf8Path));

  if (FromU8(utf8Path) != UnicodeString(L"/"))
  {
    DirEntry up; up.name = ".."; up.isDir = true; up.isParent = true;
    result.push_back(up);
  }

  TSearchRec rec;
  if (FindFirst(path + UnicodeString(L"*"), faAnyFile, rec) == 0)
  {
    do
    {
      UnicodeString n = rec.Name;
      if (n == UnicodeString(L".") || n == UnicodeString(L"..")) continue;
      DirEntry e;
      e.name = ToU8(n);
      e.isDir = (rec.Attr & faDirectory) != 0;
      e.size = rec.Size;
      result.push_back(e);
    }
    while (FindNext(rec) == 0);
    FindClose(rec);
  }

  std::sort(result.begin() + (result.empty() ? 0 : (result[0].isParent ? 1 : 0)), result.end(),
            [](const DirEntry & a, const DirEntry & b) {
              if (a.isDir != b.isDir) return a.isDir > b.isDir;     // dirs first
              return a.name < b.name;
            });
  return result;
}

} // namespace engine
