//---------------------------------------------------------------------------
// filepanel.h — one file panel of the Commander, laid out like WinSCP's:
//
//   [ directory v ] [open] [filter] | [back] [forward] | [parent] [root] [home] [refresh] | [find] [tree]
//   [ Upload v ] [Edit] [X] [Properties] [New v] | [+] [-] [all]
//   /full/path/of/the/directory                      <- caption, highlighted in the active panel
//   Name | Size | Type | Changed                     (local)    Name | Size | Changed | Rights | Owner   (remote)
//   ...
//   0 B of 12.3 KiB in 0 of 7                                   3 hidden
//
// Local or remote, both through enginebridge. Header-only (no Q_OBJECT), included by main.cpp.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_FILEPANEL_H
#define WINSCP_UIQT_FILEPANEL_H

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QTableView>
#include <QTimeZone>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <atomic>
#include <functional>
#include <vector>

#include "enginebridge.h"
#include "uicommon.h"
#include "icons.h"
#include "panellogic.h"
#include "localops.h"

// Flat toolbar buttons (WinSCP's toolbars are flat; the platform styles frame every button).
inline const char * const kFlatButtons =
  "QToolButton { border:1px solid transparent; border-radius:3px; padding:2px 3px; background:transparent; }"
  "QToolButton:hover { background:palette(midlight); border-color:palette(mid); }"
  "QToolButton:pressed, QToolButton:checked { background:palette(mid); border-color:palette(dark); }"
  "QToolButton::menu-indicator { image:none; width:0px; }";

// The font of the file lists and other dense views: a notch smaller than the platform's default on
// macOS (13 pt there; WinSCP's lists are compact).
inline QFont denseFont(const QFont & base)
{
  QFont f = base;
#ifdef Q_OS_MACOS
  if (f.pointSizeF() > 12.0) f.setPointSizeF(12.0);
#endif
  return f;
}

inline bool gShowHidden = true;                         // WinSCP "Show hidden files" toggle (dotfiles)
inline std::atomic<bool> gTransferRunning{false};       // a background transfer batch is in flight

// A timestamp of a listing, the way WinSCP shows it: the system's short date (four-digit year) and
// time, the same in both panels. `engineSecs` = DirEntry::mtimeSecs; 0 = unknown -> empty.
inline QString formatPanelTime(std::int64_t engineSecs, bool hasSeconds)
{
  if (engineSecs <= 0) return QString();
  static const QString datePattern = u8(panellogic::fourDigitYear(s8(QLocale::system().dateFormat(QLocale::ShortFormat))));
  static const QString timeShort = QLocale::system().timeFormat(QLocale::ShortFormat);
  static const QString timeLong = u8(panellogic::withSeconds(s8(timeShort)));
  const QDateTime dt = QDateTime::fromSecsSinceEpoch(panellogic::wallSecondsSince1970(engineSecs), QTimeZone::utc());
  return QLocale::system().toString(dt.date(), datePattern) + " " + QLocale::system().toString(dt.time(), hasSeconds ? timeLong : timeShort);
}

// The caption above the list: shows the path; a click turns it into an edit box.
class PathCaption : public QStackedWidget
{
public:
  std::function<void(const QString &)> onNavigate;
  std::function<void()> onClicked;

  explicit PathCaption(QWidget * parent = nullptr) : QStackedWidget(parent)
  {
    FLabel = new QLabel; FLabel->setTextFormat(Qt::PlainText);
    FLabel->setToolTip("Click to type a path (Ctrl+O opens the Open Directory dialog)");
    FLabel->setCursor(Qt::IBeamCursor);
    FLabel->installEventFilter(this);
    FEdit = new QLineEdit; FEdit->setFrame(false);
    FEdit->installEventFilter(this);
    addWidget(FLabel); addWidget(FEdit);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    QObject::connect(FEdit, &QLineEdit::returnPressed, [this] {
      const QString p = FEdit->text().trimmed();
      setCurrentWidget(FLabel);
      if (!p.isEmpty() && onNavigate) onNavigate(p);
    });
    setActive(false);
  }
  void setPath(const QString & path) { FPath = path; FLabel->setText(path); if (currentWidget() == FLabel) FEdit->setText(path); }
  void setActive(bool a)
  {
    FLabel->setStyleSheet(QString("QLabel { padding:2px 5px; color:%1; background:%2; }")
                            .arg(a ? "palette(highlighted-text)" : "palette(window-text)", a ? "palette(highlight)" : "palette(mid)"));
  }
  void startEdit() { FEdit->setText(FPath); setCurrentWidget(FEdit); FEdit->setFocus(); FEdit->selectAll(); }
  bool editing() const { return currentWidget() == FEdit; }
  QSize sizeHint() const override { return QSize(100, FLabel->sizeHint().height()); }
  QSize minimumSizeHint() const override { return QSize(20, FLabel->sizeHint().height()); }

protected:
  bool eventFilter(QObject * obj, QEvent * ev) override
  {
    if (obj == FLabel && ev->type() == QEvent::MouseButtonRelease) { if (onClicked) onClicked(); startEdit(); return true; }
    if (obj == FEdit && ev->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(ev)->key() == Qt::Key_Escape)
    { setCurrentWidget(FLabel); return true; }
    if (obj == FEdit && ev->type() == QEvent::FocusOut) setCurrentWidget(FLabel);
    return false;
  }

private:
  QLabel * FLabel; QLineEdit * FEdit; QString FPath;
};

class FilePanel : public QWidget
{
public:
  enum Column { ColName, ColSize, ColType, ColChanged, ColRights, ColOwner };

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
  // A panel toolbar button: "transfer", "edit", "delete", "props", "newfile", "newdir", "newlink",
  // "selectmask", "unselectmask", "find", "filter", "opendir". The panel is active when it fires.
  std::function<void(const QString &)> onCommand;
  static inline FilePanel * s_dragSource = nullptr;  // panel a drag originated from

  // Mirror operations (used by synchronized browsing; navigate directly, no callbacks -> no loop).
  void enterSubdir(const QString & name)
  {
    for (const auto & e : FEntries)
      if (e.isDir && !e.isParent && u8(e.name) == name) { navigate(u8(engine::joinPath(s8(FPath), e.name))); return; }
  }
  void upOne() { navigate(u8(engine::parentDir(s8(FPath)))); }

  //--- directory tree (lazy, click-to-navigate) ---
  void setTreeVisible(bool v) { FTree->setVisible(v); FTreeBtn->setChecked(v); if (v && FTreeModel->rowCount() == 0) treeReload(); }
  bool treeVisible() const { return !FTree->isHidden(); }
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
    auto * it = new QStandardItem(folderIcon(), label);
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
    for (const auto & e : entries) if (e.isDir && !e.isParent && (gShowHidden || e.name.empty() || e.name[0] != '.')) dirs << u8(e.name);
    if (FRemote && path != FPath) engine::listRemoteDir(s8(FPath));  // restore session CWD + the panel's listing
    return dirs;
  }

  explicit FilePanel(QWidget * parent = nullptr) : QWidget(parent)
  {
    auto * layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto toolBtn = [this](const QString & icon, const QString & tip, const QString & text = QString()) {
      auto * b = new QToolButton;
      b->setIcon(appIcon(icon)); b->setToolTip(tip); b->setAutoRaise(true); b->setFocusPolicy(Qt::NoFocus);
      b->setIconSize(QSize(16, 16));
      if (!text.isEmpty()) { b->setText(text); b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); }
      return b;
    };
    auto sep = [] { auto * f = new QFrame; f->setFrameShape(QFrame::VLine); f->setFrameShadow(QFrame::Sunken); f->setFixedHeight(18); return f; };
    auto command = [this](QToolButton * b, const QString & cmd) {
      QObject::connect(b, &QToolButton::clicked, [this, cmd] { activateSelf(); if (onCommand) onCommand(cmd); });
    };

    // --- navigation bar: directory drop-down + the navigation buttons ---
    auto * nav = new QWidget;
    auto * navLay = new QHBoxLayout(nav);
    navLay->setContentsMargins(2, 2, 2, 1); navLay->setSpacing(1);
    nav->setStyleSheet(kFlatButtons);
    FDirCombo = new QComboBox;
    FDirCombo->setFocusPolicy(Qt::NoFocus);
    FDirCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    FDirCombo->setMinimumContentsLength(14);
    FDirCombo->setMaximumWidth(230);
    FDirCombo->setToolTip("Current directory: its parent directories and the directories visited recently");
    auto * openBtn = toolBtn("opendir", "Open directory / bookmark (Ctrl+O)");
    FFilterBtn = toolBtn("filter", "Filter: show only the files matching a mask"); FFilterBtn->setCheckable(true);
    FBackBtn = toolBtn("back", "Back (Alt+Left)");
    FFwdBtn = toolBtn("forward", "Forward (Alt+Right)");
    auto * upBtn = toolBtn("parent", "Parent directory (Backspace)");
    auto * rootBtn = toolBtn("root", "Root directory (Ctrl+\\)");
    auto * homeBtn = toolBtn("home", "Home directory");
    auto * rfBtn = toolBtn("refresh", "Refresh (Ctrl+R)");
    auto * findBtn = toolBtn("find", "Find files (Ctrl+Alt+F)");
    FTreeBtn = toolBtn("tree", "Directory tree"); FTreeBtn->setCheckable(true);
    navLay->addWidget(FDirCombo, 1); navLay->addWidget(openBtn); navLay->addWidget(FFilterBtn); navLay->addWidget(sep());
    navLay->addWidget(FBackBtn); navLay->addWidget(FFwdBtn); navLay->addWidget(sep());
    navLay->addWidget(upBtn); navLay->addWidget(rootBtn); navLay->addWidget(homeBtn); navLay->addWidget(rfBtn); navLay->addWidget(sep());
    navLay->addWidget(findBtn); navLay->addWidget(FTreeBtn); navLay->addStretch(1);

    // --- operations bar ---
    auto * ops = new QWidget;
    auto * opsLay = new QHBoxLayout(ops);
    opsLay->setContentsMargins(2, 0, 2, 2); opsLay->setSpacing(1);
    ops->setStyleSheet(kFlatButtons);
    FTransferBtn = toolBtn("upload", "Copy the selected files to the other panel (F5)", "Upload");
    auto * editBtn = toolBtn("edit", "Edit the selected file (F4)", "Edit");
    auto * delBtn = toolBtn("delete", "Delete the selected files (F8)");
    auto * propsBtn = toolBtn("properties", "Properties of the selected files (F9)", "Properties");
    auto * newBtn = toolBtn("newfile", "Create a file, a directory or a link", "New \xE2\x96\xBE");
    newBtn->setPopupMode(QToolButton::InstantPopup);
    auto * newMenu = new QMenu(newBtn);
    newMenu->addAction(appIcon("newfile"), "File\xE2\x80\xA6\tShift+F4", [this] { activateSelf(); if (onCommand) onCommand("newfile"); });
    newMenu->addAction(appIcon("newdir"), "Directory\xE2\x80\xA6\tF7", [this] { activateSelf(); if (onCommand) onCommand("newdir"); });
    FNewLinkAct = newMenu->addAction(appIcon("link"), "Link\xE2\x80\xA6\tCtrl+Alt+F6", [this] { activateSelf(); if (onCommand) onCommand("newlink"); });
    newBtn->setMenu(newMenu);
    auto * selBtn = toolBtn("select", "Select files by mask (Ctrl++)");
    auto * unselBtn = toolBtn("unselect", "Unselect files by mask (Ctrl+-)");
    auto * allBtn = toolBtn("selectall", "Select all (Ctrl+A)");
    opsLay->addWidget(FTransferBtn); opsLay->addWidget(editBtn); opsLay->addWidget(delBtn); opsLay->addWidget(propsBtn);
    opsLay->addWidget(newBtn); opsLay->addWidget(sep());
    opsLay->addWidget(selBtn); opsLay->addWidget(unselBtn); opsLay->addWidget(allBtn); opsLay->addStretch(1);

    // --- path caption ---
    FCaption = new PathCaption;
    FCaption->onNavigate = [this](const QString & p) { navigate(p); FView->setFocus(); };
    FCaption->onClicked = [this] { activateSelf(); };

    FView = new QTableView;
    FModel = new QStandardItemModel(this);
    FView->setModel(FModel);
    FView->setFont(denseFont(FView->font()));
    FView->setSelectionBehavior(QAbstractItemView::SelectRows);
    FView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    FView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FView->setSortingEnabled(false);
    FView->setShowGrid(false);
    FView->setWordWrap(false);
    FView->setAlternatingRowColors(true);
    FView->setFrameShape(QFrame::NoFrame);
    FView->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    FView->verticalHeader()->setVisible(false);
    FView->verticalHeader()->setMinimumSectionSize(1);
    FView->verticalHeader()->setDefaultSectionSize(qMax(18, FView->fontMetrics().height() + 4));   // compact rows, like WinSCP
    FView->setIconSize(QSize(16, 16));
    FView->installEventFilter(this);
    FView->viewport()->installEventFilter(this);     // drag start + drop target
    FView->viewport()->setAcceptDrops(true);

    // --- status line: "0 B of 12.3 KiB in 0 of 7" on the left, hidden/filtered counts on the right ---
    auto * status = new QWidget;
    auto * stLay = new QHBoxLayout(status);
    stLay->setContentsMargins(6, 2, 6, 2); stLay->setSpacing(10);
    FStatus = new QLabel; FStatusRight = new QLabel;
    FStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    stLay->addWidget(FStatus, 1); stLay->addWidget(FStatusRight);
    status->setStyleSheet("QWidget { border-top:1px solid palette(mid); } QLabel { border:none; }");

    // Optional directory tree (WinSCP-style), hidden by default; lazy-loaded, click navigates.
    FTree = new QTreeView; FTreeModel = new QStandardItemModel(this);
    FTree->setModel(FTreeModel); FTree->setHeaderHidden(true); FTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    FTree->setFrameShape(QFrame::NoFrame);
    FTree->setFont(denseFont(FTree->font()));
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

    layout->addWidget(nav);
    layout->addWidget(ops);
    layout->addWidget(FCaption);
    layout->addWidget(split, 1);
    layout->addWidget(status);

    QObject::connect(FView, &QTableView::doubleClicked,
                     [this](const QModelIndex & ix) { activate(ix.row()); });
    QObject::connect(upBtn, &QToolButton::clicked, [this] { activateSelf(); goUp(); });
    QObject::connect(rootBtn, &QToolButton::clicked, [this] { activateSelf(); navigate("/"); });
    QObject::connect(rfBtn, &QToolButton::clicked, [this] { activateSelf(); refresh(); });
    QObject::connect(FBackBtn, &QToolButton::clicked, [this] { activateSelf(); goBack(); });
    QObject::connect(FFwdBtn, &QToolButton::clicked, [this] { activateSelf(); goForward(); });
    QObject::connect(homeBtn, &QToolButton::clicked, [this] { activateSelf(); if (onHome) onHome(); });
    QObject::connect(FTreeBtn, &QToolButton::clicked, [this](bool on) { activateSelf(); setTreeVisible(on); });
    QObject::connect(allBtn, &QToolButton::clicked, [this] { activateSelf(); selectAll(); });
    // the filter button: asks for a mask; a click while a filter is on clears it
    QObject::connect(FFilterBtn, &QToolButton::clicked, [this] {
      activateSelf();
      FFilterBtn->setChecked(!FFilter.isEmpty());
      if (!FFilter.isEmpty()) setFilter(QString());
      else if (onCommand) onCommand("filter");
    });
    command(openBtn, "opendir"); command(findBtn, "find");
    command(FTransferBtn, "transfer"); command(editBtn, "edit"); command(delBtn, "delete"); command(propsBtn, "props");
    command(selBtn, "selectmask"); command(unselBtn, "unselectmask");
    QObject::connect(FDirCombo, QOverload<int>::of(&QComboBox::activated), [this](int i) {
      const QString p = FDirCombo->itemData(i).toString();
      activateSelf();
      if (!p.isEmpty() && p != FPath) navigate(p); else fillDirCombo();
    });
    QObject::connect(FView->selectionModel(), &QItemSelectionModel::selectionChanged,
                     [this] { updateStatus(); if (onStatusChanged) onStatusChanged(); });

    auto * hh = FView->horizontalHeader();
    hh->setSectionsClickable(true);
    hh->setSectionsMovable(false);
    hh->setStretchLastSection(true);                 // last column fills leftover space
    hh->setHighlightSections(false);
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);   // WinSCP: plain, left-aligned captions
    hh->setMinimumSectionSize(30);
    { QFont f = denseFont(hh->font()); f.setBold(false); hh->setFont(f); }
    hh->setFixedHeight(qMax(20, hh->fontMetrics().height() + 6));
    QObject::connect(hh, &QHeaderView::sectionClicked, [this](int col) { sortByColumn(col); });
    QObject::connect(hh, &QHeaderView::sectionResized, [this](int col, int, int w) {
      if (FConfiguring || col < 0 || col >= (int)FColumns.size() || col == (int)FColumns.size() - 1) return;
      QSettings().setValue(QString("panel/%1/width%2").arg(FRemote ? "remote" : "local").arg((int)FColumns[col]), w);
    });
    FRemote = true;      // force the first configuration
    setLocal();
    updateNavButtons();
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
    updateStatus();
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
    updateStatus();
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
  void setLocal()
  {
    const bool changed = FRemote;
    FRemote = false; FLabel.clear();
    FTransferBtn->setText("Upload"); FTransferBtn->setIcon(appIcon("upload"));
    FTransferBtn->setToolTip("Upload / copy the selected files to the other panel (F5)");
    if (changed) { configureColumns(); loadRecent(); }
  }
  void setRemote(const QString & label)
  {
    const bool changed = !FRemote || FLabel != label;
    FRemote = true; FLabel = label;
    FTransferBtn->setText("Download"); FTransferBtn->setIcon(appIcon("download"));
    FTransferBtn->setToolTip("Download the selected files to the other panel (F5)");
    if (changed) { configureColumns(); loadRecent(); }
  }
  // "user@host - SFTP" of a remote panel, empty for a local one.
  QString label() const { return FLabel; }
  // The other panel is remote: this (local) panel's transfer button uploads; otherwise it copies.
  void setTransferCaption(const QString & text, const QString & icon) { FTransferBtn->setText(text); FTransferBtn->setIcon(appIcon(icon)); }
  void setLinkEnabled(bool on) { FNewLinkAct->setEnabled(on); }

  void setActive(bool a) { FActive = a; FCaption->setActive(a); }
  bool isActive() const { return FActive; }
  void editPath() { FCaption->startEdit(); }

  void goBack()    { if (FHistIdx > 0) historyGo(FHistIdx - 1); }
  void goForward() { if (FHistIdx + 1 < FHistory.size()) historyGo(FHistIdx + 1); }
  // Move to another entry of the history; the position only changes if the directory really opened.
  void historyGo(int index)
  {
    const int was = FHistIdx; const QString target = FHistory[index];
    FHistIdx = index; FNav = true; navigate(target); FNav = false;
    if (FPath != target) FHistIdx = was;
    updateNavButtons();
  }

  void navigate(const QString & pathIn)
  {
    if (FRemote && gTransferRunning.load()) return;   // engine busy with a transfer batch
    const QString path = (pathIn.size() > 1 && pathIn.startsWith('/')) ? u8(panellogic::normalizePath(s8(pathIn))) : pathIn;
    if (!FNav)   // record history (truncate any forward entries)
    {
      while (FHistory.size() > FHistIdx + 1) FHistory.removeLast();
      if (FHistory.isEmpty() || FHistory.last() != path) { FHistory.append(path); FHistIdx = FHistory.size() - 1; }
    }
    std::string listError;
    std::vector<engine::DirEntry> entries = FRemote ? engine::listRemoteDir(s8(path), &listError) : engine::listLocalDir(s8(path));
    if (!FRemote && !QFileInfo(path).isDir()) listError = s8(QString("Directory \"%1\" does not exist or cannot be read.").arg(path));
    if (!listError.empty())
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
        std::vector<engine::DirEntry> back = FRemote ? engine::listRemoteDir(s8(prev), &again) : engine::listLocalDir(s8(prev));
        if (again.empty()) entries = back; else { entries.clear(); }
      }
      else if (FRemote) entries.clear();
      if (onListError) onListError(u8(listError));
      if (prev.isEmpty()) FPath = path;      // first listing of the session failed: nothing to go back to
    }
    else FPath = path;
    FCaption->setPath(FPath);
    if (FPath != FListedPath) FDirSizes.clear();   // calculated sizes belong to the directory they were made for
    FListedPath = FPath;
    FAll = entries;
    applyFilters();
    if (listError.empty()) addRecent(FPath);
    fillDirCombo();
    updateNavButtons();
    updateStatus();
    if (onStatusChanged) onStatusChanged();
  }

  //--- filter (file mask) ---
  QString filter() const { return FFilter; }
  // Show only the entries matching `mask` (empty = everything). Returns false + message for a bad mask.
  bool setFilter(const QString & mask, QString * error = nullptr)
  {
    std::string err;
    if (!mask.isEmpty() && !engine::validateFileMask(s8(mask), &err)) { if (error) *error = u8(err); FFilterBtn->setChecked(!FFilter.isEmpty()); return false; }
    FFilter = mask.trimmed();
    FFilterBtn->setChecked(!FFilter.isEmpty());
    FFilterBtn->setToolTip(FFilter.isEmpty() ? "Filter: show only the files matching a mask"
                                             : "Filter: " + FFilter + " (click to show all files)");
    applyFilters();
    updateStatus();
    if (onStatusChanged) onStatusChanged();
    return true;
  }
  int hiddenCount() const { return FHiddenCount; }
  int filteredCount() const { return FFilteredCount; }

  // Sort FEntries by the active column (".." always first), then dirs-first within the
  // chosen key (WinSCP groups folders), respecting ascending/descending.
  void sortEntries()
  {
    const Column key = (FSortCol >= 0 && FSortCol < (int)FColumns.size()) ? FColumns[FSortCol] : ColName;
    std::stable_sort(FEntries.begin(), FEntries.end(), [this, key](const engine::DirEntry & a, const engine::DirEntry & b) {
      if (a.isParent != b.isParent) return a.isParent > b.isParent;     // ".." first
      if (a.isDir != b.isDir) return a.isDir > b.isDir;                 // dirs before files
      int c = 0;
      switch (key)
      {
        case ColSize: { qint64 sa = a.isDir ? FDirSizes.value(u8(a.name), 0) : (qint64)a.size,   // Size (calculated, for directories)
                               sb = b.isDir ? FDirSizes.value(u8(b.name), 0) : (qint64)b.size;
                        c = (sa < sb) ? -1 : (sa > sb) ? 1 : 0; break; }
        case ColChanged: c = (a.mtimeSecs < b.mtimeSecs) ? -1 : (a.mtimeSecs > b.mtimeSecs) ? 1 : 0; break;
        case ColRights: c = QString::compare(u8(a.rights), u8(b.rights)); break;
        case ColOwner: c = QString::compare(u8(a.owner), u8(b.owner)); break;
        case ColType: c = QString::compare(typeOf(a), typeOf(b), Qt::CaseInsensitive); break;
        default: break;
      }
      if (c == 0) c = QString::compare(u8(a.name), u8(b.name), Qt::CaseInsensitive);   // Name, and the tie-break
      if (c == 0) c = QString::compare(u8(a.name), u8(b.name));
      return FSortAsc ? (c < 0) : (c > 0);
    });
  }

  void renderEntries()
  {
    // Remove only the rows (NOT clear()) so the columns, header sections and the user's column
    // widths survive a navigation. Sort arrow goes on the header text via setHeaderData.
    if (FModel->rowCount() > 0) FModel->removeRows(0, FModel->rowCount());
    for (int i = 0; i < (int)FColumns.size(); ++i) {
      QString h = columnTitle(FColumns[i]); if (i == FSortCol) h += FSortAsc ? "  \xE2\x96\xB4" : "  \xE2\x96\xBE";
      FModel->setHeaderData(i, Qt::Horizontal, h);
      FModel->setHeaderData(i, Qt::Horizontal, QVariant(int((FColumns[i] == ColSize ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter)), Qt::TextAlignmentRole);
    }
    for (const auto & e : FEntries)
    {
      QList<QStandardItem *> row;
      for (Column c : FColumns)
      {
        QStandardItem * it = nullptr;
        switch (c)
        {
          case ColName:
            it = new QStandardItem(u8(e.name)); it->setIcon(iconFor(e));
            if (e.isLink && !e.linkTarget.empty()) it->setToolTip(u8(e.name) + " \xE2\x86\x92 " + u8(e.linkTarget));
            break;
          case ColSize:
            it = new QStandardItem(e.isDir ? (FDirSizes.contains(u8(e.name)) ? u8(engine::formatSize(FDirSizes.value(u8(e.name)))) : QString())
                                           : u8(engine::formatSize(e.size)));
            it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            break;
          case ColType: it = new QStandardItem(typeOf(e)); break;
          case ColChanged: it = new QStandardItem(e.isParent ? QString() : formatPanelTime(e.mtimeSecs, e.mtimeHasSeconds)); break;
          case ColRights: it = new QStandardItem(u8(e.rights)); break;
          case ColOwner: it = new QStandardItem(u8(e.ownerName.empty() ? e.owner : e.ownerName)); break;
        }
        row << it;
      }
      FModel->appendRow(row);
    }
    if (FModel->rowCount() > 0) FView->selectRow(0);
  }

  void sortByColumn(int col)
  {
    if (col == FSortCol) FSortAsc = !FSortAsc; else { FSortCol = col; FSortAsc = true; }
    sortEntries();
    renderEntries();
    updateStatus();
    if (onStatusChanged) onStatusChanged();
  }

  // Per-panel status, as WinSCP words it: "<selected size> of <total size> in <selected> of <total>".
  QString statusText() const
  {
    qint64 total = 0, selBytes = 0; int count = 0;
    for (const auto & e : FEntries) { if (e.isParent) continue; ++count; total += e.isDir ? FDirSizes.value(u8(e.name), 0) : (qint64)e.size; }
    const QStringList sel = selectedItems();
    for (const QString & n : sel) selBytes += sizeOf(n);
    return QString("%1 of %2 in %3 of %4").arg(u8(engine::formatSize(selBytes)), u8(engine::formatSize(total))).arg(sel.size()).arg(count);
  }
  QString statusRightText() const
  {
    QStringList parts;
    if (!FFilter.isEmpty()) parts << QString("%1 filtered (%2)").arg(FFilteredCount).arg(FFilter);
    if (FHiddenCount > 0) parts << QString("%1 hidden").arg(FHiddenCount);
    return parts.join("   ");
  }
  void updateStatus() { FStatus->setText(statusText()); FStatusRight->setText(statusRightText()); }

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

  // Select (or deselect) the entries matching a WinSCP file mask (e.g. "*.txt; *.log") — Ctrl+Num+/-.
  // Directories are matched by name too when `applyToDirs` (the dialog's "Apply to directories").
  // An invalid mask selects nothing and returns false.
  bool selectByMask(const QString & mask, bool sel, bool applyToDirs = true) {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return false;
    std::vector<engine::DirEntry> asFiles = FEntries;     // a directory matches by its name, like a file
    for (auto & e : asFiles) e.isDir = false;
    std::vector<bool> match;
    if (!engine::matchFileMask(s8(mask), asFiles, match)) return false;
    for (int r = 0; r < (int)FEntries.size(); ++r) {
      if (FEntries[r].isParent || !match[(size_t)r]) continue;
      if (FEntries[r].isDir && !applyToDirs) continue;
      sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, FModel->columnCount() - 1)),
                 sel ? QItemSelectionModel::Select : QItemSelectionModel::Deselect);
    }
    updateStatus();
    if (onStatusChanged) onStatusChanged();
    return true;
  }
  void selectAll() {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return;
    for (int r = 0; r < (int)FEntries.size(); ++r)
      if (!FEntries[r].isParent)
        sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, FModel->columnCount() - 1)), QItemSelectionModel::Select);
  }
  void clearSelection() { if (FView->selectionModel()) FView->selectionModel()->clearSelection(); }
  void invertSelection() {
    QItemSelectionModel * sm = FView->selectionModel(); if (!sm) return;
    for (int r = 0; r < (int)FEntries.size(); ++r)
      if (!FEntries[r].isParent)
        sm->select(QItemSelection(FModel->index(r, 0), FModel->index(r, FModel->columnCount() - 1)), QItemSelectionModel::Toggle);
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
  // Column captions and every cell as shown (for scripted UI checks of the layout).
  QStringList dumpColumns() const {
    QStringList out;
    QStringList heads; for (Column c : FColumns) heads << columnTitle(c);
    out << heads.join(" | ");
    for (int r = 0; r < FModel->rowCount(); ++r) {
      QStringList cells; for (int c = 0; c < FModel->columnCount(); ++c) cells << (FModel->item(r, c) ? FModel->item(r, c)->text() : QString());
      out << cells.join(" | ");
    }
    return out;
  }
  QStringList recentDirs() const { return FRecent; }
  QStringList dirComboItems() const { QStringList out; for (int i = 0; i < FDirCombo->count(); ++i) out << FDirCombo->itemData(i).toString(); return out; }

  static QIcon folderIcon() {
    static QIcon ic = [] { QIcon i = QFileIconProvider().icon(QFileIconProvider::Folder); return usable(i) ? i : appIcon("folder"); }();
    return ic;
  }
  static bool usable(const QIcon & ic) { return !ic.isNull() && !ic.pixmap(16, 16).isNull(); }
  // Icon for an entry: folder for dirs, folder-with-arrow for "..", file-type icon (by extension) for
  // files, with a link mark for symbolic links. Cached per extension so listing stays fast. The
  // platform's icons are used where it has them (some Linux themes have none: own icons then).
  QIcon iconFor(const engine::DirEntry & e) const {
    static QFileIconProvider prov;
    static QIcon upIcon = appIcon("folderup");
    static QIcon genericFile = [] { QIcon i = prov.icon(QFileIconProvider::File); return usable(i) ? i : appIcon("file"); }();
    static QHash<QString, QIcon> byExt;
    static QIcon folderLink = linkOverlayIcon(folderIcon()), fileLink = linkOverlayIcon(genericFile);
    if (e.isParent) return upIcon;
    if (e.isDir) return e.isLink ? folderLink : folderIcon();
    if (e.isLink) return fileLink;
    QString n = u8(e.name); int dot = n.lastIndexOf('.');
    QString ext = (dot > 0) ? n.mid(dot + 1).toLower() : QString();
    if (ext.isEmpty()) return genericFile;
    auto it = byExt.find(ext);
    if (it != byExt.end()) return it.value();
    QIcon ic = prov.icon(QFileInfo("x." + ext));   // type icon by extension
    if (!usable(ic)) ic = genericFile;
    byExt.insert(ext, ic);
    return ic;
  }
  // The "Type" column: what kind of file this is, from its name.
  static QString typeOf(const engine::DirEntry & e) {
    if (e.isParent) return "Parent directory";
    if (e.isLink) return e.isDir ? "Link to folder" : "Symbolic link";
    if (e.isDir) return "File folder";
    static QMimeDatabase db;
    static QHash<QString, QString> byExt;
    const QString n = u8(e.name); const int dot = n.lastIndexOf('.');
    const QString ext = (dot > 0) ? n.mid(dot + 1).toLower() : QString();
    if (ext.isEmpty()) return "File";
    auto it = byExt.find(ext);
    if (it != byExt.end()) return it.value();
    const QMimeType mt = db.mimeTypeForFile("x." + ext, QMimeDatabase::MatchExtension);
    QString t = (mt.isValid() && !mt.isDefault()) ? mt.comment() : QString();
    // no description on this system (the comment is then just the MIME name): "TXT File", as Windows
    // shows a type it has no name for
    if (t.isEmpty() || t.contains('/')) t = (ext == "txt") ? QString("Text Document") : ext.toUpper() + " File";
    else t[0] = t[0].toUpper();
    byExt.insert(ext, t);
    return t;
  }
  void refresh() { FNav = true; navigate(FPath); FNav = false; }
  // Norton-style: toggle selection of the current row, then move down one.
  void toggleSelectAndAdvance() {
    int row = FView->currentIndex().row();
    if (row < 0) return;
    QItemSelectionModel * sm = FView->selectionModel();
    sm->select(QItemSelection(FModel->index(row, 0), FModel->index(row, FModel->columnCount() - 1)),
               QItemSelectionModel::Toggle);
    int next = (row + 1 < FModel->rowCount()) ? row + 1 : row;
    sm->setCurrentIndex(FModel->index(next, 0), QItemSelectionModel::NoUpdate);
    FView->scrollTo(FModel->index(next, 0));
  }
  // (the other panel follows - synchronized browsing - only if this one really moved)
  void goUp() { if (FPath == "/") return; const QString before = FPath; navigate(u8(engine::parentDir(s8(FPath)))); if (FPath != before && onLeaveDir) onLeaveDir(); }

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
      if (ke->matches(QKeySequence::SelectAll)) { selectAll(); return true; }   // never the ".." row
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
            QString tmp = privateTempDir("dragout");
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
  void activateSelf() { if (onActivated) onActivated(); }

  void activate(int row)
  {
    if (row < 0 || row >= static_cast<int>(FEntries.size())) return;
    const auto & e = FEntries[static_cast<size_t>(row)];
    if (!e.isDir) { if (onFileOpen) onFileOpen(); return; }
    if (e.isParent) { goUp(); }
    else { QString nm = u8(e.name); const QString before = FPath; navigate(u8(engine::joinPath(s8(FPath), e.name))); if (FPath != before && onEnterDir) onEnterDir(nm); }
  }

  static QString columnTitle(Column c)
  {
    switch (c) { case ColName: return "Name"; case ColSize: return "Size"; case ColType: return "Type";
                 case ColChanged: return "Changed"; case ColRights: return "Rights"; case ColOwner: return "Owner"; }
    return QString();
  }
  static int defaultWidth(Column c)
  {
    switch (c) { case ColName: return 190; case ColSize: return 72; case ColType: return 118;
                 case ColChanged: return 132; case ColRights: return 80; case ColOwner: return 70; }
    return 80;
  }
  // WinSCP's default columns: local = Name, Size, Type, Changed; remote = Name, Size, Changed, Rights, Owner.
  void configureColumns()
  {
    FConfiguring = true;
    FColumns = FRemote ? std::vector<Column>{ ColName, ColSize, ColChanged, ColRights, ColOwner }
                       : std::vector<Column>{ ColName, ColSize, ColType, ColChanged };
    FModel->removeRows(0, FModel->rowCount());
    FModel->setColumnCount((int)FColumns.size());
    if (FSortCol >= (int)FColumns.size()) { FSortCol = 0; FSortAsc = true; }
    QSettings s;
    for (int i = 0; i < (int)FColumns.size(); ++i)
    {
      FModel->setHeaderData(i, Qt::Horizontal, columnTitle(FColumns[i]));
      FView->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Interactive);
      const int w = s.value(QString("panel/%1/width%2").arg(FRemote ? "remote" : "local").arg((int)FColumns[i]), defaultWidth(FColumns[i])).toInt();
      FView->setColumnWidth(i, qBound(30, w, 1200));
    }
    FConfiguring = false;
  }

  // FAll -> FEntries: drop hidden files (when not shown) and the entries the filter excludes.
  void applyFilters()
  {
    FEntries.clear(); FHiddenCount = 0; FFilteredCount = 0;
    std::vector<bool> match;
    engine::matchFileMask(s8(FFilter), FAll, match);
    for (size_t i = 0; i < FAll.size(); ++i)
    {
      const engine::DirEntry & e = FAll[i];
      if (!e.isParent && !e.name.empty() && e.name[0] == '.' && !gShowHidden) { ++FHiddenCount; continue; }
      if (!e.isParent && !match[i]) { ++FFilteredCount; continue; }
      FEntries.push_back(e);
    }
    FDirs = 0; FFiles = 0;
    for (const auto & e : FEntries) { if (e.isParent) continue; if (e.isDir) ++FDirs; else ++FFiles; }
    sortEntries();
    renderEntries();
  }

  //--- recent directories (per local side / per remote session label; persisted) ---
  QString recentKey() const { return FRemote ? "recent/remote/" + QString(FLabel).replace('/', '_') : QString("recent/local"); }
  void loadRecent() { FRecent = QSettings().value(recentKey()).toStringList(); }
  void addRecent(const QString & p)
  {
    std::vector<std::string> v; for (const QString & r : FRecent) v.push_back(s8(r));
    panellogic::pushRecent(v, s8(p), 15);
    QStringList out; for (const std::string & r : v) out << u8(r);
    if (out == FRecent) return;
    FRecent = out;
    QSettings().setValue(recentKey(), FRecent);
  }
  // The drop-down: the path from the root down to the current directory, then the recent directories.
  void fillDirCombo()
  {
    FDirCombo->blockSignals(true);
    FDirCombo->clear();
    const std::vector<panellogic::PathStep> steps = panellogic::pathHierarchy(s8(FPath));
    for (size_t i = 0; i < steps.size(); ++i)
      FDirCombo->addItem(i + 1 == steps.size() ? appIcon("opendir") : folderIcon(), QString(int(i) * 2, ' ') + u8(steps[i].label), u8(steps[i].path));
    const int current = FDirCombo->count() - 1;
    bool first = true;
    for (const QString & r : FRecent)
    {
      if (r == FPath) continue;
      if (first) { FDirCombo->insertSeparator(FDirCombo->count()); first = false; }
      FDirCombo->addItem(appIcon("back"), r, r);
    }
    FDirCombo->setCurrentIndex(current);
    FDirCombo->blockSignals(false);
  }
  void updateNavButtons() { FBackBtn->setEnabled(FHistIdx > 0); FFwdBtn->setEnabled(FHistIdx + 1 < FHistory.size()); }

  PathCaption * FCaption;
  QComboBox * FDirCombo;
  QToolButton * FBackBtn, * FFwdBtn, * FFilterBtn, * FTreeBtn, * FTransferBtn;
  QAction * FNewLinkAct = nullptr;
  QTableView * FView;
  QStandardItemModel * FModel;
  QLabel * FStatus, * FStatusRight;
  QTreeView * FTree = nullptr;
  QStandardItemModel * FTreeModel = nullptr;
  bool FTreePopulating = false;
  QString FPath, FLabel, FFilter;
  bool FRemote = false;
  bool FActive = false;
  bool FNav = false;
  bool FConfiguring = false;
  int FDirs = 0, FFiles = 0, FHiddenCount = 0, FFilteredCount = 0;
  int FSortCol = 0; bool FSortAsc = true;
  QPoint FDragStart;
  QStringList FHistory, FRecent;
  int FHistIdx = -1;
  std::vector<Column> FColumns;
  std::vector<engine::DirEntry> FAll;       // the directory as listed
  std::vector<engine::DirEntry> FEntries;   // what the panel shows (after hidden files + filter), sorted
  QHash<QString, qint64> FDirSizes;   // name -> calculated total size, for FListedPath
  QString FListedPath;
};

#endif
