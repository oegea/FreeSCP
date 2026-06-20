//---------------------------------------------------------------------------
// SysStrFuncs.cpp — System.SysUtils free string functions (subset). ASCII-correct;
// full Unicode case/locale folding deferred to the platform layer.
//---------------------------------------------------------------------------
#include "winscp/SysStrFuncs.h"
#include <string>
#include <cstdlib>

// UTF-16 -> narrow (ASCII range) for numeric parsing.
static std::string Narrow(const UnicodeString & S)
{
  const std::u16string & w = S.raw();
  std::string r; r.reserve(w.size());
  for (char16_t c : w) r.push_back(static_cast<char>(c));
  return r;
}

UnicodeString __fastcall IntToStr(__int64 Value) { return UnicodeString(Value); }

UnicodeString __fastcall IntToHex(__int64 Value, int Digits)
{
  static const char * D = "0123456789ABCDEF";
  std::string s;
  unsigned long long v = static_cast<unsigned long long>(Value);
  do { s.insert(s.begin(), D[v & 0xF]); v >>= 4; } while (v != 0);
  while (static_cast<int>(s.size()) < Digits) s.insert(s.begin(), '0');
  return UnicodeString(s.c_str());
}

int __fastcall StrToInt(const UnicodeString & S) { return static_cast<int>(std::strtol(Narrow(S).c_str(), nullptr, 10)); }
__int64 __fastcall StrToInt64(const UnicodeString & S) { return std::strtoll(Narrow(S).c_str(), nullptr, 10); }

int __fastcall StrToIntDef(const UnicodeString & S, int Default)
{
  std::string n = Narrow(S); char * end = nullptr;
  long v = std::strtol(n.c_str(), &end, 10);
  return (end == n.c_str() || *end != '\0') ? Default : static_cast<int>(v);
}
__int64 __fastcall StrToInt64Def(const UnicodeString & S, __int64 Default)
{
  std::string n = Narrow(S); char * end = nullptr;
  long long v = std::strtoll(n.c_str(), &end, 10);
  return (end == n.c_str() || *end != '\0') ? Default : v;
}

UnicodeString __fastcall Trim(const UnicodeString & S) { return S.Trim(); }
UnicodeString __fastcall TrimLeft(const UnicodeString & S) { return S.TrimLeft(); }
UnicodeString __fastcall TrimRight(const UnicodeString & S) { return S.TrimRight(); }
UnicodeString __fastcall UpperCase(const UnicodeString & S) { return S.UpperCase(); }
UnicodeString __fastcall LowerCase(const UnicodeString & S) { return S.LowerCase(); }

bool __fastcall SameText(const UnicodeString & A, const UnicodeString & B) { return A.CompareIC(B) == 0; }
bool __fastcall SameStr(const UnicodeString & A, const UnicodeString & B) { return A == B; }
int __fastcall CompareText(const UnicodeString & A, const UnicodeString & B) { return A.CompareIC(B); }
int __fastcall AnsiCompareText(const UnicodeString & A, const UnicodeString & B) { return A.CompareIC(B); }
int __fastcall AnsiCompareStr(const UnicodeString & A, const UnicodeString & B) { return A.Compare(B); }
bool __fastcall AnsiSameText(const UnicodeString & A, const UnicodeString & B) { return A.CompareIC(B) == 0; }

UnicodeString __fastcall IntToHex(__int64 Value) { return IntToHex(Value, 1); }
