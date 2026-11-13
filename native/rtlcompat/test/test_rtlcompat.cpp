//---------------------------------------------------------------------------
// De-risk test for the two Phase-1 keystones: UnicodeString semantics + __property proxy.
// Plain asserts, no framework (keeps Phase 0 dependency-free).
//---------------------------------------------------------------------------
#include "winscp/UnicodeString.h"
#include "winscp/Property.h"
#include "winscp/SysExtra.h"
#include <algorithm>
#include <string>
#undef NDEBUG   // these tests ARE the asserts: keep them alive in Release builds (they were compiled out)
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

  // --- single-character search in 2-byte strings (regression: libc++ sent these to libc's
  //     4-byte wmemchr under -fshort-wchar, so nothing was ever found; see ShortWchar.h) ---
  {
    std::u16string u16(u"/a\\b:c");
    assert(u16.find(u'/') == 0);
    assert(u16.find(u':') == 4);
    assert(u16.find(u'z') == std::u16string::npos);
    assert(std::find(u16.begin(), u16.end(), u'b') - u16.begin() == 3);
    const unsigned short words[] = { 7, 8, 9, 10 };
    assert(std::find(words, words + 4, (unsigned short)9) == words + 2);

    UnicodeString path(L"/Users/me/app/winscp-qt");
    assert(path.LastDelimiter(UnicodeString(L"/\\:")) == 14);
    assert(path.IsDelimiter(UnicodeString(L"/\\:"), 7));
    assert(path.Pos(UnicodeString(L"m")) == 8);
    assert(ExtractFilePath(path) == UnicodeString(L"/Users/me/app/"));
    assert(ExtractFileDir(path) == UnicodeString(L"/Users/me/app"));
    assert(ExtractFileName(path) == UnicodeString(L"winscp-qt"));
    assert(ChangeFileExt(UnicodeString(L"/x/y.txt"), UnicodeString(L".ini")) == UnicodeString(L"/x/y.ini"));

    // std::wstring (2-byte here; used by the FileZilla FTP backend): header-inline length/compare/find.
    // (Out-of-line members such as operator+/append live in the system libc++ built for 4-byte
    // wchar_t and stay off-limits under -fshort-wchar; the engine does not use them.)
    std::wstring w(L"hello world");
    assert(w.size() == 11);
    assert(w.find(L'w') == 6);
    assert(w.find(L"world") == 6);
    assert(w == std::wstring(L"hello world"));
    assert(w.compare(L"hello worle") < 0);
  }

  std::printf("rtlcompat: all assertions passed\n");
  return 0;
}
