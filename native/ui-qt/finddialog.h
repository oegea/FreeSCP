//---------------------------------------------------------------------------
// finddialog.h — "Find Files": recursive search of the local or remote panel's directory tree by
// WinSCP file mask, with results you can jump to.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_FINDDIALOG_H
#define WINSCP_UIQT_FINDDIALOG_H

#include <QString>
#include <functional>

class QWidget;

struct FindContext
{
  bool remote = false;
  QString startDir;
  // A remote search uses the session: `acquire` must return false if it is busy (a transfer is
  // running) and otherwise mark it busy until `release`.
  std::function<bool()> acquire;
  std::function<void()> release;
  // "Focus": show `dir` in the panel and select `name`.
  std::function<void(const QString & dir, const QString & name)> focus;
};

// `autoMask` (scripted runs): start searching for it at once, print the results and close.
void showFindDialog(QWidget * parent, const FindContext & ctx, const QString & autoMask = QString());

#endif
