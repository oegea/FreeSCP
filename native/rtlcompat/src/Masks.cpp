//---------------------------------------------------------------------------
// Masks.cpp — System::Masks::TMask glob matcher (*, ?, [set]/[!set]). Case-insensitive, as
// Delphi's TMask is. The engine mostly uses its own TFileMasks but falls back to this base.
//---------------------------------------------------------------------------
#include "Masks.hpp"

namespace {
  wchar_t lower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? (wchar_t)(c + 32) : c; }

  // glob match over 1-based UnicodeString ranges, backtracking on '*'.
  bool match(const UnicodeString & pat, int pi, const UnicodeString & s, int si)
  {
    int pn = pat.Length(), sn = s.Length();
    while (pi <= pn)
    {
      wchar_t pc = pat[pi];
      if (pc == L'*')
      {
        while (pi <= pn && pat[pi] == L'*') ++pi;        // collapse runs
        if (pi > pn) return true;                         // trailing '*' matches the rest
        for (int k = si; k <= sn + 1; ++k)
          if (match(pat, pi, s, k)) return true;
        return false;
      }
      if (si > sn) return false;
      if (pc == L'?')
      {
        ++pi; ++si; continue;
      }
      if (pc == L'[')
      {
        int j = pi + 1; bool neg = false;
        if (j <= pn && (pat[j] == L'!' || pat[j] == L'^')) { neg = true; ++j; }
        bool hit = false; wchar_t sc = lower(s[si]);
        while (j <= pn && pat[j] != L']')
        {
          if (j + 2 <= pn && pat[j + 1] == L'-' && pat[j + 2] != L']')
          { if (sc >= lower(pat[j]) && sc <= lower(pat[j + 2])) hit = true; j += 3; }
          else { if (sc == lower(pat[j])) hit = true; ++j; }
        }
        if (j <= pn) ++j;                                 // skip ']'
        if (hit == neg) return false;
        pi = j; ++si; continue;
      }
      if (lower(pc) != lower(s[si])) return false;
      ++pi; ++si;
    }
    return si > sn;
  }
}

bool __fastcall Masks::TMask::Matches(const UnicodeString & Filename)
{
  return match(FMask, 1, Filename, 1);
}
