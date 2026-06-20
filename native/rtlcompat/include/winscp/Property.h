//---------------------------------------------------------------------------
// Property.h — emulation of Embarcadero C++Builder __property for clang/gcc.
//
// C++Builder syntax (compiler-native, unsupported by clang):
//     __property TStrings* MoreMessages = {read=FMoreMessages};
//     __property UnicodeString Name = {read=GetName, write=SetName};
//
// We cannot make clang parse `__property ... = {read=..., write=...}`. The port
// strategy (see docs/porting/PLAN.md, Phase 1) is a codegen pass that rewrites such
// declarations into the proxy members below. These templates are the *runtime* the
// rewrite targets; this header de-risks that mechanism (it compiles & behaves on clang).
//
// Trade-off: each property proxy stores a back-pointer to its owner, so objects grow by
// one pointer per property. Acceptable for the ~13 TObject engine classes; value structs
// with many fields will instead use plain getters/setters.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_PROPERTY_H
#define WINSCP_RTLCOMPAT_PROPERTY_H

namespace winscp { namespace rtl {

// Read-only property backed by a const getter:  T v = obj->Prop;
template <class Owner, typename T, T (Owner::*Getter)()>
class ROProperty
{
public:
  explicit ROProperty(Owner * owner) : FOwner(owner) {}
  operator T() const { return (FOwner->*Getter)(); }
  // member-of-pointer access for properties returning pointer-like values: obj->Prop->Foo()
  T operator->() const { return (FOwner->*Getter)(); }
private:
  Owner * FOwner;
};

// Read/write property backed by getter + setter:  obj->Prop = v;  T v = obj->Prop;
template <class Owner, typename T, T (Owner::*Getter)(), void (Owner::*Setter)(T)>
class RWProperty
{
public:
  explicit RWProperty(Owner * owner) : FOwner(owner) {}
  operator T() const { return (FOwner->*Getter)(); }
  RWProperty & operator=(const T & value) { (FOwner->*Setter)(value); return *this; }
  RWProperty & operator=(const RWProperty & other) { return operator=(static_cast<T>(other)); }
private:
  Owner * FOwner;
};

// Read-only property backed directly by a field (read=FField): zero call overhead.
template <typename T>
class ROField
{
public:
  explicit ROField(const T * field) : FField(field) {}
  operator T() const { return *FField; }
  T operator->() const { return *FField; }
private:
  const T * FField;
};

// Indexed read property:  T v = obj->Prop[i];
template <class Owner, typename T, typename Index, T (Owner::*Getter)(Index)>
class IndexedROProperty
{
public:
  explicit IndexedROProperty(Owner * owner) : FOwner(owner) {}
  T operator[](Index i) const { return (FOwner->*Getter)(i); }
private:
  Owner * FOwner;
};

} } // namespace winscp::rtl

#endif
