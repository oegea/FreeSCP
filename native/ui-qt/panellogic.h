//---------------------------------------------------------------------------
// panellogic.h — pure logic of the file panels (no Qt, no engine), unit-tested in
// tests/logic_test.cpp: the path drop-down, recent directories, date patterns, permission strings.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_PANELLOGIC_H
#define WINSCP_UIQT_PANELLOGIC_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace panellogic {

struct PathStep { std::string label, path; };

// "/a/b/c" -> { "/", "/" }, { "a", "/a" }, { "b", "/a/b" }, { "c", "/a/b/c" }.
// Repeated and trailing slashes are ignored; an empty or relative path gives just the root.
inline std::vector<PathStep> pathHierarchy(const std::string & path)
{
  std::vector<PathStep> out;
  out.push_back(PathStep{ "/", "/" });
  if (path.empty() || path[0] != '/') return out;
  std::string cur;
  size_t i = 0;
  while (i < path.size())
  {
    while (i < path.size() && path[i] == '/') ++i;
    size_t j = i;
    while (j < path.size() && path[j] != '/') ++j;
    if (j > i)
    {
      const std::string part = path.substr(i, j - i);
      cur += "/" + part;
      out.push_back(PathStep{ part, cur });
    }
    i = j;
  }
  return out;
}

// The path without repeated or trailing slashes ("/" stays "/").
inline std::string normalizePath(const std::string & path)
{
  const std::vector<PathStep> steps = pathHierarchy(path);
  return steps.back().path;
}

// Most-recently-used list: `path` moves to the front, duplicates go, at most `max` entries stay.
inline void pushRecent(std::vector<std::string> & list, const std::string & path, size_t max)
{
  if (path.empty()) return;
  for (size_t i = 0; i < list.size(); )
    if (list[i] == path) list.erase(list.begin() + static_cast<std::ptrdiff_t>(i)); else ++i;
  list.insert(list.begin(), path);
  if (list.size() > max) list.resize(max);
}

// A locale's short date pattern with a four-digit year ("d/M/yy" -> "d/M/yyyy"); quoted text is kept.
inline std::string fourDigitYear(const std::string & pattern)
{
  std::string out; bool quoted = false;
  for (size_t i = 0; i < pattern.size(); )
  {
    const char c = pattern[i];
    if (c == '\'') { quoted = !quoted; out += c; ++i; continue; }
    if (!quoted && c == 'y')
    {
      size_t j = i; while (j < pattern.size() && pattern[j] == 'y') ++j;
      out += "yyyy"; i = j; continue;
    }
    out += c; ++i;
  }
  return out;
}

// A locale's short time pattern with seconds added after the minutes ("H:mm" -> "H:mm:ss",
// "h:mm AP" -> "h:mm:ss AP"); unchanged if it has seconds already or no minutes.
inline std::string withSeconds(const std::string & pattern)
{
  bool quoted = false; size_t lastMinute = std::string::npos;
  for (size_t i = 0; i < pattern.size(); ++i)
  {
    const char c = pattern[i];
    if (c == '\'') { quoted = !quoted; continue; }
    if (quoted) continue;
    if (c == 's') return pattern;
    if (c == 'm') lastMinute = i;
  }
  if (lastMinute == std::string::npos) return pattern;
  std::string out = pattern;
  out.insert(lastMinute + 1, ":ss");
  return out;
}

// The engine gives timestamps as seconds of a TDateTime (days since 1899-12-30, local time).
// -> seconds since 1970-01-01 of that same wall-clock time.
inline std::int64_t wallSecondsSince1970(std::int64_t engineSecs) { return engineSecs - 25569LL * 86400LL; }

// "rwxr-xr-x" -> 0755. Special bits: s/S (setuid, setgid), t/T (sticky). -1 if not a rights string.
inline int octalFromRights(const std::string & rights)
{
  if (rights.size() != 9) return -1;
  int v = 0;
  for (int i = 0; i < 9; ++i)
  {
    const char c = rights[static_cast<size_t>(i)];
    const int bit = 1 << (8 - i);
    const int pos = i % 3;
    if (c == '-') continue;
    if (pos == 0 && c == 'r') { v |= bit; continue; }
    if (pos == 1 && c == 'w') { v |= bit; continue; }
    if (pos == 2)
    {
      const int special = (i == 2) ? 04000 : (i == 5) ? 02000 : 01000;
      if (c == 'x') { v |= bit; continue; }
      if ((i < 6 && c == 's') || (i == 8 && c == 't')) { v |= bit | special; continue; }
      if ((i < 6 && c == 'S') || (i == 8 && c == 'T')) { v |= special; continue; }
    }
    return -1;
  }
  return v;
}

// 0755 -> "rwxr-xr-x" (with s/S/t/T for the special bits).
inline std::string rightsFromOctal(int v)
{
  std::string s = "---------";
  const char letters[3] = { 'r', 'w', 'x' };
  for (int i = 0; i < 9; ++i) if (v & (1 << (8 - i))) s[static_cast<size_t>(i)] = letters[i % 3];
  if (v & 04000) s[2] = (s[2] == 'x') ? 's' : 'S';
  if (v & 02000) s[5] = (s[5] == 'x') ? 's' : 'S';
  if (v & 01000) s[8] = (s[8] == 'x') ? 't' : 'T';
  return s;
}

// Three or four octal digits ("644", "0755", "4755").
inline bool validOctal(const std::string & s)
{
  if (s.size() != 3 && s.size() != 4) return false;
  for (char c : s) if (c < '0' || c > '7') return false;
  return true;
}

// 0755 -> "755", 04755 -> "4755".
inline std::string octalString(int v)
{
  std::string s;
  if (v & 07000) s += static_cast<char>('0' + ((v >> 9) & 7));
  s += static_cast<char>('0' + ((v >> 6) & 7));
  s += static_cast<char>('0' + ((v >> 3) & 7));
  s += static_cast<char>('0' + (v & 7));
  return s;
}

// Session time for the status bar, like WinSCP: "0:00:07", "1:02:03", "27:15:00".
inline std::string elapsedText(std::int64_t secs)
{
  if (secs < 0) secs = 0;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", static_cast<long long>(secs / 3600),
                static_cast<long long>((secs / 60) % 60), static_cast<long long>(secs % 60));
  return buf;
}

} // namespace panellogic

#endif
