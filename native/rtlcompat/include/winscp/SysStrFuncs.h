//---------------------------------------------------------------------------
// SysStrFuncs.h — free string functions from System.SysUtils the engine calls unqualified
// (IntToStr, StrToInt, Trim, UpperCase, ...). Declarations; bodies in src/SysUtils.cpp.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_SYSSTRFUNCS_H
#define WINSCP_RTLCOMPAT_SYSSTRFUNCS_H

#include "winscp/rtldefs.h"
#include "winscp/UnicodeString.h"

UnicodeString __fastcall IntToStr(__int64 Value);
UnicodeString __fastcall IntToHex(__int64 Value, int Digits);
UnicodeString __fastcall IntToHex(__int64 Value);
int __fastcall StrToInt(const UnicodeString & S);
__int64 __fastcall StrToInt64(const UnicodeString & S);
int __fastcall StrToIntDef(const UnicodeString & S, int Default);
__int64 __fastcall StrToInt64Def(const UnicodeString & S, __int64 Default);

UnicodeString __fastcall Trim(const UnicodeString & S);
UnicodeString __fastcall TrimLeft(const UnicodeString & S);
UnicodeString __fastcall TrimRight(const UnicodeString & S);
UnicodeString __fastcall UpperCase(const UnicodeString & S);
UnicodeString __fastcall LowerCase(const UnicodeString & S);

bool __fastcall SameText(const UnicodeString & A, const UnicodeString & B);
bool __fastcall SameStr(const UnicodeString & A, const UnicodeString & B);
int __fastcall CompareText(const UnicodeString & A, const UnicodeString & B);
int __fastcall AnsiCompareText(const UnicodeString & A, const UnicodeString & B);
int __fastcall AnsiCompareStr(const UnicodeString & A, const UnicodeString & B);
bool __fastcall AnsiSameText(const UnicodeString & A, const UnicodeString & B);

#endif
