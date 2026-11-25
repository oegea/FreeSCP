//---------------------------------------------------------------------------
// keeplogic.h — pure logic of "Keep remote directory up to date" (no Qt, no engine), unit-tested:
// given two snapshots of a local directory tree, which directories have to be synchronized.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_KEEPLOGIC_H
#define WINSCP_UIQT_KEEPLOGIC_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace keeplogic {

struct Stamp
{
  std::int64_t size = 0;
  std::int64_t mtimeMs = 0;
  bool isDir = false;
  bool operator==(const Stamp & o) const { return size == o.size && mtimeMs == o.mtimeMs && isDir == o.isDir; }
};
// relative path with "/" separators ("a.txt", "sub/b.txt") -> what was seen
typedef std::map<std::string, Stamp> Snapshot;

// "sub/b.txt" -> "sub"; "a.txt" -> "" (the watched directory itself)
inline std::string parentOf(const std::string & rel)
{
  const size_t slash = rel.rfind('/');
  return (slash == std::string::npos) ? std::string() : rel.substr(0, slash);
}

// Is `dir` the same as, or an ancestor directory of, `path`? ("" is an ancestor of everything.)
inline bool covers(const std::string & dir, const std::string & path)
{
  if (dir.empty()) return true;
  if (path.size() < dir.size() || path.compare(0, dir.size(), dir) != 0) return false;
  return path.size() == dir.size() || path[dir.size()] == '/';
}

// The directories to synchronize so that every difference between the two snapshots is taken
// care of: the parent of each entry that appeared, disappeared or changed - a directory is listed
// only if none of its ancestors is (synchronizing the ancestor does it). Sorted. Empty = no change.
// A directory's own time stamp changing is not a change (its entries are compared one by one).
inline std::vector<std::string> changedRoots(const Snapshot & before, const Snapshot & after)
{
  std::set<std::string> dirs;
  for (const auto & kv : after)
  {
    auto it = before.find(kv.first);
    if (it == before.end()) { dirs.insert(parentOf(kv.first)); continue; }                 // new
    const Stamp & a = it->second, & b = kv.second;
    if (a.isDir != b.isDir) { dirs.insert(parentOf(kv.first)); continue; }                 // file <-> directory
    if (!b.isDir && !(a == b)) dirs.insert(parentOf(kv.first));                            // modified file
  }
  for (const auto & kv : before)
    if (after.find(kv.first) == after.end()) dirs.insert(parentOf(kv.first));              // gone
  // A directory that no longer exists cannot be a root (it is in `before` only): its parent is in
  // the set already, because the directory itself counts as "gone".
  std::vector<std::string> out;
  for (const std::string & d : dirs)
  {
    if (!d.empty() && after.find(d) == after.end()) continue;
    bool covered = false;
    for (const std::string & o : dirs) if (o != d && covers(o, d) && (o.empty() || after.find(o) != after.end())) { covered = true; break; }
    if (!covered) out.push_back(d);
  }
  return out;
}

} // namespace keeplogic

#endif
