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
#include "winscp/DynamicArray.h"
#include "winscp/SysStrFuncs.h"

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

// Delphi varargs (ARRAYOFCONST) record + resource-string pointer. Minimal: enough for the
// engine's Format/exception ctor signatures to resolve.
struct TVarRec
{
  union { void * VPointer; __int64 VInt64; double VExtended; const wchar_t * VPWideChar; };
  unsigned char VType;
};
struct TResStringRec { unsigned int * Module; int Identifier; };
typedef TResStringRec * PResStringRec;

// Sysutils::Exception — base of ExtException. Only the surface the engine touches.
namespace Sysutils {
  class Exception
  {
  public:
    __fastcall Exception(const UnicodeString & Msg) : Message(Msg) {}
    __fastcall Exception(const UnicodeString & Msg, int /*HelpContext*/) : Message(Msg) {}
    __fastcall Exception(const UnicodeString & Msg, const TVarRec *, const int) : Message(Msg) {}
    __fastcall Exception(const UnicodeString & Msg, const TVarRec *, const int, int) : Message(Msg) {}
    __fastcall Exception(NativeUInt /*Ident*/) {}
    __fastcall Exception(NativeUInt /*Ident*/, int /*HelpContext*/) {}
    __fastcall Exception(NativeUInt /*Ident*/, const TVarRec *, const int) {}
    __fastcall Exception(PResStringRec, const TVarRec *, const int, int) {}
    virtual __fastcall ~Exception() {}
    UnicodeString Message;
  };
  // Standard Delphi exception hierarchy (subset the engine derives from).
  class EAbort : public Exception { public: using Exception::Exception; };
  class EOSError : public Exception { public: using Exception::Exception; DWORD ErrorCode = 0; };
  class EConvertError : public Exception { public: using Exception::Exception; };
  class EAccessViolation : public Exception { public: using Exception::Exception; };
  class EInOutError : public Exception { public: using Exception::Exception; int ErrorCode = 0; };
}
using Sysutils::Exception;
using Sysutils::EAbort;
using Sysutils::EOSError;
using Sysutils::EConvertError;

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

// System::Variant (OLE variant). Minimal stub; real conversions added when a .cpp needs them.
class Variant
{
public:
  Variant() = default;
  Variant(int) {}
  Variant(const UnicodeString &) {}
  UnicodeString __fastcall ToString() const { return UnicodeString(); }
};

extern const UnicodeString EmptyStr;

// Raise EOSError for the current/last OS error (errno on POSIX).
void __fastcall RaiseLastOSError();
void __fastcall RaiseLastOSError(int LastError);

// Low-level file IO (System.SysUtils). Handle is an OS fd here. -1 on error.
int __fastcall FileRead(int Handle, void * Buffer, int Count);
int __fastcall FileWrite(int Handle, const void * Buffer, int Count);
int __fastcall FileRead(int Handle, System::DynamicArray<System::Byte> Buffer, int Offset, int Count);
int __fastcall FileWrite(int Handle, const System::DynamicArray<System::Byte> Buffer, int Offset, int Count);

#endif
