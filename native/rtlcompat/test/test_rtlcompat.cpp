//---------------------------------------------------------------------------
// De-risk test for the two Phase-1 keystones: UnicodeString semantics + __property proxy.
// Plain asserts, no framework (keeps Phase 0 dependency-free).
//---------------------------------------------------------------------------
#include "winscp/UnicodeString.h"
#include "winscp/Property.h"
#include <cassert>
#include <cstdio>

// A class shaped like generated C++Builder code, exercising the property proxies the way
// the codegen rewrite will emit them.
class Sample
{
public:
  Sample() : Name(this), Doubled(this) {}

  UnicodeString GetName() { return FName; }
  void SetName(UnicodeString v) { FName = v; }
  int GetDoubled() { return FValue * 2; }

  void SetValue(int v) { FValue = v; }

  winscp::rtl::RWProperty<Sample, UnicodeString, &Sample::GetName, &Sample::SetName> Name;
  winscp::rtl::ROProperty<Sample, int, &Sample::GetDoubled> Doubled;

private:
  UnicodeString FName;
  int FValue = 0;
};

int main()
{
  // --- UnicodeString: 1-based indexing, Length, SubString, Pos, concat ---
  UnicodeString s = L"Hello";
  assert(s.Length() == 5);
  assert(s[1] == L'H');          // Delphi 1-based
  assert(s[5] == L'o');
  assert(s.SubString(2, 3) == UnicodeString(L"ell"));
  assert(s.Pos(L"llo") == 3);
  assert(s.Pos(L"xyz") == 0);

  UnicodeString joined = s + L", " + UnicodeString(L"world");
  assert(joined == UnicodeString(L"Hello, world"));

  // wchar_t must be 2 bytes (UTF-16) under -fshort-wchar
  static_assert(sizeof(wchar_t) == 2, "expected -fshort-wchar");

  // --- __property proxy: read/write via member syntax ---
  Sample obj;
  obj.Name = UnicodeString(L"winscp");      // write through setter
  assert(static_cast<UnicodeString>(obj.Name) == UnicodeString(L"winscp"));  // read through getter
  obj.SetValue(21);
  assert(static_cast<int>(obj.Doubled) == 42);  // computed read-only property

  std::printf("rtlcompat: all assertions passed\n");
  return 0;
}
