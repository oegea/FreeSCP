//---------------------------------------------------------------------------
// Regression test for Delphi Format (the engine's most-used RTL function).
//---------------------------------------------------------------------------
#include "SysUtils.hpp"
#define FORMAT(S, F) Format(S, ARRAYOFCONST(F))
#include <cassert>
#include <cstdio>
#include <string>

static std::string nb(const UnicodeString & s)
{ std::string r; for (char16_t c : s.raw()) r.push_back(static_cast<char>(c)); return r; }

int main()
{
  assert(nb(FORMAT(L"%s=%d", (UnicodeString(L"x"), 42))) == "x=42");
  assert(nb(FORMAT(L"%2.2d", (5))) == "05");
  assert(nb(FORMAT(L"%02d", (7))) == "07");
  assert(nb(FORMAT(L"[%-8s]", (UnicodeString(L"hi")))) == "[hi      ]");
  assert(nb(FORMAT(L"[%8s]", (UnicodeString(L"hi")))) == "[      hi]");
  assert(nb(FORMAT(L"%x", (255))) == "ff");
  assert(nb(FORMAT(L"%X", (255))) == "FF");
  assert(nb(FORMAT(L"%u", (7u))) == "7");
  assert(nb(FORMAT(L"100%%", ())) == "100%");
  assert(nb(FORMAT(L"%s [%d-%2.2d-%2.2d]", (UnicodeString(L"D"), 2026, 6, 9))) == "D [2026-06-09]");
  assert(nb(FORMAT(L"%1:d %0:d", (1, 2))) == "2 1");  // positional
  std::printf("format: all assertions passed\n");
  return 0;
}
