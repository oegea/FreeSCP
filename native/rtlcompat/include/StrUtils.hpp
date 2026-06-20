//---------------------------------------------------------------------------
// StrUtils.hpp — System.StrUtils subset (search/prefix helpers). Bodies in src/StrUtils.cpp.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_STRUTILS_HPP
#define WINSCP_RTLCOMPAT_STRUTILS_HPP

#include "winscp/UnicodeString.h"

bool __fastcall IsDelimiter(const UnicodeString & Delimiters, const UnicodeString & S, int Index);
bool __fastcall StartsStr(const UnicodeString & SubText, const UnicodeString & Text);
bool __fastcall EndsStr(const UnicodeString & SubText, const UnicodeString & Text);
bool __fastcall StartsText(const UnicodeString & SubText, const UnicodeString & Text);
bool __fastcall EndsText(const UnicodeString & SubText, const UnicodeString & Text);
bool __fastcall ContainsStr(const UnicodeString & Text, const UnicodeString & SubText);
bool __fastcall ContainsText(const UnicodeString & Text, const UnicodeString & SubText);
UnicodeString __fastcall ReplaceStr(const UnicodeString & Text, const UnicodeString & From, const UnicodeString & To);
UnicodeString __fastcall ReplaceText(const UnicodeString & Text, const UnicodeString & From, const UnicodeString & To);
UnicodeString __fastcall DupeString(const UnicodeString & S, int Count);
int __fastcall PosEx(const UnicodeString & SubStr, const UnicodeString & S, int Offset);
UnicodeString __fastcall LeftStr(const UnicodeString & S, int Count);
int __fastcall FindDelimiter(const UnicodeString & Delimiters, const UnicodeString & S, int Offset = 1);
// SysUtils.LastDelimiter: 1-based index of the last char of S that is in Delimiters, else 0.
int __fastcall LastDelimiter(const UnicodeString & Delimiters, const UnicodeString & S);
// Delphi System.Pos(SubStr, S) — 1-based index of first match, 0 if none.
int __fastcall Pos(const UnicodeString & SubStr, const UnicodeString & S);
// SysUtils.StrLIComp — case-insensitive compare of up to MaxLen chars (<0/0/>0).
int __fastcall StrLIComp(const wchar_t * S1, const wchar_t * S2, int MaxLen);
int __fastcall IndexStr(const UnicodeString & AText, const UnicodeString * AValues, int Count);
int __fastcall IndexText(const UnicodeString & AText, const UnicodeString * AValues, int Count);

#endif
