//---------------------------------------------------------------------------
// SysUtils.hpp — System::Sysutils subset: Exception, TDateTime, TFormatSettings,
// TSearchRec, EmptyStr, TLibModule. Declarations sufficient to parse the engine headers;
// bodies/behaviour are filled in as the corresponding .cpp files are ported.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_SYSUTILS_HPP
#define WINSCP_RTLCOMPAT_SYSUTILS_HPP

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/WinCompat.h"
#include "winscp/Object.h"
#include "winscp/UnicodeString.h"
#include "winscp/AnsiStrings.h"
#include "winscp/DynamicArray.h"
#include "winscp/SysStrFuncs.h"
#include <vector>
#include <utility>

// Delphi date/time = floating day count since 1899-12-30.
class TDateTime
{
public:
  TDateTime() = default;
  TDateTime(double value) : FValue(value) {}
  // C++Builder TDateTime(Hour,Min,Sec,MSec) — time-of-day fraction.
  TDateTime(int H, int M, int S, int MS)
  { FValue = (((H * 60.0 + M) * 60.0 + S) * 1000.0 + MS) / 86400000.0; }
  operator double() const { return FValue; }
  double Val() const { return FValue; }
  TDateTime & operator+=(double d) { FValue += d; return *this; }
  TDateTime & operator-=(double d) { FValue -= d; return *this; }
  TDateTime & operator+=(const TDateTime & o) { FValue += o.FValue; return *this; }
  TDateTime & operator-=(const TDateTime & o) { FValue -= o.FValue; return *this; }
  // Delphi C++Builder TDateTime helpers (bodies in SysExtra.cpp).
  UnicodeString __fastcall DateString() const;
  UnicodeString __fastcall TimeString() const;
  UnicodeString __fastcall DateTimeString() const;
  UnicodeString __fastcall FormatString(const UnicodeString & Fmt) const;
  void __fastcall DecodeDate(Word * Y, Word * M, Word * D) const;
  void __fastcall DecodeTime(Word * H, Word * M, Word * S, Word * MS) const;
private:
  double FValue = 0.0;
};

extern const TDateTime MinDateTime;
extern const TDateTime MaxDateTime;

// Delphi static array (e.g. ShortMonthNames): has .Size() and decays to a pointer.
template <class T, int N>
struct StaticArray
{
  T FData[N];
  T & operator[](int i) { return FData[i]; }
  const T & operator[](int i) const { return FData[i]; }
  int __fastcall Size() const { return N; }
  int __fastcall Length() const { return N; }
  operator T *() { return FData; }
  operator const T *() const { return FData; }
};

struct TFormatSettings
{
  Char DecimalSeparator = '.';
  Char ThousandSeparator = ',';
  Char DateSeparator = '/';
  Char TimeSeparator = ':';
  UnicodeString ShortDateFormat;
  UnicodeString LongDateFormat;
  UnicodeString ShortTimeFormat;
  UnicodeString LongTimeFormat;
  StaticArray<UnicodeString, 13> ShortMonthNames;
  StaticArray<UnicodeString, 13> LongMonthNames;
  StaticArray<UnicodeString, 8> ShortDayNames;
  StaticArray<UnicodeString, 8> LongDayNames;
  static TFormatSettings __fastcall Create() { return TFormatSettings(); }
  static TFormatSettings __fastcall Create(int /*Locale*/) { return TFormatSettings(); }
};
extern TFormatSettings FormatSettings;

// Delphi varargs element (ARRAYOFCONST). Richer than the Win layout — carries enough type
// info for Format to render each value. Constructible from the scalar/string types the
// engine passes to FORMAT(...)/FMTLOAD(...).
struct TVarRec
{
  enum TType { vtInteger, vtInt64, vtBoolean, vtFloat, vtString, vtPointer } VType = vtInteger;
  __int64 VI = 0;
  double VF = 0.0;
  UnicodeString VS;
  void * VP = nullptr;

  TVarRec() = default;
  TVarRec(int v)                  : VType(vtInteger), VI(v) {}
  TVarRec(unsigned int v)         : VType(vtInteger), VI(v) {}
  TVarRec(long v)                 : VType(vtInt64),   VI(v) {}
  TVarRec(unsigned long v)        : VType(vtInt64),   VI(static_cast<__int64>(v)) {}
  TVarRec(long long v)            : VType(vtInt64),   VI(v) {}
  TVarRec(unsigned long long v)   : VType(vtInt64),   VI(static_cast<__int64>(v)) {}
  TVarRec(bool v)                 : VType(vtBoolean), VI(v) {}
  TVarRec(double v)               : VType(vtFloat),   VF(v) {}
  TVarRec(wchar_t v)              : VType(vtString),  VS(UnicodeString(static_cast<char16_t>(v), 1)) {}
  TVarRec(const wchar_t * v)      : VType(vtString),  VS(v) {}
  TVarRec(const char * v)         : VType(vtString),  VS(v) {}
  TVarRec(const UnicodeString & v): VType(vtString),  VS(v) {}
  TVarRec(const AnsiStringBase & v): VType(vtString), VS(v) {}
  TVarRec(void * v)               : VType(vtPointer), VP(v) {}
};

struct TResStringRec { unsigned int * Module; int Identifier; };
typedef TResStringRec * PResStringRec;

// Sysutils::Exception — base of ExtException. Derives TObject so InheritsFrom/ClassName work.
namespace Sysutils {
  class Exception : public TObject
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
using Sysutils::EAccessViolation;
using Sysutils::EInOutError;

// TSearchRec — directory enumeration record (base of WinSCP's TSearchRecSmart).
struct TSearchRec
{
  int Time = 0;
  __int64 Size = 0;
  int Attr = 0;
  UnicodeString Name;
  int ExcludeAttr = 0;
  TDateTime TimeStamp;
  TWin32FindData FindData{};
  int FindHandle = -1;
};

typedef UnicodeString TFileName;

// Delphi timestamp (date+time split) and time span.
struct TTimeStamp { int Time = 0; int Date = 0; };
struct TTimeSpan
{
  __int64 Ticks = 0;  // 100ns units
  TTimeSpan() = default;
  explicit TTimeSpan(__int64 ticks) : Ticks(ticks) {}
  double __fastcall GetTotalSeconds() const { return Ticks / 1e7; }
  double __fastcall GetTotalMilliseconds() const { return Ticks / 1e4; }
  double __fastcall GetTotalMinutes() const { return Ticks / 6e8; }
  double __fastcall GetTotalHours() const { return Ticks / 3.6e10; }
  double __fastcall GetTotalDays() const { return Ticks / 8.64e11; }
  __declspec(property(get=GetTotalSeconds)) double TotalSeconds;
  __declspec(property(get=GetTotalMilliseconds)) double TotalMilliseconds;
  __declspec(property(get=GetTotalMinutes)) double TotalMinutes;
  __declspec(property(get=GetTotalHours)) double TotalHours;
  __declspec(property(get=GetTotalDays)) double TotalDays;
  // integer components
  int __fastcall GetSeconds() const { return static_cast<int>((Ticks / 10000000) % 60); }
  int __fastcall GetMinutes() const { return static_cast<int>((Ticks / 600000000) % 60); }
  int __fastcall GetHours() const { return static_cast<int>((Ticks / 36000000000LL) % 24); }
  int __fastcall GetDays() const { return static_cast<int>(Ticks / 864000000000LL); }
  __declspec(property(get=GetSeconds)) int Seconds;
  __declspec(property(get=GetMinutes)) int Minutes;
  __declspec(property(get=GetHours)) int Hours;
  __declspec(property(get=GetDays)) int Days;
  static const TTimeSpan Zero;
  static TTimeSpan __fastcall FromSeconds(double s) { return TTimeSpan(static_cast<__int64>(s * 1e7)); }
  static TTimeSpan __fastcall FromMilliseconds(double m) { return TTimeSpan(static_cast<__int64>(m * 1e4)); }
  bool operator==(const TTimeSpan & o) const { return Ticks == o.Ticks; }
  bool operator!=(const TTimeSpan & o) const { return Ticks != o.Ticks; }
  bool operator<(const TTimeSpan & o) const { return Ticks < o.Ticks; }
  bool operator>(const TTimeSpan & o) const { return Ticks > o.Ticks; }
  bool operator<=(const TTimeSpan & o) const { return Ticks <= o.Ticks; }
  bool operator>=(const TTimeSpan & o) const { return Ticks >= o.Ticks; }
};

class EDirectoryNotFoundException : public Exception { public: using Exception::Exception; };
class EEncodingError : public Exception { public: using Exception::Exception; };

struct TLibModule
{
  NativeUInt Instance = 0;   // HINSTANCE-like handle (compared against (unsigned)ptr)
  NativeUInt ResInstance = 0;
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
extern int RandSeed;
extern const UnicodeString System_Sysconst_SOSError;

// Delphi open-array constructor: ARRAYOFCONST((a, b, c)) -> TVarRecArray(a, b, c).
struct TVarRecArray
{
  std::vector<TVarRec> Items;
  template <class... A> TVarRecArray(A &&... a) { (Items.push_back(TVarRec(std::forward<A>(a))), ...); }
};
#define ARRAYOFCONST(v) (::TVarRecArray v)

// Delphi Format / FmtLoadStr (resource string + format). FORMAT/FMTLOAD macros in Global.h.
UnicodeString __fastcall Format(const UnicodeString & Fmt, const TVarRecArray & Args);
UnicodeString __fastcall Format(const UnicodeString & Fmt, const TVarRec * Args, int Args_Size);
UnicodeString __fastcall FmtLoadStr(int Ident, const TVarRecArray & Args);

// Resource strings (ident -> text). Placeholder table until source/resource is wired in.
UnicodeString __fastcall LoadStr(int Ident);

// Raise EOSError for the current/last OS error (errno on POSIX).
void __fastcall RaiseLastOSError();
void __fastcall RaiseLastOSError(int LastError);
NORETURN void __fastcall Abort();   // raises EAbort (silent exception)
#include "winscp/SysExtra.h"

// Low-level file IO (System.SysUtils). Handle is an OS fd here. -1 on error.
int __fastcall FileRead(int Handle, void * Buffer, int Count);
int __fastcall FileWrite(int Handle, const void * Buffer, int Count);
int __fastcall FileRead(int Handle, System::DynamicArray<System::Byte> Buffer, int Offset, int Count);
int __fastcall FileWrite(int Handle, const System::DynamicArray<System::Byte> Buffer, int Offset, int Count);

#endif
