//---------------------------------------------------------------------------
// Object.h — TObject + Delphi RTTI shim, in a low header so both SysUtils (Exception) and
// Classes can derive from TObject without a circular include.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_OBJECT_H
#define WINSCP_RTLCOMPAT_OBJECT_H

#include "winscp/rtldefs.h"
#include "winscp/UnicodeString.h"

class TObject;

// __classid(C)/InheritsFrom/ClassName via typeid + dynamic_cast (exact is-a, no engine edits).
namespace winscp {
  struct TClassInfo { bool (*isInstance)(const TObject *); const wchar_t * name; };
}
typedef const winscp::TClassInfo * TClass;

class TObject
{
public:
  TObject() = default;
  virtual ~TObject() {}
  virtual UnicodeString __fastcall ClassName() const;
  bool __fastcall InheritsFrom(TClass cls) const { return cls && cls->isInstance(this); }
};

namespace winscp {
  template <class C> const TClassInfo * classid()
  {
    static TClassInfo ci{ [](const TObject * o) { return dynamic_cast<const C *>(o) != nullptr; }, L"" };
    return &ci;
  }
}
#define __classid(C) (::winscp::classid<C>())

#endif
