//---------------------------------------------------------------------------
// keepuptodate.h — WinSCP's "Keep remote directory up to date": watch a local directory and
// reflect every change on a remote directory until stopped.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_KEEPUPTODATE_H
#define WINSCP_UIQT_KEEPUPTODATE_H

#include <QString>
#include <functional>

class QWidget;

struct KeepUpToDateContext
{
  QString localDir, remoteDir;            // proposed (the panels' directories)
  // Take / give back the engine for one synchronization (the main window's "a transfer is running"
  // gate). acquire() returns false if the engine is busy.
  std::function<bool()> acquire;
  std::function<void()> release;
  std::function<void(const QString &)> log;   // session log line
};

// Shows the dialog; returns when it is closed (watching has stopped by then).
// Scripted runs: WINSCP_KEEP_MS=<n> starts watching at once and stops and closes after n ms.
void showKeepUpToDateDialog(QWidget * parent, const KeepUpToDateContext & context);

#endif
