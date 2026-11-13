//---------------------------------------------------------------------------
// Object.h — TObject + Delphi RTTI shim, in a low header so both SysUtils (Exception) and
// Classes can derive from TObject without a circular include.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_OBJECT_H
#define WINSCP_RTLCOMPAT_OBJECT_H

#include "winscp/rtldefs.h"
#include "winscp/UnicodeString.h"
#include <functional>

#include <cstdlib>
#include <cstddef>
#include <new>

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

  // Delphi/VCL allocates every TObject descendant ZERO-FILLED (TObject.NewInstance ->
  // InitInstance), and the engine relies on it: plenty of constructors leave pointer/flag members
  // unset and expect NULL/false (e.g. TSessionData::FUnsafeSettings, which Modify() writes
  // through when non-NULL). With the default allocator those members hold whatever the heap
  // contained - it worked while fresh pages happened to be zero and crashed once memory was
  // reused (opening a second connection). Class-level operator new restores the VCL guarantee
  // for all heap-allocated descendants; constructors and member initialisers still run on top.
  static void * operator new(std::size_t size)
  {
    void * p = std::calloc(1, size != 0 ? size : 1);
    if (p == nullptr) throw std::bad_alloc();
    return p;
  }
  static void * operator new[](std::size_t size) { return operator new(size); }
  static void * operator new(std::size_t, void * where) noexcept { return where; }   // keep placement new usable
  static void operator delete(void * p) noexcept { std::free(p); }
  static void operator delete[](void * p) noexcept { std::free(p); }
  static void operator delete(void *, void *) noexcept {}
  virtual UnicodeString __fastcall ClassName() const;
  UnicodeString __fastcall QualifiedClassName() const { return ClassName(); }
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

// Delphi try/finally -> RAII scope guard. genprops.py rewrites
//   try { A } __finally { B }   ->   { auto _f = winscp::MakeFinally([&]{ B }); A }
// so B runs on every exit (normal, return, or exception) — exact try/finally semantics.
namespace winscp {
  template <class F> struct TFinallyGuard
  {
    F f; bool armed = true;
    explicit TFinallyGuard(F fn) : f(fn) {}
    TFinallyGuard(TFinallyGuard && o) : f(o.f), armed(o.armed) { o.armed = false; }
    ~TFinallyGuard() { if (armed) f(); }
  };
  template <class F> TFinallyGuard<F> MakeFinally(F f) { return TFinallyGuard<F>(f); }

  // Delphi __closure events -> std::function. genprops rewrites:
  //   event typedef  RET (__closure *T)(ARGS)        -> typedef std::function<RET(ARGS)> T
  //   handler bind   X->OnE = Method;                -> X->OnE = MakeClosure(this, &Self::Method)
  // so `EventField = MethodName` (Delphi implicit-this) binds the receiver, callable/nullable
  // like the original. Covers non-const and const member functions.
  template <class C, class R, class... A>
  std::function<R(A...)> MakeClosure(C * self, R (C::*m)(A...))
  { return [self, m](A... a) { return (self->*m)(a...); }; }
  template <class C, class R, class... A>
  std::function<R(A...)> MakeClosure(C * self, R (C::*m)(A...) const)
  { return [self, m](A... a) { return (self->*m)(a...); }; }
}

#endif
