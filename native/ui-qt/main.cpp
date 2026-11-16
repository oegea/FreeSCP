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

#ifndef FREESCP_VERSION
#define FREESCP_VERSION "dev"
#endif

static bool gShowHidden = true;        // WinSCP "Show hidden files" toggle (dotfiles)
static bool gConfirmDelete = true;     // confirm before deleting
static bool gConfirmOverwrite = true;  // confirm before overwriting (stored; enforcement TBD)
static int  gParallelMax = 2;          // max concurrent connections for queue parallelism (1-4)
static bool gAltColors = true;         // alternating row colors in panels
static bool gCompareByTime = true;     // Compare Directories criteria (WinSCP defaults: time on, size off)
static bool gCompareBySize = false;
static std::atomic<bool> gTransferRunning{false};  // a background transfer batch is in flight

//===========================================================================
// Properties dialog — WinSCP-style permissions grid (Owner/Group/Others x R/W/X) with a synced
// octal field. Returns the chosen octal string, or empty if cancelled.
//===========================================================================
static QString showPropertiesDialog(QWidget * parent, const QString & name, const QString & curOctal,
                                    const QString & info, bool isDir, bool * recursiveOut)
{
  QDialog dlg(parent);
  dlg.setWindowTitle(name + " — Properties");

  auto * outer = new QVBoxLayout(&dlg);
  if (!info.isEmpty()) { auto * il = new QLabel(info); il->setTextInteractionFlags(Qt::TextSelectableByMouse); outer->addWidget(il); }
  auto * grp = new QGroupBox("Permissions");
  auto * g = new QGridLayout(grp);
  g->addWidget(new QLabel("R"), 0, 1, Qt::AlignHCenter);
  g->addWidget(new QLabel("W"), 0, 2, Qt::AlignHCenter);
  g->addWidget(new QLabel("X"), 0, 3, Qt::AlignHCenter);
  const char * rowNames[3] = { "Owner", "Group", "Others" };
  QCheckBox * cb[9];
  for (int row = 0; row < 3; ++row)
  {
    g->addWidget(new QLabel(rowNames[row]), row + 1, 0);
    for (int col = 0; col < 3; ++col)
    {
      cb[row * 3 + col] = new QCheckBox;
      g->addWidget(cb[row * 3 + col], row + 1, col + 1, Qt::AlignHCenter);
    }
  }
  outer->addWidget(grp);

  auto * octRow = new QHBoxLayout;
  octRow->addWidget(new QLabel("Octal:"));
  auto * octEdit = new QLineEdit;
  octRow->addWidget(octEdit);
  outer->addLayout(octRow);

  // seed from curOctal (last 3 digits)
  int initial = curOctal.right(3).toInt(nullptr, 8);
  for (int i = 0; i < 9; ++i) cb[i]->setChecked((initial >> (8 - i)) & 1);

  bool guard = false;
  auto syncOctalFromBoxes = [&] {
    if (guard) return; guard = true;
    int v = 0; for (int i = 0; i < 9; ++i) if (cb[i]->isChecked()) v |= (1 << (8 - i));
    octEdit->setText(QString("%1").arg(v, 3, 8, QChar('0')));
    guard = false;
  };
  auto syncBoxesFromOctal = [&] {
    if (guard) return; guard = true;
    int v = octEdit->text().toInt(nullptr, 8);
    for (int i = 0; i < 9; ++i) cb[i]->setChecked((v >> (8 - i)) & 1);
    guard = false;
  };
  for (int i = 0; i < 9; ++i) QObject::connect(cb[i], &QCheckBox::toggled, syncOctalFromBoxes);
  QObject::connect(octEdit, &QLineEdit::textEdited, syncBoxesFromOctal);
  syncOctalFromBoxes();

  auto * cbRec = new QCheckBox("Set recursively (apply to directory contents)");
  cbRec->setVisible(isDir);
  outer->addWidget(cbRec);

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  outer->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  QObject::connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return QString();
  if (recursiveOut) *recursiveOut = cbRec->isChecked();
  return octEdit->text();
}

//===========================================================================
// File panel — a WinSCP-style pane: address bar + file list (Name/Size/Changed/Rights/Owner)
// + a per-panel status line. Local or remote, both via enginebridge.
//===========================================================================
class FilePanel : public QWidget
{
public:
  std::function<void()> onActivated;
  std::function<void()> onStatusChanged;
  std::function<void()> onHome;
  std::function<void()> onFileOpen;   // file (not dir) activated
  std::function<void(const QString &)> onEnterDir;  // user entered a subdir (for sync browsing)
  std::function<void()> onLeaveDir;                 // user went up (for sync browsing)
  std::function<void()> onSwitchPanel;              // Tab pressed -> focus the other panel
  std::function<void(FilePanel *, const QStringList &)> onDropFiles;  // files dropped from another panel
  std::function<void(FilePanel *, const QStringList &)> onDropExternal; // local files dropped from Finder
  std::function<void(const QString &)> onListError;   // a remote directory could not be opened/read
  static FilePanel * s_dragSource;                  // panel a drag originated from

  // Mirror operations (used by synchronized browsing; navigate directly, no callbacks -> no loop).
  void enterSubdir(const QString & name)
  {
    for (const auto & e : FEntries)
      if (e.isDir && !e.isParent && u8(e.name) == name) { navigate(u8(engine::joinPath(s8(FPath), e.name))); return; }
  }
  void upOne() { navigate(u8(engine::parentDir(s8(FPath)))); }

  //--- directory tree (lazy, click-to-navigate) ---
  void setTreeVisible(bool v) { FTree->setVisible(v); if (v && FTreeModel->rowCount() == 0) treeReload(); }
  bool treeVisible() const { return FTree->isVisible(); }
  void treeReload() {
    FTreePopulating = true;
    FTreeModel->clear();
    auto * root = makeTreeNode("/", "/");
    FTreeModel->appendRow(root);
    populateTreeNode(root);
    FTree->expand(root->index());
    FTreePopulating = false;
  }
  QStandardItem * makeTreeNode(const QString & label, const QString & path) {
    auto * it = new QStandardItem(qApp->style()->standardIcon(QStyle::SP_DirIcon), label);
    it->setData(path, Qt::UserRole + 1);     // full path
    it->setData(false, Qt::UserRole + 2);    // populated?
    return it;
  }
  void populateTreeNode(QStandardItem * node) {
    if (node->data(Qt::UserRole + 2).toBool()) return;
    node->setData(true, Qt::UserRole + 2);
    node->removeRows(0, node->rowCount());   // drop the placeholder
    QString path = node->data(Qt::UserRole + 1).toString();
    for (const QString & name : subdirsOf(path)) {
      auto * child = makeTreeNode(name, u8(engine::joinPath(s8(path), s8(name))));
      child->appendRow(new QStandardItem());  // placeholder -> shows the expand arrow
      node->appendRow(child);
    }
  }
  void treeExpand(const QModelIndex & ix) { if (auto * n = FTreeModel->itemFromIndex(ix)) populateTreeNode(n); }
  QStringList subdirsOf(const QString & path) {
    QStringList dirs;
    if (FRemote && gTransferRunning.load()) return dirs;   // don't disturb the session mid-transfer
    auto entries = FRemote ? engine::listRemoteDir(s8(path)) : engine::listLocalDir(s8(path));
    for (const auto & e : entries) if (e.isDir && !e.isParent) dirs << u8(e.name);
    if (FRemote && path != FPath) engine::listRemoteDir(s8(FPath));  // restore session CWD + the panel's listing
    return dirs;
  }

  explicit FilePanel(QWidget * parent = nullptr) : QWidget(parent)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Title bar (host/protocol or "Local").
    FHeader = new QLabel("Local");
    FHeader->setStyleSheet("padding:4px 6px; font-weight:bold; background:palette(midlight);");

    // Address bar: path + small Up/Refresh buttons (WinSCP-ish).
    auto * addr = new QWidget;
    auto * addrLay = new QHBoxLayout(addr);
    addrLay->setContentsMargins(2, 2, 2, 2);
    addrLay->setSpacing(2);
    auto * backBtn = new QToolButton; backBtn->setText("\xE2\x97\x80"); backBtn->setToolTip("Back");
    auto * fwdBtn  = new QToolButton; fwdBtn->setText("\xE2\x96\xB6");  fwdBtn->setToolTip("Forward");
    auto * upBtn = new QToolButton; upBtn->setText("\xE2\xAC\x86");  upBtn->setToolTip("Parent directory");
    auto * homeBtn = new QToolButton; homeBtn->setText("\xF0\x9F\x8F\xA0"); homeBtn->setToolTip("Home");
    auto * rfBtn = new QToolButton; rfBtn->setText("\xE2\x9F\xB3"); rfBtn->setToolTip("Refresh");
    FPathEdit = new QLineEdit;
    addrLay->addWidget(backBtn);
    addrLay->addWidget(fwdBtn);
    addrLay->addWidget(upBtn);
    addrLay->addWidget(homeBtn);
    addrLay->addWidget(FPathEdit, 1);
    addrLay->addWidget(rfBtn);

    FView = new QTableView;
    FModel = new QStandardItemModel(this);
    FView->setModel(FModel);
    FView->setSelectionBehavior(QAbstractItemView::SelectRows);
    FView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    FView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FView->setSortingEnabled(false);
    FView->setShowGrid(false);
    FView->setAlternatingRowColors(true);
    FView->verticalHeader()->setVisible(false);
    FView->verticalHeader()->setDefaultSectionSize(20);
    FView->installEventFilter(this);
    FView->viewport()->installEventFilter(this);     // drag start + drop target
    FView->viewport()->setAcceptDrops(true);

    FStatus = new QLabel;
    FStatus->setStyleSheet("padding:2px 6px; background:palette(window); border-top:1px solid palette(mid);");

    // Optional directory tree (WinSCP-style), hidden by default; lazy-loaded, click navigates.
    FTree = new QTreeView; FTreeModel = new QStandardItemModel(this);
    FTree->setModel(FTreeModel); FTree->setHeaderHidden(true); FTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FTree->hide();
    QObject::connect(FTree, &QTreeView::expanded, this, [this](const QModelIndex & ix){ treeExpand(ix); });
    QObject::connect(FTree, &QTreeView::clicked, this, [this](const QModelIndex & ix){
      if (FTreePopulating) return;
      QString p = ix.data(Qt::UserRole + 1).toString();
      if (!p.isEmpty() && p != FPath) navigate(p);
    });
    auto * split = new QSplitter(Qt::Horizontal);
    split->addWidget(FTree); split->addWidget(FView);
    split->setStretchFactor(0, 0); split->setStretchFactor(1, 1);
    split->setSizes({ 200, 600 });

    layout->addWidget(FHeader);
    layout->addWidget(addr);
    layout->addWidget(split, 1);
    layout->addWidget(FStatus);

    QObject::connect(FView, &QTableView::doubleClicked,
                     [this](const QModelIndex & ix) { activate(ix.row()); });
    QObject::connect(FPathEdit, &QLineEdit::returnPressed,
                     [this] { navigate(FPathEdit->text()); });
    QObject::connect(upBtn, &QToolButton::clicked, [this] { goUp(); });
    QObject::connect(rfBtn, &QToolButton::clicked, [this] { refresh(); });
    QObject::connect(backBtn, &QToolButton::clicked, [this] { goBack(); });
    QObject::connect(fwdBtn, &QToolButton::clicked, [this] { goForward(); });
    QObject::connect(homeBtn, &QToolButton::clicked, [this] { if (onHome) onHome(); });
    QObject::connect(FView->selectionModel(), &QItemSelectionModel::selectionChanged,
                     [this] { if (onStatusChanged) onStatusChanged(); });
    // Columns + header set up ONCE here (not per-render) so they're user-resizable and widths persist.
    FModel->setColumnCount(5);
    FModel->setHorizontalHeaderLabels({ "Name", "Size", "Changed", "Rights", "Owner" });
    auto * hh = FView->horizontalHeader();
    hh->setSectionsClickable(true);
    hh->setSectionsMovable(false);
    hh->setStretchLastSection(true);                 // last column fills leftover space
    for (int c = 0; c < 5; ++c) hh->setSectionResizeMode(c, QHeaderView::Interactive);  // all resizable
    FView->setColumnWidth(0, 240); FView->setColumnWidth(1, 90);
    FView->setColumnWidth(2, 150); FView->setColumnWidth(3, 95);
    QObject::connect(hh, &QHeaderView::sectionClicked, [this](int col) { sortByColumn(col); });
  }

  QString path() const { return FPath; }
  QTableView * view() const { return FView; }
  bool isRemote() const { return FRemote; }
  // Size (bytes) of a listed entry by name, for the transfer queue; 0 if unknown/dir.
  qint64 sizeOf(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (e.name == n) return e.isDir ? FDirSizes.value(name, 0) : (qint64)e.size;
    return 0;
  }
  // The current listing (for Compare Directories).
  const std::vector<engine::DirEntry> & entries() const { return FEntries; }
  // A calculated directory size ("Calculate Directory Sizes"): shown in the Size column until the
  // panel moves to another directory. Ignored if the panel is no longer showing `forPath`.
  void setDirSize(const QString & forPath, const QString & name, qint64 bytes) {
    if (forPath != FPath) return;
    FDirSizes.insert(name, bytes);
    for (int r = 0; r < (int)FEntries.size(); ++r)
      if (FEntries[r].isDir && !FEntries[r].isParent && u8(FEntries[r].name) == name && FModel->item(r, 1))
        FModel->item(r, 1)->setText(u8(engine::formatSize(bytes)));
    if (onStatusChanged) onStatusChanged();
  }
  bool hasDirSize(const QString & name) const { return FDirSizes.contains(name); }
  // Replace the selection with exactly these entries; the first one becomes the current row.
  void selectNames(const QStringList & names) {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return;
    sm->clearSelection();
    int first = -1;
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent || !names.contains(u8(FEntries[r].name))) continue;
      sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, FModel->columnCount() - 1)), QItemSelectionModel::Select);
      if (first < 0) first = r;
    }
    if (first >= 0) { sm->setCurrentIndex(FModel->index(first, 0), QItemSelectionModel::NoUpdate); FView->scrollTo(FModel->index(first, 0)); }
    if (onStatusChanged) onStatusChanged();
  }
  // Entry metadata by name (nullptr if not in the current listing).
  const engine::DirEntry * entryNamed(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (e.name == n) return &e;
    return nullptr;
  }
  // Does the current listing already contain this name (for overwrite confirmation)?
  bool contains(const QString & name) const {
    std::string n = s8(name);
    for (const auto & e : FEntries) if (!e.isParent && e.name == n) return true;
    return false;
  }
  void setLocal() { FRemote = false; FHeader->setText("\xF0\x9F\x92\xBB  Local"); }
  void setRemote(const QString & label) { FRemote = true; FHeader->setText("\xF0\x9F\x8C\x90  " + label); }

  void setActive(bool a)
  {
    FActive = a;
    FHeader->setStyleSheet(QString("padding:4px 6px; font-weight:bold; color:%1; background:%2;")
                             .arg(a ? "palette(highlighted-text)" : "palette(window-text)")
                             .arg(a ? "palette(highlight)" : "palette(midlight)"));
  }

  void goBack()    { if (FHistIdx > 0) { FNav = true; navigate(FHistory[--FHistIdx]); FNav = false; } }
  void goForward() { if (FHistIdx + 1 < FHistory.size()) { FNav = true; navigate(FHistory[++FHistIdx]); FNav = false; } }

  void navigate(const QString & path)
  {
    if (FRemote && gTransferRunning.load()) return;   // engine busy with a transfer batch
    if (!FNav)   // record history (truncate any forward entries)
    {
      while (FHistory.size() > FHistIdx + 1) FHistory.removeLast();
      if (FHistory.isEmpty() || FHistory.last() != path) { FHistory.append(path); FHistIdx = FHistory.size() - 1; }
    }
    std::string listError;
    std::vector<engine::DirEntry> entries = FRemote ? engine::listRemoteDir(s8(path), &listError) : engine::listLocalDir(s8(path));
    if (FRemote && !listError.empty())
    {
      // WinSCP reports the error and stays where it was; it never shows an unreadable directory as
      // an empty one. Go back to the directory the panel was showing (the session may already have
      // changed into the unreadable one).
      QString prev = FPath;
      if (!FHistory.isEmpty() && FHistory.last() == path && !FNav)
      { FHistory.removeLast(); FHistIdx = FHistory.size() - 1; }
      if (!prev.isEmpty() && prev != path)
      {
        std::string again;
        std::vector<engine::DirEntry> back = engine::listRemoteDir(s8(prev), &again);
        if (again.empty()) entries = back; else { entries.clear(); }
      }
      else entries.clear();
      FPathEdit->setText(prev);
      if (onListError) onListError(u8(listError));
      if (prev.isEmpty()) FPath = path;      // first listing of the session failed: nothing to go back to
    }
    else
    {
      FPath = path;
      FPathEdit->setText(path);
    }
    if (FPath != FListedPath) FDirSizes.clear();   // calculated sizes belong to the directory they were made for
    FListedPath = FPath;
    FEntries = entries;
    if (!gShowHidden)
      FEntries.erase(std::remove_if(FEntries.begin(), FEntries.end(),
        [](const engine::DirEntry & e) { return !e.isParent && !e.name.empty() && e.name[0] == '.'; }),
        FEntries.end());
    FDirs = 0; FFiles = 0;
    for (const auto & e : FEntries) { if (e.isParent) continue; if (e.isDir) ++FDirs; else ++FFiles; }
    sortEntries();
    renderEntries();
    if (onStatusChanged) onStatusChanged();
  }

  // Sort FEntries by the active column ("." parent always first), then dirs-first within the
  // chosen key (WinSCP groups folders), respecting ascending/descending.
  void sortEntries()
  {
    std::stable_sort(FEntries.begin(), FEntries.end(), [this](const engine::DirEntry & a, const engine::DirEntry & b) {
      if (a.isParent != b.isParent) return a.isParent > b.isParent;     // ".." first
      if (a.isDir != b.isDir) return a.isDir > b.isDir;                 // dirs before files
      int c = 0;
      switch (FSortCol)
      {
        case 1: { qint64 sa = a.isDir ? FDirSizes.value(u8(a.name), 0) : (qint64)a.size,   // Size (calculated, for directories)
                         sb = b.isDir ? FDirSizes.value(u8(b.name), 0) : (qint64)b.size;
                  c = (sa < sb) ? -1 : (sa > sb) ? 1 : 0; break; }
        case 2: c = QString::compare(u8(a.modified), u8(b.modified)); break;     // Changed
        case 3: c = QString::compare(u8(a.rights), u8(b.rights)); break;         // Rights
        case 4: c = QString::compare(u8(a.owner), u8(b.owner)); break;           // Owner
        default: c = QString::compare(u8(a.name), u8(b.name), Qt::CaseInsensitive); break;  // Name
      }
      return FSortAsc ? (c < 0) : (c > 0);
    });
  }

  void renderEntries()
  {
    // Remove only the rows (NOT clear()) so the columns, header sections and the user's column
    // widths survive a navigation. Sort arrow goes on the header text via setHeaderData.
    if (FModel->rowCount() > 0) FModel->removeRows(0, FModel->rowCount());
    static const char * heads[5] = { "Name", "Size", "Changed", "Rights", "Owner" };
    for (int i = 0; i < 5; ++i) {
      QString h = heads[i]; if (i == FSortCol) h += FSortAsc ? "  \xE2\x96\xB2" : "  \xE2\x96\xBC";
      FModel->setHeaderData(i, Qt::Horizontal, h);
    }
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      auto * nameItem = new QStandardItem(u8(e.name));
      nameItem->setIcon(iconFor(e));               // native folder / file-type icon
      row << nameItem;
      auto * sz = new QStandardItem(e.isDir ? (FDirSizes.contains(u8(e.name)) ? u8(engine::formatSize(FDirSizes.value(u8(e.name)))) : QString())
                                            : u8(engine::formatSize(e.size)));
      sz->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row << sz;
      row << new QStandardItem(u8(e.modified));
      row << new QStandardItem(u8(e.rights));
      row << new QStandardItem(u8(e.owner));
      FModel->appendRow(row);
    }
    if (FModel->rowCount() > 0) FView->selectRow(0);
  }

  void sortByColumn(int col)
  {
    if (col == FSortCol) FSortAsc = !FSortAsc; else { FSortCol = col; FSortAsc = true; }
    sortEntries();
    renderEntries();
    if (onStatusChanged) onStatusChanged();
  }

  // Per-panel status: selection / totals (WinSCP shows selected of total).
  QString statusText() const
  {
    QStringList sel = selectedItems();
    if (!sel.isEmpty())
    {
      qint64 bytes = 0; for (const QString & n : sel) bytes += sizeOf(n);
      return QString("%1 of %2 selected  \xE2\x80\x94  %3")
        .arg(sel.size()).arg(FFiles + FDirs).arg(u8(engine::formatSize(bytes)));
    }
    return QString("%1 director%2, %3 file(s)").arg(FDirs).arg(FDirs == 1 ? "y" : "ies").arg(FFiles);
  }

  // Selected real files only (excludes dirs + "..").
  QStringList selectedFiles() const { return selected(false); }
  // Selected items including directories (excludes ".."); used by copy/move/delete which the
  // engine handles recursively for folders.
  QStringList selectedItems() const { return selected(true); }

  QStringList selected(bool includeDirs) const
  {
    QStringList out;
    if (!FView->selectionModel()) return out;
    const auto rows = FView->selectionModel()->selectedRows();
    for (const auto & ix : rows)
    {
      int r = ix.row();
      if (r < 0 || r >= (int)FEntries.size() || FEntries[r].isParent) continue;
      if (FEntries[r].isDir && !includeDirs) continue;
      out << u8(FEntries[r].name);
    }
    return out;
  }

  // Select (or deselect) all entries matching a wildcard mask (e.g. "*.txt") — WinSCP Ctrl+Num+/-.
  void selectByMask(const QString & mask, bool sel) {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return;
    QRegularExpression re(QRegularExpression::wildcardToRegularExpression(mask), QRegularExpression::CaseInsensitiveOption);
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent) continue;
      if (re.match(u8(FEntries[r].name)).hasMatch())
        sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, 4)),
                   sel ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
    }
    if (onStatusChanged) onStatusChanged();
  }
  void setStatusLine(const QString & s) { FStatus->setText(s); }
  // The listing as text (name, size column, selected mark) - for scripted UI checks.
  QStringList dump() const {
    QStringList out;
    QItemSelectionModel * sm = FView->selectionModel();
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent) continue;
      out << QString("%1 %2%3 | %4 | %5").arg(sm && sm->isRowSelected(r, QModelIndex()) ? "*" : " ")
               .arg(u8(FEntries[r].name), FEntries[r].isDir ? "/" : "")
               .arg(FModel->item(r, 1) ? FModel->item(r, 1)->text() : QString())
               .arg(u8(FEntries[r].modified));
    }
    return out;
  }
  // Native icon for an entry: folder icon for dirs, "up" for "..", file-type icon (by extension) for
  // files. Cached per extension so listing stays fast.
  QIcon iconFor(const engine::DirEntry & e) const {
    static QFileIconProvider prov;
    static QIcon folder = prov.icon(QFileIconProvider::Folder);
    static QIcon upIcon = qApp->style()->standardIcon(QStyle::SP_FileDialogToParent);
    static QIcon genericFile = prov.icon(QFileIconProvider::File);
    static QHash<QString, QIcon> byExt;
    if (e.isParent) return upIcon;
    if (e.isDir) return folder;
    QString n = u8(e.name); int dot = n.lastIndexOf('.');
    QString ext = (dot > 0) ? n.mid(dot + 1).toLower() : QString();
    if (ext.isEmpty()) return genericFile;
    auto it = byExt.find(ext);
    if (it != byExt.end()) return it.value();
    QIcon ic = prov.icon(QFileInfo("x." + ext));   // type icon by extension
    if (ic.isNull()) ic = genericFile;
    byExt.insert(ext, ic);
    return ic;
  }
  void refresh() { navigate(FPath); }
  // Norton-style: toggle selection of the current row, then move down one.
  void toggleSelectAndAdvance() {
    int row = FView->currentIndex().row();
    if (row < 0) return;
    QItemSelectionModel * sm = FView->selectionModel();
    QModelIndex ix = FModel->index(row, 0);
    sm->select(QItemSelection(FModel->index(row, 0), FModel->index(row, FModel->columnCount() - 1)),
               QItemSelectionModel::Toggle);
    int next = (row + 1 < FModel->rowCount()) ? row + 1 : row;
    FView->setCurrentIndex(FModel->index(next, 0));
  }
  void goUp() { navigate(u8(engine::parentDir(s8(FPath)))); if (onLeaveDir) onLeaveDir(); }

protected:
  bool eventFilter(QObject * obj, QEvent * ev) override
  {
    if (ev->type() == QEvent::FocusIn && onActivated) onActivated();
    if (ev->type() == QEvent::KeyPress)
    {
      auto * ke = static_cast<QKeyEvent *>(ev);
      if (ke->key() == Qt::Key_Backspace) { goUp(); return true; }
      if (ke->key() == Qt::Key_Tab) { if (onSwitchPanel) onSwitchPanel(); return true; }
      if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
      { activate(FView->currentIndex().row()); return true; }
    }
    // --- drag & drop between panels ---
    if (obj == FView->viewport())
    {
      static const char * kFmt = "application/x-winscp-files";
      if (ev->type() == QEvent::MouseButtonPress)
      { auto * me = static_cast<QMouseEvent *>(ev); if (me->button() == Qt::LeftButton) FDragStart = me->pos(); }
      else if (ev->type() == QEvent::MouseMove)
      {
        auto * me = static_cast<QMouseEvent *>(ev);
        if ((me->buttons() & Qt::LeftButton) &&
            (me->pos() - FDragStart).manhattanLength() >= QApplication::startDragDistance() &&
            !selectedItems().isEmpty())
        {
          s_dragSource = this;
          auto * mime = new QMimeData; mime->setData(kFmt, "1");   // inter-panel marker
          // Also expose the files as URLs so they can be dropped on Finder/Nautilus (drag OUT).
          // Local: real paths (free). Remote: download the selected files to a temp dir first.
          QList<QUrl> urls;
          QStringList selFiles = selectedFiles();   // files only (dirs not exported this way)
          if (!FRemote) {
            for (const QString & f : selFiles) urls << QUrl::fromLocalFile(FPath + "/" + f);
          } else if (!selFiles.isEmpty() && !gTransferRunning.load()) {
            QString tmp = QDir::tempPath() + "/freescp-dragout"; QDir().mkpath(tmp);
            QGuiApplication::setOverrideCursor(Qt::BusyCursor);
            for (const QString & f : selFiles) {
              std::string err;
              if (engine::downloadFromRemote(engine::joinPath(s8(FPath), s8(f)), s8(tmp), &err, true))
                urls << QUrl::fromLocalFile(tmp + "/" + f);
            }
            QGuiApplication::restoreOverrideCursor();
          }
          if (!urls.isEmpty()) mime->setUrls(urls);
          auto * drag = new QDrag(this); drag->setMimeData(mime);
          drag->exec(Qt::CopyAction);
          s_dragSource = nullptr;
          return true;
        }
      }
      else if (ev->type() == QEvent::DragEnter || ev->type() == QEvent::DragMove)
      {
        auto * de = static_cast<QDragMoveEvent *>(ev);
        bool internal = de->mimeData()->hasFormat(kFmt) && s_dragSource && s_dragSource != this;
        bool external = de->mimeData()->hasUrls();   // files from Finder/Nautilus
        if (internal || external) { de->acceptProposedAction(); return true; }
      }
      else if (ev->type() == QEvent::Drop)
      {
        auto * de = static_cast<QDropEvent *>(ev);
        if (de->mimeData()->hasFormat(kFmt) && s_dragSource && s_dragSource != this)
        { FilePanel * src = s_dragSource; if (onDropFiles) onDropFiles(src, src->selectedItems()); de->acceptProposedAction(); return true; }
        if (de->mimeData()->hasUrls())   // external (Finder) -> import the local files into this panel
        {
          QStringList paths;
          for (const QUrl & u : de->mimeData()->urls()) if (u.isLocalFile()) paths << u.toLocalFile();
          if (!paths.isEmpty() && onDropExternal) { onDropExternal(this, paths); de->acceptProposedAction(); return true; }
        }
      }
    }
    return false;
  }

private:
  void activate(int row)
  {
    if (row < 0 || row >= static_cast<int>(FEntries.size())) return;
    const auto & e = FEntries[static_cast<size_t>(row)];
    if (!e.isDir) { if (onFileOpen) onFileOpen(); return; }
    if (e.isParent) { goUp(); }
    else { QString nm = u8(e.name); navigate(u8(engine::joinPath(s8(FPath), e.name))); if (onEnterDir) onEnterDir(nm); }
  }

  QLabel * FHeader;
  QLineEdit * FPathEdit;
  QTableView * FView;
  QStandardItemModel * FModel;
  QLabel * FStatus;
  QTreeView * FTree = nullptr;
  QStandardItemModel * FTreeModel = nullptr;
  bool FTreePopulating = false;
  QString FPath;
  bool FRemote = false;
  bool FActive = false;
  bool FNav = false;
  int FDirs = 0, FFiles = 0;
  int FSortCol = 0; bool FSortAsc = true;
  QPoint FDragStart;
  QStringList FHistory;
  int FHistIdx = -1;
  std::vector<engine::DirEntry> FEntries;
  QHash<QString, qint64> FDirSizes;   // name -> calculated total size, for FListedPath
  QString FListedPath;
};
FilePanel * FilePanel::s_dragSource = nullptr;

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
  QCoreApplication::setOrganizationName(qgetenv("QT_QPA_PLATFORM") == "offscreen" ? "WinSCP-native-port-test"
                                                                                  : "WinSCP-native-port");
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

  // Main toolbar (native icons, WinSCP-style text-beside-icon).
  auto * tb = window.addToolBar("Main");
  tb->setMovable(false);
  tb->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  tb->setIconSize(QSize(16, 16));   // WinSCP-style small toolbar icons
  auto ic = [&](QStyle::StandardPixmap sp) { return window.style()->standardIcon(sp); };
  auto * actConnect = tb->addAction(ic(QStyle::SP_DialogOpenButton), "Login");
  tb->addSeparator();
  auto * actHome = tb->addAction(ic(QStyle::SP_DirHomeIcon), "Home");
  auto * actRefresh = tb->addAction(ic(QStyle::SP_BrowserReload), "Refresh");
  auto * actDisconnect = tb->addAction(ic(QStyle::SP_DialogCloseButton), "Disconnect"); actDisconnect->setEnabled(false);
  tb->addSeparator();
  auto * actSync = tb->addAction(ic(QStyle::SP_FileDialogContentsView), "Sync browsing"); actSync->setCheckable(true);
  auto * actTree = tb->addAction(ic(QStyle::SP_DirIcon), "Directory tree"); actTree->setCheckable(true);
  tb->addSeparator();
  auto * tbNewDir = tb->addAction(ic(QStyle::SP_FileDialogNewFolder), "New folder");
  auto * tbDelete = tb->addAction(ic(QStyle::SP_TrashIcon), "Delete");
  auto * tbProps  = tb->addAction(ic(QStyle::SP_FileDialogInfoView), "Properties");
  tb->addSeparator();
  auto * tbQueue  = tb->addAction(ic(QStyle::SP_FileDialogDetailedView), "Queue"); tbQueue->setCheckable(true);
  auto * tbLog    = tb->addAction(ic(QStyle::SP_FileDialogListView), "Log"); tbLog->setCheckable(true);
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
  auto * newTabBtn = new QToolButton; newTabBtn->setText("+"); newTabBtn->setToolTip("New session (Ctrl+N)");
  newTabBtn->setAutoRaise(true);
  tabRowLay->addWidget(tabBar); tabRowLay->addWidget(newTabBtn); tabRowLay->addStretch(1);   // tabs + "+" to the left
  tabRow->hide();
  centralLay->addWidget(tabRow);
  centralLay->addWidget(splitter, 1);

  // Bottom function-key bar (the iconic WinSCP/Norton-Commander row).
  auto * fkeys = new QWidget;
  auto * fkLay = new QHBoxLayout(fkeys);
  fkLay->setContentsMargins(3, 3, 3, 3);
  fkLay->setSpacing(3);
  central->setLayout(centralLay);
  centralLay->addWidget(fkeys);
  window.setCentralWidget(central);

  FilePanel * active = left;

  auto updateStatuses = [&] {
    left->setStatusLine(left->statusText());
    right->setStatusLine(right->statusText());
    window.statusBar()->showMessage(active->path());
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
        if (ok < total) appMessage(&window, "Transfer",
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
        if (ok < total) appMessage(&window, "Import",
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
    right->setRemote(QString("%1@%2 \xE2\x80\x94 %3").arg(u8(o.user), u8(o.host), protocolName(o.protocol)));
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

  // Select files by wildcard mask (Ctrl+Num+) / deselect (Ctrl+Num-).
  auto doSelectMask = [&](bool sel) {
    bool okIn = false;
    QString mask = QInputDialog::getText(&window, sel ? "Select files" : "Unselect files",
                                         "File mask (e.g. *.txt):", QLineEdit::Normal, "*", &okIn);
    if (okIn && !mask.isEmpty()) active->selectByMask(mask, sel);
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
  auto doProps = [&] {
    if (busy()) return;
    QStringList sel = active->selectedItems();
    if (sel.size() != 1) { window.statusBar()->showMessage("Select exactly one item"); return; }
    if (!active->isRemote()) { appMessage(&window, "Properties", "Properties (permissions) apply to remote files."); return; }
    QString name = sel.first();
    std::string cur = engine::remoteFileOctal(s8(name));
    const engine::DirEntry * e = active->entryNamed(name);
    QString info;
    if (e) {
      info = QString("Type: %1\nSize: %2\nOwner: %3\nModified: %4")
        .arg(e->isDir ? "Directory" : "File")
        .arg(e->isDir ? QString("—") : u8(engine::formatSize(e->size)))
        .arg(e->owner.empty() ? QString("—") : u8(e->owner))
        .arg(e->modified.empty() ? QString("—") : u8(e->modified));
    }
    bool recursive = false;
    QString oct = showPropertiesDialog(&window, name, cur.empty() ? "644" : u8(cur),
                                       info, e && e->isDir, &recursive);
    if (oct.isEmpty()) return;
    std::string err;
    bool ok = engine::remoteChmod(s8(name), s8(oct), recursive, &err);
    active->refresh();
    window.statusBar()->showMessage(ok ? (recursive ? "Set " + oct + " recursively" : "Set " + oct)
                                       : "chmod failed — " + u8(err));
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
  auto installContextMenu = [&](FilePanel * panel) {
    panel->view()->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(panel->view(), &QWidget::customContextMenuRequested, [&, panel](const QPoint & pos) {
      active = panel; panel->onActivated();
      QMenu menu;
      menu.addAction(active->isRemote() ? "Download (F5)" : "Upload / Copy (F5)", doCopy);
      menu.addAction("Rename (F2)", doRename);
      menu.addAction("Delete (F8)", doDelete);
      menu.addSeparator();
      menu.addAction("Create Directory (F7)", doMkdir);
      if (active->isRemote()) menu.addAction("Properties (F9)", doProps);
      menu.addSeparator();
      menu.addAction("Compare Files", [&]{ doCompareFiles(); });
      menu.addAction("Calculate Directory Sizes", doCalcSizes);
      menu.exec(panel->view()->viewport()->mapToGlobal(pos));
    });
  };
  installContextMenu(left);
  installContextMenu(right);

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
      "<tr><td><b>No&nbsp;telemetry</b></td><td>No tracking, no usage data, no account. FreeSCP talks to "
      "the servers you connect to; the one exception is a check for a newer version (it sends nothing "
      "about you, and Preferences can turn it off).</td></tr>"
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

  QAction * actPrefs = nullptr;
  // Menu bar (WinSCP layout; actions wired to the same ops as the toolbar/F-keys).
  {
    auto * mLocal = window.menuBar()->addMenu("&Local");
    mLocal->addAction("&Refresh", [&]{ left->refresh(); });
    mLocal->addAction("&Home", [&]{ left->navigate(u8(engine::homeDir())); });
    auto * mFiles = window.menuBar()->addMenu("&Files");
    mFiles->addAction("&Copy / Upload\tF5", doCopy);
    mFiles->addAction("&Rename\tF2", doRename);
    mFiles->addAction("&Delete\tF8", doDelete);
    mFiles->addAction("&Properties\tF9", doProps);
    mFiles->addSeparator();
    mFiles->addAction("Create &Directory\tF7", doMkdir);
    mFiles->addAction("Create &File\tShift+F4", doNewFile);
    mFiles->addSeparator();
    mFiles->addAction("&Select files\xE2\x80\xA6\tCtrl++", [&]{ doSelectMask(true); });
    mFiles->addAction("&Unselect files\xE2\x80\xA6\tCtrl+-", [&]{ doSelectMask(false); });
    mFiles->addAction("Copy &path to clipboard\tCtrl+Shift+C", doCopyPath);
    mFiles->addSeparator();
    mFiles->addAction("Co&mpare Files\tCtrl+Shift+D", [&]{ doCompareFiles(); });
    mFiles->addAction("Calculate Directory Si&zes\tShift+Alt+Enter", doCalcSizes);
    auto * mSession = window.menuBar()->addMenu("&Session");
    mSession->addAction("&Login\xE2\x80\xA6", doConnect);
    mSession->addAction("&Disconnect", doDisconnect);
    mSession->addSeparator();
    mSession->addAction("E&xit\tF10", doQuit);
    auto * mOptions = window.menuBar()->addMenu("&Options");
    mOptions->addAction("Session &log\tCtrl+L", [&]{ logDock->setVisible(!logDock->isVisible()); });
    mOptions->addAction("Open &diagnostics log file", [&]{
      QString p = u8(engine::logPath());
      window.statusBar()->showMessage("Diagnostics log: " + p);
      QProcess::startDetached("open", { "-R", p });   // reveal in Finder (macOS)
    });
    mOptions->addAction("Transfer &queue\tCtrl+Q", [&]{ queueDock->setVisible(!queueDock->isVisible()); });
    auto * actHidden = mOptions->addAction("Show &hidden files"); actHidden->setCheckable(true); actHidden->setChecked(gShowHidden);
    actHidden->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_H));
    QObject::connect(actHidden, &QAction::toggled, [&](bool on){ gShowHidden = on; left->refresh(); right->refresh();
      QSettings().setValue("prefs/showHidden", on); });
    mOptions->addAction("&Transfer settings\xE2\x80\xA6", [&]{ editTransferSettings(&window); });
    mOptions->addSeparator();
    actPrefs = mOptions->addAction("&Preferences\xE2\x80\xA6", [&]{
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
    });
    auto * mCommands = window.menuBar()->addMenu("&Commands");
    mCommands->addAction("Open &Terminal\tCtrl+T", doOpenTerminal);
    mCommands->addAction("&Synchronize\xE2\x80\xA6\tCtrl+S", doSync);
    mCommands->addAction("&Compare Directories\tShift+F2", doCompareDirs);
    mCommands->addAction("&Find Files\xE2\x80\xA6\tCtrl+Alt+F", [&]{ doFind(QString()); });
    { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_S);
      QObject::connect(a, &QAction::triggered, doSync); window.addAction(a); }
    auto * mRemote = window.menuBar()->addMenu("&Remote");
    mRemote->addAction("&Refresh", [&]{ if (right->isRemote()) right->refresh(); });

    // Bookmarks: persisted directory shortcuts; the menu is rebuilt each time it opens.
    auto * mBookmarks = window.menuBar()->addMenu("&Bookmarks");
    auto addBookmark = [&]{
      QSettings st; QStringList bm = st.value("bookmarks").toStringList();
      QString p = active->path();
      if (!bm.contains(p)) { bm << p; st.setValue("bookmarks", bm); st.sync(); }
      window.statusBar()->showMessage("Bookmarked " + p);
    };
    QObject::connect(mBookmarks, &QMenu::aboutToShow, [&, mBookmarks]{
      mBookmarks->clear();
      mBookmarks->addAction("&Add current directory\tCtrl+B", addBookmark);
      QStringList bm = QSettings().value("bookmarks").toStringList();
      if (!bm.isEmpty()) {
        mBookmarks->addSeparator();
        for (const QString & p : bm) { auto * a = mBookmarks->addAction(p); QObject::connect(a, &QAction::triggered, [&, p]{ active->navigate(p); }); }
        mBookmarks->addSeparator();
        mBookmarks->addAction("&Remove current directory", [&]{
          QSettings st; QStringList b = st.value("bookmarks").toStringList();
          b.removeAll(active->path()); st.setValue("bookmarks", b); st.sync();
        });
      }
    });
    { auto * a = new QAction(&window); a->setShortcut(Qt::CTRL | Qt::Key_B);
      QObject::connect(a, &QAction::triggered, addBookmark); window.addAction(a); }
    auto * mHelp = window.menuBar()->addMenu("&Help");
    mHelp->addAction("&Support FreeSCP (free, open source, no telemetry)\xE2\x80\xA6", [&]{ showWelcome(false); });
    mHelp->addAction("Check for &Updates\xE2\x80\xA6", [&]{ updates::check(&window, true); });
    mHelp->addAction("&About FreeSCP", showAbout);
  }

  //--- function-key bar buttons ------------------------------------------
  auto addFKey = [&](const QString & text, const std::function<void()> & fn) {
    auto * b = new QPushButton(text);
    b->setFocusPolicy(Qt::NoFocus);
    QObject::connect(b, &QPushButton::clicked, fn);
    fkLay->addWidget(b, 1);
    return b;
  };
  addFKey("F2 Rename", doRename);
  addFKey("F4 Edit", doEdit);
  addFKey("F5 Copy", doCopy);
  addFKey("F6 Move", doMove);
  addFKey("F7 Create Directory", doMkdir);
  addFKey("F8 Delete", doDelete);
  addFKey("F9 Properties", doProps);
  addFKey("F10 Quit", doQuit);

  //--- toolbar / shortcuts ------------------------------------------------
  QObject::connect(actConnect, &QAction::triggered, doConnect);
  QObject::connect(newTabBtn, &QToolButton::clicked, doConnect);   // "+" tab = open another session
  QObject::connect(tbNewDir, &QAction::triggered, doMkdir);
  QObject::connect(tbDelete, &QAction::triggered, doDelete);
  QObject::connect(tbProps,  &QAction::triggered, doProps);
  QObject::connect(tbQueue, &QAction::toggled, [&](bool v){ queueDock->setVisible(v); });
  QObject::connect(tbLog,   &QAction::toggled, [&](bool v){ logDock->setVisible(v); });
  QObject::connect(queueDock, &QDockWidget::visibilityChanged, [tbQueue](bool v){ tbQueue->setChecked(v); });
  QObject::connect(logDock,   &QDockWidget::visibilityChanged, [tbLog](bool v){ tbLog->setChecked(v); });
  QObject::connect(actDisconnect, &QAction::triggered, doDisconnect);
  QObject::connect(actHome, &QAction::triggered, [&] { if (!active->isRemote()) active->navigate(u8(engine::homeDir())); });
  QObject::connect(actRefresh, &QAction::triggered, [&] { active->refresh(); });

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

  window.statusBar()->showMessage(QString::fromUtf8(engine::banner().c_str()));
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
