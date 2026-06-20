//---------------------------------------------------------------------------
// DelphiSet.h — emulation of Embarcadero System::Set<T, Lo, Hi> (Delphi `set of`).
// Backed by std::bitset. Operators match the engine's usage (<<, >>, Contains, ==).
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_DELPHISET_H
#define WINSCP_RTLCOMPAT_DELPHISET_H

#include <bitset>
#include <cstddef>

template <typename T, T Lo, T Hi>
class Set
{
public:
  Set() = default;
  Set & operator<<(const T v) { FBits.set(idx(v)); return *this; }   // include
  Set & operator>>(const T v) { FBits.reset(idx(v)); return *this; } // exclude
  bool Contains(const T v) const { return FBits.test(idx(v)); }
  bool Empty() const { return FBits.none(); }
  Set operator+(const Set & o) const { Set r(*this); r.FBits |= o.FBits; return r; }
  Set operator*(const Set & o) const { Set r(*this); r.FBits &= o.FBits; return r; }
  bool operator==(const Set & o) const { return FBits == o.FBits; }
  bool operator!=(const Set & o) const { return FBits != o.FBits; }
private:
  static std::size_t idx(const T v) { return static_cast<std::size_t>(v); }
  std::bitset<static_cast<std::size_t>(Hi) + 1> FBits;
};

#endif
