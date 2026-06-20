//---------------------------------------------------------------------------
// StrUtils.cpp — System.StrUtils bodies (subset).
//---------------------------------------------------------------------------
#include "StrUtils.hpp"
#include "WideStrUtils.hpp"
#include "winscp/SysStrFuncs.h"

bool __fastcall IsDelimiter(const UnicodeString & Delimiters, const UnicodeString & S, int Index)
{
  return S.IsDelimiter(Delimiters, Index);
}

TEncodeType __fastcall DetectUTF8Encoding(const RawByteString & S)
{
  const std::string & b = S.raw();
  bool anyMultibyte = false;
  size_t i = 0, n = b.size();
  while (i < n)
  {
    unsigned char c = (unsigned char)b[i];
    if (c < 0x80) { ++i; continue; }
    int extra;
    if      ((c & 0xE0) == 0xC0) extra = 1;
    else if ((c & 0xF0) == 0xE0) extra = 2;
    else if ((c & 0xF8) == 0xF0) extra = 3;
    else return etANSI;                       // invalid lead byte
    if (i + extra >= n) return etANSI;         // truncated sequence
    for (int k = 1; k <= extra; ++k)
      if (((unsigned char)b[i + k] & 0xC0) != 0x80) return etANSI;  // bad continuation
    anyMultibyte = true;
    i += extra + 1;
  }
  return anyMultibyte ? etUTF8 : etUSASCII;
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

int __fastcall IndexStr(const UnicodeString & AText, const UnicodeString * AValues, int Count)
{ for (int i = 0; i < Count; ++i) if (AValues[i] == AText) return i; return -1; }
int __fastcall IndexText(const UnicodeString & AText, const UnicodeString * AValues, int Count)
{ for (int i = 0; i < Count; ++i) if (AValues[i].CompareIC(AText) == 0) return i; return -1; }

UnicodeString __fastcall LeftStr(const UnicodeString & S, int Count)
{ if (Count <= 0) return UnicodeString(); if (Count >= S.Length()) return S; return S.SubString(1, Count); }
int __fastcall FindDelimiter(const UnicodeString & Delimiters, const UnicodeString & S, int Offset)
{ for (int i = (Offset < 1 ? 1 : Offset); i <= S.Length(); ++i) if (S.IsDelimiter(Delimiters, i)) return i; return 0; }
int __fastcall LastDelimiter(const UnicodeString & Delimiters, const UnicodeString & S)
{ return S.LastDelimiter(Delimiters); }
