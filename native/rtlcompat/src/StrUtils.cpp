//---------------------------------------------------------------------------
// StrUtils.cpp — System.StrUtils bodies (subset).
//---------------------------------------------------------------------------
#include "StrUtils.hpp"
#include "winscp/SysStrFuncs.h"

bool __fastcall IsDelimiter(const UnicodeString & Delimiters, const UnicodeString & S, int Index)
{
  return S.IsDelimiter(Delimiters, Index);
}
bool __fastcall StartsStr(const UnicodeString & SubText, const UnicodeString & Text)
{
  return SubText.Length() <= Text.Length() && Text.SubString(1, SubText.Length()) == SubText;
}
bool __fastcall EndsStr(const UnicodeString & SubText, const UnicodeString & Text)
{
  return SubText.Length() <= Text.Length() &&
         Text.SubString(Text.Length() - SubText.Length() + 1, SubText.Length()) == SubText;
}
bool __fastcall StartsText(const UnicodeString & SubText, const UnicodeString & Text)
{ return StartsStr(UpperCase(SubText), UpperCase(Text)); }
bool __fastcall EndsText(const UnicodeString & SubText, const UnicodeString & Text)
{ return EndsStr(UpperCase(SubText), UpperCase(Text)); }
bool __fastcall ContainsStr(const UnicodeString & Text, const UnicodeString & SubText)
{ return Text.Pos(SubText) > 0; }
bool __fastcall ContainsText(const UnicodeString & Text, const UnicodeString & SubText)
{ return UpperCase(Text).Pos(UpperCase(SubText)) > 0; }

UnicodeString __fastcall ReplaceStr(const UnicodeString & Text, const UnicodeString & From, const UnicodeString & To)
{
  if (From.IsEmpty()) return Text;
  UnicodeString Result, Rest = Text;
  int p;
  while ((p = Rest.Pos(From)) > 0)
  { Result += Rest.SubString(1, p - 1) + To; Rest = Rest.SubString(p + From.Length(), Rest.Length()); }
  return Result + Rest;
}
UnicodeString __fastcall ReplaceText(const UnicodeString & Text, const UnicodeString & From, const UnicodeString & To)
{ return ReplaceStr(Text, From, To); }  // case-insensitive variant: TODO

UnicodeString __fastcall DupeString(const UnicodeString & S, int Count)
{ UnicodeString r; for (int i = 0; i < Count; ++i) r += S; return r; }

int __fastcall PosEx(const UnicodeString & SubStr, const UnicodeString & S, int Offset)
{
  if (Offset < 1) Offset = 1;
  int p = S.SubString(Offset, S.Length()).Pos(SubStr);
  return (p == 0) ? 0 : p + Offset - 1;
}
