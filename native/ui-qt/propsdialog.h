//---------------------------------------------------------------------------
// propsdialog.h — the small dialogs of the file panels, modelled on WinSCP's: Properties
// (permissions, owner, group), Create Link, Duplicate / Move (server side), Select / Filter by mask,
// Open Directory (with bookmarks) and Server and Protocol Information.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_PROPSDIALOG_H
#define WINSCP_UIQT_PROPSDIALOG_H

#include <QString>
#include <QStringList>
#include <functional>
#include "enginebridge.h"

class QWidget;

struct PropsRequest
{
  QStringList names;            // the selected entries (one or more)
  QString location;             // directory they are in
  bool remote = true;
  bool anyDirectory = false;    // offers "recursively" / "add X to directories"
  QString sizeText;             // "1.2 KiB", or "3 files" style text
  QString linkTarget;           // single symbolic link: what it points to
  QString modified;
  QString owner, group;         // of the first entry; empty when unknown
  int rights = -1;              // of the first entry (octal value), -1 when unknown
  bool canChangeRights = true, canChangeOwner = false, canChangeGroup = false;
};
struct PropsResult
{
  bool accepted = false;
  bool rightsChanged = false; QString octal; bool addXToDirs = false;
  bool ownerChanged = false;  QString owner;
  bool groupChanged = false;  QString group;
  bool recursive = false;
  bool anything() const { return rightsChanged || ownerChanged || groupChanged; }
};
PropsResult showPropertiesDialog(QWidget * parent, const PropsRequest & request);

// Create Link. `name` is pre-filled; returns false if cancelled.
bool showLinkDialog(QWidget * parent, QString & name, QString & target, bool & symbolic, bool canSymbolic, bool canHard);

// Duplicate / Move on the server: target directory and (for a single entry) the new name.
bool showRemoteCopyDialog(QWidget * parent, bool move, const QStringList & names, QString & targetDir, QString & newName);

// Select / Unselect files by mask.
bool showSelectDialog(QWidget * parent, bool select, QString & mask, bool & applyToDirectories);
// The panel filter mask (empty = show everything).
bool showFilterDialog(QWidget * parent, QString & mask);

// Open Directory: a path to type plus the bookmarks (QSettings "bookmarks"); returns the chosen path.
QStringList bookmarkList();
void setBookmarkList(const QStringList & list);
bool showOpenDirectoryDialog(QWidget * parent, const QString & current, const QStringList & recent, QString & chosen);

// Server and Protocol Information. `space(path, info, error)` is called for the "Space available" page.
void showSessionInfoDialog(QWidget * parent, const engine::SessionInformation & info, const QString & path,
                           const std::function<bool(const QString &, engine::SpaceInfo &, QString *)> & space);
// The same information as plain text (Copy to Clipboard, and the scripted checks).
QString sessionInfoText(const engine::SessionInformation & info);

#endif
