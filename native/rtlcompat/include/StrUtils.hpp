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

#endif
