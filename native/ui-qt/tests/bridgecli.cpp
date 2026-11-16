//---------------------------------------------------------------------------
// bridgecli.cpp — drives the Qt UI's engine bridge (enginebridge.h) from the command line, with no
// Qt involved. The GUI reaches the engine only through that header, so this exercises exactly the
// code path a dialog uses: connection options (SSH tunnel, proxy, key files), authentication
// prompts, transfer settings, find, directory sizes. Used by native/scripts/e2e-features.sh.
//
//   freescp-bridge-cli key=value ... -- "command|arg|arg" ...
//
// Options:  host= port= user= pass= proto=sftp|scp|ftp|dav|s3  key=<file>  tls=0|1|implicit
//           passive=0|1  tunnel=host:port:user:pass  tunnelkey=<file>
//           proxy=socks4|socks5|http:host:port[:user:pass]
//           answers=a|b|c   (queue of answers for authentication prompts; unset = no prompt callback)
// Commands: see the dispatch table in main().
// Exit code: 0 = every command succeeded, 1 = connect failed, 2 = a command failed, 3 = usage.
//---------------------------------------------------------------------------
#include "enginebridge.h"

#include <cstdio>
#include <fcntl.h>
#include <cstdlib>
#include <deque>
#include <map>
#include <string>
#include <chrono>
#include <thread>
#include <vector>

namespace {

std::vector<std::string> split(const std::string & s, char sep)
{
  std::vector<std::string> out; std::string cur;
  for (char c : s) { if (c == sep) { out.push_back(cur); cur.clear(); } else cur += c; }
  out.push_back(cur);
  return out;
}

std::string arg(const std::vector<std::string> & a, size_t i, const std::string & def = std::string())
{ return (i < a.size()) ? a[i] : def; }

} // namespace

int main(int argc, char ** argv)
{
  std::map<std::string, std::string> opt;
  std::vector<std::string> commands;
  bool inCommands = false;
  for (int i = 1; i < argc; ++i)
  {
    std::string a = argv[i];
    if (!inCommands && a == "--") { inCommands = true; continue; }
    if (inCommands) { commands.push_back(a); continue; }
    size_t eq = a.find('=');
    if (eq == std::string::npos) { std::fprintf(stderr, "bad option: %s\n", a.c_str()); return 3; }
    opt[a.substr(0, eq)] = a.substr(eq + 1);
  }

  engine::ConnectOptions o;
  o.host = opt.count("host") ? opt["host"] : "127.0.0.1";
  o.user = opt["user"]; o.password = opt["pass"]; o.keyFile = opt["key"];
  const std::string proto = opt.count("proto") ? opt["proto"] : "sftp";
  o.protocol = proto == "scp" ? engine::Protocol::Scp : proto == "ftp" ? engine::Protocol::Ftp
             : proto == "dav" ? engine::Protocol::WebDav : proto == "s3" ? engine::Protocol::S3 : engine::Protocol::Sftp;
  o.port = opt.count("port") ? std::atoi(opt["port"].c_str()) : 22;
  if (opt.count("tls")) { o.tls = (opt["tls"] != "0"); o.ftpExplicitTls = (opt["tls"] != "implicit"); }
  if (opt.count("passive")) o.ftpPassive = (opt["passive"] != "0");
  if (opt.count("crlf")) o.serverCrLf = (opt["crlf"] != "0");
  if (opt.count("utf8")) o.utf8Names = (opt["utf8"] == "on") ? 1 : (opt["utf8"] == "off") ? 2 : 0;
  if (opt.count("tunnel"))
  {
    auto t = split(opt["tunnel"], ':');
    o.tunnel = true; o.tunnelHost = arg(t, 0); o.tunnelPort = std::atoi(arg(t, 1, "22").c_str());
    o.tunnelUser = arg(t, 2); o.tunnelPassword = arg(t, 3);
  }
  o.tunnelKeyFile = opt["tunnelkey"];
  if (opt.count("proxy"))
  {
    auto p = split(opt["proxy"], ':');
    o.proxyMethod = arg(p, 0) == "socks4" ? 1 : arg(p, 0) == "socks5" ? 2 : 3;
    o.proxyHost = arg(p, 1); o.proxyPort = std::atoi(arg(p, 2, "0").c_str());
    o.proxyUser = arg(p, 3); o.proxyPassword = arg(p, 4);
  }

  std::deque<std::string> answers;
  if (opt.count("answers"))
  {
    for (const std::string & a : split(opt["answers"], '|')) answers.push_back(a);
    engine::setPromptCallback([&answers](const engine::PromptRequest & rq, std::vector<std::string> & out) -> bool {
      std::printf("PROMPT kind=%d tunnel=%d fields=%zu name='%s' instructions='%s'\n",
                  rq.kind, rq.forTunnel ? 1 : 0, rq.prompts.size(), rq.name.c_str(), rq.instructions.c_str());
      for (size_t i = 0; i < rq.prompts.size(); ++i)
      {
        std::printf("  field[%zu] echo=%d label='%s'\n", i, rq.echo[i] ? 1 : 0, rq.prompts[i].c_str());
        if (answers.empty()) { std::printf("  (no answer left -> cancel)\n"); return false; }
        out[i] = answers.front(); answers.pop_front();
      }
      return true;
    });
  }
  engine::setConfirmCallback([](const std::string & msg) -> bool {
    std::printf("CONFIRM %.60s...\n", msg.c_str());
    return true;
  });

  engine::ConnectResult r = engine::connect(o);
  if (!r.ok) { std::printf("CONNECT FAILED: %s\n", r.error.c_str()); engine::shutdown(3000); return 1; }
  std::printf("CONNECTED cwd=%s\n", r.currentDir.c_str());

  int failed = 0;
  for (const std::string & c : commands)
  {
    std::vector<std::string> a = split(c, '|');
    const std::string & cmd = a[0];
    std::string err; bool ok = true;
    if (cmd == "ls")
    {
      auto list = engine::listRemoteDir(arg(a, 1), &err);
      ok = err.empty();
      std::printf("LS %s : %zu entries\n", engine::remoteCurrentDir().c_str(), list.size());
      for (const auto & e : list)
        std::printf("  %c %12lld %s  [%s] mtime=%lld%s\n", e.isDir ? 'd' : '-', (long long)e.size, e.name.c_str(),
                    e.modified.c_str(), (long long)e.mtimeSecs, e.mtimeHasSeconds ? "" : " (minutes)");
    }
    else if (cmd == "put") ok = engine::uploadToRemote(arg(a, 1), arg(a, 2), &err);
    else if (cmd == "putplain") ok = engine::uploadToRemote(arg(a, 1), arg(a, 2), &err, true);
    else if (cmd == "getplain")
    {
      std::string path = arg(a, 1); size_t slash = path.rfind('/');
      engine::listRemoteDir(slash == 0 ? "/" : path.substr(0, slash));
      ok = engine::downloadFromRemote(path, arg(a, 2), &err, true);
    }
    else if (cmd == "cancelafter")   // cancelafter|<bytes> : the next transfers are cancelled once that much went through (0 = off)
    {
      const long long limit = std::atoll(arg(a, 1, "0").c_str());
      if (limit <= 0) engine::setProgressSink(nullptr);
      else engine::setProgressSink([limit](const engine::TransferProgress & tp) { return tp.transferred >= limit; });
    }
    else if (cmd == "get")
    {
      // downloadFromRemote looks the file up in the current listing, like the panel does.
      std::string path = arg(a, 1); size_t slash = path.rfind('/');
      engine::listRemoteDir(slash == 0 ? "/" : path.substr(0, slash));
      ok = engine::downloadFromRemote(path, arg(a, 2), &err);
    }
    else if (cmd == "set")   // set|key=value|key=value... : transfer settings (unset keys keep their value)
    {
      engine::TransferSettings x = engine::transferSettings();
      for (size_t i = 1; i < a.size(); ++i)
      {
        size_t eq = a[i].find('=');
        std::string k = a[i].substr(0, eq), v = (eq == std::string::npos) ? std::string() : a[i].substr(eq + 1);
        if (k == "mode") x.mode = (v == "text") ? 1 : (v == "auto") ? 2 : 0;
        else if (k == "textmask") x.textMask = v;
        else if (k == "time") x.preserveTime = (v != "0");
        else if (k == "rights") { x.setRights = !v.empty(); x.rightsOctal = v; }
        else if (k == "case") x.fileNameCase = (v == "upper") ? 1 : (v == "lower") ? 2 : 0;
        else if (k == "resume") x.resume = (v == "on") ? 0 : (v == "off") ? 2 : 1;
        else if (k == "threshold") x.resumeThresholdKB = std::atoll(v.c_str());
        else if (k == "speed") x.speedLimitKB = std::atoi(v.c_str());
        else if (k == "mask") { for (char & c : v) if (c == '^') c = '|'; x.fileMask = v; }   // '^' stands for '|' (the argument separator)
        else if (k == "newer") x.newerOnly = (v != "0");
        else if (k == "hidden") x.excludeHidden = (v == "exclude");
        else { ok = false; err = "unknown setting " + k; }
      }
      engine::setTransferSettings(x);
    }
    else if (cmd == "checkmask") ok = engine::validateFileMask(arg(a, 1), &err);
    else if (cmd == "find" || cmd == "lfind")   // find|<dir>|<mask>[|<stop after N>]
    {
      int n = 0, dirs = 0; const int limit = std::atoi(arg(a, 3, "0").c_str());
      std::string mask = arg(a, 2, "*"); for (char & c : mask) if (c == '^') c = '|';
      auto onFound = [&](const engine::FoundFile & f) -> bool {
        std::printf("  FOUND %c %10lld %s   (dir=%s name=%s) [%s]\n", f.isDir ? 'd' : '-', (long long)f.size, f.path.c_str(), f.dir.c_str(), f.name.c_str(), f.modified.c_str());
        return (++n >= limit) && (limit > 0);
      };
      auto onDir = [&](const std::string &) -> bool { ++dirs; return false; };
      ok = (cmd == "find") ? engine::remoteFind(arg(a, 1), mask, onFound, onDir, &err)
                           : engine::localFind(arg(a, 1), mask, onFound, onDir, &err);
      std::printf("FIND matches=%d directories-searched=%d\n", n, dirs);
      if (ok && !err.empty()) { std::printf("  (warning: %s)\n", err.c_str()); err.clear(); }
    }
    else if (cmd == "du" || cmd == "ldu")   // du|<name in current dir>   ldu|<local path>
    {
      engine::SizeResult r;
      if (cmd == "du") { engine::listRemoteDir(std::string()); ok = engine::remoteCalculateSize(arg(a, 1), r, nullptr, &err); }
      else ok = engine::localCalculateSize(arg(a, 1), r, nullptr, &err);
      std::printf("DU bytes=%lld files=%d dirs=%d\n", (long long)r.bytes, r.files, r.dirs);
    }
    else if (cmd == "sync")   // sync|<local dir>|<remote dir>|up|down|both[|delete][|dry]
    {
      const std::string dir = arg(a, 3, "up");
      const int mode = (dir == "down") ? 1 : (dir == "both") ? 2 : 0;
      bool del = false, dry = false;
      for (size_t i = 4; i < a.size(); ++i) { if (a[i] == "delete") del = true; if (a[i] == "dry") dry = true; }
      auto items = engine::synchronizeCollect(arg(a, 1), arg(a, 2), mode, del, &err);
      ok = err.empty();
      std::printf("SYNC %zu change(s)\n", items.size());
      for (const auto & it : items) std::printf("  %s %s%s\n", it.action.c_str(), it.name.c_str(), it.isDir ? "/" : "");
      if (ok && !dry && !items.empty()) ok = engine::synchronizeApply(&err);
      engine::synchronizeRelease();
    }
    else if (cmd == "fds")   // number of open file descriptors of this process (leak checks)
    {
      int n = 0;
      for (int fd = 0; fd < 4096; ++fd) if (::fcntl(fd, F_GETFD) != -1) ++n;
      std::printf("FDS %d\n", n);
    }
    else if (cmd == "sleep") std::this_thread::sleep_for(std::chrono::seconds(std::atoi(arg(a, 1, "1").c_str())));
    else if (cmd == "mkdir") ok = engine::remoteMakeDir(arg(a, 1), &err);
    else if (cmd == "rm") { engine::listRemoteDir(std::string()); ok = engine::remoteDelete(arg(a, 1), &err); }
    else if (cmd == "parallel")
    {
      std::printf("PARALLEL supported=%d\n", engine::parallelSupported() ? 1 : 0);
      if (engine::parallelSupported())
      {
        int h = engine::openParallelConnection(&err);
        ok = (h > 0);
        if (ok && a.size() >= 3) ok = engine::uploadVia(h, arg(a, 1), arg(a, 2), &err);
        engine::closeParallelConnections();
      }
    }
    else { std::fprintf(stderr, "unknown command: %s\n", cmd.c_str()); engine::shutdown(3000); return 3; }
    std::printf("%s %s%s%s\n", ok ? "OK  " : "FAIL", c.c_str(), err.empty() ? "" : " : ", err.c_str());
    if (!ok) failed = 1;
  }

  engine::shutdown(3000);
  return failed ? 2 : 0;
}
