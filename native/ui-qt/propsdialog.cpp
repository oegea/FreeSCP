//---------------------------------------------------------------------------
// propsdialog.cpp — see propsdialog.h.
//---------------------------------------------------------------------------
#include "propsdialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>

#include "uicommon.h"
#include "icons.h"
#include "panellogic.h"

namespace {

QLabel * valueLabel(const QString & text)
{
  auto * l = new QLabel(text);
  l->setTextInteractionFlags(Qt::TextSelectableByMouse);
  l->setWordWrap(true);
  return l;
}

QFrame * hline()
{
  auto * f = new QFrame; f->setFrameShape(QFrame::HLine); f->setFrameShadow(QFrame::Sunken);
  return f;
}

QDialogButtonBox * okCancel(QDialog & d, const QString & okText = QString())
{
  auto * bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  if (!okText.isEmpty()) bb->button(QDialogButtonBox::Ok)->setText(okText);
  bb->button(QDialogButtonBox::Ok)->setObjectName("dialogOk");
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  return bb;
}

} // namespace

//===========================================================================
// Properties
//===========================================================================
PropsResult showPropertiesDialog(QWidget * parent, const PropsRequest & rq)
{
  PropsResult res;
  QDialog d(parent);
  const bool single = rq.names.size() == 1;
  d.setWindowTitle(single ? rq.names.first() + " Properties" : QString("%1 files Properties").arg(rq.names.size()));

  auto * outer = new QVBoxLayout(&d);
  auto * tabs = new QTabWidget; outer->addWidget(tabs);
  auto * page = new QWidget; tabs->addTab(page, "Common");
  auto * lay = new QVBoxLayout(page);

  // --- what it is ---
  auto * head = new QHBoxLayout;
  auto * icon = new QLabel; icon->setPixmap(appIcon(rq.anyDirectory && single ? "folder" : single ? "file" : "copy").pixmap(32, 32));
  head->addWidget(icon);
  auto * title = valueLabel(single ? rq.names.first() : QString("%1 files").arg(rq.names.size()));
  { QFont f = title->font(); f.setBold(true); title->setFont(f); }
  head->addWidget(title, 1);
  lay->addLayout(head);
  lay->addWidget(hline());

  auto * info = new QFormLayout;
  info->setLabelAlignment(Qt::AlignLeft);
  auto oneLine = [](const QString & t) { QLabel * l = valueLabel(t); l->setWordWrap(false); return l; };
  info->addRow("Location:", valueLabel(rq.location));
  if (!rq.sizeText.isEmpty()) info->addRow("Size:", oneLine(rq.sizeText));
  if (!rq.linkTarget.isEmpty()) info->addRow("Link to:", valueLabel(rq.linkTarget));
  if (!rq.modified.isEmpty()) info->addRow("Changed:", oneLine(rq.modified));
  d.setMinimumWidth(400);
  lay->addLayout(info);
  lay->addWidget(hline());

  // --- owner / group ---
  auto * og = new QFormLayout;
  auto * ownerEdit = new QLineEdit(rq.owner); ownerEdit->setObjectName("owner"); ownerEdit->setEnabled(rq.canChangeOwner);
  auto * groupEdit = new QLineEdit(rq.group); groupEdit->setObjectName("group"); groupEdit->setEnabled(rq.canChangeGroup);
  ownerEdit->setToolTip("A user name, or a numeric user id"); groupEdit->setToolTip("A group name, or a numeric group id");
  og->addRow("&Owner:", ownerEdit);
  og->addRow("&Group:", groupEdit);
  if (rq.remote || !rq.owner.isEmpty()) lay->addLayout(og); else { ownerEdit->hide(); groupEdit->hide(); }

  // --- permissions grid (Owner/Group/Others x R/W/X) + octal, kept in sync ---
  auto * grp = new QGroupBox("Permissions");
  auto * g = new QGridLayout(grp);
  const char * rowNames[3] = { "Owner", "Group", "Others" };
  const char * colNames[3] = { "&R", "&W", "&X" };
  QCheckBox * cb[9];
  for (int row = 0; row < 3; ++row)
  {
    g->addWidget(new QLabel(rowNames[row]), row, 0);
    for (int col = 0; col < 3; ++col)
    {
      cb[row * 3 + col] = new QCheckBox(QString(colNames[col]).remove(row == 0 ? "" : "&"));
      g->addWidget(cb[row * 3 + col], row, col + 1);
    }
  }
  const char * specialNames[3] = { "Set UID", "Set GID", "Sticky bit" };
  QCheckBox * special[3];
  for (int i = 0; i < 3; ++i) { special[i] = new QCheckBox(specialNames[i]); g->addWidget(special[i], i, 4); }
  g->setColumnMinimumWidth(4, 90);
  g->addWidget(new QLabel("O&ctal:"), 3, 0);
  auto * octEdit = new QLineEdit; octEdit->setObjectName("octal"); octEdit->setMaxLength(4); octEdit->setMaximumWidth(70);
  if (auto * buddy = qobject_cast<QLabel *>(g->itemAtPosition(3, 0)->widget())) buddy->setBuddy(octEdit);
  g->addWidget(octEdit, 3, 1, 1, 2);
  auto * cbAddX = new QCheckBox("Add &X to directories"); cbAddX->setObjectName("addx");
  cbAddX->setToolTip("Directories also get the X permission wherever R is set");
  g->addWidget(cbAddX, 4, 0, 1, 5);
  grp->setEnabled(rq.canChangeRights);
  lay->addWidget(grp);

  const int initial = (rq.rights >= 0) ? rq.rights : 0644;
  bool guard = false;
  auto valueFromBoxes = [&]() -> int {
    int v = 0;
    for (int i = 0; i < 9; ++i) if (cb[i]->isChecked()) v |= (1 << (8 - i));
    if (special[0]->isChecked()) v |= 04000;
    if (special[1]->isChecked()) v |= 02000;
    if (special[2]->isChecked()) v |= 01000;
    return v;
  };
  auto setBoxes = [&](int v) {
    for (int i = 0; i < 9; ++i) cb[i]->setChecked((v >> (8 - i)) & 1);
    special[0]->setChecked(v & 04000); special[1]->setChecked(v & 02000); special[2]->setChecked(v & 01000);
  };
  auto syncOctalFromBoxes = [&] {
    if (guard) return;
    guard = true; octEdit->setText(u8(panellogic::octalString(valueFromBoxes()))); guard = false;
  };
  auto syncBoxesFromOctal = [&] {
    if (guard) return;
    if (!panellogic::validOctal(s8(octEdit->text()))) return;   // while typing: leave the boxes alone
    guard = true; setBoxes(octEdit->text().toInt(nullptr, 8)); guard = false;
  };
  for (int i = 0; i < 9; ++i) QObject::connect(cb[i], &QCheckBox::toggled, &d, syncOctalFromBoxes);
  for (int i = 0; i < 3; ++i) QObject::connect(special[i], &QCheckBox::toggled, &d, syncOctalFromBoxes);
  QObject::connect(octEdit, &QLineEdit::textChanged, &d, syncBoxesFromOctal);
  guard = true; setBoxes(initial); guard = false;
  syncOctalFromBoxes();
  const QString initialOctal = octEdit->text();

  auto * cbRec = new QCheckBox("Set owner, group and permissions &recursively"); cbRec->setObjectName("recursive");
  cbRec->setVisible(rq.anyDirectory && rq.remote);
  cbAddX->setVisible(rq.anyDirectory && rq.remote);
  lay->addWidget(cbRec);
  lay->addStretch(1);

  auto * bb = okCancel(d);
  outer->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    if (!panellogic::validOctal(s8(octEdit->text())))
    { appMessage(&d, "Properties", "The permissions must be three or four octal digits, for example 644 or 0755."); octEdit->setFocus(); return; }
    if ((ownerEdit->isEnabled() && ownerEdit->text().trimmed().isEmpty() && !rq.owner.isEmpty()) ||
        (groupEdit->isEnabled() && groupEdit->text().trimmed().isEmpty() && !rq.group.isEmpty()))
    { appMessage(&d, "Properties", "The owner and the group cannot be empty."); return; }
    d.accept();
  });

  if (execDialog(d, "properties") != QDialog::Accepted) return res;
  res.accepted = true;
  res.octal = octEdit->text();
  res.recursive = cbRec->isVisibleTo(&d) ? cbRec->isChecked() : false;
  res.addXToDirs = cbAddX->isChecked() && rq.anyDirectory;
  // Several files, or a recursive change: the permissions shown are only the first file's, so an
  // untouched grid must not be applied to everything. They are sent when edited or when asked for
  // recursively with "add X".
  res.rightsChanged = rq.canChangeRights && (res.octal != initialOctal || (res.recursive && res.addXToDirs));
  res.owner = ownerEdit->text().trimmed(); res.group = groupEdit->text().trimmed();
  res.ownerChanged = rq.canChangeOwner && !res.owner.isEmpty() && res.owner != rq.owner;
  res.groupChanged = rq.canChangeGroup && !res.group.isEmpty() && res.group != rq.group;
  // "recursively" with nothing edited re-applies what is shown, to the whole tree (WinSCP does the same)
  // - for one entry only: with several, the grid shows just the first one's permissions
  if (res.recursive && !res.anything() && rq.canChangeRights && single) res.rightsChanged = true;
  return res;
}

//===========================================================================
// Create Link
//===========================================================================
bool showLinkDialog(QWidget * parent, QString & name, QString & target, bool & symbolic, bool canSymbolic, bool canHard)
{
  QDialog d(parent); d.setWindowTitle("Add Link");
  auto * lay = new QVBoxLayout(&d);
  auto * form = new QFormLayout;
  auto * nameEdit = new QLineEdit(name); nameEdit->setObjectName("name"); nameEdit->setMinimumWidth(340);
  auto * targetEdit = new QLineEdit(target); targetEdit->setObjectName("target");
  form->addRow("&Link file:", nameEdit);
  form->addRow("&Point link to:", targetEdit);
  lay->addLayout(form);
  auto * cbHard = new QCheckBox("&Hard link"); cbHard->setObjectName("hard");
  cbHard->setChecked(!symbolic || !canSymbolic);
  cbHard->setEnabled(canSymbolic && canHard);
  lay->addWidget(cbHard);
  auto * bb = okCancel(d);
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    if (nameEdit->text().trimmed().isEmpty() || targetEdit->text().trimmed().isEmpty())
    { appMessage(&d, "Add Link", "Enter the name of the link and what it points to."); return; }
    if (nameEdit->text().contains('/')) { appMessage(&d, "Add Link", "The link name cannot contain \"/\": it is created in the current directory."); return; }
    d.accept();
  });
  nameEdit->setFocus();
  if (execDialog(d, "link") != QDialog::Accepted) return false;
  name = nameEdit->text().trimmed(); target = targetEdit->text().trimmed(); symbolic = !cbHard->isChecked();
  return true;
}

//===========================================================================
// Duplicate / Move (server side)
//===========================================================================
bool showRemoteCopyDialog(QWidget * parent, bool move, const QStringList & names, QString & targetDir, QString & newName)
{
  QDialog d(parent); d.setWindowTitle(move ? "Move" : "Duplicate");
  auto * lay = new QVBoxLayout(&d);
  const QString what = names.size() == 1 ? QString("file \"%1\"").arg(names.first()) : QString("%1 files").arg(names.size());
  lay->addWidget(new QLabel(QString("%1 %2 to remote directory:").arg(move ? "Move" : "Duplicate", what)));
  auto * dirEdit = new QLineEdit(targetDir); dirEdit->setObjectName("target"); dirEdit->setMinimumWidth(400);
  lay->addWidget(dirEdit);
  auto * nameEdit = new QLineEdit(newName); nameEdit->setObjectName("name");
  auto * nameLabel = new QLabel("&New name:"); nameLabel->setBuddy(nameEdit);
  lay->addWidget(nameLabel); lay->addWidget(nameEdit);
  nameLabel->setVisible(names.size() == 1); nameEdit->setVisible(names.size() == 1);
  auto * hint = new QLabel(move ? "The files are moved on the server."
                                : "The files are copied on the server; nothing is downloaded.");
  hint->setStyleSheet("color: gray;");
  lay->addWidget(hint);
  auto * bb = okCancel(d, move ? "Move" : "Duplicate");
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    if (dirEdit->text().trimmed().isEmpty()) { appMessage(&d, d.windowTitle(), "Enter the target directory."); return; }
    if (nameEdit->isVisibleTo(&d) && (nameEdit->text().trimmed().isEmpty() || nameEdit->text().contains('/')))
    { appMessage(&d, d.windowTitle(), "Enter a file name (without \"/\")."); return; }
    d.accept();
  });
  (names.size() == 1 ? nameEdit : dirEdit)->setFocus();
  if (execDialog(d, move ? "moveto" : "duplicate") != QDialog::Accepted) return false;
  targetDir = dirEdit->text().trimmed();
  newName = names.size() == 1 ? nameEdit->text().trimmed() : QString();
  return true;
}

//===========================================================================
// Select / Filter
//===========================================================================
namespace {
const char * const kMaskHint =
  "Examples:  *.txt     *.jpg; *.png     * | *.bak   (all but)     *>1M   (larger than 1 MB)";

bool maskDialog(QWidget * parent, const QString & title, const QString & prompt, const QString & shotName, bool allowEmpty,
                QString & mask, bool * applyToDirs)
{
  QDialog d(parent); d.setWindowTitle(title);
  auto * lay = new QVBoxLayout(&d);
  auto * label = new QLabel(prompt);
  auto * edit = new QLineEdit(mask); edit->setObjectName("mask"); edit->setMinimumWidth(360);
  label->setBuddy(edit);
  lay->addWidget(label); lay->addWidget(edit);
  auto * hint = new QLabel(kMaskHint); hint->setStyleSheet("color: gray;");
  lay->addWidget(hint);
  QCheckBox * cbDirs = nullptr;
  if (applyToDirs != nullptr)
  {
    cbDirs = new QCheckBox("Apply to &directories"); cbDirs->setObjectName("dirs"); cbDirs->setChecked(*applyToDirs);
    lay->addWidget(cbDirs);
  }
  auto * bb = okCancel(d);
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] {
    const QString m = edit->text().trimmed();
    std::string err;
    if (m.isEmpty() && !allowEmpty) { appMessage(&d, title, "Enter a file mask."); return; }
    if (!m.isEmpty() && !engine::validateFileMask(s8(m), &err))
    { appMessage(&d, title, "The file mask is not valid:\n" + u8(err)); edit->setFocus(); return; }
    d.accept();
  });
  edit->setFocus(); edit->selectAll();
  if (execDialog(d, shotName) != QDialog::Accepted) return false;
  mask = edit->text().trimmed();
  if (cbDirs != nullptr) *applyToDirs = cbDirs->isChecked();
  return true;
}
} // namespace

bool showSelectDialog(QWidget * parent, bool select, QString & mask, bool & applyToDirectories)
{
  return maskDialog(parent, select ? "Select files" : "Unselect files", "File &mask:", select ? "select" : "unselect", false, mask, &applyToDirectories);
}

bool showFilterDialog(QWidget * parent, QString & mask)
{
  return maskDialog(parent, "Filter", "Show only the files matching this &mask (empty shows all):", "filter", true, mask, nullptr);
}

//===========================================================================
// Open Directory + bookmarks
//===========================================================================
QStringList bookmarkList() { return QSettings().value("bookmarks").toStringList(); }
void setBookmarkList(const QStringList & list) { QSettings s; s.setValue("bookmarks", list); s.sync(); }

bool showOpenDirectoryDialog(QWidget * parent, const QString & current, const QStringList & recent, QString & chosen)
{
  QDialog d(parent); d.setWindowTitle("Open directory"); d.resize(480, 380);
  auto * lay = new QVBoxLayout(&d);
  auto * label = new QLabel("&Open directory:");
  auto * edit = new QLineEdit(current); edit->setObjectName("path"); label->setBuddy(edit);
  lay->addWidget(label); lay->addWidget(edit);
  auto * tabs = new QTabWidget; lay->addWidget(tabs, 1);

  auto * bmPage = new QWidget; auto * bmLay = new QHBoxLayout(bmPage);
  auto * list = new QListWidget; list->setObjectName("bookmarks");
  list->addItems(bookmarkList());
  bmLay->addWidget(list, 1);
  auto * btns = new QVBoxLayout;
  auto * bAdd = new QPushButton("&Add"); bAdd->setToolTip("Bookmark the path typed above");
  auto * bRemove = new QPushButton("&Remove");
  auto * bUp = new QPushButton("&Up"); auto * bDown = new QPushButton("&Down");
  btns->addWidget(bAdd); btns->addWidget(bRemove); btns->addWidget(bUp); btns->addWidget(bDown); btns->addStretch(1);
  bmLay->addLayout(btns);
  tabs->addTab(bmPage, "Bookmarks");

  auto * recentList = new QListWidget; recentList->addItems(recent);
  tabs->addTab(recentList, "Recent directories");

  auto save = [&] { QStringList b; for (int i = 0; i < list->count(); ++i) b << list->item(i)->text(); setBookmarkList(b); };
  QObject::connect(bAdd, &QPushButton::clicked, &d, [&] {
    const QString p = edit->text().trimmed();
    if (p.isEmpty()) return;
    if (list->findItems(p, Qt::MatchExactly).isEmpty()) { list->addItem(p); save(); }
    list->setCurrentRow(list->count() - 1);
  });
  QObject::connect(bRemove, &QPushButton::clicked, &d, [&] { delete list->takeItem(list->currentRow()); save(); });
  auto moveRow = [&](int delta) {
    const int r = list->currentRow(), to = r + delta;
    if (r < 0 || to < 0 || to >= list->count()) return;
    list->insertItem(to, list->takeItem(r)); list->setCurrentRow(to); save();
  };
  QObject::connect(bUp, &QPushButton::clicked, &d, [&] { moveRow(-1); });
  QObject::connect(bDown, &QPushButton::clicked, &d, [&] { moveRow(1); });
  for (QListWidget * l : { list, recentList })
  {
    QObject::connect(l, &QListWidget::currentTextChanged, &d, [edit](const QString & t) { if (!t.isEmpty()) edit->setText(t); });
    QObject::connect(l, &QListWidget::itemDoubleClicked, &d, [&d, edit](QListWidgetItem * it) { edit->setText(it->text()); d.accept(); });
  }

  auto * bb = okCancel(d, "Open");
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::accepted, &d, [&] { if (!edit->text().trimmed().isEmpty()) d.accept(); });
  edit->setFocus(); edit->selectAll();
  if (execDialog(d, "opendir") != QDialog::Accepted) return false;
  chosen = edit->text().trimmed();
  return !chosen.isEmpty();
}

//===========================================================================
// Server and Protocol Information
//===========================================================================
QString sessionInfoText(const engine::SessionInformation & info)
{
  QStringList out;
  for (const auto & r : info.session) out << u8(r.label) + ": " + u8(r.value);
  out << QString();
  for (const auto & r : info.capabilities) out << u8(r.label) + ": " + u8(r.value);
  if (!info.additional.empty()) { out << QString(); out << u8(info.additional); }
  return out.join("\n");
}

void showSessionInfoDialog(QWidget * parent, const engine::SessionInformation & info, const QString & path,
                           const std::function<bool(const QString &, engine::SpaceInfo &, QString *)> & space)
{
  QDialog d(parent); d.setWindowTitle("Server and Protocol Information"); d.resize(560, 460);
  auto * lay = new QVBoxLayout(&d);
  auto * tabs = new QTabWidget; lay->addWidget(tabs, 1);

  auto makeTable = [](const std::vector<engine::InfoRow> & rows) {
    auto * t = new QTreeWidget; t->setRootIsDecorated(false); t->setHeaderLabels({ "Item", "Value" });
    t->setAlternatingRowColors(true); t->setUniformRowHeights(true);
    for (const auto & r : rows) new QTreeWidgetItem(t, QStringList{ u8(r.label), u8(r.value) });
    t->header()->setStretchLastSection(true);
    t->setColumnWidth(0, 250);
    return t;
  };
  auto * protoPage = new QWidget; auto * protoLay = new QVBoxLayout(protoPage);
  protoLay->addWidget(makeTable(info.session), 1);
  if (!info.additional.empty())
  {
    protoLay->addWidget(new QLabel("Additional information:"));
    auto * extra = new QPlainTextEdit(u8(info.additional)); extra->setReadOnly(true); extra->setFixedHeight(120);
    protoLay->addWidget(extra);
  }
  tabs->addTab(protoPage, "Protocol");
  tabs->addTab(makeTable(info.capabilities), "Capabilities");

  auto * spacePage = new QWidget; auto * spLay = new QVBoxLayout(spacePage);
  auto * pathRow = new QHBoxLayout;
  auto * pathEdit = new QLineEdit(path); pathEdit->setObjectName("spacepath");
  auto * check = new QPushButton("&Check space");
  pathRow->addWidget(new QLabel("&Path:")); pathRow->addWidget(pathEdit, 1); pathRow->addWidget(check);
  spLay->addLayout(pathRow);
  auto * spaceTable = new QTreeWidget; spaceTable->setRootIsDecorated(false); spaceTable->setHeaderLabels({ "Item", "Value" });
  spaceTable->setColumnWidth(0, 250);
  spLay->addWidget(spaceTable, 1);
  auto fill = [&] {
    spaceTable->clear();
    engine::SpaceInfo si; QString err;
    if (!space || !space(pathEdit->text().trimmed(), si, &err))
    { new QTreeWidgetItem(spaceTable, QStringList{ "Not available", err }); return; }
    auto row = [&](const QString & label, std::int64_t v) {
      new QTreeWidgetItem(spaceTable, QStringList{ label, v > 0 ? u8(engine::formatSize(v)) + QString("  (%L1 bytes)").arg((qlonglong)v) : QString("Unknown") });
    };
    row("Bytes on device", si.total); row("Free bytes on device", si.unused);
    row("Total bytes for user", si.totalForUser); row("Free bytes for user", si.unusedForUser);
  };
  QObject::connect(check, &QPushButton::clicked, &d, fill);
  QObject::connect(pathEdit, &QLineEdit::returnPressed, &d, fill);
  int spaceTab = tabs->addTab(spacePage, "Space available");
  QObject::connect(tabs, &QTabWidget::currentChanged, &d, [&, spaceTab](int i) { if (i == spaceTab && spaceTable->topLevelItemCount() == 0) fill(); });

  auto * bb = new QDialogButtonBox(QDialogButtonBox::Close);
  auto * copy = bb->addButton("Copy to Clip&board", QDialogButtonBox::ActionRole);
  lay->addWidget(bb);
  QObject::connect(bb, &QDialogButtonBox::rejected, &d, &QDialog::reject);
  QObject::connect(copy, &QPushButton::clicked, &d, [&] { QApplication::clipboard()->setText(sessionInfoText(info)); });
  { bool okIdx = false; int forced = qEnvironmentVariable("WINSCP_INFO_TAB").toInt(&okIdx);   // dev: render a given page
    if (okIdx && forced >= 0 && forced < tabs->count()) tabs->setCurrentIndex(forced); }
  execDialog(d, "sessioninfo");
}
