//---------------------------------------------------------------------------
// enginebridge.h — clean boundary between the Qt UI and the ported engine.
//
// The engine + rtlcompat are compiled with -fshort-wchar (2-byte wchar_t, Delphi UTF-16) and
// expose UnicodeString everywhere. Qt is compiled normally (4-byte wchar_t, QString). Mixing
// the two ABIs in one translation unit is unsafe, so ALL engine access goes through this
// header, which uses only std types (UTF-8 std::string). enginebridge.cpp is the ONLY TU
// compiled with the engine flags; the Qt code includes just this header.
//
// This is the pattern the whole GUI uses to talk to the engine (local now; remote sessions
// the same way once the SSH/SFTP backend lands).
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_ENGINEBRIDGE_H
#define WINSCP_UIQT_ENGINEBRIDGE_H

#include <string>
#include <vector>
#include <cstdint>

namespace engine {

struct DirEntry
{
  std::string name;       // UTF-8
  std::int64_t size = 0;
  bool isDir = false;
  bool isParent = false;  // the ".." entry
  std::string modified;   // formatted timestamp (empty for "..")
};

// Engine version/banner (proves winscpcore is linked and callable).
std::string banner();

// List a local directory via the ported engine file enumeration (FindFirst/TSearchRec).
// Returns entries sorted dirs-first then by name; includes ".." unless at root.
std::vector<DirEntry> listLocalDir(const std::string & utf8Path);

// Path helpers (ported engine routines).
std::string homeDir();
std::string parentDir(const std::string & utf8Path);
std::string joinPath(const std::string & dir, const std::string & name);
std::string formatSize(std::int64_t bytes);

// Copy a local file (real file operation via the platform layer). Returns true on success.
bool copyFile(const std::string & srcUtf8, const std::string & dstUtf8);

} // namespace engine

#endif
