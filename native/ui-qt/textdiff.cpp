//---------------------------------------------------------------------------
// textdiff.cpp — see textdiff.h.
//
// Algorithm: patience diff. Lines that occur exactly once on both sides are anchors; the longest
// increasing run of anchors is kept (they are certainly "the same line") and the gaps between
// anchors are solved recursively. A gap without anchors falls back to a classic LCS table when it is
// small, and is reported as one changed block when it is not. That keeps time and memory bounded on
// large files and gives the diffs people expect (moved blocks do not smear across the file).
//---------------------------------------------------------------------------
#include "textdiff.h"

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace textdiff {

namespace {

std::string normalize(const std::string & line, const Options & o)
{
  std::string s = line;
  if (o.ignoreLineEndings && !s.empty() && s.back() == '\r') s.pop_back();
  if (o.ignoreTrailingSpace)
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
  if (o.ignoreCase)
    for (char & c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

// Work on integer ids: equal lines get equal ids.
struct Ctx
{
  std::vector<int> a, b;
  std::vector<Row> out;
};

void emitBlock(Ctx & c, int alo, int ahi, int blo, int bhi)
{
  int na = ahi - alo, nb = bhi - blo, paired = std::min(na, nb);
  for (int i = 0; i < paired; ++i) { Row r; r.kind = Row::Changed; r.left = alo + i; r.right = blo + i; c.out.push_back(r); }
  for (int i = paired; i < na; ++i) { Row r; r.kind = Row::LeftOnly; r.left = alo + i; c.out.push_back(r); }
  for (int i = paired; i < nb; ++i) { Row r; r.kind = Row::RightOnly; r.right = blo + i; c.out.push_back(r); }
}

void emitSame(Ctx & c, int ai, int bi)
{
  Row r; r.kind = Row::Same; r.left = ai; r.right = bi; c.out.push_back(r);
}

// Classic LCS for a small gap.
void lcs(Ctx & c, int alo, int ahi, int blo, int bhi)
{
  const int n = ahi - alo, m = bhi - blo;
  std::vector<int> t(static_cast<size_t>(n + 1) * (m + 1), 0);
  auto at = [&](int i, int j) -> int & { return t[static_cast<size_t>(i) * (m + 1) + j]; };
  for (int i = n - 1; i >= 0; --i)
    for (int j = m - 1; j >= 0; --j)
      at(i, j) = (c.a[alo + i] == c.b[blo + j]) ? at(i + 1, j + 1) + 1 : std::max(at(i + 1, j), at(i, j + 1));
  int i = 0, j = 0, di = 0, dj = 0;   // [di,i) x [dj,j) = pending changed block
  while (i < n && j < m)
  {
    if (c.a[alo + i] == c.b[blo + j])
    {
      emitBlock(c, alo + di, alo + i, blo + dj, blo + j);
      emitSame(c, alo + i, blo + j);
      ++i; ++j; di = i; dj = j;
    }
    else if (at(i + 1, j) >= at(i, j + 1)) ++i;
    else ++j;
  }
  emitBlock(c, alo + di, ahi, blo + dj, bhi);
}

void solve(Ctx & c, int alo, int ahi, int blo, int bhi)
{
  // common prefix
  while (alo < ahi && blo < bhi && c.a[alo] == c.b[blo]) { emitSame(c, alo, blo); ++alo; ++blo; }
  // common suffix (emitted after the middle)
  int sa = ahi, sb = bhi;
  while (sa > alo && sb > blo && c.a[sa - 1] == c.b[sb - 1]) { --sa; --sb; }

  if (alo == sa || blo == sb)
  {
    emitBlock(c, alo, sa, blo, sb);
  }
  else
  {
    // anchors: ids occurring exactly once in each range
    std::unordered_map<int, std::pair<int, int>> count;   // id -> (#in a, #in b)
    std::unordered_map<int, int> posA, posB;
    for (int i = alo; i < sa; ++i) { ++count[c.a[i]].first; posA[c.a[i]] = i; }
    for (int j = blo; j < sb; ++j) { ++count[c.b[j]].second; posB[c.b[j]] = j; }
    std::vector<std::pair<int, int>> pairs;               // (pos in a, pos in b), ordered by a
    for (int i = alo; i < sa; ++i)
    {
      auto it = count.find(c.a[i]);
      if (it->second.first == 1 && it->second.second == 1) pairs.push_back({ i, posB[c.a[i]] });
    }
    // longest increasing subsequence on the b positions (patience sorting)
    std::vector<int> tails, tailIdx, prev(pairs.size(), -1);
    for (size_t k = 0; k < pairs.size(); ++k)
    {
      auto it = std::lower_bound(tails.begin(), tails.end(), pairs[k].second);
      size_t pos = static_cast<size_t>(it - tails.begin());
      if (it == tails.end()) { tails.push_back(pairs[k].second); tailIdx.push_back(static_cast<int>(k)); }
      else { *it = pairs[k].second; tailIdx[pos] = static_cast<int>(k); }
      prev[k] = (pos > 0) ? tailIdx[pos - 1] : -1;
    }
    std::vector<std::pair<int, int>> anchors;
    for (int k = tailIdx.empty() ? -1 : tailIdx.back(); k >= 0; k = prev[static_cast<size_t>(k)]) anchors.push_back(pairs[static_cast<size_t>(k)]);
    std::reverse(anchors.begin(), anchors.end());

    if (anchors.empty())
    {
      const long long cells = static_cast<long long>(sa - alo) * (sb - blo);
      if (cells <= 4000000LL) lcs(c, alo, sa, blo, sb);
      else emitBlock(c, alo, sa, blo, sb);
    }
    else
    {
      int ca = alo, cb = blo;
      for (const auto & an : anchors)
      {
        solve(c, ca, an.first, cb, an.second);
        emitSame(c, an.first, an.second);
        ca = an.first + 1; cb = an.second + 1;
      }
      solve(c, ca, sa, cb, sb);
    }
  }

  for (int i = sa, j = sb; i < ahi; ++i, ++j) emitSame(c, i, j);
}

} // namespace

std::vector<std::string> splitLines(const std::string & text, bool * crlf)
{
  std::vector<std::string> lines;
  if (crlf) *crlf = false;
  size_t start = 0;
  while (start < text.size())
  {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) { lines.push_back(text.substr(start)); break; }
    size_t end = nl;
    if (end > start && text[end - 1] == '\r') { --end; if (crlf) *crlf = true; }
    lines.push_back(text.substr(start, end - start));
    start = nl + 1;
  }
  return lines;
}

std::vector<Row> diff(const std::vector<std::string> & left, const std::vector<std::string> & right, const Options & options)
{
  Ctx c;
  std::unordered_map<std::string, int> ids;
  auto idOf = [&](const std::string & line) {
    auto it = ids.find(line);
    if (it != ids.end()) return it->second;
    int id = static_cast<int>(ids.size());
    ids.emplace(line, id);
    return id;
  };
  c.a.reserve(left.size()); c.b.reserve(right.size());
  for (const std::string & l : left) c.a.push_back(idOf(normalize(l, options)));
  for (const std::string & r : right) c.b.push_back(idOf(normalize(r, options)));
  c.out.reserve(std::max(left.size(), right.size()));
  solve(c, 0, static_cast<int>(c.a.size()), 0, static_cast<int>(c.b.size()));
  return c.out;
}

int countBlocks(const std::vector<Row> & rows)
{
  int n = 0; bool in = false;
  for (const Row & r : rows)
  {
    if (r.kind != Row::Same) { if (!in) { ++n; in = true; } }
    else in = false;
  }
  return n;
}

bool looksBinary(const std::string & data)
{
  const size_t n = std::min<size_t>(data.size(), 8192);
  for (size_t i = 0; i < n; ++i) if (data[i] == '\0') return true;
  return false;
}

} // namespace textdiff
