//---------------------------------------------------------------------------
// DynamicArray.h — Embarcadero System::DynamicArray<T> (Delphi dynamic array, e.g. TBytes).
// Backed by std::vector; 0-based indexing (unlike UnicodeString). Length is a property.
//---------------------------------------------------------------------------
#ifndef WINSCP_RTLCOMPAT_DYNAMICARRAY_H
#define WINSCP_RTLCOMPAT_DYNAMICARRAY_H

#include "winscp/rtldefs.h"
#include <vector>
#include <cstddef>

namespace System {

typedef unsigned char Byte;

template <typename T>
class DynamicArray
{
public:
  DynamicArray() = default;
  DynamicArray(int len) : FData(static_cast<size_t>(len)) {}

  int __fastcall get_length() const { return static_cast<int>(FData.size()); }
  void __fastcall set_length(int len) { FData.resize(static_cast<size_t>(len)); }
  __declspec(property(get=get_length, put=set_length)) int Length;

  T & operator[](int index) { return FData[static_cast<size_t>(index)]; }
  const T & operator[](int index) const { return FData[static_cast<size_t>(index)]; }
  T * begin() { return FData.data(); }
  T * end() { return FData.data() + FData.size(); }

private:
  std::vector<T> FData;
};

} // namespace System

typedef System::DynamicArray<System::Byte> TBytes;

#endif
