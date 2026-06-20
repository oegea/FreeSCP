//---------------------------------------------------------------------------
// Classes.hpp — System::Classes subset: TObject, TPersistent, TStrings, TStringList,
// TList, TStream, TMethod, TComponent. Declarations sufficient to parse the engine
// headers. Method bodies are added as .cpp files are ported.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_CLASSES_HPP
#define WINSCP_RTLCOMPAT_CLASSES_HPP

#include "winscp/rtldefs.h"
#include "winscp/wintypes.h"
#include "winscp/UnicodeString.h"
#include "winscp/DelphiSet.h"
#include "System.Types.hpp"

// Delphi method pointer (code+data); TMulticastEvent in Common.h reinterprets through it.
struct TMethod
{
  void * Code = nullptr;
  void * Data = nullptr;
};

class TObject
{
public:
  TObject() = default;
  virtual ~TObject() {}
};

// Delphi event types (closures). __closure is a no-op macro, so these are plain function
// pointers here; method-pointer (TObject + code) fidelity is added if/when needed.
typedef void __fastcall (__closure * TNotifyEvent)(TObject * Sender);

// Thread entry (System.Classes BeginThread). TThreadID = OS thread id.
typedef int (* TThreadFunc)(void * Parameter);
typedef unsigned long TThreadID;

// Keyboard/mouse modifier set (Vcl.Controls TShiftState).
enum TShiftStateEnum { ssShift, ssAlt, ssCtrl, ssLeft, ssRight, ssMiddle,
  ssDouble, ssTouch, ssPen, ssCommand, ssHorizontal };
typedef Set<TShiftStateEnum, ssShift, ssHorizontal> TShiftState;

class TPersistent : public TObject
{
public:
  virtual void __fastcall Assign(TPersistent * Source) { (void)Source; }
};

enum TListNotification { lnAdded, lnExtracted, lnDeleted };

class TList : public TObject
{
public:
  int __fastcall GetCount();
  void * __fastcall Get(int Index);
  void __fastcall Add(void * Item);
  void __fastcall Clear();
protected:
  virtual void __fastcall Notify(void * Ptr, TListNotification Action);
};

class TStream;

class TStrings : public TPersistent
{
public:
  virtual int __fastcall GetCount() = 0;
  virtual UnicodeString __fastcall Get(int Index) = 0;
  virtual void __fastcall Clear() = 0;
  virtual int __fastcall Add(const UnicodeString & S);
  void __fastcall AddStrings(TStrings * Strings);
  UnicodeString __fastcall GetText();
  void __fastcall SetText(const UnicodeString & Text);
};

class TStringList : public TStrings
{
public:
  virtual int __fastcall GetCount();
  virtual UnicodeString __fastcall Get(int Index);
  virtual void __fastcall Clear();
  bool Sorted = false;
  bool CaseSensitive = false;
  System::Types::TDuplicates Duplicates = System::Types::dupIgnore;
};

class TStream : public TObject
{
public:
  virtual __int64 __fastcall Read(void * Buffer, __int64 Count) = 0;
  virtual __int64 __fastcall Write(const void * Buffer, __int64 Count) = 0;
  virtual __int64 __fastcall Seek(__int64 Offset, int Origin);
  __int64 Position = 0;
  __int64 Size = 0;
};

class THandleStream : public TStream
{
public:
  virtual __int64 __fastcall Read(void * Buffer, __int64 Count);
  virtual __int64 __fastcall Write(const void * Buffer, __int64 Count);
  int Handle = -1;
};

class TFileStream : public THandleStream
{
public:
  __fastcall TFileStream(const UnicodeString & FileName, Word Mode);
};

class TMemoryStream : public TStream
{
public:
  virtual __int64 __fastcall Read(void * Buffer, __int64 Count);
  virtual __int64 __fastcall Write(const void * Buffer, __int64 Count);
  void * __fastcall GetMemory();
  __declspec(property(get=GetMemory)) void * Memory;  // Delphi property, used as ->Memory
  void __fastcall Clear();
  void __fastcall SetSize(__int64 NewSize);
};

class TStringStream : public TMemoryStream
{
public:
  __fastcall TStringStream(const UnicodeString & AString);
  UnicodeString __fastcall DataString();
};

// The engine often qualifies these as Classes::TStrings etc. (their Embarcadero unit).
namespace Classes {
  using ::TObject;
  using ::TPersistent;
  using ::TList;
  using ::TStrings;
  using ::TStringList;
  using ::TStream;
  using ::TMethod;
  using ::TListNotification;
}

#endif
