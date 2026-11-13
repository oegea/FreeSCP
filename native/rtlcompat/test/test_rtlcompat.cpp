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

    // std::wstring is deliberately NOT tested: both libc++ and libstdc++ export basic_string<wchar_t>
    // members from their shared library, compiled for a 4-byte wchar_t. Whether a given call is
    // inlined (and then correct, thanks to ShortWchar.h) depends on the library version and the
    // optimiser - find() passed with Xcode 26 and failed with Xcode 16 and with libstdc++ 11.
    // The engine only builds two std::wstring values from a pointer and reads them back with
    // c_str() (FileZilla tools interface); everything else uses UnicodeString / std::u16string.
  }

  // --- UTF8String <-> UnicodeString really transcodes (was byte-widening: "ñ" became "Ã±") ---
  {
    UTF8String u8("ma\xC3\xB1" "ana \xE6\xB5\x8B\xE8\xAF\x95 \xF0\x9F\x94\x92");   // "mañana 测试 🔒"
    UnicodeString w(u8);
    assert(w.Length() == 12);                       // 6 + space + 2 CJK + space + surrogate pair
    assert(w[3] == L'\x00F1' && w[8] == L'\x6D4B');
    assert(w[11] == L'\xD83D' && w[12] == L'\xDD12');
    assert(std::string(UTF8String(w).c_str()) == std::string(u8.c_str()));   // lossless round trip
    // not UTF-8 (latin1 "ñ" = F1 followed by ASCII): bytes survive instead of eating what follows
    UnicodeString legacy(UTF8String("a\xF1" "bc"));
    assert(legacy.Length() == 4 && legacy[2] == L'\x00F1' && legacy[3] == L'b');
    // from a wide literal: encoded, not narrowed (FTP sends UTF8String(cmd.c_str()))
    assert(std::string(UTF8String(L"\x00F1\x6D4B").c_str()) == "\xC3\xB1\xE6\xB5\x8B");
    assert(UTF8String(L"a\x00F1z", 2).Length() == 3);
    // AnsiString/RawByteString keep widening byte-for-byte
    assert(UnicodeString(RawByteString("\xC3\xB1")).Length() == 2);
  }

  std::printf("rtlcompat: all assertions passed\n");
  return 0;
}
