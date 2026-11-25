//---------------------------------------------------------------------------
// localops.h — operations on local files the panels need beyond the engine bridge: permissions and
// ownership (Properties), links, copy/move within the local side (Duplicate / Move To). POSIX.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_LOCALOPS_H
#define WINSCP_UIQT_LOCALOPS_H

#include <QDir>
#include <QFile>
#include <cstdlib>
#include <QString>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <grp.h>
#include <pwd.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

// A directory only this user can read, for the copies of remote files the app works on (edit, open,
// compare, drag out): created once per run with mkdtemp (mode 0700, unpredictable name) - a fixed
// name under /tmp is shared with every other user of a Linux machine. Removed by removePrivateTemp().
inline QString & privateTempBaseRef() { static QString base; return base; }
inline QString privateTempDir(const QString & sub = QString())
{
  QString & base = privateTempBaseRef();
  if (base.isEmpty())
  {
    QByteArray tmpl = QFile::encodeName(QDir::tempPath() + "/freescp-XXXXXX");
    const char * made = ::mkdtemp(tmpl.data());
    base = (made != nullptr) ? QFile::decodeName(made) : QDir::tempPath();
  }
  if (sub.isEmpty()) return base;
  QDir().mkpath(base + "/" + sub);
  return base + "/" + sub;
}
inline void removePrivateTemp()
{
  QString & base = privateTempBaseRef();
  if (!base.isEmpty() && base != QDir::tempPath() && base.contains("/freescp-")) QDir(base).removeRecursively();
  base.clear();
}

struct LocalFileInfo
{
  int mode = -1;          // permission bits, including setuid/setgid/sticky (07777)
  QString owner, group;   // names, or numeric ids when there is no name
};

inline bool localFileInfo(const QString & path, LocalFileInfo & out)
{
  struct stat st;
  if (::stat(QFile::encodeName(path).constData(), &st) != 0 && ::lstat(QFile::encodeName(path).constData(), &st) != 0) return false;
  out.mode = static_cast<int>(st.st_mode & 07777);
  const struct passwd * pw = ::getpwuid(st.st_uid);
  const struct group * gr = ::getgrgid(st.st_gid);
  out.owner = (pw != nullptr) ? QString::fromLocal8Bit(pw->pw_name) : QString::number(st.st_uid);
  out.group = (gr != nullptr) ? QString::fromLocal8Bit(gr->gr_name) : QString::number(st.st_gid);
  return true;
}

inline bool localChmod(const QString & path, int mode, QString * error)
{
  if (::chmod(QFile::encodeName(path).constData(), static_cast<mode_t>(mode & 07777)) == 0) return true;
  if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
  return false;
}

// A link at `linkPath` pointing to `target`. A symbolic link stores `target` as typed (it may be
// relative to the link's directory); a hard link needs the real file: a relative `target` is taken
// from `baseDir`.
inline bool localCreateLink(const QString & linkPath, const QString & target, bool symbolic, const QString & baseDir, QString * error)
{
  int rc;
  if (symbolic) rc = ::symlink(QFile::encodeName(target).constData(), QFile::encodeName(linkPath).constData());
  else
  {
    const QString real = target.startsWith('/') ? target : baseDir + "/" + target;
    rc = ::link(QFile::encodeName(real).constData(), QFile::encodeName(linkPath).constData());
  }
  if (rc == 0) return true;
  if (error) *error = QString::fromLocal8Bit(std::strerror(errno));
  return false;
}

// Copy or move `src` to the full path `dst` (files, links and whole directories). Never overwrites.
inline bool localCopyOrMove(const QString & src, const QString & dst, bool move, QString * error)
{
  namespace fs = std::filesystem;
  const fs::path from(QFile::encodeName(src).constData()), to(QFile::encodeName(dst).constData());
  std::error_code ec;
  auto fail = [&](const QString & m) { if (error) *error = m; return false; };
  if (!fs::exists(fs::symlink_status(from, ec))) return fail("The source does not exist.");
  const fs::path fromN = from.lexically_normal(), toN = to.lexically_normal();
  if (fromN == toN) return fail("The source and the target are the same file.");
  // (on a case-insensitive volume "File.txt" exists when "file.txt" does: renaming to another case is fine)
  const bool sameEntry = move && fs::equivalent(from, to, ec) && fromN.parent_path() == toN.parent_path();
  ec.clear();
  if (!sameEntry && fs::exists(fs::symlink_status(to, ec))) return fail(QString("\"%1\" already exists.").arg(dst));
  if (!fs::is_directory(to.parent_path(), ec)) return fail(QString("The directory \"%1\" does not exist.").arg(QFile::decodeName(to.parent_path().c_str())));
  const bool isDir = fs::is_directory(fs::symlink_status(from, ec));
  if (isDir)
  {
    const std::string a = fromN.string() + "/", b = toN.string() + "/";
    if (b.compare(0, a.size(), a) == 0) return fail("A directory cannot be copied or moved into itself.");
  }
  if (move)
  {
    fs::rename(from, to, ec);
    if (!ec) return true;
    if (ec != std::errc::cross_device_link) return fail(QString::fromLocal8Bit(ec.message().c_str()));
    ec.clear();       // another file system: copy, then remove the original
  }
  fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
  if (ec) return fail(QString::fromLocal8Bit(ec.message().c_str()));
  if (move) { fs::remove_all(from, ec); if (ec) return fail(QString::fromLocal8Bit(ec.message().c_str())); }
  return true;
}

#endif
