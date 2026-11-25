//---------------------------------------------------------------------------
// Unit tests for the UI's pure logic: text comparison (textdiff) and Compare Directories
// (panelcompare). No Qt and no engine needed.
//---------------------------------------------------------------------------
#undef NDEBUG   // these tests ARE the asserts
#include <cassert>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
#include "../textdiff.h"
#include "../panelcompare.h"
#include "../panellogic.h"

using textdiff::Row;

static std::vector<std::string> L(std::initializer_list<const char *> l) { return std::vector<std::string>(l.begin(), l.end()); }

// "=" same, "~" changed, "<" left only, ">" right only
static std::string shape(const std::vector<Row> & rows)
{
  std::string s;
  for (const Row & r : rows) s += (r.kind == Row::Same) ? '=' : (r.kind == Row::Changed) ? '~' : (r.kind == Row::LeftOnly) ? '<' : '>';
  return s;
}

// Every diff must be a faithful account of both inputs: each line appears exactly once, in order,
// and rows marked Same really are equal.
static void checkConsistent(const std::vector<std::string> & a, const std::vector<std::string> & b, const std::vector<Row> & rows)
{
  int ia = 0, ib = 0;
  for (const Row & r : rows)
  {
    if (r.left >= 0) { assert(r.left == ia); ++ia; }
    if (r.right >= 0) { assert(r.right == ib); ++ib; }
    switch (r.kind)
    {
      case Row::Same: assert(r.left >= 0 && r.right >= 0 && a[(size_t)r.left] == b[(size_t)r.right]); break;
      case Row::Changed: assert(r.left >= 0 && r.right >= 0); break;
      case Row::LeftOnly: assert(r.left >= 0 && r.right < 0); break;
      case Row::RightOnly: assert(r.left < 0 && r.right >= 0); break;
    }
  }
  assert(ia == (int)a.size() && ib == (int)b.size());
}

static engine::DirEntry F(const char * name, long long size, long long mtime, bool secs = true)
{ engine::DirEntry e; e.name = name; e.size = size; e.mtimeSecs = mtime; e.mtimeHasSeconds = secs; return e; }
static engine::DirEntry D(const char * name) { engine::DirEntry e; e.name = name; e.isDir = true; return e; }
static std::string J(const std::vector<std::string> & v) { std::string s; for (auto & x : v) s += x + ","; return s; }

#include "versioncmp.h"
static int versionTests()
{
  int bad = 0;
  auto expect = [&](bool got, bool want, const char * what) { if (got != want) { printf("FAIL version: %s\n", what); ++bad; } };
  expect(versionIsNewer("v0.3.1", "0.3.0"), true,  "patch");
  expect(versionIsNewer("0.4.0", "0.3.9"),  true,  "minor");
  expect(versionIsNewer("1.0", "0.99.99"),  true,  "major, fewer parts");
  expect(versionIsNewer("0.10.0", "0.9.0"), true,  "numeric, not lexical");
  expect(versionIsNewer("v0.3.0", "0.3.0"), false, "same");
  expect(versionIsNewer("0.3", "0.3.0"),    false, "same, fewer parts");
  expect(versionIsNewer("0.3.0.1", "0.3.0"), true, "extra part");
  expect(versionIsNewer("0.2.9", "0.3.0"),  false, "older");
  expect(versionIsNewer("0.3.0-beta", "0.3.0"), false, "suffix ignored");
  expect(versionIsNewer("", "0.3.0"),       false, "empty");
  expect(versionIsNewer("latest", "0.3.0"), false, "garbage");
  expect(versionIsNewer("99999999999999999999", "0.3.0"), false, "overflow");
  expect(versionIsNewer("<html>", "0.3.0"), false, "html");
  return bad;
}


// The file panels' pure logic: path drop-down, recent directories, date patterns, permissions.
static void testPanelLogic()
{
  using namespace panellogic;
  // --- path hierarchy ---
  { auto h = pathHierarchy("/a/b/c"); assert(h.size() == 4);
    assert(h[0].label == "/" && h[0].path == "/"); assert(h[1].label == "a" && h[1].path == "/a");
    assert(h[2].path == "/a/b"); assert(h[3].label == "c" && h[3].path == "/a/b/c"); }
  { auto h = pathHierarchy("/"); assert(h.size() == 1 && h[0].path == "/"); }
  { auto h = pathHierarchy(""); assert(h.size() == 1 && h[0].path == "/"); }
  { auto h = pathHierarchy("relative/x"); assert(h.size() == 1); }
  { auto h = pathHierarchy("//a///b/"); assert(h.size() == 3 && h[2].path == "/a/b"); }
  { auto h = pathHierarchy("/with space/\xC3\xB1 \xE6\xB5\x8B"); assert(h.size() == 3 && h[2].label == "\xC3\xB1 \xE6\xB5\x8B" && h[2].path == "/with space/\xC3\xB1 \xE6\xB5\x8B"); }
  assert(normalizePath("/a//b/") == "/a/b"); assert(normalizePath("/") == "/"); assert(normalizePath("///") == "/");
  assert(normalizePath("/a/./b") == "/a/./b");      // "." and ".." are the server's business, not ours

  // --- recent directories ---
  { std::vector<std::string> r;
    pushRecent(r, "/a", 3); pushRecent(r, "/b", 3); pushRecent(r, "/a", 3);
    assert(r.size() == 2 && r[0] == "/a" && r[1] == "/b");
    pushRecent(r, "/c", 3); pushRecent(r, "/d", 3);
    assert(r.size() == 3 && r[0] == "/d" && r[1] == "/c" && r[2] == "/a");
    pushRecent(r, "", 3); assert(r.size() == 3 && r[0] == "/d");
    pushRecent(r, "/a", 1); assert(r.size() == 1 && r[0] == "/a"); }

  // --- date / time patterns ---
  assert(fourDigitYear("d/M/yy") == "d/M/yyyy");
  assert(fourDigitYear("dd/MM/yyyy") == "dd/MM/yyyy");
  assert(fourDigitYear("M/d/yy") == "M/d/yyyy");
  assert(fourDigitYear("yy-MM-dd") == "yyyy-MM-dd");
  assert(fourDigitYear("d MMM yyyy") == "d MMM yyyy");
  assert(fourDigitYear("d 'day of yy' yy") == "d 'day of yy' yyyy");
  assert(fourDigitYear("") == "");
  assert(withSeconds("H:mm") == "H:mm:ss");
  assert(withSeconds("HH:mm") == "HH:mm:ss");
  assert(withSeconds("h:mm AP") == "h:mm:ss AP");
  assert(withSeconds("HH:mm:ss") == "HH:mm:ss");
  assert(withSeconds("h 'm' mm a") == "h 'm' mm:ss a");
  assert(withSeconds("HH") == "HH");
  assert(wallSecondsSince1970(25569LL * 86400LL) == 0);
  assert(wallSecondsSince1970(25569LL * 86400LL + 3661) == 3661);

  // --- permissions ---
  assert(octalFromRights("rwxr-xr-x") == 0755);
  assert(octalFromRights("rw-r--r--") == 0644);
  assert(octalFromRights("---------") == 0);
  assert(octalFromRights("rwsr-xr-x") == 04755);
  assert(octalFromRights("rwSr--r--") == 04644);
  assert(octalFromRights("rwxr-sr-x") == 02755);
  assert(octalFromRights("rwxrwxrwt") == 01777);
  assert(octalFromRights("rwxrwxrwT") == 01776);
  assert(octalFromRights("") == -1);
  assert(octalFromRights("rwxr-xr-") == -1);
  assert(octalFromRights("rwxr-xr-xx") == -1);
  assert(octalFromRights("xwrr-xr-x") == -1);
  assert(octalFromRights("rwtr-xr-x") == -1);     // sticky only in the last position
  for (int v = 0; v <= 07777; ++v) assert(octalFromRights(rightsFromOctal(v)) == v);
  assert(rightsFromOctal(0755) == "rwxr-xr-x"); assert(rightsFromOctal(04644) == "rwSr--r--"); assert(rightsFromOctal(01777) == "rwxrwxrwt");
  assert(validOctal("644") && validOctal("0755") && validOctal("4755") && validOctal("000"));
  assert(!validOctal("") && !validOctal("64") && !validOctal("64444") && !validOctal("9z") && !validOctal("68a") && !validOctal("-44"));
  assert(octalString(0755) == "755"); assert(octalString(0) == "000"); assert(octalString(04755) == "4755"); assert(octalString(07) == "007");
  for (int v = 0; v <= 07777; ++v) { const std::string o = octalString(v); assert(validOctal(o)); assert(std::stoi(o, nullptr, 8) == v); }

  // --- session time ---
  assert(elapsedText(0) == "0:00:00"); assert(elapsedText(7) == "0:00:07"); assert(elapsedText(3723) == "1:02:03");
  assert(elapsedText(27 * 3600 + 15 * 60) == "27:15:00"); assert(elapsedText(-5) == "0:00:00");
  std::printf("panel logic: ok\n");
}

int main()
{
  testPanelLogic();
  if (versionTests() != 0) return 1;
  //--- splitLines ---
  {
    bool crlf = true;
    assert(textdiff::splitLines("", &crlf).empty() && !crlf);
    assert(textdiff::splitLines("a\nb\n") == L({ "a", "b" }));
    assert(textdiff::splitLines("a\nb") == L({ "a", "b" }));          // no final newline
    assert(textdiff::splitLines("a\n\nb\n") == L({ "a", "", "b" }));   // empty line kept
    assert(textdiff::splitLines("\n") == L({ "" }));
    assert(textdiff::splitLines("a\r\nb\r\n", &crlf) == L({ "a", "b" }) && crlf);
    assert(textdiff::splitLines("a\rb\n") == L({ "a\rb" }));           // a lone CR is not a line break
  }

  //--- diff shapes ---
  {
    auto a = L({ "one", "two", "three" });
    assert(shape(textdiff::diff(a, a)) == "===");
    assert(textdiff::countBlocks(textdiff::diff(a, a)) == 0);
    assert(shape(textdiff::diff({}, {})) == "");
    assert(shape(textdiff::diff(a, {})) == "<<<");
    assert(shape(textdiff::diff({}, a)) == ">>>");

    auto b = L({ "one", "TWO", "three" });
    auto r = textdiff::diff(a, b);
    assert(shape(r) == "=~=" && r[1].left == 1 && r[1].right == 1 && textdiff::countBlocks(r) == 1);

    assert(shape(textdiff::diff(L({ "a", "b", "c" }), L({ "a", "c" }))) == "=<=");            // line removed
    assert(shape(textdiff::diff(L({ "a", "c" }), L({ "a", "b", "c" }))) == "=>=");            // line added
    assert(shape(textdiff::diff(L({ "a", "b" }), L({ "x", "a", "b" }))) == ">==");            // added at the top
    assert(shape(textdiff::diff(L({ "a", "b" }), L({ "a", "b", "x", "y" }))) == "==>>");      // added at the end
    // a changed block with more lines on one side: pairs first, then the surplus
    assert(shape(textdiff::diff(L({ "a", "1", "2", "3", "z" }), L({ "a", "X", "z" }))) == "=~<<=");
    assert(shape(textdiff::diff(L({ "a", "X", "z" }), L({ "a", "1", "2", "3", "z" }))) == "=~>>=");
    // two separate blocks
    auto two = textdiff::diff(L({ "a", "b", "c", "d", "e" }), L({ "a", "B", "c", "d", "E" }));
    assert(shape(two) == "=~==~" && textdiff::countBlocks(two) == 2);
    // repeated lines (no unique anchors): still a minimal, consistent result
    auto rep = textdiff::diff(L({ "x", "x", "x" }), L({ "x", "x" }));
    assert(shape(rep).size() == 3 && textdiff::countBlocks(rep) == 1);
    checkConsistent(L({ "x", "x", "x" }), L({ "x", "x" }), rep);
    // a moved block shows as remove + add, never as "same" out of order
    auto mv = textdiff::diff(L({ "A", "B", "1", "2" }), L({ "1", "2", "A", "B" }));
    checkConsistent(L({ "A", "B", "1", "2" }), L({ "1", "2", "A", "B" }), mv);
  }

  //--- options ---
  {
    textdiff::Options o;
    assert(shape(textdiff::diff(L({ "a\r", "b" }), L({ "a", "b" }), o)) == "==");             // CR ignored by default
    o.ignoreLineEndings = false;
    assert(shape(textdiff::diff(L({ "a\r", "b" }), L({ "a", "b" }), o)) == "~=");
    textdiff::Options sp; sp.ignoreTrailingSpace = true;
    assert(shape(textdiff::diff(L({ "a  ", "b\t" }), L({ "a", "b" }), sp)) == "==");
    assert(shape(textdiff::diff(L({ "a  " }), L({ "a" }))) == "~");
    textdiff::Options ic; ic.ignoreCase = true;
    assert(shape(textdiff::diff(L({ "Hello" }), L({ "hELLO" }), ic)) == "=");
  }

  //--- randomized: consistency + applying the diff to the left side yields the right side ---
  {
    std::mt19937 gen(12345);
    for (int round = 0; round < 300; ++round)
    {
      std::vector<std::string> a, b;
      int n = (int)(gen() % 60);
      for (int i = 0; i < n; ++i) a.push_back("line" + std::to_string(gen() % 12));   // many duplicates on purpose
      b = a;
      int edits = (int)(gen() % 8);
      for (int e = 0; e < edits; ++e)
      {
        int kind = (int)(gen() % 3);
        size_t pos = b.empty() ? 0 : gen() % (b.size() + 1);
        if (kind == 0) b.insert(b.begin() + (long)pos, "new" + std::to_string(gen() % 5));
        else if (kind == 1 && !b.empty()) b.erase(b.begin() + (long)(pos % b.size()));
        else if (!b.empty()) b[pos % b.size()] = "chg" + std::to_string(gen() % 5);
      }
      auto rows = textdiff::diff(a, b);
      checkConsistent(a, b, rows);
      std::vector<std::string> rebuilt;
      for (const Row & r : rows) if (r.right >= 0) rebuilt.push_back(r.kind == Row::Same ? a[(size_t)r.left] : b[(size_t)r.right]);
      assert(rebuilt == b);
      if (a == b) assert(textdiff::countBlocks(rows) == 0); else assert(textdiff::countBlocks(rows) >= 1);
    }
  }

  //--- large inputs stay fast and bounded (anchors, then the "one block" fallback) ---
  {
    std::vector<std::string> a, b;
    for (int i = 0; i < 200000; ++i) a.push_back("row " + std::to_string(i));
    b = a; b[100000] = "changed"; b.insert(b.begin() + 50, "inserted");
    auto rows = textdiff::diff(a, b);
    checkConsistent(a, b, rows);
    assert(textdiff::countBlocks(rows) == 2);
    std::vector<std::string> x(3000, "same"), y(3001, "other");     // no anchors, 9M cells: fallback block
    auto big = textdiff::diff(x, y);
    checkConsistent(x, y, big);
    assert(textdiff::countBlocks(big) == 1);
  }

  //--- looksBinary ---
  assert(!textdiff::looksBinary("plain text\n"));
  assert(textdiff::looksBinary(std::string("ab\0cd", 5)));
  assert(!textdiff::looksBinary(""));

  //--- Compare Directories ---
  {
    std::vector<engine::DirEntry> left = { D(".."), D("docs"), F("same.txt", 10, 1000), F("newer-left.txt", 10, 2000),
                                           F("older-left.txt", 10, 500), F("only-left.txt", 1, 1), F("size.bin", 5, 1000) };
    left[0].isParent = true;
    std::vector<engine::DirEntry> right = { D("docs"), D("only-right-dir"), F("same.txt", 10, 1000), F("newer-left.txt", 10, 1500),
                                            F("older-left.txt", 10, 900), F("only-right.txt", 1, 1), F("size.bin", 7, 1000) };
    CompareCriteria byTime;                                   // the default: time only
    CompareResult r = compareDirectories(left, right, byTime);
    assert(J(r.left) == "newer-left.txt,only-left.txt,");     // missing on the right, or newer here
    assert(J(r.right) == "older-left.txt,only-right.txt,");   // never directories
    CompareCriteria bySize; bySize.byTime = false; bySize.bySize = true;
    r = compareDirectories(left, right, bySize);
    assert(J(r.left) == "only-left.txt,size.bin,");
    assert(J(r.right) == "only-right.txt,size.bin,");
    CompareCriteria both; both.bySize = true;
    r = compareDirectories(left, right, both);
    assert(J(r.left) == "newer-left.txt,only-left.txt,size.bin,");
    assert(J(r.right) == "older-left.txt,only-right.txt,size.bin,");
    CompareCriteria none; none.byTime = false;                // only existence
    r = compareDirectories(left, right, none);
    assert(J(r.left) == "only-left.txt," && J(r.right) == "only-right.txt,");
    // identical listings: nothing marked
    r = compareDirectories(left, left, both);
    assert(r.left.empty() && r.right.empty());
    // a directory with the same name as a file is not a counterpart
    r = compareDirectories({ F("x", 1, 1) }, { D("x") }, byTime);
    assert(J(r.left) == "x," && r.right.empty());
    // precision: one side only knows minutes -> 12:00:05 is not newer than 12:00
    r = compareDirectories({ F("a", 1, 43205) }, { F("a", 1, 43200, false) }, byTime);
    assert(r.left.empty() && r.right.empty());
    r = compareDirectories({ F("a", 1, 43265) }, { F("a", 1, 43200, false) }, byTime);   // a minute later: newer
    assert(J(r.left) == "a,");
    // unknown time on either side: not "newer"
    r = compareDirectories({ F("a", 1, 500) }, { F("a", 1, 0) }, byTime);
    assert(r.left.empty() && r.right.empty());
    // names are compared exactly (a remote file system is case sensitive)
    r = compareDirectories({ F("Readme", 1, 1) }, { F("readme", 1, 1) }, byTime);
    assert(J(r.left) == "Readme," && J(r.right) == "readme,");
  }

  std::printf("ui logic: all assertions passed\n");
  return 0;
}
