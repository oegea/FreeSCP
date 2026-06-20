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
#include <vector>

// Delphi method pointer (code+data); TMulticastEvent in Common.h reinterprets through it.
struct TMethod
{
  void * Code = nullptr;
  void * Data = nullptr;
};

#include "winscp/Object.h"   // TObject + RTTI shim (shared with SysUtils for Exception)

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

// TList — list of untyped pointers (System.Classes). Delphi 0-based indexing.
class TList : public TObject
{
public:
  int __fastcall GetCount() { return static_cast<int>(FItems.size()); }
  void __fastcall SetCount(int NewCount) { FItems.resize(static_cast<size_t>(NewCount)); }
  __declspec(property(get=GetCount, put=SetCount)) int Count;

  void * __fastcall Get(int Index) { return FItems[static_cast<size_t>(Index)]; }
  void __fastcall Put(int Index, void * Item) { FItems[static_cast<size_t>(Index)] = Item; }
  __declspec(property(get=Get, put=Put)) void * Items[];

  virtual int __fastcall Add(void * Item);
  void __fastcall Insert(int Index, void * Item);
  virtual void __fastcall Delete(int Index);
  int __fastcall IndexOf(void * Item);
  int __fastcall Remove(void * Item);
  virtual void __fastcall Clear();
  void __fastcall Move(int CurIndex, int NewIndex);
  void __fastcall Sort(int (* Compare)(void *, void *));

protected:
  virtual void __fastcall Notify(void * Ptr, TListNotification Action) { (void)Ptr; (void)Action; }
  std::vector<void *> FItems;
};

class TStream;

// TStrings — abstract string list with Objects/name=value support (System.Classes).
class TStrings : public TPersistent
{
public:
  virtual ~TStrings() {}
  virtual int __fastcall GetCount() = 0;
  __declspec(property(get=GetCount)) int Count;

  virtual UnicodeString __fastcall Get(int Index) = 0;
  virtual void __fastcall Put(int Index, const UnicodeString & S) = 0;
  __declspec(property(get=Get, put=Put)) UnicodeString Strings[];

  virtual TObject * __fastcall GetObject(int Index) = 0;
  virtual void __fastcall PutObject(int Index, TObject * AObject) = 0;
  __declspec(property(get=GetObject, put=PutObject)) TObject * Objects[];

  virtual int __fastcall Add(const UnicodeString & S);
  virtual int __fastcall AddObject(const UnicodeString & S, TObject * AObject) = 0;
  void __fastcall AddStrings(TStrings * Strings);
  virtual void __fastcall Insert(int Index, const UnicodeString & S) = 0;
  virtual void __fastcall Delete(int Index) = 0;
  virtual void __fastcall Clear() = 0;
  virtual int __fastcall IndexOf(const UnicodeString & S);
  int __fastcall IndexOfName(const UnicodeString & Name);
  void __fastcall Move(int CurIndex, int NewIndex);
  void __fastcall Exchange(int Index1, int Index2);

  UnicodeString __fastcall GetText();
  void __fastcall SetText(const UnicodeString & Text);
  __declspec(property(get=GetText, put=SetText)) UnicodeString Text;

  UnicodeString __fastcall GetCommaText();
  void __fastcall SetCommaText(const UnicodeString & Value);
  __declspec(property(get=GetCommaText, put=SetCommaText)) UnicodeString CommaText;

  UnicodeString __fastcall GetName(int Index);
  UnicodeString __fastcall GetValue(const UnicodeString & Name);
  void __fastcall SetValue(const UnicodeString & Name, const UnicodeString & Value);
  UnicodeString __fastcall GetValueFromIndex(int Index);
  __declspec(property(get=GetName)) UnicodeString Names[];
  __declspec(property(get=GetValue, put=SetValue)) UnicodeString Values[];
  __declspec(property(get=GetValueFromIndex)) UnicodeString ValueFromIndex[];

  wchar_t Delimiter = L',';
  wchar_t QuoteChar = L'"';
  UnicodeString NameValueSeparator = UnicodeString(L"=");
};

// TStringList — concrete TStrings backed by a vector.
class TStringList : public TStrings
{
public:
  virtual int __fastcall GetCount();
  virtual UnicodeString __fastcall Get(int Index);
  virtual void __fastcall Put(int Index, const UnicodeString & S);
  virtual TObject * __fastcall GetObject(int Index);
  virtual void __fastcall PutObject(int Index, TObject * AObject);
  virtual int __fastcall AddObject(const UnicodeString & S, TObject * AObject);
  virtual void __fastcall Insert(int Index, const UnicodeString & S);
  virtual void __fastcall Delete(int Index);
  virtual void __fastcall Clear();
  virtual int __fastcall IndexOf(const UnicodeString & S);
  void __fastcall Sort();

  bool Sorted = false;
  bool CaseSensitive = false;
  bool StrictDelimiter = false;
  System::Types::TDuplicates Duplicates = System::Types::dupIgnore;

private:
  struct TItem { UnicodeString FString; TObject * FObject; };
  std::vector<TItem> FList;
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
  virtual int __fastcall Read(System::DynamicArray<System::Byte> Buffer, int Offset, int Count)
  { return Read(&Buffer[Offset], Count); }
  virtual int __fastcall Write(const System::DynamicArray<System::Byte> Buffer, int Offset, int Count)
  { return Write(&const_cast<System::DynamicArray<System::Byte> &>(Buffer)[Offset], Count); }
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
  using TStream::Read;   // un-hide the DynamicArray overloads hidden by the decls below
  using TStream::Write;
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
  using TStream::Read;
  using TStream::Write;
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
class EListError : public Exception { public: using Exception::Exception; };
class EStringListError : public Exception { public: using Exception::Exception; };
class EArgumentException : public Exception { public: using Exception::Exception; };
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
