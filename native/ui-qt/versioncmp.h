//---------------------------------------------------------------------------
// versioncmp.h — compare release version strings ("v0.3.0", "0.10", "1.2.3-beta"). No Qt.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_VERSIONCMP_H
#define WINSCP_UIQT_VERSIONCMP_H

#include <string>
#include <vector>

// The numeric components of a version: "v0.3.0" -> {0,3,0}. An optional leading 'v'/'V' is skipped
// and parsing stops at the first character that is neither a digit nor a dot ("1.2.3-beta" -> {1,2,3}).
// Empty when the string does not start with a number (so garbage is never "newer").
inline std::vector<long> versionParts(const std::string & s)
{
  std::vector<long> parts;
  size_t i = 0;
  if (i < s.size() && (s[i] == 'v' || s[i] == 'V')) ++i;
  bool inNumber = false; long cur = 0; int digits = 0;
  for (; i < s.size(); ++i)
  {
    const char c = s[i];
    if (c >= '0' && c <= '9')
    {
      if (++digits > 9) return {};          // not a version number
      cur = cur * 10 + (c - '0'); inNumber = true;
    }
    else if (c == '.' && inNumber) { parts.push_back(cur); cur = 0; digits = 0; inNumber = false; }
    else break;
  }
  if (inNumber) parts.push_back(cur);
  return parts;
}

// True when `candidate` is a later version than `current`. Missing components count as 0
// ("0.3" == "0.3.0"); an unparsable candidate is never newer.
inline bool versionIsNewer(const std::string & candidate, const std::string & current)
{
  const std::vector<long> a = versionParts(candidate), b = versionParts(current);
  if (a.empty()) return false;
  const size_t n = a.size() > b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i)
  {
    const long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
    if (x != y) return x > y;
  }
  return false;
}

#endif
