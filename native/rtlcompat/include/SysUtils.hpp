//---------------------------------------------------------------------------
// SysUtils.hpp — System::Sysutils subset: Exception, TDateTime, TFormatSettings,
// TSearchRec, EmptyStr, TLibModule. Declarations sufficient to parse the engine headers;
// bodies/behaviour are filled in as the corresponding .cpp files are ported.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_SYSUTILS_HPP
#define WINSCP_RTLCOMPAT_SYSUTILS_HPP

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/UnicodeString.h"
#include "winscp/AnsiStrings.h"

// Delphi date/time = floating day count since 1899-12-30.
class TDateTime
{
public:
  TDateTime() = default;
  TDateTime(double value) : FValue(value) {}
  operator double() const { return FValue; }
  double Val() const { return FValue; }
private:
  double FValue = 0.0;
};

struct TFormatSettings
{
  Char DecimalSeparator = '.';
  Char ThousandSeparator = ',';
  UnicodeString ShortDateFormat;
  UnicodeString LongDateFormat;
  UnicodeString ShortTimeFormat;
  UnicodeString LongTimeFormat;
};

// Sysutils::Exception — base of ExtException. Only the surface the engine touches.
namespace Sysutils {
  class Exception
  {
  public:
    __fastcall Exception(const UnicodeString & Msg) : Message(Msg) {}
    __fastcall Exception(const UnicodeString & Msg, int /*HelpContext*/) : Message(Msg) {}
    virtual __fastcall ~Exception() {}
    UnicodeString Message;
  };
}
using Sysutils::Exception;

// TSearchRec — directory enumeration record (base of WinSCP's TSearchRecSmart).
struct TSearchRec
{
  int Attr = 0;
  __int64 Size = 0;
  TDateTime TimeStamp;
  UnicodeString Name;
};

struct TLibModule
{
  void * Instance = nullptr;
  TLibModule * Next = nullptr;
};

extern const UnicodeString EmptyStr;

#endif
