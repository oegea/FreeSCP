//---------------------------------------------------------------------------
// textdiff.h — line-based comparison of two texts, as rows for a side-by-side view.
// No Qt, no engine: plain std, so it is unit-tested on its own.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_TEXTDIFF_H
#define WINSCP_UIQT_TEXTDIFF_H

#include <string>
#include <vector>

namespace textdiff {

struct Row
{
  enum Kind { Same, Changed, LeftOnly, RightOnly };
  Kind kind = Same;
  int left = -1;     // 0-based line index in the left text, -1 = no line on this side (filler)
  int right = -1;
};

struct Options
{
  bool ignoreLineEndings = true;   // "a\r\n" equals "a\n"
  bool ignoreTrailingSpace = false;
  bool ignoreCase = false;
};

// Split into lines. A final line without a terminator is a line; a text ending in a newline does
// not get an extra empty line. The terminator is not part of the line; `crlf` (optional) reports
// whether any line ended in CR LF.
std::vector<std::string> splitLines(const std::string & text, bool * crlf = nullptr);

// The aligned rows: unchanged lines side by side, a changed block as paired Changed rows followed
// by the surplus LeftOnly / RightOnly rows.
std::vector<Row> diff(const std::vector<std::string> & left, const std::vector<std::string> & right,
                      const Options & options = Options());

// Number of difference blocks (maximal runs of non-Same rows).
int countBlocks(const std::vector<Row> & rows);

// Heuristic used before offering a text comparison: NUL bytes in the first 8 KB.
bool looksBinary(const std::string & data);

} // namespace textdiff

#endif
