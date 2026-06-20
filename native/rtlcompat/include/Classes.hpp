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
#include "SysUtils.hpp"   // Exception (base of stream errors)

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

// Delphi TSeekOrigin + legacy integer constants (engine uses soFromBeginning/soCurrent/soEnd).
enum TSeekOrigin { soBeginning, soCurrent, soEnd };
const int soFromBeginning = 0;
const int soFromCurrent = 1;
const int soFromEnd = 2;

// Stream file-open mode bits (System.Classes / SysUtils fmXxx).
const Word fmCreate = 0xFF00;
const Word fmOpenRead = 0x0000;
const Word fmOpenWrite = 0x0001;
const Word fmOpenReadWrite = 0x0002;
const Word fmShareDenyNone = 0x0040;

class TStream : public TObject
{
public:
  virtual ~TStream() {}
  virtual int __fastcall Read(void * Buffer, int Count) = 0;
  virtual int __fastcall Write(const void * Buffer, int Count) = 0;
  virtual __int64 __fastcall Seek(__int64 Offset, Word Origin);

  void __fastcall ReadBuffer(void * Buffer, int Count);
  void __fastcall WriteBuffer(const void * Buffer, int Count);
  __int64 __fastcall CopyFrom(TStream * Source, __int64 Count);

  __int64 __fastcall GetPosition();
  void __fastcall SetPosition(__int64 Pos);
  __declspec(property(get=GetPosition, put=SetPosition)) __int64 Position;
  virtual __int64 __fastcall GetSize();
  virtual void __fastcall SetSize(__int64 NewSize);
  __declspec(property(get=GetSize, put=SetSize)) __int64 Size;
};

class THandleStream : public TStream
{
public:
  __fastcall THandleStream(int AHandle) : FHandle(AHandle) {}
  virtual int __fastcall Read(void * Buffer, int Count);
  virtual int __fastcall Write(const void * Buffer, int Count);
  virtual __int64 __fastcall Seek(__int64 Offset, Word Origin);
  __declspec(property(get=GetHandle)) int Handle;
  int __fastcall GetHandle() { return FHandle; }
protected:
  int FHandle = -1;
};

class TFileStream : public THandleStream
{
public:
  __fastcall TFileStream(const UnicodeString & FileName, Word Mode);
  virtual __fastcall ~TFileStream();
};

class TMemoryStream : public TStream
{
public:
  __fastcall TMemoryStream() = default;
  virtual __fastcall ~TMemoryStream();
  virtual int __fastcall Read(void * Buffer, int Count);
  virtual int __fastcall Write(const void * Buffer, int Count);
  virtual __int64 __fastcall Seek(__int64 Offset, Word Origin);
  virtual __int64 __fastcall GetSize();
  virtual void __fastcall SetSize(__int64 NewSize);
  void * __fastcall GetMemory();
  __declspec(property(get=GetMemory)) void * Memory;  // Delphi property, used as ->Memory
  void __fastcall Clear();
protected:
  unsigned char * FData = nullptr;
  __int64 FSize = 0;
  __int64 FCapacity = 0;
  __int64 FPosition = 0;
  friend class TStream;
};

class TStringStream : public TMemoryStream
{
public:
  __fastcall TStringStream(const UnicodeString & AString);
  UnicodeString __fastcall DataString();
};

// Stream exceptions (System.Classes).
class EStreamError : public Exception { public: using Exception::Exception; };
class EReadError  : public EStreamError { public: using EStreamError::EStreamError; };
class EWriteError : public EStreamError { public: using EStreamError::EStreamError; };
class EFCreateError : public EStreamError { public: using EStreamError::EStreamError; };
class EFOpenError : public EStreamError { public: using EStreamError::EStreamError; };

// The engine qualifies these as Classes::TStrings or System::TObject (their Embarcadero
// units). Expose both spellings.
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
namespace System {
  using ::TObject;
  using ::TPersistent;
  using ::TList;
  using ::TStrings;
  using ::TStringList;
  using ::TStream;
  using ::TMethod;
  using ::TNotifyEvent;
}

#endif
