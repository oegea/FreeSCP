//---------------------------------------------------------------------------
// panelcompare.h — WinSCP's "Compare Directories": which files of two listings differ.
//
// A file is marked in a panel when the other panel has no file of that name, or - depending on the
// criteria - when it is NEWER than its counterpart (by time) or has a different size (by size).
// Directories are never marked. Pure logic over engine::DirEntry, no Qt, so it is unit-tested.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_PANELCOMPARE_H
#define WINSCP_UIQT_PANELCOMPARE_H

#include <map>
#include <string>
#include <vector>
#include "enginebridge.h"

struct CompareCriteria
{
  bool byTime = true;    // WinSCP defaults: time on, size off
  bool bySize = false;
};

struct CompareResult
{
  std::vector<std::string> left;    // names to mark in the first listing
  std::vector<std::string> right;   // names to mark in the second listing
};

namespace panelcompare_detail {

// Is `a` newer than `b`? Compared at the coarser precision of the two listings: a listing that only
// has minutes (FTP LIST, `ls` over SCP for older files) must not make every file look different.
inline bool newer(const engine::DirEntry & a, const engine::DirEntry & b)
{
  if (a.mtimeSecs == 0 || b.mtimeSecs == 0) return false;   // unknown time: cannot tell
  if (a.mtimeHasSeconds && b.mtimeHasSeconds) return a.mtimeSecs > b.mtimeSecs;
  return (a.mtimeSecs / 60) > (b.mtimeSecs / 60);
}

inline void oneSide(const std::vector<engine::DirEntry> & mine, const std::vector<engine::DirEntry> & other,
                    const CompareCriteria & c, std::vector<std::string> & out)
{
  std::map<std::string, const engine::DirEntry *> byName;
  for (const engine::DirEntry & e : other)
    if (!e.isParent && !e.isDir) byName[e.name] = &e;
  for (const engine::DirEntry & e : mine)
  {
    if (e.isParent || e.isDir) continue;
    auto it = byName.find(e.name);
    bool differs;
    if (it == byName.end()) differs = true;                 // no such file on the other side
    else
    {
      differs = c.bySize && (e.size != it->second->size);
      if (!differs && c.byTime) differs = newer(e, *it->second);
    }
    if (differs) out.push_back(e.name);
  }
}

} // namespace panelcompare_detail

inline CompareResult compareDirectories(const std::vector<engine::DirEntry> & left,
                                        const std::vector<engine::DirEntry> & right,
                                        const CompareCriteria & criteria)
{
  CompareResult r;
  panelcompare_detail::oneSide(left, right, criteria, r.left);
  panelcompare_detail::oneSide(right, left, criteria, r.right);
  return r;
}

#endif
