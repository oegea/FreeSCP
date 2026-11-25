//---------------------------------------------------------------------------
// WinSCP native port — Qt 6 GUI, faithful to WinSCP's Commander interface.
//
// Skeleton mirrors WinSCP: an iconic Login dialog (sites tree + Session form) on startup, then
// the dual-pane Commander (local | remote) with per-panel address bars, the Name/Size/Changed/
// Rights/Owner columns, and the classic bottom function-key bar (F5 Copy / F6 Move / F7 Create
// Directory / F8 Delete / ...). Both panels talk to the ported engine through enginebridge.
//---------------------------------------------------------------------------
#include <cstdlib>
#include <cstdio>
#include <QApplication>
#include <QMainWindow>
#include <QSplitter>
#include <QTableView>
#include <QStandardItemModel>
#include <QHeaderView>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QLabel>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QAction>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDrag>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QClipboard>
#include <QTreeView>
#include <QTabBar>
#include <QSplitter>
#include <QRegularExpression>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QStandardPaths>
#include <QComboBox>
#include <QTabWidget>
#include <QString>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QPlainTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include <QCloseEvent>
#include <QDockWidget>
#include <QCheckBox>
#include <QMenu>
#include <QTimer>
#include <QSettings>
#include <QPainter>
#include "updatecheck.h"
#include <QProgressDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileIconProvider>
#include <QHash>
#include <QIcon>
#include <QFont>
#include <QStyle>
#include <algorithm>
#include <map>
#include <QEventLoop>
#include <functional>
#include <thread>
#include <atomic>

#include "enginebridge.h"
#include "uicommon.h"
#include "logindialog.h"
#include "transferdialog.h"
#include "finddialog.h"
#include "diffview.h"
#include "panelcompare.h"
#include "filepanel.h"
#include "icons.h"
#include "propsdialog.h"
#include "localops.h"
#include "keepuptodate.h"

#ifndef FREESCP_VERSION
#define FREESCP_VERSION "dev"
#endif

static bool gConfirmDelete = true;     // confirm before deleting
static bool gConfirmOverwrite = true;  // confirm before overwriting (stored; enforcement TBD)
static int  gParallelMax = 2;          // max concurrent connections for queue parallelism (1-4)
static bool gAltColors = true;         // alternating row colors in panels
static bool gCompareByTime = true;     // Compare Directories criteria (WinSCP defaults: time on, size off)
static bool gCompareBySize = false;

//===========================================================================
// Internal text editor (the classic WinSCP notepad): edit + save, line/col, Find, word-wrap,
// Save & Close, and an unsaved-changes prompt on close. onSave returns true on success.
//===========================================================================
class EditorWindow : public QMainWindow
{
public:
  QPlainTextEdit * te;
  std::function<bool()> onSave;

  EditorWindow(QWidget * parent, const QString & title, const QString & text) : QMainWindow(parent), FBase(title)
  {
    setAttribute(Qt::WA_DeleteOnClose);
    te = new QPlainTextEdit; te->setPlainText(text);
    te->setLineWrapMode(QPlainTextEdit::NoWrap);
    te->setFont(QFont("Menlo", 12));
    te->setTabStopDistance(4 * te->fontMetrics().horizontalAdvance(' '));
    setCentralWidget(te);
    auto * tb = addToolBar("Edit"); tb->setMovable(false);
    auto * aSave = tb->addAction("\xF0\x9F\x92\xBE Save"); aSave->setShortcut(QKeySequence::Save);
    connect(aSave, &QAction::triggered, this, [this]{ doSave(); });
    auto * aSaveClose = tb->addAction("Save && Close");
    connect(aSaveClose, &QAction::triggered, this, [this]{ if (doSave()) close(); });
    tb->addSeparator();
    auto * aFind = tb->addAction("\xF0\x9F\x94\x8D Find"); aFind->setShortcut(QKeySequence::Find);
    connect(aFind, &QAction::triggered, this, [this]{ findText(); });
    auto * aWrap = tb->addAction("Wrap"); aWrap->setCheckable(true);
    connect(aWrap, &QAction::toggled, this, [this](bool w){ te->setLineWrapMode(w ? QPlainTextEdit::WidgetWidth : QPlainTextEdit::NoWrap); });
    statusBar();
    connect(te, &QPlainTextEdit::cursorPositionChanged, this, [this]{ updateStatus(); });
    connect(te->document(), &QTextDocument::modificationChanged, this, [this](bool){ updateTitle(); });
    updateTitle(); updateStatus();
    resize(760, 580);
  }
  bool doSave() { if (onSave && onSave()) { te->document()->setModified(false); updateTitle(); return true; } return false; }

protected:
  void closeEvent(QCloseEvent * e) override
  {
    if (te->document()->isModified())
    {
      auto r = appMessage(this, "Editor", "Save changes before closing?",
                          QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
      if (r == QMessageBox::Cancel) { e->ignore(); return; }
      if (r == QMessageBox::Save && !doSave()) { e->ignore(); return; }
    }
    e->accept();
  }

private:
  QString FBase, FLastFind;
  void updateTitle() { setWindowTitle((te->document()->isModified() ? "* " : "") + FBase); }
  void updateStatus() { auto c = te->textCursor(); statusBar()->showMessage(QString("Ln %1, Col %2").arg(c.blockNumber() + 1).arg(c.columnNumber() + 1)); }
  void findText()
  {
    bool ok; QString q = QInputDialog::getText(this, "Find", "Find:", QLineEdit::Normal, FLastFind, &ok);
    if (ok && !q.isEmpty()) { FLastFind = q; if (!te->find(q)) { te->moveCursor(QTextCursor::Start); if (!te->find(q)) statusBar()->showMessage("Not found: " + q); } }
  }
};

//===========================================================================

int main(int argc, char ** argv)
{
  engine::installCrashHandler();   // dump a backtrace to the log on a hard crash (BEFORE anything else)
  QApplication app(argc, argv);
  // QSettings scope. Headless/offscreen test runs use an ISOLATED org so they never read or wipe the
  // user's real saved sites/prefs. Every QSettings() default-ctor below inherits this org + app name.
  // WINSCP_SETTINGS_ORG names another store (e.g. to render screenshots with the real platform
  // plugin without touching the user's settings).
  QCoreApplication::setOrganizationName(!qEnvironmentVariableIsEmpty("WINSCP_SETTINGS_ORG") ? qEnvironmentVariable("WINSCP_SETTINGS_ORG")
                                        : qgetenv("QT_QPA_PLATFORM") == "offscreen" ? QString("WinSCP-native-port-test")
                                                                                    : QString("WinSCP-native-port"));
  app.setApplicationName("WinSCP");
  app.setApplicationVersion(FREESCP_VERSION);
  app.setApplicationDisplayName("FreeSCP");   // stable app-menu name (avoids LaunchServices "FreeSCP 2")
  app.setWindowIcon(QIcon(":/winscp.png"));
  { QSettings s;
    gShowHidden = s.value("prefs/showHidden", true).toBool();
    gConfirmDelete = s.value("prefs/confirmDelete", true).toBool();
    gConfirmOverwrite = s.value("prefs/confirmOverwrite", true).toBool();
    gParallelMax = s.value("prefs/parallelMax", 2).toInt();
    gAltColors = s.value("prefs/altColors", true).toBool();
    gCompareByTime = s.value("prefs/compareByTime", true).toBool();
    gCompareBySize = s.value("prefs/compareBySize", false).toBool(); }
  engine::setTransferSettings(loadTransferSettings());   // text/binary, timestamps, masks, resume, speed limit

  QMainWindow window;
  // Scripted-UI commands (WINSCP_UITEST); each feature registers the ones that exercise it.
  std::map<QString, std::function<void(const QStringList &)>> uiCmd;
  window.setWindowTitle("FreeSCP");
  window.resize(1100, 720);
  { QSettings s;
    if (s.contains("win/geometry")) window.restoreGeometry(s.value("win/geometry").toByteArray()); }

  // Main toolbar, in the order of WinSCP's Commander: session, synchronization, tools, queue,
  // transfer settings. File operations live on each panel's own toolbar (see filepanel.h).
  auto * tb = window.addToolBar("Main");
  tb->setMovable(false);
  tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  tb->setIconSize(QSize(16, 16));   // WinSCP-style small toolbar icons
  auto * actConnect = tb->addAction(appIcon("newtab"), "New Tab"); actConnect->setToolTip("Open a new session (Ctrl+N)");
  auto * actDisconnect = tb->addAction(appIcon("disconnect"), "Disconnect"); actDisconnect->setEnabled(false);
  tb->addSeparator();
  auto * tbSyncDirs = tb->addAction(appIcon("sync"), "Synchronize"); tbSyncDirs->setToolTip("Synchronize the local directory with the remote one (Ctrl+S)");
  auto * tbCompare = tb->addAction(appIcon("compare"), "Compare"); tbCompare->setToolTip("Compare directories: mark the files that differ (Shift+F2)");
  auto * actSync = tb->addAction(appIcon("syncbrowse"), "Sync browsing"); actSync->setCheckable(true);
  actSync->setToolTip("Synchronized browsing: changing directory in one panel does the same in the other");
  tb->addSeparator();
  auto * tbTerminal = tb->addAction(appIcon("terminal"), "Terminal"); tbTerminal->setToolTip("Open the server in a terminal (Ctrl+T)");
  tb->addSeparator();
  auto * tbQueue  = tb->addAction(appIcon("queue"), "Queue"); tbQueue->setCheckable(true); tbQueue->setToolTip("Transfer queue (Ctrl+Q)");
  auto * tbLog    = tb->addAction(appIcon("log"), "Log"); tbLog->setCheckable(true); tbLog->setToolTip("Session log (Ctrl+L)");
  tb->addSeparator();
  auto * tbXfer   = tb->addAction(appIcon("transfer"), "Transfer Settings"); tbXfer->setToolTip("How files are transferred: text/binary, timestamps, masks, speed limit");
  auto * tbPrefs  = tb->addAction(appIcon("settings"), "Preferences");
  auto * actTree = new QAction(appIcon("tree"), "Directory &tree", &window); actTree->setCheckable(true);
  // Keep every command reachable: size the window so the toolbar never collapses into Qt's
  // hard-to-hover overflow popup at normal sizes (WinSCP keeps its toolbar fully visible).
  window.setMinimumWidth(qMax(960, tb->sizeHint().width() + 16));

  // Panels.
  auto * splitter = new QSplitter(Qt::Horizontal);
  auto * left = new FilePanel;
  auto * right = new FilePanel;
  left->setLocal();
  right->setLocal();
  splitter->addWidget(left);
  splitter->addWidget(right);
  { QSettings s;
    if (s.contains("win/splitter")) splitter->restoreState(s.value("win/splitter").toByteArray());
    else splitter->setSizes({ 550, 550 }); }

  auto * central = new QWidget;
  auto * centralLay = new QVBoxLayout(central);
  centralLay->setContentsMargins(0, 0, 0, 0);
  centralLay->setSpacing(0);
  // Session tab bar (one tab per open connection); hidden until the first connect.
  auto * tabBar = new QTabBar;
  tabBar->setTabsClosable(true);
  tabBar->setExpanding(false);
  tabBar->setDrawBase(false);
  tabBar->setDocumentMode(true);          // left-aligns tabs (macOS centers them otherwise)

  auto * tabRow = new QWidget;
  auto * tabRowLay = new QHBoxLayout(tabRow);
  tabRowLay->setContentsMargins(0, 0, 0, 0); tabRowLay->setSpacing(0);
  auto * newTabBtn = new QToolButton; newTabBtn->setText("New Tab"); newTabBtn->setIcon(appIcon("newtab"));
  newTabBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); newTabBtn->setIconSize(QSize(16, 16));
  newTabBtn->setToolTip("New session (Ctrl+N)"); newTabBtn->setFocusPolicy(Qt::NoFocus);
  newTabBtn->setAutoRaise(true);
  tabRow->setStyleSheet(kFlatButtons);
  tabRowLay->addWidget(tabBar); tabRowLay->addWidget(newTabBtn); tabRowLay->addStretch(1);   // tabs + "+" to the left
  tabRow->hide();
  centralLay->addWidget(tabRow);
  centralLay->addWidget(splitter, 1);

  // Bottom function-key bar (the iconic WinSCP/Norton-Commander row).
  auto * fkeys = new QWidget;
  auto * fkLay = new QHBoxLayout(fkeys);
  fkLay->setContentsMargins(2, 1, 2, 1);
  fkLay->setSpacing(0);
  fkeys->setObjectName("fkeys");
  fkeys->setStyleSheet(QString("QWidget#fkeys { border-top:1px solid palette(mid); } ") + kFlatButtons);
  central->setLayout(centralLay);
  centralLay->addWidget(fkeys);
  window.setCentralWidget(central);

  FilePanel * active = left;

  // Main status bar, like WinSCP's: messages on the left; on the right a lock when the session is
  // encrypted, the protocol and the time the session has been open.
  auto * sbLock = new QLabel; auto * sbProto = new QLabel; auto * sbTime = new QLabel;
  sbLock->setFixedSize(18, 16); sbProto->setMinimumWidth(52); sbTime->setMinimumWidth(52);
  sbProto->setAlignment(Qt::AlignCenter); sbTime->setAlignment(Qt::AlignCenter);
  for (QLabel * l : { sbLock, sbProto, sbTime }) window.statusBar()->addPermanentWidget(l);
  static QString sessProtocol; static bool sessEncrypted = false; static qint64 sessLoginWall = 0;
  auto wallNow = [] { const QDateTime n = QDateTime::currentDateTime(); return n.toSecsSinceEpoch() + n.offsetFromUtc(); };
  auto showSessionStatus = [&] {
    const bool on = !sessProtocol.isEmpty();
    sbLock->setPixmap(on ? appIcon(sessEncrypted ? "lock" : "unlock").pixmap(16, 16) : QPixmap());
    sbLock->setToolTip(!on ? QString() : sessEncrypted ? "The connection is encrypted" : "The connection is NOT encrypted");
    sbProto->setText(sessProtocol);
    sbTime->setText(on ? u8(panellogic::elapsedText(wallNow() - sessLoginWall)) : QString());
  };
  // Ask the engine about the active session (not while a transfer holds it).
  auto readSessionStatus = [&] {
    if (gTransferRunning.load()) return;
    if (!engine::remoteConnected()) { sessProtocol.clear(); sessEncrypted = false; sessLoginWall = 0; }
    else
    {
      const engine::SessionInformation in = engine::sessionInformation();
      sessProtocol = u8(in.protocol); sessEncrypted = in.encrypted;
      sessLoginWall = panellogic::wallSecondsSince1970(in.loginSecs);
    }
    showSessionStatus();
  };
  auto * sessTimer = new QTimer(&window);
  QObject::connect(sessTimer, &QTimer::timeout, [&] { if (!sessProtocol.isEmpty()) showSessionStatus(); });
  sessTimer->start(1000);

  auto updateStatuses = [&] { left->updateStatus(); right->updateStatus(); };
  // What each panel's transfer button does, and whether links can be created there.
  auto refreshPanelRoles = [&] {
    for (FilePanel * p : { left, right })
    {
      FilePanel * other = (p == left) ? right : left;
      if (!p->isRemote()) p->setTransferCaption(other->isRemote() ? "Upload" : "Copy", other->isRemote() ? "upload" : "copy");
      p->setLinkEnabled(!p->isRemote() || (!gTransferRunning.load() && engine::remoteCapable(engine::Capability::SymbolicLink)));
    }
  };
  left->onStatusChanged = updateStatuses;
  right->onStatusChanged = updateStatuses;
  left->onActivated = [&] { active = left; left->setActive(true); right->setActive(false); updateStatuses(); };
  right->onActivated = [&] { active = right; right->setActive(true); left->setActive(false); updateStatuses(); };
  static QString remoteHome = "/";
  static QString connHost, connUser; static int connPort = 22; static bool connSsh = false;  // for Open Terminal
  left->onHome  = [&] { left->navigate(u8(engine::homeDir())); };
  right->onHome = [&] { right->navigate(right->isRemote() ? remoteHome : u8(engine::homeDir())); };
  // Synchronized browsing: entering/leaving a dir in one panel mirrors to the other.
  left->onEnterDir  = [&](const QString & n) { if (actSync->isChecked()) right->enterSubdir(n); };
  left->onLeaveDir  = [&] { if (actSync->isChecked()) right->upOne(); };
  right->onEnterDir = [&](const QString & n) { if (actSync->isChecked()) left->enterSubdir(n); };
  right->onLeaveDir = [&] { if (actSync->isChecked()) left->upOne(); };
  QObject::connect(actTree, &QAction::toggled, [&](bool v){ left->setTreeVisible(v); right->setTreeVisible(v); });
  left->onSwitchPanel  = [&] { right->view()->setFocus(); };
  right->onSwitchPanel = [&] { left->view()->setFocus(); };

  // Session log dock (see what the engine does — invaluable for testing).
  auto * logDock = new QDockWidget("Session log", &window);
  auto * logView = new QPlainTextEdit; logView->setReadOnly(true);
  logView->setMaximumBlockCount(2000);
  logDock->setWidget(logView);
  window.addDockWidget(Qt::BottomDockWidgetArea, logDock);
  logDock->hide();
  auto log = [&](const QString & s) { logView->appendPlainText(s); engine::logLine("[ui] " + s8(s)); };

  // Transfer queue dock (WinSCP-style): a list of transfers with live progress. Processed
  // sequentially on the UI thread (processEvents keeps it responsive) — visible like WinSCP's queue.
  auto * queueDock = new QDockWidget("Transfer queue", &window);
  auto * queueWidget = new QWidget;
  auto * queueLay = new QVBoxLayout(queueWidget);
  queueLay->setContentsMargins(0, 0, 0, 0); queueLay->setSpacing(2);
  auto * queueBtns = new QHBoxLayout; queueBtns->setContentsMargins(3, 2, 3, 0);
  auto * btnCancel = new QPushButton("Cancel"); btnCancel->setEnabled(false);
  auto * btnClear = new QPushButton("Clear finished");
  queueBtns->addWidget(btnCancel); queueBtns->addWidget(btnClear); queueBtns->addStretch(1);
  auto * queueView = new QTableView;
  auto * queueModel = new QStandardItemModel(0, 7, &window);
  queueModel->setHorizontalHeaderLabels({ "Operation", "File", "Size", "Progress", "Speed", "Time left", "Status" });
  queueView->setModel(queueModel);
  queueView->verticalHeader()->setVisible(false);
  queueView->verticalHeader()->setDefaultSectionSize(20);   // compact rows (match the panels)
  queueView->setShowGrid(false);
  queueView->setAlternatingRowColors(true);
  queueView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  queueView->horizontalHeader()->setStretchLastSection(true);
  queueView->setColumnWidth(1, 200);
  queueLay->addLayout(queueBtns); queueLay->addWidget(queueView);
  queueDock->setWidget(queueWidget);
  window.addDockWidget(Qt::BottomDockWidgetArea, queueDock);
  queueDock->hide();

  auto fmtSpeed = [](qint64 cps) -> QString { return cps > 0 ? u8(engine::formatSize(cps)) + "/s" : QString(); };
  auto fmtEta = [](qint64 secs) -> QString {
    if (secs <= 0) return QString();
    return QString::asprintf("%lld:%02lld", (long long)(secs / 60), (long long)(secs % 60));
  };
  auto queueAdd = [&](const QString & op, const QString & file, qint64 size) -> int {
    QList<QStandardItem *> row;
    row << new QStandardItem(op) << new QStandardItem(file)
        << new QStandardItem(size > 0 ? u8(engine::formatSize(size)) : QString())
        << new QStandardItem("0%") << new QStandardItem() << new QStandardItem() << new QStandardItem("queued");
    queueModel->appendRow(row);
    return queueModel->rowCount() - 1;
  };
  auto queueCell = [&](int r, int c, const QString & v) { if (r >= 0 && r < queueModel->rowCount()) queueModel->item(r, c)->setText(v); };
  auto queueStatus = [&](int r, const QString & s) { queueCell(r, 6, s); };

  // Single-file upload that shows in the transfer queue (used by editor save + edit-and-watch).
  // Runs on a worker so the UI stays responsive; refreshes the remote panel on success.
  auto enqueueUpload = [&](const QString & localPath, const QString & remoteDir, const QString & opName) {
    queueDock->show();
    int row = queueAdd(opName, QFileInfo(localPath).fileName(), QFileInfo(localPath).size());
    queueStatus(row, "active");
    std::string lp = s8(localPath), rd = s8(remoteDir);
    std::thread([&, row, lp, rd]{
      std::string err; bool ok = engine::uploadToRemote(lp, rd, &err, true);   // editor save: this exact file, as is
      QMetaObject::invokeMethod(&window, [&, row, ok, err]{
        queueStatus(row, ok ? "done" : ("failed: " + u8(err)));
        if (ok) { queueCell(row, 3, "100%"); window.statusBar()->showMessage("Uploaded " + queueModel->item(row, 1)->text()); if (right->isRemote()) right->refresh(); }
        else window.statusBar()->showMessage("Upload failed: " + u8(err));
      }, Qt::QueuedConnection);
    }).detach();
  };

  // Cancel flag for the currently-running batch (Cancel button -> sink returns true -> engine aborts).
  auto currentCancel = std::make_shared<std::shared_ptr<std::atomic<bool>>>();
  QObject::connect(btnCancel, &QPushButton::clicked, [&, currentCancel]{
    if (*currentCancel) (*currentCancel)->store(true);
    window.statusBar()->showMessage("Cancelling\xE2\x80\xA6");
  });
  QObject::connect(btnClear, &QPushButton::clicked, [&]{
    for (int r = queueModel->rowCount() - 1; r >= 0; --r) {
      QString st = queueModel->item(r, 6)->text();
      if (st == "done" || st.startsWith("failed") || st.startsWith("cancelled")) queueModel->removeRow(r);
    }
  });

  // A lost connection (dropped by the server or the network, a laptop that slept): offer to open
  // the session again in the same tab, back in the directory the panel was showing.
  static bool reconnecting = false;
  auto offerReconnect = [&]() -> bool {
    if (reconnecting || gTransferRunning.load() || !engine::sessionLost()) return false;
    reconnecting = true;
    bool ok = false;
    log("The connection to the server was lost.");
    if (appMessage(&window, "Connection lost", "The connection to the server was lost.\n\nReconnect?",
                   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes)
    {
      window.statusBar()->showMessage("Reconnecting\xE2\x80\xA6");
      QApplication::processEvents();
      const QString where = right->path();
      const engine::ConnectResult cr = engine::reconnect();
      if (cr.ok)
      {
        log("Reconnected. Remote directory: " + u8(cr.currentDir));
        readSessionStatus(); refreshPanelRoles();
        right->navigate(where.isEmpty() ? u8(cr.currentDir) : where);
        window.statusBar()->showMessage("Reconnected");
        ok = true;
      }
      else
      {
        log("Reconnect FAILED: " + u8(cr.error));
        window.statusBar()->showMessage("Reconnect failed");
        appMessage(&window, "Reconnect", u8(cr.error));
      }
    }
    reconnecting = false;
    return ok;
  };

  auto busy = [&]() -> bool {
    if (gTransferRunning.load()) { window.statusBar()->showMessage("Transfer in progress\xE2\x80\xA6 please wait"); return true; }
    return false;
  };

  // Background transfer: runs the batch on a worker thread (UI never freezes); the queue dock updates
  // live with %/speed/time-left (marshaled to the UI thread). Cancel aborts via the engine progress
  // sink. Single-connection/non-thread-safe engine -> serialized by the bridge mutex + gTransferRunning
  // gating remote UI actions for the batch (local browsing stays responsive).
  // Overwrite confirmation: returns the names to transfer (all, or non-existing on Skip) or empty on
  // Cancel. Honors the Preferences "Confirm overwrites" toggle.
  auto confirmOverwrite = [&](FilePanel * dest, QStringList files) -> QStringList {
    if (!gConfirmOverwrite) return files;
    QStringList existing; for (const QString & f : files) if (dest->contains(f)) existing << f;
    if (existing.isEmpty()) return files;
    QMessageBox box(&window); box.setIcon(QMessageBox::Question); box.setWindowTitle("Confirm overwrite");
    box.setText(QString("%1 of %2 item(s) already exist in %3.").arg(existing.size()).arg(files.size()).arg(dest->path()));
    if (existing.size() <= 10) box.setInformativeText(existing.join("\n"));
    auto * over = box.addButton("Overwrite", QMessageBox::AcceptRole);
    auto * skip = box.addButton("Skip existing", QMessageBox::ActionRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    if (box.clickedButton() == over) return files;
    if (box.clickedButton() == skip) { QStringList keep; for (const QString & f : files) if (!dest->contains(f)) keep << f; return keep; }
    return QStringList();
  };

  auto startTransfer = [&](FilePanel * from, FilePanel * to, QStringList files, bool isMove) {
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (from->isRemote() && to->isRemote())
    { appMessage(&window, "Transfer", "Remote-to-remote is not supported yet."); return; }
    if (busy()) return;
    files = confirmOverwrite(to, files);
    if (files.isEmpty()) { window.statusBar()->showMessage("Transfer cancelled"); return; }
    const bool srcRemote = from->isRemote(), dstRemote = to->isRemote();
    const std::string srcDir = s8(from->path()), dstDir = s8(to->path());
    const QString op = srcRemote ? "Download" : (dstRemote ? "Upload" : "Copy");
    queueDock->show();
    std::vector<int> rows; std::vector<std::string> names;
    for (const QString & f : files) { rows.push_back(queueAdd(isMove ? "Move" : op, f, from->sizeOf(f))); names.push_back(s8(f)); }
    gTransferRunning = true;
    btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    *currentCancel = cancel;

    // Parallelism: a plain remote copy of >1 files uses up to 2 extra connections (each its own
    // worker). Moves, local copies and single files stay serial. Falls back to serial if the pool
    // can't open. Per-worker progress -> that worker's current queue row.
    const bool wantParallel = (srcRemote != dstRemote) && !isMove && names.size() > 1 && engine::parallelSupported();
    std::vector<int> handles;
    if (wantParallel)
    {
      int want = (int)std::min<size_t>((size_t)std::max(1, gParallelMax), names.size());
      for (int k = 0; k < want; ++k) { int h = engine::openParallelConnection(nullptr); if (h > 0) handles.push_back(h); }
      if (handles.size() < 2) { engine::closeParallelConnections(); handles.clear(); }  // need >=2 to be worth it
    }
    const int nWorkers = handles.empty() ? 1 : (int)handles.size();

    auto markActive = [&, rows](int rowVal) { QMetaObject::invokeMethod(&window, [&, rowVal]{ queueStatus(rowVal, "active"); }, Qt::QueuedConnection); };
    auto markDone = [&](int rowVal, const QString & status, bool okRow) {
      QMetaObject::invokeMethod(&window, [&, rowVal, status, okRow]{
        queueStatus(rowVal, status); if (okRow) { queueCell(rowVal, 3, "100%"); queueCell(rowVal, 4, QString()); queueCell(rowVal, 5, QString()); }
      }, Qt::QueuedConnection);
    };
    auto progressFor = [&, cancel](int rowVal, const engine::TransferProgress & tp) -> bool {
      int pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
      qint64 cps = tp.cps; qint64 eta = (cps > 0 && tp.total > tp.transferred) ? (tp.total - tp.transferred) / cps : 0;
      QMetaObject::invokeMethod(&window, [&, rowVal, pct, cps, eta]{
        queueCell(rowVal, 3, QString("%1%").arg(pct)); queueCell(rowVal, 4, fmtSpeed(cps)); queueCell(rowVal, 5, fmtEta(eta));
      }, Qt::QueuedConnection);
      return cancel->load();
    };

    auto nextIdx = std::make_shared<std::atomic<size_t>>(0);
    auto okCount = std::make_shared<std::atomic<int>>(0);

    // Coordinator thread: spawn workers, join, then marshal completion (keeps the UI thread free).
    std::thread([&, rows, names, srcDir, dstDir, srcRemote, dstRemote, isMove, from, to, cancel, handles, nWorkers, nextIdx, okCount, markActive, markDone, progressFor]{
      auto worker = [&](int handle) {   // handle 0 = primary (serial), >=1 = pool connection
        auto curRow = std::make_shared<std::atomic<int>>(-1);
        if (handle == 0) engine::setProgressSink([&, curRow](const engine::TransferProgress & tp){ return progressFor(curRow->load(), tp); });
        else engine::setProgressSinkVia(handle, [&, curRow](const engine::TransferProgress & tp){ return progressFor(curRow->load(), tp); });
        for (;;)
        {
          size_t i = nextIdx->fetch_add(1);
          if (i >= names.size()) break;
          int rowVal = rows[i];
          if (cancel->load()) { markDone(rowVal, "cancelled", false); continue; }
          curRow->store(rowVal); markActive(rowVal);
          std::string src = engine::joinPath(srcDir, names[i]);
          std::string err; bool r;
          if (!srcRemote && !dstRemote) r = engine::copyFile(src, engine::joinPath(dstDir, names[i]));
          else if (handle != 0) r = srcRemote ? engine::downloadVia(handle, src, dstDir, &err) : engine::uploadVia(handle, src, dstDir, &err);
          else r = srcRemote ? engine::downloadFromRemote(src, dstDir, &err) : engine::uploadToRemote(src, dstDir, &err);
          if (r && isMove) r = srcRemote ? engine::remoteDelete(names[i], &err) : engine::localDelete(srcDir, names[i], &err);
          bool cancelled = cancel->load();
          markDone(rowVal, cancelled ? "cancelled" : (r ? "done" : ("failed" + (err.empty() ? QString() : (": " + u8(err))))), r && !cancelled);
          if (r && !cancelled) okCount->fetch_add(1);
        }
      };
      std::vector<std::thread> ts;
      if (handles.empty()) worker(0);
      else { for (int h : handles) ts.emplace_back([&, h]{ worker(h); }); for (auto & t : ts) t.join(); }

      int ok = okCount->load(), total = (int)names.size();
      QMetaObject::invokeMethod(&window, [&, ok, total, isMove, from, to]{
        engine::setProgressSink(nullptr);
        engine::closeParallelConnections();
        gTransferRunning = false;
        btnCancel->setEnabled(false);
        to->refresh(); if (isMove) from->refresh();
        window.statusBar()->showMessage(QString("Transferred %1/%2 item(s)").arg(ok).arg(total));
        if (ok < total && engine::sessionLost()) offerReconnect();     // the reason they failed
        else if (ok < total) appMessage(&window, "Transfer",
          QString("%1 of %2 item(s) failed. See the Transfer queue for the error.").arg(total - ok).arg(total));
      }, Qt::QueuedConnection);
    }).detach();
  };

  // Drag files from one panel, drop on the other -> transfer (copy).
  left->onDropFiles  = [&](FilePanel * src, const QStringList & f) { startTransfer(src, left, f, false); };
  right->onDropFiles = [&](FilePanel * src, const QStringList & f) { startTransfer(src, right, f, false); };

  // External drop (Finder/Nautilus) of local files -> upload (remote panel) or copy (local panel).
  auto importFiles = [&](FilePanel * dest, const QStringList & absPathsIn) {
    if (busy()) return;
    const bool toRemote = dest->isRemote();
    const std::string destDir = s8(dest->path());
    // overwrite confirmation by basename
    QStringList baseNames; for (const QString & p : absPathsIn) baseNames << QFileInfo(p).fileName();
    QStringList keepNames = confirmOverwrite(dest, baseNames);
    if (keepNames.isEmpty()) { window.statusBar()->showMessage("Import cancelled"); return; }
    QStringList absPaths; for (const QString & p : absPathsIn) if (keepNames.contains(QFileInfo(p).fileName())) absPaths << p;
    queueDock->show();
    std::vector<int> rows; std::vector<std::string> srcs, nm;
    for (const QString & p : absPaths) {
      QFileInfo fi(p);                                    // files AND folders (engine recurses dirs)
      rows.push_back(queueAdd(toRemote ? "Upload" : "Copy", fi.fileName(), fi.isDir() ? 0 : fi.size()));
      srcs.push_back(s8(p)); nm.push_back(s8(fi.fileName()));
    }
    if (rows.empty()) return;
    gTransferRunning = true; btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false); *currentCancel = cancel;
    auto curRow = std::make_shared<std::atomic<int>>(rows[0]);
    engine::setProgressSink([&, cancel, curRow](const engine::TransferProgress & tp) -> bool {
      int rowVal = curRow->load(), pct = tp.total > 0 ? (int)(tp.transferred * 100 / tp.total) : 0;
      qint64 cps = tp.cps, eta = (cps > 0 && tp.total > tp.transferred) ? (tp.total - tp.transferred) / cps : 0;
      QMetaObject::invokeMethod(&window, [&, rowVal, pct, cps, eta]{ queueCell(rowVal, 3, QString("%1%").arg(pct)); queueCell(rowVal, 4, fmtSpeed(cps)); queueCell(rowVal, 5, fmtEta(eta)); }, Qt::QueuedConnection);
      return cancel->load();
    });
    std::thread([&, rows, srcs, nm, destDir, toRemote, dest, cancel, curRow]{
      int ok = 0;
      for (size_t i = 0; i < srcs.size(); ++i) {
        if (cancel->load()) { QMetaObject::invokeMethod(&window, [&, r = rows[i]]{ queueStatus(r, "cancelled"); }, Qt::QueuedConnection); continue; }
        int rowVal = rows[i]; curRow->store(rowVal);
        QMetaObject::invokeMethod(&window, [&, rowVal]{ queueStatus(rowVal, "active"); }, Qt::QueuedConnection);
        std::string err; bool r = toRemote ? engine::uploadToRemote(srcs[i], destDir, &err)
                                            : engine::copyFile(srcs[i], engine::joinPath(destDir, nm[i]));
        QString status = r ? "done" : ("failed" + (err.empty() ? QString() : (": " + u8(err))));
        QMetaObject::invokeMethod(&window, [&, rowVal, status, r]{ queueStatus(rowVal, status); if (r) queueCell(rowVal, 3, "100%"); }, Qt::QueuedConnection);
        if (r) ++ok;
      }
      int total = (int)srcs.size();
      QMetaObject::invokeMethod(&window, [&, ok, total, dest]{
        engine::setProgressSink(nullptr); gTransferRunning = false; btnCancel->setEnabled(false);
        dest->refresh(); window.statusBar()->showMessage(QString("Imported %1/%2 item(s)").arg(ok).arg(total));
        if (ok < total && engine::sessionLost()) offerReconnect();
        else if (ok < total) appMessage(&window, "Import",
          QString("%1 of %2 item(s) failed. See the Transfer queue for the error.").arg(total - ok).arg(total));
      }, Qt::QueuedConnection);
    }).detach();
  };
  left->onDropExternal = importFiles; right->onDropExternal = importFiles;

  // A remote directory that cannot be opened is an error the user sees (WinSCP does the same),
  // not an empty panel.
  auto listError = [&](const QString & msg) {
    window.statusBar()->showMessage("Error listing directory");
    log("Error listing directory: " + msg);
    // not from inside the panel's own navigation: it would overwrite the listing we come back with
    if (engine::sessionLost() && !reconnecting) { QTimer::singleShot(0, &window, [&]{ offerReconnect(); }); return; }
    appMessage(&window, "Error", msg);
  };
  left->onListError = listError; right->onListError = listError;

  // Edit-and-watch: a remote file opened in an (external) editor is downloaded to temp and watched;
  // saving re-uploads it automatically — the classic WinSCP "edit remote in your editor" magic.
  auto * watcher = new QFileSystemWatcher(&window);
  auto * watchRemoteDir = new QHash<QString, QString>;   // localTempPath -> remote dir
  auto reupload = [&, watcher, watchRemoteDir](const QString & localPath) {
    auto it = watchRemoteDir->find(localPath);
    if (it == watchRemoteDir->end()) return;
    enqueueUpload(localPath, it.value(), "Edit-upload");   // shows in the transfer queue
    if (QFileInfo::exists(localPath) && !watcher->files().contains(localPath)) watcher->addPath(localPath);  // editors that save-by-rename drop the watch
  };
  QObject::connect(watcher, &QFileSystemWatcher::fileChanged, &window, [reupload](const QString & p) { reupload(p); });

  // Host-key verification: show the engine's message (fingerprint) + Yes/No instead of auto-accepting.
  engine::setConfirmCallback([&](const std::string & msg) -> bool {
    if (qgetenv("QT_QPA_PLATFORM") == "offscreen") return true;   // headless tests: auto-accept
    return appMessage(&window, "Verify server identity", u8(msg),   // SSH host key or TLS certificate
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
  });

  // Authentication prompts the stored credentials could not answer: a missing or rejected password,
  // a key passphrase, keyboard-interactive / two-factor challenges, proxy credentials.
  engine::setPromptCallback([&](const engine::PromptRequest & rq, std::vector<std::string> & answers) -> bool {
    return showAuthPrompt(&window, rq, answers);
  });

  //--- session tabs -------------------------------------------------------
  static int curTab = -1;        // previously-active tab (for save-on-switch)
  static bool tabSwitching = false;
  // After a successful connect: add a tab for the new (already-active) bridge session.
  auto addSessionTab = [&](const QString & label) {
    tabSwitching = true;
    int i = tabBar->addTab(label);
    tabBar->setTabData(i, QStringList{ right->path(), left->path() });   // [remote, local]
    tabBar->setCurrentIndex(i);
    curTab = i;
    tabRow->setVisible(true);
    tabSwitching = false;
  };
  // Switch panels to reflect tab i (assumes engine session i is/should be active).
  auto activateTab = [&](int i) {
    engine::switchSession(i);
    right->setRemote(tabBar->tabText(i));
    QStringList st = tabBar->tabData(i).toStringList();
    if (st.size() >= 1 && !st[0].isEmpty()) right->navigate(st[0]);
    if (st.size() >= 2 && !st[1].isEmpty()) left->navigate(st[1]);
    curTab = i;
    readSessionStatus(); refreshPanelRoles();
  };
  QObject::connect(tabBar, &QTabBar::currentChanged, [&](int i) {
    if (tabSwitching || i < 0) return;
    if (curTab >= 0 && curTab < tabBar->count())                         // save outgoing tab's paths
      tabBar->setTabData(curTab, QStringList{ right->path(), left->path() });
    tabSwitching = true; activateTab(i); tabSwitching = false;
  });
  auto closeTab = [&](int i) {
    if (busy() || i < 0 || i >= tabBar->count()) return;
    tabSwitching = true;
    engine::closeSession(i);
    tabBar->removeTab(i);
    if (tabBar->count() == 0) {
      tabRow->hide(); curTab = -1;
      right->setLocal(); right->navigate(u8(engine::homeDir()));
      actDisconnect->setEnabled(false); window.setWindowTitle("FreeSCP");
      readSessionStatus(); refreshPanelRoles();
    } else {
      int ni = qMin(i, tabBar->count() - 1);
      tabBar->setCurrentIndex(ni); activateTab(ni);
    }
    tabSwitching = false;
  };
  QObject::connect(tabBar, &QTabBar::tabCloseRequested, [&](int i) { closeTab(i); });

  //--- operations ---------------------------------------------------------
  // After a successful connect (interactive or scripted): show the session in the remote panel.
  static engine::ConnectOptions connOpts;   // the active session's options (Open Terminal, copy URL)
  auto sessionOpened = [&](const engine::ConnectOptions & o, const engine::ConnectResult & r) {
    right->setRemote(QString("%1@%2").arg(u8(o.user), u8(o.host)));
    remoteHome = u8(r.currentDir);
    connOpts = o;
    connHost = u8(o.host); connUser = u8(o.user); connPort = o.port;
    connSsh = (o.protocol == engine::Protocol::Sftp || o.protocol == engine::Protocol::Scp);
    right->navigate(u8(r.currentDir));
    right->onActivated();
    actDisconnect->setEnabled(true);
    window.setWindowTitle(QString("%1@%2 \xE2\x80\x94 FreeSCP").arg(u8(o.user), u8(o.host)));
    QString route = routeSummary(o);
    log("Connected" + (route.isEmpty() ? QString() : " (" + route + ")") + ". Remote directory: " + u8(r.currentDir));
    addSessionTab(QString("%1@%2").arg(u8(o.user), u8(o.host)));   // open a tab for this session
    readSessionStatus(); refreshPanelRoles();
  };
  auto connectWith = [&](const engine::ConnectOptions & o) -> bool {
    window.statusBar()->showMessage("Connecting\xE2\x80\xA6");
    QString route = routeSummary(o);
    log(QString("Connecting to %1:%2%3 \xE2\x80\xA6").arg(u8(o.host)).arg(o.port).arg(route.isEmpty() ? QString() : " via " + route));
    QApplication::processEvents();
    // Remember where the current tab is before another session takes over the panels (it used to
    // come back at the directory it was opened in, not the one the user had navigated to).
    if (curTab >= 0 && curTab < tabBar->count() && right->isRemote())
      tabBar->setTabData(curTab, QStringList{ right->path(), left->path() });
    engine::ConnectResult r = engine::connect(o);
    if (!r.ok)
    {
      log("FAILED: " + u8(r.error));
      logDock->show();
      appMessage(&window, "Login", u8(r.error));
      window.statusBar()->showMessage("Connection failed");
      return false;
    }
    sessionOpened(o, r);
    return true;
  };
  auto doConnect = [&] {
    if (busy()) return;
    LoginParams lp = showLoginDialog(&window);
    if (!lp.ok) return;
    if (lp.o.host.empty()) { appMessage(&window, "Login", "Please enter a host name."); return; }
    connectWith(lp.o);
  };
  auto doDisconnect = [&] {
    if (busy()) return;
    if (tabBar->count() > 0) { closeTab(tabBar->currentIndex()); window.statusBar()->showMessage("Disconnected"); log("Disconnected."); return; }
    engine::disconnectSftp();
    right->setLocal();
    right->navigate(u8(engine::homeDir()));
    actDisconnect->setEnabled(false);
    window.setWindowTitle("FreeSCP");
    window.statusBar()->showMessage("Disconnected");
    log("Disconnected.");
    readSessionStatus(); refreshPanelRoles();
  };

  // Open a system terminal with an ssh command to the current server (the "open in PuTTY" of Windows).
  auto doOpenTerminal = [&] {
    if (!engine::remoteConnected() || !connSsh)
    { window.statusBar()->showMessage("Open Terminal needs an active SFTP/SCP session"); return; }
    // A tunnelled session: the target is only reachable through the jump host (ssh -J).
    QString jump = (connOpts.tunnel && !connOpts.tunnelHost.empty())
      ? QString("-J %1%2:%3 ").arg(connOpts.tunnelUser.empty() ? QString() : u8(connOpts.tunnelUser) + "@", u8(connOpts.tunnelHost)).arg(connOpts.tunnelPort)
      : QString();
    QString sshCmd = QString("ssh %1%2@%3 -p %4").arg(jump, connUser, connHost).arg(connPort);
#ifdef Q_OS_MACOS
    QString script = QString("tell application \"Terminal\"\nactivate\ndo script \"%1\"\nend tell").arg(sshCmd);
    QProcess::startDetached("osascript", { "-e", script });
#else
    // Linux: try the common terminal emulators with an ssh command.
    for (const QString & term : { QString("x-terminal-emulator"), QString("gnome-terminal"), QString("konsole"), QString("xterm") })
      if (QStandardPaths::findExecutable(term).size())
      { QProcess::startDetached(term, term == "gnome-terminal" ? QStringList{ "--", "bash", "-c", sshCmd + "; exec bash" } : QStringList{ "-e", sshCmd }); break; }
#endif
    window.statusBar()->showMessage("Opened terminal: " + sshCmd);
  };

  // Transfer settings (WinSCP's Transfer Settings dialog): they apply to every transfer and
  // synchronization from now on and are remembered.
  auto applyTransferSettings = [&](const engine::TransferSettings & x) {
    engine::setTransferSettings(x);
    saveTransferSettings(x);
    window.statusBar()->showMessage("Transfer settings: " + transferSummary(x));
  };
  auto editTransferSettings = [&](QWidget * parent) -> bool {
    engine::TransferSettings x = engine::transferSettings();
    if (!showTransferSettingsDialog(parent, x)) return false;
    applyTransferSettings(x);
    return true;
  };

  // WinSCP-style Copy/Move dialog: shows the count + an editable target directory. Returns the target
  // (empty = cancelled). If the user changes the target, the dest panel is navigated there first so
  // the transfer + the post-transfer refresh both target it.
  auto showCopyDialog = [&](const QString & op, int count, const QString & defTarget) -> QString {
    QDialog d(&window); d.setWindowTitle(op);
    auto * lay = new QVBoxLayout(&d);
    lay->addWidget(new QLabel(QString("%1 %2 item(s) to:").arg(op).arg(count)));
    auto * edit = new QLineEdit(defTarget); edit->setMinimumWidth(380); edit->selectAll();
    lay->addWidget(edit);
    // How the files will be transferred, and a way to change it right here (as in WinSCP).
    auto * xferRow = new QHBoxLayout;
    auto * xferLabel = new QLabel(transferSummary(engine::transferSettings()));
    xferLabel->setStyleSheet("color: gray;"); xferLabel->setWordWrap(true);
    auto * xferBtn = new QPushButton("Transfer settings\xE2\x80\xA6");
    xferRow->addWidget(xferLabel, 1); xferRow->addWidget(xferBtn);
    lay->addLayout(xferRow);
    QObject::connect(xferBtn, &QPushButton::clicked, &d, [&] {
      if (editTransferSettings(&d)) xferLabel->setText(transferSummary(engine::transferSettings()));
    });
    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(op);
    lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    edit->setFocus();
    if (!qEnvironmentVariableIsEmpty("WINSCP_COPY_TARGET")) edit->setText(qEnvironmentVariable("WINSCP_COPY_TARGET"));   // scripted runs
    return (execDialog(d, "copy") == QDialog::Accepted) ? edit->text() : QString();
  };
  auto doCopy = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedItems();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (active->isRemote() && dst->isRemote())
    { appMessage(&window, "Copy", "Remote-to-remote copy is not supported yet."); return; }
    if (busy()) return;
    QString target = showCopyDialog("Copy", files.size(), dst->path());
    if (target.isEmpty()) return;
    if (target != dst->path())
    {
      dst->navigate(target);
      // the typed directory could not be opened (the error was shown): do NOT transfer into
      // whatever directory the panel fell back to
      if (dst->isRemote() && dst->path() != target) { window.statusBar()->showMessage("Copy cancelled: cannot open " + target); return; }
    }
    startTransfer(active, dst, files, false);
  };
  auto doMove = [&] {
    FilePanel * dst = (active == left) ? right : left;
    QStringList files = active->selectedItems();
    if (files.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (active->isRemote() && dst->isRemote())
    { appMessage(&window, "Move", "Remote-to-remote move is not supported yet."); return; }
    if (busy()) return;
    QString target = showCopyDialog("Move", files.size(), dst->path());
    if (target.isEmpty()) return;
    if (target != dst->path())
    {
      dst->navigate(target);
      // the typed directory could not be opened (the error was shown): do NOT transfer into
      // whatever directory the panel fell back to
      if (dst->isRemote() && dst->path() != target) { window.statusBar()->showMessage("Move cancelled: cannot open " + target); return; }
    }
    startTransfer(active, dst, files, true);
  };
  auto doMkdir = [&] {
    if (busy()) return;
    bool okIn = false;
    QString name = QInputDialog::getText(&window, "Create folder", "New folder name:", QLineEdit::Normal, "", &okIn);
    if (!okIn || name.isEmpty()) return;
    std::string err;
    bool ok = active->isRemote() ? engine::remoteMakeDir(s8(name), &err)
                                 : engine::localMakeDir(s8(active->path()), s8(name), &err);
    active->refresh();
    if (ok) { window.statusBar()->showMessage("Created " + name); return; }
    // WinSCP surfaces a failed folder creation as an error dialog (e.g. permission denied),
    // not a silent status-bar line.
    QString detail = u8(err).trimmed();
    window.statusBar()->showMessage("Create folder failed");
    appMessage(&window, "Create folder",
               QString("Error creating folder \"%1\".\n\n%2")
                   .arg(name, detail.isEmpty() ? QStringLiteral("The operation failed.") : detail),
               QMessageBox::Ok);
  };

  // Select files by mask (Ctrl+Num+) / unselect (Ctrl+Num-), as WinSCP's Select dialog.
  auto doSelectMask = [&](bool sel) {
    static QString lastMask = "*"; static bool applyToDirs = false;
    QString mask = lastMask; bool dirs = applyToDirs;
    if (!showSelectDialog(&window, sel, mask, dirs)) return;
    lastMask = mask; applyToDirs = dirs;
    if (!active->selectByMask(mask, sel, dirs)) appMessage(&window, sel ? "Select files" : "Unselect files", "The file mask is not valid.");
  };

  // Show only the files matching a mask in the active panel (empty mask = everything).
  auto doFilter = [&] {
    QString mask = active->filter();
    if (!showFilterDialog(&window, mask)) return;
    QString err;
    if (!active->setFilter(mask, &err)) appMessage(&window, "Filter", "The file mask is not valid:\n" + err);
    else window.statusBar()->showMessage(mask.isEmpty() ? "Filter removed" : "Filter: " + mask);
  };

  // Open Directory (Ctrl+O): type a path or pick a bookmark / recent directory.
  auto doOpenDir = [&] {
    QString chosen;
    if (showOpenDirectoryDialog(&window, active->path(), active->recentDirs(), chosen)) active->navigate(chosen);
  };

  // Create a new empty file (Shift+F4) — WinSCP's "New > File".
  auto doNewFile = [&] {
    if (active->isRemote() && busy()) return;
    bool okIn = false;
    QString name = QInputDialog::getText(&window, "Create file", "New file name:", QLineEdit::Normal, "", &okIn);
    if (!okIn || name.isEmpty()) return;
    bool ok; std::string err;
    if (active->isRemote()) {
      QString tmp = QDir::tempPath() + "/winscp-new/" + name; QDir().mkpath(QDir::tempPath() + "/winscp-new");
      { QFile f(tmp); f.open(QIODevice::WriteOnly); f.close(); }
      ok = engine::uploadToRemote(s8(tmp), s8(active->path()), &err, true);
    } else {
      QFile f(active->path() + "/" + name); ok = f.open(QIODevice::WriteOnly); if (ok) f.close(); else err = "cannot create";
    }
    active->refresh();
    window.statusBar()->showMessage(ok ? "Created " + name : "Create file failed — " + u8(err));
  };

  // Copy the selected entries' full path(s) to the clipboard (and, for remote, an sftp:// URL).
  auto doCopyPath = [&] {
    QStringList sel = active->selectedItems(); if (sel.isEmpty()) return;
    QStringList paths;
    for (const QString & f : sel) {
      QString full = active->path() + (active->path().endsWith('/') ? "" : "/") + f;
      if (active->isRemote() && connSsh && !connHost.isEmpty())
        paths << QString("sftp://%1@%2:%3%4").arg(connUser, connHost).arg(connPort).arg(full);
      else paths << full;
    }
    QApplication::clipboard()->setText(paths.join("\n"));
    window.statusBar()->showMessage(QString("Copied %1 path(s) to clipboard").arg(paths.size()));
  };
  auto doRename = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select exactly one item to rename"); return; }
    bool okIn = false;
    QString nn = QInputDialog::getText(&window, "Rename", "New name:", QLineEdit::Normal, sel.first(), &okIn);
    if (!okIn || nn.isEmpty() || nn == sel.first()) return;
    std::string err;
    bool ok = active->isRemote() ? engine::remoteRename(s8(sel.first()), s8(nn), &err)
                                 : engine::localRename(s8(active->path()), s8(sel.first()), s8(nn), &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? "Renamed to " + nn : "Rename failed — " + u8(err));
  };
  auto doDelete = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (gConfirmDelete && appMessage(&window, "Delete", QString("Delete %1 item(s)?").arg(sel.size()),
                                     QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    engine::logLine("[ui] doDelete: " + std::to_string(sel.size()) + " item(s) remote=" + (active->isRemote() ? "1" : "0") + " in '" + s8(active->path()) + "'");
    int ok = 0; std::string err, lastErr;
    for (const QString & f : sel) {
      bool r = active->isRemote() ? engine::remoteDelete(s8(f), &err) : engine::localDelete(s8(active->path()), s8(f), &err);
      if (r) ++ok; else lastErr = err;
    }
    engine::logLine("[ui] doDelete done: " + std::to_string(ok) + "/" + std::to_string(sel.size()) + (lastErr.empty() ? "" : (" lastErr=" + lastErr)));
    active->refresh();
    window.statusBar()->showMessage(QString("Deleted %1/%2 item(s)").arg(ok).arg(sel.size()));
    if (ok < sel.size()) appMessage(&window, "Delete", "Some items could not be deleted:\n" + u8(lastErr));
  };
  // Properties (F9) of the selected entries: permissions, and on servers that allow it owner and group.
  auto doProps = [&] {
    if (active->isRemote() && busy()) return;
    FilePanel * panel = active;
    const QStringList sel = panel->selectedItems();
    if (sel.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    PropsRequest rq;
    rq.names = sel; rq.location = panel->path(); rq.remote = panel->isRemote();
    qint64 bytes = 0; bool sizeKnown = true;
    for (const QString & n : sel)
    {
      const engine::DirEntry * x = panel->entryNamed(n);
      if (x != nullptr && x->isDir) { rq.anyDirectory = true; if (!panel->hasDirSize(n)) sizeKnown = false; }
      bytes += panel->sizeOf(n);
    }
    if (sizeKnown) rq.sizeText = u8(engine::formatSize(bytes)) + (sel.size() > 1 ? QString(" in %1 items").arg(sel.size()) : QString());
    else if (sel.size() > 1) rq.sizeText = QString("%1 items").arg(sel.size());
    const engine::DirEntry * e = panel->entryNamed(sel.first());
    if (e != nullptr && sel.size() == 1)
    {
      rq.linkTarget = u8(e->linkTarget);
      rq.modified = formatPanelTime(e->mtimeSecs, e->mtimeHasSeconds);
    }
    if (panel->isRemote())
    {
      using C = engine::Capability;
      rq.canChangeRights = engine::remoteCapable(C::ChangeMode);
      rq.canChangeOwner = engine::remoteCapable(C::ChangeOwner);
      rq.canChangeGroup = engine::remoteCapable(C::ChangeGroup);
      if (e != nullptr)
      {
        rq.owner = u8(e->ownerName); rq.group = u8(e->groupName);
        rq.rights = panellogic::octalFromRights(e->rights);
        if (rq.rights < 0) { const std::string oct = engine::remoteFileOctal(e->name); if (!oct.empty()) rq.rights = QString(u8(oct)).toInt(nullptr, 8); }
      }
    }
    else
    {
      LocalFileInfo li;
      if (localFileInfo(panel->path() + "/" + sel.first(), li)) { rq.rights = li.mode; rq.owner = li.owner; rq.group = li.group; }
      rq.canChangeRights = true;
    }
    const PropsResult r = showPropertiesDialog(&window, rq);
    if (!r.accepted || !r.anything()) return;
    int ok = 0; QString lastErr;
    for (const QString & n : sel)
    {
      if (panel->isRemote())
      {
        engine::RemoteProps p;
        p.setRights = r.rightsChanged; p.octal = s8(r.octal); p.addXToDirs = r.addXToDirs;
        p.setOwner = r.ownerChanged; p.owner = s8(r.owner);
        p.setGroup = r.groupChanged; p.group = s8(r.group);
        p.recursive = r.recursive;
        std::string err;
        if (engine::remoteSetProperties(s8(n), p, &err)) ++ok; else lastErr = u8(err);
      }
      else
      {
        QString err;
        if (!r.rightsChanged || localChmod(panel->path() + "/" + n, r.octal.toInt(nullptr, 8), &err)) ++ok; else lastErr = err;
      }
    }
    panel->refresh();
    panel->selectNames(sel);
    window.statusBar()->showMessage(QString("Properties changed for %1 of %2 item(s)").arg(ok).arg(sel.size()));
    if (ok < sel.size())
      appMessage(&window, "Properties", QString("The properties of %1 of %2 item(s) could not be changed.\n\n%3").arg(sel.size() - ok).arg(sel.size()).arg(lastErr.trimmed()));
  };

  // New > Link (Ctrl+Alt+F6): a symbolic (or hard) link in the active panel's directory.
  auto createLink = [&](FilePanel * panel, const QString & name, const QString & target, bool symbolic) -> bool {
    QString error;
    bool ok;
    if (panel->isRemote()) { std::string err; ok = engine::remoteCreateLink(s8(name), s8(target), symbolic, &err); error = u8(err); }
    else ok = localCreateLink(panel->path() + "/" + name, target, symbolic, panel->path(), &error);
    panel->refresh();
    if (ok) { panel->selectNames(QStringList(name)); window.statusBar()->showMessage("Created link " + name); }
    else
    {
      window.statusBar()->showMessage("Create link failed");
      appMessage(&window, "Add Link", QString("Error creating link \"%1\".\n\n%2").arg(name, error.trimmed().isEmpty() ? QString("The operation failed.") : error.trimmed()));
    }
    return ok;
  };
  auto doNewLink = [&] {
    if (active->isRemote() && busy()) return;
    FilePanel * panel = active;
    using C = engine::Capability;
    const bool canSym = !panel->isRemote() || engine::remoteCapable(C::SymbolicLink);
    const bool canHard = !panel->isRemote() || engine::remoteCapable(C::HardLink);
    if (!canSym && !canHard) { appMessage(&window, "Add Link", "The server does not support links."); return; }
    QString name, target; bool symbolic = canSym;
    const QStringList sel = panel->selectedItems();
    if (sel.size() == 1) target = sel.first();     // WinSCP: the link points to the selected file
    if (!showLinkDialog(&window, name, target, symbolic, canSym, canHard)) return;
    createLink(panel, name, target, symbolic);
  };

  // Duplicate (Shift+F5) / Move To (Shift+F6): copy or move within the same side - on the server for
  // a remote panel (nothing is downloaded), on this machine for a local one.
  auto copyOrMoveWithin = [&](FilePanel * panel, bool move, const QStringList & names, const QString & targetDir, const QString & newName) -> bool {
    int ok = 0; QString lastErr;
    const QString tdir = u8(panellogic::normalizePath(s8(targetDir)));
    for (const QString & n : names)
    {
      const QString nn = (names.size() == 1 && !newName.isEmpty()) ? newName : n;
      if (panel->isRemote())
      {
        std::string err;
        const bool r = move ? engine::remoteMoveTo(s8(n), s8(tdir), s8(nn), &err) : engine::remoteDuplicate(s8(n), s8(tdir), s8(nn), &err);
        if (r) ++ok; else lastErr = u8(err);
      }
      else
      {
        QString err;
        if (localCopyOrMove(panel->path() + "/" + n, tdir + (tdir.endsWith('/') ? "" : "/") + nn, move, &err)) ++ok; else lastErr = err;
      }
    }
    left->refresh(); right->refresh();
    const QString verb = move ? "Moved" : "Duplicated";
    window.statusBar()->showMessage(QString("%1 %2 of %3 item(s)").arg(verb).arg(ok).arg(names.size()));
    if (ok < names.size())
      appMessage(&window, move ? "Move" : "Duplicate",
                 QString("%1 of %2 item(s) could not be %3.\n\n%4").arg(names.size() - ok).arg(names.size()).arg(move ? "moved" : "duplicated", lastErr.trimmed()));
    return ok == names.size();
  };
  auto doCopyOrMoveWithin = [&](bool move) {
    if (busy()) return;
    FilePanel * panel = active;
    const QStringList sel = panel->selectedItems();
    if (sel.isEmpty()) { window.statusBar()->showMessage("No files selected"); return; }
    if (panel->isRemote() && !engine::remoteCapable(move ? engine::Capability::RemoteMove : engine::Capability::RemoteCopy))
    { appMessage(&window, move ? "Move" : "Duplicate", move ? "The server does not support moving files." : "The server does not support duplicating files."); return; }
    QString targetDir = panel->path(), newName = sel.size() == 1 ? sel.first() : QString();
    if (!showRemoteCopyDialog(&window, move, sel, targetDir, newName)) return;
    copyOrMoveWithin(panel, move, sel, targetDir, newName);
  };

  // Server and Protocol Information (the session's protocol, encryption, capabilities, free space).
  auto doSessionInfo = [&] {
    if (!engine::remoteConnected()) { appMessage(&window, "Server and Protocol Information", "Connect a remote session first."); return; }
    if (busy()) return;
    FilePanel * rp = right->isRemote() ? right : left;
    showSessionInfoDialog(&window, engine::sessionInformation(), rp->path(),
      [&](const QString & path, engine::SpaceInfo & si, QString * error) -> bool {
        std::string err; const bool ok = engine::remoteSpaceAvailable(s8(path), si, &err);
        if (!ok && error) *error = u8(err);
        return ok;
      });
  };

  auto doOpen = [&] {
    if (active->isRemote() && busy()) return;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one file to open"); return; }
    QString f = sel.first();
    QString localPath;
    if (active->isRemote())
    {
      QString tmp = QDir::tempPath() + "/winscp-open";
      QDir().mkpath(tmp);
      std::string err;
      window.statusBar()->showMessage("Opening " + f + "\xE2\x80\xA6");
      QApplication::processEvents();
      if (!engine::downloadFromRemote(engine::joinPath(s8(active->path()), s8(f)), s8(tmp), &err, true))
      { appMessage(&window, "Open", "Could not download: " + u8(err)); return; }
      localPath = tmp + "/" + f;
      // watch the temp copy: any save (this app's editor or an external one) re-uploads it
      (*watchRemoteDir)[localPath] = active->path();
      if (!watcher->files().contains(localPath)) watcher->addPath(localPath);
    }
    else localPath = active->path() + "/" + f;
    QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    window.statusBar()->showMessage(active->isRemote() ? ("Opened " + f + " (auto-uploads on save)") : ("Opened " + f));
  };
  // Internal editor (F4): download a remote file to temp, edit in a built-in text editor; Save writes
  // it back (re-uploads for remote). Local files are edited in place.
  auto doEdit = [&] {
    if (active->isRemote() && busy()) return;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select one file to edit"); return; }
    QString f = sel.first();
    bool remote = active->isRemote();
    QString remoteDir = active->path();
    QString localPath;
    if (remote)
    {
      QString tmp = QDir::tempPath() + "/winscp-edit"; QDir().mkpath(tmp);
      std::string err;
      if (!engine::downloadFromRemote(engine::joinPath(s8(remoteDir), s8(f)), s8(tmp), &err, true))
      { appMessage(&window, "Edit", "Could not download: " + u8(err)); return; }
      localPath = tmp + "/" + f;
    }
    else localPath = active->path() + "/" + f;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) { appMessage(&window, "Edit", "Cannot open file"); return; }
    QString text = QString::fromUtf8(file.readAll()); file.close();

    auto * ed = new EditorWindow(&window, f + (remote ? "  \xE2\x80\x94  " + remoteDir + " (remote)" : ""), text);
    auto * te = ed->te;
    ed->onSave = [te, localPath, remote, remoteDir, f, &window, &left, enqueueUpload]() -> bool {
      QFile out(localPath);
      if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) { appMessage(&window, "Save", "Cannot write file"); return false; }
      out.write(te->toPlainText().toUtf8()); out.close();
      if (remote) enqueueUpload(localPath, remoteDir, "Edit-upload");   // shows in the transfer queue
      else { window.statusBar()->showMessage("Saved " + f); left->refresh(); }
      return true;
    };
    ed->show();
  };
  auto doQuit = [&] { window.close(); };

  // Synchronize directories (local panel <-> remote panel) via the engine's TSynchronizeChecklist.
  auto doSync = [&] {
    FilePanel * lp = left->isRemote() ? right : left;
    FilePanel * rp = left->isRemote() ? left : right;
    if (!rp->isRemote()) { appMessage(&window, "Synchronize", "Connect a remote session first."); return; }
    if (busy()) return;
    QDialog dlg(&window); dlg.setWindowTitle("Synchronize"); dlg.resize(600, 420);
    auto * v = new QVBoxLayout(&dlg);
    auto * form = new QFormLayout;
    form->addRow("Local:", new QLabel(lp->path()));
    form->addRow("Remote:", new QLabel(rp->path()));
    auto * dir = new QComboBox; dir->addItems({ "Local \xE2\x86\x92 Remote (upload)", "Remote \xE2\x86\x92 Local (download)", "Both directions" });
    form->addRow("Direction:", dir);
    auto * cbDel = new QCheckBox("Delete files missing on the source");
    form->addRow("", cbDel);
    v->addLayout(form);
    auto * list = new QTableView; auto * lm = new QStandardItemModel(0, 3, &dlg);
    lm->setHorizontalHeaderLabels({ "Action", "File", "Size" }); list->setModel(lm);
    list->setEditTriggers(QAbstractItemView::NoEditTriggers); list->verticalHeader()->setVisible(false);
    list->horizontalHeader()->setStretchLastSection(true); list->setColumnWidth(1, 320);
    v->addWidget(list, 1);
    auto * bb = new QDialogButtonBox; auto * bCompare = bb->addButton("Compare", QDialogButtonBox::ActionRole);
    auto * bApply = bb->addButton("Synchronize", QDialogButtonBox::AcceptRole); bApply->setEnabled(false);
    bb->addButton(QDialogButtonBox::Close);
    v->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(bCompare, &QPushButton::clicked, [&]{
      lm->removeRows(0, lm->rowCount());
      std::string err;
      auto items = engine::synchronizeCollect(s8(lp->path()), s8(rp->path()), dir->currentIndex(), cbDel->isChecked(), &err);
      if (!err.empty()) { appMessage(&dlg, "Synchronize", u8(err)); return; }
      for (const auto & it : items) {
        QList<QStandardItem *> row;
        row << new QStandardItem(u8(it.action)) << new QStandardItem(u8(it.name))
            << new QStandardItem(it.isDir ? QString() : u8(engine::formatSize(it.size)));
        lm->appendRow(row);
      }
      bApply->setEnabled(lm->rowCount() > 0);
      dlg.setWindowTitle(QString("Synchronize \xE2\x80\x94 %1 change(s)").arg(lm->rowCount()));
    });
    QObject::connect(bApply, &QPushButton::clicked, [&]{
      std::string err;
      bool ok = engine::synchronizeApply(&err);
      engine::synchronizeRelease();
      if (!ok) appMessage(&dlg, "Synchronize", u8(err));
      dlg.accept();
    });
    dlg.exec();
    engine::synchronizeRelease();
    lp->refresh(); rp->refresh();
  };

  //--- Keep Remote Directory up to Date -------------------------------------------
  // Watch the local panel's directory and reflect every change on the remote panel's directory.
  auto doKeepUpToDate = [&] {
    FilePanel * lp = left->isRemote() ? right : left;
    FilePanel * rp = left->isRemote() ? left : right;
    if (!rp->isRemote()) { appMessage(&window, "Keep remote directory up to date", "Connect a remote session first."); return; }
    if (busy()) return;
    KeepUpToDateContext ctx;
    ctx.localDir = lp->path(); ctx.remoteDir = rp->path();
    ctx.acquire = [&]() -> bool { if (gTransferRunning.load()) return false; gTransferRunning = true; return true; };
    ctx.release = [&] { gTransferRunning = false; };
    ctx.log = [&](const QString & l) { log(l); };
    showKeepUpToDateDialog(&window, ctx);
    lp->refresh(); rp->refresh();
  };

  //--- Find Files --------------------------------------------------------------
  auto doFind = [&](const QString & autoMask) {
    FilePanel * panel = active;
    FindContext ctx;
    ctx.remote = panel->isRemote();
    ctx.startDir = panel->path();
    ctx.acquire = [&]() -> bool { if (busy()) return false; gTransferRunning = true; return true; };
    ctx.release = [&] { gTransferRunning = false; };
    ctx.focus = [&, panel](const QString & dir, const QString & name) {
      panel->navigate(dir);
      panel->selectNames(QStringList(name));
      panel->view()->setFocus();
    };
    showFindDialog(&window, ctx, autoMask);
  };

  //--- Calculate Directory Sizes ---------------------------------------------
  // The selected directories (or every directory of the panel when none is selected); the totals
  // appear in the Size column. Runs in the background; the queue's Cancel button stops it.
  auto doCalcSizes = [&] {
    FilePanel * panel = active;
    QStringList dirs;
    for (const QString & n : panel->selectedItems()) { const engine::DirEntry * e = panel->entryNamed(n); if (e && e->isDir) dirs << n; }
    if (dirs.isEmpty())
      for (const engine::DirEntry & e : panel->entries()) if (e.isDir && !e.isParent) dirs << u8(e.name);
    if (dirs.isEmpty()) { window.statusBar()->showMessage("No directories here"); return; }
    if (busy()) return;
    gTransferRunning = true;
    btnCancel->setEnabled(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    *currentCancel = cancel;
    const bool remote = panel->isRemote();
    const QString path = panel->path();
    window.statusBar()->showMessage(QString("Calculating the size of %1 director%2\xE2\x80\xA6").arg(dirs.size()).arg(dirs.size() == 1 ? "y" : "ies"));
    std::thread([&, panel, dirs, remote, path, cancel] {
      qint64 total = 0; int files = 0, done = 0; std::string lastErr;
      for (const QString & name : dirs)
      {
        if (cancel->load()) break;
        engine::SizeResult r; std::string err;
        auto cancelled = [cancel]() { return cancel->load(); };
        bool ok = remote ? engine::remoteCalculateSize(s8(name), r, cancelled, &err)
                         : engine::localCalculateSize(engine::joinPath(s8(path), s8(name)), r, cancelled, &err);
        if (cancel->load()) break;
        if (!ok && !err.empty()) lastErr = err;
        total += r.bytes; files += r.files; ++done;
        const qint64 bytes = r.bytes; const int n = done, of = (int)dirs.size();
        QMetaObject::invokeMethod(&window, [&, panel, path, name, bytes, n, of] {
          panel->setDirSize(path, name, bytes);
          window.statusBar()->showMessage(QString("Calculating sizes\xE2\x80\xA6 %1/%2").arg(n).arg(of));
        }, Qt::QueuedConnection);
      }
      const bool wasCancelled = cancel->load();
      QMetaObject::invokeMethod(&window, [&, total, files, done, wasCancelled, lastErr] {
        gTransferRunning = false;
        btnCancel->setEnabled(false);
        window.statusBar()->showMessage(QString("%1 director%2: %3 in %4 file(s)%5")
          .arg(done).arg(done == 1 ? "y" : "ies").arg(u8(engine::formatSize(total))).arg(files).arg(wasCancelled ? " (cancelled)" : ""));
        if (!lastErr.empty()) appMessage(&window, "Calculate Directory Sizes", "Some directories could not be read:\n" + u8(lastErr));
      }, Qt::QueuedConnection);
    }).detach();
  };

  //--- Compare Directories ------------------------------------------------------
  // Marks, in both panels, the files that are missing or (by the chosen criteria) different on
  // the other side - WinSCP's Commands > Compare Directories.
  auto doCompareDirs = [&] {
    CompareCriteria c; c.byTime = gCompareByTime; c.bySize = gCompareBySize;
    CompareResult r = compareDirectories(left->entries(), right->entries(), c);
    QStringList l, rr;
    for (const std::string & n : r.left) l << u8(n);
    for (const std::string & n : r.right) rr << u8(n);
    left->selectNames(l); right->selectNames(rr);
    const QString how = c.byTime && c.bySize ? "time and size" : c.byTime ? "time" : c.bySize ? "size" : "existence only";
    if (l.isEmpty() && rr.isEmpty())
    {
      window.statusBar()->showMessage("Compare directories: no differences (by " + how + ")");
      appMessage(&window, "Compare Directories", "No differences found (compared by " + how + ").");
    }
    else
      window.statusBar()->showMessage(QString("Compare directories (by %1): %2 file(s) marked on the left, %3 on the right")
                                        .arg(how).arg(l.size()).arg(rr.size()));
  };

  //--- Compare Files (side by side) --------------------------------------------
  // The selected file against the file of the same name in the other panel; or, when the other
  // panel has no such file, against the single file selected there.
  static int compareSeq = 0;
  auto doCompareFiles = [&]() -> QString {
    FilePanel * other = (active == left) ? right : left;
    QStringList sel = active->selectedFiles();
    if (sel.size() != 1) { QString m = "Select one file to compare."; window.statusBar()->showMessage(m); return m; }
    QString mine = sel.first(), theirs;
    const engine::DirEntry * same = other->entryNamed(mine);
    if (same != nullptr && !same->isDir) theirs = mine;
    else if (other->selectedFiles().size() == 1) theirs = other->selectedFiles().first();
    else
    {
      QString m = QString("\"%1\" has no counterpart in the other panel. Open the directory that holds the other copy, "
                          "or select the file to compare it with there.").arg(mine);
      appMessage(&window, "Compare Files", m);
      return m;
    }
    if ((active->isRemote() || other->isRemote()) && busy()) return QString();
    // the file of the LEFT panel goes on the left
    FilePanel * lpn = left, * rpn = right;
    const QString lname = (active == left) ? mine : theirs, rname = (active == left) ? theirs : mine;
    auto materialize = [&](FilePanel * pn, const QString & name, const char * sideTag, DiffSide & side) -> bool {
      side.title = QString("%1 \xE2\x80\x94 %2 (%3)").arg(name, pn->path(), pn->isRemote() ? "remote" : "local");
      if (!pn->isRemote()) { side.path = pn->path() + (pn->path().endsWith('/') ? "" : "/") + name; return true; }
      const QString tmp = QString("%1/freescp-compare/%2-%3-%4").arg(QDir::tempPath()).arg(QCoreApplication::applicationPid()).arg(++compareSeq).arg(sideTag);
      QDir().mkpath(tmp);
      window.statusBar()->showMessage("Downloading " + name + " to compare\xE2\x80\xA6");
      QApplication::processEvents();
      // downloadFromRemote looks the file up in the session's current listing = this panel's
      std::string err;
      if (!engine::downloadFromRemote(engine::joinPath(s8(pn->path()), s8(name)), s8(tmp), &err, true))
      { appMessage(&window, "Compare Files", "Could not download " + name + ":\n" + u8(err)); return false; }
      side.path = tmp + "/" + name;
      return true;
    };
    DiffSide ls, rs;
    if (!materialize(lpn, lname, "l", ls) || !materialize(rpn, rname, "r", rs)) return QString();
    QString result = showFileComparison(&window, ls, rs);
    window.statusBar()->showMessage(result);
    for (const DiffSide * sd : { &ls, &rs })       // remove the temporary copies
      if (sd->path.startsWith(QDir::tempPath() + "/freescp-compare/")) QDir(QFileInfo(sd->path).absolutePath()).removeRecursively();
    return result;
  };

  // Right-click context menu on each panel (WinSCP-style).
  std::function<void()> ctxOpen, ctxEdit, ctxMove, ctxNewFile;   // set below, once those operations exist
  auto installContextMenu = [&](FilePanel * panel) {
    panel->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(panel->view(), &QWidget::customContextMenuRequested, [&, panel](const QPoint & pos) {
      active = panel; panel->onActivated();
      FilePanel * other = (panel == left) ? right : left;
      const bool any = !panel->selectedItems().isEmpty();
      QMenu menu;
      auto add = [&](const QString & icon, const QString & text, const std::function<void()> & fn, bool enabled = true) {
        QAction * a = menu.addAction(appIcon(icon), text, fn); a->setEnabled(enabled); return a;
      };
      add("open", "Open\tF3", ctxOpen, panel->selectedFiles().size() == 1);
      add("edit", "Edit\tF4", ctxEdit, panel->selectedFiles().size() == 1);
      menu.addSeparator();
      add(panel->isRemote() ? "download" : (other->isRemote() ? "upload" : "copy"),
          panel->isRemote() ? "Download\xE2\x80\xA6\tF5" : (other->isRemote() ? "Upload\xE2\x80\xA6\tF5" : "Copy\xE2\x80\xA6\tF5"), doCopy, any);
      add("duplicate", "Duplicate\xE2\x80\xA6\tShift+F5", [&]{ doCopyOrMoveWithin(false); }, any);
      add("move", "Move To\xE2\x80\xA6\tShift+F6", [&]{ doCopyOrMoveWithin(true); }, any);
      add("delete", "Delete\tF8", doDelete, any);
      add("rename", "Rename\tF2", doRename, panel->selectedItems().size() == 1);
      menu.addSeparator();
      QMenu * mNew = menu.addMenu(appIcon("newfile"), "New");
      mNew->addAction(appIcon("newfile"), "File\xE2\x80\xA6\tShift+F4", ctxNewFile);
      mNew->addAction(appIcon("newdir"), "Directory\xE2\x80\xA6\tF7", doMkdir);
      mNew->addAction(appIcon("link"), "Link\xE2\x80\xA6\tCtrl+Alt+F6", doNewLink);
      menu.addSeparator();
      add("copy", "Copy Path to Clipboard\tCtrl+Shift+C", doCopyPath, any);
      add("compare", "Compare Files", [&]{ doCompareFiles(); }, panel->selectedFiles().size() == 1);
      add("find", "Calculate Directory Sizes", doCalcSizes);
      menu.addSeparator();
      add("properties", "Properties\tF9", doProps, any);
      menu.exec(panel->view()->viewport()->mapToGlobal(pos));
    });
  };
  installContextMenu(left);
  installContextMenu(right);
  ctxOpen = doOpen; ctxEdit = doEdit; ctxMove = doMove; ctxNewFile = doNewFile;

  // About dialog: icon on top, GPLv3, repo link, credit to WinSCP.
  // "What is this and who pays for it" - shown once on first launch and from Help > Support FreeSCP.
  // FreeSCP sends no usage data. The only request it makes on its own is the optional check for a
  // newer version (updatecheck.h); everything else is traffic to the servers the user connects to.
  // The Ko-fi page opens in the browser, on click.
  static const char * const kKofiUrl = "https://ko-fi.com/oriolegea";
  auto showWelcome = [&](bool firstRun){
    QDialog d(&window); d.setWindowTitle(firstRun ? "Welcome to FreeSCP" : "Support FreeSCP");
    auto * lay = new QVBoxLayout(&d); lay->setContentsMargins(26, 22, 26, 18); lay->setSpacing(12);
    auto * icon = new QLabel; icon->setAlignment(Qt::AlignHCenter);
    icon->setPixmap(QPixmap(":/winscp.png").scaled(84, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lay->addWidget(icon);
    auto * body = new QLabel(
      "<div align='center'>"
      "<h2 style='margin:4px'>FreeSCP is 100% free</h2>"
      "<p style='margin:2px;color:gray'>No paid tier, no ads, no account.</p>"
      "</div>"
      "<table cellspacing='8' style='margin-top:6px'>"
      "<tr><td><b>Free&nbsp;forever</b></td><td>Every feature, for everyone. There is nothing to unlock.</td></tr>"
      "<tr><td><b>Open&nbsp;source</b></td><td>GPLv3 &mdash; the code is at "
      "<a href='https://github.com/oegea/FreeSCP'>github.com/oegea/FreeSCP</a>.</td></tr>"
      "<tr><td><b>No&nbsp;telemetry</b></td><td>No tracking, no usage data, no account. FreeSCP only talks "
      "to the servers you connect to, plus a check for a newer version (it sends nothing about you, and "
      "Preferences can turn it off).</td></tr>"
      "</table>"
      "<p style='margin:10px 2px 0'>FreeSCP is built in spare time. If it is useful to you and you would "
      "like to help its development, you can buy me a coffee &mdash; entirely optional.</p>");
    body->setTextFormat(Qt::RichText); body->setOpenExternalLinks(true); body->setWordWrap(true);
    body->setMinimumWidth(430); body->setMaximumWidth(470);
    lay->addWidget(body);
    auto * bb = new QDialogButtonBox;
    auto * coffee = bb->addButton(QString::fromUtf8("\xE2\x98\x95  Buy me a coffee (Ko-fi)"), QDialogButtonBox::ActionRole);
    auto * go = bb->addButton(firstRun ? "Start using FreeSCP" : "Close", QDialogButtonBox::AcceptRole);
    go->setDefault(true);
    lay->addWidget(bb);
    QObject::connect(coffee, &QPushButton::clicked, [&]{ QDesktopServices::openUrl(QUrl(kKofiUrl)); });
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    if (!qEnvironmentVariableIsEmpty("WINSCP_WELCOME_SHOT"))   // dev affordance: render the dialog and return
      QTimer::singleShot(150, &d, [&]{ d.grab().save(qEnvironmentVariable("WINSCP_WELCOME_SHOT")); d.accept(); });
    d.exec();
  };

  auto showAbout = [&]{
    QDialog d(&window); d.setWindowTitle("About FreeSCP");
    auto * lay = new QVBoxLayout(&d);
    auto * icon = new QLabel; icon->setAlignment(Qt::AlignHCenter);
    icon->setPixmap(QPixmap(":/winscp.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    lay->addWidget(icon);
    auto * body = new QLabel(
      "<div align='center'>"
      "<h2 style='margin:4px'>FreeSCP</h2>"
      "<p style='margin:2px;color:gray'>Version " FREESCP_VERSION "</p>"
      "<p style='margin:2px'>A native macOS &amp; Linux port of WinSCP.</p>"
      "<p style='margin:2px'>SFTP · SCP · FTP · WebDAV · S3</p>"
      "<p style='margin:8px 2px 2px'><a href='https://github.com/oegea/FreeSCP'>github.com/oegea/FreeSCP</a></p>"
      "<p style='margin:2px;color:gray'>Free software under the GPLv3 — project started by Oriol Egea.</p>"
      "<p style='margin:8px 2px 2px'><b>100% free &middot; open source &middot; no telemetry.</b><br>"
      "Want to support development? <a href='https://ko-fi.com/oriolegea'>Buy me a coffee on Ko-fi</a>.</p>"
      "<p style='margin:10px 8px 2px;color:gray'>With gratitude to Martin Přikryl and the WinSCP "
      "contributors. WinSCP has been a fantastic tool on Windows for many years; FreeSCP simply "
      "brings it to other platforms, using AI to help with the port. No ownership of their work is "
      "claimed.</p>"
      "</div>");
    body->setTextFormat(Qt::RichText); body->setOpenExternalLinks(true); body->setWordWrap(true);
    body->setMaximumWidth(420);
    lay->addWidget(body);
    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok); lay->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    d.exec();
  };

  // The Options > "Show hidden files" toggle (also in Preferences).
  auto * actHidden = new QAction("Show &hidden files", &window); actHidden->setCheckable(true); actHidden->setChecked(gShowHidden);
  actHidden->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
  QObject::connect(actHidden, &QAction::toggled, [&](bool on){ gShowHidden = on; left->refresh(); right->refresh();
    QSettings().setValue("prefs/showHidden", on); });

  auto showPreferences = [&]{
    QDialog d(&window); d.setWindowTitle("Preferences"); d.resize(520, 360);
    auto * outer = new QVBoxLayout(&d);
    auto * tabs = new QTabWidget; outer->addWidget(tabs, 1);

    // --- Environment ---
    auto * envTab = new QWidget; auto * envLay = new QVBoxLayout(envTab);
    auto * cbHidden = new QCheckBox("Show hidden files"); cbHidden->setChecked(gShowHidden);
    auto * cbDel = new QCheckBox("Confirm file deletion"); cbDel->setChecked(gConfirmDelete);
    auto * cbOvr = new QCheckBox("Confirm overwrites"); cbOvr->setChecked(gConfirmOverwrite);
    auto * cbUpd = new QCheckBox("Check for new &versions at startup"); cbUpd->setChecked(updates::enabled());
    auto * updHint = new QLabel("Asks GitHub for the latest version number, at most once a day. Nothing about you "
                                "or this computer is sent, and nothing is downloaded.");
    updHint->setWordWrap(true); updHint->setStyleSheet("color: gray; margin-left: 22px;");
    envLay->addWidget(cbHidden); envLay->addWidget(cbDel); envLay->addWidget(cbOvr);
    envLay->addSpacing(10); envLay->addWidget(cbUpd); envLay->addWidget(updHint); envLay->addStretch(1);
    tabs->addTab(envTab, "Environment");

    // --- Transfer ---
    auto * trTab = new QWidget; auto * trForm = new QFormLayout(trTab);
    auto * spinPar = new QSpinBox; spinPar->setRange(1, 4); spinPar->setValue(gParallelMax);
    trForm->addRow("Maximum parallel transfers:", spinPar);
    auto * xferSummary = new QLabel(transferSummary(engine::transferSettings())); xferSummary->setWordWrap(true);
    auto * xferEdit = new QPushButton("Transfer settings\xE2\x80\xA6");
    trForm->addRow("Transfer settings:", xferSummary);
    trForm->addRow("", xferEdit);
    QObject::connect(xferEdit, &QPushButton::clicked, &d, [&, xferSummary] {
      if (editTransferSettings(&d)) xferSummary->setText(transferSummary(engine::transferSettings()));
    });
    tabs->addTab(trTab, "Transfer");

    // --- Panels ---
    auto * pnTab = new QWidget; auto * pnLay = new QVBoxLayout(pnTab);
    auto * cbAlt = new QCheckBox("Alternating row colors"); cbAlt->setChecked(gAltColors);
    auto * cmpBox = new QGroupBox("Compare directories");
    auto * cmpLay = new QVBoxLayout(cmpBox);
    auto * cbCmpTime = new QCheckBox("Compare by &time (mark the newer file)"); cbCmpTime->setChecked(gCompareByTime);
    auto * cbCmpSize = new QCheckBox("Compare by &size (mark files whose sizes differ)"); cbCmpSize->setChecked(gCompareBySize);
    auto * cmpHint = new QLabel("Files that exist in one panel only are always marked.");
    cmpHint->setStyleSheet("color: gray;");
    cmpLay->addWidget(cbCmpTime); cmpLay->addWidget(cbCmpSize); cmpLay->addWidget(cmpHint);
    pnLay->addWidget(cbAlt); pnLay->addWidget(cmpBox); pnLay->addStretch(1);
    tabs->addTab(pnTab, "Panels");

    auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    outer->addWidget(bb);
    QObject::connect(bb, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (execDialog(d, "preferences") != QDialog::Accepted) return;
    gShowHidden = cbHidden->isChecked(); gConfirmDelete = cbDel->isChecked();
    gConfirmOverwrite = cbOvr->isChecked(); gParallelMax = spinPar->value(); gAltColors = cbAlt->isChecked();
    gCompareByTime = cbCmpTime->isChecked(); gCompareBySize = cbCmpSize->isChecked();
    updates::setEnabled(cbUpd->isChecked());
    actHidden->setChecked(gShowHidden);
    QSettings s;
    s.setValue("prefs/showHidden", gShowHidden); s.setValue("prefs/confirmDelete", gConfirmDelete);
    s.setValue("prefs/confirmOverwrite", gConfirmOverwrite); s.setValue("prefs/parallelMax", gParallelMax);
    s.setValue("prefs/altColors", gAltColors);
    s.setValue("prefs/compareByTime", gCompareByTime); s.setValue("prefs/compareBySize", gCompareBySize);
    left->view()->setAlternatingRowColors(gAltColors); right->view()->setAlternatingRowColors(gAltColors);
    left->refresh(); right->refresh();
  };

  auto addBookmark = [&]{
    QStringList bm = bookmarkList();
    const QString p = active->path();
    if (!bm.contains(p)) { bm << p; setBookmarkList(bm); }
    window.statusBar()->showMessage("Bookmarked " + p);
  };
  // The session-independent part of the toolbar's Transfer Settings caption.
  auto showXferCaption = [&] {
    const bool def = transferSummary(engine::transferSettings()) == transferSummary(engine::TransferSettings());
    tbXfer->setText(def ? "Transfer Settings: Default" : "Transfer Settings: Custom");
    tbXfer->setToolTip("Transfer settings: " + transferSummary(engine::transferSettings()));
  };
  showXferCaption();

  QAction * actPrefs = nullptr;
  // Menu bar in WinSCP's Commander order: Local, Mark, Files, Commands, Tabs, Options, Remote, Help.
  // "Local" acts on the left panel and "Remote" on the right one, as in WinSCP.
  {
    auto * mb = window.menuBar();
    auto sideMenu = [&](const QString & title, FilePanel * pn) {
      auto * m = mb->addMenu(title);
      auto on = [&, pn](const std::function<void()> & f) -> std::function<void()> { return [&, pn, f]{ pn->onActivated(); f(); }; };
      auto * go = m->addMenu("&Go To");
      go->addAction(appIcon("opendir"), "&Open Directory/Bookmark\xE2\x80\xA6\tCtrl+O", on(doOpenDir));
      go->addSeparator();
      go->addAction(appIcon("parent"), "&Parent Directory\tBackspace", on([&]{ active->goUp(); }));
      go->addAction(appIcon("root"), "&Root Directory\tCtrl+\\", on([&]{ active->navigate("/"); }));
      go->addAction(appIcon("home"), "&Home Directory\tCtrl+H", on([&]{ if (active->onHome) active->onHome(); }));
      go->addSeparator();
      go->addAction(appIcon("back"), "&Back\tAlt+Left", on([&]{ active->goBack(); }));
      go->addAction(appIcon("forward"), "&Forward\tAlt+Right", on([&]{ active->goForward(); }));
      m->addAction(appIcon("refresh"), "&Refresh\tCtrl+R", on([&]{ active->refresh(); }));
      m->addSeparator();
      m->addAction(appIcon("bookmark"), "&Add Path to Bookmarks\tCtrl+B", on(addBookmark));
      auto * bm = m->addMenu("&Bookmarks");
      QObject::connect(bm, &QMenu::aboutToShow, [&, bm, pn]{
        bm->clear();
        const QStringList list = bookmarkList();
        for (const QString & p : list) bm->addAction(appIcon("bookmark"), p, [&, pn, p]{ pn->onActivated(); pn->navigate(p); });
        if (list.isEmpty()) bm->addAction("(no bookmarks)")->setEnabled(false);
        else
        {
          bm->addSeparator();
          bm->addAction("&Remove current directory", [&, pn]{ QStringList b = bookmarkList(); b.removeAll(pn->path()); setBookmarkList(b); });
        }
      });
      m->addSeparator();
      m->addAction(appIcon("filter"), "&Filter\xE2\x80\xA6\tCtrl+Alt+I", on(doFilter));
      auto * sort = m->addMenu("&Sort");
      QObject::connect(sort, &QMenu::aboutToShow, [sort, pn]{
        sort->clear();
        const QStringList cols = pn->dumpColumns().value(0).split(" | ");
        for (int i = 0; i < cols.size(); ++i) sort->addAction("By " + cols[i], [pn, i]{ pn->sortByColumn(i); });
      });
      auto * tree = m->addAction(appIcon("tree"), "&Tree");
      tree->setCheckable(true);
      QObject::connect(tree, &QAction::triggered, [pn](bool v){ pn->setTreeVisible(v); });
      QObject::connect(m, &QMenu::aboutToShow, [tree, pn]{ tree->setChecked(pn->treeVisible()); });
    };

    sideMenu("&Local", left);

    auto * mMark = mb->addMenu("&Mark");
    mMark->addAction(appIcon("select"), "&Select Files\xE2\x80\xA6\tCtrl++", [&]{ doSelectMask(true); });
    mMark->addAction(appIcon("unselect"), "&Unselect Files\xE2\x80\xA6\tCtrl+-", [&]{ doSelectMask(false); });
    mMark->addAction(appIcon("selectall"), "Select &All\tCtrl+A", [&]{ active->selectAll(); });
    mMark->addAction("&Invert Selection\tCtrl+*", [&]{ active->invertSelection(); });
    mMark->addAction("&Clear Selection", [&]{ active->clearSelection(); });

    auto * mFiles = mb->addMenu("&Files");
    auto * mNew = mFiles->addMenu(appIcon("newfile"), "&New");
    mNew->addAction(appIcon("newfile"), "&File\xE2\x80\xA6\tShift+F4", doNewFile);
    mNew->addAction(appIcon("newdir"), "&Directory\xE2\x80\xA6\tF7", doMkdir);
    mNew->addAction(appIcon("link"), "&Link\xE2\x80\xA6\tCtrl+Alt+F6", doNewLink);
    mFiles->addSeparator();
    mFiles->addAction(appIcon("open"), "&Open\tF3", doOpen);
    mFiles->addAction(appIcon("edit"), "&Edit\tF4", doEdit);
    mFiles->addSeparator();
    mFiles->addAction(appIcon("copy"), "&Copy (Upload / Download)\xE2\x80\xA6\tF5", doCopy);
    mFiles->addAction(appIcon("duplicate"), "D&uplicate\xE2\x80\xA6\tShift+F5", [&]{ doCopyOrMoveWithin(false); });
    mFiles->addAction(appIcon("move"), "&Move\xE2\x80\xA6\tF6", doMove);
    mFiles->addAction(appIcon("move"), "Move &To\xE2\x80\xA6\tShift+F6", [&]{ doCopyOrMoveWithin(true); });
    mFiles->addAction(appIcon("delete"), "&Delete\tF8", doDelete);
    mFiles->addAction(appIcon("rename"), "&Rename\tF2", doRename);
    mFiles->addAction(appIcon("properties"), "&Properties\tF9", doProps);
    mFiles->addSeparator();
    mFiles->addAction("Copy Pat&h to Clipboard\tCtrl+Shift+C", doCopyPath);
    mFiles->addAction(appIcon("compare"), "Compare F&iles\tCtrl+Shift+D", [&]{ doCompareFiles(); });
    mFiles->addAction("Calculate Directory Si&zes\tShift+Alt+Enter", doCalcSizes);

    auto * mCommands = mb->addMenu("&Commands");
    mCommands->addAction(appIcon("compare"), "&Compare Directories\tShift+F2", doCompareDirs);
    mCommands->addAction(appIcon("sync"), "&Synchronize\xE2\x80\xA6\tCtrl+S", doSync);
    mCommands->addAction(appIcon("upload"), "&Keep Remote Directory up to Date\xE2\x80\xA6\tCtrl+U", doKeepUpToDate);
    actSync->setText("Synchronize &Browsing"); actSync->setIconText("Sync browsing");
    mCommands->addAction(actSync);
    mCommands->addAction(appIcon("find"), "&Find Files\xE2\x80\xA6\tCtrl+Alt+F", [&]{ doFind(QString()); });
    mCommands->addSeparator();
    mCommands->addAction(appIcon("terminal"), "Open &Terminal\tCtrl+T", doOpenTerminal);

    auto * mTabs = mb->addMenu("&Tabs");
    mTabs->addAction(appIcon("newtab"), "&New Tab\xE2\x80\xA6\tCtrl+N", doConnect);
    mTabs->addAction(appIcon("disconnect"), "&Close Tab (Disconnect)\tCtrl+Shift+W", doDisconnect);
    auto * mSites = mTabs->addMenu(appIcon("login"), "&Sites");
    QObject::connect(mSites, &QMenu::aboutToShow, [&, mSites]{
      mSites->clear();
      const QStringList names = savedSiteNames();
      for (const QString & n : names)
        mSites->addAction(appIcon("login"), n, [&, n]{ if (busy()) return; engine::ConnectOptions o; if (loadSite(n, o)) connectWith(o); });
      if (names.isEmpty()) mSites->addAction("(no saved sites)")->setEnabled(false);
      mSites->addSeparator();
      mSites->addAction("Site &Manager\xE2\x80\xA6", doConnect);
    });
    mTabs->addSeparator();
    mTabs->addAction(appIcon("info"), "Server/Protocol &Information\tCtrl+I", doSessionInfo);
    mTabs->addSeparator();
    mTabs->addAction(appIcon("quit"), "&Quit\tF10", doQuit);

    auto * mOptions = mb->addMenu("&Options");
    mOptions->addAction(actHidden);
    actTree->setText("Directory &Tree in Both Panels\tCtrl+Alt+T");
    mOptions->addAction(actTree);
    mOptions->addSeparator();
    mOptions->addAction(appIcon("queue"), "Transfer &Queue\tCtrl+Q", [&]{ queueDock->setVisible(!queueDock->isVisible()); });
    mOptions->addAction(appIcon("log"), "Session &Log\tCtrl+L", [&]{ logDock->setVisible(!logDock->isVisible()); });
    mOptions->addAction("Open &Diagnostics Log File", [&]{
      const QString p = u8(engine::logPath());
      window.statusBar()->showMessage("Diagnostics log: " + p);
#ifdef Q_OS_MACOS
      QProcess::startDetached("open", { "-R", p });   // reveal in Finder
#else
      QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath()));
#endif
    });
    mOptions->addSeparator();
    mOptions->addAction(appIcon("transfer"), "&Transfer Settings\xE2\x80\xA6", [&]{ editTransferSettings(&window); showXferCaption(); });
    actPrefs = mOptions->addAction(appIcon("settings"), "&Preferences\xE2\x80\xA6", showPreferences);

    sideMenu("&Remote", right);

    auto * mHelp = mb->addMenu("&Help");
    mHelp->addAction("&Support FreeSCP (free, open source, no telemetry)\xE2\x80\xA6", [&]{ showWelcome(false); });
    mHelp->addAction("Check for &Updates\xE2\x80\xA6", [&]{ updates::check(&window, true); });
    mHelp->addAction("&About FreeSCP", showAbout);
  }

  // The panels' own toolbar buttons.
  auto panelCommand = [&](const QString & c) {
    if (c == "transfer") doCopy();
    else if (c == "edit") doEdit();
    else if (c == "delete") doDelete();
    else if (c == "props") doProps();
    else if (c == "newfile") doNewFile();
    else if (c == "newdir") doMkdir();
    else if (c == "newlink") doNewLink();
    else if (c == "selectmask") doSelectMask(true);
    else if (c == "unselectmask") doSelectMask(false);
    else if (c == "find") doFind(QString());
    else if (c == "filter") doFilter();
    else if (c == "opendir") doOpenDir();
  };
  left->onCommand = panelCommand; right->onCommand = panelCommand;

  //--- function-key bar buttons ------------------------------------------
  auto addFKey = [&](const QString & icon, const QString & text, const std::function<void()> & fn) {
    auto * b = new QToolButton;                      // flat, like WinSCP's bottom toolbar
    b->setText(text); b->setIcon(appIcon(icon)); b->setIconSize(QSize(16, 16));
    b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); b->setAutoRaise(true);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    b->setFocusPolicy(Qt::NoFocus);
    QObject::connect(b, &QToolButton::clicked, fn);
    fkLay->addWidget(b, 1);
    return b;
  };
  addFKey("rename", "F2 Rename", doRename);
  addFKey("edit", "F4 Edit", doEdit);
  addFKey("copy", "F5 Copy", doCopy);
  addFKey("move", "F6 Move", doMove);
  addFKey("newdir", "F7 Create Directory", doMkdir);
  addFKey("delete", "F8 Delete", doDelete);
  addFKey("properties", "F9 Properties", doProps);
  addFKey("quit", "F10 Quit", doQuit);

  //--- toolbar / shortcuts ------------------------------------------------
  QObject::connect(actConnect, &QAction::triggered, doConnect);
  QObject::connect(newTabBtn, &QToolButton::clicked, doConnect);   // "+" tab = open another session
  QObject::connect(tbSyncDirs, &QAction::triggered, doSync);
  QObject::connect(tbCompare, &QAction::triggered, doCompareDirs);
  QObject::connect(tbTerminal, &QAction::triggered, doOpenTerminal);
  QObject::connect(tbXfer, &QAction::triggered, [&]{ editTransferSettings(&window); showXferCaption(); });
  QObject::connect(tbPrefs, &QAction::triggered, showPreferences);
  QObject::connect(tbQueue, &QAction::toggled, [&](bool v){ queueDock->setVisible(v); });
  QObject::connect(tbLog,   &QAction::toggled, [&](bool v){ logDock->setVisible(v); });
  QObject::connect(queueDock, &QDockWidget::visibilityChanged, [tbQueue](bool v){ tbQueue->setChecked(v); });
  QObject::connect(logDock,   &QDockWidget::visibilityChanged, [tbLog](bool v){ tbLog->setChecked(v); });
  QObject::connect(actDisconnect, &QAction::triggered, doDisconnect);

  auto shortcut = [&](QKeySequence k, const std::function<void()> & fn) {
    auto * a = new QAction(&window); a->setShortcut(k);
    QObject::connect(a, &QAction::triggered, fn); window.addAction(a);
  };
  shortcut(Qt::Key_F2, doRename);
  shortcut(Qt::Key_F3, doOpen);
  shortcut(Qt::Key_F4, doEdit);
  shortcut(Qt::Key_F5, doCopy);
  left->onFileOpen = doEdit; right->onFileOpen = doEdit;   // double-click a file -> internal editor (F3 = OS editor)
  shortcut(Qt::Key_F6, doMove);
  shortcut(Qt::Key_F7, doMkdir);
  shortcut(Qt::Key_F8, doDelete);
  shortcut(QKeySequence::Delete, doDelete);
  shortcut(Qt::Key_F9, doProps);
  shortcut(Qt::Key_F10, doQuit);
  // toggle the session log
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_L);
    QObject::connect(a, &QAction::triggered, [&]{ logDock->setVisible(!logDock->isVisible()); });
    window.addAction(a); }
  { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_Q);
    QObject::connect(a, &QAction::triggered, [&]{ queueDock->setVisible(!queueDock->isVisible()); });
    window.addAction(a); }
  // WinSCP keyboard set. Qt::CTRL auto-maps to Command on macOS and Ctrl on Linux/Windows, so these
  // are faithful on Linux and native on mac with no per-OS code. F-keys are identical everywhere.
  shortcut(Qt::CTRL | Qt::Key_R, [&]{ active->refresh(); });               // refresh
  shortcut(QKeySequence::Refresh, [&]{ active->refresh(); });
  shortcut(Qt::ALT | Qt::Key_Left,  [&]{ active->goBack(); });             // history back
  shortcut(Qt::ALT | Qt::Key_Right, [&]{ active->goForward(); });          // history forward
  shortcut(Qt::ALT | Qt::Key_Up,    [&]{ active->goUp(); });               // parent dir
  shortcut(Qt::CTRL | Qt::Key_N, [&]{ doConnect(); });                     // new session
  shortcut(Qt::CTRL | Qt::Key_T, doOpenTerminal);                          // open ssh terminal
  shortcut(Qt::SHIFT | Qt::Key_F4, doNewFile);                             // new empty file
  shortcut(Qt::CTRL | Qt::Key_Plus,  [&]{ doSelectMask(true); });          // select by mask
  shortcut(Qt::CTRL | Qt::Key_Equal, [&]{ doSelectMask(true); });          // (same key without shift)
  shortcut(Qt::CTRL | Qt::Key_Minus, [&]{ doSelectMask(false); });         // unselect by mask
  shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_C, doCopyPath);                  // copy path/URL
  shortcut(Qt::Key_Insert, [&]{ active->toggleSelectAndAdvance(); });      // Norton select + advance
  shortcut(Qt::Key_F1, showAbout);
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_F, [&]{ doFind(QString()); });        // find files
  shortcut(Qt::SHIFT | Qt::Key_F2, doCompareDirs);                           // compare directories
  shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_D, [&]{ doCompareFiles(); });       // compare files (diff)
  shortcut(Qt::SHIFT | Qt::ALT | Qt::Key_Return, doCalcSizes);               // calculate directory sizes
  shortcut(Qt::SHIFT | Qt::ALT | Qt::Key_Enter, doCalcSizes);
  shortcut(Qt::CTRL | Qt::Key_S, doSync);                                    // synchronize
  shortcut(Qt::CTRL | Qt::Key_B, addBookmark);                               // bookmark the current directory
  shortcut(Qt::CTRL | Qt::Key_U, doKeepUpToDate);                            // keep remote directory up to date
  shortcut(Qt::CTRL | Qt::Key_O, doOpenDir);                                 // open directory / bookmark
  shortcut(Qt::SHIFT | Qt::Key_F5, [&]{ doCopyOrMoveWithin(false); });       // duplicate
  shortcut(Qt::SHIFT | Qt::Key_F6, [&]{ doCopyOrMoveWithin(true); });        // move to
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_F6, doNewLink);                      // new link
  shortcut(Qt::CTRL | Qt::Key_Backslash, [&]{ active->navigate("/"); });     // root directory
  shortcut(Qt::CTRL | Qt::Key_H, [&]{ if (active->onHome) active->onHome(); });   // home directory
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_I, doFilter);                        // filter
  shortcut(Qt::CTRL | Qt::Key_Asterisk, [&]{ active->invertSelection(); });  // invert selection
  shortcut(Qt::CTRL | Qt::Key_I, doSessionInfo);                             // server/protocol information
  shortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_W, doDisconnect);                  // close tab
  shortcut(Qt::CTRL | Qt::ALT | Qt::Key_T, [&]{ actTree->toggle(); });       // directory tree

  left->setActive(true);
  left->navigate(u8(engine::homeDir()));
  right->navigate(u8(engine::homeDir()));
  updateStatuses();

  // Persist window geometry + splitter on quit.
  QObject::connect(&app, &QApplication::aboutToQuit, [&]{
    QSettings s;
    s.setValue("win/geometry", window.saveGeometry());
    s.setValue("win/splitter", splitter->saveState());
  });

  engine::logLine("[ui] " + engine::banner());
  window.statusBar()->showMessage("Ready");
  window.show();

  // Dev affordances (scripted runs).
  auto optionsFromEnv = [&](engine::ConnectOptions & o) {
    QStringList p = qEnvironmentVariable("WINSCP_AUTOCONNECT").split(':');
    o.host = s8(p.value(0)); o.port = p.value(1).toInt(); o.user = s8(p.value(2)); o.password = s8(p.value(3));
    QString pr = p.value(4);
    o.protocol = pr=="scp"?engine::Protocol::Scp : pr=="dav"?engine::Protocol::WebDav
               : pr=="s3"?engine::Protocol::S3 : pr=="ftp"?engine::Protocol::Ftp : engine::Protocol::Sftp;
    o.keyFile = s8(p.value(5));
    QStringList t = qEnvironmentVariable("WINSCP_AC_TUNNEL").split(':', Qt::SkipEmptyParts);
    if (t.size() >= 3) { o.tunnel = true; o.tunnelHost = s8(t[0]); o.tunnelPort = t[1].toInt(); o.tunnelUser = s8(t[2]);
                         o.tunnelPassword = s8(t.value(3)); o.tunnelKeyFile = s8(t.value(4)); }
    QStringList x = qEnvironmentVariable("WINSCP_AC_PROXY").split(':', Qt::SkipEmptyParts);
    if (x.size() >= 3) { o.proxyMethod = x[0]=="socks4" ? 1 : x[0]=="socks5" ? 2 : 3; o.proxyHost = s8(x[1]); o.proxyPort = x[2].toInt();
                         o.proxyUser = s8(x.value(3)); o.proxyPassword = s8(x.value(4)); }
    if (!qEnvironmentVariableIsEmpty("WINSCP_AC_TLS")) { o.tls = true; o.ftpExplicitTls = qEnvironmentVariable("WINSCP_AC_TLS") != "implicit"; }
    if (qEnvironmentVariable("WINSCP_AC_PASSIVE") == "0") o.ftpPassive = false;
    if (qEnvironmentVariable("WINSCP_AC_CRLF") == "1") o.serverCrLf = true;
    if (!qEnvironmentVariableIsEmpty("WINSCP_AC_UTF8")) o.utf8Names = qEnvironmentVariable("WINSCP_AC_UTF8") == "on" ? 1 : 2;
  };
  auto autoConnectFromEnv = [&] {
      // WINSCP_AUTOCONNECT=host:port:user:pass:proto[:keyfile]; optional WINSCP_AC_TUNNEL=host:port:user:pass[:keyfile]
      // and WINSCP_AC_PROXY=socks4|socks5|http:host:port[:user:pass]. WINSCP_AUTOCONNECT_SITE=<name>
      // connects with a saved site instead (exercises the Login dialog's stored settings).
      QString ac = qEnvironmentVariable("WINSCP_AUTOCONNECT");
      QString acSite = qEnvironmentVariable("WINSCP_AUTOCONNECT_SITE");
      if (!ac.isEmpty() || !acSite.isEmpty()) {
        engine::ConnectOptions o;
        bool have = true;
        if (!acSite.isEmpty()) {
          have = loadSite(acSite, o);
          if (!have) { window.statusBar()->showMessage("Connect failed: no saved site named " + acSite);
                       fprintf(stderr, "[autoconnect] no saved site named %s\n", acSite.toUtf8().constData()); }
        } else optionsFromEnv(o);
        engine::ConnectResult r;
        if (have) r = engine::connect(o);
        if (have && r.ok) { sessionOpened(o, r);
          if (!qEnvironmentVariable("WINSCP_DEMO").isEmpty()) {   // marketing screenshot: generic dirs
            left->navigate("/tmp/freescp-demo");
            right->navigate("/config/demo");
          }
          // exercise the BACKGROUND transfer queue end-to-end (multi-file, on the worker thread)
          QString xf = qEnvironmentVariable("WINSCP_AUTOXFER");
          if (!xf.isEmpty()) {
            QDir().mkpath("/tmp/dlq"); left->navigate("/tmp/dlq");
            startTransfer(right, left, xf.split(','), false);
          }
        }
        else if (have)   // make a failed auto-connect visible in the render (verifies the error text the user would see)
        { window.statusBar()->showMessage("Connect failed: " + QString::fromUtf8(r.error.c_str()));
          fprintf(stderr, "[autoconnect] failed: %s\n", r.error.c_str()); }
      }
  };

  // WINSCP_UITEST="cmd;cmd;..." drives the UI without a human, after the optional auto-connect.
  // Commands are registered in uiCmd by the features they exercise; the generic ones are below.
  // Arguments are separated by '|'. Each command runs to completion before the next starts.
  uiCmd["wait"] = [&](const QStringList & a) { QEventLoop l; QTimer::singleShot(a.value(0).toInt(), &l, &QEventLoop::quit); l.exec(); };
  uiCmd["waitidle"] = [&](const QStringList &) {   // until the background transfer batch has finished
    QEventLoop l; QTimer tm; QObject::connect(&tm, &QTimer::timeout, [&]{ if (!gTransferRunning.load()) l.quit(); });
    tm.start(50); l.exec();
    QEventLoop l2; QTimer::singleShot(150, &l2, &QEventLoop::quit); l2.exec();   // let the queued completion handlers run
  };
  uiCmd["shot"] = [&](const QStringList & a) { window.grab().save(a.value(0)); };
  uiCmd["login"] = [&](const QStringList &) { doConnect(); };
  // connect|host:port:user:pass:proto : open ANOTHER session (a new tab);  tab|<index> : switch;  closetab|<index>
  uiCmd["connect"] = [&](const QStringList & a) {
    QStringList p = a.value(0).split(':');
    engine::ConnectOptions o;
    o.host = s8(p.value(0)); o.port = p.value(1).toInt(); o.user = s8(p.value(2)); o.password = s8(p.value(3));
    QString pr = p.value(4);
    o.protocol = pr=="scp"?engine::Protocol::Scp : pr=="dav"?engine::Protocol::WebDav
               : pr=="s3"?engine::Protocol::S3 : pr=="ftp"?engine::Protocol::Ftp : engine::Protocol::Sftp;
    connectWith(o);
  };
  uiCmd["tab"] = [&](const QStringList & a) { tabBar->setCurrentIndex(a.value(0).toInt()); };
  uiCmd["closetab"] = [&](const QStringList & a) { closeTab(a.value(0).toInt()); };
  uiCmd["tabs"] = [&](const QStringList &) {
    fprintf(stdout, "[tabs] count=%d current=%d engine-active=%d\n", tabBar->count(), tabBar->currentIndex(), engine::activeSession()); fflush(stdout);
  };
  uiCmd["find"] = [&](const QStringList & a) {          // find|left|*.txt
    (a.value(0) == "right" ? right : left)->onActivated(); doFind(a.value(1, "*"));
  };
  uiCmd["dirsizes"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->onActivated(); doCalcSizes(); };
  uiCmd["comparedirs"] = [&](const QStringList & a) {   // comparedirs[|time|size]
    if (a.size() >= 1 && !a.value(0).isEmpty()) { gCompareByTime = a.contains("time"); gCompareBySize = a.contains("size"); }
    doCompareDirs();
  };
  uiCmd["comparefiles"] = [&](const QStringList & a) {  // comparefiles|left  (uses that panel's selection)
    (a.value(0) == "right" ? right : left)->onActivated();
    QString r = doCompareFiles();
    fprintf(stdout, "[compare] %s\n", r.toUtf8().constData()); fflush(stdout);
  };
  uiCmd["checkupdate"] = [&](const QStringList &) { updates::check(&window, true); };   // Help > Check for Updates
  uiCmd["prefs"] = [&](const QStringList &) { if (actPrefs) actPrefs->trigger(); };
  uiCmd["xfersettings"] = [&](const QStringList &) { editTransferSettings(&window); };   // the dialog (render / accept)
  uiCmd["printxfer"] = [&](const QStringList &) {
    fprintf(stdout, "[xfer] %s\n", transferSummary(engine::transferSettings()).toUtf8().constData()); fflush(stdout);
  };
  uiCmd["setxfer"] = [&](const QStringList & a) {   // setxfer|mode=text|speed=300|... (persisted, like the dialog)
    engine::TransferSettings x = engine::transferSettings();
    for (const QString & kv : a) {
      const QString k = kv.section('=', 0, 0), v = kv.section('=', 1);
      if (k == "mode") x.mode = v == "text" ? 1 : v == "auto" ? 2 : 0;
      else if (k == "textmask") x.textMask = s8(v);
      else if (k == "time") x.preserveTime = v != "0";
      else if (k == "rights") { x.setRights = !v.isEmpty(); if (!v.isEmpty()) x.rightsOctal = s8(v); }
      else if (k == "case") x.fileNameCase = v == "upper" ? 1 : v == "lower" ? 2 : 0;
      else if (k == "resume") x.resume = v == "on" ? 0 : v == "off" ? 2 : 1;
      else if (k == "threshold") x.resumeThresholdKB = v.toLongLong();
      else if (k == "speed") x.speedLimitKB = v.toInt();
      else if (k == "mask") x.fileMask = s8(QString(v).replace('^', '|'));
      else if (k == "newer") x.newerOnly = v != "0";
      else if (k == "hidden") x.excludeHidden = v == "exclude";
      else if (k == "defaults") x = engine::TransferSettings();
    }
    QString err;
    if (!validateTransferSettings(x, &err)) { fprintf(stderr, "[uitest] invalid transfer settings: %s\n", err.toUtf8().constData()); return; }
    applyTransferSettings(x);
  };
  // xfer|right|left : transfer the source panel's selection to the other panel (no dialog)
  uiCmd["xfer"] = [&](const QStringList & a) {
    FilePanel * from = (a.value(0) == "right" ? right : left), * to = (from == right ? left : right);
    startTransfer(from, to, from->selectedItems(), false);
  };
  uiCmd["copy"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->onActivated(); doCopy(); };   // F5 with its dialog
  // The Login dialog pre-filled from the WINSCP_AUTOCONNECT / WINSCP_AC_* variables; connects if accepted.
  uiCmd["loginpreset"] = [&](const QStringList &) {
    engine::ConnectOptions o; optionsFromEnv(o);
    LoginParams lp = showLoginDialog(&window, &o);
    if (lp.ok) connectWith(lp.o);
  };
  // --- panels: layout, status, filter, selection ---
  auto side = [&](const QString & n) -> FilePanel * { return n == "right" ? right : left; };
  uiCmd["columns"] = [&](const QStringList & a) {     // columns|left : captions + every cell as shown
    fprintf(stdout, "[columns %s]\n", a.value(0).toUtf8().constData());
    for (const QString & line : side(a.value(0))->dumpColumns()) fprintf(stdout, "  %s\n", line.toUtf8().constData());
    fflush(stdout);
  };
  uiCmd["status"] = [&](const QStringList & a) {      // status|left : the panel's status line
    FilePanel * pn = side(a.value(0));
    fprintf(stdout, "[status %s] %s || %s\n", a.value(0).toUtf8().constData(), pn->statusText().toUtf8().constData(), pn->statusRightText().toUtf8().constData());
    fflush(stdout);
  };
  uiCmd["mainstatus"] = [&](const QStringList &) {    // the main status bar's session part
    fprintf(stdout, "[mainstatus] protocol='%s' encrypted=%d time='%s'\n", sbProto->text().toUtf8().constData(), sessEncrypted ? 1 : 0, sbTime->text().toUtf8().constData());
    fflush(stdout);
  };
  uiCmd["filter"] = [&](const QStringList & a) {      // filter|left|*.txt  (no mask = remove; "dialog" = through the dialog)
    FilePanel * pn = side(a.value(0)); pn->onActivated();
    if (a.value(1) == "dialog") { doFilter(); return; }
    QString err;
    if (!pn->setFilter(QString(a.value(1)).replace('^', '|'), &err)) { fprintf(stdout, "[filter] invalid: %s\n", err.toUtf8().constData()); fflush(stdout); }
  };
  uiCmd["hidden"] = [&](const QStringList & a) { actHidden->setChecked(a.value(0) != "0"); };   // hidden|0  /  hidden|1
  uiCmd["selectall"] = [&](const QStringList & a) { side(a.value(0))->selectAll(); };
  uiCmd["invert"] = [&](const QStringList & a) { side(a.value(0))->invertSelection(); };
  uiCmd["clearsel"] = [&](const QStringList & a) { side(a.value(0))->clearSelection(); };
  uiCmd["selectmask"] = [&](const QStringList & a) {  // selectmask|left|*.txt|dirs   (mask semantics of the Select dialog)
    FilePanel * pn = side(a.value(0));
    const bool ok = pn->selectByMask(QString(a.value(1)).replace('^', '|'), true, a.value(2) == "dirs");
    if (!ok) { fprintf(stdout, "[selectmask] invalid mask\n"); fflush(stdout); }
  };
  uiCmd["selectdialog"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); doSelectMask(a.value(1) != "unselect"); };
  uiCmd["sort"] = [&](const QStringList & a) { side(a.value(0))->sortByColumn(a.value(1).toInt()); };   // sort|left|<column index>
  uiCmd["recent"] = [&](const QStringList & a) {      // recent|left : recent directories + the drop-down's paths
    FilePanel * pn = side(a.value(0));
    fprintf(stdout, "[recent %s] %s\n[dircombo %s] %s\n", a.value(0).toUtf8().constData(), pn->recentDirs().join(" , ").toUtf8().constData(),
            a.value(0).toUtf8().constData(), pn->dirComboItems().join(" , ").toUtf8().constData());
    fflush(stdout);
  };
  uiCmd["up"] = [&](const QStringList & a) { side(a.value(0))->goUp(); };
  uiCmd["back"] = [&](const QStringList & a) { side(a.value(0))->goBack(); };
  uiCmd["forward"] = [&](const QStringList & a) { side(a.value(0))->goForward(); };
  uiCmd["opendir"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); doOpenDir(); };
  uiCmd["bookmark"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); addBookmark(); };
  uiCmd["bookmarks"] = [&](const QStringList &) { fprintf(stdout, "[bookmarks] %s\n", bookmarkList().join(" , ").toUtf8().constData()); fflush(stdout); };
  // --- operations ---
  uiCmd["props"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); doProps(); };          // the Properties dialog
  uiCmd["newlink"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); doNewLink(); };      // the Add Link dialog
  uiCmd["mklink"] = [&](const QStringList & a) {      // mklink|right|name|target[|hard]
    createLink(side(a.value(0)), a.value(1), a.value(2), a.value(3) != "hard");
  };
  uiCmd["duplicate"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); doCopyOrMoveWithin(false); };   // the dialogs
  uiCmd["moveto"] = [&](const QStringList & a) { side(a.value(0))->onActivated(); doCopyOrMoveWithin(true); };
  uiCmd["sessioninfo"] = [&](const QStringList & a) {  // sessioninfo[|dialog]
    if (a.value(0) == "dialog") { doSessionInfo(); return; }
    fprintf(stdout, "[sessioninfo]\n%s\n", sessionInfoText(engine::sessionInformation()).toUtf8().constData()); fflush(stdout);
  };
  uiCmd["mkdir"] = [&](const QStringList & a) {       // mkdir|right|name   (no dialog)
    FilePanel * pn = side(a.value(0)); std::string err;
    const bool ok = pn->isRemote() ? engine::remoteMakeDir(s8(a.value(1)), &err) : engine::localMakeDir(s8(pn->path()), s8(a.value(1)), &err);
    pn->refresh();
    fprintf(stdout, "[mkdir] %s %s\n", ok ? "ok" : "FAILED", err.c_str()); fflush(stdout);
  };
  uiCmd["delete"] = [&](const QStringList & a) {      // delete|right : F8 on the selection, confirmed
    side(a.value(0))->onActivated();
    const bool confirm = gConfirmDelete; gConfirmDelete = false; doDelete(); gConfirmDelete = confirm;
  };
  uiCmd["icons"] = [&](const QStringList & a) {       // icons|file.png : every own icon on one sheet, for review
    const QStringList names = appIconNames();
    const int cols = 8, cell = 112;
    QPixmap sheet(cols * cell, ((names.size() + cols - 1) / cols) * 64); sheet.fill(Qt::white);
    QPainter p(&sheet);
    for (int i = 0; i < names.size(); ++i)
    {
      const int x = (i % cols) * cell, y = (i / cols) * 64;
      appIcon(names[i]).paint(&p, QRect(x + 8, y + 6, 32, 32));
      appIcon(names[i]).paint(&p, QRect(x + 48, y + 14, 16, 16));
      p.setPen(Qt::black); p.drawText(QRect(x + 2, y + 42, cell - 4, 16), Qt::AlignLeft, names[i]);
    }
    p.end(); sheet.save(a.value(0));
    int blank = 0;      // an icon that draws nothing at toolbar size
    for (const QString & n : names)
    {
      const QImage im = appIcon(n).pixmap(16, 16).toImage().convertToFormat(QImage::Format_ARGB32);
      bool any = false;
      for (int y = 0; y < im.height() && !any; ++y) for (int x = 0; x < im.width(); ++x) if (qAlpha(im.pixel(x, y)) > 40) { any = true; break; }
      if (!any) ++blank;
    }
    fprintf(stdout, "[icons] %d blank=%d\n", (int)names.size(), blank); fflush(stdout);
  };
  uiCmd["keepuptodate"] = [&](const QStringList &) { doKeepUpToDate(); };   // with WINSCP_KEEP_MS: watch for that long
  uiCmd["dropsession"] = [&](const QStringList &) { engine::debugDropConnection(); };   // the connection is gone, the tab stays
  uiCmd["nav"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->navigate(a.value(1)); };
  uiCmd["activate"] = [&](const QStringList & a) { (a.value(0) == "right" ? right : left)->onActivated(); };
  uiCmd["select"] = [&](const QStringList & a) {   // select|left|*.txt  (replaces the selection)
    FilePanel * pn = (a.value(0) == "right" ? right : left);
    pn->view()->clearSelection(); pn->selectByMask(a.value(1, "*"), true);
  };
  uiCmd["print"] = [&](const QStringList & a) {    // print|left : the panel's listing, for assertions
    FilePanel * pn = (a.value(0) == "right" ? right : left);
    fprintf(stdout, "[panel %s] %s\n", a.value(0).toUtf8().constData(), pn->path().toUtf8().constData());
    for (const QString & line : pn->dump()) fprintf(stdout, "  %s\n", line.toUtf8().constData());
    fflush(stdout);
  };
  const QStringList uiTest = qEnvironmentVariable("WINSCP_UITEST").split(';', Qt::SkipEmptyParts);
  if (!uiTest.isEmpty())
    QMetaObject::invokeMethod(&window, [&, uiTest]{
      autoConnectFromEnv();
      for (const QString & step : uiTest)
      {
        QStringList parts = step.split('|');
        const QString name = parts.takeFirst().trimmed();
        if (name == "quit") break;
        auto it = uiCmd.find(name);
        if (it == uiCmd.end()) { fprintf(stderr, "[uitest] unknown command: %s\n", name.toUtf8().constData()); continue; }
        fprintf(stderr, "[uitest] %s\n", step.toUtf8().constData());
        it->second(parts);
        QApplication::processEvents();
      }
      app.quit();
    }, Qt::QueuedConnection);
  // WINSCP_SHOT=path renders the main window to a PNG and quits (UI verification).
  else if (!qEnvironmentVariableIsEmpty("WINSCP_SHOT"))
    QMetaObject::invokeMethod(&window, [&]{
      autoConnectFromEnv();
      // Grab after a delay so background transfers (detached worker) complete first.
      QTimer::singleShot(qEnvironmentVariableIsEmpty("WINSCP_AUTOXFER") ? 100 : 3000, [&]{
        window.grab().save(qEnvironmentVariable("WINSCP_SHOT"));
        app.quit();
      });
    }, Qt::QueuedConnection);

  // First launch: say once that the app is free / open source / telemetry-free and how to support it.
  // Never in headless or scripted runs (offscreen platform, render hooks, smoke tests).
  const bool scripted = uiScripted() || !qEnvironmentVariableIsEmpty("WINSCP_NO_AUTOLOGIN");
  const bool welcomeDue = !qEnvironmentVariableIsEmpty("WINSCP_WELCOME_SHOT")
                          || (!scripted && !QSettings().value("ui/welcomeShown", false).toBool());

  // Offer the Login dialog on startup (like WinSCP), unless launched headless for a smoke test.
  QMetaObject::invokeMethod(&window, [&, welcomeDue]{
    if (welcomeDue)
    {
      showWelcome(true);
      if (!qEnvironmentVariableIsEmpty("WINSCP_WELCOME_SHOT")) { app.quit(); return; }
      QSettings().setValue("ui/welcomeShown", true);
    }
    if (qEnvironmentVariableIsEmpty("WINSCP_NO_AUTOLOGIN") && qEnvironmentVariableIsEmpty("WINSCP_UITEST")) doConnect();
  }, Qt::QueuedConnection);

  // A newer version? (at most once a day, optional - see updatecheck.h). After the first-launch note.
  if (!welcomeDue) QTimer::singleShot(1200, &window, [&]{ updates::check(&window, false); });

  int rc = app.exec();
  // Close sessions while the engine is alive (see engine::shutdown). If a transfer is still running
  // on the worker thread, skip all destructors rather than pull the engine from under it.
  // A background job (transfer, size calculation, search) still running holds references to the
  // window: leave without running destructors rather than let it touch freed objects.
  if (gTransferRunning.load()) { fflush(nullptr); std::_Exit(rc); }
  if (!engine::shutdown(3000)) { fflush(nullptr); std::_Exit(rc); }
  return rc;
}
