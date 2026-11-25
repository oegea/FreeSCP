//---------------------------------------------------------------------------
// keepuptodate.cpp — see keepuptodate.h.
//
// The local tree is polled (path, size, time of every entry): portable, and it also sees a file
// rewritten in place, which directory-change notifications do not report on every platform. Each
// difference is synchronized through the engine's own Synchronize (local -> remote), restricted to
// the directories that changed (keeplogic.h), on a worker thread so Stop stays responsive.
//---------------------------------------------------------------------------
#include "keepuptodate.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <thread>

#include "enginebridge.h"
#include "keeplogic.h"
#include "uicommon.h"

namespace {

keeplogic::Snapshot takeSnapshot(const QString & root, bool subdirs, int * entries)
{
  keeplogic::Snapshot snap;
  const QDir base(root);
  QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                  subdirs ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
  while (it.hasNext())
  {
    it.next();
    const QFileInfo fi = it.fileInfo();
    keeplogic::Stamp st;
    st.isDir = fi.isDir() && !fi.isSymLink();
    st.size = st.isDir ? 0 : fi.size();
    st.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    snap[s8(base.relativeFilePath(fi.filePath()))] = st;
  }
  if (entries) *entries = static_cast<int>(snap.size());
  return snap;
}

class KeepDialog : public QDialog
{
public:
  KeepDialog(QWidget * parent, const KeepUpToDateContext & ctx) : QDialog(parent), FCtx(ctx)
  {
    setWindowTitle("Keep remote directory up to date");
    resize(620, 460);
    auto * lay = new QVBoxLayout(this);

    lay->addWidget(new QLabel("&Watch for changes in the local directory:"));
    auto * localRow = new QHBoxLayout;
    FLocal = new QLineEdit(ctx.localDir); FLocal->setObjectName("local");
    auto * browse = new QPushButton("B&rowse\xE2\x80\xA6");
    localRow->addWidget(FLocal, 1); localRow->addWidget(browse);
    lay->addLayout(localRow);
    if (auto * l = qobject_cast<QLabel *>(lay->itemAt(0)->widget())) l->setBuddy(FLocal);
    auto * remoteLabel = new QLabel("\xE2\x80\xA6" "and automatically reflect them on the &remote directory:");
    FRemote = new QLineEdit(ctx.remoteDir); FRemote->setObjectName("remote");
    remoteLabel->setBuddy(FRemote);
    lay->addWidget(remoteLabel); lay->addWidget(FRemote);

    auto * opts = new QGroupBox("Synchronize options");
    auto * ol = new QVBoxLayout(opts);
    QSettings s;
    FDelete = new QCheckBox("&Delete files (a file removed locally is removed on the server)"); FDelete->setObjectName("delete");
    FDelete->setChecked(s.value("keepuptodate/delete", false).toBool());
    FSubdirs = new QCheckBox("Update &subdirectories"); FSubdirs->setObjectName("subdirs");
    FSubdirs->setChecked(s.value("keepuptodate/subdirs", true).toBool());
    FInitial = new QCheckBox("Synchronize the whole directory when &starting"); FInitial->setObjectName("initial");
    FInitial->setChecked(s.value("keepuptodate/initial", false).toBool());
    ol->addWidget(FDelete); ol->addWidget(FSubdirs); ol->addWidget(FInitial);
    lay->addWidget(opts);

    FLog = new QListWidget; FLog->setObjectName("log");
    FLog->setAlternatingRowColors(true);
    lay->addWidget(FLog, 1);
    FStatus = new QLabel("Not watching."); FStatus->setStyleSheet("color: gray;");
    lay->addWidget(FStatus);

    auto * btns = new QHBoxLayout;
    FStart = new QPushButton("&Start"); FStart->setDefault(true);
    FStop = new QPushButton("S&top"); FStop->setEnabled(false);
    FClose = new QPushButton("&Close");
    btns->addStretch(1); btns->addWidget(FStart); btns->addWidget(FStop); btns->addWidget(FClose);
    lay->addLayout(btns);

    FTimer = new QTimer(this);
    connect(FTimer, &QTimer::timeout, this, [this] { poll(); });
    connect(FStart, &QPushButton::clicked, this, [this] { start(); });
    connect(FStop, &QPushButton::clicked, this, [this] { stop(); });
    connect(FClose, &QPushButton::clicked, this, [this] { close(); });
    connect(browse, &QPushButton::clicked, this, [this] {
      const QString d = QFileDialog::getExistingDirectory(this, "Local directory to watch", FLocal->text());
      if (!d.isEmpty()) FLocal->setText(d);
    });
  }

  void start()
  {
    if (FWatching) return;
    FRoot = QDir::cleanPath(FLocal->text().trimmed());
    FRemoteRoot = FRemote->text().trimmed();
    while (FRemoteRoot.size() > 1 && FRemoteRoot.endsWith('/')) FRemoteRoot.chop(1);
    if (FRoot.isEmpty() || !QFileInfo(FRoot).isDir())
    { appMessage(this, windowTitle(), QString("The local directory \"%1\" does not exist.").arg(FLocal->text())); return; }
    if (FRemoteRoot.isEmpty()) { appMessage(this, windowTitle(), "Enter the remote directory."); return; }
    // the remote directory must exist and be readable before anything is deleted or uploaded on its account
    if (!FCtx.acquire || !FCtx.acquire()) { appMessage(this, windowTitle(), "Another operation is running. Try again when it has finished."); return; }
    std::string err;
    engine::listRemoteDir(s8(FRemoteRoot), &err);
    if (FCtx.release) FCtx.release();
    if (!err.empty()) { appMessage(this, windowTitle(), QString("The remote directory \"%1\" cannot be opened:\n\n%2").arg(FRemoteRoot, u8(err))); return; }

    QSettings s;
    s.setValue("keepuptodate/delete", FDelete->isChecked()); s.setValue("keepuptodate/subdirs", FSubdirs->isChecked());
    s.setValue("keepuptodate/initial", FInitial->isChecked());
    FWatching = true; FStopping = false;
    for (QWidget * w : std::initializer_list<QWidget *>{ FLocal, FRemote, FDelete, FSubdirs, FInitial, FStart }) w->setEnabled(false);
    FStop->setEnabled(true);
    int n = 0;
    FSnapshot = takeSnapshot(FRoot, FSubdirs->isChecked(), &n);
    line(QString("Watching %1 (%2 entries) -> %3").arg(FRoot).arg(n).arg(FRemoteRoot));
    setStatus();
    FTimer->start(1000);
    if (FInitial->isChecked()) synchronize(std::vector<std::string>{ std::string() });
  }

  void stop()
  {
    if (!FWatching) return;
    FTimer->stop();
    if (FBusy) { FStopping = true; FStatus->setText("Stopping after the current synchronization\xE2\x80\xA6"); FStop->setEnabled(false); return; }
    FWatching = false; FStopping = false;
    for (QWidget * w : std::initializer_list<QWidget *>{ FLocal, FRemote, FDelete, FSubdirs, FInitial, FStart }) w->setEnabled(true);
    FStop->setEnabled(false);
    line("Stopped.");
    setStatus();
    if (FCloseWhenStopped) accept();
  }

  // scripted runs
  void startSoonAndStopAfter(int ms)
  {
    QTimer::singleShot(50, this, [this] { start(); if (!FWatching) { reject(); } });
    QTimer::singleShot(ms, this, [this] { FCloseWhenStopped = true; if (FWatching) stop(); else accept(); });
  }
  QStringList logLines() const { QStringList out; for (int i = 0; i < FLog->count(); ++i) out << FLog->item(i)->text(); return out; }

protected:
  void closeEvent(QCloseEvent * e) override
  {
    if (FWatching) { FCloseWhenStopped = true; stop(); if (FWatching) { e->ignore(); return; } }
    e->accept();
  }
  void reject() override { close(); }      // Escape: stop first

private:
  void line(const QString & text)
  {
    const QString l = QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text;
    FLog->addItem(l); FLog->scrollToBottom();
    while (FLog->count() > 2000) delete FLog->takeItem(0);
    if (FCtx.log) FCtx.log("[keep up to date] " + text);
  }
  void setStatus()
  {
    FStatus->setText(!FWatching ? QString("Not watching.")
                     : QString("Watching for changes\xE2\x80\xA6  %1 change(s) synchronized, %2 error(s).").arg(FDone).arg(FErrors));
  }

  void poll()
  {
    if (!FWatching || FBusy) return;
    if (!QFileInfo(FRoot).isDir()) { line("The local directory is gone."); ++FErrors; stop(); return; }
    QElapsedTimer t; t.start();
    keeplogic::Snapshot now = takeSnapshot(FRoot, FSubdirs->isChecked(), nullptr);
    FTimer->setInterval(qBound(1000, int(t.elapsed()) * 20, 30000));     // a big tree is scanned less often
    std::vector<std::string> roots = keeplogic::changedRoots(FSnapshot, now);
    if (!roots.empty())
    {
      // A file still being written changes again at the next poll: wait until two polls agree.
      if (!(now == FPending)) { FPending = now; return; }
      FPending.clear();
      FSnapshot = now;
    }
    // Directories whose synchronization failed are tried again - those only, never "everything"
    // (with "Delete files" that would remove remote files in directories that never changed) -
    // and not more often than every ten seconds.
    if (!FRetry.empty() && (!roots.empty() || FRetryTimer.elapsed() >= 10000))
    {
      for (const std::string & r : FRetry)
      {
        std::string d = r;      // a directory that is gone meanwhile: its nearest ancestor that exists
        while (!d.empty() && !QFileInfo(FRoot + "/" + u8(d)).isDir()) d = keeplogic::parentOf(d);
        roots.push_back(d);
      }
      FRetry.clear();
      std::sort(roots.begin(), roots.end());
      roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
      std::vector<std::string> minimal;
      for (const std::string & d : roots)
      {
        bool covered = false;
        for (const std::string & o : roots) if (o != d && keeplogic::covers(o, d)) { covered = true; break; }
        if (!covered) minimal.push_back(d);
      }
      roots = minimal;
    }
    if (roots.empty()) return;
    synchronize(roots);
  }

  void synchronize(const std::vector<std::string> & roots)
  {
    if (!FCtx.acquire || !FCtx.acquire()) { line("Another operation is running: will retry."); FRetry = roots; FRetryTimer.start(); return; }
    FBusy = true;
    FStatus->setText("Synchronizing\xE2\x80\xA6");
    const std::string local = s8(FRoot), remote = s8(FRemoteRoot);
    const bool del = FDelete->isChecked(), subdirs = FSubdirs->isChecked();
    struct Result { QStringList lines; int done = 0, errors = 0; std::vector<std::string> failed; };
    auto result = std::make_shared<Result>();
    std::thread([this, roots, local, remote, del, subdirs, result] {
      for (const std::string & rel : roots)
      {
        const std::string l = rel.empty() ? local : local + "/" + rel;
        const std::string r = rel.empty() ? remote : (remote == "/" ? "/" + rel : remote + "/" + rel);
        const QString where = rel.empty() ? QString() : u8(rel) + "/";
        std::string err;
        // A local directory that is not on the server yet (it was there before watching started, or
        // its upload failed): there is nothing to compare with - upload it, from the highest
        // directory that is missing. Never fall back to synchronizing a parent: with "Delete
        // files" that would remove remote files in directories nothing changed in.
        if (!rel.empty())
        {
          std::string probe;
          engine::listRemoteDir(r, &probe);
          if (!probe.empty())
          {
            std::string child = rel, anc = keeplogic::parentOf(rel), ancRemote;
            bool found = false;
            for (;;)
            {
              ancRemote = anc.empty() ? remote : (remote == "/" ? "/" + anc : remote + "/" + anc);
              std::string e2; engine::listRemoteDir(ancRemote, &e2);
              if (e2.empty()) { found = true; break; }
              if (anc.empty()) break;
              child = anc; anc = keeplogic::parentOf(anc);
            }
            std::string upErr;
            if (found && engine::uploadToRemote(local + "/" + child, ancRemote, &upErr)) { result->lines << "Upload  " + u8(child) + "/"; ++result->done; }
            else { result->lines << "Error in " + where + ": " + u8(found ? upErr : probe).section('\n', 0, 0); ++result->errors; result->failed.push_back(rel); }
            continue;
          }
        }
        const std::vector<engine::SyncItem> items = engine::synchronizeCollect(l, r, 0, del, &err, subdirs);
        if (!err.empty()) { result->lines << "Error in " + (where.isEmpty() ? QString("the directory") : where) + ": " + u8(err).section('\n', 0, 0); ++result->errors; result->failed.push_back(rel); engine::synchronizeRelease(); continue; }
        if (!items.empty())
        {
          std::string applyErr;
          const bool ok = engine::synchronizeApply(&applyErr);
          for (const engine::SyncItem & it : items)
            result->lines << QString("%1  %2%3%4").arg(u8(it.action), where, u8(it.name), it.isDir ? "/" : "");
          if (ok) result->done += static_cast<int>(items.size());
          else { result->lines << "Error: " + u8(applyErr).section('\n', 0, 0); ++result->errors; result->failed.push_back(rel); }
        }
        engine::synchronizeRelease();
      }
      QMetaObject::invokeMethod(this, [this, result] {
        if (FCtx.release) FCtx.release();
        for (const QString & l : result->lines) line(l);
        FDone += result->done; FErrors += result->errors;
        FBusy = false;
        if (!result->failed.empty()) { FRetry = result->failed; FRetryTimer.start(); }
        setStatus();
        if (FStopping) stop();
      }, Qt::QueuedConnection);
    }).detach();
  }

  KeepUpToDateContext FCtx;
  QLineEdit * FLocal, * FRemote;
  QCheckBox * FDelete, * FSubdirs, * FInitial;
  QListWidget * FLog;
  QLabel * FStatus;
  QPushButton * FStart, * FStop, * FClose;
  QTimer * FTimer;
  QString FRoot, FRemoteRoot;
  keeplogic::Snapshot FSnapshot, FPending;
  std::vector<std::string> FRetry;      // directories to synchronize again after an error
  QElapsedTimer FRetryTimer;
  bool FWatching = false, FBusy = false, FStopping = false, FCloseWhenStopped = false;
  int FDone = 0, FErrors = 0;
};

} // namespace

void showKeepUpToDateDialog(QWidget * parent, const KeepUpToDateContext & context)
{
  KeepDialog d(parent, context);
  const QString dir = qEnvironmentVariable("WINSCP_DIALOG_SHOTS");
  bool okMs = false; const int ms = qEnvironmentVariable("WINSCP_KEEP_MS").toInt(&okMs);
  if (okMs && ms > 0)
  {
    QTimer::singleShot(30, &d, [&d] { applyDialogPresets(d, "keepuptodate"); });
    d.startSoonAndStopAfter(ms);
    if (!dir.isEmpty()) QTimer::singleShot(qMax(300, ms - 300), &d, [&d, dir] { d.grab().save(dir + "/keepuptodate.png"); });
    d.exec();
    for (const QString & l : d.logLines()) fprintf(stdout, "[keep] %s\n", l.toUtf8().constData());
    fflush(stdout);
    return;
  }
  if (!dir.isEmpty() || uiScripted())      // a scripted run without WINSCP_KEEP_MS: render and close
  {
    QTimer::singleShot(250, &d, [&d, dir] { if (!dir.isEmpty()) d.grab().save(dir + "/keepuptodate.png"); d.close(); });
  }
  d.exec();
}
