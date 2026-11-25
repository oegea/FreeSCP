//---------------------------------------------------------------------------
// icons.h — FreeSCP's own toolbar/panel icon set.
//
// Small flat icons drawn with QPainter (no image files, nothing taken from WinSCP), so the toolbars
// look the same on macOS and on every Linux desktop - the platform's standard icons differ from
// theme to theme and some themes lack half of them. Each icon is resolution independent.
//---------------------------------------------------------------------------
#ifndef WINSCP_UIQT_ICONS_H
#define WINSCP_UIQT_ICONS_H

#include <QIcon>
#include <QString>
#include <QStringList>

// An icon by name ("back", "upload", "delete", ...); a null icon for an unknown name.
QIcon appIcon(const QString & name);
// Every name appIcon() knows (for the icon sheet used to review them, and the tests).
QStringList appIconNames();
// `base` with a small "link" arrow in the lower-left corner (symbolic links in the file panels).
QIcon linkOverlayIcon(const QIcon & base);

#endif
